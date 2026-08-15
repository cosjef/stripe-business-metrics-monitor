/*
 * Host tests for Stripe key format checking.
 *
 *   cd firmware/test && make && ./test_stripe_key
 *
 * The failure this guards against is a customer pasting the wrong key and the
 * device failing minutes later with no explanation. Spec 9.1 step 3 asks for a
 * restricted key specifically; a secret key would give a desk ornament the
 * authority to issue refunds.
 */
#include <stdio.h>
#include <string.h>

#include "../main/stripe_key.h"

static int failures = 0;
static int checks = 0;

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void check_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
    }
}

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

static void check_false(const char *what, int cond)
{
    checks++;
    if (cond) {
        failures++;
        printf("  FAIL %s: expected false\n", what);
    }
}

/* Realistic shapes. Not real keys. */
#define RK_LIVE "rk_live_51MnOpQrStUvWxYz0123456789abcdefGHIJKLMNOP"
#define RK_TEST "rk_test_51MnOpQrStUvWxYz0123456789abcdefGHIJKLMNOP"
#define SK_LIVE "sk_live_51MnOpQrStUvWxYz0123456789abcdefGHIJKLMNOP"
#define PK_LIVE "pk_live_51MnOpQrStUvWxYz0123456789abcdefGHIJKLMNOP"

static void test_accepts_restricted_keys(void)
{
    printf("restricted keys are accepted\n");

    check_int("rk_live_", stripe_key_validate(RK_LIVE), STRIPE_KEY_OK);
    check_int("rk_test_", stripe_key_validate(RK_TEST), STRIPE_KEY_OK);
}

/*
 * A secret key would let a stolen desk ornament issue refunds. Spec 9.1 draws
 * this distinction explicitly, so the rejection names it rather than saying
 * "invalid".
 */
static void test_rejects_secret_key(void)
{
    printf("secret keys are rejected, by name\n");

    check_int("sk_live_ rejected", stripe_key_validate(SK_LIVE),
              STRIPE_KEY_IS_SECRET);
    check_int("sk_test_ rejected",
              stripe_key_validate("sk_test_51abcdefGHIJKLMNOPqrstuvwxyz0123"),
              STRIPE_KEY_IS_SECRET);

    const char *msg = stripe_key_result_str(STRIPE_KEY_IS_SECRET);
    check_true("message mentions restricted", strstr(msg, "restricted") != NULL);
}

static void test_rejects_publishable_key(void)
{
    printf("publishable keys are rejected, by name\n");

    check_int("pk_live_ rejected", stripe_key_validate(PK_LIVE),
              STRIPE_KEY_IS_PUBLISHABLE);
    check_int("pk_test_ rejected",
              stripe_key_validate("pk_test_51abcdefGHIJKLMNOPqrstuvwxyz0123"),
              STRIPE_KEY_IS_PUBLISHABLE);
}

static void test_rejects_malformed(void)
{
    printf("malformed input is rejected\n");

    check_int("empty", stripe_key_validate(""), STRIPE_KEY_EMPTY);
    check_int("NULL", stripe_key_validate(NULL), STRIPE_KEY_EMPTY);
    check_int("no prefix", stripe_key_validate("abcdef123456789012345678901234"),
              STRIPE_KEY_BAD_PREFIX);
    check_int("wrong prefix", stripe_key_validate("ak_live_1234567890abcdefghij"),
              STRIPE_KEY_BAD_PREFIX);
    check_int("prefix only", stripe_key_validate("rk_live_"),
              STRIPE_KEY_TOO_SHORT);
    check_int("too short", stripe_key_validate("rk_live_abc"),
              STRIPE_KEY_TOO_SHORT);

    char longkey[STRIPE_KEY_MAX_LEN + 50];
    memcpy(longkey, "rk_live_", 8);
    memset(longkey + 8, 'a', sizeof(longkey) - 9);
    longkey[sizeof(longkey) - 1] = '\0';
    check_int("too long", stripe_key_validate(longkey), STRIPE_KEY_TOO_LONG);
}

