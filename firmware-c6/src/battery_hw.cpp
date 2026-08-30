/*
 * Battery sensing on the C6 AMOLED, via the AXP2101.
 *
 * The S3's battery_hw.c reads a divider on GPIO1 with the ADC. This board has
 * no such divider: the PMIC measures the cell and reports millivolts over
 * I2C. Reusing the S3's reader here would have been an artifact of the wrong
 * board -- it would sample a pin that senses nothing.
 *
 * Verified before writing this: the chip at I2C 0x34 answers register 0x03
 * with 0x4A, the AXP2101's documented chip ID. It is the part the code
 * assumes, not an assumption inherited from the S3.
 *
 * Only the read lives here. Every judgement about what a voltage MEANS --
 * thresholds, hysteresis, plausibility, what charging implies -- is in
 * core/src/battery.c, which is host-tested with 54 checks. That split is the
 * point: the parts that can be wrong quietly are the parts hardware makes
 * hard to test.
 */
#include "battery_hw.h"

#include <Arduino.h>
#include <XPowersLib.h>

extern "C" {
#include "battery.h"
}

/* Owned by main.cpp, which brings the PMIC up before the display. */
extern XPowersPMU s_pmu;

static battery_level_t s_level = BATTERY_UNKNOWN;

bool battery_hw_read(battery_reading_t *out)
{
    if (out == NULL) {
        return false;
    }

    /*
     * getBattVoltage() returns the cell in millivolts directly -- there is no
     * divider to undo, so battery_cell_mv() is not used on this board.
     */
    const int mv = (int)s_pmu.getBattVoltage();
    const bool charging = s_pmu.isCharging() || s_pmu.isVbusIn();

    out->cell_mv = mv;
    out->charging = charging;

    /*
     * Hysteresis needs the previous level, so it is kept here rather than
     * recomputed from scratch each call. Passing BATTERY_UNKNOWN the first
     * time takes the reading at face value, which is what we want at boot.
     */
    s_level = battery_level(mv, charging, s_level);
    out->level = s_level;

    return true;
}
