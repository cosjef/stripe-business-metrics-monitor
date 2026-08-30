/*
 * Which screens belong in the rotation (spec 6.1).
 *
 * The deck is not fixed. Spec 6.1 already treats churn conditionally --
 * "render it into rotation only when nonzero for the period, so it does not
 * occupy 8 seconds displaying a permanent zero" -- and the same reasoning
 * applies to any metric an account does not use.
 *
 * An account with no trials should not spend a third of its rotation showing
 * a zero and an uncomputable percentage. Dropping them makes every remaining
 * metric come round more often, which is the point of a glanceable
 * instrument.
 *
 * No ESP-IDF dependencies, so this is tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SCREEN_MRR = 0,
    SCREEN_NEW_PAID,
    SCREEN_PAID_SUBS,
    SCREEN_TRIALS,
    SCREEN_CONVERSION,
    SCREEN_CANCELLATIONS,
    SCREEN_ARR,
    SCREEN_ARPU,
    SCREEN_NET_CHANGE,
    SCREEN_FAILED,
    SCREEN_COUNT
} screen_id_t;

/* What the account currently looks like, for deciding visibility. */
typedef struct {
    bool have_data;      /* a fetch has succeeded at least once */
    int trial_count;
    int churned_30d;      /* cancellations in the rolling window */
    bool have_conversion; /* 30 days of trial history exist */

    /*
     * Failed payments are only shown when the key can read invoices AND
     * something is actually failing. A permanent zero would be noise, and a
     * screen the key cannot populate would be a lie.
     */
    bool have_invoices;   /* the key has Invoices: Read */
    int failed_count;
} rotation_state_t;

/*
 * True if `id` should appear in the rotation given the current state.
 *
 * Before any data arrives, the core screens are shown (with dashes) so the
 * device is not blank; the conditional ones stay hidden until there is
 * something to say.
 */
bool rotation_screen_visible(screen_id_t id, const rotation_state_t *st);

/*
 * Fill `out` with the visible screen ids in display order, returning how many.
 * `out` must have room for SCREEN_COUNT entries.
 *
 * Never returns zero: MRR is always present, because a revenue readout with
 * no screens is worse than one showing a dash.
 */
int rotation_build(const rotation_state_t *st, screen_id_t *out);
