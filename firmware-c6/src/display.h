/*
 * Display bring-up and the LVGL binding.
 *
 * Call order matters and is not interchangeable:
 *   power_up()          -- PMIC rails, and the ALDO3 pulse that resets the panel
 *   display_init()      -- QSPI bus, CO5300, driving-voltage registers
 *   display_lvgl_init() -- lv_init, draw buffer, flush callback
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* QSPI bus, panel init, and the manufacturer driving-voltage registers.
 * Requires the PMIC rails to already be up. */
bool display_init(void);

/* lv_init(), the draw buffer, and the flush callback. After this, LVGL can
 * draw and lv_timer_handler() must be called regularly. */
bool display_lvgl_init(void);

/* Direct panel fill, bypassing LVGL. Used by the bring-up probe to prove the
 * glass before any LVGL object exists. */
void display_fill(uint16_t color);

/* Panel brightness, 0-255. This is a panel command, not PWM on a pin. */
void display_brightness(uint8_t level);
