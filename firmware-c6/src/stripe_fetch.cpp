/*
 * Stripe fetch: HTTPS on NetworkClientSecure, parsed by the shared streaming
 * scanner.
 *
 * The body is read in small chunks straight off the socket and fed to
 * jsonstream_feed(), which folds each subscription into a running total and
 * forgets it. Nothing reassembles the response. See jsonstream.h for why that
 * is kept even though this runtime measured 131KB of contiguous heap with TLS
 * open -- it is O(1) in account size, which buffering is not.
 */
#include "stripe_fetch.h"

#include <Arduino.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "stripe_ca.h"

extern "C" {
#include "invoices.h"
#include "jsonstream.h"
}

#define STRIPE_HOST "api.stripe.com"
#define STRIPE_PORT 443

/*
 * Stripe's maximum. Page size no longer costs memory here -- the scanner does
 * not hold the response -- so a larger page only means fewer round trips.
 */
#define SUBS_PAGE_LIMIT 100

/* Hard stop so a misbehaving has_more cannot loop forever. */
#define SUBS_MAX_PAGES 160

/* Socket read chunk. Small on purpose: this is a scratch buffer on the stack,
 * and the scanner is explicitly tested against every chunk boundary. */
#define READ_CHUNK 512

/* A response that stops arriving should fail, not hang the rotation. */
#define READ_TIMEOUT_MS 6000

/*
 * The key is COPIED, not borrowed.
 *
 * An earlier version stored the caller's pointer. The portal validates a key
 * straight from the HTTP request handler, where the string is a temporary
 * that is freed the moment the response is sent -- so validation succeeded,
 * the key was saved to NVS, and every fetch afterwards read freed memory and
 * reported "no key". The device would have needed a reboot to work at all,
 * immediately after the one moment the owner was watching it.
 */
static char s_key[STRIPE_KEY_BUF_LEN];

void stripe_fetch_set_key(const char *key)
{
    snprintf(s_key, sizeof(s_key), "%s", key ? key : "");
}

/* stripe_fetch_strerror lives in core/src/fetch_result.c so the host tests
 * can reach it without Arduino networking. */

/*
 * Read the status line and headers, returning the HTTP status code.
 *
 * Leaves the stream positioned at the first byte of the body. Returns -1 if
 * the response is malformed or the socket dies mid-header.
 */
static int read_status_and_headers(NetworkClientSecure &client)
{
    const uint32_t deadline = millis() + READ_TIMEOUT_MS;

    String status = client.readStringUntil('\n');
    if (status.length() == 0) {
        Serial.println("stripe: empty status line (connected but no response)");
        return -1;
    }
    /* "HTTP/1.1 200 OK" -- the code is the second field. */
    const int sp = status.indexOf(' ');
    if (sp < 0) {
        Serial.println("stripe: malformed status line");
        return -1;
    }
    const int code = status.substring(sp + 1, sp + 4).toInt();

    /* Drain headers. The body is chunked or length-delimited; either way the
     * scanner tolerates whatever framing bytes survive, because it only
     * reacts to JSON structure. */
    while (millis() < deadline) {
        String line = client.readStringUntil('\n');
        /* A bare CR (or empty line) terminates the header block. */
        if (line.length() == 0 || line == "\r") {
            return code;
        }
    }
    return -1;
}

