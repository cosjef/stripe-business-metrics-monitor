/*
 * Battery level logic: divider math, thresholds, hysteresis.
 *
 *   cd firmware/test && make && ./test_battery
 *
 * The ADC read itself is hardware and lives behind a thin wrapper. Everything
 * decided from the resulting millivolts is pure, and that is what matters:
 * getting the divider ratio wrong misreports the voltage by 3x, and omitting
 * hysteresis makes the warning screen flicker in and out every poll as a cell
 * sags under WiFi transmit bursts and recovers between them.
 *
 * Thresholds are in millivolts throughout. No floating point -- same rule the
 * money code follows, for the same reason: exact comparisons.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/battery.h"

static int failures = 0;
static int checks = 0;

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

static const char *level_name(battery_level_t l)
{
    switch (l) {
    case BATTERY_UNKNOWN:  return "UNKNOWN";
    case BATTERY_CHARGING: return "CHARGING";
    case BATTERY_OK:       return "OK";
    case BATTERY_LOW:      return "LOW";
    case BATTERY_CRITICAL: return "CRITICAL";
    }
    return "?";
}

static void check_level(const char *what, battery_level_t got, battery_level_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %s, want %s\n",
               what, level_name(got), level_name(want));
    }
}

/* ---- divider math ---- */

/*
 * The board senses the cell through a 200K/100K divider, so the ADC sees one
 * third of the true voltage. Getting this backwards reads a healthy 4.0V cell
 * as 1.33V -- instant false "critical" -- so it is worth pinning exactly.
 */
static void test_divider_math(void)
{
    printf("200K/100K divider: ADC sees one third of cell voltage\n");

    check_int("1400mV at ADC -> 4200mV cell (full)",
              battery_cell_mv(1400), 4200);
    check_int("1233mV at ADC -> 3699mV cell",
              battery_cell_mv(1233), 3699);
    check_int("1000mV at ADC -> 3000mV cell (empty)",
              battery_cell_mv(1000), 3000);
    check_int("0 at ADC -> 0",
              battery_cell_mv(0), 0);
}

/* ---- thresholds ---- */

/*
 * A 3.7V lithium cell's usable range is roughly 4.2V full to 3.0V empty, and
 * the discharge curve is flat across the middle -- most of the drop happens in
 * the last stretch. So the thresholds sit low: 3.5V still has meaningful
 * runtime left, 3.3V does not.
 */
static void test_thresholds(void)
{
    printf("level thresholds across the discharge curve\n");

    /* Fresh from the charger. */
    check_level("4200mV -> ok", battery_level(4200, false, BATTERY_UNKNOWN),
                BATTERY_OK);
    check_level("3900mV -> ok", battery_level(3900, false, BATTERY_UNKNOWN),
                BATTERY_OK);
    /* The flat middle of the curve. */
    check_level("3700mV -> ok", battery_level(3700, false, BATTERY_UNKNOWN),
                BATTERY_OK);
    check_level("3600mV -> ok", battery_level(3600, false, BATTERY_UNKNOWN),
                BATTERY_OK);

    /* Getting low, but still running. */
    check_level("3500mV -> low", battery_level(3500, false, BATTERY_UNKNOWN),
                BATTERY_LOW);
    check_level("3400mV -> low", battery_level(3400, false, BATTERY_UNKNOWN),
                BATTERY_LOW);

    /* Minutes left; the protection circuit cuts out near 3.0V. */
    check_level("3300mV -> critical",
                battery_level(3300, false, BATTERY_UNKNOWN), BATTERY_CRITICAL);
    check_level("3100mV -> critical",
                battery_level(3100, false, BATTERY_UNKNOWN), BATTERY_CRITICAL);

    /* Exact boundaries, since off-by-one here is invisible in normal use. */
    check_level("exactly at low threshold -> low",
                battery_level(BATTERY_LOW_MV, false, BATTERY_UNKNOWN),
                BATTERY_LOW);
    check_level("one mV above low threshold -> ok",
                battery_level(BATTERY_LOW_MV + 1, false, BATTERY_UNKNOWN),
                BATTERY_OK);
    check_level("exactly at critical threshold -> critical",
                battery_level(BATTERY_CRITICAL_MV, false, BATTERY_UNKNOWN),
                BATTERY_CRITICAL);
    check_level("one mV above critical -> low",
                battery_level(BATTERY_CRITICAL_MV + 1, false, BATTERY_UNKNOWN),
                BATTERY_LOW);
}

/*
 * On USB the charger holds the rail near 4.2V regardless of the cell, so the
 * reading says nothing about stored charge. Report charging and show nothing:
 * a device on a desk with a cable in it does not need a battery warning.
 */
static void test_charging_suppresses_warnings(void)
{
    printf("on USB power, level reports charging and warnings are suppressed\n");

    check_level("4200mV while charging", battery_level(4200, true, BATTERY_OK),
                BATTERY_CHARGING);
    check_level("3400mV while charging still charging -- rail is not the cell",
                battery_level(3400, true, BATTERY_LOW), BATTERY_CHARGING);
    check_level("3100mV while charging still charging",
                battery_level(3100, true, BATTERY_CRITICAL), BATTERY_CHARGING);

    check_true("charging never warns", !battery_should_warn(BATTERY_CHARGING));
    check_true("ok never warns", !battery_should_warn(BATTERY_OK));
    check_true("unknown never warns -- no reading is not bad news",
               !battery_should_warn(BATTERY_UNKNOWN));
    check_true("low warns", battery_should_warn(BATTERY_LOW));
    check_true("critical warns", battery_should_warn(BATTERY_CRITICAL));
}

