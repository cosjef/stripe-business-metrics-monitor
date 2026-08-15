/*
 * Stripe Revenue Display -- Stage 1 bring-up.
 *
 * Renders the three-zone skeleton (spec 5.1) with hardcoded values, to prove
 * the hardware path and validate layout geometry on real glass before any
 * network code exists.
 *
 * Typeface: SF Compact Bold, not the monospace face spec 5.4 calls for. See
 * tools/gen_fonts.sh for the reasoning and hero_size.h for what that changes
 * about width measurement.
 *
 * Known Stage 1 limitations, tracked in firmware-build-plan.md:
 *   - No partial-window updates yet (spec 5.3); LVGL redraws as it sees fit.
 *   - Single hardcoded screen; rotation and the other eight screens are Stage 2.
 */
#include "display.h"
#include "board_config.h"
#include "layout.h"
#include "hero_size.h"
#include "baseline.h"
#include "fonts.h"
#include "colortest.h"

/* Set to 1 to draw the color diagnostic instead of the skeleton. */
#define COLORTEST_ENABLED 0

#include <string.h>

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* Hardcoded Stage 1 fixture, matching the MRR mockup in 01-mrr.png */
#define FIXTURE_LABEL     "MRR"
#define FIXTURE_HERO      "$6.5k"
#define FIXTURE_SUBTITLE  "+$118 today"
#define FIXTURE_DOT_INDEX (0)
#define FIXTURE_DOT_COUNT (6)

/*
 * Create a label whose *baseline* sits at `baseline_y`.
 *
 * The spec positions everything by baseline (5.1); LVGL positions by top-left.
 * A font's baseline sits `line_height - base_line` below its top edge, where
 * lv_font_t.base_line is the descent below the baseline.
 */
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, uint32_t color,
                            int x, int baseline_y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);

    /* Per-object local styles, not a shared static style -- a single static
     * lv_style_t reused across labels would apply the last-set font and color
     * to every one of them. */
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);

    lv_obj_set_pos(label, x,
                   baseline_to_top(baseline_y,
                                   (int)lv_font_get_line_height(font),
                                   (int)font->base_line));

    return label;
}

static void draw_rotation_dots(lv_obj_t *parent, int active, int total)
{
    const int span = (total - 1) * DOTS_GAP;
    const int x0 = (LCD_H_RES - span) / 2;

    for (int i = 0; i < total; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        /* Drop theme borders/shadows/padding; a dot is just a filled circle. */
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOTS_RADIUS * 2, DOTS_RADIUS * 2);
        lv_obj_set_pos(dot, x0 + i * DOTS_GAP - DOTS_RADIUS,
                       DOTS_CENTER_Y - DOTS_RADIUS);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, lv_color_hex(i == active ? COLOR_PRIMARY : COLOR_INACTIVE), 0);
        /* remove_style_all() leaves bg_opa transparent; without this the dots
         * are painted but invisible. */
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void draw_skeleton(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* Strip the theme's styling first. LVGL's default theme paints its own
     * background onto the screen; leaving it in place lets the theme color
     * show through instead of ours. */
    lv_obj_remove_style_all(scr);

    /* #121211, not pure black -- an IPS backlight makes #000000 read as dark
     * charcoal with visible edge bleed (spec 3.1). */
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* The label's y=16 in the spec is the top of the text block, not a
     * baseline (5.1), so offset by the ascent to land it correctly. */
    const lv_font_t *label_font = font_for_size(SIZE_LABEL);
    make_label(scr, FIXTURE_LABEL, label_font, COLOR_MUTED, PAD_PX,
               LABEL_BASELINE_Y + font_ascent((int)lv_font_get_line_height(label_font),
                                              (int)label_font->base_line));

    /* Hero size is computed from the value, never hardcoded (spec 2.4). */
    const int hero_px = hero_size_for_text(FIXTURE_HERO);
    make_label(scr, FIXTURE_HERO, font_for_size(hero_px), COLOR_PRIMARY,
               PAD_PX, HERO_BASELINE_Y);

    /* Green means exactly one thing: realized positive movement (spec 4.2). */
    make_label(scr, FIXTURE_SUBTITLE, font_for_size(SIZE_SUBTITLE), COLOR_GREEN,
               PAD_PX, SUBTITLE_BASELINE_Y);

    draw_rotation_dots(scr, FIXTURE_DOT_INDEX, FIXTURE_DOT_COUNT);
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());

    const int hero_px = hero_size_for_text(FIXTURE_HERO);
    ESP_LOGI(TAG, "hero '%s': %dpx (%.1fmm at 8.7px/mm), width %d/%dpx",
             FIXTURE_HERO, hero_px, hero_px / 8.7,
             text_width_px(FIXTURE_HERO, hero_px), TEXT_COLUMN_PX);

    lvgl_port_lock(0);
#if COLORTEST_ENABLED
    colortest_draw();
#else
    draw_skeleton();
#endif
    lvgl_port_unlock();

    ESP_LOGI(TAG, "%s rendered",
             COLORTEST_ENABLED ? "color test" : "skeleton");
}
