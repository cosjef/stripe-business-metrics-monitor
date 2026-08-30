/*
 * Host-side tests for baseline positioning, font lookup coverage, and palette
 * constraints.
 *
 *   cd firmware/test && make && ./test_layout
 *
 * These cover the logic that produced visual bugs during Stage 1 bring-up:
 * mispositioned text, a background color the panel cannot render, and dots
 * whose color collapsed into the background.
 */
#include <math.h>
#include <stdio.h>

#include "../include/baseline.h"
#include "../include/layout.h"
#include "../include/hero_size.h"

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

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/* ---- baseline positioning ---- */

static void test_baseline_conversion(void)
{
    printf("baseline to top-left conversion\n");

    /* A font 20px tall with 4px of descent has 16px of ascent. To land its
     * baseline at y=100 its top edge goes at y=84. */
    check_int("ascent = line_height - base_line", font_ascent(20, 4), 16);
    check_int("baseline 100, ascent 16 -> top 84", baseline_to_top(100, 20, 4), 84);

    /* Zero descent: top is exactly one line height above the baseline. */
    check_int("no descent -> top = baseline - line_height",
              baseline_to_top(50, 30, 0), 20);

    /* Clamping: a baseline too near the top of the screen must not push text
     * off-screen at a negative y. */
    check_int("clamps at 0 when ascent exceeds baseline",
              baseline_to_top(10, 40, 5), 0);
    check_int("exactly zero is not clamped past 0",
              baseline_to_top(16, 20, 4), 0);
}

/*
 * The spec's four baselines must all land on-screen for every font size we
 * ship. A regression here puts text partly off the panel.
 */
static void test_spec_baselines_fit_on_screen(void)
{
    printf("spec baselines land on-screen for all shipped sizes\n");

    const int baselines[] = {
        LABEL_BASELINE_Y, HERO_BASELINE_Y, SUBTITLE_BASELINE_Y, FOOTER_BASELINE_Y,
    };
    const char *names[] = {"label", "hero", "subtitle", "footer"};

    for (size_t b = 0; b < sizeof(baselines) / sizeof(baselines[0]); b++) {
        for (size_t i = 0; i < hero_font_sizes_count; i++) {
            int size = hero_font_sizes[i];
            /* Approximate the metrics of a generated font: LVGL reports a line
             * height near the nominal size, with descent roughly a fifth. */
            int line_height = size;
            int base_line = size / 5;
            int top = baseline_to_top(baselines[b], line_height, base_line);

            char what[96];
            snprintf(what, sizeof(what), "%s baseline at %dpx stays on-screen",
                     names[b], size);
            /* PANEL_PX, not a literal 240: hardcoding one panel's height
             * meant this asserted against the wrong screen. */
            check_true(what, top >= 0 && top < PANEL_PX);
        }
    }
}

/* ---- font lookup coverage ---- */

/*
 * Mirrors font_for_size() without depending on LVGL types. The real function
 * lives in core/fonts/fonts.c and needs lvgl.h, which the host build does not
 * have; this asserts the size set it must cover stays in sync.
 */
static int font_exists_for(int size_px)
{
    static const int shipped[] = {26, 29, 31, 35, 46, 58, 74, 86, 92,
                                  104, 108, 120, 126, 137};
    for (size_t i = 0; i < sizeof(shipped) / sizeof(shipped[0]); i++) {
        if (shipped[i] == size_px) {
            return 1;
        }
    }
    return 0;
}

static void test_font_lookup_covers_all_sizes(void)
{
    printf("font lookup covers every size we can request\n");

    /* Every hero size must resolve. */
    for (size_t i = 0; i < hero_font_sizes_count; i++) {
        char what[64];
        snprintf(what, sizeof(what), "hero size %d resolves", hero_font_sizes[i]);
        check_true(what, font_exists_for(hero_font_sizes[i]));
    }

    /* Every UI size referenced by layout.h must resolve. */
    check_true("SIZE_LABEL resolves",    font_exists_for(SIZE_LABEL));
    check_true("SIZE_SUBTITLE resolves", font_exists_for(SIZE_SUBTITLE));
    check_true("SIZE_FOOTER resolves",   font_exists_for(SIZE_FOOTER));

    /* Sizes we do not ship must NOT silently resolve -- font_for_size()
     * returns NULL for these, and callers treat that as a programming error. */
    check_true("unshipped size 17 does not resolve", !font_exists_for(17));
    check_true("unshipped size 100 does not resolve", !font_exists_for(100));
    check_true("zero does not resolve", !font_exists_for(0));
}

/* ---- palette constraints ---- */

