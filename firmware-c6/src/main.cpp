/*
 * Stripe Revenue Display -- Arduino port, stage 3: provisioning.
 *
 * The device now provisions itself. On first boot it has no credentials, so
 * it runs setup mode: an open "Setup-XXXX" AP with a captive portal (spec
 * 9.1), showing State C on screen. Once it has WiFi and a validated Stripe
 * key it joins the network and rotates the deck.
 *
 * Two-phase setup, because the key is checked against the live API the moment
 * it is entered (spec 9.1 step 4) rather than stored untested:
 *
 *   1. WiFi form -> credentials saved -> device joins the network
 *   2. Key form (AP still up, alongside the station link) -> key fetched
 *      against Stripe -> stored only if it actually works
 *
 * A key that is merely well-formed is not accepted: the owner finds out it is
 * wrong while they are still holding their phone, not hours later via a screen
 * they have stopped looking at.
 *
 * Validation logic and its customer-facing strings are shared from
 * firmware/main/ (provision.c, stripe_key.c), so both builds say the same
 * thing and the host suite keeps covering them.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <lvgl.h>

#include "battery_hw.h"
#include "board.h"
#include "display.h"
#include "portal.h"
#include "settings.h"
#include "stripe_fetch.h"

extern "C" {
#include "cache.h"
#include "format.h"
#include "freshness.h"
#include "invoices.h"
#include "history.h"
#include "mrr.h"
#include "provision.h"
#include "rotation.h"
#include "state.h"
#include "screens.h"
#include "stripe_key.h"
}
#include "layout.h"

#define FIRMWARE_VERSION "v0.4.0"

/*
 * Timezone, as a POSIX TZ string.
 *
 * The daily history buckets by LOCAL day, because "today's MRR" has to mean
 * the day the owner is living in, not UTC. configTzTime hands the rule to
 * newlib so DST is handled by the C library rather than by arithmetic here --
 * the ESP-IDF build derived the offset by hand and needed a correction for
 * the two calendars landing on different days, which is exactly the kind of
 * code that is wrong twice a year.
 */
/*
 * Force the stale screen after this many seconds, to see State A on the glass
 * without waiting out the real fifteen-minute threshold or unplugging the
 * router. 0 disables. Must be 0 in anything shipped.
 */
#define FORCE_STALE_AFTER_S 0

#define DEVICE_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

/* Clocks before this are unset, not merely wrong: 2023-01-01. */
#define CLOCK_SANE_EPOCH 1672531200

#define ROTATE_MS  5000
#define REFRESH_MS (5 * 60 * 1000)   /* spec 7.1: poll every five minutes */
#define JOIN_TIMEOUT_MS 30000

/* Not static: battery_hw.cpp reads the cell through this same handle rather
 * than opening a second one. */
XPowersPMU s_pmu;

/* What the device is doing, which decides what the screen shows. */
typedef enum {
    MODE_SETUP_WIFI = 0,   /* no credentials: AP up, WiFi form */
    MODE_SETUP_KEY,        /* on the network, no key: AP up, key form */
    MODE_RUNNING,          /* provisioned: rotating the deck */
} app_mode_t;

static app_mode_t s_mode = MODE_SETUP_WIFI;

#define FIELD_LEN 40

/*
 * screen_id_t and the visibility rules come from rotation.h, not from a local
 * enum.
 *
 * The rules are the interesting part: an account with no trials should not
 * spend a rotation slot on a permanent zero (spec 6.1). That logic already
 * exists and is covered by test_rotation, so it is shared rather than
 * reimplemented -- a second copy of "which screens are worth showing" is
 * exactly the kind of thing that drifts silently between builds.
 *
 * Only the screens this port can currently fill are populated; the rest are
 * held back by the rotation state below until their inputs exist.
 */
static struct {
    char hero[FIELD_LEN];
    char subtitle[FIELD_LEN];
} s_values[SCREEN_COUNT];

static const char *s_labels[SCREEN_COUNT] = {
    [SCREEN_MRR]           = "MRR",
    [SCREEN_NEW_PAID]      = "NEW PAID",
    [SCREEN_PAID_SUBS]     = "PAID SUBS",
    [SCREEN_TRIALS]        = "TRIALS",
    [SCREEN_CONVERSION]    = "CONVERSION",
    [SCREEN_CANCELLATIONS] = "CANCELLED",
    [SCREEN_ARR]           = "ANNUAL RUN RATE",
    [SCREEN_ARPU]          = "ARPU",
    [SCREEN_NET_CHANGE]    = "NET 30D",
    [SCREEN_FAILED]        = "FAILED",
};

/* Which screens are worth showing, rebuilt after every fetch. */
static rotation_state_t s_rot_state;
static screen_id_t s_visible[SCREEN_COUNT];
static int s_visible_count = 1;

static char s_setup_ssid[SETUP_SSID_LEN];
static char s_key[STRIPE_KEY_MAX_LEN + 1];
static bool s_have_data;
static uint32_t s_last_refresh;

/*
 * The MRR series behind the card's delta.
 *
 * Loaded from NVS at boot so a reboot does not restart the seven-day wait
 * before a trend can honestly be drawn, and saved only when the day rolls
 * over -- NVS is flash, and a five-minute poll would be thousands of writes a
 * month for a value that changes once a day.
 */
/*
 * Whether the numbers on screen can still be trusted.
 *
 * Spec 6.2 calls the stale screen the most important in the deck: a
 * confidently displayed stale number is worse than an obviously stale one,
 * and that is precisely how cheap dashboards fail -- freezing on a four-hour
 * figure with no indication. Until this was wired the device did exactly
 * that: a failed fetch left the deck rotating last-good values with nothing
 * to say they were old.
 */
static freshness_t s_freshness;
static bool s_auth_failed;

/*
 * Whether the values on screen came from flash rather than the network.
 *
 * Kept because restored values must never be presented as live. They are
 * shown with their real age, so the freshness machinery treats them as data
 * of that age and the stale screen takes over if they are too old -- a
 * cached figure displayed confidently is exactly the failure the stale
 * screen exists to prevent.
 */
static bool s_from_cache;

/* Latest battery reading, refreshed on the rotation tick. */
static battery_reading_t s_battery;

static history_t s_history;
static int32_t s_history_day = -1;   /* last epoch day recorded */
static bool s_clock_ok;

/* Defined below with the rest of the time handling; used by apply_totals. */
static void record_history(int64_t mrr_cents);
static void update_delta(int64_t mrr_cents);

/* ---- hardware ---- */

