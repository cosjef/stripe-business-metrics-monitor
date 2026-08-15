/*
 * Hero value auto-sizing and text width measurement.
 *
 * Spec 2.4 requires hero sizes be computed from the value, never hardcoded per
 * screen, so a customer at $145k MRR does not overflow the 208px column.
 *
 * DEVIATION FROM SPEC 2.3: the spec computes width as `len * size * 0.6em`,
 * which holds only for a monospace face. This build renders SF Compact Bold
 * (proportional) because monospace was measurably less legible at distance --
 * it spent a full character cell on '.' and shrank the digits to compensate.
 * Width is therefore measured per glyph against a table of real advances.
 * The constraint the spec cares about (fit the column, stay above the
 * legibility floor) is unchanged.
 */
#pragma once

#include <stddef.h>

/*
 * Available bitmap font sizes for hero values, ascending.
 */
extern const int hero_font_sizes[];
extern const size_t hero_font_sizes_count;

/*
 * Rendered width of `text` at `size_px`, in pixels, using real per-glyph
 * advance widths. Characters outside the generated glyph range fall back to a
 * conservative (wide) estimate so unknown text errs toward a smaller font
 * rather than silently overflowing.
 */
int text_width_px(const char *text, int size_px);

/*
 * True if `text` fits the 208px column at `size_px`.
 */
_Bool text_fits(const char *text, int size_px);

/*
 * Largest available bitmap size at which `text` fits the column, capped at
 * SIZE_HERO_MAX. Returns 0 for an empty string. If even the smallest available
 * size overflows, returns that smallest size -- the caller should abbreviate
 * (spec 6.1 abbreviates $6,512 to $6.5k for exactly this reason).
 */
int hero_size_for_text(const char *text);
