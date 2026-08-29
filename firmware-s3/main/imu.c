/*
 * QMI8658 IMU: tap-to-advance navigation.
 *
 * Taps are detected HOST-SIDE from raw accelerometer magnitude (tapdetect.c),
 * not by the chip's own tap engine. The engine is still configured below
 * because that configuration is correct and was verified register-by-register,
 * but it never once fired on this part (WHO_AM_I 0x05, revision 0x7C) across
 * many counted tests, for impacts up to 8604 mg. The cause was never found.
 * See tapdetect.h for the detection trade-off that ships.
 */
#include "imu.h"
#include "tapstatus.h"
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

#define REG_WHO_AM_I   0x00
#define REG_REVISION   0x01
#define REG_CTRL1      0x02
#define REG_CTRL2      0x03
#define REG_CTRL7      0x08
#define REG_CTRL8      0x09
#define REG_CTRL9      0x0A
#define REG_CAL1_L     0x0B
#define REG_CAL1_H     0x0C
#define REG_CAL2_L     0x0D
#define REG_CAL2_H     0x0E
#define REG_CAL3_L     0x0F
#define REG_CAL3_H     0x10
#define REG_CAL4_L     0x11
#define REG_CAL4_H     0x12
#define REG_STATUSINT  0x2D
#define REG_STATUS0    0x2E  /* data-ready flags only -- NOT tap */
#define REG_STATUS1    0x2F  /* bit1 = tap; CLEARS ON READ */
#define REG_AX_L       0x35
#define REG_TAP_STATUS 0x59

/* CTRL9 command interface */
#define CTRL9_CMD_ACK           0x00
#define CTRL9_CMD_CONFIGURE_TAP 0x0C

/* STATUS1 bits */
#define STATUS1_TAP 0x02

/*
 * Tap engine parameters, from the QMI8658 datasheet's worked example for a
 * firm finger tap, with Z normal to the display face.
 *
 * Window values are in SAMPLES, so they only hold at the 500Hz ODR set in
 * CTRL2. Changing the ODR means rescaling these.
 */
/*
 * Axis priority (0x04 = Z > X > Y).
 *
 * This does NOT gate which axes are watched, and changing it does not affect
 * sensitivity. The peak detector triggers on the SQUARE SUM of all three axes
 * (datasheet 10.x: "If the square sum of the Linear Acceleration of three axes
 * is higher than the PeakMagThr, the peak detecting is started"). Priority is
 * only a tie-break, used to label TAP_AXIS when two or three axes peak at
 * exactly the same sample.
 *
 * Recorded because an earlier version of this comment claimed the opposite and
 * sent debugging down a dead end: impacts were being lost to a bad TAP_STATUS
 * mask (see tapstatus.h), not to the priority setting.
 */
#define TAP_PRIORITY     0x04    /* Z > X > Y (tie-break only) */
/*
 * Windows, in SAMPLES at the 500Hz ODR set in CTRL2.
 *
 * Widened from the datasheet example (20/50/250). At 500Hz those are 40ms/
 * 100ms/500ms, which assumes a sharp impulse reaching a bare sensor. Through a
 * plastic case the impulse is spread out and damped, so a short peak window
 * can reject it before the shape qualifies.
 */
#define TAP_PEAK_WINDOW  50    /* 100ms */
#define TAP_TAP_WINDOW   150   /* 300ms */
#define TAP_DTAP_WINDOW  400   /* 800ms */
#define TAP_ALPHA        0x08    /* 0.0625, 7-bit fraction */
#define TAP_GAMMA        0x20    /* 0.25,   7-bit fraction */

/*
 * Magnitude thresholds for the chip's tap engine, in g^2 with a 10-bit
 * fraction. These have no effect on shipped behavior -- the engine never
 * fires -- and are retained only so the configuration stays complete if
 * someone revisits it.
 */
#define TAP_PEAK_MAG_THR 0x0190  /* 0.4 g^2 */
#define TAP_UDM_THR      0x00C8  /* 0.2 g^2 */

