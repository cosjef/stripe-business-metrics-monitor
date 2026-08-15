/*
 * QMI8658 accelerometer, used for double-tap screen navigation.
 *
 * Verified on hardware: the chip answers at I2C 0x6B (not 0x6A), WHO_AM_I
 * 0x05, revision 0x7C, on the bus wired to GPIO41/GPIO42.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

/* Sensitivity for the +/-8g range we configure. */
#define IMU_LSB_PER_G 4096

/*
 * Bring up the I2C bus and the accelerometer. Gyro stays off -- it costs
 * power and tap detection does not need it.
 */
esp_err_t imu_init(void);

/* Read raw accelerometer counts. */
esp_err_t imu_read_accel(int16_t *x, int16_t *y, int16_t *z);

/*
 * Start a background task that polls the accelerometer and invokes `on_tap`
 * when a double tap is detected. The callback runs on that task, not in an
 * ISR, so it may take locks and draw to the screen.
 */
esp_err_t imu_start_tap_watch(void (*on_tap)(void));
