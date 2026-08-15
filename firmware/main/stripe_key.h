/*
 * Stripe API key format checking.
 *
 * This is a shape check, not authentication -- only Stripe can say whether a
 * key works. Its job is to catch the mistakes a customer makes while typing or
 * pasting, so the portal can say what is wrong instead of the device failing
 * silently minutes later (spec 9.1 step 3).
 *
 * No ESP-IDF dependencies, so it is tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Stripe keys are well under this; the buffer bounds what the portal accepts. */
#define STRIPE_KEY_MAX_LEN 255

typedef enum {
    STRIPE_KEY_OK = 0,
    STRIPE_KEY_EMPTY,
    STRIPE_KEY_TOO_LONG,
    STRIPE_KEY_BAD_PREFIX,     /* not a recognised Stripe key prefix at all */
    STRIPE_KEY_IS_SECRET,      /* sk_ -- far too much authority for this device */
    STRIPE_KEY_IS_PUBLISHABLE, /* pk_ -- cannot read subscriptions */
    STRIPE_KEY_TOO_SHORT,
    STRIPE_KEY_BAD_CHARS,
} stripe_key_result_t;

/*
 * Check the shape of a Stripe key.
 *
 * Accepts restricted keys (rk_live_/rk_test_) only. Spec 9.1 is explicit that
 * this device takes a restricted key with read access to Subscriptions, Events
 * and Customers, and nothing else: the worst case for a stolen device is then
 * that someone learns a subscriber count, rather than being able to issue
 * refunds.
 *
 * Secret and publishable keys get their own results so the portal can explain
 * the difference rather than saying "invalid".
 */
stripe_key_result_t stripe_key_validate(const char *key);

/* Message for the portal to show. Never includes the key itself. */
const char *stripe_key_result_str(stripe_key_result_t r);

/* True if the key targets test mode (rk_test_), for display purposes. */
bool stripe_key_is_test_mode(const char *key);

/*
 * Write a redacted form of `key` into `out` for logs and screens, e.g.
 * "rk_live_...4c21". Never emit a whole key: serial output is routinely shared
 * during support.
 */
void stripe_key_redact(const char *key, char *out, size_t out_len);
