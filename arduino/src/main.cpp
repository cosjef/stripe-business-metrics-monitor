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

#include "board.h"
#include "display.h"
#include "portal.h"
#include "settings.h"
#include "stripe_fetch.h"

extern "C" {
#include "format.h"
#include "history.h"
#include "provision.h"
#include "rotation.h"
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
#define DEVICE_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"

/* Clocks before this are unset, not merely wrong: 2023-01-01. */
#define CLOCK_SANE_EPOCH 1672531200

#define ROTATE_MS  5000
#define REFRESH_MS (5 * 60 * 1000)   /* spec 7.1: poll every five minutes */
#define JOIN_TIMEOUT_MS 30000

static XPowersPMU s_pmu;

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

/* `slot` indexes the visible list, not screen_id_t. */
static void show(int slot)
{
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

static void rebuild_rotation(void)
{
    const int n = rotation_build(&s_rot_state, s_visible);
    if (n <= 0) {
        /* rotation_build guarantees MRR, but never leave the device with an
         * empty deck -- a blank rotation reads as broken. */
        s_visible[0] = SCREEN_MRR;
        s_visible_count = 1;
        return;
    }
    s_visible_count = n;
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

    /* Subscriber flow, for the PAID SUBS card. */
    s_flow_g = t->new_count;
    s_flow_l = t->churned_count;
    const int net = s_flow_g - s_flow_l;
    snprintf(s_flow_net, sizeof(s_flow_net), "net %s%d",
             net >= 0 ? "+" : "", net);
    snprintf(s_flow_gained, sizeof(s_flow_gained), "%d joined", s_flow_g);
    snprintf(s_flow_lost, sizeof(s_flow_lost), "%d left", s_flow_l);
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

static void refresh(void)
{
    mrr_totals_t totals;
    bool truncated = false;

    const stripe_fetch_result_t r = stripe_fetch_totals(&totals, &truncated);
    if (r != STRIPE_FETCH_OK) {
        Serial.printf("fetch failed: %s\n", stripe_fetch_strerror(r));
        /* Keep the last good values on screen rather than blanking the deck.
         * Marking them visibly stale (freshness.c) is still to port. */
        return;
    }

    apply_totals(&totals);
    Serial.printf("fetch ok: MRR %lld cents, %d active, %d trial, "
                  "flow +%d/-%d%s\n",
                  (long long)totals.mrr_cents, totals.active_count,
                  totals.trial_count, totals.new_count, totals.churned_count,
                  truncated ? " (TRUNCATED)" : "");
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

    /* Clock before the first fetch, so that fetch can be bucketed. */
    sync_clock();

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
    static int index;

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
        index = (index + 1) % SCREEN_COUNT;
        show(index);
    }

    if (s_have_data && now - s_last_refresh >= REFRESH_MS) {
        s_last_refresh = now;
        refresh();
    }

    delay(5);
}
