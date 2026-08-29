/*
 * Pixel-level tests for the screen deck (spec 6), rendered through real LVGL
 * into an offscreen framebuffer.
 *
 *   cd firmware/test && make && ./test_screens
 *
 * These assert what a photograph of the device would show: background color,
 * where ink actually lands, which colors are used, and how many rotation dots
 * are filled. Every visual bug in Stage 1 was found by eye; this is the
 * automated replacement.
 *
 * On failure a PPM of the offending screen is written to /tmp for inspection.
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"

#include "../main/layout.h"
#include "../main/screens.h"
#include "../main/hero_size.h"

static int failures = 0;
static int checks = 0;
static const char *current_screen = "?";

static void fail(const char *what, const char *detail)
{
    failures++;
    printf("  FAIL [%s] %s: %s\n", current_screen, what, detail);

    char path[128];
    snprintf(path, sizeof(path), "/tmp/fail_%s.ppm", current_screen);
    harness_dump_ppm(path);
}

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        fail(what, "expected true");
    }
}

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        char d[96];
        snprintf(d, sizeof(d), "got %d, want %d", got, want);
        fail(what, d);
    }
}

static void check_between(const char *what, int got, int lo, int hi)
{
    checks++;
    if (got < lo || got > hi) {
        char d[96];
        snprintf(d, sizeof(d), "got %d, want %d..%d", got, lo, hi);
        fail(what, d);
    }
}

/* RGB565 quantization means an exact match is the wrong test. */
#define TOL 6

/* ---- shared expectations every screen must meet ---- */

/*
 * The field must be the palette background. This is the check that would have
 * caught the #121211 bug, the inverted colors, and the LVGL theme painting
 * over the screen -- all of which took five attempts to find by eye.
 */
static void expect_background(void)
{
    /* Sample corners and center, away from any text. */
    const int pts[][2] = {{2, 2}, {237, 2}, {2, 237}, {237, 237}, {120, 60}};

    for (size_t i = 0; i < sizeof(pts) / sizeof(pts[0]); i++) {
        char what[64];
        snprintf(what, sizeof(what), "background at (%d,%d)", pts[i][0], pts[i][1]);
        checks++;
        if (!harness_color_near(harness_pixel(pts[i][0], pts[i][1]), COLOR_BG, TOL)) {
            char d[96];
            snprintf(d, sizeof(d), "got 0x%06X, want 0x%06X",
                     harness_pixel(pts[i][0], pts[i][1]), (unsigned)COLOR_BG);
            fail(what, d);
        }
    }
}

/* Nothing may be drawn outside the 16px padding (spec 5.1). */
static void expect_padding_respected(void)
{
    int x0, y0, x1, y1;
    if (!harness_ink_bounds(0, 0, 240, 240, COLOR_BG, TOL, &x0, &y0, &x1, &y1)) {
        check_true("something was drawn", 0);
        return;
    }

    checks++;
    if (x0 < PAD_PX) {
        char d[96];
        snprintf(d, sizeof(d), "ink starts at x=%d, left padding is %d", x0, PAD_PX);
        fail("left padding respected", d);
    }

    checks++;
    if (x1 > 240 - PAD_PX) {
        char d[128];
        snprintf(d, sizeof(d), "ink ends at x=%d, right edge of column is %d",
                 x1, 240 - PAD_PX);
        fail("right padding respected / column not overflowed", d);
    }
}

/*
 * Text with the given baseline must have its ink sitting on that line.
 * Catches the LVGL-top-left vs spec-baseline confusion.
 *
 * Tolerance below the baseline is generous because glyphs legitimately descend
 * past it: in Roboto Condensed Bold '$' extends ~9% of the em below the digit
 * baseline (measured: '$' spans y=9..91 at 88px where digits span 19..83), and
 * 'y' in "today" descends further still. Ink appearing well ABOVE the baseline
 * is the real failure -- that means the text was positioned by its top edge
 * instead of its baseline.
 */
