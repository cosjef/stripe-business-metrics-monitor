/*
 * Connectivity watchdog. See netwatch.h for why this exists.
 */
#include "netwatch.h"

void netwatch_init(netwatch_t *w)
{
    w->last_trigger = 0;
}

bool netwatch_should_reconnect(const netwatch_t *w, int consecutive_failures)
{
    if (consecutive_failures < NETWATCH_FAILURES_BEFORE_RECONNECT) {
        return false;
    }

    if (w->last_trigger == 0) {
        /* First intervention of this outage. */
        return true;
    }

    /* Already tried once. Wait out a further run of failures before trying
     * again, so the radio is not restarted on every poll. */
    return consecutive_failures >=
           w->last_trigger + NETWATCH_FAILURES_BETWEEN_RECONNECTS;
}

void netwatch_on_reconnect_triggered(netwatch_t *w)
{
    /* Record where we intervened. Note this is set from the threshold rather
     * than passed in: the caller has just acted on should_reconnect(), so the
     * count is known to be at or past it. Storing the actual count would let a
     * skipped poll cycle push the next attempt further out than intended. */
    w->last_trigger = NETWATCH_FAILURES_BEFORE_RECONNECT;
}

void netwatch_on_success(netwatch_t *w)
{
    w->last_trigger = 0;
}

int netwatch_last_trigger(const netwatch_t *w)
{
    return w->last_trigger;
}
