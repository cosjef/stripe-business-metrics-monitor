/*
 * AXP2101 power management. See axp2101.h for why this must run before the
 * display comes up.
 */
#include "axp2101.h"

#include "axp2101_reg.h"
#include "board_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "axp2101";

static i2c_master_bus_handle_t s_bus = NULL;
static i2c_master_dev_handle_t s_pmic = NULL;

/* Battery voltage, 14-bit, in the ADC data registers. */
#define AXP_REG_ADC_DATA_H  (0x34)
#define AXP_REG_CHARGE_STAT (0x01)

static esp_err_t reg_read(uint8_t reg, uint8_t *out, size_t len)
{
    return i2c_master_transmit_receive(s_pmic, &reg, 1, out, len,
                                       pdMS_TO_TICKS(100));
}

static esp_err_t reg_write(uint8_t reg, uint8_t val)
{
    const uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_pmic, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

/*
 * Set one ALDO rail's voltage and enable it.
 *
 * Voltage first, then enable: bringing a rail up at whatever value it happened
 * to hold and correcting afterwards would briefly present the wrong voltage to
 * the panel.
 */
static esp_err_t aldo_set(int n, int millivolts)
{
    const int step = axp_aldo_encode(millivolts);
    const int vreg = axp_aldo_vol_reg(n);
    const int bit = axp_aldo_enable_bit(n);

    if (step < 0 || vreg < 0 || bit == 0) {
        ESP_LOGE(TAG, "ALDO%d at %dmV is not a valid request", n, millivolts);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(reg_write((uint8_t)vreg, (uint8_t)step),
                        TAG, "ALDO%d voltage write failed", n);

    uint8_t onoff = 0;
    ESP_RETURN_ON_ERROR(reg_read(AXP_REG_LDO_ONOFF_CTRL0, &onoff, 1),
                        TAG, "read of enable register failed");

    /* Read-modify-write: the other rails' bits live in this same register and
     * must not be disturbed. */
    onoff |= (uint8_t)bit;
    ESP_RETURN_ON_ERROR(reg_write(AXP_REG_LDO_ONOFF_CTRL0, onoff),
                        TAG, "ALDO%d enable failed", n);

    return ESP_OK;
}

/* Set or clear one ALDO's enable bit without disturbing the others. */
static esp_err_t aldo_enable(int n, bool on)
{
    const int bit = axp_aldo_enable_bit(n);
    if (bit == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t onoff = 0;
    ESP_RETURN_ON_ERROR(reg_read(AXP_REG_LDO_ONOFF_CTRL0, &onoff, 1),
                        TAG, "enable register read failed");

    if (on) {
        onoff |= (uint8_t)bit;
    } else {
        onoff &= (uint8_t)~bit;
    }

    return reg_write(AXP_REG_LDO_ONOFF_CTRL0, onoff);
}

esp_err_t axp2101_pulse_panel_reset(void)
{
    if (!s_pmic) {
        return ESP_ERR_INVALID_STATE;
    }

    /* on -> off -> on, 100ms holds. The panel needs the low period long
     * enough to register as a reset rather than a glitch. */
    ESP_RETURN_ON_ERROR(aldo_enable(AXP_PANEL_RESET_ALDO, true),
                        TAG, "panel reset: rail on failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(aldo_enable(AXP_PANEL_RESET_ALDO, false),
                        TAG, "panel reset: rail off failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_RETURN_ON_ERROR(aldo_enable(AXP_PANEL_RESET_ALDO, true),
                        TAG, "panel reset: rail back on failed");
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "panel reset pulsed on ALDO%d", AXP_PANEL_RESET_ALDO);
    return ESP_OK;
}

esp_err_t axp2101_init(void)
{
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BSP_I2C_NUM,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus),
                        TAG, "I2C bus init failed");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_I2C_ADDR,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_pmic),
                        TAG, "PMIC not found at 0x%02X", AXP2101_I2C_ADDR);

    /*
     * All four ALDO rails to 3.3V, matching Waveshare's power_bsp.cpp. Which
     * specific rail feeds the panel is not documented, so all four are set --
     * the same thing their firmware does, and the board demonstrably works
     * that way.
     */
    for (int n = 1; n <= 4; n++) {
        const esp_err_t err = aldo_set(n, AXP_BOARD_ALDO_MV);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ALDO%d setup failed (%s)", n, esp_err_to_name(err));
        }
    }

    /*
     * Now reset the panel by cycling its rail. This must happen before the
     * display driver sends its first command, which is why it lives here
     * rather than in display.c.
     */
    /*
     * No ALDO3 pulse.
     *
     * Clawdmeter cycles ALDO3 as a panel reset, and I copied that. Waveshare's
     * own working example for this board does not: it sets ALDO3 to 3.3V,
     * enables it, and leaves it alone. Their demo drives the panel correctly,
     * so the pulse is either unnecessary here or actively harmful -- cycling
     * the rail after the panel has powered up can leave it in an
     * indeterminate state, which is what a dark screen with successful init
     * looks like.
     */

    ESP_LOGI(TAG, "PMIC up at 0x%02X, ALDO1-4 at %dmV",
             AXP2101_I2C_ADDR, AXP_BOARD_ALDO_MV);
    return ESP_OK;
}

int axp2101_battery_mv(void)
{
    if (!s_pmic) {
        return 0;
    }

    uint8_t d[2] = {0};
    if (reg_read(AXP_REG_ADC_DATA_H, d, 2) != ESP_OK) {
        return 0;
    }

    /* 14-bit value across two registers. */
    return ((int)(d[0] & 0x3F) << 8) | d[1];
}

bool axp2101_is_charging(void)
{
    if (!s_pmic) {
        return false;
    }

    uint8_t st = 0;
    if (reg_read(AXP_REG_CHARGE_STAT, &st, 1) != ESP_OK) {
        return false;
    }

    /* Bit 5 of the status register indicates an active charge. */
    return (st & (1 << 5)) != 0;
}