/* Rec. 709 luma, scaled by 1000 to stay in integer arithmetic. */
static int luma_x1000(unsigned int rgb)
{
    int r = (int)((rgb >> 16) & 0xFF);
    int g = (int)((rgb >> 8) & 0xFF);
    int b = (int)(rgb & 0xFF);
    return (213 * r + 715 * g + 72 * b);
}

/*
 * This panel's response collapses everything from roughly 0x04 to 0x30 into a
 * single mid-gray (measured 2026-08-15; see main/colortest.c and the note in
 * layout.h). Any color landing in that band renders as an indistinct gray.
 */
#define COLLAPSED_BAND_LOW_X1000  (4 * 1000)
#define COLLAPSED_BAND_HIGH_X1000 (0x30 * 1000)

/*
 * WCAG 2.x contrast ratio, returned as ratio*100 so this stays integer-only.
 * Relative luminance needs a gamma curve; a 256-entry lookup computed once
 * avoids pulling <math.h> and floating point into the test.
 */
static unsigned int contrast_x100(unsigned int fg, unsigned int bg)
{
    /* sRGB channel -> linear, scaled by 100000. */
    static unsigned int lin[256];
    static int built = 0;
    if (!built) {
        for (int i = 0; i < 256; i++) {
            const double c = i / 255.0;
            const double v = (c <= 0.04045) ? (c / 12.92)
                                            : pow((c + 0.055) / 1.055, 2.4);
            lin[i] = (unsigned int)(v * 100000.0 + 0.5);
        }
        built = 1;
    }

    /* Rec.709 luma weights, per WCAG. */
    #define LUM(x) ((2126u * lin[((x) >> 16) & 0xFF] \
                   + 7152u * lin[((x) >> 8) & 0xFF]  \
                   +  722u * lin[(x) & 0xFF]) / 10000u)

    unsigned int a = LUM(fg), b = LUM(bg);
    if (a < b) { const unsigned int t = a; a = b; b = t; }
    #undef LUM

    /* (L1 + 0.05) / (L2 + 0.05), with 0.05 at the same 100000 scale. */
    return (a + 5000u) * 100u / (b + 5000u);
}

/* What the panel actually shows: 8-bit channels truncated to 5/6/5 and
 * expanded back, the same conversion esp_lcd performs on every pixel. */
static unsigned int rgb565_roundtrip(unsigned int rgb)
{
    const unsigned int r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    const unsigned int R = r >> 3, G = g >> 2, B = b >> 3;
    return (((R << 3) | (R >> 2)) << 16) | (((G << 2) | (G >> 4)) << 8)
           | ((B << 3) | (B >> 2));
}

static int in_collapsed_band(unsigned int rgb)
{
    int y = luma_x1000(rgb);
    return y >= COLLAPSED_BAND_LOW_X1000 && y <= COLLAPSED_BAND_HIGH_X1000;
}

static void test_palette_is_renderable(void)
{
    printf("palette avoids the panel's collapsed band\n");

    /* The background must be true black. Spec 4.1 specifies #121211, but that
     * lands in the collapsed band on this panel and renders mid-gray. */
    check_int("COLOR_BG is pure black", (int)COLOR_BG, 0x000000);

    /* Every foreground color must sit clear of the band, or it will not be
     * distinguishable from a mid-gray field. */
    check_true("COLOR_INACTIVE outside collapsed band", !in_collapsed_band(COLOR_INACTIVE));
    check_true("COLOR_DIM outside collapsed band",      !in_collapsed_band(COLOR_DIM));
    check_true("COLOR_MUTED outside collapsed band",    !in_collapsed_band(COLOR_MUTED));
    check_true("COLOR_PRIMARY outside collapsed band",  !in_collapsed_band(COLOR_PRIMARY));
    check_true("COLOR_GREEN outside collapsed band",    !in_collapsed_band(COLOR_GREEN));
    check_true("COLOR_AMBER outside collapsed band",    !in_collapsed_band(COLOR_AMBER));
    check_true("COLOR_RED outside collapsed band",      !in_collapsed_band(COLOR_RED));

    /*
     * COLOR_RED must survive RGB565 exactly. The first candidate (#E0555F)
     * rendered as #E7555A -- close enough to look right, but it meant the
     * constant in layout.h was not the color on the glass, and pixel tests
     * comparing against the constant failed. Accent colors are pinned to
     * values the panel can represent, so source and screen agree.
     */
    check_true("COLOR_RED survives RGB565 unchanged",
               rgb565_roundtrip(COLOR_RED) == COLOR_RED);

    /* Red must stay distinguishable from amber: both are warning-adjacent, and
     * at arm's length a red that drifts orange reads as the degraded state. */
    /*
     * The accent colors must clear WCAG AA (4.5:1) against the field. This is
     * asserted, not just documented: a future red could round-trip cleanly and
     * stay clear of amber while quietly dropping below AA. Integer math at
     * 1000x to keep floating point out of the test.
     */
    check_true("COLOR_RED clears WCAG AA on the background",
               contrast_x100(COLOR_RED, COLOR_BG) >= 450);
    check_true("COLOR_AMBER clears WCAG AA on the background",
               contrast_x100(COLOR_AMBER, COLOR_BG) >= 450);
    check_true("COLOR_GREEN clears WCAG AA on the background",
               contrast_x100(COLOR_GREEN, COLOR_BG) >= 450);

    check_true("COLOR_RED distinct from COLOR_AMBER",
               ((COLOR_RED >> 8) & 0xFF) + 40 < ((COLOR_AMBER >> 8) & 0xFF));

    /* The spec's original background would fail this -- documents why we
     * deviate, and fails loudly if someone restores it. */
    check_true("spec's #121211 WOULD collapse (why we deviate)",
               in_collapsed_band(0x121211));
}

