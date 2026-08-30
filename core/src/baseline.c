/*
 * Baseline-to-top-left position conversion. See baseline.h.
 *
 * No LVGL or ESP-IDF dependencies, so this builds and tests on the host.
 */
#include "baseline.h"

int font_ascent(int line_height, int base_line)
{
    return line_height - base_line;
}

int baseline_to_top(int baseline_y, int line_height, int base_line)
{
    int top = baseline_y - font_ascent(line_height, base_line);
    if (top < 0) {
        top = 0;
    }
    return top;
}
