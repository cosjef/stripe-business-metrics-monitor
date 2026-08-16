/*
 * State selection: which screen the device shows, and why (spec 6.2).
 *
 *   cd firmware/test && make && ./test_state
 *
 * The device has four display states -- setup, auth error, stale, and normal
 * rotation -- and they are not independent. They form a precedence order, and
 * getting that order wrong is not a cosmetic bug: it sends the reader to debug
 * the wrong problem. A stale MRR shown when the real fault is a revoked key
 * looks like a network blip, so the owner waits instead of re-issuing the key.
 *
 * Every transition here was verified by hand on hardware using the
 * FORCE_STALE_AFTER_S / FORCE_AUTH_FAIL_AFTER_S switches in main.c. Hand
 * verification does not survive a refactor, which is what this file is for.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../main/state.h"

static int failures = 0;
static int checks = 0;

static const char *state_name(display_state_t s)
{
    switch (s) {
    case DISPLAY_SETUP:      return "SETUP";
    case DISPLAY_AUTH_ERROR: return "AUTH_ERROR";
    case DISPLAY_BATTERY:    return "BATTERY";
    case DISPLAY_STALE:      return "STALE";
    case DISPLAY_ROTATION:   return "ROTATION";
    }
    return "?";
}

static void check_state(const char *what, display_state_t got, display_state_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %s, want %s\n",
               what, state_name(got), state_name(want));
    }
}

/* ---- precedence ---- */

/*
 * The four states in isolation. These are the easy cases; the ordering tests
 * below are the ones that actually matter.
 */
static void test_states_in_isolation(void)
{
    printf("each state on its own (spec 6.2)\n");

    const device_status_t normal = {
        .provisioned = true, .auth_failed = false, .stale = false,
    };
    check_state("provisioned, healthy, fresh -> rotation",
                display_state(&normal), DISPLAY_ROTATION);

    const device_status_t unprovisioned = {
        .provisioned = false, .auth_failed = false, .stale = false,
    };
    check_state("not provisioned -> setup",
                display_state(&unprovisioned), DISPLAY_SETUP);

    const device_status_t auth = {
        .provisioned = true, .auth_failed = true, .stale = false,
    };
    check_state("key rejected -> auth error",
                display_state(&auth), DISPLAY_AUTH_ERROR);

    const device_status_t stale = {
        .provisioned = true, .auth_failed = false, .stale = true,
    };
    check_state("data too old -> stale",
                display_state(&stale), DISPLAY_STALE);
}

/*
 * State B outranks State A.
 *
 * Stale says "this number is old". An auth failure says "there will be no more
 * numbers until you act". The second is strictly more useful, and a revoked key
 * always drags the data stale behind it -- so this combination is not
 * hypothetical, it is what every auth failure looks like after 15 minutes.
 */
static void test_auth_outranks_stale(void)
{
    printf("auth error outranks stale (spec 6.2)\n");

    const device_status_t both = {
        .provisioned = true, .auth_failed = true, .stale = true,
    };
    check_state("auth failure wins over stale",
                display_state(&both), DISPLAY_AUTH_ERROR);
}

/*
 * Setup outranks everything.
 *
 * An unprovisioned device has no key to reject and no data to go stale, so
 * these combinations should be unreachable. Assert them anyway: if a future
 * refactor leaves a stale flag set across a factory reset, the device must
 * still show the setup screen rather than a stale number the new owner has no
 * way to interpret.
 */
static void test_setup_outranks_everything(void)
{
    printf("setup outranks all other states (spec 6.2)\n");

    const device_status_t setup_and_auth = {
        .provisioned = false, .auth_failed = true, .stale = false,
    };
    check_state("setup wins over auth error",
                display_state(&setup_and_auth), DISPLAY_SETUP);

    const device_status_t setup_and_stale = {
        .provisioned = false, .auth_failed = false, .stale = true,
    };
    check_state("setup wins over stale",
                display_state(&setup_and_stale), DISPLAY_SETUP);

    const device_status_t everything = {
        .provisioned = false, .auth_failed = true, .stale = true,
    };
    check_state("setup wins over everything",
                display_state(&everything), DISPLAY_SETUP);
}

/*
 * Exhaustive: all eight combinations of the three flags. Small enough to
 * enumerate, so there is no reason to leave any of it to inference.
 */
static void test_all_combinations(void)
{
    printf("all 16 flag combinations resolve as specified\n");

    for (int p = 0; p < 2; p++) {
        for (int a = 0; a < 2; a++) {
            for (int b = 0; b < 2; b++) {
                for (int s = 0; s < 2; s++) {
                    const device_status_t st = {
                        .provisioned = (p != 0),
                        .auth_failed = (a != 0),
                        .battery_warn = (b != 0),
                        .stale = (s != 0),
                    };

                    display_state_t want;
                    if (!p) {
                        want = DISPLAY_SETUP;
                    } else if (a) {
                        want = DISPLAY_AUTH_ERROR;
                    } else if (b) {
                        want = DISPLAY_BATTERY;
                    } else if (s) {
                        want = DISPLAY_STALE;
                    } else {
                        want = DISPLAY_ROTATION;
                    }

                    char what[96];
                    snprintf(what, sizeof(what),
                             "provisioned=%d auth=%d battery=%d stale=%d",
                             p, a, b, s);
                    check_state(what, display_state(&st), want);
                }
            }
        }
    }
}

