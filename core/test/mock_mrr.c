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

/* The current PAID SUBS screen, for a fair comparison. */
static void render_subs_current(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    label_at(scr, "PAID SUBS", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    const int px = hero_size_for_text("33");
    label_at_baseline(scr, "33", font_for_size(px), COLOR_PRIMARY,
                      PAD_PX, HERO_BASELINE_Y);

    label_at_baseline(scr, "active", font_for_size(SIZE_SUBTITLE),
                      COLOR_MUTED, PAD_PX, SUBTITLE_BASELINE_Y);

    dots(scr, 1, 4);

    harness_render();
    harness_dump_ppm("mock_subs_current.ppm");
}

/*
 * PAID SUBS, option B: the count plus what it is made of.
 *
 * Hand-built here rather than calling a renderer, because the renderer does
 * not exist yet -- this is the design question, not the implementation. The
 * numbers are this account's real monthly-equivalent tiers.
 *
 * Why a segmented bar rather than a growth bar: "33 active" is already said
 * on the MRR card's subtitle, so repeating the count with a trend would tell
 * the same story twice. The composition is the thing MRR cannot say, and the
 * one that would change a pricing decision.
 */
static void render_subs(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "PAID SUBS", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    label_at(scr, "3 tiers", font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_text("33");
    label_at_baseline(scr, "33", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    /*
     * Segmented bar: one span per tier, widths by share of the base.
     * Descending by count, so the dominant tier reads first.
     */
    const int bar_w = card_w - 2 * CARD_PAD;
    const int bar_y = CARD_Y + CARD_BAR_DY;
    const int GAPPX = 3;

    struct { int count; uint32_t color; } seg[] = {
        { 14, 0x5DCAA5 },   /* $29 -- 42% */
        { 12, 0x4A9E86 },   /* $49 -- 36% */
        {  6, 0x37725F },   /* $13 -- 18% */
        {  1, 0x8E8C84 },   /* annual outlier -- 3%, neutral */
    };
    const int nseg = 4, total = 33;

    int x = inner_x;
    for (int i = 0; i < nseg; i++) {
        int w = (bar_w * seg[i].count) / total - GAPPX;
        if (w < 4) {
            w = 4;          /* a tier of one must still be visible */
        }
        lv_obj_t *sg = lv_obj_create(scr);
        lv_obj_remove_style_all(sg);
        lv_obj_set_pos(sg, x, bar_y);
        lv_obj_set_size(sg, w, CARD_BAR_H);
        lv_obj_set_style_bg_color(sg, lv_color_hex(seg[i].color), 0);
        lv_obj_set_style_bg_opa(sg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(sg, 4, 0);
        lv_obj_clear_flag(sg, LV_OBJ_FLAG_SCROLLABLE);
        x += w + GAPPX;
    }

    /* Legend: the tiers named, in the same order as the bar. */
    label_at(scr, "$29 x14   $49 x12   $13 x6",
             font_for_size(SIZE_FOOTER), COLOR_DIM,
             inner_x, CARD_Y + CARD_CAPTION_DY);

    dots(scr, 1, 4);

    harness_render();
    harness_dump_ppm("mock_subs.ppm");
}

/*
 * PAID SUBS, option 1: the flow behind the count.
 *
 * A count of 33 looks identical whether the month added 3 or added 10 and
 * lost 7. This account did the latter -- +10 / -7 for a net of +3, a 17.5%
 * monthly churn rate -- and the current screen hides all of it.
 *
 * Two opposed bars from a shared centre line: gained to the right, lost to
 * the left, scaled against the larger of the two so the bigger side always
 * fills its half. The pill carries the net, which is the summary the owner
 * actually wants.
 *
 * Rejected on the way here: a segmented bar of price tiers. It rendered
 * well, but tiers change when the owner reprices -- twice a year, maybe --
 * and a glanceable device should spend its pixels on what moves.
 */
static void render_subs_flow(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "PAID SUBS", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    /* Net as the pill: green because +3 is realized growth (spec 4.2). */
    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, "net +3");
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    label_at(scr, "active", font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_width("33", card_w - 2 * CARD_PAD);
    label_at_baseline(scr, "33", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    /*
     * Opposed bars. The centre is the midpoint of the card's inner width;
     * each side scales against max(new, churned) so the larger side fills
     * its half and the ratio between them is the visible fact.
     */
    const int bar_w = card_w - 2 * CARD_PAD;
    const int bar_y = CARD_Y + CARD_BAR_DY;
    const int half = bar_w / 2;
    const int cx = inner_x + half;

    const int gained = 10, lost = 7;
    const int peak = gained > lost ? gained : lost;

    const int gw = (half - 4) * gained / peak;
    const int lw = (half - 4) * lost / peak;

    lv_obj_t *g = lv_obj_create(scr);
    lv_obj_remove_style_all(g);
    lv_obj_set_pos(g, cx + 4, bar_y);
    lv_obj_set_size(g, gw, CARD_BAR_H);
    lv_obj_set_style_bg_color(g, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);

    /* Lost is amber, not red: churn is ordinary business, not a breach.
     * Red is reserved for FAILED (spec 4.2). */
    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, cx - 4 - lw, bar_y);
    lv_obj_set_size(l, lw, CARD_BAR_H);
    lv_obj_set_style_bg_color(l, lv_color_hex(COLOR_AMBER), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);

    /*
     * Captions anchored to their own bars, not laid out as one string.
     *
     * A single left-aligned caption drifts out of register the moment the
     * bars change length, which is every month. Each label is positioned
     * against the end of the bar it describes: the lost count sits at the
     * left edge of the amber span, the joined count at the left edge of the
     * green one, so the number always sits under its own bar.
     */
    label_at(scr, "7 left", font_for_size(SIZE_FOOTER), COLOR_DIM,
             cx - 4 - lw, CARD_Y + CARD_CAPTION_DY);

    label_at(scr, "10 joined", font_for_size(SIZE_FOOTER), COLOR_DIM,
             cx + 4, CARD_Y + CARD_CAPTION_DY);

    dots(scr, 1, 4);

    harness_render();
    harness_dump_ppm("mock_subs_flow.ppm");
}

/*
 * PAID SUBS flow, flush-left variant.
 *
 * The centred version anchors the bars to the card's midpoint, so neither end
 * aligns with the hero or subtitle above them and the block reads adrift. The
 * whole flow group is laid out left to right instead: lost then gained, one
 * continuous run starting at the same inner edge as everything else, with
 * each caption under the start of its own span.
 *
 * The trade: the centred version shows the RATIO of the two sides at a
 * glance, because they oppose across a fixed axis. This one shows the TOTAL
 * flow as one bar and makes the split a proportion within it, which is a
 * different reading. Both are honest; the question is which fact matters at
 * arm's length.
 */
static void render_subs_flow_left(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "PAID SUBS", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, "net +3");
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    label_at(scr, "active", font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_width("33", card_w - 2 * CARD_PAD);
    label_at_baseline(scr, "33", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    /* One run, flush left: total flow split by proportion. */
    const int bar_w = card_w - 2 * CARD_PAD;
    const int bar_y = CARD_Y + CARD_BAR_DY;
    const int GAPPX = 4;

    const int gained = 10, lost = 7;
    const int total = gained + lost;

    const int lw = (bar_w - GAPPX) * lost / total;
    const int gw = (bar_w - GAPPX) - lw;

    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, inner_x, bar_y);
    lv_obj_set_size(l, lw, CARD_BAR_H);
    lv_obj_set_style_bg_color(l, lv_color_hex(COLOR_AMBER), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *g = lv_obj_create(scr);
    lv_obj_remove_style_all(g);
    lv_obj_set_pos(g, inner_x + lw + GAPPX, bar_y);
    lv_obj_set_size(g, gw, CARD_BAR_H);
    lv_obj_set_style_bg_color(g, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);

    label_at(scr, "7 left", font_for_size(SIZE_FOOTER), COLOR_DIM,
             inner_x, CARD_Y + CARD_CAPTION_DY);
    label_at(scr, "10 joined", font_for_size(SIZE_FOOTER), COLOR_DIM,
             inner_x + lw + GAPPX, CARD_Y + CARD_CAPTION_DY);

    dots(scr, 1, 4);

    harness_render();
    harness_dump_ppm("mock_subs_flow_left.ppm");
}

/*
 * MRR card with ARR folded in as a footer line.
 *
 * ARR is mrr_cents * 12 -- a restatement in different units, carrying no
 * information MRR does not. It does not earn a rotation slot on a deck whose
 * discipline is one fact per screen. But it is the figure people quote, and
 * "$13,276/yr" lands differently from "$1,106/mo" even though they are the
 * same number, so it is shown next to the value it derives from rather than
 * implied to be independent news.
 *
 * The line goes inside the card, under the caption, in the footer colour --
 * quiet, and visibly subordinate to the hero. There is room: the caption sits
 * 248px into a 300px card, leaving 52px, and a 26px footer needs about 34
 * with its leading.
 */
static void render_mrr_with_arr(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "MRR", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, "+4.2%");
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    label_at(scr, "33 active", font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_width("$1,106.33", card_w - 2 * CARD_PAD);
    label_at_baseline(scr, "$1,106.33", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    const int bar_w = card_w - 2 * CARD_PAD;
    const int bar_y = CARD_Y + CARD_BAR_DY;

    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, inner_x, bar_y);
    lv_obj_set_size(track, bar_w, CARD_BAR_H);
    lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *fill = lv_obj_create(scr);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, inner_x, bar_y);
    lv_obj_set_size(fill, (bar_w * 78) / 100, CARD_BAR_H);
    lv_obj_set_style_bg_color(fill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, LV_RADIUS_CIRCLE, 0);

    label_at(scr, "vs $1,061 last month", font_for_size(SIZE_FOOTER),
             COLOR_DIM, inner_x, CARD_Y + CARD_CAPTION_DY);

    /* The ARR line: dim, inside the card, clearly subordinate. */
    label_at(scr, "$13,276 / yr", font_for_size(SIZE_FOOTER),
             COLOR_DIM, inner_x, CARD_Y + CARD_CAPTION_DY + 34);

    dots(scr, 0, 4);

    harness_render();
    harness_dump_ppm("mock_mrr_arr.ppm");
}

/* The ARR screen as it stands today, for comparison. */
static void render_arr_screen(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    label_at(scr, "ANNUAL RUN RATE", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    const int px = hero_size_for_text("$13,276");
    label_at_baseline(scr, "$13,276", font_for_size(px), COLOR_PRIMARY,
                      PAD_PX, HERO_BASELINE_Y);

    label_at_baseline(scr, "at current MRR", font_for_size(SIZE_SUBTITLE),
                      COLOR_MUTED, PAD_PX, SUBTITLE_BASELINE_Y);

    dots(scr, 3, 5);

    harness_render();
    harness_dump_ppm("mock_arr_screen.ppm");
}

/*
 * ARR with the card treatment and a trend.
 *
 * Worth being honest about what this can and cannot be: ARR is mrr_cents * 12,
 * so its percentage change is IDENTICAL to MRR's -- the twelve cancels. The
 * delta pill therefore reads the same on both screens by arithmetic, not by
 * coincidence, and no amount of layout can make it independent news.
 *
 * What it can legitimately add is scale framing: the annual figure is the one
 * people quote, and the year-over-year money amount ("+$536/yr") is a
 * different-feeling quantity from "+4.2%" even though it is the same fact.
 */
static void render_arr_card(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "ANNUAL RUN RATE", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, "+4.2%");
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    label_at(scr, "at current MRR", font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_width("$13,276", card_w - 2 * CARD_PAD);
    label_at_baseline(scr, "$13,276", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    const int bar_w = card_w - 2 * CARD_PAD;
    const int bar_y = CARD_Y + CARD_BAR_DY;

    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_pos(track, inner_x, bar_y);
    lv_obj_set_size(track, bar_w, CARD_BAR_H);
    lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_TRACK), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *fill = lv_obj_create(scr);
    lv_obj_remove_style_all(fill);
    lv_obj_set_pos(fill, inner_x, bar_y);
    lv_obj_set_size(fill, (bar_w * 78) / 100, CARD_BAR_H);
    lv_obj_set_style_bg_color(fill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fill, LV_RADIUS_CIRCLE, 0);

    /* The annual money amount, which is the one thing here that MRR's card
     * does not already say in the same units. */
    label_at(scr, "+$536 vs last month", font_for_size(SIZE_FOOTER),
             COLOR_DIM, inner_x, CARD_Y + CARD_CAPTION_DY);

    dots(scr, 3, 5);

    harness_render();
    harness_dump_ppm("mock_arr_card.ppm");
}

/*
 * ARPU as customer-mix quality.
 *
 * The average alone is inert. What moves, and what nobody else on the deck
 * can say, is whether the customers being won are worth more than the ones
 * being lost -- here $35.40 against $25.00, so the mix is improving from both
 * ends at once.
 *
 * Three labellings, because the numbers alone made the reader do the
 * comparison themselves. Each states the conclusion at a different volume.
 */
static void arpu_card_base(const char *pill_text, const char *caption,
                           const char *subtitle)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "ARPU", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, pill_text);
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    label_at(scr, subtitle, font_for_size(SIZE_SUBTITLE), COLOR_MUTED,
             inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int col = card_w - 2 * CARD_PAD;
    const int px = hero_size_for_width("$33.53", col);
    label_at_baseline(scr, "$33.53", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    /*
     * Two opposed spans scaled against the larger value: joining on the left
     * in green, leaving on the right in amber. Unlike the flow bar this is a
     * comparison of two rates, not a split of one total, so the widths are
     * proportional to the dollar amounts.
     */
    const int bar_y = CARD_Y + CARD_BAR_DY;
    const int joining = 3540, leaving = 2500;
    const int peak = joining > leaving ? joining : leaving;
    const int gw = col * joining / peak;
    const int lw = col * leaving / peak;

    lv_obj_t *g = lv_obj_create(scr);
    lv_obj_remove_style_all(g);
    lv_obj_set_pos(g, inner_x, bar_y);
    lv_obj_set_size(g, gw, CARD_BAR_H);
    lv_obj_set_style_bg_color(g, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);

    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, inner_x, bar_y + CARD_BAR_H + 6);
    lv_obj_set_size(l, lw, CARD_BAR_H);
    lv_obj_set_style_bg_color(l, lv_color_hex(COLOR_AMBER), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);

    label_at(scr, caption, font_for_size(SIZE_FOOTER), COLOR_DIM,
             inner_x, CARD_Y + CARD_CAPTION_DY + 20);

    dots(scr, 5, 7);
    harness_render();
}

/* 1: name the two groups in plain words. */
static void render_arpu_a(void)
{
    arpu_card_base("mix improving", "joining $35.40   leaving $25.00",
                   "per subscriber");
    harness_dump_ppm("mock_arpu_a.ppm");
}

/* 2: state the conclusion as the subtitle, numbers as evidence. */
static void render_arpu_b(void)
{
    arpu_card_base("+$10.40", "new customers worth more than lost ones",
                   "winning better customers");
    harness_dump_ppm("mock_arpu_b.ppm");
}

/* 3: lead with the gap itself. */
static void render_arpu_c(void)
{
    arpu_card_base("41% better", "each new customer beats each lost one",
                   "per subscriber");
    harness_dump_ppm("mock_arpu_c.ppm");
}

/*
 * ARPU mix, with each label under the bar it describes.
 *
 * The first version stacked two bars and put both labels in one row beneath
 * them, so nothing said which bar was which -- the reader had to infer it
 * from colour. Each bar now carries its own label directly underneath, in the
 * bar's own colour, so the pairing needs no decoding.
 */
static void render_arpu_paired(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;
    const int col = card_w - 2 * CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, MIX_CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "ARPU", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    lv_obj_t *pill = lv_label_create(scr);
    lv_label_set_text(pill, "+$10.40");
    lv_obj_set_style_text_font(pill, font_for_size(SIZE_FOOTER), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_color(pill, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_right(pill, PILL_PAD_X, 0);
    lv_obj_set_style_pad_top(pill, PILL_PAD_Y, 0);
    lv_obj_set_style_pad_bottom(pill, PILL_PAD_Y, 0);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -PAD_PX, LABEL_BASELINE_Y - PILL_PAD_Y);

    /*
     * No subtitle here, deliberately.
     *
     * "winning better customers" said the same thing the two labelled bars
     * already show and the pill already states -- the conclusion three times
     * -- and at 31px it crowded the hero it sat above. The bars carry the
     * comparison; the pill carries the verdict.
     */
    /* No subtitle: the budget does not allow one, and "per subscriber" only
     * restates the ARPU label. See layout_c6.h. */
    const int px = hero_size_for_width("$33.53", col);
    label_at_baseline(scr, "$33.53", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + MIX_HERO_BASELINE_DY);

    /*
     * Two rows, each a bar with its own caption beneath it. Widths are
     * proportional to the dollar amounts and scaled against the larger, so
     * the green bar being visibly longer is the answer to "are the customers
     * I win worth more than the ones I lose".
     */
    const int joining = 3540, leaving = 2500;
    const int peak = joining > leaving ? joining : leaving;
    const int bar_h = CARD_BAR_H;
    const int row1 = CARD_Y + MIX_ROW1_DY;
    const int row2 = CARD_Y + MIX_ROW2_DY;

    lv_obj_t *g = lv_obj_create(scr);
    lv_obj_remove_style_all(g);
    lv_obj_set_pos(g, inner_x, row1);
    lv_obj_set_size(g, col * joining / peak, bar_h);
    lv_obj_set_style_bg_color(g, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(g, LV_RADIUS_CIRCLE, 0);

    /* Label in the bar's own colour: the pairing then needs no legend. */
    label_at(scr, "joining  $35.40", font_for_size(SIZE_FOOTER),
             COLOR_GREEN, inner_x, row1 + bar_h + MIX_LABEL_GAP);

    lv_obj_t *l = lv_obj_create(scr);
    lv_obj_remove_style_all(l);
    lv_obj_set_pos(l, inner_x, row2);
    lv_obj_set_size(l, col * leaving / peak, bar_h);
    lv_obj_set_style_bg_color(l, lv_color_hex(COLOR_AMBER), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, LV_RADIUS_CIRCLE, 0);

    label_at(scr, "leaving  $25.00", font_for_size(SIZE_FOOTER),
             COLOR_AMBER, inner_x, row2 + bar_h + MIX_LABEL_GAP);

    dots(scr, 5, 7);
    harness_render();
    harness_dump_ppm("mock_arpu_paired.ppm");
}

/* The honesty gate: too few on either side to compare. */
static void render_arpu_gated(void)
{
    lv_obj_t *scr = harness_screen();
    prepare(scr);

    const int card_w = PANEL_PX - 2 * PAD_PX;
    const int inner_x = PAD_PX + CARD_PAD;
    const int col = card_w - 2 * CARD_PAD;

    lv_obj_t *card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, PAD_PX, CARD_Y);
    lv_obj_set_size(card, card_w, CARD_H);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, CARD_RADIUS, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    label_at(scr, "ARPU", font_for_size(SIZE_LABEL), COLOR_MUTED,
             PAD_PX, LABEL_BASELINE_Y);

    label_at(scr, "per subscriber", font_for_size(SIZE_SUBTITLE),
             COLOR_MUTED, inner_x, CARD_Y + CARD_SUBTITLE_DY);

    const int px = hero_size_for_width("$33.53", col);
    label_at_baseline(scr, "$33.53", font_for_size(px), COLOR_PRIMARY,
                      inner_x, CARD_Y + CARD_HERO_BASELINE_DY);

    label_at(scr, "too few to compare (3 joined, 2 left)",
             font_for_size(SIZE_FOOTER), COLOR_DIM,
             inner_x, CARD_Y + CARD_CAPTION_DY);

    dots(scr, 5, 7);
    harness_render();
    harness_dump_ppm("mock_arpu_gated.ppm");
}

int main(void)
{
    harness_init();
    render_current();
    render_option_b();
    render_collecting();
    render_subs_current();
    render_subs();
    render_subs_flow();
    render_subs_flow_left();
    render_mrr_with_arr();
    render_arr_screen();
    render_arr_card();
    render_arpu_a();
    render_arpu_b();
    render_arpu_c();
    render_arpu_paired();
    render_arpu_gated();
    printf("wrote mock_current/option_b/collecting .ppm at %dx%d\n",
           HARNESS_W, HARNESS_H);
    return 0;
}
