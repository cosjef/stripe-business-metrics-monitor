/*
 * Stripe fetch for the Arduino port.
 *
 * Wraps the shared streaming scanner (firmware/main/jsonstream.c) in
 * NetworkClientSecure, so the response is folded into a running total as its
 * bytes arrive and never assembled.
 */
#pragma once

#include <stdbool.h>

extern "C" {
#include "mrr.h"
}

typedef enum {
    STRIPE_FETCH_OK = 0,
    STRIPE_FETCH_NO_KEY,
    STRIPE_FETCH_NO_NETWORK,
    STRIPE_FETCH_TLS_FAILED,
    STRIPE_FETCH_UNAUTHORIZED,   /* 401: key revoked or wrong scope */
    STRIPE_FETCH_HTTP_ERROR,
    STRIPE_FETCH_BAD_RESPONSE,
} stripe_fetch_result_t;

/* Stripe keys are at most 255 characters; the buffer holds one plus a NUL. */
#define STRIPE_KEY_BUF_LEN 256

/*
 * Set the secret key. The string is COPIED, so the caller may free it
 * immediately -- the portal passes a request-scoped temporary.
 */
void stripe_fetch_set_key(const char *key);

/*
 * Fetch every subscription and fold it into `out`.
 *
 * Paginates with starting_after until has_more is false. `truncated` is set
 * when the page cap was hit, so the caller can mark the figure incomplete
 * rather than presenting a partial total as final.
 */
stripe_fetch_result_t stripe_fetch_totals(mrr_totals_t *out, bool *truncated);

/*
 * Failed payments: money that did not arrive and can still be chased.
 *
 * A separate call because invoices are a different endpoint and a different
 * document shape. Returns STRIPE_FETCH_OK with zeroes when nothing is
 * failing, which is the normal state.
 */
typedef struct {
    int count;
    int64_t cents;
    int64_t next_retry;   /* Unix seconds, 0 if Stripe has stopped retrying */
} stripe_failed_t;

stripe_fetch_result_t stripe_fetch_failed(stripe_failed_t *out);

/* Human-readable name for a result, for logging. */
const char *stripe_fetch_strerror(stripe_fetch_result_t r);
