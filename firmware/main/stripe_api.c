#include "stripe_api.h"
#include "stripe_key.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_tls_errors.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "stripe";

#define STRIPE_HOST "api.stripe.com"
#define VALIDATE_URL "https://" STRIPE_HOST "/v1/subscriptions?limit=1"

/* The validation response is tiny (one subscription). A full subscriptions
 * page is 200-400KB and comes later, in Stage 5, where PSRAM makes a
 * full-buffer parse viable (spec 8.3 assumed far less heap). */
#define VALIDATE_BUF_LEN (16 * 1024)

/* Stripe is not slow, but a phone-tethered or congested network can be. */
#define HTTP_TIMEOUT_MS 15000

static char s_key[STRIPE_KEY_MAX_LEN + 1] = "";

/* Accumulates the response body across esp_http_client's chunk events. */
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
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
        ESP_LOGW(TAG, "response exceeded %u bytes, truncating", (unsigned)r->cap);
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
