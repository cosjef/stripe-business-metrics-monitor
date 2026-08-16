/*
 * Host tests for cached-value validation (spec 7.4 step 1).
 *
 *   cd firmware/test && make && ./test_cache
 *
 * The failure these guard against is showing fabricated numbers: corrupt or
 * stale flash reinterpreted as current revenue would be a confident lie, which
 * is precisely what spec 1 principle 4 forbids.
 */
#include <stdio.h>
#include <string.h>

#include "../main/cache.h"

static int failures = 0;
static int checks = 0;

static void check_i64(const char *what, int64_t got, int64_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %lld, want %lld\n", what,
               (long long)got, (long long)want);
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

/* A realistic cache, matching the live account. */
static cache_t good(void)
{
    cache_t c = {0};
    c.version = CACHE_VERSION;
    c.saved_at_utc = 1786000000;
    c.mrr_cents = 94133;
    c.active_count = 28;
    c.trial_count = 0;
    c.churned_30d = 1;
    c.new_paid_30d = 0;
    c.failed_count = 1;
    c.failed_cents = 2900;
    c.have_invoices = true;
    return c;
}

static void test_accepts_a_good_cache(void)
{
    printf("a well-formed cache is accepted\n");

    const cache_t c = good();
    check_true("valid", cache_is_valid(&c));
}

/*
 * A cache written by older firmware must be discarded, not reinterpreted.
 * Reading old bytes as a new layout would put fabricated figures on screen.
 */
static void test_rejects_wrong_version(void)
{
    printf("a cache from another firmware version is discarded\n");

    cache_t c = good();
    c.version = CACHE_VERSION + 1;
    check_false("newer version rejected", cache_is_valid(&c));

    c.version = 0;
    check_false("zero version rejected", cache_is_valid(&c));

    c.version = CACHE_VERSION - 1;
    check_false("older version rejected", cache_is_valid(&c));
}

/*
 * Uninitialized or corrupt flash reads as garbage. It must fail validation
 * rather than reach the screen.
 */
static void test_rejects_corruption(void)
{
    printf("corrupt values are rejected\n");

    cache_t c = good();
    c.saved_at_utc = 0;
    check_false("no timestamp", cache_is_valid(&c));

    c = good();
    c.active_count = -5;
    check_false("negative subscriber count", cache_is_valid(&c));

    c = good();
    c.trial_count = -1;
    check_false("negative trial count", cache_is_valid(&c));

    c = good();
    c.failed_count = -3;
    check_false("negative failed count", cache_is_valid(&c));

    /* Erased flash is all 0xFF, which reads as huge values. */
    c = good();
    c.active_count = 999999999;
    check_false("implausible subscriber count", cache_is_valid(&c));

    c = good();
    c.mrr_cents = -100;
    check_false("negative MRR", cache_is_valid(&c));

    /* $100m/month is beyond any account this device is for. */
    c = good();
    c.mrr_cents = 100000000000LL;
    check_false("implausible MRR", cache_is_valid(&c));
}

static void test_zero_values_are_valid(void)
{
    printf("a genuinely empty account is valid\n");

    cache_t c = good();
    c.mrr_cents = 0;
    c.active_count = 0;
    c.churned_30d = 0;
    c.failed_count = 0;
    c.failed_cents = 0;

    /* Zero is a real answer, not corruption. */
    check_true("all zeros still valid", cache_is_valid(&c));
}

static void test_null_is_invalid(void)
{
    printf("a NULL cache is invalid\n");
    check_false("NULL", cache_is_valid(NULL));
}

/* ---- age ---- */

static void test_age(void)
{
    printf("cache age\n");

    const cache_t c = good();

    check_i64("just saved", cache_age_seconds(&c, c.saved_at_utc), 0);
    check_i64("an hour later", cache_age_seconds(&c, c.saved_at_utc + 3600), 3600);
    check_i64("a day later", cache_age_seconds(&c, c.saved_at_utc + 86400), 86400);

    /*
     * Before NTP syncs the clock can be behind the saved timestamp, making
     * the cache look future-dated. That is unusable rather than fresh.
     */
    check_i64("clock behind the cache", cache_age_seconds(&c, c.saved_at_utc - 100), -1);

    check_i64("invalid cache has no age", cache_age_seconds(NULL, 1786000000), -1);
}

/*
 * Past a day the figures mislead more than they help: a subscriber count from
 * last week shown as "stale" still invites the reader to act on it.
 */
static void test_too_old(void)
{
    printf("a cache older than a day is not shown\n");

    const cache_t c = good();

    check_false("fresh", cache_too_old(&c, c.saved_at_utc));
    check_false("an hour", cache_too_old(&c, c.saved_at_utc + 3600));
    check_false("23 hours", cache_too_old(&c, c.saved_at_utc + 23 * 3600));
    check_true("25 hours", cache_too_old(&c, c.saved_at_utc + 25 * 3600));
    check_true("a week", cache_too_old(&c, c.saved_at_utc + 7 * 86400));

    /* An unusable age counts as too old rather than as fresh. */
    check_true("future-dated is too old",
               cache_too_old(&c, c.saved_at_utc - 100));
}

/*
 * A round trip through raw bytes, which is what NVS stores.
 */
static void test_survives_byte_round_trip(void)
{
    printf("the struct survives a byte round trip\n");

    const cache_t original = good();

    uint8_t blob[sizeof(cache_t)];
    memcpy(blob, &original, sizeof(blob));

    cache_t restored;
    memcpy(&restored, blob, sizeof(restored));

    check_true("still valid", cache_is_valid(&restored));
    check_i64("MRR preserved", restored.mrr_cents, original.mrr_cents);
    check_i64("count preserved", restored.active_count, original.active_count);
    check_i64("timestamp preserved", restored.saved_at_utc, original.saved_at_utc);
    check_true("flags preserved", restored.have_invoices == original.have_invoices);
}

int main(void)
{
    printf("cached value tests (spec 7.4 step 1)\n\n");

    test_accepts_a_good_cache();
    test_rejects_wrong_version();
    test_rejects_corruption();
    test_zero_values_are_valid();
    test_null_is_invalid();
    test_age();
    test_too_old();
    test_survives_byte_round_trip();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
