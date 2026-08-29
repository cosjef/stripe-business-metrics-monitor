/*
 * Layout constants for the Waveshare ESP32-S3-LCD-1.54 (240x240 IPS, 220 PPI).
 *
 * These are the original values, traceable to stripe-revenue-display-spec.md
 * and verified on hardware. layout.h selects this block or layout_c6.h by
 * target; nothing here should change when porting.
 *
 * Prefixed S3_ so both panels' constants can be compared in one test.
 */
#pragma once

#define S3_PANEL_PX             (240)

/* Padding and the usable text column (spec 2.3, appendix A) */
#define S3_PAD_PX               (16)
#define S3_TEXT_COLUMN_PX       (208)   /* 240 - 2*16 */

/* Baselines for the three-zone skeleton (spec 5.1) */
#define S3_LABEL_BASELINE_Y     (16)
#define S3_HERO_BASELINE_Y      (150)
#define S3_SUBTITLE_BASELINE_Y  (178)
#define S3_FOOTER_BASELINE_Y    (210)

#define S3_DOTS_CENTER_Y        (214)
#define S3_DOTS_RADIUS          (4)
#define S3_DOTS_GAP             (17)

/* Type sizes (spec 2.2) */
#define S3_SIZE_LABEL           (20)
#define S3_SIZE_SUBTITLE        (22)
#define S3_SIZE_FOOTER          (18)
#define S3_SIZE_HERO_MAX        (96)
#define S3_SIZE_HERO_MIN        (24)

#define S3_LEGIBILITY_FLOOR_PX  (24)
#define S3_ABSOLUTE_FLOOR_PX    (20)