/* Fetch one page, feeding it to the scanner. Returns the HTTP status. */
static int fetch_page(NetworkClientSecure &client, jsonstream_t *js,
                      const char *cursor)
{
    if (!client.connect(STRIPE_HOST, STRIPE_PORT)) {
        char err[128] = "";
        client.lastError(err, sizeof(err));
        Serial.printf("stripe: connect failed: %s (heap free=%u largest=%u)\n",
                      err[0] ? err : "(none)",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return -1;
    }

    /* Build the request. status=all so trials are counted separately rather
     * than silently dropped from the deck. */
    String path = String("/v1/subscriptions?status=all&limit=") + SUBS_PAGE_LIMIT;
    if (cursor && cursor[0]) {
        path += "&starting_after=";
        path += cursor;
    }

    client.printf("GET %s HTTP/1.1\r\n", path.c_str());
    client.printf("Host: %s\r\n", STRIPE_HOST);
    client.printf("Authorization: Bearer %s\r\n", s_key);
    /* Identify the client, as Stripe asks integrations to. */
    client.print("User-Agent: stripe-metrics-monitor/0.4 (esp32-c6)\r\n");
    /* No keep-alive: one page per connection keeps the socket state simple,
     * and pagination past the first page is rare. */
    client.print("Connection: close\r\n");
    client.print("Accept: application/json\r\n\r\n");

    const int status = read_status_and_headers(client);
    if (status < 0) {
        /* The handshake succeeded and the request went out; the failure is in
         * reading the response -- a dropped connection or a read timeout. That
         * is a different fault from a TLS failure, so report it as one. */
        Serial.printf("stripe: no response (connected=%d heap largest=%u)\n",
                      (int)client.connected(),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        client.stop();
        return -2;
    }

    /*
     * Stream the body. Even on an error status the body is drained through
     * the scanner's feed -- it costs nothing and keeps the socket teardown
     * uniform -- but the caller decides what the status means.
     */
    char buf[READ_CHUNK];
    uint32_t last_data = millis();

    while (client.connected() || client.available()) {
        const int n = client.available();
        if (n > 0) {
            const int got = client.read((uint8_t *)buf,
                                        n < READ_CHUNK ? n : READ_CHUNK);
            if (got > 0) {
                if (status == 200) {
                    jsonstream_feed(js, buf, (size_t)got);
                }
                last_data = millis();
            }
        } else {
            if (millis() - last_data > READ_TIMEOUT_MS) {
                break;
            }
            delay(1);
        }
    }

    client.stop();
    return status;
}

/*
 * Fetch open invoices and total the recoverable failures.
 *
 * status=open filters server-side, so void and paid invoices never cross the
 * wire -- the scanner still checks, because a filter that silently changes
 * would otherwise inflate the figure with money that cannot be collected.
 */
stripe_fetch_result_t stripe_fetch_failed(stripe_failed_t *out)
{
    if (out == NULL) {
        return STRIPE_FETCH_BAD_RESPONSE;
    }
    memset(out, 0, sizeof(*out));

    if (s_key[0] == '\0') {
        return STRIPE_FETCH_NO_KEY;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return STRIPE_FETCH_NO_NETWORK;
    }

    invoices_t *iv = (invoices_t *)malloc(sizeof(invoices_t));
    if (iv == NULL) {
        return STRIPE_FETCH_BAD_RESPONSE;
    }
    invoices_init(iv);

    NetworkClientSecure client;
    client.setCACert(STRIPE_ROOT_CA);
    client.setHandshakeTimeout(8);

    stripe_fetch_result_t result = STRIPE_FETCH_OK;

    if (!client.connect(STRIPE_HOST, STRIPE_PORT)) {
        free(iv);
        return STRIPE_FETCH_TLS_FAILED;
    }

    client.printf("GET /v1/invoices?status=open&limit=100 HTTP/1.1\r\n");
    client.printf("Host: %s\r\n", STRIPE_HOST);
    client.printf("Authorization: Bearer %s\r\n", s_key);
    client.print("User-Agent: stripe-metrics-monitor/0.4 (esp32-c6)\r\n");
    client.print("Connection: close\r\n");
    client.print("Accept: application/json\r\n\r\n");

    const int status = read_status_and_headers(client);
    if (status == 401 || status == 403) {
        /*
         * The key cannot read invoices. Not an error worth showing: the
         * screen simply stays hidden, the same as an account with nothing
         * failing.
         */
        result = STRIPE_FETCH_UNAUTHORIZED;
    } else if (status != 200) {
        result = STRIPE_FETCH_HTTP_ERROR;
    } else {
        char buf[READ_CHUNK];
        uint32_t last_data = millis();
        while (client.connected() || client.available()) {
            const int n = client.available();
            if (n > 0) {
                const int got = client.read((uint8_t *)buf,
                                            n < READ_CHUNK ? n : READ_CHUNK);
                if (got > 0) {
                    invoices_feed(iv, buf, (size_t)got);
                    last_data = millis();
                }
            } else {
                if (millis() - last_data > READ_TIMEOUT_MS) {
                    break;
                }
                delay(1);
            }
        }
        invoices_finish(iv);

        out->count = iv->failed_count;
        out->cents = iv->failed_cents;
        out->next_retry = iv->next_retry;
    }

    client.stop();
    free(iv);
    return result;
}

stripe_fetch_result_t stripe_fetch_totals(mrr_totals_t *out, bool *truncated)
{
    if (out == NULL) {
        return STRIPE_FETCH_BAD_RESPONSE;
    }
    memset(out, 0, sizeof(*out));
    if (truncated) {
        *truncated = false;
    }

    if (s_key[0] == '\0') {
        return STRIPE_FETCH_NO_KEY;
    }
    if (WiFi.status() != WL_CONNECTED) {
        return STRIPE_FETCH_NO_NETWORK;
    }

    /* 400 bytes; small enough for the stack, but the fetch runs from a task
     * whose depth is not ours to assume, so it goes on the heap. */
    jsonstream_t *js = (jsonstream_t *)malloc(sizeof(jsonstream_t));
    if (js == NULL) {
        return STRIPE_FETCH_BAD_RESPONSE;
    }
    jsonstream_init(js);

    /*
     * Flow window: the last 30 days, matching the rolling window the deck
     * already uses. Skipped when the clock has not synced -- the scanner then
     * reports zero flow rather than counting every subscription ever created
     * as new this month.
     */
    {
        const time_t now = time(NULL);
        if (now > 1672531200) {          /* 2023-01-01: clock is real */
            jsonstream_set_window(js, (int64_t)now - 30 * 86400);
            /* Same length again, immediately before, for the pace comparison. */
            jsonstream_set_span(js, 30 * 86400);
        }
    }

    NetworkClientSecure client;
    /*
     * Verify the server against Stripe's root CA. See stripe_ca.h for which
     * certificate this is and why it is the self-signed root rather than the
     * cross-signed copy the server actually sends.
     *
     * A failure here is a refusal, not a fallback: if the certificate does
     * not check out the fetch fails and the deck keeps showing its last good
     * values. Never retry such a failure with setInsecure() -- that would
     * turn the one defence against a substituted revenue figure into a
     * speed bump.
     */
    client.setCACert(STRIPE_ROOT_CA);
    /* setHandshakeTimeout, not setTimeout: the latter is Stream's read
     * timeout and does not govern the TLS handshake at all. Both are in
     * seconds. */
    client.setHandshakeTimeout(8);

    char cursor[JSONSTREAM_TOKEN_MAX] = "";
    stripe_fetch_result_t result = STRIPE_FETCH_OK;
    int pages = 0;

    for (int page = 0; page < SUBS_MAX_PAGES; page++) {
        const int status = fetch_page(client, js, cursor);
        pages++;

        if (status == -2) {
            result = STRIPE_FETCH_NO_RESPONSE;
            break;
        }
        if (status < 0) {
            result = STRIPE_FETCH_TLS_FAILED;
            break;
        }
        if (status == 401 || status == 403) {
            result = STRIPE_FETCH_UNAUTHORIZED;
            break;
        }
        if (status != 200) {
            Serial.printf("stripe: HTTP %d\n", status);
            result = STRIPE_FETCH_HTTP_ERROR;
            break;
        }

        jsonstream_finish(js);

        if (!jsonstream_has_more(js)) {
            break;
        }

        /*
         * Advance the cursor. If Stripe says there is more but hands back the
         * same last id, stop: continuing would re-fetch the same page forever
         * and double-count every subscription in it.
         */
        const char *last = jsonstream_last_id(js);
        if (last == NULL || last[0] == '\0' || strcmp(last, cursor) == 0) {
            if (truncated) {
                *truncated = true;
            }
            break;
        }
        snprintf(cursor, sizeof(cursor), "%s", last);

        if (page == SUBS_MAX_PAGES - 1 && truncated) {
            *truncated = true;
        }
    }

    if (result == STRIPE_FETCH_OK) {
        *out = *jsonstream_totals(js);
        Serial.printf("stripe: %d page(s), %d active, %d trialing\n",
                      pages, out->active_count, out->trial_count);
    }

    free(js);
    return result;
}
