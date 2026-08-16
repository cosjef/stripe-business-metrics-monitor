/*
 * Battery sensing and level reporting.
 *
 * The board senses the cell on GPIO1 through a 200K/100K divider, so the ADC
 * reads one third of the true cell voltage.
 *
 * This header splits into two halves deliberately. Everything above
 * battery_start() is pure integer logic over millivolts and is tested on the
 * host; only the read itself touches ADC hardware. The split exists because
 * the parts that can be wrong quietly -- divider ratio, thresholds,
 * hysteresis -- are exactly the parts hardware makes hard to test.
 *
 * Millivolts throughout, no floating point. Same rule the money code follows,
 * for the same reason: exact comparisons and no accumulated error.
 */
#pragma once

#include <stdbool.h>

/*
 * A 3.7V lithium cell runs about 4.2V full to 3.0V empty, and the discharge
 * curve is flat across the middle -- most of the voltage drop is in the last
 * stretch. The thresholds sit low because 3.6V still has hours in it.
 */
#define BATTERY_LOW_MV       3500   /* getting low; still running fine */
#define BATTERY_CRITICAL_MV  3300   /* minutes left; protection cuts near 3.0V */

/*
 * Recovering to a better level requires clearing the threshold by this margin.
 * The cell sags under WiFi transmit bursts and recovers between them, tens of
 * millivolts at a time; without this the warning screen appears and vanishes
 * around the boundary, which reads as a malfunction rather than a warning.
 *
 * Applies only to improvement. Getting worse always takes effect immediately --
 * bad news is never delayed.
 */
#define BATTERY_HYSTERESIS_MV 80

/*
 * Outside this band the reading is not a lithium cell: the sense pin is
 * floating, disconnected, or the ADC failed. Report unknown rather than
 * critical -- warning about a battery because the ADC broke sends the owner to
 * charge a device that is not low.
 */
#define BATTERY_PLAUSIBLE_MIN_MV 2500
#define BATTERY_PLAUSIBLE_MAX_MV 4400

typedef enum {
    BATTERY_UNKNOWN = 0,  /* no plausible reading; show nothing */
    BATTERY_CHARGING,     /* on USB; the rail says nothing about the cell */
    BATTERY_OK,
    BATTERY_LOW,
    BATTERY_CRITICAL,
} battery_level_t;

/* Convert a raw ADC millivolt reading to cell millivolts (undo the divider). */
int battery_cell_mv(int adc_mv);

/*
 * Classify a cell voltage.
 *
 * `charging` short-circuits to BATTERY_CHARGING: with USB attached the charger
 * holds the rail near 4.2V regardless of the cell, so the reading carries no
 * information about stored charge.
 *
 * `previous` is the last reported level, used for hysteresis. Pass
 * BATTERY_UNKNOWN when there is no history -- the reading is then taken at
 * face value.
 */
battery_level_t battery_level(int cell_mv, bool charging,
                              battery_level_t previous);

/* Whether this level is worth interrupting the display for. */
bool battery_should_warn(battery_level_t level);

/* ---- hardware ---- */

/*
 * Bring up the ADC on the battery sense pin. Safe to call when no battery is
 * connected; readings then fall outside the plausible band and report unknown.
 */
int battery_start(void);

/*
 * Read the cell, in millivolts, with the divider already undone. Returns 0 if
 * the ADC is unavailable, which battery_level() maps to unknown.
 *
 * Averages several samples: a single ADC reading on the S3 is noisy enough to
 * wander tens of millivolts, which is the same magnitude as the hysteresis
 * band this is feeding.
 */
int battery_read_mv(void);

/* Current level, with charging detection and hysteresis applied internally. */
battery_level_t battery_current_level(void);