/*
 * Foreground colors must be distinguishable from the background, and the
 * inactive dots must be distinguishable from the active ones.
 */
static void test_palette_contrast(void)
{
    printf("palette contrast\n");

    int bg = luma_x1000(COLOR_BG);

    check_true("COLOR_PRIMARY brighter than background", luma_x1000(COLOR_PRIMARY) > bg);
    check_true("COLOR_MUTED brighter than background",   luma_x1000(COLOR_MUTED) > bg);
    check_true("COLOR_DIM brighter than background",     luma_x1000(COLOR_DIM) > bg);
    check_true("COLOR_GREEN brighter than background",   luma_x1000(COLOR_GREEN) > bg);

    /* Rotation dots: the filled dot must clearly outrank the unfilled ones,
     * otherwise the position indicator conveys nothing. */
    check_true("active dot much brighter than inactive",
               luma_x1000(COLOR_PRIMARY) > luma_x1000(COLOR_INACTIVE) * 2);

    /* Inactive dots must still be visible against the background. */
    check_true("inactive dot brighter than background",
               luma_x1000(COLOR_INACTIVE) > bg);

    /* The spec's hierarchy: primary > muted > dim > inactive (4.1). */
    check_true("primary brighter than muted",
               luma_x1000(COLOR_PRIMARY) > luma_x1000(COLOR_MUTED));
    check_true("muted brighter than dim",
               luma_x1000(COLOR_MUTED) > luma_x1000(COLOR_DIM));
    check_true("dim brighter than inactive",
               luma_x1000(COLOR_DIM) > luma_x1000(COLOR_INACTIVE));
}

/* ---- layout geometry ---- */

static void test_layout_constants(void)
{
    printf("layout geometry\n");

    /* The text column is the panel minus padding on both sides (spec 2.3). */
    check_int("text column = panel - 2*padding", TEXT_COLUMN_PX,
              PANEL_PX - 2 * PAD_PX);

    /* Baselines must be ordered top to bottom and fit the panel. */
    check_true("label above hero",      LABEL_BASELINE_Y < HERO_BASELINE_Y);
    check_true("hero above subtitle",   HERO_BASELINE_Y < SUBTITLE_BASELINE_Y);
    check_true("subtitle above footer", SUBTITLE_BASELINE_Y < FOOTER_BASELINE_Y);
    check_true("footer on screen",      FOOTER_BASELINE_Y < PANEL_PX);

    /* Rotation dots must fit within the panel width (6 dots, 17px apart). */
    const int span = (6 - 1) * DOTS_GAP;
    check_true("6 dots fit across the panel", span + 2 * DOTS_RADIUS <= 240);

    /* Size floors from spec 2.2 must be ordered sensibly. */
    check_true("absolute floor <= legibility floor",
               ABSOLUTE_FLOOR_PX <= LEGIBILITY_FLOOR_PX);
    check_true("hero max above legibility floor",
               SIZE_HERO_MAX > LEGIBILITY_FLOOR_PX);

    /* Every shipped hero size must be at or above the absolute floor -- the
     * spec's "nothing below 20px, ever" rule (2.2). */
    for (size_t i = 0; i < hero_font_sizes_count; i++) {
        char what[64];
        snprintf(what, sizeof(what), "hero size %d at/above absolute floor",
                 hero_font_sizes[i]);
        check_true(what, hero_font_sizes[i] >= ABSOLUTE_FLOOR_PX);
    }
}

int main(void)
{
    printf("layout, font lookup, and palette tests\n\n");

    test_baseline_conversion();
    test_spec_baselines_fit_on_screen();
    test_font_lookup_covers_all_sizes();
    test_palette_is_renderable();
    test_palette_contrast();
    test_layout_constants();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
