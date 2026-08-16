#include "stripe_api.h"
#include "stripe_key.h"
#include "stripe_parse.h"
#include "events.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_errors.h"
#include "esp_heap_caps.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "stripe";

#define STRIPE_HOST "api.stripe.com"
#define VALIDATE_URL "https://" STRIPE_HOST "/v1/subscriptions?limit=1"

/* The validation response is tiny (one subscription). A full subscriptions
 * page is 200-400KB and comes later, in Stage 5, where PSRAM makes a
 * full-buffer parse viable (spec 8.3 assumed far less heap). */
#define VALIDATE_BUF_LEN (16 * 1024)

/* A full subscriptions page. Spec 7.1 sizes this at 200-400KB, and spec 8.3
 * assumed it could not be buffered -- but that assumed ~320KB of heap. This
 * board has 8MB PSRAM, so a full-buffer parse is comfortable. */
#define SUBS_BUF_LEN (512 * 1024)
#define SUBS_URL "https://" STRIPE_HOST \
                 "/v1/subscriptions?status=all&limit=100&expand[]=data.discount"

/* Stripe is not slow, but a phone-tethered or congested network can be. */
#define HTTP_TIMEOUT_MS 15000

static char s_key[STRIPE_KEY_MAX_LEN + 1] = "";

/* Accumulates the response body across esp_http_client's chunk events. */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
    bool overflowed;
} response_t;

const char *stripe_result_str(stripe_result_t r)
{
    switch (r) {
    case STRIPE_OK:               return "OK";
    case STRIPE_ERR_NETWORK:      return "Cannot reach Stripe";
    case STRIPE_ERR_TLS:          return "Secure connection failed";
    case STRIPE_ERR_UNAUTHORIZED: return "Stripe key rejected";
    case STRIPE_ERR_RATE_LIMITED: return "Rate limited by Stripe";
    case STRIPE_ERR_SERVER:       return "Stripe server error";
    case STRIPE_ERR_BAD_RESPONSE: return "Unexpected response from Stripe";
    case STRIPE_ERR_NO_MEMORY:    return "Out of memory";
    }
    return "Unknown error";
}

void stripe_set_key(const char *key)
{
    if (key == NULL) {
        s_key[0] = '\0';
        return;
    }

    strncpy(s_key, key, sizeof(s_key) - 1);
    s_key[sizeof(s_key) - 1] = '\0';

    char redacted[64];
    stripe_key_redact(s_key, redacted, sizeof(redacted));
    ESP_LOGI(TAG, "key set: %s (%s mode)", redacted,
             stripe_key_is_test_mode(s_key) ? "test" : "live");
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    response_t *r = (response_t *)evt->user_data;

    if (evt->event_id != HTTP_EVENT_ON_DATA || r == NULL) {
        return ESP_OK;
    }

    /* Truncate rather than overflow. A truncated body fails to parse, which
     * surfaces as BAD_RESPONSE -- an honest error, unlike a smashed heap. */
    if (r->len + evt->data_len >= r->cap) {
        /* Log once, not once per chunk: a truncating response fires this
         * callback hundreds of times and floods the console. */
        if (!r->overflowed) {
            r->overflowed = true;
            ESP_LOGW(TAG, "response exceeded %u bytes, truncating",
                     (unsigned)r->cap);
        }
        return ESP_OK;
    }

    memcpy(r->buf + r->len, evt->data, evt->data_len);
    r->len += evt->data_len;
    r->buf[r->len] = '\0';
    return ESP_OK;
}

static stripe_result_t classify_status(int status)
{
    if (status >= 200 && status < 300) {
        return STRIPE_OK;
    }
    if (status == 401 || status == 403) {
        /* Spec 7.4 step 5 and 6.2 State B: this must be visible, not retried
         * silently. A revoked key never recovers on its own. */
        return STRIPE_ERR_UNAUTHORIZED;
    }
    if (status == 429) {
        return STRIPE_ERR_RATE_LIMITED;
    }
    if (status >= 500) {
        return STRIPE_ERR_SERVER;
    }
    return STRIPE_ERR_BAD_RESPONSE;
}