static bool power_up(void)
{
    Wire.begin(IIC_SDA, IIC_SCL);

    if (!s_pmu.begin(Wire, AXP2101_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("FAIL: AXP2101 not responding at 0x34");
        return false;
    }

    s_pmu.setALDO1Voltage(3300);
    s_pmu.setALDO2Voltage(3300);
    s_pmu.setALDO3Voltage(3300);
    s_pmu.setALDO4Voltage(3300);

    s_pmu.enableALDO1();
    s_pmu.enableALDO2();
    s_pmu.enableALDO4();

    /* ALDO3 is the panel reset -- see display.cpp. */
    s_pmu.enableALDO3();
    delay(100);
    s_pmu.disableALDO3();
    delay(100);
    s_pmu.enableALDO3();
    delay(100);

    s_pmu.enableBattDetection();
    s_pmu.enableBattVoltageMeasure();
    return true;
}

/* ---- screens ---- */

/* Dashes, not zeroes: an invented number is worse than an obvious blank. */
static void clear_values(void)
{
    for (int i = 0; i < SCREEN_COUNT; i++) {
        snprintf(s_values[i].hero, FIELD_LEN, "--");
        s_values[i].subtitle[0] = '\0';
    }
}

static void draw_setup_screen(const char *line1)
{
    screen_draw_setup(lv_screen_active(), line1, s_setup_ssid,
                      "on your phone", FIRMWARE_VERSION);
}

/*
 * Delta strings for the card, rebuilt after each fetch.
 *
 * Held in static buffers because card_data_t borrows its strings; they must
 * outlive the draw call.
 */
static char s_delta[16];
static char s_comparison[FIELD_LEN];
/* Flow labels for the PAID SUBS card; borrowed by card_data_t, so static. */
/* CANCELLED screen: at-risk figures, or plain churn when nobody is leaving. */
static char s_risk_hero[24];
static char s_risk_sub[FIELD_LEN];
static char s_risk_caption[FIELD_LEN];
static char s_risk_pill[16];

/* ARR card: the annual figure's own delta strings. */
static char s_arr_delta[16];
static char s_arr_caption[FIELD_LEN];
static bool s_arr_has_delta;
static bool s_arr_is_gain;
static int s_arr_fill_pct;

/* NEW PAID and NET 30D: revenue flow, not just head count. */
static char s_new_hero[24], s_new_sub[FIELD_LEN], s_new_caption[FIELD_LEN];
static char s_new_pill[16], s_new_top[32], s_new_bottom[32];
static bool s_new_has_pace, s_new_is_gain;
static int s_new_this, s_new_prev;

/* FAILED: money that did not arrive and can still be chased. */
static char s_failed_hero[24], s_failed_sub[FIELD_LEN];
static char s_failed_caption[FIELD_LEN];
static int s_failed_count;
static char s_net_hero[24], s_net_sub[FIELD_LEN], s_net_caption[FIELD_LEN];
static char s_net_pill[16];
static bool s_net_is_gain;
static int s_net_fill_pct;

/* ARPU mix: are the customers we win worth more than the ones we lose? */
static char s_arpu_pill[16], s_arpu_top[32], s_arpu_bottom[32];
static char s_arpu_caption[FIELD_LEN];
static bool s_arpu_has_mix, s_arpu_is_gain;
static int s_arpu_top_v, s_arpu_bottom_v;
static bool s_risk_actionable;   /* true when someone has given notice */
static int s_risk_fill_pct;

static char s_flow_net[16];
static char s_flow_gained[16];
static char s_flow_lost[16];
static int s_flow_g, s_flow_l;
static bool s_has_delta;
static bool s_delta_is_gain;
static int s_fill_pct;

/*
 * Turn the series into the card's trend, or decline to.
 *
 * history_has_trend() is the gate: under seven samples there is no honest
 * direction to report, and the card renders its collecting state instead. Two
 * points make a straight line, which would assert a trend nobody measured.
 */
static void update_delta(int64_t mrr_cents)
{
    if (!history_has_trend(&s_history)) {
        s_has_delta = false;
        const int n = history_count(&s_history);
        snprintf(s_comparison, sizeof(s_comparison),
                 "collecting history (%d/%d)", n, HISTORY_MIN_FOR_TREND);
        return;
    }

    const int64_t oldest = history_oldest(&s_history);
    if (oldest <= 0) {
        /* Percentage against zero is undefined, not infinite. */
        s_has_delta = false;
        snprintf(s_comparison, sizeof(s_comparison), "no baseline");
        return;
    }

    const int64_t change = history_change(&s_history);

    /* Integer maths in tenths of a percent, so the money path stays free of
     * floating point like the rest of the device. */
    const int64_t tenths = (change * 1000) / oldest;
    snprintf(s_delta, sizeof(s_delta), "%s%lld.%d%%",
             tenths >= 0 ? "+" : "-",
             (long long)(tenths < 0 ? -tenths : tenths) / 10,
             (int)((tenths < 0 ? -tenths : tenths) % 10));

    char baseline[24];
    format_money_compact(oldest, baseline, sizeof(baseline));
    snprintf(s_comparison, sizeof(s_comparison), "vs %s %d days ago",
             baseline, history_day_span(&s_history));

    s_delta_is_gain = change >= 0;

    /*
     * Bar fill: where the current value sits between the window's low and
     * high. A bar showing "percent of the maximum" would sit near full
     * forever and say nothing; against the range it actually moves.
     */
    const int64_t lo = history_min(&s_history);
    const int64_t hi = history_max(&s_history);
    if (hi > lo) {
        s_fill_pct = (int)(((mrr_cents - lo) * 100) / (hi - lo));
    } else {
        s_fill_pct = 100;   /* flat series: full rather than empty */
    }

    s_has_delta = true;
}

/*
 * Draw the stale screen: the same last-good numbers, presented as old.
 *
 * It deliberately keeps the pre-card panel layout. The stale screen must not
 * look like a live one -- the visual difference IS the signal, and dressing
 * old data in the same card the live deck uses would undo the point.
 */
static void draw_stale(void)
{
    char age[24];
    freshness_format_age(freshness_age_ms(&s_freshness, (int64_t)millis()),
                         age, sizeof(age));

    char age_line[FIELD_LEN];
    snprintf(age_line, sizeof(age_line), "stale | %s", age);

    /* The footer says what the device is doing about it, so the reader knows
     * whether to act or wait. */
    char footer[FIELD_LEN];
    snprintf(footer, sizeof(footer), "retrying");

    /* MRR is the anchor metric, so that is the value shown as stale. */
    screen_draw_stale(lv_screen_active(), s_labels[SCREEN_MRR],
                      s_values[SCREEN_MRR].hero, age_line, footer);
}

/*
 * Fill in the battery fields every card shares.
 *
 * Reported only when the reading is plausible: UNKNOWN means the sensor is
 * wrong rather than the cell, and a glyph drawn from a bad reading would be a
 * confident claim about something unmeasured.
 */
static void set_battery(card_data_t *c)
{
    if (s_battery.level == BATTERY_UNKNOWN) {
        c->battery_pct = -1;
        return;
    }
    c->battery_pct = s_pmu.getBatteryPercent();
    c->battery_charging = s_battery.charging;
}

/* `slot` indexes the visible list, not screen_id_t. */
static void show(int slot)
{
    /*
     * What to draw is a decision over device conditions, not a default. The
     * precedence lives in state.c and is covered by 42 host checks; encoding
     * it here with a chain of ifs is exactly how the wrong screen gets shown
     * during the one failure the reader needed to understand.
     */
    device_status_t st = {};
    st.provisioned = true;         /* setup mode never reaches show() */
    st.auth_failed = s_auth_failed;
    /*
     * Low enough to act on. CHARGING and UNKNOWN are deliberately not
     * warnings: a device on USB is fine, and an implausible reading means the
     * sensor is wrong rather than the cell -- warning then would send the
     * owner to charge a device that is not low.
     */
    st.battery_warn = (s_battery.level == BATTERY_LOW ||
                       s_battery.level == BATTERY_CRITICAL);
    st.stale = s_have_data &&
               freshness_is_stale(&s_freshness, (int64_t)millis());
#if FORCE_STALE_AFTER_S
    if (s_have_data && millis() > (uint32_t)FORCE_STALE_AFTER_S * 1000) {
        st.stale = true;
    }
#endif

    switch (display_state(&st)) {
    case DISPLAY_AUTH_ERROR:
        screen_draw_auth_error(lv_screen_active(), "Stripe key", "rejected",
                               "check it in the portal", "401");
        return;
    case DISPLAY_STALE:
        draw_stale();
        return;
    case DISPLAY_BATTERY: {
        const bool critical = (s_battery.level == BATTERY_CRITICAL);
        char pct[16], volts[24];
        snprintf(pct, sizeof(pct), "%d%%", s_pmu.getBatteryPercent());
        snprintf(volts, sizeof(volts), "%d mV", s_battery.cell_mv);
        screen_draw_battery(lv_screen_active(), pct,
                            critical ? "plug in now" : "charge soon",
                            volts, critical);
        return;
    }
    case DISPLAY_SETUP:
    case DISPLAY_ROTATION:
        break;
    }

    if (s_visible_count <= 0) {
        return;
    }
    if (slot < 0 || slot >= s_visible_count) {
        slot = 0;
    }
    const screen_id_t id = s_visible[slot];

    /*
     * MRR gets the card, because it is the only screen with a series behind
     * it. The others keep the rotation layout until they have their own
     * history to show -- a card with a permanently empty bar would be worse
     * than no card.
     */
    if (id == SCREEN_FAILED) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_failed_hero;
        c.subtitle = s_failed_sub;
        c.comparison = s_failed_caption;
        /*
         * The one screen that earns red (spec 4.2): money actively being
         * lost, and recoverable if acted on. Everything else on the deck
         * reports; this one asks.
         */
        c.accent_red = true;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_ARPU) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_values[id].hero;
        c.subtitle = s_values[id].subtitle;
        c.has_mix = s_arpu_has_mix;
        if (s_arpu_has_mix) {
            c.delta = s_arpu_pill;
            c.delta_is_gain = s_arpu_is_gain;
            c.mix_top = s_arpu_top_v;
            c.mix_bottom = s_arpu_bottom_v;
            c.mix_top_label = s_arpu_top;
            c.mix_bottom_label = s_arpu_bottom;
        } else {
            c.comparison = s_arpu_caption;
        }
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_NET_CHANGE) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_net_hero;
        c.subtitle = s_net_sub;
        c.comparison = s_net_caption;
        c.has_delta = true;
        c.delta = s_net_pill;
        c.delta_is_gain = s_net_is_gain;
        c.fill_pct = s_net_fill_pct;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_NEW_PAID) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_new_hero;
        c.subtitle = s_new_sub;
        c.has_mix = s_new_has_pace;
        if (s_new_has_pace) {
            c.delta = s_new_pill;
            c.delta_is_gain = s_new_is_gain;
            c.mix_top = s_new_this;
            c.mix_bottom = s_new_prev;
            c.mix_top_label = s_new_top;
            c.mix_bottom_label = s_new_bottom;
        } else {
            c.comparison = s_new_caption;
        }
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_ARR) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_values[id].hero;
        c.subtitle = s_values[id].subtitle;
        c.has_delta = s_arr_has_delta;
        c.delta = s_arr_delta;
        c.comparison = s_arr_caption;
        c.delta_is_gain = s_arr_is_gain;
        c.fill_pct = s_arr_fill_pct;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_CANCELLATIONS) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_risk_hero;
        c.subtitle = s_risk_sub;
        c.comparison = s_risk_caption;
        /* Amber only when something is actually at risk; a plain churn count
         * is ordinary business and stays in the neutral palette. */
        c.accent_amber = s_risk_actionable;
        c.has_delta = s_risk_actionable;
        c.delta = s_risk_pill;
        c.fill_pct = s_risk_fill_pct;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_PAID_SUBS && (s_flow_g > 0 || s_flow_l > 0)) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_values[id].hero;
        c.subtitle = s_values[id].subtitle;
        c.has_flow = true;
        c.delta = s_flow_net;
        c.delta_is_gain = (s_flow_g >= s_flow_l);
        c.flow_gained = s_flow_g;
        c.flow_lost = s_flow_l;
        c.flow_gained_label = s_flow_gained;
        c.flow_lost_label = s_flow_lost;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    if (id == SCREEN_MRR) {
        card_data_t c = {};
        c.label = s_labels[id];
        c.hero = s_values[id].hero;
        c.subtitle = s_values[id].subtitle;
        c.has_delta = s_has_delta;
        c.delta = s_delta;
        c.comparison = s_comparison;
        c.delta_is_gain = s_delta_is_gain;
        c.fill_pct = s_fill_pct;
        set_battery(&c);
        c.dot_index = slot;
        c.dot_count = s_visible_count;
        screen_draw_card(lv_screen_active(), &c);
        return;
    }

    screen_data_t d = {};
    d.label = s_labels[id];
    d.hero = s_values[id].hero;
    d.subtitle = s_values[id].subtitle;
    d.dot_index = slot;
    d.dot_count = s_visible_count;

    screen_draw_rotation(lv_screen_active(), &d);
}