/*
 * Poll interval.
 *
 * Faster sampling catches the peak of a tap impulse more reliably, but this
 * bus is shared with the audio codecs and is evidently marginal: at 5-8ms the
 * NACK rate climbed sharply and the task starved CPU 0's idle task badly
 * enough to trip the task watchdog. 20ms is well within FreeRTOS's 10ms tick
 * so the task yields cleanly. The detection threshold is calibrated against
 * what the host observes at this rate, which is the impulse TAIL rather than
 * its peak.
 */
#define POLL_INTERVAL_MS 20

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_dev = NULL;
static void (*s_on_tap)(void) = NULL;

static esp_err_t reg_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 200);
}

/* Read with retries, for configuration paths where a single NACK must not
 * abort init. The hot polling path uses reg_read directly and simply skips a
 * failed sample. */
static esp_err_t reg_read_retry(uint8_t reg, uint8_t *buf, size_t len)
{
    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < 5; attempt++) {
        err = reg_read(reg, buf, len);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return err;
}

/*
 * Write a register, retrying on failure.
 *
 * This bus is shared with the audio codecs and NACKs intermittently. A single
 * failed write during tap configuration aborted init entirely and left
 * navigation dead, so every configuration write retries.
 */
static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t out[2] = {reg, val};
    esp_err_t err = ESP_FAIL;

    for (int attempt = 0; attempt < 5; attempt++) {
        err = i2c_master_transmit(s_dev, out, sizeof(out), 200);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    return err;
}

/*
 * Issue a CTRL9 command and wait for the chip to acknowledge it.
 *
 * The protocol is: write the command to CTRL9, poll STATUSINT bit 7 (CmdDone),
 * then write CTRL_CMD_ACK to clear it.
 */
static esp_err_t ctrl9_command(uint8_t cmd)
{
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL9, cmd), TAG, "CTRL9 write failed");

    for (int i = 0; i < 100; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        uint8_t st = 0;
        if (reg_read_retry(REG_STATUSINT, &st, 1) == ESP_OK && (st & 0x80)) {
            reg_write(REG_CTRL9, CTRL9_CMD_ACK);
            return ESP_OK;
        }
    }

    ESP_LOGE(TAG, "CTRL9 command 0x%02X NOT acknowledged after 500ms", cmd);
    return ESP_ERR_TIMEOUT;
}

/*
 * Configure the on-chip tap engine.
 *
 * Two things here are easy to get wrong and both cost a debugging cycle:
 *
 *  - The parameters do not fit one command. They are written as TWO CTRL9
 *    0x0C passes, distinguished by CAL4_H being 0x01 then 0x02.
 *  - The accelerometer must be DISABLED during configuration. The datasheet
 *    requires CTRL7.aEN = CTRL7.gEN = 0 while configuring.
 */
