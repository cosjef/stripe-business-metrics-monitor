/*
 * Single-tap detection from accelerometer magnitude.
 *
 * A tap shows up as a brief spike in total acceleration above the ~1000 mg the
 * device reads at rest.
 *
 * WHY SINGLE, NOT DOUBLE. An earlier version required a double tap, on the
 * theory that a single tap is too easy to trigger accidentally. Measured on
 * hardware, that turned out to be the wrong trade:
 *
 *   - One physical tap produces SEVERAL impacts as the case rings out --
 *     observed gaps of 20, 30, and 60 ms between them. Pairing logic cannot
 *     reliably tell ringing from a deliberate second tap.
 *   - A user's natural gap between two taps measured ~570 ms median, straddling
 *     any reasonable pairing window, so some pairs registered and some did not.
 *
 * The result was a gesture that fired unpredictably. A single tap with a high
 * threshold and a lockout is both easier to perform and far more reliable:
 * accidental contact is rejected by the threshold, and ringing by the lockout.
 *
 * Pure arithmetic, no ESP-IDF or I2C, so it is tested on the host.
 */
#pragma once

#include <stdint.h>

/*
 * Acceleration magnitude, in milli-g, that counts as a deliberate tap.
 *
 * Chosen from measurement: deliberate taps landed at 6000-10433 mg, while case
 * ringing and incidental knocks clustered at 1450-2700 mg. 4000 sits cleanly
 * between the two populations.
 */
#define TAP_THRESHOLD_MG 4000

/*
 * After an accepted tap, ignore everything for this long. Covers the case
 * ringing out (which produces impacts for a few hundred ms) and stops one
 * physical tap from advancing several screens.
 */
#define TAP_LOCKOUT_MS 500

/*
 * Magnitude must fall back below this before another tap can register, so a
 * sustained high reading cannot latch on.
 */
#define TAP_RELEASE_MG 2000

typedef struct {
    /* When the current lockout expires; 0 if not locked out. */
    uint32_t lockout_until_ms;
    /* True while magnitude is still elevated from the last impact. */
    _Bool armed;
} tap_detector_t;

/* Reset a detector to its initial state. */
void tap_detector_init(tap_detector_t *d);

/*
 * Feed one accelerometer sample.
 *
 * `magnitude_mg` is the vector magnitude sqrt(x^2+y^2+z^2) in milli-g.
 * `now_ms` is a monotonic millisecond timestamp.
 *
 * Returns true exactly once per deliberate tap.
 */
_Bool tap_detector_feed(tap_detector_t *d, int32_t magnitude_mg, uint32_t now_ms);

/*
 * Vector magnitude in milli-g from raw QMI8658 counts.
 * `lsb_per_g` is the sensitivity for the configured range (4096 at +/-8g).
 */
int32_t accel_magnitude_mg(int16_t x, int16_t y, int16_t z, int32_t lsb_per_g);
