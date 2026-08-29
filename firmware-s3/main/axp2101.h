/*
 * AXP2101 power management, over I2C.
 *
 * MUST be initialised before display_init(). On this board the PMIC gates the
 * AMOLED's power rails: without it the panel initialises, reports success,
 * LVGL renders, and no pixel emits light. Every log line says success, and
 * there is no backlight to check -- a silent failure that cost most of a day.
 *
 * Waveshare's factory firmware boot log shows the required order:
 *
 *     Initialize I2C bus
 *     Initialize pmic power
 *     axp2101: Init PMU SUCCESS!
 *     Initialize SPI bus
 *
 * The register arithmetic lives in axp2101_reg.h and is host-tested; this file
 * is the hardware half.
 */
#pragma once

#include <stdbool.h>

#include "esp_err.h"

/*
 * Bring up the shared I2C bus and power the board's rails.
 *
 * Also used later by the touch controller, the IMU and the RTC, which all sit
 * on the same bus.
 */
esp_err_t axp2101_init(void);

/*
 * Pulse ALDO3 to reset the panel.
 *
 * On this board the CO5300's reset pin is not wired to an MCU GPIO -- it is
 * wired to the ALDO3 rail. So a panel reset means cycling that rail:
 * on, off, on, with 100ms holds.
 *
 * Without it the controller sits in an indeterminate state and the screen
 * stays black even though QSPI init, the vendor init sequence and the
 * brightness write all report success. That is exactly the failure we hit,
 * and it is why "no reset GPIO" was a misleading reading of the schematic:
 * there IS a reset, it is just a power rail rather than a pin.
 *
 * Called from axp2101_init(); exposed separately in case a panel recovery
 * path ever needs it.
 */
esp_err_t axp2101_pulse_panel_reset(void);

/* Battery voltage in millivolts, or 0 if unavailable. */
int axp2101_battery_mv(void);

/* Whether the charger is currently active. */
bool axp2101_is_charging(void);
