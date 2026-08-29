/*
 * AXP2101 register encoding.
 *
 *   cd firmware/test && make && ./test_axp2101
 *
 * The PMIC is why the AMOLED stayed black. On this board the AXP2101 gates the
 * panel's power rails, and our display_init() went straight to QSPI without
 * touching it -- so the panel initialised, reported success, LVGL rendered,
 * and no pixel emitted light. Every log line said success. There is no
 * backlight to check, which is what made it a silent failure.
 *
 * Waveshare's own boot log shows the required order plainly:
 *
 *     Initialize I2C bus
 *     Initialize pmic power
 *     axp2101: Init PMU SUCCESS!     <- rails come up here
 *     Initialize SPI bus             <- only then the display
 *
 * This file tests the arithmetic half: turning a millivolt request into the
 * register value the chip expects. Getting that wrong is dangerous in a way a
 * blank screen is not -- an over-volted rail can damage the panel -- so the
 * encoding is range-checked rather than trusted.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../main/axp2101_reg.h"

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

static void check_true(const char *what, bool cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/* ---- ALDO voltage encoding ---- */

/*
 * The ALDO rails run 500mV to 3500mV in 100mV steps, encoded as (mv-500)/100.
 * Waveshare sets all four to 3300mV for this board.
 */
static void test_aldo_encoding(void)
{
    printf("ALDO millivolts encode to register steps\n");

    check_int("500mV is step 0",   axp_aldo_encode(500),  0);
    check_int("1000mV is step 5",  axp_aldo_encode(1000), 5);
    check_int("3300mV is step 28", axp_aldo_encode(3300), 28);
    check_int("3500mV is step 30", axp_aldo_encode(3500), 30);
}

/*
 * Out-of-range requests must be refused, not clamped silently. An over-volted
 * rail can damage the panel, and a caller asking for 5V has a bug that should
 * surface rather than be quietly turned into 3.5V.
 */
static void test_out_of_range_is_refused(void)
{
    printf("out-of-range voltages are refused, not clamped\n");

    check_int("below minimum refused", axp_aldo_encode(400),  -1);
    check_int("above maximum refused", axp_aldo_encode(3600), -1);
    check_int("zero refused",          axp_aldo_encode(0),    -1);
    check_int("negative refused",      axp_aldo_encode(-100), -1);

    /* Non-multiples of the step size are refused too: silently rounding a
     * 3350mV request to 3300 or 3400 hides the caller's mistake. */
    check_int("non-step value refused", axp_aldo_encode(3350), -1);
}

/* ---- register map ---- */

/*
 * Each ALDO has its own voltage register. Mixing these up would set the wrong
 * rail, which on this board could mean powering the touch controller at the
 * panel's voltage or vice versa.
 */
static void test_register_map(void)
{
    printf("each ALDO maps to its own voltage register\n");

    check_int("ALDO1", axp_aldo_vol_reg(1), 0x92);
    check_int("ALDO2", axp_aldo_vol_reg(2), 0x93);
    check_int("ALDO3", axp_aldo_vol_reg(3), 0x94);
    check_int("ALDO4", axp_aldo_vol_reg(4), 0x95);

    check_int("no ALDO0", axp_aldo_vol_reg(0), -1);
    check_int("no ALDO5", axp_aldo_vol_reg(5), -1);

    /* All four registers must be distinct. */
    for (int a = 1; a <= 4; a++) {
        for (int b = a + 1; b <= 4; b++) {
            checks++;
            if (axp_aldo_vol_reg(a) == axp_aldo_vol_reg(b)) {
                failures++;
                printf("  FAIL ALDO%d and ALDO%d share a register\n", a, b);
            }
        }
    }
}

/*
 * The enable bits all live in one register, one bit per rail. Writing the
 * wrong bit turns off a rail something else depends on.
 */
static void test_enable_bits(void)
{
    printf("ALDO enable bits are distinct and in the right register\n");

    check_int("enable register", AXP_REG_LDO_ONOFF_CTRL0, 0x90);

    check_int("ALDO1 is bit 0", axp_aldo_enable_bit(1), 1 << 0);
    check_int("ALDO2 is bit 1", axp_aldo_enable_bit(2), 1 << 1);
    check_int("ALDO3 is bit 2", axp_aldo_enable_bit(3), 1 << 2);
    check_int("ALDO4 is bit 3", axp_aldo_enable_bit(4), 1 << 3);

    check_int("no bit for ALDO0", axp_aldo_enable_bit(0), 0);
    check_int("no bit for ALDO5", axp_aldo_enable_bit(5), 0);

    /* Enabling all four sets exactly four bits, none overlapping. */
    const int all = axp_aldo_enable_bit(1) | axp_aldo_enable_bit(2) |
                    axp_aldo_enable_bit(3) | axp_aldo_enable_bit(4);
    check_int("all four rails is 0x0F", all, 0x0F);
}

/*
 * The board's own configuration, from Waveshare's power_bsp.cpp. Stated as a
 * test so the values are checkable rather than buried in a driver.
 */
static void test_board_configuration(void)
{
    printf("this board wants all four ALDO rails at 3.3V\n");

    check_int("panel rail voltage", AXP_BOARD_ALDO_MV, 3300);
    check_true("which is a valid encoding",
               axp_aldo_encode(AXP_BOARD_ALDO_MV) >= 0);
    check_int("and encodes to 28", axp_aldo_encode(AXP_BOARD_ALDO_MV), 28);
}

/*
 * The panel reset rail.
 *
 * The CO5300's reset pin is wired to ALDO3, not to an MCU GPIO, so cycling
 * that rail is the only way to reset the panel. Getting the rail number wrong
 * would reset something else and leave the display black -- which is precisely
 * the failure this constant exists to prevent, so it is pinned rather than
 * left as a comment.
 */
static void test_panel_reset_rail(void)
{
    printf("the panel reset rail is ALDO3\n");

    check_int("reset rail", AXP_PANEL_RESET_ALDO, 3);

    /* It must be a real, addressable rail. */
    check_true("has a voltage register",
               axp_aldo_vol_reg(AXP_PANEL_RESET_ALDO) > 0);
    check_true("has an enable bit",
               axp_aldo_enable_bit(AXP_PANEL_RESET_ALDO) != 0);

    /* Toggling it must not disturb the other rails: the enable bits share one
     * register, so a mask error here would cut power to touch or the IMU. */
    const int reset_bit = axp_aldo_enable_bit(AXP_PANEL_RESET_ALDO);
    for (int n = 1; n <= 4; n++) {
        if (n == AXP_PANEL_RESET_ALDO) {
            continue;
        }
        checks++;
        if ((axp_aldo_enable_bit(n) & reset_bit) != 0) {
            failures++;
            printf("  FAIL ALDO%d shares a bit with the reset rail\n", n);
        }
    }
}

int main(void)
{
    test_aldo_encoding();
    test_out_of_range_is_refused();
    test_register_map();
    test_enable_bits();
    test_board_configuration();
    test_panel_reset_rail();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
