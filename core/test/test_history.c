/*
 * Daily metric history, for the sparklines on the 480x480 panel.
 *
 *   cd firmware/test && make && ./test_history
 *
 * The C6 panel leaves 54% of its height empty once the hero, label, subtitle
 * and footer are placed at their correct physical sizes. A sparkline is the
 * one thing that fills it with information rather than decoration: it turns
 * each screen from a snapshot into a direction. "$1,052 and climbing" is a
 * different fact from "$1,052", and it is the fact an owner actually wants.
 *
 * The hard part is not drawing it. Stripe exposes no historical MRR endpoint,
 * so the device must accumulate its own history, one sample per local day,
 * starting from first boot. That means:
 *
 *   - a fresh device has no trend to show and must say so honestly rather
 *     than drawing a flat line, which would assert stability that was never
 *     measured;
 *   - samples must survive reboots, so the ring buffer lives in NVS;
 *   - a device switched off for a week must not draw those days as zero,
 *     because zero MRR is a catastrophe and an absent sample is not.
 *
 * That last distinction is the whole reason this is a module with tests
 * rather than an array.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/history.h"

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

static void check_i64(const char *what, int64_t got, int64_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %lld, want %lld\n",
               what, (long long)got, (long long)want);
    }
}

static void check_true(const char *what, bool cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/* Day 0 is an arbitrary epoch day number; only differences matter. */
#define D0 20000

/* ---- recording ---- */

static void test_starts_empty(void)
{
    printf("a fresh device has no history and says so\n");

    history_t h;
    history_init(&h);

    check_int("no samples", history_count(&h), 0);
    check_true("not enough to draw a trend", !history_has_trend(&h));
}

static void test_records_one_sample_per_day(void)
{
    printf("one sample per day, later samples replace earlier same-day ones\n");

    history_t h;
    history_init(&h);

    history_record(&h, D0, 100000);
    check_int("first sample stored", history_count(&h), 1);

    /* A second poll on the same day updates rather than appends -- the device
     * polls every ten minutes, so appending would fill the buffer in hours. */
    history_record(&h, D0, 105000);
    check_int("same day does not append", history_count(&h), 1);
    check_i64("same day updates the value", history_latest(&h), 105000);

    history_record(&h, D0 + 1, 110000);
    check_int("next day appends", history_count(&h), 2);
    check_i64("latest is the new day", history_latest(&h), 110000);
}

/*
 * The ring buffer. Older samples fall off the end rather than growing without
 * bound; the window is what the sparkline shows.
 */
static void test_ring_wraps_at_capacity(void)
{
    printf("the buffer holds a fixed window and drops the oldest\n");

    history_t h;
    history_init(&h);

    for (int i = 0; i < HISTORY_DAYS + 10; i++) {
        history_record(&h, D0 + i, 1000 + i);
    }

    check_int("count caps at the window", history_count(&h), HISTORY_DAYS);
    check_i64("latest is the most recent",
              history_latest(&h), 1000 + HISTORY_DAYS + 9);
    check_i64("oldest has been dropped",
              history_oldest(&h), 1000 + 10);
}

/* ---- gaps ---- */

/*
 * The property that makes this worth testing. A device switched off for a
 * week, or one that lost WiFi, has no samples for those days. Recording zero
 * would draw a cliff to the bottom of the chart and read as catastrophic
 * revenue loss. Absent and zero are different facts.
 */
static void test_gaps_are_not_zeros(void)
{
    printf("missing days are absent, never zero\n");

    history_t h;
    history_init(&h);

    history_record(&h, D0, 100000);
    /* Device off for five days. */
    history_record(&h, D0 + 6, 120000);

    check_int("only the two real samples exist", history_count(&h), 2);

    /* Neither stored value is zero, and nothing was invented for the gap. */
    check_i64("oldest is the real first sample", history_oldest(&h), 100000);
    check_i64("latest is the real second sample", history_latest(&h), 120000);

    /* The gap is visible as a day-number jump, so a renderer can space points
     * by date rather than by index if it chooses. */
    check_int("day span covers the gap",
              history_day_span(&h), 6);
}

/*
 * A sample recorded with a day number older than the newest is out of order --
 * possible if the clock steps backwards after an NTP correction. It must not
 * corrupt the series.
 */
