/*
 * Stripe Revenue Display -- Stage 3.
 *
 * On first boot the device has no WiFi credentials, so it runs setup mode: an
 * open "Setup-XXXX" AP with a captive portal (spec 9.1), showing State C on
 * screen. Once provisioned it joins the network and rotates through the six
 * metric screens (spec 6.1) on an 8-second timer.
 *
 * Metric values are still hardcoded fixtures; Stage 4 replaces them with live
 * Stripe data.
 *
 * All drawing lives in screens.c, which depends only on LVGL and is covered by
 * pixel tests in the host harness (firmware/test/test_screens.c). This file
 * owns hardware bring-up and the rotation timer only.
 *
 * Known Stage 3 limitations, tracked in firmware-build-plan.md:
 *   - No partial-window updates (spec 5.3); LVGL redraws as it sees fit.
 *   - The portal collects WiFi only. The Stripe key and preferences come in
 *     Stage 4, where the key can be validated the moment it is entered
 *     (spec 9.1 step 4) rather than stored untested.
 *   - Stale and auth-error states are unreachable at runtime: those screens
 *     exist and are pixel-tested, but no live data yet triggers them.
 */
#include "display.h"
#include "board_config.h"
#include "layout.h"
#include "hero_size.h"
#include "screens.h"
#include "imu.h"
#include "settings.h"
#include "wifi.h"
#include "portal.h"
#include "stripe_key.h"
#include "stripe_api.h"
#include "format.h"
#include "events.h"
#include "freshness.h"

#include "esp_netif_sntp.h"
#include <time.h>
#include "colortest.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

/* Shown in the setup screen footer, for support (spec 6.2 State C). */
#define FIRMWARE_VERSION "v0.3.0"

/* Set to 1 to draw the color diagnostic instead of the rotation. */
#define COLORTEST_ENABLED 0

/*
 * Screen contents, refreshed from Stripe (spec 7.1, 7.2).
 *
 * The strings are owned here rather than by the screen code, so a refresh can
 * rewrite them in place while the rotation timer keeps running.
 */
/* Wide enough for the longest subtitle ("today, 12 churned") plus slack;
 * the compiler checks this and will fail the build if a format can overrun. */
#define FIELD_LEN 40

static struct {
    char hero[FIELD_LEN];
    char subtitle[FIELD_LEN];
} s_values[6];

static screen_data_t s_rotation[] = {
    { .label = "MRR",        .hero_is_gain = 0, .subtitle_is_gain = 1 },
    { .label = "NEW PAID",   .hero_is_gain = 1, .subtitle_is_gain = 0 },
    { .label = "PAID SUBS",  .hero_is_gain = 0, .subtitle_is_gain = 1 },
    { .label = "TRIALS",     .hero_is_gain = 0, .subtitle_is_gain = 0 },
    { .label = "CONVERSION", .hero_is_gain = 0, .subtitle_is_gain = 0 },
    { .label = "LAST EVENT", .hero_is_gain = 1, .subtitle_is_gain = 0 },
};

/* Until the first fetch lands, show dashes rather than invented numbers.
 * Spec 1 principle 4: the device never lies, and a plausible-looking zero
 * would be a lie about an account that has not been read yet. */
static void set_placeholders(void)
{
    for (int i = 0; i < 6; i++) {
        snprintf(s_values[i].hero, FIELD_LEN, "--");
        s_values[i].subtitle[0] = '\0';
        s_rotation[i].hero = s_values[i].hero;
        s_rotation[i].subtitle = s_values[i].subtitle;
    }
}

