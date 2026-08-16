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
        SCREEN_MRR,
        SCREEN_NEW_PAID,
        SCREEN_CANCELLATIONS,
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
