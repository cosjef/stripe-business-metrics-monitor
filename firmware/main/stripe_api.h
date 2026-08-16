/*
 * Stripe API client.
 *
 * Three GET calls, no webhooks -- webhooks would need an inbound public
 * endpoint, which is exactly the coupling this design avoids (spec 7.1).
 *
 * TLS uses ESP-IDF's certificate bundle (Mozilla roots, maintained by
 * Espressif) rather than a pinned Stripe root. Pinning is smaller and narrows
 * trust, but a root rotation would brick every device in the field until
 * firmware is updated -- unacceptable for a sold appliance.
 */
#pragma once

#include "esp_err.h"
#include "events.h"
#include "mrr.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    STRIPE_OK = 0,
    STRIPE_ERR_NETWORK,      /* could not reach the API at all */
    STRIPE_ERR_TLS,          /* handshake or certificate failure */
    STRIPE_ERR_UNAUTHORIZED, /* 401: key revoked, wrong scope, or account gone */
    STRIPE_ERR_RATE_LIMITED, /* 429 */
    STRIPE_ERR_SERVER,       /* 5xx */
    STRIPE_ERR_BAD_RESPONSE, /* 2xx but unparseable */
    STRIPE_ERR_NO_MEMORY,
} stripe_result_t;

/* Message for logs and the auth-error screen. Never includes the key. */
const char *stripe_result_str(stripe_result_t r);

/*
 * Result of the setup-time validation call (spec 9.1 step 4).
 *
 * On success this carries enough to show the customer their own data on the
 * phone before setup completes -- the moment the spec says converts skeptics.
 */
typedef struct {
    stripe_result_t result;
    int http_status;        /* for the error-code footer on State B */
    bool has_subscriptions; /* whether the account has any at all */
    bool test_mode;         /* derived from the key prefix */
} stripe_validation_t;

/*
 * Fetch subscriptions and compute totals (spec 7.1, 7.2).
 *
 * Uses limit=100 and expands data.discount, because a discount that is not
 * applied makes a 50%-off annual plan read at double (spec 7.2 step 1). NOTE
 * this expansion requires the restricted key to also carry Coupons: Read.
 *
 * Pagination is not followed: `truncated` reports when an account has more
 * than one page. Multi-page accounts land with the polling layer.
 */
stripe_result_t stripe_fetch_totals(mrr_totals_t *out, bool *truncated);

/*
 * Fetch recent events (spec 7.3).
 *
 * `since_utc` maps to created[gte], so only today's events are pulled. Spec
 * 7.4 step 4 is emphatic that this needs a correct local-midnight epoch:
 * Stripe returns UTC, and truncating to UTC midnight rolls "today" over
 * mid-evening for US timezones.
 */
stripe_result_t stripe_fetch_events(int64_t since_utc, event_totals_t *out);

/*
 * As above, but with separate windows: `fetch_since` is how far back to ask
 * Stripe, `day_start_utc` is what counts as "today" for the daily figures.
 */
stripe_result_t stripe_fetch_events_since(int64_t fetch_since,
                                          int64_t day_start_utc,
                                          event_totals_t *out);

/* Set the API key used for subsequent calls. Stored in RAM only. */
void stripe_set_key(const char *key);

/*
 * Validate the key with `GET /v1/subscriptions?limit=1` (spec 9.1 step 4).
 *
 * A deliberately tiny request: it proves the key works and has the right scope
 * without pulling a full subscriptions page.
 */
stripe_validation_t stripe_validate_key(void);
