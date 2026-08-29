/*
 * Design mock: the MRR screen, current layout vs option B.
 *
 * Renders through the real LVGL, the real generated fonts and the real C6
 * layout constants, so what comes out is what the panel would show -- not an
 * illustration of it. Two PPMs are written and converted to PNG by the
 * calling script.
 *
 * This is a mock, not a test. It asserts nothing and is not part of the
 * suite; it exists so a layout can be judged before it costs a flash cycle.
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"

#include "baseline.h"
#include "fonts.h"
#include "hero_size.h"
#include "layout.h"
#include "screens.h"

/* Option B adds two tokens the current palette has no name for: a card fill
 * that lifts content off the black field, and a track for the bar. Both are
 * dark enough to stay in the AMOLED's cheap range. */
#define COLOR_CARD   0x1A1A18
#define COLOR_TRACK  0x2A2A28

static lv_obj_t *label_at(lv_obj_t *parent, const char *text,
                          const lv_font_t *font, uint32_t color, int x, int y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *label_at_baseline(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, uint32_t color,
                                   int x, int baseline_y)
{
    return label_at(parent, text, font, color, x,
                    baseline_to_top(baseline_y,
                                    (int)lv_font_get_line_height(font),
                                    (int)font->base_line));
}

static void prepare(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void dots(lv_obj_t *scr, int active, int total)
{
    const int span = (total - 1) * DOTS_GAP;
    const int x0 = (PANEL_PX - span) / 2;

    for (int i = 0; i < total; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOTS_RADIUS * 2, DOTS_RADIUS * 2);
        lv_obj_set_pos(d, x0 + i * DOTS_GAP - DOTS_RADIUS,
                       DOTS_CENTER_Y - DOTS_RADIUS);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            d, lv_color_hex(i == active ? COLOR_PRIMARY : COLOR_INACTIVE), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
}

/* ---- current design: label, hero, subtitle, dots ---- */
static void render_current(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    label_at(scr, "MRR", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    const int px = hero_size_for_text("$1,106.33");
    label_at_baseline(scr, "$1,106.33", font_for_size(px), COLOR_PRIMARY,
                      PAD_PX, HERO_BASELINE_Y);

    label_at_baseline(scr, "33 active", font_for_size(SIZE_SUBTITLE),
                      COLOR_MUTED, PAD_PX, SUBTITLE_BASELINE_Y);

    dots(scr, 0, 4);

    harness_render();
    harness_dump_ppm("mock_current.ppm");
}

/*
 * Option B, rendered by the REAL screen_draw_card().
 *
 * The first draft of this mock hand-built the layout, which meant it proved
 * nothing about the code that ships. This calls the shipped renderer, so what
 * comes out is what the panel will show.
 */
static void render_option_b(void)
{
    lv_obj_t *scr = harness_screen();

    /* No prepare() here: screen_draw_card owns its objects and caches
     * pointers to them. Cleaning the screen behind its back leaves those
     * pointers dangling, which is what hung the first version of this mock. */
    card_data_t d = {0};
    d.label = "MRR";
    d.hero = "$1,106.33";
    d.subtitle = "33 active";
    d.has_delta = true;
    d.delta = "+4.2%";
    d.comparison = "vs $1,061 last month";
    d.delta_is_gain = true;
    d.fill_pct = 78;
    d.dot_index = 0;
    d.dot_count = 4;

    screen_draw_card(scr, &d);
    harness_render();
    harness_dump_ppm("mock_option_b.ppm");
}

/* The honest state before seven days of history exist. */
static void render_collecting(void)
{
    lv_obj_t *scr = harness_screen();

    card_data_t d = {0};
    d.label = "MRR";
    d.hero = "$1,106.33";
    d.subtitle = "33 active";
    d.has_delta = false;
    d.comparison = "collecting history";
    d.dot_index = 0;
    d.dot_count = 4;

    screen_draw_card(scr, &d);
    harness_render();
    harness_dump_ppm("mock_collecting.ppm");
}

int main(void)
{
    harness_init();
    render_current();
    render_option_b();
    render_collecting();
    printf("wrote mock_current/option_b/collecting .ppm at %dx%d\n",
           HARNESS_W, HARNESS_H);
    return 0;
}
