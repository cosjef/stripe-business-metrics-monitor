/*
 * Generated monospace bitmap fonts (see tools/gen_fonts.sh).
 *
 * Sizes match hero_font_sizes[] in hero_size.c plus the UI sizes from layout.h,
 * so a size computed by hero_size_for_text() always has a font to render it.
 */
#pragma once

#include "lvgl.h"

/* Hero sizes */
LV_FONT_DECLARE(stripe_sans_24)
LV_FONT_DECLARE(stripe_sans_32)
LV_FONT_DECLARE(stripe_sans_40)
LV_FONT_DECLARE(stripe_sans_52)
LV_FONT_DECLARE(stripe_sans_60)
LV_FONT_DECLARE(stripe_sans_64)
LV_FONT_DECLARE(stripe_sans_76)
LV_FONT_DECLARE(stripe_sans_88)
LV_FONT_DECLARE(stripe_sans_96)

/* UI sizes: footer (18), label (20), subtitle (22) */
LV_FONT_DECLARE(stripe_sans_18)
LV_FONT_DECLARE(stripe_sans_20)
LV_FONT_DECLARE(stripe_sans_22)

/*
 * Return the generated font for `size_px`, or NULL if no font exists at that
 * size. Callers should treat NULL as a programming error: any size returned by
 * hero_size_for_text() is guaranteed to resolve.
 */
const lv_font_t *font_for_size(int size_px);
