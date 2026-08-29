/*
 * Screen rendering for the deck in spec 6.
 *
 * Depends only on LVGL, so this builds for the device and for the host test
 * harness. No ESP-IDF, no SPI, no panel.
 */
#include "screens.h"

#include <string.h>

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

    /*
     * Style the screen once, not on every draw.
     *
     * lv_obj_remove_style_all() strips a screen object back to nothing --
     * including state LVGL itself attaches when the display is registered. On
     * the first draw that is harmless, because the screen is fresh. On the
     * second it tears down a screen the display is actively bound to, and on
     * this QSPI AMOLED the result is a panel that never updates again: the
     * first frame lands, everything after it is discarded while every call
     * still reports success.
     *
     * That single line cost a day of debugging. It looked like a driver
     * problem, a locking problem, a task problem and a buffer problem in turn,
     * because the symptom -- one frame then silence -- is what all of those
     * would also produce.
     *
     * The style is idempotent, so applying it once is sufficient; children are
     * still cleaned on every draw, which is what actually needs to change.
     */
    /*
     * Set the background every time, but never remove_style_all.
     *
     * Setting a style repeatedly is idempotent and safe; stripping one is not.
     * A first attempt guarded this behind a static flag so it ran once, but
     * that left later screens unstyled -- the background was never re-applied
     * after a state screen changed it.
     */
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

/*
 * Persistent objects for the rotation screen.
 *
 * These are created once and updated thereafter. Rebuilding the tree on every
 * draw -- lv_obj_clean() plus fresh labels -- destroyed state the display
 * binds to the screen on the C6's QSPI AMOLED, and the panel stopped
 * accepting frames after the first while every call still returned success.
 *
 * Verified on hardware both ways: rebuilding froze on frame one, updating text
 * in place rotated correctly every five seconds.
 */
static struct {
    lv_obj_t *screen;          /* which screen these belong to */
    lv_obj_t *label;
    lv_obj_t *hero;
    lv_obj_t *subtitle;
    lv_obj_t *dots[SCREENS_MAX_DOTS];
    int dot_count;
    int hero_px;               /* current hero font size, to avoid resetting */
    int hero_pos_px;           /* size the position was computed for */
} s_rot;