/*
 * An implausible reading means the sense pin is disconnected or the ADC failed.
 * Report unknown rather than critical: showing a battery warning because the
 * ADC is broken sends the owner to charge a device that is not actually low.
 */
static void test_implausible_readings(void)
{
    printf("implausible readings report unknown, not critical\n");

    check_level("0mV -> unknown (pin floating or disconnected)",
                battery_level(0, false, BATTERY_UNKNOWN), BATTERY_UNKNOWN);
    check_level("500mV -> unknown (below any real lithium cell)",
                battery_level(500, false, BATTERY_UNKNOWN), BATTERY_UNKNOWN);
    check_level("6000mV -> unknown (above any single cell)",
                battery_level(6000, false, BATTERY_UNKNOWN), BATTERY_UNKNOWN);

    /* Just inside the plausible band still reads normally. */
    check_level("at the plausibility floor -> critical",
                battery_level(BATTERY_PLAUSIBLE_MIN_MV, false, BATTERY_UNKNOWN),
                BATTERY_CRITICAL);
    check_level("at the plausibility ceiling -> ok",
                battery_level(BATTERY_PLAUSIBLE_MAX_MV, false, BATTERY_UNKNOWN),
                BATTERY_OK);
}

/* ---- hysteresis ---- */

/*
 * The reason this module has state at all.
 *
 * A lithium cell sags under load and recovers between bursts. This device
 * transmits over WiFi every 60 seconds, and the sag is tens of millivolts --
 * easily enough to cross a threshold and cross back. Without hysteresis the
 * warning screen would appear and vanish repeatedly around the boundary, which
 * reads as a malfunction rather than a warning.
 *
 * So recovering to a better level requires clearing the threshold by a margin;
 * getting worse takes effect immediately. Bad news is never delayed.
 */
static void test_hysteresis(void)
{
    printf("hysteresis stops the warning flickering as the cell sags\n");

    /* Dropping into low happens the moment it is warranted. */
    check_level("ok -> low on first reading below threshold",
                battery_level(3490, false, BATTERY_OK), BATTERY_LOW);

    /* Recovering does not, until it clears the threshold by the margin. */
    check_level("low stays low just above the threshold",
                battery_level(BATTERY_LOW_MV + 10, false, BATTERY_LOW),
                BATTERY_LOW);
    check_level("low stays low within the hysteresis band",
                battery_level(BATTERY_LOW_MV + BATTERY_HYSTERESIS_MV - 1,
                              false, BATTERY_LOW),
                BATTERY_LOW);
    check_level("low -> ok once clear of the band",
                battery_level(BATTERY_LOW_MV + BATTERY_HYSTERESIS_MV + 1,
                              false, BATTERY_LOW),
                BATTERY_OK);

    /* Same rule one level down. */
    check_level("critical -> low only once clear of the band",
                battery_level(BATTERY_CRITICAL_MV + BATTERY_HYSTERESIS_MV + 1,
                              false, BATTERY_CRITICAL),
                BATTERY_LOW);
    check_level("critical stays critical inside the band",
                battery_level(BATTERY_CRITICAL_MV + 10, false,
                              BATTERY_CRITICAL),
                BATTERY_CRITICAL);

    /* Worsening is always immediate, even from a warned state. */
    check_level("low -> critical immediately",
                battery_level(3200, false, BATTERY_LOW), BATTERY_CRITICAL);
}

/*
 * Unplugging USB must not strand the device in CHARGING. The previous level
 * was CHARGING, which is not a level the hysteresis band can be measured
 * against, so the first battery reading after unplug is taken at face value.
 */
static void test_unplug_settles_immediately(void)
{
    printf("unplugging USB resolves to a real level on the first reading\n");

    check_level("charging -> ok on unplug with a healthy cell",
                battery_level(3900, false, BATTERY_CHARGING), BATTERY_OK);
    check_level("charging -> low on unplug with a sagging cell",
                battery_level(3400, false, BATTERY_CHARGING), BATTERY_LOW);
    check_level("charging -> critical on unplug with a flat cell",
                battery_level(3100, false, BATTERY_CHARGING),
                BATTERY_CRITICAL);
}

/*
 * A whole discharge walked end to end, the way the device actually experiences
 * it: full, the long flat middle, the knee, then the warnings.
 */
static void test_discharge_sequence(void)
{
    printf("a full discharge produces one clean transition per level\n");

    const int mv[] = {4200, 4100, 3950, 3800, 3700, 3650, 3600,
                      3550, 3480, 3400, 3350, 3300, 3200, 3100};
    const battery_level_t want[] = {
        BATTERY_OK, BATTERY_OK, BATTERY_OK, BATTERY_OK, BATTERY_OK,
        BATTERY_OK, BATTERY_OK, BATTERY_OK,
        BATTERY_LOW, BATTERY_LOW, BATTERY_LOW,
        BATTERY_CRITICAL, BATTERY_CRITICAL, BATTERY_CRITICAL,
    };

    battery_level_t prev = BATTERY_UNKNOWN;
    int transitions = 0;

    for (size_t i = 0; i < sizeof(mv) / sizeof(mv[0]); i++) {
        const battery_level_t got = battery_level(mv[i], false, prev);

        char what[80];
        snprintf(what, sizeof(what), "%dmV during discharge", mv[i]);
        check_level(what, got, want[i]);

        if (prev != BATTERY_UNKNOWN && got != prev) {
            transitions++;
        }
        prev = got;
    }

    /* ok -> low -> critical, and nothing else. No oscillation. */
    check_int("exactly two transitions across the discharge", transitions, 2);
}

int main(void)
{
    test_divider_math();
    test_thresholds();
    test_charging_suppresses_warnings();
    test_implausible_readings();
    test_hysteresis();
    test_unplug_settles_immediately();
    test_discharge_sequence();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
