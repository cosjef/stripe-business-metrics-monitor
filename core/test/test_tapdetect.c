/*
 * Host tests for single-tap detection.
 *
 *   cd firmware/test && make && ./test_tapdetect
 *
 * All baselines come from measurements on the real device:
 *   - at rest:            ~1000 mg, noise within ~1020 mg
 *   - case ringing:       1450-2700 mg, impacts 20-60 ms apart
 *   - deliberate taps:    6000-10433 mg
 */
#include <stdio.h>

#include "../include/tapdetect.h"

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

/* A tap: a spike lasting ~20ms, then settling back to gravity. */
static int feed_tap(tap_detector_t *d, uint32_t *t, int32_t peak_mg)
{
    int fired = 0;
    fired += feed_level(d, peak_mg, t, 2, 10);
    fired += feed_level(d, 1000, t, 4, 10);
    return fired;
}

/* ---- core behavior ---- */

static void test_deliberate_tap_fires(void)
{
    printf("a deliberate tap fires exactly once\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    check_int("7000mg tap fires once", feed_tap(&d, &t, 7000), 1);

    /* Real measured peaks must all register. */
    const int32_t measured[] = {6123, 7004, 7157, 7716, 7859, 7906, 8332,
                                8919, 9893, 10433};
    for (size_t i = 0; i < sizeof(measured) / sizeof(measured[0]); i++) {
        tap_detector_t d2;
        tap_detector_init(&d2);
        uint32_t t2 = 0;
        char what[64];
        snprintf(what, sizeof(what), "measured tap %ld mg fires",
                 (long)measured[i]);
        check_int(what, feed_tap(&d2, &t2, measured[i]), 1);
    }
}

/*
 * The lockout is what stops one physical tap advancing several screens: the
 * case rings for a few hundred ms after a strike.
 */
static void test_ringing_produces_one_tap(void)
{
    printf("case ringing after a tap produces only one tap\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    int fired = 0;
    /* The strike. */
    fired += feed_level(&d, 8000, &t, 2, 10);
    fired += feed_level(&d, 1000, &t, 2, 10);
    /* Ringing: repeated impacts 20-60ms apart, at the magnitudes measured. */
    fired += feed_level(&d, 4200, &t, 1, 20);
    fired += feed_level(&d, 1200, &t, 1, 10);
    fired += feed_level(&d, 5100, &t, 1, 30);
    fired += feed_level(&d, 1100, &t, 1, 10);
    fired += feed_level(&d, 4300, &t, 1, 20);
    fired += feed_level(&d, 1000, &t, 10, 10);

    check_int("one strike plus ringing fires once", fired, 1);
}

/*
 * Two deliberate taps, separated by more than the lockout, must both count --
 * a user tapping twice expects to advance twice.
 */
static void test_separate_taps_both_fire(void)
{
    printf("two separated taps both fire\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;

    int fired = 0;
    fired += feed_tap(&d, &t, 7000);
    /* feed_tap only advances 60ms, so wait out the rest of the lockout. */
    t += TAP_LOCKOUT_MS + 100;
    fired += feed_tap(&d, &t, 7000);

    check_int("two well-separated taps fire twice", fired, 2);
}

/*
 * The lockout boundary itself: a second tap just inside it must be swallowed,
 * and just outside it must register. This is what keeps one strike from
 * advancing several screens while still letting a user tap twice on purpose.
 */
static void test_lockout_boundary(void)
{
    printf("lockout boundary\n");

    /* Second tap while still locked out: ignored. */
    {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 1000;
        int fired = feed_tap(&d, &t, 7000);
        t += TAP_LOCKOUT_MS / 2;
        fired += feed_tap(&d, &t, 7000);
        check_int("tap inside lockout is ignored", fired, 1);
    }

    /* Second tap after the lockout: registers. */
    {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 1000;
        int fired = feed_tap(&d, &t, 7000);
        t += TAP_LOCKOUT_MS + 50;
        fired += feed_tap(&d, &t, 7000);
        check_int("tap after lockout registers", fired, 2);
    }
}

/* ---- rejection ---- */

static void test_rest_never_fires(void)
{
    printf("resting on a desk never fires\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;

    const int32_t rest[] = {995, 1000, 1005, 1010, 1020, 998, 1002};

    int fired = 0;
    for (int rep = 0; rep < 200; rep++) {
        for (size_t i = 0; i < sizeof(rest) / sizeof(rest[0]); i++) {
            if (tap_detector_feed(&d, rest[i], t)) {
                fired++;
            }
            t += 10;
        }
    }

    check_int("1400 resting samples produce no taps", fired, 0);
}

/*
 * Incidental knocks and ringing, measured while tapping normally. All must be
 * rejected: these are what the device sees when it is bumped or set down, and
 * the earlier double-tap version fired on them.
 */
static void test_soft_knocks_rejected(void)
{
    printf("soft knocks and ringing are rejected\n");

    /* Settling and light-contact magnitudes measured on hardware. All sit
     * below the deliberate-tap population and must be rejected. */
    const int32_t soft[] = {1453, 1486, 1499, 1510, 1527, 1558, 1599, 1631,
                            1678, 1699, 1750, 1784, 1858, 1917, 1943, 1995,
                            1997, 2000, 2038, 2156, 2158, 2159, 2182, 2352,
                            2582, 2669, 2681, 2996, 3000, 3004, 3937, 3954};

    for (size_t i = 0; i < sizeof(soft) / sizeof(soft[0]); i++) {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 0;
        char what[72];
        snprintf(what, sizeof(what), "%ld mg knock rejected", (long)soft[i]);
        check_int(what, feed_tap(&d, &t, soft[i]), 0);
    }
}

/*
 * The taps that the original 4000 mg threshold was silently dropping. These
 * are real taps measured during normal use; every one must now register, or
 * the device goes back to feeling like it needs several taps.
 */
static void test_soft_real_taps_accepted(void)
{
    printf("softer real taps are accepted\n");

    const int32_t soft_taps[] = {4005, 4267, 4549, 4764, 7002,
                                 7943, 7973, 8244, 8604};

    for (size_t i = 0; i < sizeof(soft_taps) / sizeof(soft_taps[0]); i++) {
        tap_detector_t d;
        tap_detector_init(&d);
        uint32_t t = 0;
        char what[72];
        snprintf(what, sizeof(what), "%ld mg real tap accepted", (long)soft_taps[i]);
        check_int(what, feed_tap(&d, &t, soft_taps[i]), 1);
    }
}

/*
 * The gap between the loudest rejected knock and the softest accepted tap.
 * If a future threshold change closes this gap, the device becomes either
 * unresponsive or trigger-happy -- so assert the separation explicitly.
 */
static void test_threshold_separates_populations(void)
{
    printf("threshold separates knocks from taps\n");

    const int32_t loudest_knock = 3954;
    const int32_t softest_tap = 4005;

    check_true("threshold above the loudest measured knock",
               TAP_THRESHOLD_MG > loudest_knock);
    check_true("threshold below the softest measured real tap",
               TAP_THRESHOLD_MG < softest_tap);
}

static void test_handling_does_not_fire(void)
{
    printf("picking the device up does not fire\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;

    /* A slow swing up to 2.5g and back -- below threshold throughout. */
    int fired = 0;
    for (int mg = 1000; mg <= 2500; mg += 50) {
        if (tap_detector_feed(&d, mg, t)) fired++;
        t += 20;
    }
    for (int mg = 2500; mg >= 1000; mg -= 50) {
        if (tap_detector_feed(&d, mg, t)) fired++;
        t += 20;
    }

    check_int("slow handling does not fire", fired, 0);
}

/*
 * A sustained high reading (the device held against something vibrating, or
 * shaken continuously) fires at most once per lockout, never faster.
 *
 * It deliberately does NOT fire only once: continuous shaking advancing the
 * screen repeatedly is reasonable behavior, and suppressing it entirely would
 * require distinguishing "still the same disturbance" from "a new one", which
 * the ringing data shows is not reliably possible.
 */
static void test_sustained_high_is_rate_limited(void)
{
    printf("a sustained high reading is rate-limited to the lockout\n");

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 0;

    /* Held above threshold for 2 seconds. */
    const int fired = feed_level(&d, 6000, &t, 200, 10);

    /* 2000ms of sustained input at a 500ms lockout allows at most 5 taps
     * (one immediately, then one per lockout period). */
    const int max_expected = 2000 / TAP_LOCKOUT_MS + 1;
    char what[96];
    snprintf(what, sizeof(what), "2s sustained fires <= %d times (got %d)",
             max_expected, fired);
    check_true(what, fired >= 1 && fired <= max_expected);
}

/* ---- replay of the real capture ---- */

/*
 * The 48 impacts logged during natural tapping on hardware. Under the old
 * double-tap logic this produced 14 accepted gestures, which felt random.
 * With a 4000 mg threshold only the deliberate strikes should survive.
 */
static void test_real_capture_replay(void)
{
    printf("replaying the real tap capture\n");

    /* (magnitude_mg, ms_since_previous) as logged. */
    static const int32_t capture[][2] = {
        {7004, 0},   {3795, 280},  {1784, 90},   {6123, 650},  {7157, 230},
        {3126, 280}, {1558, 670},  {1539, 1170}, {2352, 150},  {1494, 20},
        {4030, 210}, {2681, 700},  {1858, 800},  {3966, 30},   {1678, 150},
        {3799, 870}, {2582, 730},  {7906, 240},  {1917, 1840}, {1510, 1380},
        {3950, 350}, {7859, 510},  {4239, 739},  {8919, 741},  {1681, 580},
        {1618, 150}, {3986, 290},  {6421, 1020}, {1699, 650},  {1757, 1870},
        {1499, 150}, {8332, 570},  {6923, 280},  {2182, 740},  {9893, 1760},
        {2038, 790}, {7716, 1249}, {4030, 1251}, {1599, 2070}, {1486, 690},
        {1453, 60},  {10433, 239}, {1505, 141},  {1527, 760},  {1750, 30},
        {2669, 220}, {1522, 170},  {1631, 439},
    };

    tap_detector_t d;
    tap_detector_init(&d);
    uint32_t t = 1000;
    int fired = 0;

    for (size_t i = 0; i < sizeof(capture) / sizeof(capture[0]); i++) {
        t += (uint32_t)capture[i][1];
        /* The impact itself, then a settle back toward rest. */
        if (tap_detector_feed(&d, capture[i][0], t)) fired++;
        t += 10;
        if (tap_detector_feed(&d, 1000, t)) fired++;
        t += 10;
    }

    /* Count how many impacts in the capture are genuinely above threshold --
     * that is the upper bound on what should be accepted. The lockout may
     * legitimately swallow some of them.
     *
     * Note this capture predates the threshold revision and was recorded with
     * firmer tapping, so the absolute count is less meaningful than the
     * invariant: never accept more taps than there were qualifying impacts. */
    int qualifying = 0;
    for (size_t i = 0; i < sizeof(capture) / sizeof(capture[0]); i++) {
        if (capture[i][0] >= TAP_THRESHOLD_MG) {
            qualifying++;
        }
    }

    check_true("never accepts more taps than qualifying impacts", fired <= qualifying);
    check_true("accepts a reasonable share of qualifying impacts",
               fired >= qualifying / 2);
    printf("    (accepted %d of %d qualifying impacts, from 48 total)\n",
           fired, qualifying);
}

/* ---- magnitude helper ---- */

static void test_magnitude(void)
{
    printf("acceleration magnitude\n");

    check_int("z=-1g reads 1000mg", accel_magnitude_mg(0, 0, -4096, 4096), 1000);
    check_int("z=+1g reads 1000mg", accel_magnitude_mg(0, 0, 4096, 4096), 1000);
    check_int("x=1g reads 1000mg", accel_magnitude_mg(4096, 0, 0, 4096), 1000);

    const int32_t rest = accel_magnitude_mg(-419, 143, -3969, 4096);
    check_true("measured rest vector is near 1000mg", rest > 950 && rest < 1050);

    check_int("zero reads zero", accel_magnitude_mg(0, 0, 0, 4096), 0);
    check_int("2g on one axis", accel_magnitude_mg(8192, 0, 0, 4096), 2000);
}

int main(void)
{
    printf("single-tap detection tests\n\n");

    test_deliberate_tap_fires();
    test_ringing_produces_one_tap();
    test_separate_taps_both_fire();
    test_lockout_boundary();
    test_rest_never_fires();
    test_soft_knocks_rejected();
    test_soft_real_taps_accepted();
    test_threshold_separates_populations();
    test_handling_does_not_fire();
    test_sustained_high_is_rate_limited();
    test_real_capture_replay();
    test_magnitude();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
