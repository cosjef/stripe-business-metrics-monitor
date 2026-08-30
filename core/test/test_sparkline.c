/*
 * Sparkline plotting: turning a history series into screen coordinates.
 *
 *   cd firmware/test && make && ./test_sparkline
 *
 * This is the arithmetic half of the sparkline -- mapping N samples onto a
 * pixel rectangle. Drawing is LVGL's job; deciding WHERE the points go is
 * where the bugs live, and it is pure integer maths, so it is tested here.
 *
 * The failure modes worth guarding are all silent ones. A flat series that
 * divides by zero. An off-by-one that clips the newest sample off the right
 * edge -- the single most important point, since it is today. A y-axis that
 * is not inverted, drawing growth as decline, which is worse than drawing
 * nothing because it is confidently wrong.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/history.h"
#include "../include/sparkline.h"

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

static void check_true(const char *what, bool cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

#define D0 20000

/* A plot box matching the C6 layout: full text column, 84px tall. */
static const spark_box_t BOX = { .x = 23, .y = 258, .w = 434, .h = 84 };

/* ---- refusing to draw ---- */

/*
 * The honesty rule, inherited from history.h. Too few samples means no trend
 * was measured, so none is drawn. A two-point line looks authoritative and
 * asserts a direction from noise.
 */
static void test_refuses_without_enough_history(void)
{
    printf("no plot until there is enough history to be honest\n");

    history_t h;
    history_init(&h);

    spark_point_t pts[HISTORY_DAYS];

    check_int("empty series plots nothing",
              spark_plot(&h, &BOX, pts, HISTORY_DAYS), 0);

    for (int i = 0; i < HISTORY_MIN_FOR_TREND - 1; i++) {
        history_record(&h, D0 + i, 100000 + i * 100);
    }
    check_int("still nothing just under the threshold",
              spark_plot(&h, &BOX, pts, HISTORY_DAYS), 0);

    history_record(&h, D0 + HISTORY_MIN_FOR_TREND - 1, 200000);
    check_int("plots once there is a week",
              spark_plot(&h, &BOX, pts, HISTORY_DAYS),
              HISTORY_MIN_FOR_TREND);
}

/* ---- geometry ---- */

/*
 * The newest sample must land exactly on the right edge. It is today's value,
 * the one the hero above is showing, and clipping it by a pixel would break
 * the visual link between the number and the end of its own line.
 */
static void test_endpoints_touch_the_edges(void)
{
    printf("oldest sits at the left edge, newest at the right\n");

    history_t h;
    history_init(&h);
    for (int i = 0; i < 10; i++) {
        history_record(&h, D0 + i, 100000 + i * 1000);
    }

    spark_point_t pts[HISTORY_DAYS];
    const int n = spark_plot(&h, &BOX, pts, HISTORY_DAYS);

    check_int("ten points", n, 10);
    check_int("first x is the left edge", pts[0].x, BOX.x);
    check_int("last x is the right edge", pts[n - 1].x, BOX.x + BOX.w - 1);
}

/*
 * Y is inverted: a bigger value must sit HIGHER on screen, meaning a smaller
 * y. Getting this backwards renders growth as decline -- confidently wrong,
 * which is worse than blank.
 */
static void test_y_axis_is_inverted(void)
{
    printf("larger values plot higher on the screen\n");

    history_t h;
    history_init(&h);
    for (int i = 0; i < 8; i++) {
        history_record(&h, D0 + i, 100000 + i * 1000);   /* rising */
    }

    spark_point_t pts[HISTORY_DAYS];
    const int n = spark_plot(&h, &BOX, pts, HISTORY_DAYS);

    check_true("a rising series ends higher than it started",
               pts[n - 1].y < pts[0].y);
    check_int("the maximum touches the top", pts[n - 1].y, BOX.y);
    check_int("the minimum touches the bottom", pts[0].y, BOX.y + BOX.h - 1);
}

/*
 * Every point must land inside the box. A sample outside it would draw over
 * the hero or the subtitle, and LVGL will happily render it there.
 */
