/*
 * Display bring-up: SPI bus, ST7789 panel, and LVGL port.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

/*
 * Initialize the SPI bus, ST7789 panel, backlight, and LVGL port.
 * Must be called once before any drawing.
 */
esp_err_t display_init(void);

/*
 * The LVGL display handle, valid after a successful display_init().
 */
lv_display_t *display_handle(void);


/*
 * Set backlight on or off. PWM dimming (spec 3.3) is Stage 8 work; this is a
 * plain on/off for bring-up.
 */
void display_backlight(_Bool on);
