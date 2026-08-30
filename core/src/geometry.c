/*
 * Panel geometry. See geometry.h for why sizes are derived rather than scaled.
 */
#include "geometry.h"

#include <math.h>

const geom_panel_t GEOM_PANEL_S3 = {
    .name = "S3 1.54in 240x240 IPS",
    .diagonal_in = 1.54,
    .pixels = 240,
};

const geom_panel_t GEOM_PANEL_C6 = {
    .name = "C6 2.16in 480x480 AMOLED",
    .diagonal_in = 2.16,
    .pixels = 480,
};

#define MM_PER_IN 25.4

double geom_side_mm(const geom_panel_t *p)
{
    /* Square panel: side = diagonal / sqrt(2). */
    return p->diagonal_in / sqrt(2.0) * MM_PER_IN;
}

double geom_ppi(const geom_panel_t *p)
{
    return (double)p->pixels / (p->diagonal_in / sqrt(2.0));
}

double geom_px_to_mm(int px, const geom_panel_t *p)
{
    return (double)px / geom_ppi(p) * MM_PER_IN;
}

int geom_mm_to_px(double mm, const geom_panel_t *p)
{
    return (int)(mm * geom_ppi(p) / MM_PER_IN + 0.5);
}

int geom_translate_px(int px, const geom_panel_t *from, const geom_panel_t *to)
{
    return geom_mm_to_px(geom_px_to_mm(px, from), to);
}

/*
 * The floors are compared at the precision the spec states them in.
 *
 * Spec 2.2's table reads "24px | 2.8 mm | Minimum viable", but 24px on this
 * panel is actually 2.766mm -- the spec rounded to one decimal. Comparing
 * against a hard 2.8 would therefore reject the very size the spec names as
 * the floor, which would be an artefact of arithmetic rather than a real
 * legibility judgement.
 *
 * So both sides are rounded to one decimal before comparing, matching how the
 * threshold was authored. 24px -> 2.8 passes on the S3; 34px -> 2.7 fails on
 * the C6 and 35px -> 2.8 passes. That is the intended behaviour in both cases.
 */
static bool at_least_mm(int px, const geom_panel_t *p, double floor_mm)
{
    const double got = geom_px_to_mm(px, p);
    /* Round to one decimal, the precision spec 2.2 uses. */
    const double got_1dp = (double)((int)(got * 10.0 + 0.5)) / 10.0;
    return got_1dp >= floor_mm - 1e-9;
}

bool geom_meets_legibility_floor(int px, const geom_panel_t *p)
{
    return at_least_mm(px, p, GEOM_LEGIBILITY_FLOOR_MM);
}

bool geom_meets_absolute_floor(int px, const geom_panel_t *p)
{
    return at_least_mm(px, p, GEOM_ABSOLUTE_FLOOR_MM);
}

int geom_text_column_px(const geom_panel_t *p, int pad_px)
{
    return p->pixels - 2 * pad_px;
}
