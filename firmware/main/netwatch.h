/*
 * Connectivity watchdog.
 *
 * wifi_retry.c made reconnection unbounded, but reconnection is driven entirely
 * by the STA_DISCONNECTED event -- every esp_wifi_connect() call lives in that
 * handler. When the event never fires, nothing retries.
 *
 * That is not a rare case. An AP that vanishes without deauthenticating, a link
 * that associates and then blackholes, a DHCP lease that expires unrenewed --
 * in all of them the station still believes it is connected. wifi_retry is
 * never consulted, and the poll loop fetches into the void indefinitely while
 * the screen shows stale.
 *
 * The poll loop already knew something was wrong: freshness tracks
 * consecutive_failures. It simply had no way to act on it, because main.c never
 * touched the WiFi layer after boot. This module is that missing link.
 *
 * Pure logic, host-tested. The rule is a policy decision over a counter that
 * already exists, not new bookkeeping.
 */
#pragma once

#include <stdbool.h>

/*
 * Consecutive fetch failures before the radio becomes the prime suspect.
 *
 * Derived from the backoff schedule, not picked round. freshness.c spaces
 * retries 60s, 120s, 240s, 480s..., so failures land at:
 *
 *     1 -> 0m    2 -> 1m    3 -> 3m    4 -> 7m    5 -> 15m
 *
 * The stale screen appears at 15 minutes. Five would therefore fire at exactly
 * the moment the user already sees the failure, which makes the watchdog
 * pointless -- it has to act BEFORE the symptom, not with it. Four fires at
 * seven minutes: comfortably inside the window, and well past the couple of
 * minutes a transient Stripe or DNS problem occupies.
 *
 * Lower would restart the radio over hiccups that were about to resolve, and a
 * reconnect costs several seconds of guaranteed downtime.
 *
 * If the backoff schedule in freshness.c ever changes, this needs rechecking --
 * test_netwatch asserts the relationship rather than the number.
 */
#define NETWATCH_FAILURES_BEFORE_RECONNECT 4

/*
 * Further failures before trying another reconnect.
 *
 * Without this the watchdog would fire on every poll once past the threshold,
 * restarting the radio continuously and guaranteeing no fetch ever completes --
 * turning a recoverable outage into a permanent one. With it, a second attempt
 * still happens if the first did not help, just spaced out: the AP may itself
 * be rebooting.
 */
#define NETWATCH_FAILURES_BETWEEN_RECONNECTS 4

typedef struct {
    /* Failure count at which a reconnect was last forced; 0 if none since the
     * last success. */
    int last_trigger;
} netwatch_t;

void netwatch_init(netwatch_t *w);

/*
 * Whether to force a WiFi reconnect now, given the current run of consecutive
 * fetch failures. Does not mutate; call netwatch_on_reconnect_triggered() if
 * you act on it.
 */
bool netwatch_should_reconnect(const netwatch_t *w, int consecutive_failures);

/* Record that a reconnect was forced, so it is not repeated every cycle. */
void netwatch_on_reconnect_triggered(netwatch_t *w);

/* A fetch succeeded: clear state so the next outage starts fresh. */
void netwatch_on_success(netwatch_t *w);

/* Failure count at the last forced reconnect, 0 if none. */
int netwatch_last_trigger(const netwatch_t *w);
