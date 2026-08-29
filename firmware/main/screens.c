/*
 * Screen rendering for the deck in spec 6.
 *
 * Depends only on LVGL, so this builds for the device and for the host test
 * harness. No ESP-IDF, no SPI, no panel.
 */
#include "screens.h"

#include "baseline.h"
#include "fonts.h"
#include "hero_size.h"
#include "layout.h"

/*
 * Create a label whose *baseline* sits at `baseline_y`.
 *
 * The spec positions everything by baseline (5.1); LVGL positions by top-left.
 * Styles are set per object rather than through a shared static lv_style_t --
 * a single shared style would apply the last font and color set to every label
 * using it.
 */
static lv_obj_t *label_at_baseline(lv_obj_t *parent, const char *text,
                                   const lv_font_t *font, uint32_t color,
                                   int x, int baseline_y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x,
                   baseline_to_top(baseline_y,
                                   (int)lv_font_get_line_height(font),
                                   (int)font->base_line));
    return label;
}

/*
 * Create a label whose *top edge* sits at `top_y`. The spec's label position
 * (y=16) is the top of the text block, not a baseline (5.1).
 */
static lv_obj_t *label_at_top(lv_obj_t *parent, const char *text,
                              const lv_font_t *font, uint32_t color,
                              int x, int top_y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_pos(label, x, top_y);
    return label;
}

/*
 * Prepare the screen: drop the theme's own styling and paint the palette
 * background. Without remove_style_all() the default theme paints its own
 * background over ours.
 */
