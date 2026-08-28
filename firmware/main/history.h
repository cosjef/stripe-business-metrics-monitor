/*
 * Daily metric history, for sparklines on the 480x480 panel.
 *
 * The C6 panel leaves 54% of its height empty once the hero, label, subtitle
 * and footer sit at their correct physical sizes (geometry.c). A sparkline is
 * the one addition that fills it with information rather than decoration: it
 * turns each screen from a snapshot into a direction, and "$1,052 and
 * climbing" is a different fact from "$1,052".
 *
 * The difficulty is sourcing, not drawing. Stripe exposes no historical MRR
 * endpoint, so the device accumulates its own series -- one sample per local
 * day, from first boot. Consequences this module exists to handle:
 *
 *   - A fresh device has no trend. It must decline to draw one rather than
 *     render a flat line asserting a stability nobody measured.
 *
 *   - A device switched off, or offline, has no samples for those days. Those
 *     gaps must stay absent. Recording zero would draw a cliff to the floor of
 *     the chart and read as total revenue collapse; absent and zero are
 *     completely different facts, and this device's whole discipline is never
 *     to assert what it has not measured.
 *
 * Integer cents throughout, no floating point -- same rule as the rest of the
 * money path. No ESP-IDF, so it is host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Window length. 30 days matches the rolling window the deck already uses for
 * cancellations and net change, so the sparkline covers the same period the
 * subtitles talk about.
 *
 * Cost is 30 x (int64 + int32) = 360 bytes per metric. With four metrics
 * trended that is under 1.5KB against a 24KB NVS partition.
 */
#define HISTORY_DAYS 30

/*
 * Samples required before a trend is drawn.
 *
 * Two points make a straight line, which asserts a direction that was never
 * measured. Seven gives a week -- enough for the shape to mean something, and
 * short enough that a new device is not blank for a month.
 */
#define HISTORY_MIN_FOR_TREND 7

typedef struct {
    int64_t value[HISTORY_DAYS];  /* cents, or whatever unit the metric uses */
    int32_t day[HISTORY_DAYS];    /* epoch day number, for gap detection */
    int count;                    /* samples held, up to HISTORY_DAYS */
    int head;                     /* ring index of the next write */
} history_t;

void history_init(history_t *h);

/*
 * Record today's value.
 *
 * `epoch_day` is days since the Unix epoch in LOCAL time, so a "day" matches
 * the local midnight the daily figures already reset at.
 *
 * Recording the same day twice updates rather than appends: the device polls
 * every ten minutes, so appending would fill the window in a few hours. A day
 * older than the newest sample is ignored, which protects the series from an
 * NTP correction stepping the clock backwards.
 */
void history_record(history_t *h, int32_t epoch_day, int64_t value);

int history_count(const history_t *h);

/* Whether there is enough history to honestly draw a trend. */
bool history_has_trend(const history_t *h);

/* Newest and oldest values held. Zero if empty. */
int64_t history_latest(const history_t *h);
int64_t history_oldest(const history_t *h);

/* Range, for scaling the plot. */
int64_t history_min(const history_t *h);
int64_t history_max(const history_t *h);

/*
 * Whether every sample is the same value. The renderer needs this: scaling a
 * flat series by its range would divide by zero, and a flat metric should
 * render as a flat line rather than filling the plot height.
 */
bool history_is_flat(const history_t *h);

/* Oldest to newest change, for the subtitle. Zero with fewer than 2 samples. */
int64_t history_change(const history_t *h);

/*
 * Days between the oldest and newest sample. Larger than count-1 when the
 * device was off, which lets a renderer space points by date rather than by
 * index if it wants gaps to show as gaps.
 */
int history_day_span(const history_t *h);
