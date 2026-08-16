/*
 * Front-panel button input.
 *
 * The board has three buttons. Waveshare's own example labels them
 * Left = BOOT (GPIO0), Middle = PWR (GPIO5), Right = PLUS (GPIO4); this uses
 * the leftmost to advance the rotation.
 *
 * WHY A BUTTON AND NOT THE IMU. Tap-to-advance was built first and removed:
 * this board's I2C bus intermittently returns corrupt reads that decode as
 * large single-sample accelerations, a real tap also lands in exactly one
 * 20ms sample, and the bus cannot be polled faster without tripping the task
 * watchdog. Every filter that removed the false triggers also rejected real
 * taps -- 8 phantom advances in 60 seconds while the device sat untouched. A
 * debounced digital input has none of those failure modes.
 *
 * NOTE ON GPIO0. It doubles as the download-mode strapping pin. That is
 * harmless once the firmware is running, but holding it through a reset puts
 * the board into flash mode rather than advancing a screen.
 */
#pragma once

#include "esp_err.h"

/* Invoked on each press. Runs on the button component's task, not an ISR, so
 * it may take locks and draw. */
typedef void (*button_press_cb_t)(void);

/* Start watching the leftmost button. */
esp_err_t button_start(button_press_cb_t on_press);