/* Populate the screens from a completed fetch. */
static void apply_totals(const mrr_totals_t *t, bool truncated)
{
    format_money_compact(t->mrr_cents, s_values[0].hero, FIELD_LEN);
    if (t->mixed_currency) {
        /* Do not present a sum across currencies as if it were one number. */
        snprintf(s_values[0].subtitle, FIELD_LEN, "mixed currency");
    } else if (truncated) {
        snprintf(s_values[0].subtitle, FIELD_LEN, "partial");
    } else if (t->has_tiered) {
        snprintf(s_values[0].subtitle, FIELD_LEN, "excl. usage plans");
    } else {
        /* No subtitle in the normal case. The currency is not worth a line:
         * it never changes for a given account, and spec 5.1 gives the
         * subtitle to context that earns its place. Today's delta takes this
         * slot once the events endpoint lands. */
        s_values[0].subtitle[0] = '\0';
    }

    /* Today's deltas need the events endpoint, which lands with the polling
     * layer. Dashes rather than a fabricated zero. */
    snprintf(s_values[1].hero, FIELD_LEN, "--");
    snprintf(s_values[1].subtitle, FIELD_LEN, "needs events");

    format_count(t->active_count, s_values[2].hero, FIELD_LEN);
    snprintf(s_values[2].subtitle, FIELD_LEN, "active");

    format_count(t->trial_count, s_values[3].hero, FIELD_LEN);
    snprintf(s_values[3].subtitle, FIELD_LEN, "trialing");

    /* Conversion needs 30 days of history; spec 6.1 also cautions it swings
     * wildly at low volume. */
    snprintf(s_values[4].hero, FIELD_LEN, "--");
    snprintf(s_values[4].subtitle, FIELD_LEN, "needs history");

    snprintf(s_values[5].hero, FIELD_LEN, "--");
    snprintf(s_values[5].subtitle, FIELD_LEN, "needs events");

    for (int i = 0; i < 6; i++) {
        s_rotation[i].hero = s_values[i].hero;
        s_rotation[i].subtitle = s_values[i].subtitle;
    }
}

/*
 * Fill the event-driven screens: New Paid, today's delta on MRR, and the
 * Last Event heartbeat (spec 6.1, 7.3).
 */
static void apply_events(const event_totals_t *e)
{
    /* MRR subtitle: today's realized movement. Green, because spec 4.2
     * reserves green for realized positive movement and nothing else. */
    if (e->revenue_cents > 0) {
        char delta[FORMAT_MONEY_LEN];
        format_money_delta(e->revenue_cents, delta, sizeof(delta));
        snprintf(s_values[0].subtitle, FIELD_LEN, "%s today", delta);
        s_rotation[0].subtitle_is_gain = true;
    }

    /* New paid today. */
    format_count(e->new_paid, s_values[1].hero, FIELD_LEN);
    if (e->churned > 0) {
        snprintf(s_values[1].subtitle, FIELD_LEN, "today, %d churned",
                 e->churned);
    } else {
        snprintf(s_values[1].subtitle, FIELD_LEN, "today");
    }
    /* Only green when something actually happened: a green zero would imply
     * a gain that did not occur. */
    s_rotation[1].hero_is_gain = e->new_paid > 0;

    /* The heartbeat. Confirms the device is live without a status indicator
     * (spec 6.1). */
    if (e->have_last) {
        if (e->last_amount_cents > 0) {
            format_money_delta(e->last_amount_cents, s_values[5].hero, FIELD_LEN);
            s_rotation[5].hero_is_gain = true;
        } else {
            snprintf(s_values[5].hero, FIELD_LEN, "%s",
                     event_kind_label(e->last_kind));
            s_rotation[5].hero_is_gain = false;
        }

        char age[16];
        event_format_age(e->last_created, time(NULL), age, sizeof(age));
        snprintf(s_values[5].subtitle, FIELD_LEN, "%s, %s",
                 event_kind_label(e->last_kind), age);
    }

    for (int i = 0; i < 6; i++) {
        s_rotation[i].hero = s_values[i].hero;
        s_rotation[i].subtitle = s_values[i].subtitle;
    }
}

#define ROTATION_COUNT ((int)(sizeof(s_rotation) / sizeof(s_rotation[0])))

static int s_index = 0;
static esp_timer_handle_t s_rotation_timer = NULL;

static void show_current(const char *why)
{
    screen_data_t d = s_rotation[s_index];
    d.dot_index = s_index;
    d.dot_count = ROTATION_COUNT;

    lvgl_port_lock(0);
    screen_draw_rotation(lv_screen_active(), &d);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "[%d/%d] %s: '%s' at %dpx (%s)",
             s_index + 1, ROTATION_COUNT, d.label, d.hero,
             hero_size_for_text(d.hero), why);
}