static esp_err_t configure_tap_engine(void)
{
    /* Accel and gyro off while configuring. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, 0x00), TAG, "CTRL7 disable failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    /* First parameter set: windows and axis priority. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL1_L, TAP_PEAK_WINDOW), TAG, "CAL1_L");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL1_H, TAP_PRIORITY), TAG, "CAL1_H");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL2_L, TAP_TAP_WINDOW & 0xFF), TAG, "CAL2_L");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL2_H, TAP_TAP_WINDOW >> 8), TAG, "CAL2_H");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL3_L, TAP_DTAP_WINDOW & 0xFF), TAG, "CAL3_L");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL3_H, TAP_DTAP_WINDOW >> 8), TAG, "CAL3_H");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL4_L, 0x00), TAG, "CAL4_L");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL4_H, 0x01), TAG, "CAL4_H set1");
    ESP_RETURN_ON_ERROR(ctrl9_command(CTRL9_CMD_CONFIGURE_TAP), TAG, "tap config set 1");

    /* Second parameter set: filter shape and magnitude thresholds. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL1_L, TAP_ALPHA), TAG, "CAL1_L b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL1_H, TAP_GAMMA), TAG, "CAL1_H b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL2_L, TAP_PEAK_MAG_THR & 0xFF), TAG, "CAL2_L b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL2_H, TAP_PEAK_MAG_THR >> 8), TAG, "CAL2_H b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL3_L, TAP_UDM_THR & 0xFF), TAG, "CAL3_L b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL3_H, TAP_UDM_THR >> 8), TAG, "CAL3_H b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL4_L, 0x00), TAG, "CAL4_L b");
    ESP_RETURN_ON_ERROR(reg_write(REG_CAL4_H, 0x02), TAG, "CAL4_H set2");
    ESP_RETURN_ON_ERROR(ctrl9_command(CTRL9_CMD_CONFIGURE_TAP), TAG, "tap config set 2");

    /* Accel back on, and give it time to start producing samples. The tap
     * engine works off the sample stream, so enabling tap before the
     * accelerometer is actually running may leave it inert. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL7, 0x01), TAG, "CTRL7 enable failed");
    vTaskDelay(pdMS_TO_TICKS(200));

    /* Tap_EN (bit0) + poll-STATUSINT handshake (bit7), since we poll rather
     * than wire up an interrupt pin. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL8, 0x81), TAG, "CTRL8 failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    uint8_t c8 = 0;
    reg_read_retry(REG_CTRL8, &c8, 1);
    ESP_LOGI(TAG, "tap engine enabled (CTRL8=0x%02X)", c8);

    /* Enabling the engine leaves a spurious event latched, which would fire an
     * immediate phantom tap at boot (observed as axis=0 count=0). STATUS1
     * clears on read, so read it once to discard it. */
    vTaskDelay(pdMS_TO_TICKS(20));
    uint8_t discard = 0;
    reg_read(REG_STATUS1, &discard, 1);


    return ESP_OK;
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

    /* The QMI8658 needs a moment after power-up before it answers, and the bus
     * can still be settling when we get here right after display bring-up.
     * A single read at this point NACKs intermittently, which left the tap
     * task unstarted and navigation silently dead. Retry rather than give up
     * on the first failure. */
    uint8_t id = 0;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 10; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(20));
        err = reg_read(REG_WHO_AM_I, &id, 1);
        if (err == ESP_OK && id == QMI_WHO_AM_I) {
            break;
        }
    }

    ESP_RETURN_ON_ERROR(err, TAG, "WHO_AM_I read failed after retries");
    ESP_RETURN_ON_FALSE(id == QMI_WHO_AM_I, ESP_ERR_NOT_FOUND, TAG,
                        "unexpected WHO_AM_I 0x%02X at 0x%02X", id, QMI_ADDR);

    uint8_t rev = 0;
    reg_read(REG_REVISION, &rev, 1);

    /* CTRL1 0x40: auto-increment register address, so burst reads work.
     * CTRL2 0x24: accelerometer +/-8g at 500Hz. The tap engine needs an ODR
     *             above 200Hz, and the window parameters are in samples at
     *             this rate. */
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL1, 0x40), TAG, "CTRL1 failed");
    ESP_RETURN_ON_ERROR(reg_write(REG_CTRL2, 0x24), TAG, "CTRL2 failed");
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_RETURN_ON_ERROR(configure_tap_engine(), TAG, "tap engine config failed");

    /* Silence the I2C driver's own per-failure logging. This bus is shared and
     * marginal, so occasional NACKs are expected; at error level they flooded
     * the console and burned enough CPU to contribute to a watchdog trip. Our
     * periodic health line reports the drop rate instead. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

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

    const int16_t nx = (int16_t)((raw[1] << 8) | raw[0]);
    const int16_t ny = (int16_t)((raw[3] << 8) | raw[2]);
    const int16_t nz = (int16_t)((raw[5] << 8) | raw[4]);

    /*
     * Reject physically impossible samples.
     *
     * This bus is marginal, and some transactions return corrupt data that
     * still passes the I2C layer's own checks. Observed while the device sat
     * completely still: ax=ay=az=-3856 (three identical axes), and isolated
     * "impacts" of 5000-10560 mg between consecutive 1002 mg samples. Those
     * corrupt reads were being treated as taps, which is what made every
     * threshold I tried behave inconsistently.
     *
     * All three axes reading identically is the clearest signature -- real
     * acceleration essentially never produces that.
     */
    /*
     * Reject the clearest corruption signature only: all three axes reading
     * identically (observed as ax=ay=az=-3856 while the device sat still),
     * which real acceleration essentially never produces.
     *
     * Deliberately NOT filtering saturated values here. Doing so cut false
     * triggers to zero but also rejected 3 of 5 real taps, because a genuine
     * tap through the case reaches full scale on one axis. See the trade-off
     * note in tapdetect.h.
     */
    if (nx == ny && ny == nz && nx != 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    *x = nx;
    *y = ny;
    *z = nz;
    return ESP_OK;
}

