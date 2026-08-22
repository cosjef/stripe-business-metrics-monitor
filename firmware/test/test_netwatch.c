/*
 * Connectivity watchdog: when repeated fetch failures should force a WiFi
 * reconnect, regardless of what the link state claims.
 *
 *   cd firmware/test && make && ./test_netwatch
 *
 * Background. wifi_retry.c made reconnection unbounded, but reconnection is
 * driven entirely by the STA_DISCONNECTED event -- every esp_wifi_connect()
 * call lives in that handler. If the event never fires, nothing retries.
 *
 * That happens more often than it sounds: an AP that vanishes without
 * deauthenticating (power cut, router reboot), a link that associates and
 * blackholes, a DHCP lease that expires unrenewed. In all of them the station
 * believes it is still connected, wifi_retry is never consulted, and the poll
 * loop fetches into the void forever while the screen shows stale.
 *
 * The poll loop already knows something is wrong -- freshness tracks
 * consecutive_failures -- it just had no way to act on it. This module is that
 * missing link: enough consecutive failures means the radio is suspect, so
 * force a reconnect even though nothing reported a disconnect.
 */
#include <stdbool.h>
#include <stdio.h>

#include "../main/netwatch.h"

static int failures = 0;
static int checks = 0;

static void check_true(const char *what, bool cond)
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

/* ---- when to intervene ---- */

/*
 * One or two failures are ordinary: Stripe has a bad minute, a TLS handshake
 * times out, the AP is busy. Forcing a reconnect there would make things worse,
 * because a reconnect costs several seconds of guaranteed downtime to fix a
 * problem that was about to resolve itself.
 */
static void test_transient_failures_do_not_trigger(void)
{
    printf("isolated failures do not force a reconnect\n");

    netwatch_t w;
    netwatch_init(&w);

    for (int i = 1; i < NETWATCH_FAILURES_BEFORE_RECONNECT; i++) {
        check_true("no reconnect while under the threshold",
                   !netwatch_should_reconnect(&w, i));
    }
}

/*
 * At the threshold the radio becomes the prime suspect. Everything upstream --
 * DNS, TLS, Stripe itself -- would have to be failing simultaneously and
 * continuously to produce this many consecutive failures.
 */
static void test_sustained_failures_trigger(void)
{
    printf("sustained failures force a reconnect\n");

    netwatch_t w;
    netwatch_init(&w);

    check_true("reconnect at the threshold",
               netwatch_should_reconnect(&w, NETWATCH_FAILURES_BEFORE_RECONNECT));
}

/*
 * The important safety property. Once a reconnect is triggered, it must not
 * fire again on every subsequent failure -- that would restart the radio every
 * poll cycle, guaranteeing the device never completes a fetch and turning a
 * recoverable outage into a permanent one.
 */
static void test_does_not_retrigger_every_cycle(void)
{
    printf("a reconnect is not re-triggered on every later failure\n");

    netwatch_t w;
    netwatch_init(&w);

    const int t = NETWATCH_FAILURES_BEFORE_RECONNECT;

    check_true("fires at the threshold", netwatch_should_reconnect(&w, t));
    netwatch_on_reconnect_triggered(&w);

    /* The next several failures must not re-trigger. */
    for (int i = 1; i <= 3; i++) {
        check_true("does not re-trigger immediately",
                   !netwatch_should_reconnect(&w, t + i));
    }
}

/*
 * But it must eventually try again. One reconnect may not be enough -- the AP
 * could still be booting. So it re-arms after a further run of failures,
 * spacing attempts rather than abandoning them.
 */
static void test_rearms_after_more_failures(void)
{
    printf("it re-arms so a second reconnect is possible\n");

    netwatch_t w;
    netwatch_init(&w);

    const int t = NETWATCH_FAILURES_BEFORE_RECONNECT;
    const int gap = NETWATCH_FAILURES_BETWEEN_RECONNECTS;

    check_true("first trigger", netwatch_should_reconnect(&w, t));
    netwatch_on_reconnect_triggered(&w);

    check_true("still quiet just before re-arming",
               !netwatch_should_reconnect(&w, t + gap - 1));
    check_true("re-arms after the gap",
               netwatch_should_reconnect(&w, t + gap));
}

/*
 * A success clears everything. The next outage starts from a clean slate rather
 * than inheriting a stale trigger point.
 */
static void test_success_resets(void)
{
    printf("a successful fetch resets the watchdog\n");

    netwatch_t w;
    netwatch_init(&w);

    const int t = NETWATCH_FAILURES_BEFORE_RECONNECT;

    check_true("fires", netwatch_should_reconnect(&w, t));
    netwatch_on_reconnect_triggered(&w);
    check_int("armed point recorded", netwatch_last_trigger(&w), t);

    netwatch_on_success(&w);
    check_int("cleared by success", netwatch_last_trigger(&w), 0);

    /* And a fresh outage triggers again at the normal threshold. */
    check_true("fires again on the next outage",
               netwatch_should_reconnect(&w, t));
}

/* ---- the threshold is a time budget, not just a count ---- */

/*
 * The threshold has to be crossed BEFORE the stale screen appears, or the
 * watchdog is useless -- it would only act after the user has already seen the
 * device fail.
 *
 * Failures are spaced by exponential backoff (freshness.c): 60s, 120s, 240s,
 * capped at 15 min. So the elapsed time to reach N failures is the sum of that
 * series, and it must land inside the 15 minute stale threshold.
 */
static void test_triggers_before_the_stale_screen(void)
{
    printf("the watchdog acts before the user sees a stale screen\n");

    /* Backoff series in seconds, matching freshness.c's 60s base doubling. */
    int elapsed_s = 0;
    int delay_s = 60;
    for (int i = 1; i < NETWATCH_FAILURES_BEFORE_RECONNECT; i++) {
        elapsed_s += delay_s;
        delay_s *= 2;
        if (delay_s > 900) {
            delay_s = 900;
        }
    }

    check_true("threshold is reached within the 15 minute stale window",
               elapsed_s < 15 * 60);

    /* And not so early that a brief Stripe hiccup restarts the radio. */
    check_true("but not within the first two minutes", elapsed_s >= 120);
}

int main(void)
{
    test_transient_failures_do_not_trigger();
    test_sustained_failures_trigger();
    test_does_not_retrigger_every_cycle();
    test_rearms_after_more_failures();
    test_success_resets();
    test_triggers_before_the_stale_screen();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
