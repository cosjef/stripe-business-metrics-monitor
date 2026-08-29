/*
 * Display bring-up, delegating entirely to Waveshare's vendored BSP.
 *
 * Every attempt to reimplement their panel bring-up in our own code produced
 * a boot log identical to theirs -- same PMIC init, same driver version, same
 * reset pulse, same init table, same adapter -- and a dark screen. When two
 * builds log the same thing and behave differently, the difference is in
 * something the log does not show, and hunting it by reading their source and
 * translating it into our structure kept introducing new gaps.
 *
 * So this file does no bring-up of its own. It calls their code in their
 * order and exposes the handles our app needs. C++ because their BSP is C++;
 * the interface below stays C-callable for the rest of the firmware.
 */
#include "display.h"

#include "board_config.h"
#include "layout.h"
#include "orientation.h"

#include "display_bsp.h"
#include "i2c_bsp.h"
#include "lvgl_bsp.h"
#include "power_bsp.h"

#include "esp_log.h"

static const char *TAG = "display";

/*
 * Their I2C bus and display objects. Constructed in the order main.cpp uses
 * in their example: PMIC first (it gates the panel rails), then the display,
 * then touch, then LVGL.
 */
static I2cMasterBus *s_i2c = nullptr;
static DisplayPort *s_display = nullptr;

lv_display_t *display_handle(void)
{
    /*
     * Their BSP keeps the display handle private and exposes lock/unlock
     * instead. Callers here only ever use lv_screen_active() under the lock,
     * so nothing needs the handle itself.
     */
    return nullptr;
}

void display_backlight(bool on)
{
    if (s_display) {
        s_display->Set_Backlight(on ? 100 : 0);
    }
}

esp_err_t display_init(void)
{
    /* GPIO 7 = SCL, 8 = SDA, port 0 -- their constructor's argument order. */
    s_i2c = new I2cMasterBus(BSP_I2C_SCL, BSP_I2C_SDA, 0);

    /* PMIC before the display: it powers the panel rails. */
    Custom_PmicPortInit(s_i2c, AXP2101_I2C_ADDR);

    /* Their defaults already match this board's pins, including cs = 15. */
    s_display = new DisplayPort(*s_i2c, LCD_H_RES, LCD_V_RES);
    s_display->DisplayPort_TouchInit();

    Lvgl_PortInit(*s_display);

    ESP_LOGI(TAG, "display ready: %dx%d, panel %d, hero cap %dpx, column %dpx",
             LCD_H_RES, LCD_V_RES, PANEL_PX, SIZE_HERO_MAX, TEXT_COLUMN_PX);
    return ESP_OK;
}

bool display_lock(int timeout_ms)
{
    return Lvgl_lock(timeout_ms) == ESP_OK;
}

void display_unlock(void)
{
    Lvgl_unlock();
}
