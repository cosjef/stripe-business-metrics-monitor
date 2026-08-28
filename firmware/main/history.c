/*
 * Daily metric history. See history.h for why gaps must not become zeros.
 */
#include "history.h"

void history_init(history_t *h)
{
    h->count = 0;
    h->head = 0;
}

/* Ring index of the i-th sample, oldest first. */
static int idx_of(const history_t *h, int i)
{
    const int start = (h->head - h->count + HISTORY_DAYS * 2) % HISTORY_DAYS;
    return (start + i) % HISTORY_DAYS;
}

static int newest_idx(const history_t *h)
{
    return (h->head - 1 + HISTORY_DAYS) % HISTORY_DAYS;
}

void history_record(history_t *h, int32_t epoch_day, int64_t value)
{
    if (h->count > 0) {
        const int n = newest_idx(h);

        if (epoch_day == h->day[n]) {
            /* Same day: update in place. The device polls every ten minutes,
             * so appending would exhaust the window in hours. */
            h->value[n] = value;
            return;
        }

        if (epoch_day < h->day[n]) {
            /* Older than what we already hold -- the clock stepped backwards,
             * most likely an NTP correction. Ignore rather than corrupt the
             * ordering the renderer depends on. */
            return;
        }
    }

    h->value[h->head] = value;
    h->day[h->head] = epoch_day;
    h->head = (h->head + 1) % HISTORY_DAYS;

    if (h->count < HISTORY_DAYS) {
        h->count++;
    }
}

int history_count(const history_t *h)
{
    return h->count;
}

bool history_has_trend(const history_t *h)
{
    return h->count >= HISTORY_MIN_FOR_TREND;
}

int64_t history_latest(const history_t *h)
{
    if (h->count == 0) {
        return 0;
    }
    return h->value[newest_idx(h)];
}

int64_t history_oldest(const history_t *h)
{
    if (h->count == 0) {
        return 0;
    }
    return h->value[idx_of(h, 0)];
}

int64_t history_min(const history_t *h)
{
    if (h->count == 0) {
        return 0;
    }

    int64_t m = h->value[idx_of(h, 0)];
    for (int i = 1; i < h->count; i++) {
        const int64_t v = h->value[idx_of(h, i)];
        if (v < m) {
            m = v;
        }
    }
    return m;
}

int64_t history_max(const history_t *h)
{
    if (h->count == 0) {
        return 0;
    }

    int64_t m = h->value[idx_of(h, 0)];
    for (int i = 1; i < h->count; i++) {
        const int64_t v = h->value[idx_of(h, i)];
        if (v > m) {
            m = v;
        }
    }
    return m;
}

bool history_is_flat(const history_t *h)
{
    if (h->count == 0) {
        return true;
    }
    return history_min(h) == history_max(h);
}

int64_t history_change(const history_t *h)
{
    if (h->count < 2) {
        return 0;
    }
    return history_latest(h) - history_oldest(h);
}

int history_day_span(const history_t *h)
{
    if (h->count < 2) {
        return 0;
    }
    return (int)(h->day[newest_idx(h)] - h->day[idx_of(h, 0)]);
}