static void expect_text_at_baseline(const char *what, int band_y0, int band_y1,
                                    int baseline_y, int max_descent)
{
    int x0, y0, x1, y1;
    if (!harness_ink_bounds(0, band_y0, 240, band_y1 - band_y0,
                            COLOR_BG, TOL, &x0, &y0, &x1, &y1)) {
        check_true(what, 0);
        return;
    }

    checks++;
    if (y1 > baseline_y + max_descent) {
        char d[128];
        snprintf(d, sizeof(d), "ink bottom at y=%d, more than %dpx below baseline %d",
                 y1, max_descent, baseline_y);
        fail(what, d);
    }

    checks++;
    if (y1 < baseline_y - 14) {
        char d[128];
        snprintf(d, sizeof(d), "ink bottom at y=%d, well above baseline %d "
                 "(positioned by top edge instead of baseline?)", y1, baseline_y);
        fail(what, d);
    }
}

static int count_color(uint32_t rgb)
{
    return harness_count_near(0, 0, 240, 240, rgb, TOL);
}

/* ---- rotation screens ---- */

static void test_mrr_screen(void)
{
    current_screen = "mrr";
    printf("MRR screen (spec 6.1)\n");

    const screen_data_t d = {
        .label = "MRR",
        .hero = "$6.5k",
        .subtitle = "+$118 today",
        .hero_is_gain = 0,
        .subtitle_is_gain = 1,
        .dot_index = 0,
        .dot_count = 6,
    };
    screen_draw_rotation(harness_screen(), &d);
    harness_render();

    expect_background();
    expect_padding_respected();

    /* Each element is checked inside a band that contains only itself.
     * Descent budgets scale with size: '$' descends ~9px at 88px, 'y' ~5px
     * at 22px. Bands stop short of the next element's ink so one element's
     * descender is not mistaken for the next element's body. */
    expect_text_at_baseline("label at its baseline", 0, 44, LABEL_BASELINE_Y + 20, 6);
    expect_text_at_baseline("hero at baseline y=150", 60, 160, HERO_BASELINE_Y, 10);
    expect_text_at_baseline("subtitle at baseline y=178", 161, 200,
                            SUBTITLE_BASELINE_Y, 6);

    /* The hero is primary (not green -- MRR is a level, not a realized gain). */
    check_true("hero rendered in primary", count_color(COLOR_PRIMARY) > 200);

    /* The subtitle IS a realized gain, so it is green (spec 4.2). */
    check_true("subtitle rendered in green", count_color(COLOR_GREEN) > 40);

    /* Label is muted. */
    check_true("label rendered in muted", count_color(COLOR_MUTED) > 20);
}

/*
 * The dots are the rotation position indicator. This is the check that would
 * have caught them being invisible after remove_style_all() reset bg_opa.
 */
static void test_rotation_dots(void)
{
    current_screen = "dots";
    printf("rotation dots (spec 6.1)\n");

    for (int active = 0; active < 6; active++) {
        const screen_data_t d = {
            .label = "MRR", .hero = "$6.5k", .subtitle = "+$118 today",
            .dot_index = active, .dot_count = 6,
        };
        screen_draw_rotation(harness_screen(), &d);
        harness_render();

        /* Look only at the dot band. */
        const int band_y = DOTS_CENTER_Y - DOTS_RADIUS - 1;
        const int band_h = DOTS_RADIUS * 2 + 2;

        const int lit = harness_count_near(0, band_y, 240, band_h, COLOR_PRIMARY, TOL);
        const int dim = harness_count_near(0, band_y, 240, band_h, COLOR_INACTIVE, TOL);

        char what[64];

        /* Exactly one dot is filled; the other five are inactive. Assert on
         * area rather than exact pixel counts, which antialiasing perturbs. */
        snprintf(what, sizeof(what), "active dot %d is visible", active);
        check_true(what, lit > 20);

        snprintf(what, sizeof(what), "with dot %d active, 5 inactive dots visible", active);
        check_true(what, dim > 100);

        /* The inactive dots together must outweigh the single active one. */
        snprintf(what, sizeof(what), "dot %d: inactive area exceeds active", active);
        check_true(what, dim > lit);
    }
}

