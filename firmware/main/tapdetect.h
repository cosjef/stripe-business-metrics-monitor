/*
 * Double-tap detection from accelerometer magnitude.
 *
 * A tap shows up as a brief spike in total acceleration above the ~1g the
 * device reads at rest. Two spikes close together, separated by a quiet gap,
 * are a double tap.
 *
 * Deliberately single-tap-blind: a single tap is far too easy to trigger by
 * setting the device down or bumping the desk, and a metric appliance that
 * changes screens when nudged is worse than one that does not respond at all.
 *
 * Pure arithmetic, no ESP-IDF or I2C, so it is tested on the host.
 */
#pragma once

#include <stdint.h>

/* Acceleration magnitude, in milli-g, above which a sample counts as impact.
 * At rest the device reads ~1000 mg (gravity). Measured on hardware: resting
 * noise stays within ~1020 mg, so this leaves generous headroom. */
#define TAP_THRESHOLD_MG 1450

/* A sample must fall back below this before another impact can register,
 * so one physical tap cannot be counted twice as it rings out. */
#define TAP_RELEASE_MG 1150

/* Two impacts closer together than this are one tap bouncing, not two taps. */
#define TAP_MIN_GAP_MS 60

/* Two impacts further apart than this are unrelated events. */
#define TAP_MAX_GAP_MS 600

typedef struct {
    /* Timestamp of the last confirmed impact, 0 if none pending. */
    uint32_t last_impact_ms;
    /* True while acceleration is still above threshold from the current
     * impact -- we wait for release before arming the next one. */
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
 * Returns true exactly once, on the sample that completes a double tap.
 */
_Bool tap_detector_feed(tap_detector_t *d, int32_t magnitude_mg, uint32_t now_ms);

/*
 * Vector magnitude in milli-g from raw QMI8658 counts.
 * `lsb_per_g` is the sensitivity for the configured range (4096 at +/-8g).
 */
int32_t accel_magnitude_mg(int16_t x, int16_t y, int16_t z, int32_t lsb_per_g);