stripe_validation_t stripe_validate_key(void)
{
    stripe_validation_t out = {
        .result = STRIPE_ERR_NETWORK,
        .http_status = 0,
        .has_subscriptions = false,
        .test_mode = stripe_key_is_test_mode(s_key),
    };

    if (s_key[0] == '\0') {
        out.result = STRIPE_ERR_UNAUTHORIZED;
        return out;
    }

    response_t resp = {
        .buf = malloc(VALIDATE_BUF_LEN),
        .len = 0,
        .cap = VALIDATE_BUF_LEN,
    };
    if (resp.buf == NULL) {
        out.result = STRIPE_ERR_NO_MEMORY;
        return out;
    }
    resp.buf[0] = '\0';

    const esp_http_client_config_t cfg = {
        .url = VALIDATE_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &resp,
        /* Espressif's bundled Mozilla roots; see the note in stripe_api.h. */
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        free(resp.buf);
        out.result = STRIPE_ERR_NO_MEMORY;
        return out;
    }

    char auth[STRIPE_KEY_MAX_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);
    esp_http_client_set_header(client, "Authorization", auth);

    const esp_err_t err = esp_http_client_perform(client);

    if (err != ESP_OK) {
        /* Distinguish a TLS failure from plain unreachability: they call for
         * different customer advice (clock/cert vs network). */
        out.result = (err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
                      err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
                         ? STRIPE_ERR_TLS
                         : STRIPE_ERR_NETWORK;
        ESP_LOGE(TAG, "request failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(resp.buf);
        return out;
    }

    out.http_status = esp_http_client_get_status_code(client);
    out.result = classify_status(out.http_status);

    ESP_LOGI(TAG, "validate: HTTP %d, %u bytes", out.http_status,
             (unsigned)resp.len);

    if (out.result == STRIPE_OK) {
        /* Confirm it is really a Stripe list response, not a captive portal or
         * proxy page that happened to return 200. */
        cJSON *root = cJSON_Parse(resp.buf);
        if (root == NULL) {
            out.result = STRIPE_ERR_BAD_RESPONSE;
        } else {
            const cJSON *data = cJSON_GetObjectItem(root, "data");
            if (!cJSON_IsArray(data)) {
                out.result = STRIPE_ERR_BAD_RESPONSE;
            } else {
                out.has_subscriptions = cJSON_GetArraySize(data) > 0;
            }
            cJSON_Delete(root);
        }
    }

    esp_http_client_cleanup(client);
    free(resp.buf);
    return out;
}

stripe_result_t stripe_fetch_totals(mrr_totals_t *out, bool *truncated)
{
    if (out == NULL) {
        return STRIPE_ERR_BAD_RESPONSE;
    }
    memset(out, 0, sizeof(*out));
    if (truncated) {
        *truncated = false;
    }

    if (s_key[0] == '\0') {
        return STRIPE_ERR_UNAUTHORIZED;
    }

    /* PSRAM: this is far too large for internal RAM, and is exactly the
     * allocation spec 8.3 assumed was impossible on an ESP32. */
    response_t resp = {
        .buf = heap_caps_malloc(SUBS_BUF_LEN, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = SUBS_BUF_LEN,
    };
    if (resp.buf == NULL) {
        ESP_LOGE(TAG, "could not allocate %d bytes for the response", SUBS_BUF_LEN);
        return STRIPE_ERR_NO_MEMORY;
    }
    resp.buf[0] = '\0';

    const esp_http_client_config_t cfg = {
        .url = SUBS_URL,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        heap_caps_free(resp.buf);
        return STRIPE_ERR_NO_MEMORY;
    }

    char auth[STRIPE_KEY_MAX_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);
    esp_http_client_set_header(client, "Authorization", auth);

    const esp_err_t err = esp_http_client_perform(client);
    stripe_result_t result;

    if (err != ESP_OK) {
        result = (err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
                  err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
                     ? STRIPE_ERR_TLS : STRIPE_ERR_NETWORK;
        ESP_LOGE(TAG, "fetch failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        heap_caps_free(resp.buf);
        return result;
    }

    const int status = esp_http_client_get_status_code(client);
    result = classify_status(status);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "subscriptions: HTTP %d, %u bytes", status, (unsigned)resp.len);

    if (result != STRIPE_OK) {
        heap_caps_free(resp.buf);
        return result;
    }

    /* The parsed form is large; keep it in PSRAM too rather than on the
     * caller's stack. */
    stripe_subs_t *parsed = heap_caps_malloc(sizeof(stripe_subs_t),
                                             MALLOC_CAP_SPIRAM);
    if (parsed == NULL) {
        heap_caps_free(resp.buf);
        return STRIPE_ERR_NO_MEMORY;
    }

    if (!stripe_parse_subscriptions(resp.buf, parsed)) {
        heap_caps_free(parsed);
        heap_caps_free(resp.buf);
        return STRIPE_ERR_BAD_RESPONSE;
    }

    *out = mrr_compute(parsed->subs, parsed->sub_count);

    if (truncated) {
        *truncated = parsed->truncated || parsed->has_more;
    }

    ESP_LOGI(TAG, "MRR %lld cents, %d active, %d trials%s%s",
             (long long)out->mrr_cents, out->active_count, out->trial_count,
             out->has_tiered ? " (tiered present)" : "",
             out->mixed_currency ? " (MIXED CURRENCY)" : "");

    heap_caps_free(parsed);
    heap_caps_free(resp.buf);
    return STRIPE_OK;
}

/*
 * Events are NOT small. Each one embeds a full object snapshot, so 100
 * subscription events ran past 128KB on a real account and the truncated body
 * failed to parse -- which left the heartbeat blank with no obvious cause.
 *
 * 25 events is plenty: the daily counts come from today's activity and the
 * heartbeat only needs the most recent one. Fewer events also means a much
 * smaller response to pull every 60 seconds.
 */
#define EVENTS_BUF_LEN (256 * 1024)
#define MAX_EVENTS 25

stripe_result_t stripe_fetch_events(int64_t since_utc, event_totals_t *out)
{
    return stripe_fetch_events_since(since_utc, since_utc, out);
}

stripe_result_t stripe_fetch_events_since(int64_t fetch_since,
                                          int64_t day_start_utc,
                                          event_totals_t *out)
{
    if (out == NULL) {
        return STRIPE_ERR_BAD_RESPONSE;
    }
    memset(out, 0, sizeof(*out));

    if (s_key[0] == '\0') {
        return STRIPE_ERR_UNAUTHORIZED;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://" STRIPE_HOST "/v1/events?limit=%d&created[gte]=%lld",
             MAX_EVENTS, (long long)fetch_since);

    response_t resp = {
        .buf = heap_caps_malloc(EVENTS_BUF_LEN, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = EVENTS_BUF_LEN,
    };
    if (resp.buf == NULL) {
        return STRIPE_ERR_NO_MEMORY;
    }
    resp.buf[0] = '\0';

    const esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        heap_caps_free(resp.buf);
        return STRIPE_ERR_NO_MEMORY;
    }

    char auth[STRIPE_KEY_MAX_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);
    esp_http_client_set_header(client, "Authorization", auth);

    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        heap_caps_free(resp.buf);
        return (err == ESP_ERR_ESP_TLS_FAILED_CONNECT_TO_HOST ||
                err == ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED)
                   ? STRIPE_ERR_TLS : STRIPE_ERR_NETWORK;
    }

    const int status = esp_http_client_get_status_code(client);
    stripe_result_t result = classify_status(status);
    esp_http_client_cleanup(client);

    if (result != STRIPE_OK) {
        heap_caps_free(resp.buf);
        return result;
    }

    cJSON *root = cJSON_Parse(resp.buf);
    heap_caps_free(resp.buf);

    if (root == NULL) {
        return STRIPE_ERR_BAD_RESPONSE;
    }

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return STRIPE_ERR_BAD_RESPONSE;
    }

    static stripe_event_t parsed[MAX_EVENTS];
    int n = 0;

    const cJSON *ev = NULL;
    cJSON_ArrayForEach(ev, data) {
        if (n >= MAX_EVENTS) {
            break;
        }

        const cJSON *type = cJSON_GetObjectItemCaseSensitive(ev, "type");
        const cJSON *created = cJSON_GetObjectItemCaseSensitive(ev, "created");

        parsed[n].kind = event_kind_from_type(
            cJSON_IsString(type) ? type->valuestring : NULL);
        parsed[n].created = cJSON_IsNumber(created)
                                ? (int64_t)created->valuedouble : 0;
        parsed[n].amount_cents = 0;

        /* Invoice amounts live at data.object.amount_paid. */
        if (parsed[n].kind == EVENT_INVOICE_PAID) {
            const cJSON *d = cJSON_GetObjectItemCaseSensitive(ev, "data");
            const cJSON *obj = cJSON_IsObject(d)
                ? cJSON_GetObjectItemCaseSensitive(d, "object") : NULL;
            const cJSON *amt = cJSON_IsObject(obj)
                ? cJSON_GetObjectItemCaseSensitive(obj, "amount_paid") : NULL;
            if (cJSON_IsNumber(amt)) {
                parsed[n].amount_cents = (int64_t)amt->valuedouble;
            }
        }

        n++;
    }

    cJSON_Delete(root);

    *out = events_summarize_window(parsed, n, day_start_utc, fetch_since);

    ESP_LOGI(TAG, "events: HTTP %d, %d parsed, %d new paid today, %d cancelled/30d",
             status, n, out->new_paid, out->churned_30d);


    return STRIPE_OK;
}

/*
 * Open invoices are ones Stripe has issued but not collected. Past their due
 * date with retries exhausted, they become uncollectible; both states are
 * revenue at risk.
 */
#define INVOICES_BUF_LEN (128 * 1024)

stripe_result_t stripe_fetch_failed_payments(int *out_count, int64_t *out_cents)
{
    if (out_count) *out_count = 0;
    if (out_cents) *out_cents = 0;

    if (s_key[0] == '\0') {
        return STRIPE_ERR_UNAUTHORIZED;
    }

    response_t resp = {
        .buf = heap_caps_malloc(INVOICES_BUF_LEN, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = INVOICES_BUF_LEN,
    };
    if (resp.buf == NULL) {
        return STRIPE_ERR_NO_MEMORY;
    }
    resp.buf[0] = '\0';

    const esp_http_client_config_t cfg = {
        .url = "https://" STRIPE_HOST "/v1/invoices?status=open&limit=100",
        .method = HTTP_METHOD_GET,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .event_handler = on_http_event,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        heap_caps_free(resp.buf);
        return STRIPE_ERR_NO_MEMORY;
    }

    char auth[STRIPE_KEY_MAX_LEN + 16];
    snprintf(auth, sizeof(auth), "Bearer %s", s_key);
    esp_http_client_set_header(client, "Authorization", auth);

    const esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        heap_caps_free(resp.buf);
        return STRIPE_ERR_NETWORK;
    }

    const int status = esp_http_client_get_status_code(client);
    stripe_result_t result = classify_status(status);
    esp_http_client_cleanup(client);

    if (result != STRIPE_OK) {
        /* A 401/403 here almost certainly means the key lacks Invoices: Read
         * rather than that the whole key is bad -- the subscriptions call is
         * working. Log it plainly so the cause is obvious. */
        if (result == STRIPE_ERR_UNAUTHORIZED) {
            ESP_LOGW(TAG, "invoices: HTTP %d -- key likely lacks Invoices: Read",
                     status);
        }
        heap_caps_free(resp.buf);
        return result;
    }

    cJSON *root = cJSON_Parse(resp.buf);
    heap_caps_free(resp.buf);

    if (root == NULL) {
        return STRIPE_ERR_BAD_RESPONSE;
    }

    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (!cJSON_IsArray(data)) {
        cJSON_Delete(root);
        return STRIPE_ERR_BAD_RESPONSE;
    }

    int count = 0;
    int64_t cents = 0;

    const cJSON *inv = NULL;
    cJSON_ArrayForEach(inv, data) {
        /* amount_due is what remains uncollected; amount_paid may be partial. */
        const cJSON *due = cJSON_GetObjectItemCaseSensitive(inv, "amount_due");
        const cJSON *paid = cJSON_GetObjectItemCaseSensitive(inv, "amount_paid");

        const int64_t d = cJSON_IsNumber(due) ? (int64_t)due->valuedouble : 0;
        const int64_t p = cJSON_IsNumber(paid) ? (int64_t)paid->valuedouble : 0;
        const int64_t outstanding = d - p;

        /* A zero-value open invoice is not revenue at risk. */
        if (outstanding > 0) {
            count++;
            cents += outstanding;
        }
    }

    cJSON_Delete(root);

    if (out_count) *out_count = count;
    if (out_cents) *out_cents = cents;

    ESP_LOGI(TAG, "invoices: HTTP %d, %d unpaid worth %lld cents",
             status, count, (long long)cents);

    return STRIPE_OK;
}
