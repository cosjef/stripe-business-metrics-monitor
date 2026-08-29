/*
 * Color diagnostic. Paints known solid patches so the panel's actual channel
 * mapping and black level can be read directly off the glass, instead of
 * inferred from how a mixed-color screen looks.
 *
 * Enable by setting COLORTEST_ENABLED to 1 in main.c.
 *
 * Layout (240x240), each patch 80x80 with a label:
 *   [ pure RED   ][ pure GREEN ][ pure BLUE  ]
 *   [ pure WHITE ][ pure BLACK ][  #121211   ]
 *   [ COLOR_BG   ][ COLOR_PRIM ][ COLOR_GRN  ]
 *
 * What to look for:
 *   - If RED patch looks blue (and BLUE looks red), the byte swap is wrong.
 *   - If BLACK looks white, color inversion is wrong.
 *   - If BLACK is black but #121211 is light, the value is not reaching the
 *     panel (something is painting over it).
 */
#include "colortest.h"
#include "layout.h"

#include "esp_log.h"
#include "lvgl.h"

/* Set to 1 for the near-black ramp (finding the panel's black floor);
 * 0 for the channel/palette check. */
#define COLORTEST_RAMP 0

static void patch(lv_obj_t *parent, int x, int y, uint32_t color, const char *name)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 80, 80);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(box);
    lv_label_set_text(lbl, name);
    /* Mid-gray reads against both light and dark patches. */
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x808080), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 2, 2);
}

void colortest_draw(void)
{
    /* Log what lv_color_hex() actually produces for the palette, so the value
     * LVGL holds can be compared against what the panel shows. */
    lv_color_t bg = lv_color_hex(COLOR_BG);
    lv_color_t k  = lv_color_hex(0x000000);
    ESP_LOGI("colortest", "COLOR_BG  -> r=%u g=%u b=%u", bg.red, bg.green, bg.blue);
    ESP_LOGI("colortest", "black     -> r=%u g=%u b=%u", k.red, k.green, k.blue);
    ESP_LOGI("colortest", "lv_color_t size=%u bytes", (unsigned)sizeof(lv_color_t));

    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x808080), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

#if COLORTEST_RAMP
    /* Near-black ramp, used to find this panel's usable black floor.
     * Result (2026-08-15): 0x00 is truly black, 0x04 already jumps to a
     * visible slate gray, and 0x04..0x30 all collapse to a similar mid-gray.
     * That is why COLOR_BG is pure black -- see the note in layout.h. */
    patch(scr,   0,   0, 0x000000, "00");
    patch(scr,  80,   0, 0x040404, "04");
    patch(scr, 160,   0, 0x080808, "08");

    patch(scr,   0,  80, 0x0C0C0C, "0C");
    patch(scr,  80,  80, 0x121211, "12*");
    patch(scr, 160,  80, 0x181818, "18");

    patch(scr,   0, 160, 0x202020, "20");
    patch(scr,  80, 160, 0x303030, "30");
    patch(scr, 160, 160, 0xFFFFFF, "FF");
#else
    /* Channel/palette check: R/G/B verify byte order, K/W verify inversion,
     * and the bottom row shows the real palette. */
    patch(scr,   0,   0, 0xFF0000, "R");
    patch(scr,  80,   0, 0x00FF00, "G");
    patch(scr, 160,   0, 0x0000FF, "B");

    patch(scr,   0,  80, 0xFFFFFF, "W");
    patch(scr,  80,  80, 0x000000, "K");
    patch(scr, 160,  80, COLOR_INACTIVE, "INA");

    patch(scr,   0, 160, COLOR_BG,      "BG");
    patch(scr,  80, 160, COLOR_PRIMARY, "FG");
    patch(scr, 160, 160, COLOR_GREEN,   "GRN");
#endif
}
