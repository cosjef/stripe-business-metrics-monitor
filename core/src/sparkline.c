/*
 * Sparkline plotting. See sparkline.h.
 */
#include "sparkline.h"

int spark_plot(const history_t *h, const spark_box_t *box,
               spark_point_t *out, int max_points)
{
    /* Not enough history to assert a direction. The caller draws nothing and
     * the screen falls back to the number alone. */
    if (!history_has_trend(h)) {
        return 0;
    }

    const int n = history_count(h);
    if (n > max_points || n < 2 || box->w < 2 || box->h < 1) {
        return 0;
    }

    const int64_t lo = history_min(h);
    const int64_t hi = history_max(h);

    /* A metric that has not moved is neither at its maximum nor its minimum in
     * any meaningful sense, so it draws through the middle. This also avoids
     * the division by zero that a flat range would otherwise cause. */
    if (hi == lo) {
        const int mid = box->y + box->h / 2;
        for (int i = 0; i < n; i++) {
            out[i].x = box->x + (int)(((int64_t)i * (box->w - 1)) / (n - 1));
            out[i].y = mid;
        }
        return n;
    }

    const int64_t range = hi - lo;

    for (int i = 0; i < n; i++) {
        /* Evenly spaced, with the last point landing exactly on the right
         * edge: today's value must align with the hero showing it. */
        out[i].x = box->x + (int)(((int64_t)i * (box->w - 1)) / (n - 1));

        const int64_t v = history_value_at(h, i);

        /* Inverted: larger values sit higher, meaning a smaller y. Scaled in
         * int64 before narrowing so a large cents range cannot overflow. */
        const int64_t off = ((v - lo) * (box->h - 1)) / range;
        out[i].y = box->y + (box->h - 1) - (int)off;
    }

    return n;
}