/*
 * The rotation index, owned here rather than inside loop().
 *
 * A fetch can shrink the visible list -- the last trial converts, and the
 * TRIALS screen disappears -- which would leave a stale index pointing past
 * the end until the next wrap. Keeping it beside the list means the two are
 * corrected together.
 */
static int s_slot;

static void clamp_slot(void)
{
    if (s_visible_count <= 0) {
        s_slot = 0;
    } else if (s_slot >= s_visible_count) {
        s_slot = 0;
    }
}

static void rebuild_rotation(void)
{
    const int n = rotation_build(&s_rot_state, s_visible);
    if (n <= 0) {
        /* rotation_build guarantees MRR, but never leave the device with an
         * empty deck -- a blank rotation reads as broken. */
        s_visible[0] = SCREEN_MRR;
        s_visible_count = 1;
        clamp_slot();
        return;
    }
    s_visible_count = n;
    clamp_slot();
}


static void apply_totals(const mrr_totals_t *t)
{
    format_money_compact(t->mrr_cents, s_values[SCREEN_MRR].hero, FIELD_LEN);
    snprintf(s_values[SCREEN_MRR].subtitle, FIELD_LEN, "%d active",
             t->active_count);

    format_count(t->active_count, s_values[SCREEN_PAID_SUBS].hero, FIELD_LEN);
    snprintf(s_values[SCREEN_PAID_SUBS].subtitle, FIELD_LEN, "active");

    format_count(t->trial_count, s_values[SCREEN_TRIALS].hero, FIELD_LEN);
    snprintf(s_values[SCREEN_TRIALS].subtitle, FIELD_LEN, "trialing");

    format_money_compact(t->mrr_cents * 12, s_values[SCREEN_ARR].hero,
                         FIELD_LEN);
    snprintf(s_values[SCREEN_ARR].subtitle, FIELD_LEN, "at current MRR");

    /* Guard the zero case: an account with no active subs shows "--", not a
     * division by zero. */
    if (t->active_count > 0) {
        format_money_compact(t->mrr_cents / t->active_count,
                             s_values[SCREEN_ARPU].hero, FIELD_LEN);
        snprintf(s_values[SCREEN_ARPU].subtitle, FIELD_LEN, "per subscriber");
    } else {
        snprintf(s_values[SCREEN_ARPU].hero, FIELD_LEN, "--");
        s_values[SCREEN_ARPU].subtitle[0] = '\0';
    }

    s_have_data = true;

    /*
     * Feed the visibility rules. trial_count is what keeps a TRIALS screen
     * off the deck for an account that has none -- which is this account:
     * 33 active, 0 trialing, so the screen was correctly reading zero and
     * should simply not be shown.
     *
     * The screens this port cannot yet fill (conversion, cancellations, net
     * change, failed payments) stay hidden because their inputs are still
     * false/zero here, not because of a separate list.
     */
    s_rot_state.have_data = true;
    s_rot_state.trial_count = t->trial_count;
    rebuild_rotation();

    record_history(t->mrr_cents);
    update_delta(t->mrr_cents);

    /*
     * ARR's delta.
     *
     * The percentage is identical to MRR's -- ARR is mrr_cents * 12 and the
     * twelve cancels -- so the pill deliberately carries the annual MONEY
     * amount instead. "+$536/yr" is the one thing this screen can say that
     * the MRR card does not already say in the same units.
     */
    s_arr_has_delta = s_has_delta;
    s_arr_is_gain = s_delta_is_gain;
    s_arr_fill_pct = s_fill_pct;
    if (s_arr_has_delta) {
        const int64_t change_yr = history_change(&s_history) * 12;
        char amt[24];
        format_money_delta(change_yr, amt, sizeof(amt));
        snprintf(s_arr_delta, sizeof(s_arr_delta), "%s", s_delta);
        snprintf(s_arr_caption, sizeof(s_arr_caption), "%s vs last month", amt);
    } else {
        snprintf(s_arr_caption, sizeof(s_arr_caption), "%s", s_comparison);
    }

    /*
     * NEW PAID: the month's signups, valued.
     *
     * The count is the hero because that is the figure people track, but the
     * revenue it carries is the subtitle -- ten signups at $13 and ten at $49
     * are the same number and very different months.
     */
    format_count(t->new_count, s_new_hero, sizeof(s_new_hero));
    {
        char amt[24];
        format_money_compact(t->new_cents, amt, sizeof(amt));
        snprintf(s_new_sub, sizeof(s_new_sub), "%s/mo added", amt);

        /*
         * Pace against the previous period.
         *
         * A count alone does not say whether acquisition is speeding up. Ten
         * this month against six last is the fact worth having, and it reuses
         * the two-bar comparison the ARPU card already established.
         *
         * The raw counts stay in the labels so the percentage never stands
         * alone: at these volumes "+67%" is four customers, and a reader who
         * cannot see the 10 and the 6 has no way to judge that.
         */
        s_new_has_pace = (t->new_count > 0 || t->prior_new_count > 0);
        s_new_this = t->new_count;
        s_new_prev = t->prior_new_count;

        if (s_new_has_pace) {
            snprintf(s_new_top, sizeof(s_new_top), "this month  %d",
                     t->new_count);
            snprintf(s_new_bottom, sizeof(s_new_bottom), "last month  %d",
                     t->prior_new_count);

            /*
             * The pill carries the MONEY, not the percentage.
             *
             * The two bars already show 10 against 6, so a "+67%" pill would
             * state what the bars state. The revenue those signups carried is
             * the fact that would otherwise be lost: the mix variant hides
             * the subtitle, which is where "$354/mo added" used to live, and
             * ten signups at $13 is a very different month from ten at $49.
             */
            format_money_compact(t->new_cents, s_new_pill,
                                 sizeof(s_new_pill));
            s_new_is_gain = (t->new_count >= t->prior_new_count);
        }
        snprintf(s_new_caption, sizeof(s_new_caption), "last 30 days");
    }

    /*
     * NET 30D: what the month actually did to revenue.
     *
     * This is the one screen that answers "did we grow?" in money rather than
     * heads. PAID SUBS shows +10/-7; this shows that those ten were worth
     * more than the seven, which is the fact that decides whether the month
     * was good.
     */
    {
        const int64_t net = t->new_cents - t->churned_cents;
        s_net_is_gain = (net >= 0);
        format_money_delta(net, s_net_hero, sizeof(s_net_hero));
        snprintf(s_net_sub, sizeof(s_net_sub), "net this month");

        char gained[24], lost[24];
        format_money_compact(t->new_cents, gained, sizeof(gained));
        format_money_compact(t->churned_cents, lost, sizeof(lost));
        /* Label the two figures rather than relying on the reader to know
         * that green is gain and the leading sign carries the meaning. */
        snprintf(s_net_caption, sizeof(s_net_caption), "%s new / %s lost",
                 gained, lost);

        /* The bar shows what share of the month's gross movement was gain.
         * Above half is growth; the pill names the direction. */
        const int64_t gross = t->new_cents + t->churned_cents;
        s_net_fill_pct = gross > 0 ? (int)((t->new_cents * 100) / gross) : 0;
        snprintf(s_net_pill, sizeof(s_net_pill), "%s",
                 s_net_is_gain ? "growth" : "shrink");
    }

    /*
     * ARPU mix.
     *
     * The average alone is inert. What moves -- and what no other screen can
     * say -- is whether the customers being won are worth more than the ones
     * being lost. Gated: below MRR_MIX_MIN on either side the comparison is
     * two averages over a handful of customers, where one unusual signup
     * decides the verdict.
     */
    s_arpu_has_mix = mrr_mix_comparable(t->new_count, t->churned_count);

    if (s_arpu_has_mix) {
        const int64_t joining = mrr_arpu_cents(t->new_cents, t->new_count);
        const int64_t leaving = mrr_arpu_cents(t->churned_cents,
                                               t->churned_count);
        s_arpu_top_v = (int)joining;
        s_arpu_bottom_v = (int)leaving;
        s_arpu_is_gain = (joining >= leaving);

        char a[16], b[16], gap[16];
        format_money_compact(joining, a, sizeof(a));
        format_money_compact(leaving, b, sizeof(b));
        format_money_delta(joining - leaving, gap, sizeof(gap));
        snprintf(s_arpu_top, sizeof(s_arpu_top), "joining  %s", a);
        snprintf(s_arpu_bottom, sizeof(s_arpu_bottom), "leaving  %s", b);
        snprintf(s_arpu_pill, sizeof(s_arpu_pill), "%s", gap);
    } else {
        /* Say why, rather than silently dropping to a bare average. */
        snprintf(s_arpu_caption, sizeof(s_arpu_caption),
                 "too few to compare (%d joined, %d left)",
                 t->new_count, t->churned_count);
    }

    /* Subscriber flow, for the PAID SUBS card. */
    s_flow_g = t->new_count;
    s_flow_l = t->churned_count;
    const int net = s_flow_g - s_flow_l;
    snprintf(s_flow_net, sizeof(s_flow_net), "net %s%d",
             net >= 0 ? "+" : "", net);
    snprintf(s_flow_gained, sizeof(s_flow_gained), "%d joined", s_flow_g);
    snprintf(s_flow_lost, sizeof(s_flow_lost), "%d left", s_flow_l);

    /*
     * CANCELLED: lead with what can still be changed.
     *
     * Seven subscriptions already left -- that is history, and nothing can be
     * done about it. Two have given notice and are still paying, which is the
     * only figure on this device that acting on it could still change. So the
     * at-risk money is the hero when it exists, and the churn count falls
     * back to the hero when it does not.
     */
    s_risk_actionable = (t->at_risk_count > 0);

    if (s_risk_actionable) {
        format_money_compact(t->at_risk_cents, s_risk_hero, sizeof(s_risk_hero));
        snprintf(s_risk_sub, sizeof(s_risk_sub), "%d leaving",
                 t->at_risk_count);

        /*
         * Days until the soonest departure. That deadline is what makes this
         * screen actionable rather than informational -- "$42 at risk" is a
         * fact, "$42 leaving in 15 days" is a prompt.
         */
        const time_t now = time(NULL);
        if (t->at_risk_soonest > 0 && now > CLOCK_SANE_EPOCH) {
            const long days = (long)((t->at_risk_soonest - (int64_t)now) / 86400);
            if (days > 0) {
                snprintf(s_risk_caption, sizeof(s_risk_caption),
                         "next leaves in %ld days", days);
            } else {
                snprintf(s_risk_caption, sizeof(s_risk_caption),
                         "next leaves today");
            }
        } else {
            snprintf(s_risk_caption, sizeof(s_risk_caption),
                     "%d left in 30 days", t->churned_count);
        }

        /* Share of MRR at risk, for the bar. */
        if (t->mrr_cents > 0) {
            s_risk_fill_pct = (int)((t->at_risk_cents * 100) / t->mrr_cents);
            if (s_risk_fill_pct < 2) {
                s_risk_fill_pct = 2;   /* keep a small share visible */
            }
        } else {
            s_risk_fill_pct = 0;
        }
        snprintf(s_risk_pill, sizeof(s_risk_pill), "%d%% MRR",
                 t->mrr_cents > 0
                     ? (int)((t->at_risk_cents * 100) / t->mrr_cents) : 0);
    } else {
        /* Nobody leaving: fall back to plain churn rather than showing $0. */
        format_count(t->churned_count, s_risk_hero, sizeof(s_risk_hero));
        snprintf(s_risk_sub, sizeof(s_risk_sub), "cancelled");
        snprintf(s_risk_caption, sizeof(s_risk_caption), "last 30 days");
        s_risk_pill[0] = '\0';
        s_risk_fill_pct = 0;
    }

    s_rot_state.churned_30d = t->churned_count;

    /*
     * Failed payments, from a second call.
     *
     * Its own fetch because invoices are a different endpoint. A key without
     * invoice access, or an account with nothing failing, both leave the
     * screen hidden -- have_invoices stays false and the rotation rule does
     * the rest.
     */
    {
        stripe_failed_t f;
        if (stripe_fetch_failed(&f) == STRIPE_FETCH_OK) {
            s_rot_state.have_invoices = true;
            s_rot_state.failed_count = f.count;
            s_failed_count = f.count;
            Serial.printf("failed: %d payment(s), %lld cents, retry %lld\n",
                          f.count, (long long)f.cents,
                          (long long)f.next_retry);

            if (f.count > 0) {
                format_money_compact(f.cents, s_failed_hero,
                                     sizeof(s_failed_hero));
                snprintf(s_failed_sub, sizeof(s_failed_sub),
                         "%d payment%s", f.count, f.count == 1 ? "" : "s");

                /*
                 * The retry date is what makes this actionable. Stripe having
                 * stopped retrying is the more urgent case, not the calmer
                 * one: nothing further will happen without intervention.
                 */
                const time_t now = time(NULL);
                if (f.next_retry > 0 && now > CLOCK_SANE_EPOCH) {
                    const long days =
                        (long)((f.next_retry - (int64_t)now) / 86400);
                    if (days > 1) {
                        snprintf(s_failed_caption, sizeof(s_failed_caption),
                                 "retrying in %ld days", days);
                    } else {
                        snprintf(s_failed_caption, sizeof(s_failed_caption),
                                 "retrying today");
                    }
                } else {
                    snprintf(s_failed_caption, sizeof(s_failed_caption),
                             "no retry scheduled");
                }
            }
        } else {
            /* No invoice access, or the call failed: say nothing rather than
             * implying the account is clean. */
            s_rot_state.have_invoices = false;
            s_failed_count = 0;
        }
        rebuild_rotation();
    }
}

