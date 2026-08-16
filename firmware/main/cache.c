/*
 * Cached-value validation. See cache.h.
 */
#include "cache.h"

/*
 * Plausibility bounds.
 *
 * These are not business limits, they are corruption detectors: erased flash
 * reads as 0xFF everywhere, which decodes as enormous values. Anything past
 * these is far likelier to be a bad read than a real account.
 */
#define MAX_PLAUSIBLE_SUBS  1000000
#define MAX_PLAUSIBLE_CENTS 10000000000LL   /* $100m/month */

bool cache_is_valid(const cache_t *c)
{
    if (c == NULL) {
        return false;
    }

    /* A cache from different firmware must be discarded rather than
     * reinterpreted -- old bytes read as a new layout would put fabricated
     * numbers on screen. */
    if (c->version != CACHE_VERSION) {
        return false;
    }

    /* Without a timestamp the age cannot be shown, and an undated figure
     * presented as current is exactly the confident lie to avoid. */
    if (c->saved_at_utc <= 0) {
        return false;
    }

    if (c->mrr_cents < 0 || c->mrr_cents > MAX_PLAUSIBLE_CENTS) {
        return false;
    }
    if (c->failed_cents < 0 || c->failed_cents > MAX_PLAUSIBLE_CENTS) {
        return false;
    }

    if (c->active_count < 0 || c->active_count > MAX_PLAUSIBLE_SUBS ||
        c->trial_count < 0 || c->trial_count > MAX_PLAUSIBLE_SUBS ||
        c->churned_30d < 0 || c->churned_30d > MAX_PLAUSIBLE_SUBS ||
        c->new_paid_30d < 0 || c->new_paid_30d > MAX_PLAUSIBLE_SUBS ||
        c->new_paid_today < 0 || c->new_paid_today > MAX_PLAUSIBLE_SUBS ||
        c->failed_count < 0 || c->failed_count > MAX_PLAUSIBLE_SUBS) {
        return false;
    }

    return true;
}

int64_t cache_age_seconds(const cache_t *c, int64_t now_utc)
{
    if (!cache_is_valid(c)) {
        return -1;
    }

    const int64_t age = now_utc - c->saved_at_utc;

    /* A future-dated cache means the clock has not synced yet. Reporting a
     * negative age would let a caller treat it as fresh. */
    if (age < 0) {
        return -1;
    }

    return age;
}

bool cache_too_old(const cache_t *c, int64_t now_utc)
{
    const int64_t age = cache_age_seconds(c, now_utc);

    /* An unusable age counts as too old: better to show nothing than to show
     * figures whose age cannot be established. */
    if (age < 0) {
        return true;
    }

    return age > CACHE_MAX_AGE_SECONDS;
}
