/*
 * Display bring-up: SPI bus, ST7789 panel, and LVGL port.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize the SPI bus, ST7789 panel, backlight, and LVGL port.
 * Must be called once before any drawing.
 */
esp_err_t display_init(void);

/*
 * LVGL lock, forwarded to Waveshare's BSP.
 *
 * Their headers are C++ and cannot be included from main.c, so the lock is
 * re-exported here. Pass -1 to wait indefinitely; the adapter treats 0 as
 * "do not wait", which differs from esp_lvgl_port and silently dropped every
 * draw when we first switched.
 */
_Bool display_lock(int timeout_ms);
void display_unlock(void);

/*
 * The LVGL display handle, valid after a successful display_init().
 */
lv_display_t *display_handle(void);



/*
 * Set backlight on or off. PWM dimming (spec 3.3) is Stage 8 work; this is a
 * plain on/off for bring-up.
 */
void display_backlight(_Bool on);

#ifdef __cplusplus
}
#endif