/* ---- time and history ---- */

/*
 * Local epoch day, or -1 if the clock is not set.
 *
 * Returning -1 rather than a guess is the point: without NTP the clock starts
 * at 1970 and every sample would land on the same fictional day, quietly
 * corrupting the series with values recorded against a date that never
 * happened.
 */
static int32_t local_epoch_day(void)
{
    const time_t now = time(NULL);
    if (now < CLOCK_SANE_EPOCH) {
        return -1;
    }

    struct tm local;
    localtime_r(&now, &local);

    /* mktime on a midnight-normalised copy gives the local day's start in
     * epoch seconds, which divided by a day is the local day number. */
    struct tm midnight = local;
    midnight.tm_hour = 0;
    midnight.tm_min = 0;
    midnight.tm_sec = 0;
    midnight.tm_isdst = -1;

    const time_t day_start = mktime(&midnight);
    if (day_start <= 0) {
        return -1;
    }
    return (int32_t)(day_start / 86400);
}

static void sync_clock(void)
{
    configTzTime(DEVICE_TZ, NTP_SERVER_1, NTP_SERVER_2);

    /* Wait briefly. A device that cannot reach NTP still runs -- it just
     * cannot bucket history yet, and says so rather than inventing days. */
    for (int i = 0; i < 100; i++) {
        if (time(NULL) >= CLOCK_SANE_EPOCH) {
            s_clock_ok = true;
            break;
        }
        delay(100);
        lv_timer_handler();
    }

    if (s_clock_ok) {
        char buf[32];
        const time_t now = time(NULL);
        struct tm local;
        localtime_r(&now, &local);
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
        Serial.printf("clock: %s local\n", buf);
    } else {
        Serial.println("clock: NTP did not sync; history paused");
    }
}

