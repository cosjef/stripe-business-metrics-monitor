/*
 * Host tests for double-tap detection.
 *
 *   cd firmware/test && make && ./test_tapdetect
 *
 * Baseline values come from measurements on the real device: at rest the
 * QMI8658 reads ~1000 mg total (gravity), with noise staying inside ~1020 mg
 * across 295 samples.
 */
#include <stdio.h>

#include "../main/tapdetect.h"

static int failures = 0;
static int checks = 0;

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

/* Feed a run of samples at one level, returning how many taps fired. */
static int feed_level(tap_detector_t *d, int32_t mg, uint32_t *t,
                      int count, uint32_t step_ms)
{
    int fired = 0;
    for (int i = 0; i < count; i++) {
        if (tap_detector_feed(d, mg, *t)) {
            fired++;
        }
        *t += step_ms;
    }
    return fired;
}

/* One impact: a few samples above threshold, then back to rest. */
static int feed_impact(tap_detector_t *d, uint32_t *t, int32_t peak_mg)
{
    int fired = 0;
    fired += feed_level(d, peak_mg, t, 2, 10);   /* the strike */
    fired += feed_level(d, 1000, t, 4, 10);      /* settle back to gravity */
    return fired;
}

/* ---- the core behavior ---- */

static void test_double_tap_fires(void)
{
    printf("a double tap fires exactly once\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    int fired = 0;
    fired += feed_impact(&d, &t, 1800);
    t += 100;                       /* gap between the two taps */
    fired += feed_impact(&d, &t, 1800);

    check_int("double tap fires once", fired, 1);

    /* And it must not keep firing afterwards. */
    fired = feed_level(&d, 1000, &t, 20, 10);
    check_int("no repeat after firing", fired, 0);
}

static void test_single_tap_does_not_fire(void)
{
    printf("a single tap does not fire\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    int fired = feed_impact(&d, &t, 1800);
    check_int("one impact alone does not fire", fired, 0);

    /* Waiting past the window must not fire either. */
    fired = feed_level(&d, 1000, &t, 100, 10);
    check_int("still silent after the window expires", fired, 0);
}

/*
 * The device sitting on a desk must never advance a screen. This is the
 * failure mode that would make the product feel broken.
 */
static void test_rest_never_fires(void)
{
    printf("resting on a desk never fires\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;

    /* Measured at-rest range on hardware, with margin. */
    const int32_t rest_samples[] = {995, 1000, 1005, 1010, 1020, 998, 1002};

    int fired = 0;
    for (int rep = 0; rep < 200; rep++) {
        for (size_t i = 0; i < sizeof(rest_samples) / sizeof(rest_samples[0]); i++) {
            if (tap_detector_feed(&d, rest_samples[i], t)) {
                fired++;
            }
            t += 10;
        }
    }

    check_int("1400 resting samples produce no taps", fired, 0);
}

/*
 * Being picked up or set down is a slow, sustained change -- not a tap.
 */
static void test_handling_does_not_fire(void)
{
    printf("picking the device up does not fire\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;

    /* A gradual swing up to 1.3g and back, over ~600ms. */
    int fired = 0;
    for (int mg = 1000; mg <= 1300; mg += 20) {
        if (tap_detector_feed(&d, mg, t)) fired++;
        t += 20;
    }
    for (int mg = 1300; mg >= 1000; mg -= 20) {
        if (tap_detector_feed(&d, mg, t)) fired++;
        t += 20;
    }

    check_int("slow handling below threshold does not fire", fired, 0);
}

/* ---- timing windows ---- */

static void test_taps_too_close_are_one_tap(void)
{
    printf("two impacts closer than the min gap are one tap\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    /* Impact, brief dip, immediate second impact -- this is ringing, not a
     * deliberate second tap. */
    int fired = 0;
    fired += feed_level(&d, 1800, &t, 2, 10);
    fired += feed_level(&d, 1000, &t, 1, 10);   /* only 10ms of quiet */
    fired += feed_level(&d, 1800, &t, 2, 10);
    fired += feed_level(&d, 1000, &t, 4, 10);

    check_int("ringing does not count as a double tap", fired, 0);
}

static void test_taps_too_far_apart_do_not_pair(void)
{
    printf("two impacts further apart than the max gap do not pair\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    int fired = 0;
    fired += feed_impact(&d, &t, 1800);
    t += TAP_MAX_GAP_MS + 200;      /* well past the pairing window */
    fired += feed_impact(&d, &t, 1800);

    check_int("widely separated impacts do not pair", fired, 0);
}

static void test_gap_boundaries(void)
{
    printf("pairing window boundaries\n");

    /* Just inside the window: fires. */
    {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 1000;
        int fired = 0;
        fired += feed_impact(&d, &t, 1800);
        t += TAP_MAX_GAP_MS / 2;
        fired += feed_impact(&d, &t, 1800);
        check_int("mid-window gap fires", fired, 1);
    }

    /* A third tap after a completed double tap starts fresh, and does not
     * immediately fire again on its own. */
    {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 1000;
        feed_impact(&d, &t, 1800);
        t += 100;
        feed_impact(&d, &t, 1800);   /* fires here */
        t += 100;
        int fired = feed_impact(&d, &t, 1800);
        check_int("a lone third impact does not fire", fired, 0);
    }
}

/* ---- magnitude helper ---- */

static void test_magnitude(void)
{
    printf("acceleration magnitude\n");

    /* At rest the device reads about -1g on Z and near zero elsewhere;
     * magnitude should come out ~1000 mg regardless of sign. */
    check_int("z=-1g reads 1000mg", accel_magnitude_mg(0, 0, -4096, 4096), 1000);
    check_int("z=+1g reads 1000mg", accel_magnitude_mg(0, 0, 4096, 4096), 1000);
    check_int("x=1g reads 1000mg", accel_magnitude_mg(4096, 0, 0, 4096), 1000);

    /* The real measured rest vector: ax=-419 ay=143 az=-3969. */
    const int32_t rest = accel_magnitude_mg(-419, 143, -3969, 4096);
    check_true("measured rest vector is near 1000mg", rest > 950 && rest < 1050);

    check_int("zero reads zero", accel_magnitude_mg(0, 0, 0, 4096), 0);

    /* A 2g impact on one axis. */
    check_int("2g on one axis", accel_magnitude_mg(8192, 0, 0, 4096), 2000);
}

/*
 * Feeding a real capture of the device at rest must produce nothing. These are
 * actual samples logged from hardware.
 */
static void test_real_rest_capture(void)
{
    printf("replaying a real at-rest capture\n");

    /* (ax, ay, az) triples measured on the device sitting on a desk. */
    static const int16_t capture[][3] = {
        {-419, 143, -3969}, {-498, 0, -4099}, {0, 169, -3900},
        {-306, 179, -4148}, {0, 0, -4096}, {-420, 140, -3970},
        {-410, 150, -3980}, {-430, 130, -3960}, {0, 100, -4050},
        {-350, 160, -4000}, {-380, 120, -4020}, {-400, 145, -3990},
    };

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;
    int fired = 0;

    for (int rep = 0; rep < 50; rep++) {
        for (size_t i = 0; i < sizeof(capture) / sizeof(capture[0]); i++) {
            const int32_t mg = accel_magnitude_mg(capture[i][0], capture[i][1],
                                                  capture[i][2], 4096);
            if (tap_detector_feed(&d, mg, t)) {
                fired++;
            }
            t += 10;
        }
    }

    check_int("real at-rest data produces no taps", fired, 0);
}

int main(void)
{
    printf("double-tap detection tests\n\n");

    test_double_tap_fires();
    test_single_tap_does_not_fire();
    test_rest_never_fires();
    test_handling_does_not_fire();
    test_taps_too_close_are_one_tap();
    test_taps_too_far_apart_do_not_pair();
    test_gap_boundaries();
    test_magnitude();
    test_real_rest_capture();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