/*
 * Battery warnings sit between auth error and stale.
 *
 * Below auth error: a rejected key means no more numbers at all, which outlives
 * any battery state -- and the fix (re-issue the key) is unrelated to power.
 *
 * Above stale: a dying battery explains the staleness and will get worse on its
 * own. Telling the owner their data is 20 minutes old, when the real story is
 * that the device is minutes from shutting down, sends them to debug the wrong
 * thing -- the same reasoning that puts auth error above stale.
 */
static void test_battery_precedence(void)
{
    printf("battery warning sits between auth error and stale (spec 6.2)\n");

    const device_status_t low = {
        .provisioned = true, .battery_warn = true,
    };
    check_state("low battery alone -> battery screen",
                display_state(&low), DISPLAY_BATTERY);

    const device_status_t low_and_stale = {
        .provisioned = true, .battery_warn = true, .stale = true,
    };
    check_state("battery outranks stale",
                display_state(&low_and_stale), DISPLAY_BATTERY);

    const device_status_t low_and_auth = {
        .provisioned = true, .battery_warn = true, .auth_failed = true,
    };
    check_state("auth error outranks battery",
                display_state(&low_and_auth), DISPLAY_AUTH_ERROR);

    const device_status_t unprovisioned = {
        .provisioned = false, .battery_warn = true,
    };
    check_state("setup outranks battery",
                display_state(&unprovisioned), DISPLAY_SETUP);

    /* The warning takes the screen over: there is one thing to say, and
     * cycling metrics behind a dying battery would be noise. */
    checks++;
    if (display_state_rotates(DISPLAY_BATTERY)) {
        failures++;
        printf("  FAIL battery screen must not rotate\n");
    }
}

/* ---- transitions ---- */

/*
 * The transitions themselves, walked as sequences rather than checked as
 * isolated states. Each step asserts both where the device lands and that it
 * got there from the right place.
 */
static void test_transition_sequences(void)
{
    printf("state transitions interrupt and resume correctly\n");

    /* First boot: setup -> rotation, the happy path a new owner walks. */
    device_status_t st = { .provisioned = false };
    check_state("boot unprovisioned", display_state(&st), DISPLAY_SETUP);

    st.provisioned = true;
    check_state("credentials accepted -> rotation",
                display_state(&st), DISPLAY_ROTATION);

    /* Network drops: rotation -> stale -> back to rotation. Stale must not be
     * a trap; recovery is the whole point of showing a retry countdown. */
    st.stale = true;
    check_state("data ages out -> stale", display_state(&st), DISPLAY_STALE);

    st.stale = false;
    check_state("poll succeeds -> rotation resumes",
                display_state(&st), DISPLAY_ROTATION);

    /* Key revoked mid-run. Auth failure is sticky in the firmware -- a revoked
     * key never recovers on its own -- but display_state() is a pure function
     * of current flags, so clearing the flag here models the owner issuing a
     * new key, not the device healing itself. */
    st.auth_failed = true;
    check_state("key revoked -> auth error",
                display_state(&st), DISPLAY_AUTH_ERROR);

    st.stale = true;
    check_state("data ages out behind the auth failure, still auth error",
                display_state(&st), DISPLAY_AUTH_ERROR);

    st.auth_failed = false;
    check_state("new key accepted, data still old -> stale",
                display_state(&st), DISPLAY_STALE);

    st.stale = false;
    check_state("fresh poll -> rotation resumes",
                display_state(&st), DISPLAY_ROTATION);

    /* Factory reset from any state returns to setup. */
    st.auth_failed = true;
    st.stale = true;
    st.provisioned = false;
    check_state("factory reset -> setup", display_state(&st), DISPLAY_SETUP);
}

/*
 * Rotation must keep running underneath the stale screen.
 *
 * Spec 6.2 is explicit that stale is "not a takeover screen": the label and
 * value keep changing, but every value dims and the age shows in amber. If
 * stale froze the rotation, a reader glancing over would see one screen stuck
 * and assume the device had crashed rather than gone stale.
 */
static void test_stale_does_not_freeze_rotation(void)
{
    printf("stale dims the deck without stopping it (spec 6.2)\n");

    const device_status_t stale = {
        .provisioned = true, .auth_failed = false, .stale = true,
    };

    checks++;
    if (!display_state_rotates(display_state(&stale))) {
        failures++;
        printf("  FAIL stale still advances through screens\n");
    }

    const device_status_t normal = {
        .provisioned = true, .auth_failed = false, .stale = false,
    };
    checks++;
    if (!display_state_rotates(display_state(&normal))) {
        failures++;
        printf("  FAIL rotation advances through screens\n");
    }

    /* Auth error and setup are takeover screens: there is one thing to say and
     * cycling through metrics behind it would be noise. */
    const device_status_t auth = {
        .provisioned = true, .auth_failed = true, .stale = false,
    };
    checks++;
    if (display_state_rotates(display_state(&auth))) {
        failures++;
        printf("  FAIL auth error must not rotate\n");
    }

    const device_status_t setup = { .provisioned = false };
    checks++;
    if (display_state_rotates(display_state(&setup))) {
        failures++;
        printf("  FAIL setup must not rotate\n");
    }
}

int main(void)
{
    test_states_in_isolation();
    test_auth_outranks_stale();
    test_setup_outranks_everything();
    test_battery_precedence();
    test_all_combinations();
    test_transition_sequences();
    test_stale_does_not_freeze_rotation();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