/*
 * Fold today's MRR into the series.
 *
 * history_record already updates rather than appends within a day, so calling
 * this on every fetch is safe. The NVS write is what needs rationing, so it
 * happens only when the day actually changes.
 */
static void record_history(int64_t mrr_cents)
{
    const int32_t day = local_epoch_day();
    if (day < 0) {
        return;             /* no clock: record nothing rather than a guess */
    }

    const bool day_changed = (day != s_history_day);
    history_record(&s_history, day, mrr_cents);
    s_history_day = day;

    if (day_changed) {
        if (settings_save_history(&s_history, sizeof(s_history))) {
            Serial.printf("history: %d sample(s) saved\n",
                          history_count(&s_history));
        }
    }
}

/* ---- network ---- */

/*
 * Join, keeping the UI and the portal alive while we wait.
 *
 * A join takes roughly 10-30s on this board. Blocking through it without
 * pumping the portal leaves the owner's phone talking to a socket nobody is
 * reading, so the "Connecting..." page appears to hang -- which is exactly
 * what it looked like on the bench. portal_tick() is safe to call when the
 * portal is stopped; it returns immediately.
 */
static bool join_wifi(const char *ssid, const char *pass, uint32_t timeout_ms)
{
    Serial.printf("joining %s\n", ssid);
    WiFi.begin(ssid, pass);

    const uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("OK: wifi %s\n", WiFi.localIP().toString().c_str());
            return true;
        }
        delay(20);
        lv_timer_handler();
        portal_tick();
    }
    Serial.println("wifi join timed out");
    return false;
}