/*
 * Copy-paste from a browser or email routinely picks up whitespace, quotes, or
 * a trailing newline. Naming that is far more useful than "invalid key".
 */
static void test_rejects_paste_artifacts(void)
{
    printf("paste artifacts are rejected\n");

    check_int("trailing space",
              stripe_key_validate("rk_live_51MnOpQrStUvWxYz0123456789abcd "),
              STRIPE_KEY_BAD_CHARS);
    check_int("embedded newline",
              stripe_key_validate("rk_live_51MnOpQrStUv\nWxYz0123456789abcd"),
              STRIPE_KEY_BAD_CHARS);
    check_int("quotes",
              stripe_key_validate("\"rk_live_51MnOpQrStUvWxYz0123456789ab\""),
              STRIPE_KEY_BAD_PREFIX);
    check_int("tab",
              stripe_key_validate("rk_live_51MnOpQrStUv\tWxYz0123456789abcd"),
              STRIPE_KEY_BAD_CHARS);
}

static void test_test_mode_detection(void)
{
    printf("test-mode detection\n");

    check_true("rk_test_ is test mode", stripe_key_is_test_mode(RK_TEST));
    check_false("rk_live_ is not test mode", stripe_key_is_test_mode(RK_LIVE));
    check_false("NULL is not test mode", stripe_key_is_test_mode(NULL));
}

/*
 * Serial output is shared during support, so a key must never appear whole in
 * a log line.
 */
static void test_redaction(void)
{
    printf("redaction never exposes the key\n");

    char out[64];

    stripe_key_redact(RK_LIVE, out, sizeof(out));
    check_str("redacted live key", out, "rk_live_...MNOP");
    check_true("redaction is not the whole key", strcmp(out, RK_LIVE) != 0);
    check_true("redaction omits the middle", strstr(out, "51MnOpQrSt") == NULL);

    stripe_key_redact(RK_TEST, out, sizeof(out));
    check_str("redacted test key", out, "rk_test_...MNOP");

    /* Degenerate inputs must not crash or leak. */
    stripe_key_redact("", out, sizeof(out));
    check_true("empty key redacts safely", strlen(out) < sizeof(out));

    stripe_key_redact(NULL, out, sizeof(out));
    check_true("NULL redacts safely", strlen(out) < sizeof(out));

    stripe_key_redact("rk_live_ab", out, sizeof(out));
    check_true("short key redacts safely", strlen(out) < sizeof(out));

    /* A buffer too small to hold the result must still terminate. */
    char tiny[6];
    stripe_key_redact(RK_LIVE, tiny, sizeof(tiny));
    check_true("respects a small buffer", strlen(tiny) < sizeof(tiny));
}

static void test_every_result_has_a_message(void)
{
    printf("every result has a message\n");

    const stripe_key_result_t all[] = {
        STRIPE_KEY_OK, STRIPE_KEY_EMPTY, STRIPE_KEY_TOO_LONG,
        STRIPE_KEY_BAD_PREFIX, STRIPE_KEY_IS_SECRET,
        STRIPE_KEY_IS_PUBLISHABLE, STRIPE_KEY_TOO_SHORT, STRIPE_KEY_BAD_CHARS,
    };

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        const char *msg = stripe_key_result_str(all[i]);
        char what[64];
        snprintf(what, sizeof(what), "result %d has a message", (int)all[i]);
        check_true(what, msg != NULL && msg[0] != '\0');
    }
}

int main(void)
{
    printf("Stripe key validation tests (spec 9.1)\n\n");

    test_accepts_restricted_keys();
    test_rejects_secret_key();
    test_rejects_publishable_key();
    test_rejects_malformed();
    test_rejects_paste_artifacts();
    test_test_mode_detection();
    test_redaction();
    test_every_result_has_a_message();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