/*
 * The hero must be sized from its own width, never hardcoded (spec 2.4), and
 * a large value must not overflow the column.
 */
static void test_hero_sizing_on_screen(void)
{
    current_screen = "hero_sizing";
    printf("hero auto-sizing renders within the column\n");

    const char *values[] = {"2", "94", "$6.5k", "$145k", "$1.45M", "$1,234,567"};

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        const screen_data_t d = {
            .label = "MRR", .hero = values[i], .subtitle = "today",
            .dot_index = 0, .dot_count = 6,
        };
        screen_draw_rotation(harness_screen(), &d);
        harness_render();

        int x0, y0, x1, y1;
        char what[96];

        /* Look at the hero band only. */
        if (!harness_ink_bounds(0, 60, 240, 100, COLOR_BG, TOL, &x0, &y0, &x1, &y1)) {
            snprintf(what, sizeof(what), "'%s' drew something", values[i]);
            check_true(what, 0);
            continue;
        }

        snprintf(what, sizeof(what), "'%s' starts at the left padding", values[i]);
        check_between(what, x0, PAD_PX - 2, PAD_PX + 8);

        snprintf(what, sizeof(what), "'%s' fits inside the 208px column", values[i]);
        check_true(what, x1 <= 240 - PAD_PX);
    }
}

/*
 * Green means exactly one thing: realized positive movement (spec 4.2).
 * A screen that sets neither gain flag must contain no green at all.
 */
static void test_green_discipline(void)
{
    current_screen = "green";
    printf("green appears only for realized gains (spec 4.2)\n");

    const screen_data_t neutral = {
        .label = "TRIALS", .hero = "11", .subtitle = "3 end this week",
        .hero_is_gain = 0, .subtitle_is_gain = 0,
        .dot_index = 3, .dot_count = 6,
    };
    screen_draw_rotation(harness_screen(), &neutral);
    harness_render();
    check_int("trials screen has no green", count_color(COLOR_GREEN), 0);

    const screen_data_t gain = {
        .label = "NEW PAID", .hero = "2", .subtitle = "today, $58 MRR",
        .hero_is_gain = 1, .subtitle_is_gain = 0,
        .dot_index = 1, .dot_count = 6,
    };
    screen_draw_rotation(harness_screen(), &gain);
    harness_render();
    check_true("new-paid hero is green", count_color(COLOR_GREEN) > 200);
}

/*
 * Red is reserved for threshold breaches (spec 4.2), which in this deck means
 * exactly one screen: FAILED. Everything else must contain none.
 */
static void test_red_discipline(void)
{
    current_screen = "red";
    printf("red appears only on the alert screen (spec 4.2)\n");

    const screen_data_t alert = {
        .label = "FAILED", .hero = "$29", .subtitle = "1 payment, retrying",
        .hero_is_alert = 1,
        .dot_index = 3, .dot_count = 8,
    };
    screen_draw_rotation(harness_screen(), &alert);
    harness_render();
    check_true("alert hero is red", count_color(COLOR_RED) > 100);
    check_int("no green on an alert", count_color(COLOR_GREEN), 0);

    /* An ordinary screen must contain no red at all. */
    const screen_data_t normal = {
        .label = "MRR", .hero = "$941", .subtitle = "",
        .dot_index = 0, .dot_count = 8,
    };
    screen_draw_rotation(harness_screen(), &normal);
    harness_render();
    check_int("no red on a normal screen", count_color(COLOR_RED), 0);

    /* A gain screen is green, never red. */
    const screen_data_t gain = {
        .label = "NEW PAID", .hero = "2", .subtitle = "today",
        .hero_is_gain = 1,
        .dot_index = 1, .dot_count = 8,
    };
    screen_draw_rotation(harness_screen(), &gain);
    harness_render();
    check_int("no red on a gain", count_color(COLOR_RED), 0);
    check_true("gain is green", count_color(COLOR_GREEN) > 100);

    /* If both flags are somehow set, the alert wins: a breach is the more
     * important thing to say. */
    const screen_data_t both = {
        .label = "ODD", .hero = "$1", .subtitle = "",
        .hero_is_gain = 1, .hero_is_alert = 1,
        .dot_index = 0, .dot_count = 8,
    };
    screen_draw_rotation(harness_screen(), &both);
    harness_render();
    check_true("alert outranks gain", count_color(COLOR_RED) > 0);
    check_int("and green is not used", count_color(COLOR_GREEN), 0);
}

