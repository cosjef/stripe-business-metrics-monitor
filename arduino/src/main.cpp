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
#include "provision.h"
#include "screens.h"
#include "stripe_key.h"
}
#include "layout.h"

#define FIRMWARE_VERSION "v0.4.0"

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

typedef enum {
    SCREEN_MRR = 0,
    SCREEN_PAID_SUBS,
    SCREEN_TRIALS,
    SCREEN_ARR,
    SCREEN_ARPU,
    SCREEN_COUNT,
} screen_id_t;

static struct {
    char hero[FIELD_LEN];
    char subtitle[FIELD_LEN];
} s_values[SCREEN_COUNT];

static const char *s_labels[SCREEN_COUNT] = {
    "MRR", "PAID SUBS", "TRIALS", "ANNUAL RUN RATE", "ARPU",
};

static char s_setup_ssid[SETUP_SSID_LEN];
static char s_key[STRIPE_KEY_MAX_LEN + 1];
static bool s_have_data;
static uint32_t s_last_refresh;

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

static void show(int index)
{
    screen_data_t d = {};
    d.label = s_labels[index];
    d.hero = s_values[index].hero;
    d.subtitle = s_values[index].subtitle;
    d.dot_index = index;
    d.dot_count = SCREEN_COUNT;

    screen_draw_rotation(lv_screen_active(), &d);
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
    Serial.printf("fetch ok: MRR %lld cents, %d active, %d trial%s\n",
                  (long long)totals.mrr_cents, totals.active_count,
                  totals.trial_count, truncated ? " (TRUNCATED)" : "");
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
