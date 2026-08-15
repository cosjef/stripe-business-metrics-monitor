/*
 * Host tests for QMI8658 TAP_STATUS decoding.
 *
 *   cd firmware/test && make && ./test_tapstatus
 *
 * These exist because a wrong mask here (0x03 instead of 0x07 on a three-bit
 * field) silently discarded real taps for several debugging sessions. The
 * decode was buried in the I2C polling loop where nothing could test it.
 */
#include <stdio.h>

#include "../main/tapstatus.h"

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

static void check_false(const char *what, int cond)
{
    checks++;
    if (cond) {
        failures++;
        printf("  FAIL %s: expected false\n", what);
    }
}

/*
 * The axis field is bits 6:4. This is the regression that motivated the file:
 * a 0x03 mask truncates it, and Z (3) happens to survive while other values
 * corrupt -- which is why some taps registered and others vanished.
 */
static void test_axis_is_three_bits(void)
{
    printf("axis field occupies bits 6:4\n");

    check_int("0x00 -> no axis", tap_status_decode(0x00).axis, TAP_AXIS_NONE);
    check_int("0x10 -> X",       tap_status_decode(0x10).axis, TAP_AXIS_X);
    check_int("0x20 -> Y",       tap_status_decode(0x20).axis, TAP_AXIS_Y);
    check_int("0x30 -> Z",       tap_status_decode(0x30).axis, TAP_AXIS_Z);

    /* With a 0x03 mask these all decode wrongly. */
    check_int("0x21 (Y, single) -> Y", tap_status_decode(0x21).axis, TAP_AXIS_Y);
    check_int("0x31 (Z, single) -> Z", tap_status_decode(0x31).axis, TAP_AXIS_Z);
    check_int("0x12 (X, double) -> X", tap_status_decode(0x12).axis, TAP_AXIS_X);
}

/*
 * Bit 7 is polarity, not part of the axis field. Including it would make a
 * negative-direction tap decode as a different axis entirely.
 */
static void test_polarity_does_not_corrupt_axis(void)
{
    printf("polarity bit is separate from the axis field\n");

    const tap_status_t pos = tap_status_decode(0x31);  /* Z, single, positive */
    const tap_status_t neg = tap_status_decode(0xB1);  /* Z, single, negative */

    check_int("positive Z axis", pos.axis, TAP_AXIS_Z);
    check_int("negative Z axis", neg.axis, TAP_AXIS_Z);
    check_false("positive not flagged negative", pos.negative);
    check_true("negative flagged negative", neg.negative);

    check_int("both decode as single", pos.num, TAP_NUM_SINGLE);
    check_int("negative also single", neg.num, TAP_NUM_SINGLE);
}

static void test_tap_count(void)
{
    printf("tap count field\n");

    check_int("0x30 -> none",   tap_status_decode(0x30).num, TAP_NUM_NONE);
    check_int("0x31 -> single", tap_status_decode(0x31).num, TAP_NUM_SINGLE);
    check_int("0x32 -> double", tap_status_decode(0x32).num, TAP_NUM_DOUBLE);
    check_int("0x33 -> N/A",    tap_status_decode(0x33).num, TAP_NUM_NA);
}

/*
 * Which decoded values should actually advance a screen.
 */
static void test_is_real(void)
{
    printf("real-tap qualification\n");

    check_true("Z single is real",  tap_status_is_real(tap_status_decode(0x31)));
    check_true("Y single is real",  tap_status_is_real(tap_status_decode(0x21)));
    check_true("X single is real",  tap_status_is_real(tap_status_decode(0x11)));
    check_true("Z double is real",  tap_status_is_real(tap_status_decode(0x32)));
    check_true("negative Y is real", tap_status_is_real(tap_status_decode(0xA1)));

    /* No axis means the flag latched without a detection. */
    check_false("no axis is not real", tap_status_is_real(tap_status_decode(0x01)));
    check_false("all zero is not real", tap_status_is_real(tap_status_decode(0x00)));

    /* An axis with no tap count is not a tap either. */
    check_false("axis but count 0 is not real",
                tap_status_is_real(tap_status_decode(0x30)));

    /* count 3 is documented as N/A, not a valid tap. */
    check_false("count N/A is not real",
                tap_status_is_real(tap_status_decode(0x33)));
}

/*
 * Values actually observed on hardware during testing.
 */
static void test_observed_values(void)
{
    printf("values observed on hardware\n");

    /* Logged as "axis=3 count=1" -- a clean Z single tap. */
    const tap_status_t z = tap_status_decode(0x31);
    check_int("observed Z tap axis", z.axis, TAP_AXIS_Z);
    check_int("observed Z tap count", z.num, TAP_NUM_SINGLE);
    check_true("observed Z tap is real", tap_status_is_real(z));

    /* Logged as "axis=2 count=1" -- a Y single tap. */
    const tap_status_t y = tap_status_decode(0x21);
    check_int("observed Y tap axis", y.axis, TAP_AXIS_Y);
    check_true("observed Y tap is real", tap_status_is_real(y));

    /* Logged as "axis=2 count=3", which under the old 0x03 mask came from a
     * raw byte whose real axis bits were being truncated. count=3 is N/A, so
     * this must not advance a screen. */
    check_false("count=3 rejected", tap_status_is_real(tap_status_decode(0x23)));
}

/*
 * Every possible byte must decode without asserting, and is_real must only
 * ever accept a sane combination.
 */
static void test_exhaustive(void)
{
    printf("all 256 register values decode sanely\n");

    int real_count = 0;
    for (int raw = 0; raw <= 0xFF; raw++) {
        const tap_status_t s = tap_status_decode((uint8_t)raw);

        checks++;
        if (s.axis > TAP_AXIS_Z) {
            failures++;
            printf("  FAIL raw 0x%02X decoded axis %d out of range\n", raw, s.axis);
        }

        if (tap_status_is_real(s)) {
            real_count++;
            checks++;
            if (s.axis == TAP_AXIS_NONE) {
                failures++;
                printf("  FAIL raw 0x%02X accepted with no axis\n", raw);
            }
        }
    }

    /* 3 axes (X,Y,Z) x 2 valid counts (single,double) x 2 polarities
     * x 4 combinations of the reserved bits 3:2, which are unconstrained
     * = 48 acceptable bytes. */
    check_int("exactly 48 byte values are real taps", real_count, 48);
}

int main(void)
{
    printf("QMI8658 TAP_STATUS decoding tests\n\n");

    test_axis_is_three_bits();
    test_polarity_does_not_corrupt_axis();
    test_tap_count();
    test_is_real();
    test_observed_values();
    test_exhaustive();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
