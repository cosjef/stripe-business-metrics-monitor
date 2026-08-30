/*
 * Last-good values persisted to flash (spec 7.4 step 1).
 *
 * "On boot, render cached values immediately, then refresh. The screen is
 * never blank."
 *
 * Currently the device shows dashes for roughly twelve seconds after every
 * boot while WiFi associates, NTP syncs and the first fetch completes. For a
 * desk instrument that is restarted rarely but read constantly, that blank
 * period is the most visible thing about a restart.
 *
 * The cached figures are shown WITH their age, not as if they were live. A
 * number from an hour ago presented confidently is exactly the failure spec
 * 6.2 exists to prevent -- so the freshness machinery treats a restored cache
 * as data of that age, and the stale screen takes over if it is too old.
 *
 * No ESP-IDF dependencies in the struct or its validation, so those are
 * tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Bumped whenever the struct layout changes. A cache written by older
 * firmware is discarded rather than misread -- reinterpreting old bytes as a
 * new layout would put fabricated numbers on screen, which is worse than a
 * brief dash.
 */
#define CACHE_VERSION 1

typedef struct {
    uint32_t version;
    int64_t saved_at_utc;    /* when this was written, for the age display */

    int64_t mrr_cents;
    int active_count;
    int trial_count;
    int churned_30d;
    int new_paid_30d;
    int new_paid_today;
    int failed_count;
    int64_t failed_cents;

    bool have_invoices;      /* whether the key could read invoices */
    bool mixed_currency;
    bool has_tiered;
} cache_t;

/*
 * True if a loaded cache is usable.
 *
 * Rejects the wrong version, a missing timestamp, and values that cannot be
 * real -- negative counts, or an MRR beyond any plausible account. Corrupt
 * flash should surface as "no cache" rather than as nonsense on screen.
 */
bool cache_is_valid(const cache_t *c);

/*
 * Age of a cache in seconds, given the current time. Returns -1 if the cache
 * is invalid or the clock is behind the saved timestamp, which happens before
 * NTP syncs.
 */
int64_t cache_age_seconds(const cache_t *c, int64_t now_utc);

/*
 * True if a cache is too old to show at all.
 *
 * Beyond a day the figures are more likely to mislead than help: a subscriber
 * count from last week presented as "stale" still invites the reader to act
 * on it.
 */
bool cache_too_old(const cache_t *c, int64_t now_utc);

#define CACHE_MAX_AGE_SECONDS (24 * 3600)