static void reset_screen(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void draw_dots(lv_obj_t *parent, int active, int total)
{
    if (total <= 0) {
        return;
    }

    const int span = (total - 1) * DOTS_GAP;
    /* PANEL_PX, not a literal: this was hardcoded to 240 and so centred the
     * dots around x=120 on the 480px panel, drawing them half off the left
     * edge as one wide smear. */
    const int x0 = (PANEL_PX - span) / 2;

    for (int i = 0; i < total; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        /* Drop theme borders, shadows, and padding; a dot is a filled circle. */
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOTS_RADIUS * 2, DOTS_RADIUS * 2);
        lv_obj_set_pos(dot, x0 + i * DOTS_GAP - DOTS_RADIUS,
                       DOTS_CENTER_Y - DOTS_RADIUS);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(
            dot, lv_color_hex(i == active ? COLOR_PRIMARY : COLOR_INACTIVE), 0);
        /* remove_style_all() leaves bg_opa transparent; without this the dots
         * are created and positioned but never painted. */
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void screen_draw_rotation(lv_obj_t *scr, const screen_data_t *data)
{
    reset_screen(scr);

    label_at_top(scr, data->label, font_for_size(SIZE_LABEL), COLOR_MUTED,
                 PAD_PX, LABEL_BASELINE_Y);

    /* Hero size is computed from the value, never hardcoded (spec 2.4). */
    /* Red outranks green: a screen cannot be both a gain and a breach, and if
     * the flags ever disagree the alert is the more important thing to say. */
    uint32_t hero_color = COLOR_PRIMARY;
    if (data->hero_is_alert) {
        hero_color = COLOR_RED;
    } else if (data->hero_is_gain) {
        hero_color = COLOR_GREEN;
    }

    const int hero_px = hero_size_for_text(data->hero);
    label_at_baseline(scr, data->hero, font_for_size(hero_px), hero_color,
                      PAD_PX, HERO_BASELINE_Y);

    if (data->subtitle && data->subtitle[0]) {
        label_at_baseline(scr, data->subtitle, font_for_size(SIZE_SUBTITLE),
                          data->subtitle_is_gain ? COLOR_GREEN : COLOR_MUTED,
                          PAD_PX, SUBTITLE_BASELINE_Y);
    }

    draw_dots(scr, data->dot_index, data->dot_count);
}

void screen_draw_stale(lv_obj_t *scr, const char *label, const char *hero,
                       const char *age, const char *footer)
{
    reset_screen(scr);

    /* Everything dims: the label to dim, the value to muted. A stale number
     * must never be presented with the same confidence as a live one. */
    label_at_top(scr, label, font_for_size(SIZE_LABEL), COLOR_DIM,
                 PAD_PX, LABEL_BASELINE_Y);

    const int hero_px = hero_size_for_text(hero);
    label_at_baseline(scr, hero, font_for_size(hero_px), COLOR_MUTED,
                      PAD_PX, HERO_BASELINE_Y);

    /* The age is the one thing shown at full attention, in amber. */
    label_at_baseline(scr, age, font_for_size(SIZE_SUBTITLE), COLOR_AMBER,
                      PAD_PX, SUBTITLE_BASELINE_Y);

    label_at_baseline(scr, footer, font_for_size(SIZE_FOOTER), COLOR_DIM,
                      PAD_PX, FOOTER_BASELINE_Y);
}

void screen_draw_auth_error(lv_obj_t *scr, const char *line1, const char *line2,
                            const char *hint, const char *errcode)
{
    reset_screen(scr);

    /* Amber label marks the degraded state (spec 4.2). */
    label_at_top(scr, "NO ACCESS", font_for_size(SIZE_LABEL), COLOR_AMBER,
                 PAD_PX, LABEL_BASELINE_Y);

    /* Two lines of plain language, sized to fit rather than at hero scale --
     * this is a sentence, not a number. */
    label_at_baseline(scr, line1, font_for_size(SIZE_MESSAGE), COLOR_PRIMARY,
                      PAD_PX, MSG_LINE1_Y);
    label_at_baseline(scr, line2, font_for_size(SIZE_MESSAGE), COLOR_PRIMARY,
                      PAD_PX, MSG_LINE2_Y);

    label_at_baseline(scr, hint, font_for_size(SIZE_LABEL), COLOR_MUTED,
                      PAD_PX, MSG_HINT_Y);

    /* Error code for support, deliberately quiet. */
    label_at_baseline(scr, errcode, font_for_size(SIZE_FOOTER), COLOR_DIM,
                      PAD_PX, FOOTER_BASELINE_Y);
}

void screen_draw_setup(lv_obj_t *scr, const char *line1, const char *ssid,
                       const char *hint, const char *version)
{
    reset_screen(scr);

    label_at_top(scr, "SETUP", font_for_size(SIZE_LABEL), COLOR_MUTED,
                 PAD_PX, LABEL_BASELINE_Y);

    label_at_baseline(scr, line1, font_for_size(SIZE_MESSAGE), COLOR_PRIMARY,
                      PAD_PX, MSG_LINE1_Y);

    /* The SSID is what the customer must act on, so it gets the accent. */
    label_at_baseline(scr, ssid, font_for_size(SIZE_MESSAGE), COLOR_GREEN,
                      PAD_PX, MSG_LINE2_Y);

    label_at_baseline(scr, hint, font_for_size(SIZE_LABEL), COLOR_MUTED,
                      PAD_PX, MSG_HINT_Y);

    label_at_baseline(scr, version, font_for_size(SIZE_FOOTER), COLOR_DIM,
                      PAD_PX, FOOTER_BASELINE_Y);
}

void screen_draw_battery(lv_obj_t *scr, const char *pct, const char *line,
                         const char *voltage, _Bool critical)
{
    reset_screen(scr);

    /* Amber for low, red for critical -- the same distinction the rest of the
     * deck draws between a degraded state and a threshold breach (spec 4.2). */
    const uint32_t accent = critical ? COLOR_RED : COLOR_AMBER;

    label_at_top(scr, critical ? "BATTERY CRITICAL" : "BATTERY LOW",
                 font_for_size(SIZE_LABEL), accent, PAD_PX, LABEL_BASELINE_Y);

    /* The percentage is what the owner acts on, so it gets hero treatment and
     * the accent color. */
    const int hero_px = hero_size_for_text(pct);
    label_at_baseline(scr, pct, font_for_size(hero_px), accent,
                      PAD_PX, HERO_BASELINE_Y);

    label_at_baseline(scr, line, font_for_size(SIZE_SUBTITLE), COLOR_PRIMARY,
                      PAD_PX, SUBTITLE_BASELINE_Y);

    /* Voltage is diagnostic, not actionable -- same footer treatment as the
     * auth error code. */
    label_at_baseline(scr, voltage, font_for_size(SIZE_FOOTER), COLOR_DIM,
                      PAD_PX, FOOTER_BASELINE_Y);
}