/*
 * The low battery screen. Not in the original spec -- added with the cell.
 *
 * Amber for low, red for critical, matching how the deck already separates a
 * degraded state from a threshold breach (spec 4.2).
 */
static void test_battery_screen(void)
{
    current_screen = "battery";
    printf("low battery screen\n");

    screen_draw_battery(harness_screen(), "18%", "plug in soon", "3.42V", 0);
    harness_render();

    expect_background();
    expect_padding_respected();
    check_true("low battery uses amber", count_color(COLOR_AMBER) > 100);
    check_int("low battery uses no red", count_color(COLOR_RED), 0);

    /* Critical escalates to red. */
    screen_draw_battery(harness_screen(), "4%", "shutting down soon", "3.28V", 1);
    harness_render();

    expect_background();
    expect_padding_respected();
    check_true("critical battery uses red", count_color(COLOR_RED) > 100);
    check_int("critical battery drops amber", count_color(COLOR_AMBER), 0);

    /* Never green -- a dying battery is not a gain under any reading. */
    check_int("no green on a battery warning", count_color(COLOR_GREEN), 0);
}

/*
 * Redrawing must not rebuild the object tree.
 *
 * screen_draw_rotation originally called lv_obj_clean() and recreated every
 * label on each draw. On the C6's QSPI AMOLED that destroyed the display's
 * bound state: the first frame rendered and every later one was discarded
 * while every call still reported success. It presented as a driver fault, a
 * locking fault, a task fault and a buffer fault in turn.
 *
 * Proven on hardware: a build that created labels once and only changed their
 * text updated correctly every five seconds, where the rebuilding version
 * froze on frame one.
 *
 * So the invariant is that the second and subsequent draws reuse objects. A
 * changing child count means the tree is being rebuilt.
 */
static void test_redraw_reuses_objects(void)
{
    current_screen = "reuse";
    printf("redrawing updates objects instead of recreating them\n");

    const screen_data_t a = {
        .label = "MRR", .hero = "$1.1k", .subtitle = "+$82 today",
        .dot_index = 0, .dot_count = 7,
    };
    const screen_data_t b = {
        .label = "ANNUAL RUN RATE", .hero = "$13k", .subtitle = "",
        .dot_index = 1, .dot_count = 7,
    };

    screen_draw_rotation(harness_screen(), &a);
    harness_render();
    const uint32_t after_first = lv_obj_get_child_count(harness_screen());
    check_true("first draw creates objects", after_first > 0);

    screen_draw_rotation(harness_screen(), &b);
    harness_render();
    const uint32_t after_second = lv_obj_get_child_count(harness_screen());

    check_int("second draw reuses the same objects",
              (int)after_second, (int)after_first);

    /* And the content actually changed. */
    check_true("the new label is on screen", count_color(COLOR_MUTED) > 20);
    check_true("the new hero is on screen", count_color(COLOR_PRIMARY) > 200);

    /* A third draw with a different dot count must still not grow the tree
     * unboundedly -- conditional screens change how many dots are shown. */
    const screen_data_t c = {
        .label = "ARPU", .hero = "$33", .subtitle = "",
        .dot_index = 2, .dot_count = 5,
    };
    screen_draw_rotation(harness_screen(), &c);
    harness_render();
    check_true("a different dot count does not grow the tree",
               lv_obj_get_child_count(harness_screen()) <= after_first);
}

/* ---- state screens ---- */

/*
 * State A: stale. The most important screen in the deck -- a confidently
 * displayed stale number is worse than an obviously stale one (spec 6.2).
 */
