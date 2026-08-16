/*
 * Stripe event classification and today's deltas. See events.h.
 */
#include "events.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

event_kind_t event_kind_from_type(const char *type)
{
    if (type == NULL) {
        return EVENT_OTHER;
    }

    /* Exact matches only. Stripe has many event types sharing these prefixes
     * -- customer.subscription.pending_update_applied among them -- and a
     * prefix match would silently miscount them. */
    if (strcmp(type, "customer.subscription.created") == 0) {
        return EVENT_SUB_CREATED;
    }
    if (strcmp(type, "customer.subscription.updated") == 0) {
        return EVENT_SUB_UPDATED;
    }
    if (strcmp(type, "customer.subscription.deleted") == 0) {
        return EVENT_SUB_DELETED;
    }
    if (strcmp(type, "invoice.payment_succeeded") == 0) {
        return EVENT_INVOICE_PAID;
    }
    if (strcmp(type, "customer.subscription.trial_will_end") == 0) {
        return EVENT_TRIAL_ENDING;
    }

    return EVENT_OTHER;
}

int64_t local_day_start_utc(int64_t now_utc, int32_t utc_offset_seconds)
{
    /*
     * Shift into local time, truncate to the day, shift back.
     *
     * Spec 7.4 step 4: Stripe returns UTC and local midnight is not UTC
     * midnight. Truncating the UTC timestamp directly would roll "today" over
     * at 8pm Eastern, so the New Paid screen would read zero all evening.
     */
    const int64_t local = now_utc + utc_offset_seconds;

    /* Floor division: C truncates toward zero, which is wrong for timestamps
     * before 1970. Not reachable here, but the correct form costs nothing. */
    int64_t days = local / 86400;
    if (local < 0 && local % 86400 != 0) {
        days--;
    }

    return days * 86400 - utc_offset_seconds;
}

event_totals_t events_summarize(const stripe_event_t *events, int count,
                                int64_t day_start_utc)
{
    /* Default the rolling window to today, preserving the original behaviour
     * for callers that only care about daily figures. */
    return events_summarize_window(events, count, day_start_utc, day_start_utc);
}

event_totals_t events_summarize_window(const stripe_event_t *events, int count,
                                       int64_t day_start_utc,
                                       int64_t window_start_utc)
{
    event_totals_t t = {0};

    if (events == NULL || count <= 0) {
        return t;
    }

    for (int i = 0; i < count; i++) {
        const stripe_event_t *e = &events[i];

        /* Events we do not act on must not become the heartbeat, or the Last
         * Event screen shows something meaningless like a payout. */
        if (e->kind == EVENT_OTHER) {
            continue;
        }

        /* The heartbeat is the most recent interesting event regardless of
         * day (spec 6.1) -- its job is to confirm the device is live. Do not
         * assume Stripe's ordering. */
        if (!t.have_last || e->created > t.last_created) {
            t.have_last = true;
            t.last_kind = e->kind;
            t.last_created = e->created;
            t.last_amount_cents = e->amount_cents;
        }

        /* Cancellations over the rolling window. Counted before the daily
         * filter, since the window is wider than a day. */
        if (e->kind == EVENT_SUB_DELETED && e->created >= window_start_utc) {
            t.churned_30d++;
        }

        /* Daily figures count only today, or "new paid today" would never
         * reset. */
        if (e->created < day_start_utc) {
            continue;
        }

        switch (e->kind) {
        case EVENT_SUB_CREATED:
            t.new_paid++;
            break;
        case EVENT_SUB_DELETED:
            t.churned++;
            break;
        case EVENT_INVOICE_PAID:
            t.revenue_cents += e->amount_cents;
            break;
        default:
            break;
        }
    }

    return t;
}

const char *event_kind_label(event_kind_t k)
{
    switch (k) {
    case EVENT_SUB_CREATED:  return "new paid";
    case EVENT_SUB_UPDATED:  return "changed";
    case EVENT_SUB_DELETED:  return "churned";
    case EVENT_INVOICE_PAID: return "payment";
    case EVENT_TRIAL_ENDING: return "trial ending";
    case EVENT_OTHER:
    default:                 return "activity";
    }
}

void event_format_age(int64_t event_utc, int64_t now_utc,
                      char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    int64_t age = now_utc - event_utc;

    /* Before NTP syncs, the device clock can be behind Stripe's timestamps,
     * making an event look future-dated. "now" is the honest rendering; a
     * negative age would be nonsense. */
    if (age < 0) {
        age = 0;
    }

    if (age < 60) {
        snprintf(out, out_len, "now");
        return;
    }
    if (age < 3600) {
        snprintf(out, out_len, "%" PRId64 "m", age / 60);
        return;
    }
    if (age < 86400) {
        snprintf(out, out_len, "%" PRId64 "h", age / 3600);
        return;
    }
    snprintf(out, out_len, "%" PRId64 "d", age / 86400);
}
