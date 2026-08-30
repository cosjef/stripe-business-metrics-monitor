/*
 * When the next Stripe fetch is due.
 *
 * This was once an inline condition in loop():
 *
 *     if (s_have_data && now - s_last_refresh >= REFRESH_MS)
 *
 * The s_have_data guard was there to stop the deck polling before it had
 * anything to show. But it also meant a device whose *first* fetch failed
 * never retried: no data, so the condition was false forever, so no fetch,
 * so still no data. The only reason a fresh unit recovered was that the
 * portal's key-validation fetch happened to populate the values first.
 *
 * Splitting the policy out gives it a name and a test. The rule now is
 * that having data gates the *interval*, not the retry: with data, poll on
 * the normal cycle; without, retry sooner, because a device with an empty
 * screen has more reason to try again, not less.
 */
#ifndef REFRESH_H
#define REFRESH_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* spec 7.1: poll every five minutes once there is something on screen. */
#define REFRESH_INTERVAL_MS (5 * 60 * 1000)

/* Retry cadence before the first success. Short enough that a transient
 * failure at setup clears while the owner is still watching, long enough
 * not to hammer the API behind a genuinely broken key or network. */
#define REFRESH_RETRY_MS (30 * 1000)

/*
 * True when a fetch should be started now.
 *
 * now_ms and last_ms are millis() values and may wrap; the subtraction is
 * done in uint32_t so that the wrap is well defined and a fetch is not
 * delayed by 49 days at the boundary.
 */
bool refresh_due(uint32_t now_ms, uint32_t last_ms, bool have_data);

#ifdef __cplusplus
}
#endif

#endif /* REFRESH_H */
