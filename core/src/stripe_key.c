/*
 * Stripe API key format checking. See stripe_key.h.
 */
#include "stripe_key.h"

#include <stdio.h>
#include <string.h>

/* Shortest plausible key body after the prefix. Real keys are far longer;
 * this only needs to catch a truncated paste. */
#define KEY_MIN_BODY_LEN 16

static bool has_prefix(const char *s, const char *prefix)
{
    return strncmp(s, prefix, strlen(prefix)) == 0;
}

stripe_key_result_t stripe_key_validate(const char *key)
{
    if (key == NULL || key[0] == '\0') {
        return STRIPE_KEY_EMPTY;
    }

    const size_t len = strlen(key);
    if (len > STRIPE_KEY_MAX_LEN) {
        return STRIPE_KEY_TOO_LONG;
    }

    /* Name the wrong-key-type cases specifically. "Invalid key" would leave a
     * customer who pasted their secret key with nothing to act on, and that is
     * the paste most worth catching. */
    if (has_prefix(key, "sk_live_") || has_prefix(key, "sk_test_")) {
        return STRIPE_KEY_IS_SECRET;
    }
    if (has_prefix(key, "pk_live_") || has_prefix(key, "pk_test_")) {
        return STRIPE_KEY_IS_PUBLISHABLE;
    }

    const bool restricted = has_prefix(key, "rk_live_") || has_prefix(key, "rk_test_");
    if (!restricted) {
        return STRIPE_KEY_BAD_PREFIX;
    }

    const size_t prefix_len = strlen("rk_live_");
    if (len - prefix_len < KEY_MIN_BODY_LEN) {
        return STRIPE_KEY_TOO_SHORT;
    }

    /* Stripe key bodies are alphanumeric. Anything else means the paste picked
     * up whitespace, a newline, or surrounding punctuation. */
    for (const char *p = key + prefix_len; *p; p++) {
        const bool ok = (*p >= 'a' && *p <= 'z') ||
                        (*p >= 'A' && *p <= 'Z') ||
                        (*p >= '0' && *p <= '9') ||
                        *p == '_';
        if (!ok) {
            return STRIPE_KEY_BAD_CHARS;
        }
    }

    return STRIPE_KEY_OK;
}

const char *stripe_key_result_str(stripe_key_result_t r)
{
    switch (r) {
    case STRIPE_KEY_OK:
        return "OK";
    case STRIPE_KEY_EMPTY:
        return "API key is required";
    case STRIPE_KEY_TOO_LONG:
        return "That key is too long to be a Stripe key";
    case STRIPE_KEY_BAD_PREFIX:
        return "That does not look like a Stripe key. It should start with rk_live_ or rk_test_";
    case STRIPE_KEY_IS_SECRET:
        return "That is a secret key. This display needs a restricted key (rk_live_) with read access only";
    case STRIPE_KEY_IS_PUBLISHABLE:
        return "That is a publishable key. This display needs a restricted key (rk_live_) that can read subscriptions";
    case STRIPE_KEY_TOO_SHORT:
        return "That key looks incomplete. Check the whole key was copied";
    case STRIPE_KEY_BAD_CHARS:
        return "That key contains unexpected characters. Check for spaces or line breaks";
    }
    return "Unknown error";
}

bool stripe_key_is_test_mode(const char *key)
{
    return key != NULL && has_prefix(key, "rk_test_");
}

void stripe_key_redact(const char *key, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (key == NULL || key[0] == '\0') {
        snprintf(out, out_len, "(none)");
        return;
    }

    const size_t len = strlen(key);
    const size_t prefix_len = strlen("rk_live_");

    /* Too short to redact meaningfully -- show nothing but the shape. */
    if (len < prefix_len + 8) {
        snprintf(out, out_len, "(malformed)");
        return;
    }

    /* Prefix identifies live vs test at a glance; the last four characters are
     * enough for a customer to confirm which key they pasted, without the
     * middle ever appearing in a log. */
    char prefix[16];
    const size_t n = prefix_len < sizeof(prefix) - 1 ? prefix_len : sizeof(prefix) - 1;
    memcpy(prefix, key, n);
    prefix[n] = '\0';

    snprintf(out, out_len, "%s...%s", prefix, key + len - 4);
}
