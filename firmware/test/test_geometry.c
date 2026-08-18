/*
 * Display geometry: deriving layout constants for a panel from physical size.
 *
 *   cd firmware/test && make && ./test_geometry
 *
 * The spec states its legibility floor in MILLIMETRES at 50cm (spec 2.2), not
 * in pixels. The pixel values in layout.h are that physical requirement
 * resolved against one specific panel: 240x240 at 1.54", about 220 PPI.
 *
 * The C6 panel is 480x480 at 2.16" -- 2x the pixels but only 1.4x the physical
 * size, so roughly 1.43x the density. That distinction is the whole point of
 * this file. "Double every constant" would render text 1.4x physically larger
 * than the spec calls for, and "keep every constant" would render it 30%
 * smaller and break the legibility floor. Neither is right; the sizes have to
 * be re-derived from millimetres.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../main/geometry.h"

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

static void check_near(const char *what, double got, double want, double tol)
{
    checks++;
    if (got < want - tol || got > want + tol) {
        failures++;
        printf("  FAIL %s: got %.2f, want %.2f +/- %.2f\n", what, got, want, tol);
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

/* ---- density ---- */

/*
 * The spec computes 220 PPI for the S3 panel (spec 2.1: "240 px / 27.7 mm").
 * If this drifts, every size derived from it is wrong.
 */
static void test_panel_density(void)
{
    printf("panel density derived from diagonal and resolution\n");

    check_near("S3 1.54in 240x240 is ~220 PPI",
               geom_ppi(&GEOM_PANEL_S3), 220.0, 2.0);
    check_near("C6 2.16in 480x480 is ~314 PPI",
               geom_ppi(&GEOM_PANEL_C6), 314.0, 2.0);

    /* The trap this file exists to prevent. */
    check_near("C6 is only 1.43x denser, not 2x",
               geom_ppi(&GEOM_PANEL_C6) / geom_ppi(&GEOM_PANEL_S3), 1.43, 0.02);

    /* The spec's own figure for the S3, stated in mm. */
    check_near("S3 usable side is 27.7mm (spec 2.1)",
               geom_side_mm(&GEOM_PANEL_S3), 27.7, 0.3);
    check_near("C6 usable side is 38.8mm",
               geom_side_mm(&GEOM_PANEL_C6), 38.8, 0.3);
}

/* ---- physical size conversion ---- */

/*
 * The spec's legibility table (2.2) maps pixel sizes to physical heights on
 * the S3 panel. Those pairings are the ground truth this file preserves.
 */
static void test_spec_legibility_table(void)
{
    printf("spec 2.2's pixel/mm pairings hold for the S3 panel\n");

    check_near("24px is 2.8mm (minimum viable)",
               geom_px_to_mm(24, &GEOM_PANEL_S3), 2.8, 0.15);
    check_near("20px is 2.3mm (marginal)",
               geom_px_to_mm(20, &GEOM_PANEL_S3), 2.3, 0.15);
    check_near("60px is 6.9mm (glanceable)",
               geom_px_to_mm(60, &GEOM_PANEL_S3), 6.9, 0.2);
    check_near("11px is 1.3mm (unreadable)",
               geom_px_to_mm(11, &GEOM_PANEL_S3), 1.3, 0.15);
}

/*
 * The conversion that actually does the porting work: a size that is N mm on
 * one panel must be N mm on the other.
 */
static void test_size_translates_by_physical_height(void)
{
    printf("sizes translate between panels by physical height, not pixels\n");

    /* Today's hero cap. */
    const int c6_hero = geom_translate_px(96, &GEOM_PANEL_S3, &GEOM_PANEL_C6);
    check_int("96px on S3 -> 137px on C6", c6_hero, 137);

    /* Same physical height either way. */
    check_near("and both are 11.1mm",
               geom_px_to_mm(c6_hero, &GEOM_PANEL_C6), 11.1, 0.2);

    /* The floors. */
    /* Note this is the raw translation. It rounds DOWN to 34, which then
     * fails the floor check -- see test_legibility_floor_is_physical. A size
     * chosen for the ladder must be validated, not just translated. */
    check_int("24px floor translates to 34px (but see the floor test)",
              geom_translate_px(24, &GEOM_PANEL_S3, &GEOM_PANEL_C6), 34);
    check_true("...and 34px does not actually clear 2.8mm on the C6",
               !geom_meets_legibility_floor(34, &GEOM_PANEL_C6));
    check_int("so the real floor size is 35px",
              geom_mm_to_px(GEOM_LEGIBILITY_FLOOR_MM, &GEOM_PANEL_C6), 35);
    check_int("20px absolute floor -> 29px on C6",
              geom_translate_px(20, &GEOM_PANEL_S3, &GEOM_PANEL_C6), 29);

    /* Round trip returns where it started. */
    check_int("translation round-trips",
              geom_translate_px(c6_hero, &GEOM_PANEL_C6, &GEOM_PANEL_S3), 96);

    /* Identity. */
    check_int("translating to the same panel changes nothing",
              geom_translate_px(96, &GEOM_PANEL_S3, &GEOM_PANEL_S3), 96);
}