static void advance(const char *why)
{
    s_index = (s_index + 1) % ROTATION_COUNT;
    show_current(why);
}

static void rotate_cb(void *arg)
{
    (void)arg;
    advance("timer");
}

/*
 * A double tap advances immediately and restarts the rotation timer, so the
 * screen you asked for gets a full interval before it moves on. Rotation is
 * never suspended -- the device is an appliance, and one left showing a single
 * metric would stop being one (spec 1).
 */
static void on_double_tap(void)
{
    advance("tap");

    if (s_rotation_timer) {
        esp_timer_stop(s_rotation_timer);
        esp_timer_start_periodic(s_rotation_timer, ROTATION_INTERVAL_MS * 1000);
    }
}

/* Setup mode: show State C and run the portal until credentials arrive. */
static char s_setup_ssid[SETUP_SSID_LEN];
static volatile bool s_restart_pending = false;

static void show_setup_screen(void)
{
    lvgl_port_lock(0);
    screen_draw_setup(lv_screen_active(), "Join wifi", s_setup_ssid,
                      "then open browser", FIRMWARE_VERSION);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "setup mode: join \"%s\"", s_setup_ssid);
}

/*
 * Validate a Stripe key against the live API (spec 9.1 step 4).
 *
 * Runs only once WiFi is up, so the key is proven before it is stored rather
 * than failing minutes later on the device with no explanation.
 */
static bool on_stripe_key(const char *key, char *out_msg, size_t msg_len)
{
    /* This runs on the httpd task, which must have enough stack for the TLS
     * handshake -- see cfg.stack_size in portal_start(). An earlier build
     * overflowed it here and rebooted mid-request, which looked to the
     * customer like the form doing nothing at all. Check the margin so a
     * future regression reports itself instead of crashing. */
    const UBaseType_t headroom = uxTaskGetStackHighWaterMark(NULL);
    if (headroom < 2048) {
        ESP_LOGW(TAG, "low stack before TLS: %u bytes free", (unsigned)headroom);
    }

    stripe_set_key(key);
    const stripe_validation_t v = stripe_validate_key();

    if (v.result != STRIPE_OK) {
        snprintf(out_msg, msg_len, "%s (HTTP %d)",
                 stripe_result_str(v.result), v.http_status);
        ESP_LOGW(TAG, "key validation failed: %s", out_msg);
        return false;
    }

    if (settings_set_stripe_key(key) != ESP_OK) {
        snprintf(out_msg, msg_len, "Could not save the key to the device");
        return false;
    }

    /* Show the customer their own account state -- the spec's "converts
     * skeptics" moment. Deliberately not a number yet: MRR needs the
     * computation engine from Stage 5. */
    snprintf(out_msg, msg_len,
             "Key verified%s. %s",
             v.test_mode ? " (test mode)" : "",
             v.has_subscriptions ? "Found active subscriptions."
                                 : "No active subscriptions yet.");

    ESP_LOGI(TAG, "key validated and stored");

    /* Restart into normal mode once the customer has seen the confirmation. */
    s_restart_pending = true;
    return true;
}

static void on_credentials(const char *ssid, const char *pass)
{
    ESP_LOGI(TAG, "credentials received for \"%s\"", ssid);

    if (settings_set_wifi(ssid, pass) != ESP_OK) {
        ESP_LOGE(TAG, "failed to store credentials");
        return;
    }

    /* Restart rather than tearing down the AP and portal in place. A reboot
     * from a known state is far less error-prone than unwinding softAP,
     * the HTTP server, and the DNS task while a client is still associated. */
    ESP_LOGI(TAG, "restarting to join the network");
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();
}

static void run_setup_mode(void)
{
    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(wifi_start_ap(s_setup_ssid, sizeof(s_setup_ssid)));
    ESP_ERROR_CHECK(portal_start(on_credentials, on_stripe_key, false));
    show_setup_screen();
}

/*
 * Second setup phase: WiFi is configured but no Stripe key is stored yet.
 *
 * The device joins the customer's network AND runs the AP simultaneously, so
 * the key can be validated against the live API the moment it is entered
 * (spec 9.1 step 4). Validating before storing is the whole point -- a key
 * that fails should be corrected in the form, not discovered minutes later on
 * the device.
 */
