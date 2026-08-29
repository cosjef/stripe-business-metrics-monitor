/*
 * Stripe event classification and today's deltas (spec 7.3).
 *
 * Events drive the New Paid, today's-delta and Last Event screens. They do
 * NOT adjust MRR: to do that you need the dollar amount of a change, and the
 * event payload frequently cannot supply one you can trust --
 * `subscription.updated` fires for seat changes, plan swaps, trial
 * conversions and payment-method edits alike, `previous_attributes` only
 * carries fields that changed, tiered prices have no unit_amount at all, and
 * discounts live on the subscription rather than the item. Spec 7.3 concedes
 * the point by requiring hourly reconciliation "because incremental
 * adjustment drifts". MRR therefore changes only on a full recompute, where
 * it is derived from real price objects.
 *
 * Counting is different: a `customer.subscription.created` event establishes
 * that a subscription started, which is a fact the event itself carries.
 *
 * No ESP-IDF dependencies, so this is tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EVENT_SUMMARY_LEN 32

typedef enum {
    EVENT_OTHER = 0,          /* not interesting to this device */
    EVENT_SUB_CREATED,        /* customer.subscription.created */
    EVENT_SUB_UPDATED,        /* customer.subscription.updated */
    EVENT_SUB_DELETED,        /* customer.subscription.deleted */
    EVENT_INVOICE_PAID,       /* invoice.payment_succeeded */
    EVENT_TRIAL_ENDING,       /* customer.subscription.trial_will_end */
} event_kind_t;

typedef struct {
    event_kind_t kind;
    int64_t created;          /* unix seconds, UTC */
    int64_t amount_cents;     /* invoice amount, 0 when not applicable */
} stripe_event_t;

/* Running totals for "today", in the device's local timezone. */
typedef struct {
    int new_paid;             /* subscriptions created today */
    int churned;              /* subscriptions deleted today */
    int64_t revenue_cents;    /* invoices paid today */

    /*
     * Cancellations over a rolling 30-day window.
     *
     * A daily churn count is zero most days, which makes for a screen that
     * only appears on bad news. A 30-day figure is always meaningful and can
     * be watched as a trend, which is what makes it worth a permanent slot.
     */
    int churned_30d;
    int new_paid_30d;     /* subscriptions created in the same window */

    bool have_last;           /* whether last_* below are populated */
    event_kind_t last_kind;
    int64_t last_created;
    int64_t last_amount_cents;
} event_totals_t;

/* Map a Stripe event `type` string to a kind. */
event_kind_t event_kind_from_type(const char *type);

/*
 * Start of the current local day, as a UTC unix timestamp.
 *
 * Spec 7.4 step 4: Stripe returns UTC and local midnight is not UTC midnight.
 * Getting this wrong makes "today" reset at an arbitrary hour --
 * mid-afternoon for US timezones.
 *
 * `utc_offset_seconds` is the offset in effect at that moment, so callers are
 * responsible for daylight saving.
 */
int64_t local_day_start_utc(int64_t now_utc, int32_t utc_offset_seconds);

/*
 * Accumulate events into today's totals. Events outside today are counted
 * toward "last event" but not toward the daily figures.
 */
event_totals_t events_summarize(const stripe_event_t *events, int count,
                                int64_t day_start_utc);

/*
 * As above, but with an explicit start for the rolling cancellation window.
 * `window_start_utc` is typically 30 days before now.
 */
event_totals_t events_summarize_window(const stripe_event_t *events, int count,
                                       int64_t day_start_utc,
                                       int64_t window_start_utc);

/*
 * Human-readable summary of the most recent event, for the Last Event screen:
 * "new paid", "churned", "payment", "trial ending".
 */
const char *event_kind_label(event_kind_t k);

/*
 * Relative age, e.g. "2m", "3h", "5d", for the Last Event subtitle.
 */
void event_format_age(int64_t event_utc, int64_t now_utc,
                      char *out, size_t out_len);
