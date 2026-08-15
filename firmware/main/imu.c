#include "imu.h"
#include "tapdetect.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";

/* Bus shared with the audio codecs; the IMU is one device on it. */
#define IMU_I2C_PORT I2C_NUM_0
#define IMU_SCL      GPIO_NUM_41
#define IMU_SDA      GPIO_NUM_42

/* Probed on hardware: 0x6A does not respond with a valid ID, 0x6B does. */
#define QMI_ADDR     0x6B
#define QMI_WHO_AM_I 0x05

#define REG_WHO_AM_I 0x00
#define REG_REVISION 0x01
#define REG_CTRL1    0x02
#define REG_CTRL2    0x03
#define REG_CTRL7    0x08
#define REG_AX_L     0x35

/* Poll fast enough to catch the short spike a tap produces. A tap lasts only
 * a few tens of milliseconds, so 100Hz would miss most of them. */
#define POLL_INTERVAL_MS 10

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static void (*s_on_tap)(void) = NULL;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t out[2] = {reg, val};
    return i2c_master_transmit(s_dev, out, sizeof(out), 200);
}

esp_err_t imu_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = IMU_I2C_PORT,
        .scl_io_num = IMU_SCL,
        .sda_io_num = IMU_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus failed");

    /* 100kHz rather than 400kHz. Tapping the case physically disturbs the bus,
     * and at 400kHz with only the internal pull-ups that showed up as frequent
     * read failures exactly when taps were happening -- which dropped the very
     * samples tap detection needs. */
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = QMI_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev),
                        TAG, "i2c device failed");

    uint8_t id = 0;
    ESP_RETURN_ON_ERROR(reg_read(REG_WHO_AM_I, &id, 1), TAG, "WHO_AM_I read failed");
    ESP_RETURN_ON_FALSE(id == QMI_WHO_AM_I, ESP_ERR_NOT_FOUND, TAG,
                        "unexpected WHO_AM_I 0x%02X at 0x%02X", id, QMI_ADDR);

    uint8_t rev = 0;
    reg_read(REG_REVISION, &rev, 1);

    /* CTRL1 0x40: auto-increment register address, so a 6-byte burst read of
     *             the accelerometer works.
     * CTRL2 0x24: accelerometer +/-8g at 500Hz.
     * CTRL7 0x01: enable accelerometer, leave the gyro off. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL1, 0x40), TAG, "CTRL1 failed");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL2, 0x24), TAG, "CTRL2 failed");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, 0x01), TAG, "CTRL7 failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    ESP_LOGI(TAG, "QMI8658 ready at 0x%02X (rev 0x%02X)", QMI_ADDR, rev);
    return ESP_OK;
}

esp_err_t imu_read_accel(int16_t *x, int16_t *y, int16_t *z)
{
    uint8_t raw[6] = {0};

    /* Deliberately not ESP_RETURN_ON_ERROR: a dropped sample is expected
     * occasionally (see the bus speed note in imu_init) and logging each one
     * at error level floods the console during exactly the activity we are
     * trying to observe. The caller skips the sample instead. */
    const esp_err_t err = reg_read(REG_AX_L, raw, sizeof(raw));
    if (err != ESP_OK) {
        return err;
    }

    *x = (int16_t)((raw[1] << 8) | raw[0]);
    *y = (int16_t)((raw[3] << 8) | raw[2]);
    *z = (int16_t)((raw[5] << 8) | raw[4]);
    return ESP_OK;
}

static void tap_task(void *arg)
{
    (void)arg;

    tap_detector_t det;
    tap_detector_init(&det);

    uint32_t reads = 0, drops = 0;
    int32_t peak_mg = 0;

    while (1) {
        int16_t x = 0, y = 0, z = 0;
        reads++;

        if (imu_read_accel(&x, &y, &z) == ESP_OK) {
            const int32_t mg = accel_magnitude_mg(x, y, z, IMU_LSB_PER_G);
            const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

            /* Track the strongest impact seen, so thresholds can be tuned
             * against what this device and case actually produce. */
            if (mg > peak_mg) {
                peak_mg = mg;
            }

            if (tap_detector_feed(&det, mg, now) && s_on_tap) {
                ESP_LOGI(TAG, "tap (%ld mg)", (long)mg);
                s_on_tap();
            }
        } else {
            drops++;
        }

        /* Periodic health line rather than one log per dropped read. */
        if (reads % 1000 == 0) {
            ESP_LOGI(TAG, "reads=%lu drops=%lu (%lu%%) peak=%ld mg",
                     (unsigned long)reads, (unsigned long)drops,
                     (unsigned long)(drops * 100 / reads), (long)peak_mg);
            peak_mg = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t imu_start_tap_watch(void (*on_tap)(void))
{
    s_on_tap = on_tap;

    const BaseType_t ok = xTaskCreate(tap_task, "tap", 3072, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tap task failed");

    ESP_LOGI(TAG, "watching for taps (threshold %d mg)", TAP_THRESHOLD_MG);
    return ESP_OK;
}
