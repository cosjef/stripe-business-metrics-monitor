/*
 * Parse a Stripe subscriptions list response into MRR inputs.
 *
 * Split from stripe_api.c so it can be tested on the host against fixture
 * JSON, without a network or a device. The parsing is where field-name and
 * shape assumptions live, and those are exactly what break when Stripe
 * changes something.
 */
#pragma once

#include "mrr.h"

#include <stdbool.h>

/* Bounded so a large account cannot exhaust memory mid-parse. Spec 7.1 uses
 * limit=100 per page, and each subscription rarely has more than a few items. */
/*
 * Stripe object ids are short ("sub_1P2x..."), but the cursor must never be
 * truncated: a clipped id asks Stripe to resume from something that does not
 * exist, which silently skips subscriptions and under-reports MRR.
 */
#define STRIPE_ID_LEN 64

#define STRIPE_MAX_SUBS  128
#define STRIPE_MAX_ITEMS 512

typedef struct {
    mrr_subscription_t subs[STRIPE_MAX_SUBS];
    mrr_item_t items[STRIPE_MAX_ITEMS];
    int sub_count;
    int item_count;
    bool has_more;      /* Stripe's has_more: another page exists */

    /*
     * Id of the LAST subscription seen in this page, for
     * starting_after on the next request. Empty when the page was empty.
     *
     * "Last seen", not "last stored": subscriptions with statuses that do not
     * count toward MRR are skipped, and if one of those ends the page, paging
     * from the last stored id would resume too early and loop forever.
     */
    char last_id[STRIPE_ID_LEN];
    bool truncated;     /* we hit our own limits and dropped data */
} stripe_subs_t;

/*
 * Parse a `GET /v1/subscriptions` response body.
 *
 * Only `active` and `trialing` subscriptions are kept; everything else
 * (canceled, past_due, incomplete) is ignored, matching spec 7.2's
 * "status in (active, trialing)".
 *
 * Returns false if the body is not valid JSON or lacks a `data` array.
 */
bool stripe_parse_subscriptions(const char *json, stripe_subs_t *out);
