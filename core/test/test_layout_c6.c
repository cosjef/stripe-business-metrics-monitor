/*
 * The C6 layout constants, derived rather than typed.
 *
 *   cd firmware/test && make && ./test_layout_c6
 *
 * layout.h now selects a constant block by target. This file asserts that the
 * C6 block is internally consistent and correctly derived -- catching the two
 * ways a hand-typed port goes wrong: a size that breaks the legibility floor,
 * and a composition where zones overlap.
 *
 * The central rule, and the one worth stating loudly:
 *
 *     POSITIONS scale with the panel (2x). TYPE scales physically (1.43x).
 *
 * Positions are composition -- the three-zone skeleton must keep its shape or
 * rotation stops reading as one instrument changing state (spec 5.1). Type is
 * legibility -- it must stay the same physical height or it breaks spec 2.2's
 * millimetre floor. Applying either rule to both is wrong, in opposite
 * directions, and both mistakes look plausible on a spec sheet.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/geometry.h"
#include "../include/hero_size.h"
#include "../include/layout_c6.h"
/*
 * The 240x240 panel's sizes, inlined.
 *
 * This suite exists to prove the C6 values were DERIVED from those originals
 * rather than guessed, so it needs them even though that board is gone. They
 * are inlined rather than kept in a header for a device that no longer
 * exists: the numbers are historical constants now, not configuration.
 */
#define S3_PANEL_PX        240
#define S3_PAD_PX          16
#define S3_SIZE_LABEL      20
#define S3_SIZE_SUBTITLE   22
#define S3_SIZE_FOOTER     18
#define S3_SIZE_HERO_MAX   96
#define S3_SIZE_HERO_MIN   24
#define S3_TEXT_COLUMN_PX      208
#define S3_LABEL_BASELINE_Y     16
#define S3_HERO_BASELINE_Y     150
#define S3_SUBTITLE_BASELINE_Y 178
#define S3_FOOTER_BASELINE_Y   210
#define S3_DOTS_CENTER_Y       214
#define S3_DOTS_RADIUS           4
#define S3_DOTS_GAP             17
#define S3_LEGIBILITY_FLOOR_PX  24
#define S3_ABSOLUTE_FLOOR_PX    20

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

/* ---- type sizes: physical, not proportional ---- */

static void test_type_sizes_are_physically_derived(void)
{
    printf("type sizes preserve physical height (spec 2.2)\n");

    check_int("SIZE_LABEL 20 -> 29",
              C6_SIZE_LABEL,
              geom_translate_px(S3_SIZE_LABEL, &GEOM_PANEL_S3, &GEOM_PANEL_C6));
    check_int("SIZE_SUBTITLE 22 -> 31",
              C6_SIZE_SUBTITLE,
              geom_translate_px(S3_SIZE_SUBTITLE, &GEOM_PANEL_S3, &GEOM_PANEL_C6));
    check_int("SIZE_FOOTER 18 -> 26",
              C6_SIZE_FOOTER,
              geom_translate_px(S3_SIZE_FOOTER, &GEOM_PANEL_S3, &GEOM_PANEL_C6));
    check_int("SIZE_HERO_MAX 96 -> 137",
              C6_SIZE_HERO_MAX,
              geom_translate_px(S3_SIZE_HERO_MAX, &GEOM_PANEL_S3, &GEOM_PANEL_C6));

    /*
     * The two traps, asserted by their physical consequence rather than by
     * "is not equal to 192", which almost any wrong value satisfies.
     */
    check_true("the chosen hero is within 0.1mm of the S3's physical height",
               geom_px_to_mm(C6_SIZE_HERO_MAX, &GEOM_PANEL_C6) -
               geom_px_to_mm(S3_SIZE_HERO_MAX, &GEOM_PANEL_S3) < 0.1);
    check_true("doubling would be more than 4mm too tall",
               geom_px_to_mm(2 * S3_SIZE_HERO_MAX, &GEOM_PANEL_C6) -
               geom_px_to_mm(S3_SIZE_HERO_MAX, &GEOM_PANEL_S3) > 4.0);
    check_true("copying would be more than 3mm too short",
               geom_px_to_mm(S3_SIZE_HERO_MAX, &GEOM_PANEL_S3) -
               geom_px_to_mm(S3_SIZE_HERO_MAX, &GEOM_PANEL_C6) > 3.0);
}

/*
 * Every size a user must read has to clear spec 2.2's floor on the C6 panel,
 * which is a different pixel value than on the S3.
 */
