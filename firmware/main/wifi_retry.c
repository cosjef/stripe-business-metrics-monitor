/*
 * WiFi reconnection policy. See wifi_retry.h for why it is split by whether
 * the device has ever connected.
 */
#include "wifi_retry.h"

void wifi_retry_init(wifi_retry_t *r)
{
    r->ever_connected = false;
    r->consecutive_fails = 0;
}

void wifi_retry_on_connected(wifi_retry_t *r)
{
    /* Sticky: once proven, the credentials stay proven. Clearing this on a
     * later failure would drop the device back into bounded provisioning mode,
     * which is the bug this module exists to prevent. */
    r->ever_connected = true;
    r->consecutive_fails = 0;
}

void wifi_retry_on_disconnect(wifi_retry_t *r)
{
    /* Saturate rather than overflow: a device left on a dead network for
     * months would otherwise wrap this counter. */
    if (r->consecutive_fails < 1000000) {
        r->consecutive_fails++;
    }
}

bool wifi_retry_should_reconnect(const wifi_retry_t *r)
{
    if (r->ever_connected) {
        /* Known-good credentials: the network is down, not wrong. Keep
         * trying, and let the backoff keep it cheap. */
        return true;
    }

    return r->consecutive_fails < WIFI_PROVISION_MAX_RETRIES;
}

bool wifi_retry_has_given_up(const wifi_retry_t *r)
{
    return !wifi_retry_should_reconnect(r);
}

int wifi_retry_delay_ms(const wifi_retry_t *r)
{
    if (r->consecutive_fails <= 0) {
        return 0;
    }

    /* Double per failure, starting at the base delay on the second attempt so
     * the first reconnect is effectively immediate -- most drops are momentary
     * and recover at once. Shift rather than pow, and stop shifting well
     * before the cap so nothing overflows. */
    const int shift = r->consecutive_fails - 1;
    if (shift >= 20) {
        return WIFI_RECONNECT_MAX_DELAY_MS;
    }

    const long delay = (long)WIFI_RECONNECT_BASE_DELAY_MS * (1L << shift);
    if (delay >= WIFI_RECONNECT_MAX_DELAY_MS) {
        return WIFI_RECONNECT_MAX_DELAY_MS;
    }

    return (int)delay;
}

bool wifi_retry_ever_connected(const wifi_retry_t *r)
{
    return r->ever_connected;
}
