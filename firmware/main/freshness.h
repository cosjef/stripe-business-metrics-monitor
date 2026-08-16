/*
 * Data freshness and retry backoff (spec 7.4).
 *
 * Spec 6.2 calls the stale screen the most important in the deck: "a
 * confidently displayed stale number is worse than an obviously stale one,
 * and most cheap dashboards fail exactly here by freezing on a four-hour-old
 * figure with no indication."
 *
 * No ESP-IDF dependencies, so this is tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Spec 7.4 step 2 / appendix A. */
#define STALE_AFTER_MS (15 * 60 * 1000)

/* Spec 7.4 step 3: 60s, 120s, 240s, ... capped at 15 minutes. Backoff here
 * protects against cascade rather than throttling -- Stripe's read limit is
 * far above anything this device generates. */
#define BACKOFF_BASE_MS (60 * 1000)
#define BACKOFF_MAX_MS  (15 * 60 * 1000)

typedef struct {
    int64_t last_success_ms;  /* 0 if never */
    int consecutive_failures;
} freshness_t;

void freshness_init(freshness_t *f);

/* Record a successful fetch, clearing any backoff. */
void freshness_mark_success(freshness_t *f, int64_t now_ms);

/* Record a failure, deepening the backoff. */
void freshness_mark_failure(freshness_t *f);

/*
 * True if the data on screen should be presented as stale (spec 7.4 step 2).
 *
 * Data that has never loaded is NOT stale -- it is absent, which the screens
 * show as dashes. Claiming a number is stale when there has never been one
 * would be its own kind of lie.
 */
bool freshness_is_stale(const freshness_t *f, int64_t now_ms);

/* Age of the last good data in milliseconds, or -1 if never loaded. */
int64_t freshness_age_ms(const freshness_t *f, int64_t now_ms);

/* Delay before the next attempt, following the backoff schedule. */
int64_t freshness_retry_delay_ms(const freshness_t *f);

/*
 * Render an age for the stale screen: "22 min", "3 hr", "2 days".
 * Deliberately coarse -- the point is that the number is old, not exactly how
 * old, and the glyph budget is tight (spec 2.3).
 */
void freshness_format_age(int64_t age_ms, char *out, size_t out_len);
