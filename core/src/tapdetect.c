/*
 * Single-tap detection. See tapdetect.h for why it is single and not double.
 *
 * No ESP-IDF or I2C dependencies, so this builds and tests on the host.
 */
#include "tapdetect.h"

#include <math.h>

void tap_detector_init(tap_detector_t *d)
{
    d->lockout_until_ms = 0;
    d->armed = 0;
}

_Bool tap_detector_feed(tap_detector_t *d, int32_t magnitude_mg, uint32_t now_ms)
{
    /* Inside the lockout after an accepted tap: ignore everything. This is
     * what absorbs the case ringing out, which otherwise reads as a burst of
     * further taps and advances several screens from one strike. */
    if (d->lockout_until_ms != 0) {
        if (now_ms < d->lockout_until_ms) {
            return 0;
        }
        d->lockout_until_ms = 0;
        /* Also clear the release latch. The lockout has already outlasted the
         * ringing it exists to absorb, so requiring a separate low reading
         * before the next tap would leave the detector armed forever if the
         * next sample happens to arrive high. */
        d->armed = 0;
    }

    /* Wait for the reading to fall back before allowing another tap, so a
     * sustained high magnitude cannot latch on and fire repeatedly. */
    if (d->armed) {
        if (magnitude_mg < TAP_RELEASE_MG) {
            d->armed = 0;
        }
        return 0;
    }

    if (magnitude_mg < TAP_THRESHOLD_MG) {
        return 0;
    }

    d->armed = 1;
    d->lockout_until_ms = now_ms + TAP_LOCKOUT_MS;
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
