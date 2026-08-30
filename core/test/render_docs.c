/*
 * Render each deck screen to a PPM, for the README.
 *
 * Drives the real screen_draw_card and screen_draw_* functions at the real
 * 480x480 with the real fonts, so the images are what the panel shows rather
 * than an illustration of it. A hand-drawn mockup would drift from the code
 * the first time a layout changed; this cannot.
 *
 * Figures are this account's real ones, taken from the device's own logs, so
 * the README shows a device that exists rather than invented numbers.
 *
 *   make render_docs && ./render_docs && ../../docs/img/build.sh
 */
#include <stdio.h>
#include <string.h>

#include "harness.h"

#include "fonts.h"
#include "hero_size.h"
#include "layout.h"
#include "screens.h"

/* Battery shown on every card, as it is on the device. */
#define BATT_PCT 85
#define BATT_CHG true

/*
 * Force the card to rebuild before each screenshot.
 *
 * screen_draw_card keeps its objects and updates them, which is correct on the
 * device -- it shows one screen at a time, and rebuilding on every rotation is
 * what broke the panel during bring-up. But rendering ten screens in one
 * process leaves each screen's leftovers under the next: flow bars appeared
 * over mix captions, and a pill from an earlier card showed on one that has
 * none.
 *
 * Drawing a state screen in between is what the device itself does to
 * invalidate the card, and it goes through the public API -- calling
 * lv_obj_clean() directly frees objects the card still points at, and the
 * next draw follows them into freed memory. That segfaults, which at least
 * fails loudly.
 */
static void fresh(void)
{
    screen_draw_setup(harness_screen(), "", "", "", "");
}

static void shot(const char *name)
{
    char path[128];
    snprintf(path, sizeof(path), "shot_%s.ppm", name);
    harness_render();
    harness_dump_ppm(path);
}

static void card(const char *file, card_data_t *d)
{
    fresh();
    d->battery_pct = BATT_PCT;
    d->battery_charging = BATT_CHG;
    screen_draw_card(harness_screen(), d);
    shot(file);
}

int main(void)
{
    harness_init();

    /* --- MRR: the anchor metric, with its trend --- */
    card_data_t mrr = {0};
    mrr.label = "MRR";
    mrr.hero = "$1,106.33";
    mrr.subtitle = "33 active";
    mrr.has_delta = true;
    mrr.delta = "+4.2%";
    mrr.comparison = "vs $1,061 last month";
    mrr.delta_is_gain = true;
    mrr.fill_pct = 78;
    mrr.dot_index = 0;
    mrr.dot_count = 8;
    card("mrr", &mrr);

    /* --- NEW PAID: signups, and whether they are speeding up --- */
    card_data_t np = {0};
    np.label = "NEW PAID";
    np.hero = "10";
    np.delta = "$354.00";
    np.delta_is_gain = true;
    np.has_mix = true;
    np.mix_top = 10;
    np.mix_bottom = 6;
    np.mix_top_label = "this month  10";
    np.mix_bottom_label = "last month  6";
    np.dot_index = 1;
    np.dot_count = 8;
    card("new_paid", &np);

    /* --- PAID SUBS: the flow behind the count --- */
    card_data_t subs = {0};
    subs.label = "PAID SUBS";
    subs.hero = "33";
    subs.subtitle = "active";
    subs.has_flow = true;
    subs.delta = "net +3";
    subs.delta_is_gain = true;
    subs.flow_gained = 10;
    subs.flow_lost = 7;
    subs.flow_gained_label = "10 joined";
    subs.flow_lost_label = "7 left";
    subs.dot_index = 2;
    subs.dot_count = 8;
    card("paid_subs", &subs);

    /* --- CANCELLED: revenue that has given notice but not left --- */
    card_data_t risk = {0};
    risk.label = "CANCELLED";
    risk.hero = "$42.00";
    risk.subtitle = "2 leaving";
    risk.comparison = "next leaves in 15 days";
    risk.accent_amber = true;
    risk.has_delta = true;
    risk.delta = "3% MRR";
    risk.fill_pct = 4;
    risk.dot_index = 3;
    risk.dot_count = 8;
    card("cancelled", &risk);

    /* --- ARR: the annual figure, in annual units --- */
    card_data_t arr = {0};
    arr.label = "ARR";
    arr.hero = "$13,276";
    arr.subtitle = "at current MRR";
    arr.has_delta = true;
    arr.delta = "+4.2%";
    arr.comparison = "+$536 vs last month";
    arr.delta_is_gain = true;
    arr.fill_pct = 78;
    arr.dot_index = 4;
    arr.dot_count = 8;
    card("arr", &arr);

    /* --- ARPU: are the customers we win worth more than those we lose --- */
    card_data_t arpu = {0};
    arpu.label = "ARPU";
    arpu.hero = "$33.53";
    arpu.has_mix = true;
    arpu.delta = "+$10.40";
    arpu.delta_is_gain = true;
    arpu.mix_top = 3540;
    arpu.mix_bottom = 2500;
    arpu.mix_top_label = "joining  $35.40";
    arpu.mix_bottom_label = "leaving  $25.00";
    arpu.dot_index = 5;
    arpu.dot_count = 8;
    card("arpu", &arpu);

    /* --- NET 30D: what the month did to revenue --- */
    card_data_t net = {0};
    net.label = "NET 30D";
    net.hero = "+$179.00";
    net.subtitle = "net this month";
    net.comparison = "$354 new / $175 lost";
    net.has_delta = true;
    net.delta = "growth";
    net.delta_is_gain = true;
    net.fill_pct = 67;
    net.dot_index = 6;
    net.dot_count = 8;
    card("net_30d", &net);

    /* --- FAILED: the only screen that earns red --- */
    card_data_t failed = {0};
    failed.label = "FAILED";
    failed.hero = "$29.00";
    failed.subtitle = "1 payment";
    failed.comparison = "retrying in 5 days";
    failed.accent_red = true;
    failed.dot_index = 7;
    failed.dot_count = 8;
    card("failed", &failed);

    /* --- State screens, which use the pre-card layout deliberately --- */
    fresh();
    screen_draw_stale(harness_screen(), "MRR", "$1,106.33",
                      "stale | 22 min", "retrying");
    shot("stale");

    fresh();
    screen_draw_setup(harness_screen(), "Join wifi", "Setup-FE16",
                      "on your phone", "v0.4.0");
    shot("setup");

    printf("wrote 10 screenshots\n");
    return 0;
}
