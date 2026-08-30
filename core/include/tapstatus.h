/*
 * Decoding for the QMI8658 TAP_STATUS register (0x59).
 *
 * Split out from imu.c so it can be tested on the host: a bad mask here caused
 * genuine taps to be silently discarded, and nothing caught it because the
 * decode was buried in the I2C polling loop.
 *
 * Layout, per QMI8658A datasheet Table 26:
 *
 *   bit  7    TAP_POLARITY  0 = positive direction, 1 = negative
 *   bits 6:4  TAP_AXIS      0 = none, 1 = X, 2 = Y, 3 = Z
 *   bits 3:2  reserved
 *   bits 1:0  TAP_NUM       0 = none, 1 = single, 2 = double, 3 = N/A
 *
 * Note TAP_AXIS is THREE bits. Masking it with 0x03 truncates the field and
 * corrupts any value with bit 6 set, which then trips an "axis == 0 means
 * noise" filter and drops real taps.
 */
#pragma once

#include <stdint.h>

typedef enum {
    TAP_AXIS_NONE = 0,
    TAP_AXIS_X    = 1,
    TAP_AXIS_Y    = 2,
    TAP_AXIS_Z    = 3,
} tap_axis_t;

typedef enum {
    TAP_NUM_NONE   = 0,
    TAP_NUM_SINGLE = 1,
    TAP_NUM_DOUBLE = 2,
    TAP_NUM_NA     = 3,
} tap_num_t;

typedef struct {
    tap_axis_t axis;
    tap_num_t num;
    _Bool negative;   /* TAP_POLARITY */
} tap_status_t;

/* Decode a raw TAP_STATUS register value. */
tap_status_t tap_status_decode(uint8_t raw);

/*
 * True if the decoded status represents a tap worth acting on: a real axis and
 * a single or double tap. Anything else is a stale or spurious latch.
 */
_Bool tap_status_is_real(tap_status_t s);