static void test_every_size_clears_its_floor(void)
{
    printf("every readable size clears the physical floor on the C6\n");

    check_true("SIZE_LABEL clears the absolute floor",
               geom_meets_absolute_floor(C6_SIZE_LABEL, &GEOM_PANEL_C6));
    check_true("SIZE_SUBTITLE clears the absolute floor",
               geom_meets_absolute_floor(C6_SIZE_SUBTITLE, &GEOM_PANEL_C6));
    check_true("SIZE_HERO_MIN clears the legibility floor",
               geom_meets_legibility_floor(C6_SIZE_HERO_MIN, &GEOM_PANEL_C6));
    check_true("SIZE_HERO_MAX clears it comfortably",
               geom_meets_legibility_floor(C6_SIZE_HERO_MAX, &GEOM_PANEL_C6));

    /*
     * The footer sits below BOTH floors, deliberately, and does so on the S3
     * as well: 18px there is 2.07mm, under the 2.3mm absolute floor. layout.h
     * already documents this as intentional for text the user never needs to
     * read at a glance (firmware version, retry status).
     *
     * An earlier version of this test asserted the footer cleared the
     * absolute floor. It does not, and never did on either panel -- the
     * assertion was wrong, not the constant.
     */
    check_true("SIZE_FOOTER is deliberately below the legibility floor",
               !geom_meets_legibility_floor(C6_SIZE_FOOTER, &GEOM_PANEL_C6));
    check_true("SIZE_FOOTER is below the absolute floor too, by design",
               !geom_meets_absolute_floor(C6_SIZE_FOOTER, &GEOM_PANEL_C6));

    /* The same exception holds on the S3, so this is a property of the design
     * rather than something the new panel introduced. */
    check_true("footer is below the legibility floor on the S3 too",
               !geom_meets_legibility_floor(S3_SIZE_FOOTER, &GEOM_PANEL_S3));
    check_true("and below the absolute floor on the S3 too",
               !geom_meets_absolute_floor(S3_SIZE_FOOTER, &GEOM_PANEL_S3));
}

/* ---- positions: proportional, not physical ---- */

static void test_positions_scale_with_the_panel(void)
{
    printf("baselines scale 2x to preserve the three-zone composition\n");

    check_int("LABEL_BASELINE_Y doubles",    C6_LABEL_BASELINE_Y,    2 * S3_LABEL_BASELINE_Y);
    check_int("HERO_BASELINE_Y doubles",     C6_HERO_BASELINE_Y,     2 * S3_HERO_BASELINE_Y);
    check_int("SUBTITLE_BASELINE_Y doubles", C6_SUBTITLE_BASELINE_Y, 2 * S3_SUBTITLE_BASELINE_Y);
    check_int("FOOTER_BASELINE_Y doubles",   C6_FOOTER_BASELINE_Y,   2 * S3_FOOTER_BASELINE_Y);
    check_int("DOTS_CENTER_Y doubles",       C6_DOTS_CENTER_Y,       2 * S3_DOTS_CENTER_Y);

    /*
     * Positions scale by 2.00 and type by ~1.43. Asserted as actual ratios so
     * that collapsing both to one factor -- the "simplification" this file
     * exists to prevent -- fails here rather than passing an inequality.
     */
    const int pos_ratio_x100 = (C6_HERO_BASELINE_Y * 100) / S3_HERO_BASELINE_Y;
    const int type_ratio_x100 = (C6_SIZE_HERO_MAX * 100) / S3_SIZE_HERO_MAX;
    check_int("positions scale exactly 2.00x", pos_ratio_x100, 200);
    check_true("type scales ~1.43x, not 2x",
               type_ratio_x100 > 140 && type_ratio_x100 < 146);
}

/* ---- the composition must not collide ---- */

/*
 * The reason positions get their own rule. With a 137px hero at a doubled
 * baseline, nothing may overlap.
 */
static void test_zones_do_not_overlap(void)
{
    printf("the three zones stay clear of each other at C6 sizes\n");

    /* Cap height is roughly 72% of the em for this face; the hero ascends
     * that far above its baseline. */
    const int hero_top = C6_HERO_BASELINE_Y - (C6_SIZE_HERO_MAX * 72) / 100;
    const int label_bottom = C6_LABEL_BASELINE_Y + C6_SIZE_LABEL;

    check_true("hero clears the label", hero_top > label_bottom);
    check_true("subtitle baseline is below the hero baseline",
               C6_SUBTITLE_BASELINE_Y > C6_HERO_BASELINE_Y);
    check_true("subtitle has room above the footer",
               C6_FOOTER_BASELINE_Y - C6_SUBTITLE_BASELINE_Y >= C6_SIZE_SUBTITLE);
    check_true("dots sit below the footer baseline",
               C6_DOTS_CENTER_Y > C6_FOOTER_BASELINE_Y);
    check_true("dots fit on screen",
               C6_DOTS_CENTER_Y + C6_DOTS_RADIUS < 480);
    check_true("footer fits above the dots",
               C6_FOOTER_BASELINE_Y < C6_DOTS_CENTER_Y);
}