/*
 * Write the last-good values to flash.
 *
 * Only after a successful fetch, and only with a real clock: a cache stamped
 * 1970 would read as a day old the moment it was restored, and
 * cache_too_old() would throw away perfectly good data.
 */
static void save_cache(const mrr_totals_t *t)
{
    const time_t now = time(NULL);
    if (now < CLOCK_SANE_EPOCH) {
        return;
    }

    cache_t c = {};
    c.version = CACHE_VERSION;
    c.saved_at_utc = (int64_t)now;
    c.mrr_cents = t->mrr_cents;
    c.active_count = t->active_count;
    c.trial_count = t->trial_count;
    c.churned_30d = t->churned_count;
    c.new_paid_30d = t->new_count;
    c.failed_count = s_failed_count;
    c.have_invoices = s_rot_state.have_invoices;
    c.mixed_currency = t->mixed_currency;
    c.has_tiered = t->has_tiered;

    settings_save_cache(&c, sizeof(c));
}

/*
 * Restore last-good values so the screen is never blank on boot.
 *
 * The age is the point. These figures are put on screen WITH the age they
 * actually have, by seeding freshness from the cache timestamp rather than
 * marking a success -- so if the cache is older than the stale threshold the
 * device shows the stale screen immediately, instead of presenting an
 * hour-old number as current.
 *
 * Returns false when there is nothing usable, and the deck keeps its dashes.
 */
static bool restore_cache(void)
{
    cache_t c;
    if (!settings_load_cache(&c, sizeof(c))) {
        return false;
    }
    if (!cache_is_valid(&c)) {
        Serial.println("cache: present but not valid, ignoring");
        return false;
    }

    const time_t now = time(NULL);
    if (now < CLOCK_SANE_EPOCH) {
        /* No clock yet, so the age cannot be judged. Showing the values
         * anyway is fine -- they are marked stale until a fetch proves
         * otherwise. */
        Serial.println("cache: restored, age unknown (no clock yet)");
    } else if (cache_too_old(&c, (int64_t)now)) {
        Serial.println("cache: too old to show");
        return false;
    }

    mrr_totals_t t = {};
    t.mrr_cents = c.mrr_cents;
    t.active_count = c.active_count;
    t.trial_count = c.trial_count;
    t.churned_count = c.churned_30d;
    t.new_count = c.new_paid_30d;
    t.mixed_currency = c.mixed_currency;
    t.has_tiered = c.has_tiered;

    apply_totals(&t);
    s_from_cache = true;

    /*
     * Seed freshness with the cache's real age rather than marking a success.
     * millis() is near zero at boot, so the age is subtracted from now: the
     * device believes the data is exactly as old as it is.
     */
    if (now >= CLOCK_SANE_EPOCH) {
        const int64_t age_s = cache_age_seconds(&c, (int64_t)now);
        if (age_s >= 0) {
            s_freshness.last_success_ms = (int64_t)millis() - age_s * 1000;
            Serial.printf("cache: restored, %lld seconds old\n",
                          (long long)age_s);
        }
    }
    return true;
}