static void build_rotation_objects(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    memset(&s_rot, 0, sizeof(s_rot));
    s_rot.screen = scr;

    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_rot.label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_rot.label, font_for_size(SIZE_LABEL), 0);
    lv_obj_set_style_text_color(s_rot.label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(s_rot.label, PAD_PX, LABEL_BASELINE_Y);

    s_rot.hero = lv_label_create(scr);
    s_rot.hero_px = SIZE_HERO_MAX;
    lv_obj_set_style_text_font(s_rot.hero, font_for_size(SIZE_HERO_MAX), 0);
    lv_obj_set_style_text_color(s_rot.hero, lv_color_hex(COLOR_PRIMARY), 0);

    s_rot.subtitle = lv_label_create(scr);
    lv_obj_set_style_text_font(s_rot.subtitle, font_for_size(SIZE_SUBTITLE), 0);
    lv_obj_set_style_text_color(s_rot.subtitle, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(s_rot.subtitle, PAD_PX,
                   baseline_to_top(SUBTITLE_BASELINE_Y,
                                   (int)lv_font_get_line_height(font_for_size(SIZE_SUBTITLE)),
                                   (int)font_for_size(SIZE_SUBTITLE)->base_line));

    /* Every dot the deck can ever need, created once and shown or hidden. */
    for (int i = 0; i < SCREENS_MAX_DOTS; i++) {
        lv_obj_t *dot = lv_obj_create(scr);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOTS_RADIUS * 2, DOTS_RADIUS * 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s_rot.dots[i] = dot;
    }
}

void screen_draw_rotation(lv_obj_t *scr, const screen_data_t *data)
{
    /* Same guard as screen_draw_card: a stale pointer into a cleaned screen
     * is indistinguishable from a valid one by screen identity alone. */
    if (s_rot.screen != scr || s_rot.label == NULL ||
        lv_obj_get_parent(s_rot.label) != scr) {
        build_rotation_objects(scr);
    }

    lv_label_set_text(s_rot.label, data->label);

    /*
     * Hero size is computed from the value (spec 2.4), so it changes between
     * screens. Only touch the font when it actually differs -- setting a font
     * invalidates layout, and doing it needlessly on every draw is the kind of
     * churn this restructuring exists to avoid.
     */
    const int hero_px = hero_size_for_text(data->hero);
    if (hero_px != s_rot.hero_px) {
        lv_obj_set_style_text_font(s_rot.hero, font_for_size(hero_px), 0);
        s_rot.hero_px = hero_px;
    }

    uint32_t hero_color = COLOR_PRIMARY;
    if (data->hero_is_alert) {
        hero_color = COLOR_RED;
    } else if (data->hero_is_gain) {
        hero_color = COLOR_GREEN;
    }
    lv_obj_set_style_text_color(s_rot.hero, lv_color_hex(hero_color), 0);
    lv_label_set_text(s_rot.hero, data->hero);

    /*
     * Reposition only when the font changed.
     *
     * The build that rotated correctly on hardware created its labels, set
     * their position once, and thereafter called nothing but
     * lv_label_set_text. Repositioning on every draw is the main thing this
     * restructure added over that, so it is now conditional -- the baseline
     * only moves when the hero size does.
     */
    if (hero_px != s_rot.hero_pos_px) {
        lv_obj_set_pos(s_rot.hero, PAD_PX,
                       baseline_to_top(HERO_BASELINE_Y,
                                       (int)lv_font_get_line_height(font_for_size(hero_px)),
                                       (int)font_for_size(hero_px)->base_line));
        s_rot.hero_pos_px = hero_px;
    }

    if (data->subtitle && data->subtitle[0]) {
        lv_label_set_text(s_rot.subtitle, data->subtitle);
        lv_obj_set_style_text_color(
            s_rot.subtitle,
            lv_color_hex(data->subtitle_is_gain ? COLOR_GREEN : COLOR_MUTED), 0);
        lv_obj_clear_flag(s_rot.subtitle, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* Hidden, not destroyed. */
        lv_obj_add_flag(s_rot.subtitle, LV_OBJ_FLAG_HIDDEN);
    }

    /* Dots: reposition and recolour the ones in use, hide the rest. */
    const int total = data->dot_count > SCREENS_MAX_DOTS
                          ? SCREENS_MAX_DOTS : data->dot_count;
    const int span = (total - 1) * DOTS_GAP;
    const int x0 = (PANEL_PX - span) / 2;

    for (int i = 0; i < SCREENS_MAX_DOTS; i++) {
        if (i >= total) {
            lv_obj_add_flag(s_rot.dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(s_rot.dots[i], x0 + i * DOTS_GAP - DOTS_RADIUS,
                       DOTS_CENTER_Y - DOTS_RADIUS);
        lv_obj_set_style_bg_color(
            s_rot.dots[i],
            lv_color_hex(i == data->dot_index ? COLOR_PRIMARY : COLOR_INACTIVE), 0);
        lv_obj_clear_flag(s_rot.dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}


/*
 * Persistent objects for the card layout, same discipline as s_rot: create
 * once, update thereafter.
 */
static struct {
    lv_obj_t *screen;
    lv_obj_t *card;
    lv_obj_t *label;
    lv_obj_t *pill;
    lv_obj_t *subtitle;
    lv_obj_t *hero;
    lv_obj_t *track;
    lv_obj_t *fill;
    lv_obj_t *caption;
    lv_obj_t *dots[SCREENS_MAX_DOTS];
    int hero_px;
    int hero_pos_px;
} s_card;

static void build_card_objects(lv_obj_t *scr)
{
    lv_obj_clean(scr);
    memset(&s_card, 0, sizeof(s_card));
    s_card.screen = scr;

    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    /* Card first, so everything after it paints on top. */
    s_card.card = lv_obj_create(scr);
    lv_obj_remove_style_all(s_card.card);
    lv_obj_set_pos(s_card.card, PAD_PX, CARD_Y);
    lv_obj_set_size(s_card.card, card_w, CARD_H);
    lv_obj_set_style_bg_color(s_card.card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(s_card.card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card.card, CARD_RADIUS, 0);
    lv_obj_clear_flag(s_card.card, LV_OBJ_FLAG_SCROLLABLE);

    /* Label sits outside the card, where the eye starts. */
    s_card.label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_card.label, font_for_size(SIZE_LABEL), 0);
    lv_obj_set_style_text_color(s_card.label, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(s_card.label, PAD_PX, LABEL_BASELINE_Y);

    /* Delta pill, right-aligned on the label row. */
    s_card.pill = lv_label_create(scr);
    lv_obj_set_style_text_font(s_card.pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_bg_opa(s_card.pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card.pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(s_card.pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(s_card.pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(s_card.pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(s_card.pill, PILL_PAD_Y, 0);

    s_card.subtitle = lv_label_create(scr);
    lv_obj_set_style_text_font(s_card.subtitle, font_for_size(SIZE_SUBTITLE), 0);
    lv_obj_set_style_text_color(s_card.subtitle, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_pos(s_card.subtitle, inner_x, CARD_Y + CARD_SUBTITLE_DY);

    s_card.hero = lv_label_create(scr);
    s_card.hero_px = SIZE_HERO_MAX;
    lv_obj_set_style_text_font(s_card.hero, font_for_size(SIZE_HERO_MAX), 0);
    lv_obj_set_style_text_color(s_card.hero, lv_color_hex(COLOR_PRIMARY), 0);

    const int bar_w = card_w - 2 * CARD_PAD;

    s_card.track = lv_obj_create(scr);
    lv_obj_remove_style_all(s_card.track);
    lv_obj_set_pos(s_card.track, inner_x, CARD_Y + CARD_BAR_DY);
    lv_obj_set_size(s_card.track, bar_w, CARD_BAR_H);
    lv_obj_set_style_bg_color(s_card.track, lv_color_hex(COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(s_card.track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card.track, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(s_card.track, LV_OBJ_FLAG_SCROLLABLE);

    s_card.fill = lv_obj_create(scr);
    lv_obj_remove_style_all(s_card.fill);
    lv_obj_set_pos(s_card.fill, inner_x, CARD_Y + CARD_BAR_DY);
    lv_obj_set_size(s_card.fill, 0, CARD_BAR_H);
    lv_obj_set_style_bg_opa(s_card.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_card.fill, LV_RADIUS_CIRCLE, 0);
    lv_obj_clear_flag(s_card.fill, LV_OBJ_FLAG_SCROLLABLE);

    s_card.caption = lv_label_create(scr);
    lv_obj_set_style_text_font(s_card.caption, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(s_card.caption, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_pos(s_card.caption, inner_x, CARD_Y + CARD_CAPTION_DY);

    for (int i = 0; i < SCREENS_MAX_DOTS; i++) {
        lv_obj_t *dot = lv_obj_create(scr);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOTS_RADIUS * 2, DOTS_RADIUS * 2);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s_card.dots[i] = dot;
    }
}

void screen_draw_card(lv_obj_t *scr, const card_data_t *data)
{
    /*
     * Rebuild unless the cached objects are still real children of this
     * screen.
     *
     * Matching on the screen pointer alone is not enough: anything that calls
     * lv_obj_clean() on the same screen deletes these objects while leaving
     * s_card.screen pointing at it, and the next draw then writes through
     * freed pointers. lv_obj_get_parent() is the cheap way to notice, and it
     * turns a hang into a rebuild.
     */
    if (s_card.screen != scr || s_card.label == NULL ||
        lv_obj_get_parent(s_card.label) != scr) {
        build_card_objects(scr);
    }
    s_rot.screen = NULL;   /* the rotation layout must rebuild if it returns */

    lv_label_set_text(s_card.label, data->label);
    lv_label_set_text(s_card.subtitle, data->subtitle ? data->subtitle : "");

    /* Hero: size follows the value, so only touch the font when it changes. */
    const int hero_px = hero_size_for_text(data->hero);
    if (hero_px != s_card.hero_px) {
        lv_obj_set_style_text_font(s_card.hero, font_for_size(hero_px), 0);
        s_card.hero_px = hero_px;
    }
    lv_label_set_text(s_card.hero, data->hero);

    if (hero_px != s_card.hero_pos_px) {
        lv_obj_set_pos(s_card.hero, PAD_PX + CARD_PAD,
                       baseline_to_top(CARD_Y + CARD_HERO_BASELINE_DY,
                                       (int)lv_font_get_line_height(font_for_size(hero_px)),
                                       (int)font_for_size(hero_px)->base_line));
        s_card.hero_pos_px = hero_px;
    }

    /*
     * The trend, or an honest admission that there is not one yet.
     *
     * has_delta is false until enough daily samples exist to know a
     * direction. In that state the pill and bar still render, but muted and
     * empty, with the caption saying so -- the device shows that a trend is
     * coming, without asserting one it has not measured. Spec 1 principle 4:
     * it never lies, and a plausible-looking 0.0% would be a lie.
     */
    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int bar_w = card_w - 2 * CARD_PAD;

    if (data->has_delta) {
        const uint32_t accent = data->delta_is_gain ? COLOR_GREEN : COLOR_RED;

        lv_label_set_text(s_card.pill, data->delta);
        lv_obj_set_style_bg_color(s_card.pill, lv_color_hex(accent), 0);
        /* Dark text on a saturated pill: the pill is the lit element. */
        lv_obj_set_style_text_color(s_card.pill, lv_color_hex(COLOR_BG), 0);

        int pct = data->fill_pct;
        if (pct < 0) {
            pct = 0;
        } else if (pct > 100) {
            pct = 100;
        }
        lv_obj_set_size(s_card.fill, (bar_w * pct) / 100, CARD_BAR_H);
        lv_obj_set_style_bg_color(s_card.fill, lv_color_hex(accent), 0);
        lv_obj_clear_flag(s_card.fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* Muted pill on the track colour, and no fill at all. */
        lv_label_set_text(s_card.pill, "--");
        lv_obj_set_style_bg_color(s_card.pill, lv_color_hex(COLOR_TRACK), 0);
        lv_obj_set_style_text_color(s_card.pill, lv_color_hex(COLOR_DIM), 0);
        lv_obj_add_flag(s_card.fill, LV_OBJ_FLAG_HIDDEN);
    }

    /* Right-align the pill after its text changed, or the width is stale. */
    lv_obj_align(s_card.pill, LV_ALIGN_TOP_RIGHT, -PAD_PX,
                 LABEL_BASELINE_Y - PILL_PAD_Y);

    lv_label_set_text(s_card.caption, data->comparison ? data->comparison : "");

    const int total = data->dot_count > SCREENS_MAX_DOTS
                          ? SCREENS_MAX_DOTS : data->dot_count;
    const int span = (total - 1) * DOTS_GAP;
    const int x0 = (PANEL_PX - span) / 2;

    for (int i = 0; i < SCREENS_MAX_DOTS; i++) {
        if (i >= total) {
            lv_obj_add_flag(s_card.dots[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(s_card.dots[i], x0 + i * DOTS_GAP - DOTS_RADIUS,
                       DOTS_CENTER_Y - DOTS_RADIUS);
        lv_obj_set_style_bg_color(
            s_card.dots[i],
            lv_color_hex(i == data->dot_index ? COLOR_PRIMARY : COLOR_INACTIVE), 0);
        lv_obj_clear_flag(s_card.dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

void screen_draw_stale(lv_obj_t *scr, const char *label, const char *hero,
                       const char *age, const char *footer)
{
    s_rot.screen = NULL;   /* state screens rebuild; force a rebuild next time */
    s_card.screen = NULL;
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
    s_rot.screen = NULL;
    s_card.screen = NULL;
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
    s_rot.screen = NULL;
    s_card.screen = NULL;
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
    s_rot.screen = NULL;
    s_card.screen = NULL;
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
