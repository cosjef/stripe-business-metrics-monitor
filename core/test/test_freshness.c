/*
 * Host tests for freshness and backoff (spec 7.4).
 *
 *   cd firmware/test && make && ./test_freshness
 *
 * Spec 6.2 calls the stale screen the most important in the deck, because the
 * failure it prevents -- a four-hour-old figure displayed with full confidence
 * -- is invisible to the person reading it.
 */
#include <stdio.h>
#include <string.h>

#include "../include/freshness.h"

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

#define MIN_MS (60 * 1000LL)

/*
 * Data that has never loaded is absent, not stale. Showing "stale" over a
 * number that never existed would be its own lie.
 */
static void test_never_loaded_is_not_stale(void)
{
    printf("never-loaded data is absent, not stale\n");

    freshness_t f;
    freshness_init(&f);

    check_false("not stale at boot", freshness_is_stale(&f, 0));
    check_false("not stale an hour later", freshness_is_stale(&f, 60 * MIN_MS));
    check_i64("age is -1 when never loaded", freshness_age_ms(&f, 60 * MIN_MS), -1);
}

static void test_fresh_after_success(void)
{
    printf("data is fresh right after a successful fetch\n");

    freshness_t f;
    freshness_init(&f);
    freshness_mark_success(&f, 1000);

    check_false("fresh immediately", freshness_is_stale(&f, 1000));
    check_false("fresh after 1 min", freshness_is_stale(&f, 1000 + MIN_MS));
    check_false("fresh after 14 min", freshness_is_stale(&f, 1000 + 14 * MIN_MS));
}

/* Spec 7.4 step 2 / appendix A: the threshold is 15 minutes. */
static void test_stale_threshold(void)
{
    printf("stale after 15 minutes (spec 7.4 step 2)\n");

    freshness_t f;
    freshness_init(&f);

    /* A non-zero base: mark_success() clamps 0 up to 1, because 0 is the
     * sentinel for "never loaded". */
    const int64_t t0 = 1000;
    freshness_mark_success(&f, t0);

    check_false("14m59s is fresh", freshness_is_stale(&f, t0 + 15 * MIN_MS - 1000));
    check_true("15m exactly is stale", freshness_is_stale(&f, t0 + 15 * MIN_MS));
    check_true("an hour is stale", freshness_is_stale(&f, t0 + 60 * MIN_MS));
}

static void test_success_clears_staleness(void)
{
    printf("a later success clears staleness\n");

    freshness_t f;
    freshness_init(&f);
    freshness_mark_success(&f, 1000);
    check_true("stale after an hour", freshness_is_stale(&f, 1000 + 60 * MIN_MS));

    freshness_mark_success(&f, 60 * MIN_MS);
    check_false("fresh again", freshness_is_stale(&f, 60 * MIN_MS));
}

/*
 * Spec 7.4 step 3: 60s, 120s, 240s, capped at 15 minutes.
 */
static void test_backoff_schedule(void)
{
    printf("exponential backoff, capped (spec 7.4 step 3)\n");

    freshness_t f;
    freshness_init(&f);

    check_i64("no delay before any failure", freshness_retry_delay_ms(&f), 0);

    freshness_mark_failure(&f);
    check_i64("first failure: 60s", freshness_retry_delay_ms(&f), 60 * 1000);

    freshness_mark_failure(&f);
    check_i64("second: 120s", freshness_retry_delay_ms(&f), 120 * 1000);

    freshness_mark_failure(&f);
    check_i64("third: 240s", freshness_retry_delay_ms(&f), 240 * 1000);

    freshness_mark_failure(&f);
    check_i64("fourth: 480s", freshness_retry_delay_ms(&f), 480 * 1000);

    /* Must cap rather than growing without bound. */
    for (int i = 0; i < 20; i++) {
        freshness_mark_failure(&f);
    }
    check_i64("capped at 15 min", freshness_retry_delay_ms(&f), 15 * 60 * 1000);
}

static void test_success_resets_backoff(void)
{
    printf("success resets the backoff\n");

    freshness_t f;
    freshness_init(&f);

    for (int i = 0; i < 5; i++) {
        freshness_mark_failure(&f);
    }
    check_true("backed off", freshness_retry_delay_ms(&f) > 60 * 1000);

    freshness_mark_success(&f, 1000);
    check_i64("reset after success", freshness_retry_delay_ms(&f), 0);
}

/*
 * The age string is what makes staleness legible. Coarse by design: the point
 * is that the number is old, and the glyph budget is tight (spec 2.3).
 */
static void test_age_formatting(void)
{
    printf("age formatting for the stale screen\n");

    char b[32];

    freshness_format_age(0, b, sizeof(b));                 check_str("just now", b, "0 min");
    freshness_format_age(60 * 1000, b, sizeof(b));         check_str("1 min", b, "1 min");
    freshness_format_age(22 * MIN_MS, b, sizeof(b));       check_str("22 min", b, "22 min");
    freshness_format_age(59 * MIN_MS, b, sizeof(b));       check_str("59 min", b, "59 min");
    freshness_format_age(60 * MIN_MS, b, sizeof(b));       check_str("1 hr", b, "1 hr");
    freshness_format_age(3 * 60 * MIN_MS, b, sizeof(b));   check_str("3 hr", b, "3 hr");
    freshness_format_age(23 * 60 * MIN_MS, b, sizeof(b));  check_str("23 hr", b, "23 hr");
    freshness_format_age(24 * 60 * MIN_MS, b, sizeof(b));  check_str("1 day", b, "1 day");
    freshness_format_age(48 * 60 * MIN_MS, b, sizeof(b));  check_str("2 days", b, "2 days");

    /* The spec's own mockup shows "stale | 22 min". */
    freshness_format_age(22 * MIN_MS, b, sizeof(b));
    check_str("matches the spec mockup", b, "22 min");
}

static void test_age_fits_the_column(void)
{
    printf("age strings stay short enough for the subtitle\n");

    const int64_t ages[] = {
        0, MIN_MS, 22 * MIN_MS, 60 * MIN_MS, 5 * 60 * MIN_MS,
        24 * 60 * MIN_MS, 30LL * 24 * 60 * MIN_MS,
    };

    char b[32];
    for (size_t i = 0; i < sizeof(ages) / sizeof(ages[0]); i++) {
        freshness_format_age(ages[i], b, sizeof(b));
        char what[64];
        snprintf(what, sizeof(what), "\"%s\" is short", b);
        /* "stale | " plus the age must fit the 22px subtitle line. */
        check_true(what, strlen(b) <= 8);
    }
}

static void test_age_reporting(void)
{
    printf("age in milliseconds\n");

    freshness_t f;
    freshness_init(&f);
    freshness_mark_success(&f, 1000);

    check_i64("zero at the moment of success", freshness_age_ms(&f, 1000), 0);
    check_i64("one minute later", freshness_age_ms(&f, 1000 + MIN_MS), MIN_MS);
}

int main(void)
{
    printf("freshness and backoff tests (spec 7.4)\n\n");

    test_never_loaded_is_not_stale();
    test_fresh_after_success();
    test_stale_threshold();
    test_success_clears_staleness();
    test_backoff_schedule();
    test_success_resets_backoff();
    test_age_formatting();
    test_age_fits_the_column();
    test_age_reporting();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