/* ---- the text column ---- */

static void test_text_column(void)
{
    printf("the text column grows faster than the type\n");

    check_int("PAD_PX 16 -> 23 (physical)", C6_PAD_PX,
              geom_translate_px(S3_PAD_PX, &GEOM_PANEL_S3, &GEOM_PANEL_C6));
    check_int("C6 text column is 434px", C6_TEXT_COLUMN_PX, 480 - 2 * C6_PAD_PX);
    check_int("S3 text column is 208px", S3_TEXT_COLUMN_PX, 240 - 2 * S3_PAD_PX);

    /*
     * This is the headroom that matters. The column more than doubles while
     * type grows only 1.43x, so strings that overflowed on the S3 now fit --
     * which is what lets the ARR decimal come back (see format.c's 10k rule).
     */
    check_true("column grows more than type does",
               (C6_TEXT_COLUMN_PX * S3_SIZE_HERO_MAX) >
               (C6_SIZE_HERO_MAX * S3_TEXT_COLUMN_PX));
}

/*
 * The concrete payoff, stated as a test so it is not just an assertion in a
 * planning document: "$11.2k" at full hero size overflowed 208px on the S3 and
 * fits 434px on the C6.
 */
static void test_arr_decimal_fits_again(void)
{
    printf("the ARR decimal fits at full hero size on the C6\n");

    /*
     * Measured through hero_size.c rather than hardcoded, so this test breaks
     * if the advance table or the width calculation ever drifts. An earlier
     * version used literals (268/382/455); they were correct, but a test that
     * cannot notice the code changing underneath it is not guarding anything.
     */
    const int s3_width_at_96  = text_width_px("$11.2k", S3_SIZE_HERO_MAX);
    const int c6_width_at_137 = text_width_px("$11.2k", C6_SIZE_HERO_MAX);

    check_true("'$11.2k' overflowed the S3 column",
               s3_width_at_96 > S3_TEXT_COLUMN_PX);
    check_true("'$11.2k' fits the C6 column",
               c6_width_at_137 <= C6_TEXT_COLUMN_PX);

    /*
     * Exact cents still do NOT fit at full hero size.
     *
     * I previously claimed "$970.33 exact is viable" on the C6. That was based
     * on a 416px column and a 96px measurement, and it does not survive the
     * correct derivation: at the real hero cap of 137px, "$970.33" is 455px
     * against a 434px column. It would need to drop to ~130px, one step down
     * the ladder.
     *
     * So the ARR decimal comes back (that fits), but exact cents remain a
     * deliberate omission rather than a newly available option.
     */
    const int c6_exact_cents = text_width_px("$970.33", C6_SIZE_HERO_MAX);
    check_true("'$970.33' still overflows at full hero size",
               c6_exact_cents > C6_TEXT_COLUMN_PX);
    check_true("...though it fits one size step down (130px)",
               text_width_px("$970.33", 130) <= C6_TEXT_COLUMN_PX);
}

/*
 * Nothing may hardcode a panel dimension.
 *
 * draw_dots() centred on a literal 240, so on the 480px panel the dots landed
 * around x=120 and spilled off the left edge as one wide block. The constant
 * existed; the drawing code just did not use it.
 */
static void test_panel_width_is_a_constant(void)
{
    printf("panel width comes from the layout, not a literal\n");

    check_int("C6 panel is 480", C6_PANEL_PX, 480);
    check_int("S3 panel is 240", S3_PANEL_PX, 240);

    /* The dot row must fit inside the panel it is drawn on. */
    const int span = (10 - 1) * C6_DOTS_GAP;   /* worst case: 10 screens */
    const int x0 = (C6_PANEL_PX - span) / 2;
    check_true("ten dots start inside the left edge", x0 >= 0);
    check_true("and end inside the right edge",
               x0 + span + C6_DOTS_RADIUS <= C6_PANEL_PX);
}

int main(void)
{
    test_type_sizes_are_physically_derived();
    test_every_size_clears_its_floor();
    test_positions_scale_with_the_panel();
    test_zones_do_not_overlap();
    test_text_column();
    test_arr_decimal_fits_again();
    test_panel_width_is_a_constant();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