static void run_key_setup_mode(void)
{
    char ssid[WIFI_SSID_MAX_LEN + 1] = "";
    char pass[WIFI_PASS_MAX_LEN + 1] = "";

    if (settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        run_setup_mode();
        return;
    }

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(wifi_start_apsta(s_setup_ssid, sizeof(s_setup_ssid),
                                     ssid, pass));
    ESP_ERROR_CHECK(portal_start(on_credentials, on_stripe_key, true));

    lvgl_port_lock(0);
    screen_draw_setup(lv_screen_active(), "Add Stripe key", s_setup_ssid,
                      "then open browser", FIRMWARE_VERSION);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "key setup: join \"%s\", go to http://192.168.4.1/key",
             s_setup_ssid);

    /* Wait for the key to be accepted, then restart into normal mode. The
     * delay lets the customer read the confirmation before the AP drops. */
    while (!s_restart_pending) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    ESP_LOGI(TAG, "setup complete, restarting");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}

/*
 * Fetch and display real values.
 *
 * Spec 7.3 calls for a full recompute every 10 minutes plus incremental event
 * polling in between; this implements the full recompute only. Incremental
 * updates, reconciliation, backoff and the stale screen land with the full
 * polling layer.
 */
#define REFRESH_INTERVAL_MS (10 * 60 * 1000)
#define EVENTS_INTERVAL_MS  (60 * 1000)

/*
 * US Eastern. Spec 7.4 step 4: "today" must start at LOCAL midnight, or the
 * New Paid screen rolls over mid-evening and reads zero for hours.
 *
 * POSIX TZ format, and note the sign convention is inverted from what you
 * would expect -- EST5EDT means 5 hours WEST of UTC. The M3.2.0/M11.1.0 tail
 * encodes US daylight saving (second Sunday in March to first Sunday in
 * November), so the offset follows DST without firmware changes.
 */
#define DEVICE_TZ "EST5EDT,M3.2.0/2,M11.1.0/2"

static freshness_t s_freshness;
static bool s_time_synced = false;

/*
 * Local UTC offset right now, accounting for daylight saving.
 *
 * Derived by differencing local and UTC breakdowns of the same instant.
 * ESP-IDF's newlib does not provide the BSD tm_gmtoff extension, and this
 * also stays correct across DST transitions without a table.
 */
static int32_t current_utc_offset(void)
{
    const time_t now = time(NULL);

    struct tm local = {0};
    struct tm utc = {0};
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);

    int32_t diff = (int32_t)((local.tm_hour - utc.tm_hour) * 3600 +
                             (local.tm_min - utc.tm_min) * 60);

    /* Correct for the two calendars landing on different days. */
    const int day_delta = local.tm_yday - utc.tm_yday;
    if (day_delta == 1 || day_delta < -1) {
        diff += 86400;      /* local is a day ahead (incl. year wrap) */
    } else if (day_delta == -1 || day_delta > 1) {
        diff -= 86400;      /* local is a day behind */
    }

    return diff;
}

static void sync_time(void)
{
    setenv("TZ", DEVICE_TZ, 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    if (esp_netif_sntp_init(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP init failed; \"today\" may be wrong");
        return;
    }

    /* Without a correct clock, created[gte] is meaningless and the daily
     * figures would be computed against an arbitrary epoch. Worth waiting
     * for, but not worth blocking forever. */
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync timed out; daily figures deferred");
        return;
    }

    s_time_synced = true;

    const time_t now = time(NULL);
    struct tm local = {0};
    localtime_r(&now, &local);
    ESP_LOGI(TAG, "time synced: %04d-%02d-%02d %02d:%02d local (UTC%+d)",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min, (int)(current_utc_offset() / 3600));
}

