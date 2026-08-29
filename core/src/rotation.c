/*
 * Conditional screen rotation. See rotation.h.
 */
#include "rotation.h"

bool rotation_screen_visible(screen_id_t id, const rotation_state_t *st)
{
    if (st == NULL) {
        return id == SCREEN_MRR;
    }

    switch (id) {
    case SCREEN_MRR:
        /* The anchor metric (spec 6.1). Always shown, even before data
         * arrives -- a dash is honest, an empty rotation looks broken. */
        return true;

    case SCREEN_NEW_PAID:
    case SCREEN_PAID_SUBS:
        /* Core metrics: shown once there is data, since a count of zero is
         * still meaningful information about the account. */
        return st->have_data;

    case SCREEN_TRIALS:
        /* An account that does not use trials should not spend a rotation
         * slot on a permanent zero. */
        return st->have_data && st->trial_count > 0;

    case SCREEN_CONVERSION:
        /* Needs 30 days of trial history. Spec 6.1 also cautions that a
         * figure over a handful of trials swings wildly and "undermines
         * trust in the whole device". */
        return st->have_data && st->have_conversion;

    case SCREEN_CANCELLATIONS:
        /*
         * Always shown once there is data, including at zero.
         *
         * Spec 6.1 makes churn conditional to avoid a screen that reads zero
         * every day. A 30-day window does not have that problem: the figure
         * is meaningful whatever its value, and zero cancellations in a month
         * is worth knowing. It also replaces the Last Event heartbeat, which
         * in practice showed "changed" from subscription.updated -- an event
         * that fires for seat changes and payment-method edits alike, and so
         * told the reader nothing actionable.
         */
        return st->have_data;

    case SCREEN_ARR:
    case SCREEN_ARPU:
        /* Both are arithmetic on MRR, so they are available whenever MRR is. */
        return st->have_data;

    case SCREEN_NET_CHANGE:
        /* Net movement needs event history behind it. */
        return st->have_data;

    case SCREEN_FAILED:
        /*
         * Only when the key can read invoices and something is actually
         * failing. Unlike cancellations, a permanent zero here is noise: no
         * failed payments is the normal state, not news.
         */
        return st->have_data && st->have_invoices && st->failed_count > 0;

    case SCREEN_COUNT:
    default:
        return false;
    }
}

int rotation_build(const rotation_state_t *st, screen_id_t *out)
{
    if (out == NULL) {
        return 0;
    }

    /* Fixed display order, so the rotation does not reshuffle as data
     * changes. MRR leads as the anchor metric; churn sits next to the other
     * movement metrics rather than at the end. */
    static const screen_id_t order[] = {
        /*
         * Grouped by what the reader is asking, so related figures sit
         * together rather than being interleaved:
         *
         *   REVENUE      what the business earns
         *   ALERT        what needs attention
         *   MOVEMENT     what changed
         *   COMPOSITION  who you have
         *
         * MRR leads as the anchor metric (spec 6.1), followed by the same
         * figure annualized and then per subscriber. FAILED comes early
         * rather than last so the one actionable screen is not buried at the
         * end of the cycle.
         */

        /* Revenue: totals first, then the per-subscriber breakdown. */
        SCREEN_MRR,
        SCREEN_ARR,
        SCREEN_ARPU,

        /* Alert */
        SCREEN_FAILED,

        /* Movement */
        SCREEN_NET_CHANGE,
        SCREEN_NEW_PAID,
        SCREEN_CANCELLATIONS,

        /* Composition */
        SCREEN_PAID_SUBS,
        SCREEN_TRIALS,
        SCREEN_CONVERSION,
    };

    int n = 0;
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (rotation_screen_visible(order[i], st)) {
            out[n++] = order[i];
        }
    }

    return n;
}