static void test_all_points_stay_inside_the_box(void)
{
    printf("no point escapes the plot rectangle\n");

    history_t h;
    history_init(&h);

    /* Deliberately jagged, including the extremes. */
    const int64_t vals[] = {50000, 900000, 51000, 899000, 100, 999999,
                            400000, 2, 700000, 300000};
    for (int i = 0; i < 10; i++) {
        history_record(&h, D0 + i, vals[i]);
    }

    spark_point_t pts[HISTORY_DAYS];
    const int n = spark_plot(&h, &BOX, pts, HISTORY_DAYS);

    for (int i = 0; i < n; i++) {
        checks++;
        if (pts[i].x < BOX.x || pts[i].x >= BOX.x + BOX.w ||
            pts[i].y < BOX.y || pts[i].y >= BOX.y + BOX.h) {
            failures++;
            printf("  FAIL point %d at (%d,%d) is outside the box\n",
                   i, pts[i].x, pts[i].y);
        }
    }
}

/*
 * A flat series must not divide by zero, and must draw as a flat line through
 * the middle rather than pinned to an edge -- a metric that has not moved is
 * neither at its maximum nor its minimum in any meaningful sense.
 */
static void test_flat_series_draws_a_flat_line(void)
{
    printf("a flat series plots flat, and does not divide by zero\n");

    history_t h;
    history_init(&h);
    for (int i = 0; i < 10; i++) {
        history_record(&h, D0 + i, 100000);
    }

    spark_point_t pts[HISTORY_DAYS];
    const int n = spark_plot(&h, &BOX, pts, HISTORY_DAYS);

    check_int("all points plotted", n, 10);

    const int mid = BOX.y + BOX.h / 2;
    for (int i = 0; i < n; i++) {
        check_int("sits on the midline", pts[i].y, mid);
    }
}

/*
 * Points are evenly spaced by index rather than by date.
 *
 * A deliberate choice: spacing by date would leave a visible void for days the
 * device was off, which reads as a data problem rather than a business one.
 * The line is about the shape of the metric, not the device's uptime, and
 * history_day_span() remains available if that ever needs revisiting.
 */
static void test_points_are_evenly_spaced(void)
{
    printf("points space evenly by index, not by calendar date\n");

    history_t h;
    history_init(&h);
    history_record(&h, D0,      100000);
    history_record(&h, D0 + 1,  110000);
    history_record(&h, D0 + 2,  120000);
    history_record(&h, D0 + 3,  130000);
    history_record(&h, D0 + 4,  140000);
    history_record(&h, D0 + 5,  150000);
    /* A twenty day gap, then more samples. */
    history_record(&h, D0 + 25, 160000);
    history_record(&h, D0 + 26, 170000);

    spark_point_t pts[HISTORY_DAYS];
    const int n = spark_plot(&h, &BOX, pts, HISTORY_DAYS);

    const int step = pts[1].x - pts[0].x;
    for (int i = 2; i < n; i++) {
        /* Integer division makes steps differ by at most one pixel. */
        const int d = pts[i].x - pts[i - 1].x;
        checks++;
        if (d < step - 1 || d > step + 1) {
            failures++;
            printf("  FAIL step %d is %dpx, expected about %dpx\n", i, d, step);
        }
    }
}

/*
 * The caller's buffer bounds the output. A series longer than the buffer must
 * be refused rather than overrun it.
 */
static void test_respects_the_output_buffer(void)
{
    printf("a short output buffer is refused, not overrun\n");

    history_t h;
    history_init(&h);
    for (int i = 0; i < 20; i++) {
        history_record(&h, D0 + i, 100000 + i);
    }

    spark_point_t small[5];
    check_int("refuses rather than writing past the end",
              spark_plot(&h, &BOX, small, 5), 0);
}

int main(void)
{
    test_refuses_without_enough_history();
    test_endpoints_touch_the_edges();
    test_y_axis_is_inverted();
    test_all_points_stay_inside_the_box();
    test_flat_series_draws_a_flat_line();
    test_points_are_evenly_spaced();
    test_respects_the_output_buffer();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
