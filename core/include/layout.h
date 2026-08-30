/*
 * Layout and palette constants.
 *
 * Every value here traces to stripe-revenue-display-spec.md. Do not tune these
 * by eye on the bench without updating the spec -- the sizes are derived from
 * physical legibility math (spec 2.2), not aesthetics.
 */
#pragma once

/*
 * Geometry is selected by target.
 *
 * The S3 values are the originals, traceable to the spec and verified on that
 * panel. The C6 values are derived in geometry.c from physical size rather
 * than scaled by pixel count -- the C6 has 2x the pixels but only 1.4x the
 * physical size, so type must grow ~1.43x to stay the same number of
 * millimetres, while positions grow 2x to keep the composition. See
 * layout_c6.h and test_layout_c6.c.
 */
/*
 * BOARD_C6_AMOLED_216 is the Arduino/PlatformIO port's selector. That build
 * has no sdkconfig.h, so CONFIG_IDF_TARGET_ESP32C6 is never defined and the
 * C6 would silently fall through to the S3 ladder -- every size and baseline
 * wrong by the density ratio, on a panel that would still render happily.
 * Both builds must land on the same constants; see arduino/platformio.ini.
 */
#if defined(CONFIG_IDF_TARGET_ESP32C6) || defined(BOARD_C6_AMOLED_216)
#include "layout_c6.h"

#define PANEL_PX            C6_PANEL_PX
#define PAD_PX              C6_PAD_PX
#define TEXT_COLUMN_PX      C6_TEXT_COLUMN_PX
#define LABEL_BASELINE_Y    C6_LABEL_BASELINE_Y
#define HERO_BASELINE_Y     C6_HERO_BASELINE_Y
#define SUBTITLE_BASELINE_Y C6_SUBTITLE_BASELINE_Y
#define FOOTER_BASELINE_Y   C6_FOOTER_BASELINE_Y
#define DOTS_CENTER_Y       C6_DOTS_CENTER_Y
#define DOTS_RADIUS         C6_DOTS_RADIUS
#define DOTS_GAP            C6_DOTS_GAP
#define SIZE_LABEL          C6_SIZE_LABEL
#define SIZE_SUBTITLE       C6_SIZE_SUBTITLE
#define SIZE_FOOTER         C6_SIZE_FOOTER
#define SIZE_HERO_MAX       C6_SIZE_HERO_MAX
#define SIZE_HERO_MIN       C6_SIZE_HERO_MIN

/* Card layout (screen_draw_card); see layout_c6.h for the derivation. */
#define CARD_Y              C6_CARD_Y
#define CARD_H              C6_CARD_H
#define CARD_RADIUS         C6_CARD_RADIUS
#define CARD_PAD            C6_CARD_PAD
#define CARD_SUBTITLE_DY    C6_CARD_SUBTITLE_DY
#define CARD_HERO_BASELINE_DY C6_CARD_HERO_BASELINE_DY
#define CARD_BAR_DY         C6_CARD_BAR_DY
#define CARD_BAR_H          C6_CARD_BAR_H
#define CARD_CAPTION_DY     C6_CARD_CAPTION_DY
#define MIX_CARD_H          C6_MIX_CARD_H
#define MIX_HERO_BASELINE_DY C6_MIX_HERO_BASELINE_DY
#define MIX_ROW1_DY         C6_MIX_ROW1_DY
#define MIX_ROW2_DY         C6_MIX_ROW2_DY
#define MIX_LABEL_GAP       C6_MIX_LABEL_GAP
#define BATT_W              C6_BATT_W
#define BATT_H              C6_BATT_H
#define BATT_GAP            C6_BATT_GAP
#define BATT_CENTER_Y       C6_BATT_CENTER_Y
#define PILL_PAD_X          C6_PILL_PAD_X
#define PILL_PAD_Y          C6_PILL_PAD_Y

/* Message screens (setup, auth error): body text and its three baselines.
 * Scaled like everything else rather than hardcoded -- these were literals
 * (32px at y=118/152/182) and so stayed S3-sized on the C6 panel. */
#define SIZE_MESSAGE        (46)
#define MSG_LINE1_Y         (236)
#define MSG_LINE2_Y         (304)
#define MSG_HINT_Y          (364)

#else  /* S3, and the host test harness */
#include "layout_s3.h"

#define PANEL_PX            S3_PANEL_PX
#define PAD_PX              S3_PAD_PX
#define TEXT_COLUMN_PX      S3_TEXT_COLUMN_PX
#define LABEL_BASELINE_Y    S3_LABEL_BASELINE_Y
#define HERO_BASELINE_Y     S3_HERO_BASELINE_Y
#define SUBTITLE_BASELINE_Y S3_SUBTITLE_BASELINE_Y
#define FOOTER_BASELINE_Y   S3_FOOTER_BASELINE_Y
#define DOTS_CENTER_Y       S3_DOTS_CENTER_Y
#define DOTS_RADIUS         S3_DOTS_RADIUS
#define DOTS_GAP            S3_DOTS_GAP
#define SIZE_LABEL          S3_SIZE_LABEL
#define SIZE_SUBTITLE       S3_SIZE_SUBTITLE
#define SIZE_FOOTER         S3_SIZE_FOOTER
#define SIZE_HERO_MAX       S3_SIZE_HERO_MAX
#define SIZE_HERO_MIN       S3_SIZE_HERO_MIN

