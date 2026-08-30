/*
 * WiFi reconnection policy.
 *
 *   cd firmware/test && make && ./test_wifi_retry
 *
 * The device stopped updating and sat on the stale screen indefinitely. Root
 * cause was here: wifi.c retried a failed association five times and then set
 * WIFI_STATE_FAILED permanently. The counter reset only on a successful join,
 * and nothing in main.c ever read the failed state, so no code path called
 * esp_wifi_connect() again. Five disconnects at any point in the device's life
 * -- a router reboot, a channel change, a few seconds of interference --
 * bricked connectivity until someone power-cycled it.
 *
 * That bound was deliberate for FIRST-RUN provisioning, where refusing to
 * retry forever is correct: a wrong password must surface on screen rather
 * than spin silently. The mistake was applying the same rule to a device that
 * had already been on the network for days.
 *
 * So the policy is now split by whether the device has ever connected:
 *
 *   never connected  -> bounded. A wrong password is a real failure to show.
 *   was connected    -> unbounded, with backoff. The credentials are known
 *                       good, so the network is down, not wrong.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../include/wifi_retry.h"

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

/* ---- provisioning: bounded, as before ---- */

/*
 * Before the first successful join the bound still applies. A device given the
 * wrong password must stop and say so, not retry forever behind a blank
 * screen -- that is spec 4's "never lie" applied to connectivity.
 */
static void test_first_run_stays_bounded(void)
{
    printf("before ever connecting, retries stay bounded\n");

    wifi_retry_t r;
    wifi_retry_init(&r);

    for (int i = 1; i <= WIFI_PROVISION_MAX_RETRIES; i++) {
        check_true("retry is allowed while under the bound",
                   wifi_retry_should_reconnect(&r));
        wifi_retry_on_disconnect(&r);
    }

    check_true("after the bound, provisioning gives up",
               !wifi_retry_should_reconnect(&r));
    check_true("and reports failure so the screen can say so",
               wifi_retry_has_given_up(&r));
}

/* ---- steady state: never give up ---- */

/*
 * The actual fix. Once the device has held an IP, the credentials are proven,
 * so any later disconnect is the network's problem and will likely resolve.
 * Giving up permanently is never the right answer.
 */
static void test_after_connecting_never_gives_up(void)
{
    printf("once connected, reconnection is unbounded\n");

    wifi_retry_t r;
    wifi_retry_init(&r);
    wifi_retry_on_connected(&r);

    /* Far past the old limit of 5, and past any plausible outage. */
    for (int i = 0; i < 500; i++) {
        wifi_retry_on_disconnect(&r);
        if (!wifi_retry_should_reconnect(&r)) {
            failures++;
            printf("  FAIL gave up after %d disconnects\n", i + 1);
            checks++;
            return;
        }
    }
    checks++;
    check_true("still trying after 500 disconnects",
               wifi_retry_should_reconnect(&r));
    check_true("and never reports having given up",
               !wifi_retry_has_given_up(&r));
}

/*
 * The specific scenario that broke the device: a handful of failures spread
 * over a long uptime. Each one individually is unremarkable; under the old
 * policy they accumulated into a permanent stop.
 */
static void test_intermittent_failures_do_not_accumulate(void)
{
    printf("intermittent drops do not accumulate into a permanent stop\n");

    wifi_retry_t r;
    wifi_retry_init(&r);
    wifi_retry_on_connected(&r);

    /* Six separate incidents, each recovering. Under the old code the fifth
     * would have been fatal. */
    for (int incident = 0; incident < 6; incident++) {
        wifi_retry_on_disconnect(&r);
        check_true("reconnects after an isolated drop",
                   wifi_retry_should_reconnect(&r));
        wifi_retry_on_connected(&r);
    }

    check_true("never gave up across six incidents",
               !wifi_retry_has_given_up(&r));
    check_int("and the backoff is reset by each success",
              wifi_retry_delay_ms(&r), 0);
}

/* ---- backoff ---- */

/*
 * Unbounded retry must not mean hammering the radio. The delay grows with
 * consecutive failures and caps, so a long outage costs little power and the
 * AP is not flooded.
 */
static void test_backoff_grows_and_caps(void)
{
    printf("reconnect backoff grows and caps\n");

    wifi_retry_t r;
    wifi_retry_init(&r);
    wifi_retry_on_connected(&r);

    check_int("no delay before the first failure", wifi_retry_delay_ms(&r), 0);

    wifi_retry_on_disconnect(&r);
    const int first = wifi_retry_delay_ms(&r);
    check_true("first retry is immediate or near-immediate", first <= 1000);

    int prev = first;
    for (int i = 0; i < 12; i++) {
        wifi_retry_on_disconnect(&r);
        const int d = wifi_retry_delay_ms(&r);
        check_true("delay never decreases while failing", d >= prev);
        prev = d;
    }

    check_int("delay caps rather than growing without bound",
              prev, WIFI_RECONNECT_MAX_DELAY_MS);

    /* A cap that exceeded the stale threshold would mean the device could sit
     * stale purely because it was waiting to retry. */
    check_true("the cap is well under the 15 minute stale threshold",
               WIFI_RECONNECT_MAX_DELAY_MS < 15 * 60 * 1000);
}

/*
 * Success clears the backoff, so a device that drops once an hour retries
 * quickly each time rather than inheriting yesterday's delay.
 */
static void test_success_resets_backoff(void)
{
    printf("a successful join resets the backoff\n");

    wifi_retry_t r;
    wifi_retry_init(&r);
    wifi_retry_on_connected(&r);

    for (int i = 0; i < 8; i++) {
        wifi_retry_on_disconnect(&r);
    }
    check_true("backoff has grown", wifi_retry_delay_ms(&r) > 1000);

    wifi_retry_on_connected(&r);
    check_int("and is cleared by success", wifi_retry_delay_ms(&r), 0);
}

/*
 * A device that provisioned, connected, then later failed must NOT fall back
 * to the bounded provisioning policy -- that was the bug, and it would be easy
 * to reintroduce by resetting the wrong field.
 */
static void test_connected_flag_is_sticky(void)
{
    printf("having once connected is remembered across failures\n");

    wifi_retry_t r;
    wifi_retry_init(&r);
    check_true("starts as never-connected", !wifi_retry_ever_connected(&r));

    wifi_retry_on_connected(&r);
    check_true("records the first connection", wifi_retry_ever_connected(&r));

    for (int i = 0; i < 50; i++) {
        wifi_retry_on_disconnect(&r);
    }
    check_true("still remembers it after many failures",
               wifi_retry_ever_connected(&r));
    check_true("so it stays in unbounded mode",
               wifi_retry_should_reconnect(&r));
}

int main(void)
{
    test_first_run_stays_bounded();
    test_after_connecting_never_gives_up();
    test_intermittent_failures_do_not_accumulate();
    test_backoff_grows_and_caps();
    test_success_resets_backoff();
    test_connected_flag_is_sticky();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
