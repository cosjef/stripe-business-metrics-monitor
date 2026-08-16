/*
 * Battery ADC. The hardware half of battery.h; the logic is in battery.c.
 *
 * GPIO1 on this board is the battery sense pin, fed through a 200K/100K
 * divider. On the ESP32-S3 that is ADC1 channel 0.
 */
#include "battery.h"

#include "driver/gpio.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define BATTERY_ADC_UNIT     ADC_UNIT_1
#define BATTERY_ADC_CHANNEL  ADC_CHANNEL_0   /* GPIO1 */

/*
 * 12dB attenuation reads up to ~3.1V at the pin. The divider puts a full 4.2V
 * cell at 1.4V, so this covers the range with headroom rather than clipping
 * near full charge.
 */
#define BATTERY_ADC_ATTEN    ADC_ATTEN_DB_12

/* A single S3 ADC reading wanders tens of millivolts -- the same magnitude as
 * the hysteresis band it feeds. Average enough samples to settle it. */
#define BATTERY_SAMPLES      16

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_calibrated = false;
static battery_level_t s_level = BATTERY_UNKNOWN;

int battery_start(void)
{
    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
    };
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC unit init failed (%s); battery unavailable",
                 esp_err_to_name(err));
        s_adc = NULL;
        return err;
    }

    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ADC channel config failed (%s)", esp_err_to_name(err));
        return err;
    }

    /*
     * Curve fitting uses the factory calibration burned into eFuse. Without it
     * the raw-to-millivolt conversion is off by enough to matter at these
     * thresholds, so if calibration is unavailable we report unknown rather
     * than guess at a voltage.
     */
    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATTERY_ADC_UNIT,
        .chan = BATTERY_ADC_CHANNEL,
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    s_calibrated = (err == ESP_OK);

    if (!s_calibrated) {
        ESP_LOGW(TAG, "ADC calibration unavailable (%s); "
                 "battery level will report unknown", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "battery sensing on GPIO1 (ADC1 ch0), 200K/100K divider");
    }

    return ESP_OK;
}

int battery_read_mv(void)
{
    if (!s_adc || !s_calibrated) {
        return 0;
    }

    int total = 0;
    int taken = 0;

    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
            continue;
        }

        int mv = 0;
        if (adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
            continue;
        }

        total += mv;
        taken++;
    }

    if (taken == 0) {
        return 0;
    }

    return battery_cell_mv(total / taken);
}

/*
 * Whether USB is supplying power.
 *
 * This board exposes no VBUS sense line to a GPIO, so it is inferred from the
 * cell voltage: while charging, the charger holds the rail at its constant-
 * voltage setpoint, above where a resting cell sits. A cell only reads this
 * high with the charger actively driving it.
 *
 * Inference, not measurement. It cannot distinguish USB power from a cell that
 * has just come off the charger, so a freshly unplugged device may report
 * charging for a few minutes until the surface charge settles. That direction
 * is the safe one: it suppresses a warning rather than raising a false one.
 */
static bool usb_present(int cell_mv)
{
    return cell_mv >= 4150;
}

battery_level_t battery_current_level(void)
{
    const int mv = battery_read_mv();

    s_level = battery_level(mv, usb_present(mv), s_level);
    return s_level;
}
