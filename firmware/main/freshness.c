/*
 * Data freshness and retry backoff. See freshness.h.
 */
#include "freshness.h"

#include <inttypes.h>
#include <stdio.h>

void freshness_init(freshness_t *f)
{
    f->last_success_ms = 0;
    f->consecutive_failures = 0;
}

void freshness_mark_success(freshness_t *f, int64_t now_ms)
{
    /* Guard against 0, which means "never loaded" elsewhere. A success at
     * uptime zero is not something to distinguish. */
    f->last_success_ms = now_ms > 0 ? now_ms : 1;
    f->consecutive_failures = 0;
}

void freshness_mark_failure(freshness_t *f)
{
    if (f->consecutive_failures < 1000) {
        f->consecutive_failures++;
    }
}

bool freshness_is_stale(const freshness_t *f, int64_t now_ms)
{
    /* Never loaded is absent, not stale. The screens show dashes for absent
     * data; calling it stale would imply a number exists that does not. */
    if (f->last_success_ms == 0) {
        return false;
    }
    return (now_ms - f->last_success_ms) >= STALE_AFTER_MS;
}

int64_t freshness_age_ms(const freshness_t *f, int64_t now_ms)
{
    if (f->last_success_ms == 0) {
        return -1;
    }
    const int64_t age = now_ms - f->last_success_ms;
    return age > 0 ? age : 0;
}

int64_t freshness_retry_delay_ms(const freshness_t *f)
{
    if (f->consecutive_failures <= 0) {
        return 0;
    }

    /* 60s, 120s, 240s, ... Shift rather than pow, and stop shifting well
     * before the exponent could overflow. */
    const int shift = f->consecutive_failures - 1;
    if (shift >= 20) {
        return BACKOFF_MAX_MS;
    }

    const int64_t delay = BACKOFF_BASE_MS * (1LL << shift);
    return delay > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : delay;
}

void freshness_format_age(int64_t age_ms, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (age_ms < 0) {
        age_ms = 0;
    }

    const int64_t minutes = age_ms / (60 * 1000);

    /* Coarse by design: the reader needs to know the number is old, not its
     * age to the second, and the subtitle line is tight (spec 2.3). */
    if (minutes < 60) {
        snprintf(out, out_len, "%" PRId64 " min", minutes);
        return;
    }

    const int64_t hours = minutes / 60;
    if (hours < 24) {
        snprintf(out, out_len, "%" PRId64 " hr", hours);
        return;
    }

    const int64_t days = hours / 24;
    snprintf(out, out_len, "%" PRId64 " day%s", days, days == 1 ? "" : "s");
}
