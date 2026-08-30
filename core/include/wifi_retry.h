/*
 * WiFi reconnection policy.
 *
 * Split out from wifi.c so it carries no ESP-IDF dependency and can be tested
 * on the host, because the bug it fixes was invisible on the bench: it needed
 * five disconnects spread over days to appear.
 *
 * The rule is that the correct policy depends on whether the device has ever
 * successfully joined:
 *
 *   Never connected -- BOUNDED. The credentials are unproven, so a wrong
 *     password must surface on screen rather than retry forever behind a blank
 *     display. This is spec 4's "never lie" applied to connectivity.
 *
 *   Previously connected -- UNBOUNDED, with backoff. The credentials are known
 *     good, so a later disconnect means the network is down, not wrong. A
 *     router reboot must not permanently disable a desk instrument.
 *
 * The original code applied the bounded rule to both, and reset its counter
 * only on success. Five disconnects at any point in the device's lifetime --
 * accumulated across separate, individually harmless incidents -- left it in a
 * terminal state that nothing recovered from.
 */
#pragma once

#include <stdbool.h>

/*
 * Attempts allowed before giving up during first-run provisioning. Matches the
 * previous STA_MAX_RETRIES; the bound was never the problem, applying it after
 * a successful join was.
 */
#define WIFI_PROVISION_MAX_RETRIES 5

/*
 * Reconnect backoff. Doubles per consecutive failure and caps.
 *
 * The cap is deliberately well under the 15 minute stale threshold
 * (freshness.h): a device must never be showing "stale" merely because it is
 * waiting to retry. At 60s it retries at least fourteen times before data
 * could be considered stale on backoff alone.
 */
#define WIFI_RECONNECT_BASE_DELAY_MS  1000
#define WIFI_RECONNECT_MAX_DELAY_MS   60000

typedef struct {
    bool ever_connected;   /* has the device ever held an IP? */
    int consecutive_fails; /* since the last success */
} wifi_retry_t;

void wifi_retry_init(wifi_retry_t *r);

/* Call on IP_EVENT_STA_GOT_IP. */
void wifi_retry_on_connected(wifi_retry_t *r);

/* Call on WIFI_EVENT_STA_DISCONNECTED. */
void wifi_retry_on_disconnect(wifi_retry_t *r);

/* Whether to attempt another association now. */
bool wifi_retry_should_reconnect(const wifi_retry_t *r);

/*
 * Whether the device has permanently given up. Only ever true during
 * provisioning -- a previously connected device never gives up.
 */
bool wifi_retry_has_given_up(const wifi_retry_t *r);

/* How long to wait before the next attempt, in milliseconds. */
int wifi_retry_delay_ms(const wifi_retry_t *r);

/* Whether a successful join has ever happened. */
bool wifi_retry_ever_connected(const wifi_retry_t *r);