static void refresh_task(void *arg)
{
    (void)arg;

    vTaskDelay(pdMS_TO_TICKS(3000));
    sync_time();

    int64_t next_full_ms = 0;

    while (1) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        bool any_success = false;

        /* Full recompute: MRR comes only from real price objects, never from
         * event payloads (see events.h for why). */
        if (now_ms >= next_full_ms) {
            mrr_totals_t totals = {0};
            bool truncated = false;
            const stripe_result_t r = stripe_fetch_totals(&totals, &truncated);

            if (r == STRIPE_OK) {
                lvgl_port_lock(0);
                apply_totals(&totals, truncated);
                lvgl_port_unlock();
                any_success = true;
                next_full_ms = now_ms + REFRESH_INTERVAL_MS;
            } else {
                ESP_LOGW(TAG, "full refresh failed: %s", stripe_result_str(r));
            }
        }

        /* Events drive the daily figures and the heartbeat. Only meaningful
         * once the clock is right (spec 7.4 step 4). */
        if (s_time_synced) {
            const int64_t day_start =
                local_day_start_utc(time(NULL), current_utc_offset());

            /*
             * Ask Stripe for a week, but count only today.
             *
             * The daily figures reset at local midnight (spec 7.4 step 4),
             * while the Last Event heartbeat needs something to show on a
             * quiet day -- spec 6.1 wants it confirming the device is live
             * without a status indicator, and a today-only window leaves it
             * blank most mornings.
             */
            const int64_t lookback = day_start - (7 * 86400);

            event_totals_t ev = {0};
            if (stripe_fetch_events_since(lookback, day_start, &ev) == STRIPE_OK) {
                lvgl_port_lock(0);
                apply_events(&ev);
                lvgl_port_unlock();
                any_success = true;
            }
        }

        if (any_success) {
            freshness_mark_success(&s_freshness, now_ms);
            show_current("refresh");
        } else {
            freshness_mark_failure(&s_freshness);
        }

        /* Back off after failures, otherwise poll events on their interval
         * (spec 7.3, 7.4 step 3). */
        const int64_t backoff = freshness_retry_delay_ms(&s_freshness);
        vTaskDelay(pdMS_TO_TICKS(backoff > 0 ? backoff : EVENTS_INTERVAL_MS));
    }
}

static void run_normal_mode(void)
{
    char ssid[WIFI_SSID_MAX_LEN + 1] = "";
    char pass[WIFI_PASS_MAX_LEN + 1] = "";

    if (settings_get_wifi(ssid, sizeof(ssid), pass, sizeof(pass)) != ESP_OK) {
        ESP_LOGW(TAG, "stored credentials unreadable; entering setup");
        run_setup_mode();
        return;
    }

    ESP_ERROR_CHECK(wifi_init());
    ESP_ERROR_CHECK(wifi_start_sta(ssid, pass));

    set_placeholders();
    show_current("boot");

    const esp_timer_create_args_t timer_args = {
        .callback = rotate_cb,
        .name = "rotation",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_rotation_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_rotation_timer,
                                             ROTATION_INTERVAL_MS * 1000));

    ESP_LOGI(TAG, "rotating %d screens every %dms",
             ROTATION_COUNT, ROTATION_INTERVAL_MS);

    /* Tap-to-advance. A failure here is not fatal -- the device still
     * rotates on its own. */
    if (imu_init() == ESP_OK) {
        ESP_ERROR_CHECK(imu_start_tap_watch(on_double_tap));
    } else {
        ESP_LOGW(TAG, "IMU unavailable; rotation is timer-only");
    }

    /* Load the stored key and start fetching real data. */
    char key[STRIPE_KEY_MAX_LEN + 1] = "";
    if (settings_get_stripe_key(key, sizeof(key)) == ESP_OK) {
        stripe_set_key(key);
        /* 8KB: this task runs the same TLS handshake that overflowed the
         * httpd task at 4KB. */
        xTaskCreate(refresh_task, "refresh", 8192, NULL, 4, NULL);
    } else {
        ESP_LOGW(TAG, "no Stripe key stored; screens stay blank");
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(settings_init());

#if COLORTEST_ENABLED
    lvgl_port_lock(0);
    colortest_draw();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "color test rendered");
    return;
#endif

    /* Setup runs in two phases (spec 9.1): WiFi first, then the Stripe key,
     * which can only be validated once there is a network to validate over. */
    if (!settings_have_wifi()) {
        run_setup_mode();
        return;
    }

    if (!settings_have_stripe_key()) {
        run_key_setup_mode();
        return;
    }

    run_normal_mode();
}
