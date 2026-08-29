/*
 * Panel orientation.
 *
 *   cd firmware/test && make && ./test_orientation
 *
 * The enclosure puts the USB-C port at the bottom; the panel's native scan
 * order assumes it at the top, so the image arrives upside down.
 *
 * The correction is small but easy to get subtly wrong. A 180 degree rotation
 * mirrors both axes and swaps neither. Adding swap_xy transposes the image to
 * landscape -- and because this panel is square, a transposed image still fills
 * the screen and still looks like a rendered UI. It reads as a plausible
 * mistake rather than an obvious failure, which is exactly the kind of bug that
 * survives a glance. Hence asserting it rather than eyeballing it.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/orientation.h"

static int failures = 0;
static int checks = 0;

static void check_bool(const char *what, bool got, bool want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %s, want %s\n",
               what, got ? "true" : "false", want ? "true" : "false");
    }
}

/*
 * The shipped orientation: USB-C at the bottom.
 */
static void test_180_degrees(void)
{
    printf("180 degrees: both axes mirrored, neither swapped\n");

    const display_orientation_t o = display_orientation(180);

    check_bool("mirror_x set", o.mirror_x, true);
    check_bool("mirror_y set", o.mirror_y, true);

    /* The one that matters most. Swapping here transposes to landscape, which
     * on a square panel is a plausible-looking wrong answer. */
    check_bool("swap_xy NOT set -- swapping transposes to landscape",
               o.swap_xy, false);
}

/*
 * The panel's native orientation, kept so the rotation is expressed as a
 * deliberate choice rather than a magic pair of booleans.
 */
static void test_0_degrees(void)
{
    printf("0 degrees: native scan order, nothing touched\n");

    const display_orientation_t o = display_orientation(0);

    check_bool("mirror_x clear", o.mirror_x, false);
    check_bool("mirror_y clear", o.mirror_y, false);
    check_bool("swap_xy clear", o.swap_xy, false);
}

/*
 * 180 must be the exact inverse of 0 on both axes. Stated separately because
 * this is the property that makes the rotation correct, independent of which
 * specific booleans the driver happens to want.
 */
static void test_180_inverts_0(void)
{
    printf("180 is the inverse of 0 on both axes\n");

    const display_orientation_t zero = display_orientation(0);
    const display_orientation_t half = display_orientation(180);

    check_bool("mirror_x inverted", half.mirror_x, !zero.mirror_x);
    check_bool("mirror_y inverted", half.mirror_y, !zero.mirror_y);
    check_bool("transposition unchanged", half.swap_xy, zero.swap_xy);
}

/*
 * The quarter turns transpose. Not used by this enclosure, but defined rather
 * than left to fall through to a default that silently returns 0 degrees.
 */
static void test_quarter_turns_transpose(void)
{
    printf("90 and 270 transpose to landscape\n");

    const display_orientation_t r90 = display_orientation(90);
    check_bool("90 swaps axes", r90.swap_xy, true);

    const display_orientation_t r270 = display_orientation(270);
    check_bool("270 swaps axes", r270.swap_xy, true);

    /* The two quarter turns must differ, or one of them is wrong: they are
     * opposite rotations, so exactly one axis flips between them. */
    checks++;
    if (r90.mirror_x == r270.mirror_x && r90.mirror_y == r270.mirror_y) {
        failures++;
        printf("  FAIL 90 and 270 are identical; one is wrong\n");
    }
}

/*
 * An unsupported angle must fall back to native rather than to an arbitrary
 * mirrored state -- an upside-down display is a visible bug, a silently
 * transposed one is not.
 */
static void test_unsupported_angles_fall_back(void)
{
    printf("unsupported angles fall back to native orientation\n");

    const int bad[] = {-90, 1, 45, 359, 360, 720};

    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        const display_orientation_t o = display_orientation(bad[i]);

        char what[64];
        snprintf(what, sizeof(what), "%d degrees is native", bad[i]);
        checks++;
        if (o.swap_xy || o.mirror_x || o.mirror_y) {
            failures++;
            printf("  FAIL %s: got swap=%d mx=%d my=%d\n",
                   what, o.swap_xy, o.mirror_x, o.mirror_y);
        }
    }
}

/*
 * All four orientations must be distinct.
 *
 * The enclosure turned out to put the USB-C port on the side rather than the
 * bottom, so the needed rotation is a quarter turn, and which quarter is a
 * question about the physical case that no host test can answer. What the test
 * can guarantee is that cycling through the four lands on four genuinely
 * different configurations -- if two collide, a cycle would appear to skip and
 * the right one might never be reachable.
 */
static void test_all_four_are_distinct(void)
{
    printf("the four orientations are all distinct\n");

    const int angles[] = {0, 90, 180, 270};
    display_orientation_t o[4];

    for (int i = 0; i < 4; i++) {
        o[i] = display_orientation(angles[i]);
    }

    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            checks++;
            if (o[i].swap_xy == o[j].swap_xy &&
                o[i].mirror_x == o[j].mirror_x &&
                o[i].mirror_y == o[j].mirror_y) {
                failures++;
                printf("  FAIL %d and %d degrees are identical\n",
                       angles[i], angles[j]);
            }
        }
    }
}

/*
 * Cycling steps through the four in order and wraps. This backs the
 * button-driven orientation picker: with the port on the side, the correct
 * quarter turn is faster to find by eye than to derive.
 */
static void test_cycle_wraps(void)
{
    printf("cycling steps 0 -> 90 -> 180 -> 270 -> 0\n");

    checks++;
    if (display_orientation_next(0) != 90) {
        failures++;
        printf("  FAIL 0 should advance to 90\n");
    }

    checks++;
    if (display_orientation_next(90) != 180) {
        failures++;
        printf("  FAIL 90 should advance to 180\n");
    }

    checks++;
    if (display_orientation_next(180) != 270) {
        failures++;
        printf("  FAIL 180 should advance to 270\n");
    }

    checks++;
    if (display_orientation_next(270) != 0) {
        failures++;
        printf("  FAIL 270 should wrap to 0\n");
    }

    /* An unsupported angle restarts the cycle rather than sticking. */
    checks++;
    if (display_orientation_next(45) != 0) {
        failures++;
        printf("  FAIL an invalid angle should restart at 0\n");
    }
}

int main(void)
{
    test_0_degrees();
    test_180_degrees();
    test_180_inverts_0();
    test_quarter_turns_transpose();
    test_unsupported_angles_fall_back();
    test_all_four_are_distinct();
    test_cycle_wraps();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