/*
 * Detect taps from raw accelerometer magnitude.
 *
 * WHY NOT THE CHIP'S TAP ENGINE. The QMI8658 has an on-chip tap detector, and
 * it was implemented here first. It never fired -- zero detections across many
 * counted tests, for impacts up to 8604 mg. Everything observable was verified
 * correct: CTRL1/2/7/8 all read back as written, both CTRL9 CONFIGURE_TAP
 * commands ACKed within 20ms, every CAL parameter read back intact, the
 * accelerometer was enabled and producing data, sync-sample mode was off, and
 * widening the peak/tap windows changed nothing. The cause was never found on
 * this part (WHO_AM_I 0x05, revision 0x7C).
 *
 * Meanwhile the raw accelerometer reliably shows every tap: the diagnostic
 * captured all 12 impacts in one 20-second run, cleanly separated from the
 * noise floor. So this triggers on data we can actually see.
 *
 * The 20ms poll samples the impulse TAIL rather than its peak, which is why
 * the threshold is calibrated against observed magnitudes rather than against
 * what a tap "should" measure.
 */
static void tap_task(void *arg)
{
    (void)arg;

    uint32_t polls = 0, drops = 0, taps = 0;
    int32_t diag_peak = 0;

    tap_detector_t det;
    tap_detector_init(&det);


    while (1) {
        polls++;

        int16_t ax = 0, ay = 0, az = 0;
        if (imu_read_accel(&ax, &ay, &az) != ESP_OK) {
            drops++;
        } else {
            const int32_t mag_mg = accel_magnitude_mg(ax, ay, az, IMU_LSB_PER_G);
            const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

            if (mag_mg > diag_peak) {
                diag_peak = mag_mg;
            }

            /*
             * NOTE: an earlier version required the elevated reading to
             * persist across two consecutive samples, to reject corrupt I2C
             * reads. That rejected every real tap too: at a 20ms poll the tap
             * impulse lands in exactly ONE sample, so it can never be
             * confirmed by a second. Corruption is filtered in
             * imu_read_accel() by its signature instead.
             */
            if (tap_detector_feed(&det, mag_mg, now)) {
                taps++;
                ESP_LOGI(TAG, "tap (%ld mg)", (long)mag_mg);
                if (s_on_tap) {
                    s_on_tap();
                }
            }
        }

        if (polls % 500 == 0) {
            ESP_LOGI(TAG, "polls=%lu drops=%lu taps=%lu peak=%ld mg",
                     (unsigned long)polls, (unsigned long)drops,
                     (unsigned long)taps, (long)diag_peak);
            diag_peak = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t imu_start_tap_watch(void (*on_tap)(void))
{
    s_on_tap = on_tap;

    const BaseType_t ok = xTaskCreate(tap_task, "tap", 3072, NULL, 4, NULL);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "tap task failed");

    ESP_LOGI(TAG, "watching for taps (threshold %d mg, host-side)", TAP_THRESHOLD_MG);
    return ESP_OK;
}