static void test_stale_screen(void)
{
    current_screen = "stale";
    printf("State A: stale (spec 6.2)\n");

    screen_draw_stale(harness_screen(), "MRR", "$6.5k", "stale | 22 min", "retrying");
    harness_render();

    expect_background();
    expect_padding_respected();

    /* The age must be amber -- the one signal that the number is not live. */
    check_true("age shown in amber", count_color(COLOR_AMBER) > 40);

    /* The hero must be DIMMED, not primary: it is no longer trustworthy. */
    check_int("stale hero is not primary white", count_color(COLOR_PRIMARY), 0);
    check_true("stale hero rendered in muted", count_color(COLOR_MUTED) > 200);

    /* No green anywhere -- nothing here is a realized gain. */
    check_int("no green on a stale screen", count_color(COLOR_GREEN), 0);
}

static void test_auth_error_screen(void)
{
    current_screen = "auth";
    printf("State B: no access (spec 6.2)\n");

    screen_draw_auth_error(harness_screen(), "Stripe key", "rejected",
                           "check permissions", "err 401");
    harness_render();

    expect_background();
    expect_padding_respected();

    /* Amber label marks the degraded state (spec 4.2). */
    check_true("label shown in amber", count_color(COLOR_AMBER) > 20);

    /* Plain-language message for the user, in primary. */
    check_true("message rendered in primary", count_color(COLOR_PRIMARY) > 200);

    check_int("no green on an error screen", count_color(COLOR_GREEN), 0);
}

static void test_setup_screen(void)
{
    current_screen = "setup";
    printf("State C: setup (spec 6.2)\n");

    screen_draw_setup(harness_screen(), "Join wifi", "Setup-4C21",
                      "then open browser", "v1.0.3");
    harness_render();

    expect_background();
    expect_padding_respected();

    /* The SSID is the actionable element and is highlighted green. */
    check_true("SSID rendered in green", count_color(COLOR_GREEN) > 100);

    /* Instruction text in primary. */
    check_true("instruction in primary", count_color(COLOR_PRIMARY) > 100);
}

/*
 * The skeleton must not move between screens: that is what makes rotation feel
 * like one instrument changing state rather than four screens flashing by
 * (spec 5.1). Assert the label and hero bands land identically across screens.
 */
static void test_skeleton_is_stable(void)
{
    current_screen = "skeleton";
    printf("three-zone skeleton is identical across screens (spec 5.1)\n");

    const screen_data_t screens[] = {
        {"MRR",       "$6.5k", "+$118 today",   0, 1, 0, 6},
        {"PAID SUBS", "94",    "+7 this month", 0, 1, 2, 6},
        {"TRIALS",    "11",    "3 end this week", 0, 0, 3, 6},
    };

    int first_label_y1 = -1;

    for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++) {
        screen_draw_rotation(harness_screen(), &screens[i]);
        harness_render();

        int x0, y0, x1, y1;
        char what[96];

        /* Label band. */
        if (!harness_ink_bounds(0, 0, 240, 44, COLOR_BG, TOL, &x0, &y0, &x1, &y1)) {
            snprintf(what, sizeof(what), "screen %zu drew a label", i);
            check_true(what, 0);
            continue;
        }

        snprintf(what, sizeof(what), "screen %zu label starts at left padding", i);
        check_between(what, x0, PAD_PX - 2, PAD_PX + 4);

        if (first_label_y1 < 0) {
            first_label_y1 = y1;
        } else {
            snprintf(what, sizeof(what), "screen %zu label baseline matches screen 0", i);
            check_between(what, y1, first_label_y1 - 2, first_label_y1 + 2);
        }
    }
}

int main(void)
{
    printf("screen rendering tests (LVGL host harness)\n\n");

    harness_init();

    test_mrr_screen();
    test_rotation_dots();
    test_hero_sizing_on_screen();
    test_green_discipline();
    test_red_discipline();
    test_battery_screen();
    test_redraw_reuses_objects();
    test_stale_screen();
    test_auth_error_screen();
    test_setup_screen();
    test_skeleton_is_stable();

    printf("\n%d checks, %d failures\n", checks, failures);
    if (failures) {
        printf("(failed screens dumped to /tmp/fail_*.ppm)\n");
    }
    return failures == 0 ? 0 : 1;
}