static void test_out_of_order_is_ignored(void)
{
    printf("a backwards clock step does not corrupt the series\n");

    history_t h;
    history_init(&h);

    history_record(&h, D0 + 5, 100000);
    history_record(&h, D0 + 2, 999999);   /* clock went backwards */

    check_int("stale sample rejected", history_count(&h), 1);
    check_i64("series unchanged", history_latest(&h), 100000);
}

/* ---- trend readiness ---- */

/*
 * A sparkline drawn from two points is a straight line that asserts a trend
 * nobody measured. The device must decline to draw one until it has enough
 * history to be honest, and the screen falls back to today's number alone.
 */
static void test_trend_needs_enough_points(void)
{
    printf("no trend is drawn until there is enough history\n");

    history_t h;
    history_init(&h);

    for (int i = 0; i < HISTORY_MIN_FOR_TREND - 1; i++) {
        history_record(&h, D0 + i, 100000 + i);
        check_true("not yet enough to draw", !history_has_trend(&h));
    }

    history_record(&h, D0 + HISTORY_MIN_FOR_TREND - 1, 200000);
    check_true("enough now", history_has_trend(&h));
}

/* ---- rendering support ---- */

/*
 * The renderer needs the range to scale the line. A flat series must not
 * divide by zero, and must render as a flat line rather than filling the
 * whole height.
 */
static void test_range_for_scaling(void)
{
    printf("min and max support scaling, including a flat series\n");

    history_t h;
    history_init(&h);

    history_record(&h, D0,     100000);
    history_record(&h, D0 + 1, 150000);
    history_record(&h, D0 + 2,  90000);

    check_i64("min", history_min(&h), 90000);
    check_i64("max", history_max(&h), 150000);

    /* Flat series: min == max, and the renderer must be told so rather than
     * computing a zero-height scale. */
    history_t flat;
    history_init(&flat);
    for (int i = 0; i < 5; i++) {
        history_record(&flat, D0 + i, 100000);
    }
    check_true("flat series is detectable", history_is_flat(&flat));
    check_true("varying series is not flat", !history_is_flat(&h));
}

/*
 * The change over the window is what the subtitle reports -- "+$82 this
 * month". Computed from the real endpoints, not from a fitted line.
 */
static void test_change_over_window(void)
{
    printf("change over the window is first-to-last\n");

    history_t h;
    history_init(&h);

    history_record(&h, D0,     100000);
    history_record(&h, D0 + 1, 110000);
    history_record(&h, D0 + 2, 108200);

    check_i64("net change", history_change(&h), 8200);

    /* A single sample has no change to report. */
    history_t one;
    history_init(&one);
    history_record(&one, D0, 100000);
    check_i64("one sample means no change", history_change(&one), 0);
}

/*
 * history_latest_day exists so a caller restoring a series from flash can ask
 * "is today already recorded?" without indexing the ring itself. The ring
 * wrap is the part worth testing: after HISTORY_DAYS samples the newest is at
 * head-1, not at count-1.
 */
static void test_latest_day(void)
{
    history_t h;
    history_init(&h);

    check_i64("empty series has no latest day", history_latest_day(&h), -1);

    history_record(&h, D0, 100000);
    check_i64("one sample reports its day", history_latest_day(&h), D0);

    history_record(&h, D0 + 1, 101000);
    check_i64("newest day after two", history_latest_day(&h), D0 + 1);

    /* Same day again updates rather than appends, so the day is unchanged. */
    history_record(&h, D0 + 1, 102000);
    check_i64("same-day update keeps the day", history_latest_day(&h), D0 + 1);

    /* Fill past capacity so head wraps, then confirm it still reports the
     * newest rather than whatever sits at the physical end of the array. */
    history_t w;
    history_init(&w);
    for (int i = 0; i < HISTORY_DAYS + 5; i++) {
        history_record(&w, D0 + i, 100000 + i);
    }
    check_i64("after wrap, newest day is the last written",
              history_latest_day(&w), D0 + HISTORY_DAYS + 4);
}

int main(void)
{
    test_starts_empty();
    test_records_one_sample_per_day();
    test_ring_wraps_at_capacity();
    test_gaps_are_not_zeros();
    test_out_of_order_is_ignored();
    test_trend_needs_enough_points();
    test_range_for_scaling();
    test_change_over_window();
    test_latest_day();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
