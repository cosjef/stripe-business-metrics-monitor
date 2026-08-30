/*
 * Battery level logic. See battery.h for thresholds and rationale.
 *
 * Pure integer math over millivolts, no ESP-IDF, so the host tests can walk a
 * full discharge curve. The ADC read lives in battery_hw.c.
 */
#include "battery.h"

/*
 * 200K/100K divider: the ADC sees Vcell * 100/(200+100), so one third.
 * Multiplying back by 3 is exact for the integer ranges involved here.
 */
int battery_cell_mv(int adc_mv)
{
    return adc_mv * 3;
}

battery_level_t battery_level(int cell_mv, bool charging,
                              battery_level_t previous)
{
    /* With USB attached the charger holds the rail near 4.2V regardless of the
     * cell, so the reading carries no information about stored charge. */
    if (charging) {
        return BATTERY_CHARGING;
    }

    if (cell_mv < BATTERY_PLAUSIBLE_MIN_MV ||
        cell_mv > BATTERY_PLAUSIBLE_MAX_MV) {
        return BATTERY_UNKNOWN;
    }

    /* What the voltage alone says, before hysteresis. */
    battery_level_t raw;
    if (cell_mv <= BATTERY_CRITICAL_MV) {
        raw = BATTERY_CRITICAL;
    } else if (cell_mv <= BATTERY_LOW_MV) {
        raw = BATTERY_LOW;
    } else {
        raw = BATTERY_OK;
    }

    /*
     * Hysteresis applies only when improving on a previous real battery level.
     * CHARGING and UNKNOWN are not points on the voltage scale, so there is no
     * band to measure against -- the first reading after unplugging is taken at
     * face value rather than stranding the device in CHARGING.
     */
    if (previous == BATTERY_UNKNOWN || previous == BATTERY_CHARGING) {
        return raw;
    }

    /* Getting worse is always immediate. Larger enum value is worse. */
    if (raw >= previous) {
        return raw;
    }

    /*
     * Improving: require clearing the threshold we are climbing out of by the
     * hysteresis margin, otherwise hold the worse level. Checked against the
     * previous level's own threshold so each boundary gets its own band.
     */
    if (previous == BATTERY_CRITICAL &&
        cell_mv <= BATTERY_CRITICAL_MV + BATTERY_HYSTERESIS_MV) {
        return BATTERY_CRITICAL;
    }

    if (previous == BATTERY_LOW &&
        cell_mv <= BATTERY_LOW_MV + BATTERY_HYSTERESIS_MV) {
        return BATTERY_LOW;
    }

    return raw;
}

bool battery_should_warn(battery_level_t level)
{
    return level == BATTERY_LOW || level == BATTERY_CRITICAL;
}
