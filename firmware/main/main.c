/*
 * Stripe Revenue Display -- Stage 2.
 *
 * Rotates through the six metric screens (spec 6.1) on an 8-second timer,
 * driven by hardcoded fixture data. No networking yet; Stage 4 replaces the
 * fixtures with live Stripe values.
 *
 * All drawing lives in screens.c, which depends only on LVGL and is covered by
 * pixel tests in the host harness (firmware/test/test_screens.c). This file
 * owns hardware bring-up and the rotation timer only.
 *
 * Known Stage 2 limitations, tracked in firmware-build-plan.md:
 *   - No partial-window updates (spec 5.3); LVGL redraws as it sees fit.
 *   - Fixture data, so no stale/auth states are reachable at runtime yet --
 *     those screens exist and are tested, but nothing triggers them.
 */
#include "display.h"
#include "board_config.h"
#include "layout.h"
#include "hero_size.h"
#include "screens.h"
#include "colortest.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"

static const char *TAG = "main";

/* Set to 1 to draw the color diagnostic instead of the rotation. */
#define COLORTEST_ENABLED 0

/*
 * Fixture values, matching the spec's screen deck (6.1) and its mockup images.
 * Replaced by live data in Stage 4.
 *
 * Note the churn screen from 6.1 is deliberately absent: it enters rotation
 * only when nonzero, so it needs real data to make sense.
 */
static const screen_data_t s_rotation[] = {
    {
        .label = "MRR", .hero = "$6.5k", .subtitle = "+$118 today",
        .hero_is_gain = 0, .subtitle_is_gain = 1,
    },
    {
        /* A realized gain, so the hero is green (spec 4.2). */
        .label = "NEW PAID", .hero = "2", .subtitle = "today, $58 MRR",
        .hero_is_gain = 1, .subtitle_is_gain = 0,
    },
    {
        .label = "PAID SUBS", .hero = "94", .subtitle = "+7 this month",
        .hero_is_gain = 0, .subtitle_is_gain = 1,
    },
    {
        /* Not green: a trial is not yet revenue (spec 6.1). */
        .label = "TRIALS", .hero = "11", .subtitle = "3 end this week",
        .hero_is_gain = 0, .subtitle_is_gain = 0,
    },
    {
        .label = "CONVERSION", .hero = "34%", .subtitle = "trial to paid",
        .hero_is_gain = 0, .subtitle_is_gain = 0,
    },
    {
        .label = "LAST EVENT", .hero = "+$29", .subtitle = "new paid, 2m",
        .hero_is_gain = 1, .subtitle_is_gain = 0,
    },
};

#define ROTATION_COUNT ((int)(sizeof(s_rotation) / sizeof(s_rotation[0])))

static int s_index = 0;

static void show_current(void)
{
    screen_data_t d = s_rotation[s_index];
    d.dot_index = s_index;
    d.dot_count = ROTATION_COUNT;

    lvgl_port_lock(0);
    screen_draw_rotation(lv_screen_active(), &d);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "[%d/%d] %s: '%s' at %dpx",
             s_index + 1, ROTATION_COUNT, d.label, d.hero,
             hero_size_for_text(d.hero));
}

static void rotate_cb(void *arg)
{
    (void)arg;
    s_index = (s_index + 1) % ROTATION_COUNT;
    show_current();
}

void app_main(void)
{
    ESP_ERROR_CHECK(display_init());

#if COLORTEST_ENABLED
    lvgl_port_lock(0);
    colortest_draw();
    lvgl_port_unlock();
    ESP_LOGI(TAG, "color test rendered");
    return;
#endif

    show_current();

    const esp_timer_create_args_t timer_args = {
        .callback = rotate_cb,
        .name = "rotation",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, ROTATION_INTERVAL_MS * 1000));

    ESP_LOGI(TAG, "rotating %d screens every %dms",
             ROTATION_COUNT, ROTATION_INTERVAL_MS);
}
