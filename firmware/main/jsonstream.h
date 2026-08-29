/*
 * Streaming JSON scanner for Stripe subscription pages.
 *
 * The device displays about 60 bytes of state and transfers ~180KB to compute
 * it. On the S3 that ratio cost nothing, because 8MB of PSRAM held the whole
 * response. The C6 has 512KB of SRAM and a ~15KB largest contiguous block once
 * mbedTLS holds its session, so buffer-then-parse fails at any page size: at
 * ~6KB per subscription it forces one per request and forty-plus round trips.
 *
 * THAT ~15KB FIGURE IS ESP-IDF's, AND DOES NOT HOLD ON THE ARDUINO BUILD.
 * Measured on the C6 under Arduino with WiFi up and a live TLS session to
 * api.stripe.com: 155KB free, largest contiguous block 131,060 bytes, and a
 * 36KB buffer-then-parse allocation succeeds. So on that runtime this parser
 * is NOT the difference between working and not working.
 *
 * It is kept anyway, for reasons that survive the measurement:
 *   - it is O(1) in account size, where buffering is not. A 36KB buffer holds
 *     ~30 subscriptions at the measured ~1.2KB each; the device should not
 *     stop working when an account crosses a threshold nobody is watching.
 *   - it is already written and already tested, and removing it would mean
 *     new buffer-management code and new tests to replace passing ones.
 *   - the 131KB was measured before the captive portal and its sockets exist.
 *
 * Do not re-derive the old justification from the paragraph above without
 * re-measuring; on Arduino, buffering is now a viable fallback.
 *
 * Nothing requires reassembling the body. esp_http_client already delivers it
 * in chunks; concatenating them was inherited from the S3. This folds each
 * subscription into a running total as its bytes arrive and then forgets it,
 * so the resident set is a few hundred bytes rather than the document.
 *
 * NOT a general JSON parser. It walks data[] and pulls the eight fields MRR
 * needs, skipping everything else. That narrowness is deliberate: a general
 * parser would need to represent arbitrary structure, which is the cost being
 * avoided.
 *
 * No ESP-IDF, so it is host-tested -- and the tests feed every chunk size,
 * because chunk boundaries are where hand-rolled streaming parsers break.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mrr.h"

/* Longest token we need to hold across a chunk boundary: a Stripe id. */
#define JSONSTREAM_TOKEN_MAX 64

/* Nesting we track. Stripe subscriptions reach about six levels. */
#define JSONSTREAM_MAX_DEPTH 24

typedef struct {
    /* --- running results --- */
    mrr_totals_t totals;
    char last_id[JSONSTREAM_TOKEN_MAX];
    bool has_more;

    /* --- the subscription being assembled --- */
    char cur_id[JSONSTREAM_TOKEN_MAX];
    char cur_status[24];
    char cur_currency[MRR_CURRENCY_LEN];
    char cur_interval[12];
    int64_t cur_unit_amount;
    int64_t cur_quantity;
    int32_t cur_interval_count;
    int64_t cur_subtotal;      /* summed items for this subscription */
    int64_t cur_created;       /* subscription created, Unix seconds */
    int64_t cur_ended;         /* subscription ended, 0 if it has not */
    bool cur_tiered;
    bool in_subscription;
    bool item_has_price;

    /* --- scanner state --- */
    char token[JSONSTREAM_TOKEN_MAX];
    int token_len;
    char key[JSONSTREAM_TOKEN_MAX];
    int key_len;
    int depth;
    bool in_string;
    bool escaped;
    bool expect_value;   /* the next token is a value, not a key */
    bool in_data_array;
    int data_depth;      /* depth at which data[] elements sit */
    bool truncated;      /* a token was too long and was dropped */

    /* Flow window, Unix seconds. 0 means no window and no flow counting. */
    int64_t window_start;
} jsonstream_t;

void jsonstream_init(jsonstream_t *js);

/*
 * Set the flow window: subscriptions created or ended at or after
 * `window_start` (Unix seconds) are counted as joined or left.
 *
 * Optional. Without it new_count and churned_count stay zero, which is the
 * honest result for a device whose clock has not synced -- counting every
 * subscription ever created as "new this month" would be worse than showing
 * nothing. Call after jsonstream_init and before feeding.
 */
void jsonstream_set_window(jsonstream_t *js, int64_t window_start);

/* Feed one chunk. Safe to call with any split of the document. */
void jsonstream_feed(jsonstream_t *js, const char *data, size_t len);

/* Close out the final subscription, if the document ended mid-object. */
void jsonstream_finish(jsonstream_t *js);

const mrr_totals_t *jsonstream_totals(const jsonstream_t *js);
const char *jsonstream_last_id(const jsonstream_t *js);
bool jsonstream_has_more(const jsonstream_t *js);
