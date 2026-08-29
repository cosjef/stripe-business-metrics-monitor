/*
 * Screen rendering.
 *
 * Every screen in the deck (spec 6) is drawn by a function here. These depend
 * only on LVGL and the layout constants -- no ESP-IDF, no SPI, no panel -- so
 * the same code that runs on the device also runs in the host test harness,
 * where it can be rendered to a buffer and asserted on pixel by pixel.
 *
 * Screens draw onto whatever LVGL screen object is passed in, and never touch
 * the display driver or the LVGL port. Hardware setup lives in display.c.
 */
#pragma once

#include "lvgl.h"

/*
 * Data for a rotation screen (spec 6.1). Strings are borrowed, not copied;
 * they must outlive the draw call.
 */
/* Most dots the deck can show -- one per defined screen. */
#define SCREENS_MAX_DOTS 10

typedef struct {
    const char *label;      /* "MRR", "NEW PAID", ... -- rendered at 20px */
    const char *hero;       /* the value; size computed from its width */
    const char *subtitle;   /* context line, 22px */
    _Bool hero_is_gain;     /* hero in green -- realized gains ONLY (spec 4.2) */
    _Bool hero_is_alert;    /* hero in red -- threshold breaches ONLY (spec 4.2) */
    _Bool subtitle_is_gain; /* subtitle in green, same rule */
    int dot_index;          /* which rotation dot is filled, 0-based */
    int dot_count;          /* how many dots to draw */
} screen_data_t;

/*
 * Draw a rotation screen onto `scr`.
 * Clears any existing children first, so screens can be swapped in place.
 */
void screen_draw_rotation(lv_obj_t *scr, const screen_data_t *data);

/*
 * Data for the card layout (the 480x480 deck).
 *
 * The delta fields are separate from the strings on purpose: `has_delta` is
 * the honesty gate. Until the device has accumulated enough daily samples to
 * know a direction, the bar and pill render in their "collecting" state
 * rather than showing a number nobody measured.
 */
typedef struct {
    const char *label;       /* "MRR" */
    const char *hero;        /* "$1,106.33" */
    const char *subtitle;    /* "33 active" */

    _Bool has_delta;         /* false: not enough history yet */
    const char *delta;       /* "+4.2%" -- ignored unless has_delta */
    const char *comparison;  /* "vs $1,061 last month", or the collecting note */
    _Bool delta_is_gain;     /* green vs red; realized movement only (spec 4.2) */
    int fill_pct;            /* bar fill 0-100 -- ignored unless has_delta */

    /*
     * Flow variant: two spans in one run, lost then gained, sharing the bar.
     *
     * When has_flow is set the bar shows the period's total movement split by
     * proportion rather than a single fill, and fill_pct is ignored. A count
     * of active subscriptions looks identical whether the month added three
     * or added ten and lost seven; this is what tells them apart.
     */
    _Bool has_flow;
    int flow_gained;
    int flow_lost;
    const char *flow_gained_label;   /* "10 joined" */
    const char *flow_lost_label;     /* "7 left" */

    int dot_index;
    int dot_count;
} card_data_t;

/*
 * Draw the card layout: hero plus context bar.
 *
 * Replaces the three-zone skeleton on this panel. That skeleton was shaped by
 * the S3's 240x240, where a hero at its legibility floor left room for little
 * else; at 480x480 it leaves ~200px of dead band between the label and the
 * number. The card spends that band on the one thing a bare figure cannot
 * say: which way it is going.
 *
 * The hero keeps its computed size. The floor is stated in millimetres at
 * 50cm (spec 2.2) and is not available to trade for layout.
 */
void screen_draw_card(lv_obj_t *scr, const card_data_t *data);

/*
 * State A: stale (spec 6.2). All values dim to muted, the age shows in amber,
 * and the retry status sits in the footer. The most important screen in the
 * deck -- a confidently displayed stale number is worse than an obviously
 * stale one.
 *
 * `label` and `hero` are the last-good values being shown as stale.
 * `age` is a rendered age string, e.g. "stale | 22 min".
 * `footer` is the retry status, e.g. "retrying".
 */
void screen_draw_stale(lv_obj_t *scr, const char *label, const char *hero,
                       const char *age, const char *footer);

/*
 * State B: no access (spec 6.2). Key revoked, wrong scope, or account access
 * removed. Plain language for the user, error code for support.
 */
void screen_draw_auth_error(lv_obj_t *scr, const char *line1, const char *line2,
                            const char *hint, const char *errcode);

/*
 * State C: setup (spec 6.2). The only screen with 100% customer exposure.
 * Shows the AP SSID and the next action, with firmware version in the footer.
 */
void screen_draw_setup(lv_obj_t *scr, const char *line1, const char *ssid,
                       const char *hint, const char *version);

/*
 * Low battery. Not in the original spec -- added once the device gained a
 * lithium cell.
 *
 * Follows the auth-error shape rather than the rotation shape: it is a takeover
 * screen with one thing to say, not a metric. The percentage is the hero
 * because it is the number the owner acts on; the voltage sits in the footer
 * for support, the same place the auth error puts its error code.
 *
 * `critical` switches the accent from amber to red: amber is the degraded
 * state (still running, plug it in soon), red is the threshold breach (minutes
 * left), matching how the deck already uses those two colors.
 */
void screen_draw_battery(lv_obj_t *scr, const char *pct, const char *line,
                         const char *voltage, _Bool critical);
