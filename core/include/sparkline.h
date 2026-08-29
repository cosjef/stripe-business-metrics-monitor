/*
 * Sparkline plotting: history series -> screen coordinates.
 *
 * The arithmetic half of the sparkline. Drawing is LVGL's job; deciding where
 * the points go is where the bugs are, and it is pure integer maths, so it
 * lives here and is host-tested.
 *
 * Every failure mode this guards is a silent one: a flat series dividing by
 * zero, an off-by-one clipping today's sample off the right edge, or a y-axis
 * that is not inverted and renders growth as decline. That last one is worse
 * than drawing nothing, because it is confidently wrong -- the same category
 * of error the stale screen exists to prevent.
 */
#pragma once

#include "history.h"

/* A point in screen coordinates. */
typedef struct {
    int x;
    int y;
} spark_point_t;

/* The rectangle the plot fills. */
typedef struct {
    int x;
    int y;
    int w;
    int h;
} spark_box_t;

/*
 * Plot `h` into `box`, writing at most `max_points` points.
 *
 * Returns the number of points written, or 0 when nothing should be drawn --
 * which happens when there is not yet enough history to show a trend honestly
 * (history_has_trend), or when the output buffer is too small for the series.
 * Callers must treat 0 as "draw no chart" rather than an error.
 *
 * Points are spaced evenly by index rather than by calendar date. Spacing by
 * date would open a visible void for days the device was switched off, which
 * reads as a data problem rather than a business one; the line is about the
 * shape of the metric, not the device's uptime. history_day_span() remains
 * available if that judgement ever needs revisiting.
 */
int spark_plot(const history_t *h, const spark_box_t *box,
               spark_point_t *out, int max_points);
