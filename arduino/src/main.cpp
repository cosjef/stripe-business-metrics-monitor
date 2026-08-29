/*
 * Stripe Revenue Display -- Arduino port, stage 4: live data.
 *
 * Stage 1 proved the panel, stage 2 brought the tested renderer across, and
 * this fetches real numbers from Stripe and puts them on the glass. The
 * fetch/compute core is shared with the ESP-IDF tree (jsonstream.c, mrr.c,
 * format.c) rather than copied, so the host suite keeps covering it.
 *
 * Still missing, and deliberately: WiFi credentials and the Stripe key are
 * compiled in here rather than provisioned. Stage 3 -- the captive portal and
 * NVS -- is what makes this a product rather than a bench rig, and the key
 * belongs there, entered through the portal and stored in NVS. Nothing below
 * should be read as the final shape of provisioning.
 *
 * The device never invents numbers (spec 1, principle 4): until the first
 * fetch lands, the deck shows dashes rather than a plausible zero.
 */
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "board.h"
#include "display.h"
#include "stripe_fetch.h"

extern "C" {
#include "format.h"
#include "screens.h"
}
#include "layout.h"

/*
 * Bench credentials. Passed as build flags, never committed:
 *   PLATFORMIO_BUILD_FLAGS='-DWIFI_SSID=\"..\" -DWIFI_PASS=\"..\" \
 *                           -DSTRIPE_KEY=\"rk_live_..\"' pio run
 * Stage 3 replaces all three with the portal and NVS.
 */
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif
#ifndef STRIPE_KEY
#define STRIPE_KEY ""
#endif

#define ROTATE_MS  5000
#define REFRESH_MS (5 * 60 * 1000)   /* spec 7.1: poll every five minutes */

static XPowersPMU s_pmu;

/* Field width: the longest subtitle plus slack. The compiler checks the
 * formats that write into these. */
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

static bool s_have_data;      /* false until a fetch succeeds */
static bool s_truncated;

/* Seeded in setup() after the first fetch, so the five-minute clock measures
 * from that fetch rather than from boot. */
static uint32_t s_last_refresh;

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

/* Dashes, not zeroes: an invented number is worse than an obvious blank. */
static void clear_values(void)
{
    for (int i = 0; i < SCREEN_COUNT; i++) {
        snprintf(s_values[i].hero, FIELD_LEN, "--");
        s_values[i].subtitle[0] = '\0';
    }
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

    /* ARR is MRR x12 -- a projection, labelled as one on the screen. */
    format_money_compact(t->mrr_cents * 12, s_values[SCREEN_ARR].hero,
                         FIELD_LEN);
    snprintf(s_values[SCREEN_ARR].subtitle, FIELD_LEN, "at current MRR");

    /* ARPU divides by paying subscribers only, and guards the zero case --
     * an account with no active subs must show "--", not crash or show 0. */
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

static void refresh(void)
{
    mrr_totals_t totals;
    bool truncated = false;

    const uint32_t t0 = millis();
    const stripe_fetch_result_t r = stripe_fetch_totals(&totals, &truncated);
    const uint32_t took = millis() - t0;

    if (r != STRIPE_FETCH_OK) {
        Serial.printf("fetch failed: %s (%lums)\n", stripe_fetch_strerror(r),
                      (unsigned long)took);
        /* Keep showing the last good values rather than blanking the deck.
         * Marking them stale is stage 3 work (freshness.c). */
        return;
    }

    s_truncated = truncated;
    apply_totals(&totals);

    Serial.printf("fetch ok in %lums: MRR %lld cents, %d active, %d trial%s\n",
                  (unsigned long)took, (long long)totals.mrr_cents,
                  totals.active_count, totals.trial_count,
                  truncated ? " (TRUNCATED)" : "");
    Serial.printf("heap free=%u largest=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
}

void setup(void)
{
    Serial.begin(115200);
    delay(2000);

    Serial.println();
    Serial.println("=== Stripe Revenue Display -- Arduino port, stage 4 ===");

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

    /* Show the deck immediately, in its no-data state, so the device is
     * never a blank screen while the network comes up. */
    show(0);

    if (strlen(WIFI_SSID) == 0 || strlen(STRIPE_KEY) == 0) {
        Serial.println("No credentials compiled in -- deck stays on '--'.");
        Serial.println("Rebuild with PLATFORMIO_BUILD_FLAGS="
                       "'-DWIFI_SSID=\\\"..\\\" -DWIFI_PASS=\\\"..\\\" "
                       "-DSTRIPE_KEY=\\\"..\\\"'");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.printf("connecting to %s\n", WIFI_SSID);
    for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
        lv_timer_handler();
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("wifi failed; deck stays on '--'");
        return;
    }
    Serial.printf("OK: wifi %s\n", WiFi.localIP().toString().c_str());

    stripe_fetch_set_key(STRIPE_KEY);
    refresh();
    s_last_refresh = millis();   /* the refresh clock starts after fetch one */
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
