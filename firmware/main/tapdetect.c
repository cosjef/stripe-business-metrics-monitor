/*
 * Double-tap detection. See tapdetect.h for the rationale.
 *
 * No ESP-IDF or I2C dependencies, so this builds and tests on the host.
 */
#include "tapdetect.h"

#include <math.h>

void tap_detector_init(tap_detector_t *d)
{
    d->last_impact_ms = 0;
    d->armed = 0;
}

_Bool tap_detector_feed(tap_detector_t *d, int32_t magnitude_mg, uint32_t now_ms)
{
    /* Wait for acceleration to fall back to near-rest before another impact
     * can register. Without this, one physical tap rings out across several
     * samples and reads as a burst of impacts. */
    if (d->armed) {
        if (magnitude_mg < TAP_RELEASE_MG) {
            d->armed = 0;
        }
        return 0;
    }

    if (magnitude_mg < TAP_THRESHOLD_MG) {
        return 0;
    }

    /* An impact. */
    d->armed = 1;

    const uint32_t prev = d->last_impact_ms;
    d->last_impact_ms = now_ms;

    if (prev == 0) {
        /* First impact; wait to see whether a second follows. */
        return 0;
    }

    const uint32_t gap = now_ms - prev;

    if (gap < TAP_MIN_GAP_MS) {
        /* Too soon to be a deliberate second tap -- treat it as the same tap
         * still ringing, and keep waiting for a real second one. */
        return 0;
    }

    if (gap > TAP_MAX_GAP_MS) {
        /* Too late to pair. This impact becomes the new first tap. */
        return 0;
    }

    /* A genuine double tap. Reset so the next tap starts a fresh pair rather
     * than chaining off this one. */
    d->last_impact_ms = 0;
    return 1;
}

int32_t accel_magnitude_mg(int16_t x, int16_t y, int16_t z, int32_t lsb_per_g)
{
    if (lsb_per_g <= 0) {
        return 0;
    }

    /* Promote before squaring: three int16 squares overflow int32 at full
     * scale. */
    const double fx = (double)x;
    const double fy = (double)y;
    const double fz = (double)z;
    const double counts = sqrt(fx * fx + fy * fy + fz * fz);

    return (int32_t)((counts * 1000.0) / (double)lsb_per_g + 0.5);
}
