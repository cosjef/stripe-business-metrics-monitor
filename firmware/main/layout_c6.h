/*
 * Layout constants for the Waveshare ESP32-C6-Touch-AMOLED-2.16
 * (480x480 AMOLED, ~314 PPI).
 *
 * DERIVED, NOT TYPED. Every value here comes from geometry.c and is asserted
 * in test_layout_c6.c. Do not hand-tune these without updating that test --
 * the whole point is that the derivation is checkable.
 *
 * The rule, and it is two rules rather than one:
 *
 *   TYPE SIZES scale PHYSICALLY (x1.43, the density ratio).
 *     Spec 2.2 states its legibility floor in millimetres at 50cm. A 96px
 *     hero is 11.1mm on the S3; it must stay 11.1mm, which is 137px here.
 *     Doubling would give 15.5mm -- 40% too large. Copying would give 7.8mm
 *     and break the floor.
 *
 *   POSITIONS scale PROPORTIONALLY (x2, the pixel ratio).
 *     Baselines are composition, not legibility. The three-zone skeleton has
 *     to keep its shape or rotation stops reading as one instrument changing
 *     state (spec 5.1). Scaling positions physically would leave the whole
 *     layout hugging the top third of the panel.
 *
 * Applying either rule to both categories is wrong, in opposite directions,
 * and both mistakes look reasonable until measured.
 */
#pragma once

#define C6_PANEL_PX             (480)

/* Padding is physical: 16px on the S3 is 1.8mm of margin, which is 23px here. */
#define C6_PAD_PX               (23)
#define C6_TEXT_COLUMN_PX       (434)   /* 480 - 2*23 */

/* Baselines: proportional (x2). */
#define C6_LABEL_BASELINE_Y     (32)
#define C6_HERO_BASELINE_Y      (300)
#define C6_SUBTITLE_BASELINE_Y  (356)
#define C6_FOOTER_BASELINE_Y    (420)

#define C6_DOTS_CENTER_Y        (428)
#define C6_DOTS_RADIUS          (8)
#define C6_DOTS_GAP             (34)

/* Type: physical (x1.43). */
#define C6_SIZE_LABEL           (29)
#define C6_SIZE_SUBTITLE        (31)
#define C6_SIZE_FOOTER          (26)
#define C6_SIZE_HERO_MAX        (137)
/*
 * 35, not the 34 that translating the S3's 24px produces: 2.8mm is 34.6px on
 * this panel, so 34 rounds to just under its own floor (2.748mm). The hero
 * minimum must clear the floor, so it rounds up.
 */
#define C6_SIZE_HERO_MIN        (35)

/*
 * Floors, resolved for this panel's density.
 *
 * Note the legibility floor is 35px, not the 34px that translating 24px
 * produces: 2.8mm is 34.6px here, and rounding to nearest lands just under
 * its own threshold. geom_meets_legibility_floor() catches that; see
 * test_geometry.c.
 */
/*
 * Card layout (screen_draw_card).
 *
 * The three-zone skeleton above is the S3 composition scaled up. It works,
 * but it spends the panel's extra height on nothing: with the hero at its
 * physical size there is a ~200px band between the label and the number doing
 * no work. These constants spend that band on a trend.
 *
 * Derived from the same rules as everything else here -- padding and type
 * scale physically, positions proportionally -- and asserted in
 * test_screens.c so a hand-tune has to update a test.
 */
#define C6_CARD_Y               (76)    /* below the label row */
#define C6_CARD_H               (300)   /* to just above the dots */
#define C6_CARD_RADIUS          (16)
#define C6_CARD_PAD             (28)    /* inner inset, > PAD_PX for depth */

/* Rows inside the card, as offsets from its top. */
#define C6_CARD_SUBTITLE_DY     (22)
#define C6_CARD_HERO_BASELINE_DY (190)  /* baseline, not top */
#define C6_CARD_BAR_DY          (220)   /* 30px under the hero baseline */
#define C6_CARD_BAR_H           (14)
#define C6_CARD_CAPTION_DY      (248)   /* 14px under the bar */

/* Delta pill, right-aligned on the label row. */
#define C6_PILL_PAD_X           (16)
#define C6_PILL_PAD_Y           (6)

#define C6_LEGIBILITY_FLOOR_PX  (35)
#define C6_ABSOLUTE_FLOOR_PX    (29)
