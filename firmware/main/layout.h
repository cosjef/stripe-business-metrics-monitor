/*
 * Layout and palette constants.
 *
 * Every value here traces to stripe-revenue-display-spec.md. Do not tune these
 * by eye on the bench without updating the spec -- the sizes are derived from
 * physical legibility math (spec 2.2), not aesthetics.
 */
#pragma once

/* Padding and the usable text column (spec 2.3, appendix A) */
#define PAD_PX          (16)
#define TEXT_COLUMN_PX  (208)   /* 240 - 2*16 */

/* Baselines for the three-zone skeleton (spec 5.1).
 * Identical across all nine screens -- this is what makes rotation read as one
 * instrument changing state rather than four screens flashing by. */
#define LABEL_BASELINE_Y     (16)
#define HERO_BASELINE_Y      (150)
#define SUBTITLE_BASELINE_Y  (178)
#define FOOTER_BASELINE_Y    (210)

/* Rotation dots sit slightly below the footer baseline (per render_screens.py) */
#define DOTS_CENTER_Y   (214)
#define DOTS_RADIUS     (4)
#define DOTS_GAP        (17)

/* Type sizes (spec 2.2). Nothing below 20px, ever. */
#define SIZE_LABEL      (20)
#define SIZE_SUBTITLE   (22)
#define SIZE_FOOTER     (18)   /* dim/low-priority only; below the 20px floor
                                * deliberately, for text the user never needs to
                                * read at a glance (version, retry status) */
#define SIZE_HERO_MAX   (96)
#define SIZE_HERO_MIN   (24)

/* Legibility floor (spec 2.2) */
#define LEGIBILITY_FLOOR_PX  (24)  /* anything a user must read reliably */
#define ABSOLUTE_FLOOR_PX    (20)  /* hard minimum, no exceptions */

/* NOTE: spec 2.3 defines a 0.6em monospace advance and derives max characters
 * per line from it. This build renders SF Compact Bold (proportional), so that
 * constant no longer describes the font and has been removed deliberately --
 * width is measured per glyph in hero_size.c. Do not reintroduce it. */

/* Palette (spec 4.1). Values are RGB888; LVGL converts to RGB565.
 *
 * BACKGROUND DEVIATES FROM SPEC 4.1 (#121211), measured on hardware 2026-08-15.
 * Spec 3.1 assumes the backlight lifts #000000 into "dark charcoal", so it
 * picks #121211 as the honest floor. This panel behaves the opposite way: its
 * response curve is almost a step function at the bottom. A near-black ramp
 * (00/04/08/0C/12/18/20/30) showed 0x00 rendering truly black, while 0x04 had
 * already jumped to a clearly visible slate gray, and everything from 0x04 to
 * 0x30 collapsed to roughly the same mid-gray. #121211 is unreachable here --
 * it renders mid-gray, far worse than the edge bleed spec 3.1 was avoiding.
 * Pure black is the only genuinely dark value this panel can produce.
 * Re-test with main/colortest.c if the panel or driver ever changes. */
#define COLOR_BG        0x000000  /* screen field */
#define COLOR_PRIMARY   0xF4F2EC  /* hero numbers, key values */
#define COLOR_MUTED     0x8E8C84  /* labels, subtitles, context */
#define COLOR_DIM       0x6B6A64  /* footer, version, status */
#define COLOR_INACTIVE  0x3A3A37  /* unfilled rotation dots */
#define COLOR_GREEN     0x5DCAA5  /* realized gains ONLY (spec 4.2) */
#define COLOR_AMBER     0xEF9F27  /* degraded states ONLY (spec 4.2) */

/*
 * Red. Spec 4.2 keeps this out of the base palette, adding it "only for
 * threshold breaches, so its appearance carries information".
 *
 * Used on exactly one screen: FAILED, the only screen that is actionable
 * rather than informational -- money actively being lost to a declined card,
 * and recoverable if acted on. Everything else reports; this one asks.
 *
 * #E74D63 measures 5.64:1 against the near-black field, clearing WCAG AA, and
 * round-trips through RGB565 exactly -- the panel shows the value written here,
 * with no quantization drift. Two rejected candidates: Stripe's own error red
 * (#CD3D64) at 4.45:1 is below AA and muddy at distance, and #E0555F quantizes
 * to #E7555A, a 7/255 shift that makes the rendered color differ from the
 * constant.
 *
 * Adding red anywhere else re-creates the dilution spec 4.2 warns about for
 * green. A cancellation is ordinary business, not a breach.
 */
#define COLOR_RED       0xE74D63  /* threshold breaches ONLY (spec 4.2) */

/* Timing (appendix A) */
/*
 * Spec appendix A specifies 8s, written for a six-screen deck. With eight
 * screens that made the anchor metric come round only every 64 seconds, which
 * is too long to call glanceable. 5s brings the full cycle to 40s and still
 * leaves each screen readable at a glance.
 */
#define ROTATION_INTERVAL_MS   (5 * 1000)
#define STALE_THRESHOLD_MS     (15 * 60 * 1000)