static void refresh(void)
{
    mrr_totals_t totals;
    bool truncated = false;

    const stripe_fetch_result_t r = stripe_fetch_totals(&totals, &truncated);
    if (r != STRIPE_FETCH_OK) {
        freshness_mark_failure(&s_freshness);
        /*
         * An auth failure is not a network problem and must not be shown as
         * one. A revoked key drags the data stale behind it within the
         * threshold, so both flags end up true; state.c gives auth
         * precedence, because "there will be no more numbers" is a different
         * instruction from "these are old".
         */
        if (r == STRIPE_FETCH_UNAUTHORIZED) {
            s_auth_failed = true;
        }
        Serial.printf("fetch failed: %s\n", stripe_fetch_strerror(r));
        /* Keep the last good values on screen rather than blanking the deck.
         * Marking them visibly stale (freshness.c) is still to port. */
        return;
    }

    freshness_mark_success(&s_freshness, (int64_t)millis());
    s_auth_failed = false;
    s_from_cache = false;

    apply_totals(&totals);
    save_cache(&totals);
    Serial.printf("fetch ok: MRR %lld cents, %d active, %d trial, "
                  "flow +%d/-%d%s\n",
                  (long long)totals.mrr_cents, totals.active_count,
                  totals.trial_count, totals.new_count, totals.churned_count,
                  truncated ? " (TRUNCATED)" : "");
    Serial.printf("flow money: +%lld / -%lld cents, net %+lld\n",
                  (long long)totals.new_cents, (long long)totals.churned_cents,
                  (long long)(totals.new_cents - totals.churned_cents));
    /* Read here rather than reporting s_battery: the rotation tick populates
     * that, and the first fetch runs before the first tick, so logging the
     * cached value printed 0 mV on boot. */
    battery_hw_read(&s_battery);
    Serial.printf("battery: %d mV, %s\n", s_battery.cell_mv,
                  s_battery.charging ? "charging" : "on cell");
    Serial.printf("new paid pace: this=%d prior=%d\n",
                  totals.new_count, totals.prior_new_count);
    Serial.printf("arpu mix: comparable=%d joining=%lld leaving=%lld\n",
                  (int)mrr_mix_comparable(totals.new_count, totals.churned_count),
                  (long long)mrr_arpu_cents(totals.new_cents, totals.new_count),
                  (long long)mrr_arpu_cents(totals.churned_cents, totals.churned_count));
    Serial.printf("at risk: %d sub(s), %lld cents/mo, soonest %lld\n",
                  totals.at_risk_count, (long long)totals.at_risk_cents,
                  (long long)totals.at_risk_soonest);
}

/* ---- portal callbacks ---- */

static void on_creds(const char *ssid, const char *pass)
{
    /* Shared validator, so the portal and the ESP-IDF build reject the same
     * inputs with the same words. */
    const wifi_cred_result_t v = wifi_creds_validate(ssid, pass);
    if (v != WIFI_CRED_OK) {
        Serial.printf("portal: rejected creds: %s\n", wifi_cred_result_str(v));
        return;
    }

    if (!settings_set_wifi(ssid, pass)) {
        Serial.println("portal: failed to save credentials");
        return;
    }
    Serial.println("portal: credentials saved");

    /* The AP is torn down and rebuilt in the key phase by the main loop,
     * which owns mode transitions -- doing it from inside a request handler
     * would drop the socket the response is still being written to. */
    s_mode = MODE_SETUP_KEY;
}

/*
 * Validate a Stripe key by actually using it (spec 9.1 step 4).
 *
 * Shape is checked first because it is free and gives a specific message --
 * "that is a secret key" beats "invalid key" for someone who pasted the wrong
 * one. Only a well-formed key is spent on a network round trip.
 */
static bool on_key(const char *key, char *out_msg, size_t msg_len)
{
    const stripe_key_result_t shape = stripe_key_validate(key);
    if (shape != STRIPE_KEY_OK) {
        snprintf(out_msg, msg_len, "%s", stripe_key_result_str(shape));
        return false;
    }

    if (WiFi.status() != WL_CONNECTED) {
        snprintf(out_msg, msg_len, "The display is not on your network yet.");
        return false;
    }

    /* Try it against the live API before storing it. */
    stripe_fetch_set_key(key);

    mrr_totals_t totals;
    bool truncated = false;
    const stripe_fetch_result_t r = stripe_fetch_totals(&totals, &truncated);

    if (r == STRIPE_FETCH_UNAUTHORIZED) {
        snprintf(out_msg, msg_len,
                 "Stripe rejected that key. Check it has Read access to "
                 "Subscriptions.");
        stripe_fetch_set_key("");
        return false;
    }
    if (r != STRIPE_FETCH_OK) {
        snprintf(out_msg, msg_len, "Could not reach Stripe (%s). Try again.",
                 stripe_fetch_strerror(r));
        stripe_fetch_set_key("");
        return false;
    }

    if (!settings_set_stripe_key(key)) {
        snprintf(out_msg, msg_len, "Could not save the key. Try again.");
        return false;
    }

    snprintf(s_key, sizeof(s_key), "%s", key);
    apply_totals(&totals);
    Serial.println("portal: key validated and saved");

    s_mode = MODE_RUNNING;
    return true;
}

/* ---- setup ---- */

static void start_portal(bool key_phase)
{
    /*
     * The setup SSID must be derived from the factory base MAC, not from
     * WiFi.macAddress().
     *
     * WiFi.macAddress() returns the MAC of the *current* interface, and the
     * softAP MAC differs from the station MAC. Changing WiFi.mode() between
     * the wifi and key phases therefore renamed the network mid-setup --
     * "Setup-3031", then "Setup-6232", then "Setup-DD30" on the same board --
     * so the owner's phone silently dropped off the AP between entering their
     * WiFi and entering their key, and the name on the glass stopped matching
     * the name in their WiFi list.
     *
     * esp_efuse_mac_get_default() is the immutable per-board value and does
     * not move with interface state. Derive the name once and keep it.
     */
    if (s_setup_ssid[0] == '\0') {
        uint8_t mac[6];
        esp_efuse_mac_get_default(mac);
        setup_ssid_from_mac(mac, s_setup_ssid, sizeof(s_setup_ssid));
    }

    portal_start(on_creds, on_key, s_setup_ssid, key_phase);
    draw_setup_screen(key_phase ? "Add Stripe key" : "Join wifi");
}

