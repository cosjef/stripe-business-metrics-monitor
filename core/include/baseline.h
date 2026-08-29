/*
 * Baseline-to-top-left position conversion.
 *
 * The spec positions text by baseline (5.1: label y=16, hero y=150, subtitle
 * y=178, footer y=210); LVGL positions objects by their top-left corner. This
 * converts between the two.
 *
 * Kept free of LVGL types so it can be tested on the host.
 */
#pragma once

/*
 * Top edge for text whose baseline should sit at `baseline_y`.
 *
 * A font's baseline sits `line_height - base_line` below its top edge, where
 * base_line is the descent below the baseline (LVGL's naming).
 *
 * Clamped to 0: text whose ascent exceeds its baseline would otherwise be
 * positioned off-screen, which is worse than being slightly low.
 */
int baseline_to_top(int baseline_y, int line_height, int base_line);

/*
 * Ascent of a font: the distance from its top edge down to the baseline.
 */
int font_ascent(int line_height, int base_line);
