#include "button.h"

#include "esp_check.h"
#include "esp_log.h"
#include "iot_button.h"
#include "button_gpio.h"


static const char *TAG = "button";

/* Leftmost of the three, per Waveshare's own example labelling. */
#define BUTTON_GPIO 0

/* Active low: the button pulls the pin to ground when pressed. */
#define BUTTON_ACTIVE_LEVEL 0

static button_press_cb_t s_on_press = NULL;
static button_handle_t s_btn = NULL;

static void on_single_click(void *arg, void *data)
{
    (void)arg;
    (void)data;

    ESP_LOGI(TAG, "press");

    if (s_on_press) {
        s_on_press();
    }
}

esp_err_t button_start(button_press_cb_t on_press)
{
    s_on_press = on_press;

    /* Defaults give ~20ms debounce and a 180ms short-press window, which is
     * appropriate for a physical tactile switch. */
    const button_config_t btn_cfg = {0};
    const button_gpio_config_t gpio_cfg = {
        .gpio_num = BUTTON_GPIO,
        .active_level = BUTTON_ACTIVE_LEVEL,
        /* Not enabling power-save wakeup: the display is always on, so there
         * is no sleep state for the button to wake from. */
        .enable_power_save = false,
    };

    ESP_RETURN_ON_ERROR(iot_button_new_gpio_device(&btn_cfg, &gpio_cfg, &s_btn),
                        TAG, "button create failed");

    /*
     * Single click rather than press-down: a click fires on release, which
     * means holding the button does not repeat, and a long hold (the gesture
     * for entering download mode) does not advance a screen on the way in.
     */
    ESP_RETURN_ON_ERROR(
        iot_button_register_cb(s_btn, BUTTON_SINGLE_CLICK, NULL,
                               on_single_click, NULL),
        TAG, "callback registration failed");

    ESP_LOGI(TAG, "watching GPIO%d for presses", BUTTON_GPIO);

    return ESP_OK;
}