/*
 * The two tempting shortcuts, both wrong, asserted so nobody re-derives them
 * later and thinks they found a simplification.
 */
static void test_naive_scaling_is_wrong(void)
{
    printf("neither doubling nor copying pixel sizes is correct\n");

    const int correct = geom_translate_px(96, &GEOM_PANEL_S3, &GEOM_PANEL_C6);

    /* Doubling: 192px would be 15.5mm, ~40% too big. */
    check_true("doubling overshoots the correct size", 192 > correct);
    check_near("doubled 96px would be 15.5mm on the C6",
               geom_px_to_mm(192, &GEOM_PANEL_C6), 15.5, 0.3);

    /* Copying: 96px would be 7.8mm, ~30% too small. */
    check_true("copying undershoots the correct size", 96 < correct);
    check_near("copied 96px would be only 7.8mm on the C6",
               geom_px_to_mm(96, &GEOM_PANEL_C6), 7.8, 0.2);

    /* And copying breaks the floor outright. */
    check_true("copying the 24px floor falls below 2.8mm on the C6",
               geom_px_to_mm(24, &GEOM_PANEL_C6) < 2.8);
}

/* ---- the legibility floor as a rule, not a number ---- */

/*
 * The floor is 2.8mm at 50cm. Whether a given pixel size satisfies it depends
 * entirely on the panel, which is exactly why it must be checked rather than
 * assumed.
 */
static void test_legibility_floor_is_physical(void)
{
    printf("the legibility floor is 2.8mm, resolved per panel\n");

    /*
     * Spec 2.2 names 24px as "minimum viable" on this panel, so it must pass.
     * Note 24px is really 2.766mm -- the spec's table rounded it to 2.8 -- so
     * the comparison is made at one decimal, the precision the spec was
     * written in. See geometry.c.
     */
    check_true("24px clears the floor on the S3 (spec 2.2's own figure)",
               geom_meets_legibility_floor(24, &GEOM_PANEL_S3));
    check_near("and 24px really measures 2.77mm, not 2.80",
               geom_px_to_mm(24, &GEOM_PANEL_S3), 2.766, 0.01);
    check_true("23px does not clear it on the S3",
               !geom_meets_legibility_floor(23, &GEOM_PANEL_S3));

    /* 2.8mm on the C6 is 34.6px, so 34px is NOT enough -- it measures
     * 2.75mm. Rounding to nearest can land a derived size just under its own
     * threshold, which is why the floor check has no tolerance. */
    check_true("35px clears the floor on the C6",
               geom_meets_legibility_floor(35, &GEOM_PANEL_C6));
    check_true("34px does NOT clear it (2.75mm, rounds to 2.7)",
               !geom_meets_legibility_floor(34, &GEOM_PANEL_C6));
    check_true("24px does NOT clear the floor on the C6",
               !geom_meets_legibility_floor(24, &GEOM_PANEL_C6));

    /* The absolute floor, spec 2.2's hard minimum. */
    check_true("20px meets the absolute floor on the S3",
               geom_meets_absolute_floor(20, &GEOM_PANEL_S3));
    check_true("20px does NOT meet it on the C6",
               !geom_meets_absolute_floor(20, &GEOM_PANEL_C6));
    check_true("29px meets the absolute floor on the C6",
               geom_meets_absolute_floor(29, &GEOM_PANEL_C6));
}

/* ---- the text column ---- */

/*
 * Padding is also physical. 16px on the S3 is 1.8mm of margin; the same margin
 * on the denser C6 is 23px. Keeping 16px would visually tighten the layout.
 */
static void test_padding_and_column(void)
{
    printf("padding translates physically too\n");

    check_int("16px padding -> 23px on the C6",
              geom_translate_px(16, &GEOM_PANEL_S3, &GEOM_PANEL_C6), 23);

    check_int("S3 text column is 208px",
              geom_text_column_px(&GEOM_PANEL_S3, 16), 208);
    check_int("C6 text column is 434px at translated padding",
              geom_text_column_px(&GEOM_PANEL_C6, 23), 434);

    /* The column grows faster than the type, which is the headroom that lets
     * the ARR decimal come back. */
    const double col_ratio =
        (double)geom_text_column_px(&GEOM_PANEL_C6, 23) /
        (double)geom_text_column_px(&GEOM_PANEL_S3, 16);
    const double type_ratio =
        (double)geom_translate_px(96, &GEOM_PANEL_S3, &GEOM_PANEL_C6) / 96.0;

    check_true("column grows faster than type", col_ratio > type_ratio);
    check_near("column grows ~2.09x", col_ratio, 2.09, 0.05);
    check_near("type grows ~1.43x", type_ratio, 1.43, 0.03);
}

int main(void)
{
    test_panel_density();
    test_spec_legibility_table();
    test_size_translates_by_physical_height();
    test_naive_scaling_is_wrong();
    test_legibility_floor_is_physical();
    test_padding_and_column();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