/*
 * Card layout, S3 values.
 *
 * The card is a 480x480 design and is not used on the S3, but the constants
 * must exist for both targets: screens.c is compiled once for both, and the
 * host suite builds against this branch. Scaled down proportionally so a
 * render here is at least coherent rather than off-panel.
 */
#define CARD_Y              (38)
#define CARD_H              (150)
#define CARD_RADIUS         (8)
#define CARD_PAD            (14)
#define CARD_SUBTITLE_DY    (11)
#define CARD_HERO_BASELINE_DY (95)
#define CARD_BAR_DY         (110)
#define CARD_BAR_H          (7)
#define CARD_CAPTION_DY     (124)
/* Mix variant, S3 values -- half the C6 offsets, same proportions. The card
 * is a 480x480 design, but these must exist for both targets because
 * screens.c compiles once for both and the host suite builds this branch. */
#define MIX_CARD_H          (160)
#define MIX_HERO_BASELINE_DY (64)
#define MIX_ROW1_DY         (88)
#define MIX_ROW2_DY         (124)
#define MIX_LABEL_GAP       (3)
#define BATT_W              (17)
#define BATT_H              (9)
#define BATT_GAP            (8)
#define BATT_CENTER_Y       (23)
#define PILL_PAD_X          (8)
#define PILL_PAD_Y          (3)

/* Message screens (setup, auth error). The S3 originals. */
#define SIZE_MESSAGE        (32)
#define MSG_LINE1_Y         (118)
#define MSG_LINE2_Y         (152)
#define MSG_HINT_Y          (182)
#endif

/* Legibility floor (spec 2.2) */
#define LEGIBILITY_FLOOR_PX  (24)  /* anything a user must read reliably */
#define ABSOLUTE_FLOOR_PX    (20)  /* hard minimum, no exceptions */

/* NOTE: spec 2.3 defines a 0.6em monospace advance and derives max characters
 * per line from it. This build renders SF Compact Bold (proportional), so that
 * constant no longer describes the font and has been removed deliberately --
 * width is measured per glyph in hero_size.c. Do not reintroduce it. */

/* Palette (spec 4.1). Values are RGB888; LVGL converts to RGB565.
 *
 * BACKGROUND: UNVERIFIED ON THIS PANEL. Currently #000000, inherited from the
 * S3 build, but the reasoning behind that value does NOT carry over.
 *
 * On the S3's IPS panel the backlight is always on, and a measured ramp
 * (main/colortest.c) showed everything from 0x04 to 0x30 collapsing into the
 * same mid-gray -- so the spec's #121211 was unusable and #000000 was the only
 * honest dark value. That took five wrong attempts to establish.
 *
 * This is an AMOLED. Each pixel emits its own light, so a black pixel is
 * simply OFF: true black, effectively infinite contrast, and near-black values
 * should render as intended instead of collapsing. The spec's original
 * #121211 (spec 3.1) is very likely viable here, and dark pixels now cost less
 * power rather than the same.
 *
 * "Likely" is not "measured". Run colortest.c on this panel before changing
 * it, and do not repeat the S3 mistake of theorising first.
 */
#define COLOR_BG        0x000000  /* screen field -- UNVERIFIED, see above */
#define COLOR_PRIMARY   0xF4F2EC  /* hero numbers, key values */
#define COLOR_MUTED     0x8E8C84  /* labels, subtitles, context */
#define COLOR_DIM       0x6B6A64  /* footer, version, status */
#define COLOR_INACTIVE  0x3A3A37  /* unfilled rotation dots */
#define COLOR_GREEN     0x5DCAA5  /* realized gains ONLY (spec 4.2) */
#define COLOR_AMBER     0xEF9F27  /* degraded states ONLY (spec 4.2) */

/*
 * Card surfaces, added for the 480x480 card layout.
 *
 * Both sit just above the field rather than at a mid grey. On an emissive
 * panel a lit pixel costs power and a black one costs none, so a card that
 * reads as "raised" at #1A1A18 is nearly free, where a conventional #2E2E2C
 * card would light a third of the screen for the same visual job.
 *
 * UNVERIFIED on this panel, like COLOR_BG above: near-black separation is
 * exactly what the S3's IPS panel could not render, and these were chosen on
 * an AMOLED's expected behaviour rather than a measured ramp. Run colortest.c
 * before trusting the gap between BG, CARD and TRACK.
 */
#define COLOR_CARD      0x1A1A18  /* card fill -- UNVERIFIED, see above */
#define COLOR_TRACK     0x2A2A28  /* unfilled bar track */

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