void setup(void)
{
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=== Stripe Revenue Display " FIRMWARE_VERSION " ===");

    clear_values();

    if (!power_up()) {
        Serial.println("HALT: no PMIC, panel cannot be powered");
        return;
    }
    if (!display_init() || !display_lvgl_init()) {
        Serial.println("HALT: display init failed");
        return;
    }
    Serial.printf("layout: panel %dpx, hero %d-%dpx\n",
                  PANEL_PX, SIZE_HERO_MIN, SIZE_HERO_MAX);

    if (!settings_init()) {
        Serial.println("WARN: settings unavailable; setup cannot persist");
    }

    /*
     * Restore the series before anything can record into it.
     *
     * A failed or absent load leaves it zeroed by history_init, which is the
     * correct fresh-device state -- no samples, no trend, and the card says
     * it is collecting.
     */
    history_init(&s_history);
    if (settings_load_history(&s_history, sizeof(s_history))) {
        Serial.printf("history: restored %d sample(s)\n",
                      history_count(&s_history));
    }
    /*
     * Seed the day from the restored series, not from -1.
     *
     * s_history_day drives the "did the day roll over?" test that rations NVS
     * writes. Left at -1 it reads as a day change on the first fetch after
     * every boot, so a device power-cycled a few times a day would write
     * flash each time for a sample already stored.
     */
    s_history_day = history_latest_day(&s_history);
    update_delta(0);

    /*
     * Fill the screen from flash before touching the network.
     *
     * This is the whole point of the cache: WiFi, NTP and the first fetch take
     * roughly twelve seconds, and a desk instrument that is read constantly
     * and restarted rarely should not spend that time showing dashes.
     *
     * The clock has not synced yet, so the age cannot be judged here. The
     * values go up regardless and are re-aged after sync_clock() below --
     * being briefly unable to say how old they are is not a reason to show
     * nothing.
     */
    const bool had_cache = restore_cache();

    /* Decide the mode from what is actually stored. */
    char ssid[SETTINGS_SSID_LEN];
    char pass[SETTINGS_PASS_LEN];

    if (!settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
        Serial.println("no stored wifi: entering setup");
        s_mode = MODE_SETUP_WIFI;
        start_portal(false);
        return;
    }

    WiFi.mode(WIFI_STA);
    if (!join_wifi(ssid, pass, JOIN_TIMEOUT_MS)) {
        /*
         * Stored credentials that will not join. The network may simply be
         * down, and wiping a working password because the router rebooted
         * would be worse than waiting -- so the credentials are kept and the
         * portal comes up to offer a correction.
         */
        Serial.println("stored wifi failed: offering setup");
        s_mode = MODE_SETUP_WIFI;
        start_portal(false);
        return;
    }

    if (!settings_get_stripe_key(s_key, sizeof(s_key))) {
        Serial.println("no stored key: entering key setup");
        s_mode = MODE_SETUP_KEY;
        start_portal(true);
        return;
    }

    /*
     * Show the cached values now, before the fetch.
     *
     * join_wifi already ran, so this is the earliest point the deck can be
     * drawn with real numbers rather than dashes.
     */
    if (had_cache) {
        s_mode = MODE_RUNNING;
        show(0);
    }

    /* Clock before the first fetch, so that fetch can be bucketed. */
    sync_clock();

    /*
     * Re-age the restored cache now that the clock is real.
     *
     * restore_cache() ran before NTP, so it could not tell how old the values
     * were. With a clock, the age is applied properly -- and if the cache is
     * older than the stale threshold the deck switches to the stale screen on
     * its own, which is the correct outcome rather than a bug.
     */
    if (had_cache && s_clock_ok) {
        cache_t c;
        if (settings_load_cache(&c, sizeof(c)) && cache_is_valid(&c)) {
            const int64_t age_s = cache_age_seconds(&c, (int64_t)time(NULL));
            if (age_s >= 0) {
                s_freshness.last_success_ms =
                    (int64_t)millis() - age_s * 1000;
                Serial.printf("cache: aged %lld seconds\n", (long long)age_s);
            }
        }
    }

    stripe_fetch_set_key(s_key);
    s_mode = MODE_RUNNING;
    show(0);
    refresh();
    s_last_refresh = millis();
}

/* ---- loop ---- */

/*
 * Mode transitions happen here, not in the portal callbacks.
 *
 * A callback runs inside a request handler, with the response still being
 * written to a socket that belongs to the AP it would be tearing down. The
 * callbacks set the mode; this acts on it once the response is out.
 */
static void service_mode_change(void)
{
    static app_mode_t applied = MODE_SETUP_WIFI;
    static bool initialised;

    if (!initialised) {
        applied = s_mode;
        initialised = true;
        return;
    }
    if (s_mode == applied) {
        return;
    }

    if (s_mode == MODE_SETUP_KEY && applied == MODE_SETUP_WIFI) {
        /*
         * WiFi was just submitted: join it, then serve the key form.
         *
         * The portal keeps running throughout -- it is not paused first --
         * so the "Connecting..." page's refresh is answered the moment the
         * join lands, and the owner is carried straight to the key form.
         */
        char ssid[SETTINGS_SSID_LEN];
        char pass[SETTINGS_PASS_LEN];
        if (settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) &&
            join_wifi(ssid, pass, JOIN_TIMEOUT_MS)) {
            /* Same AP, same server -- only the page changes. */
            portal_set_phase(true);
            draw_setup_screen("Add Stripe key");
        } else {
            portal_pause();
            /* Could not join with what was just entered -- back to the WiFi
             * form rather than a key page the device cannot act on. */
            settings_clear_wifi();
            s_mode = MODE_SETUP_WIFI;
            start_portal(false);
        }
    } else if (s_mode == MODE_RUNNING) {
        /* Provisioned just now: the clock has not been synced this boot. */
        if (!s_clock_ok) {
            sync_clock();
        }
        /*
         * Leave the AP up briefly after a successful key.
         *
         * The owner is still looking at the "All set" page on their phone.
         * Dropping the AP the instant the callback returns closes that page
         * under them, so setup appears to fail at the exact moment it
         * succeeded. A few seconds is enough to read the confirmation.
         */
        delay(3000);
        portal_stop();
        show(0);
        s_last_refresh = millis();
    }

    applied = s_mode;
}

void loop(void)
{
    static uint32_t last_tick;
    static uint32_t last_rotate;

    const uint32_t now = millis();

    lv_tick_inc(now - last_tick);
    last_tick = now;
    lv_timer_handler();

    service_mode_change();

    if (s_mode != MODE_RUNNING) {
        portal_tick();
        delay(2);
        return;
    }

    if (now - last_rotate >= ROTATE_MS) {
        last_rotate = now;
        /* Cheap I2C read; once per rotation is far more often than a cell
         * changes level, and it keeps the warning responsive. */
        battery_hw_read(&s_battery);
        /*
         * Wrap on the VISIBLE count, not SCREEN_COUNT.
         *
         * index walks the visible list, which is rebuilt after every fetch
         * and is shorter than the enum whenever a screen is hidden -- seven
         * of ten here, since trials, conversion and failed payments have
         * nothing to show. Wrapping on the enum sent the index past the end
         * of the list, where show() clamped it back to slot 0, so MRR was
         * drawn three extra times in a row and sat on screen for twenty
         * seconds while every other screen got five. It read as ARPU (the
         * last visible screen) handing over to a stuck MRR.
         */
        if (s_visible_count > 0) {
            s_slot = (s_slot + 1) % s_visible_count;
        }
        show(s_slot);
    }

    if (s_have_data && now - s_last_refresh >= REFRESH_MS) {
        s_last_refresh = now;
        refresh();
    }

    delay(5);
}
