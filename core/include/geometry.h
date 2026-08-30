/*
 * Panel geometry: turning the spec's physical requirements into pixels.
 *
 * Spec 2.2 states the legibility floor in MILLIMETRES at 50cm, not in pixels.
 * The pixel constants in layout.h are that requirement resolved against one
 * panel -- 240x240 at 1.54", about 220 PPI. On a different panel the same
 * millimetres are a different number of pixels.
 *
 * This matters more than it sounds. The C6 panel is 480x480 at 2.16": twice
 * the pixels but only 1.4x the physical size, so about 1.43x the density.
 * Doubling the constants would render text ~40% physically larger than the
 * spec asks for; copying them unchanged would render it ~30% smaller and drop
 * below the legibility floor. Both are plausible-looking mistakes.
 *
 * No ESP-IDF, no LVGL -- pure arithmetic, tested on the host.
 */
#pragma once

#include <stdbool.h>

/*
 * A square display panel. Both panels we target are square; if a non-square
 * one ever appears this needs a second dimension.
 */
typedef struct {
    const char *name;
    double diagonal_in;  /* as marketed, e.g. 1.54 */
    int pixels;          /* per side */
} geom_panel_t;

/* Waveshare ESP32-S3-LCD-1.54: 240x240 IPS, the original target. */
extern const geom_panel_t GEOM_PANEL_S3;

/* Waveshare ESP32-C6-Touch-AMOLED-2.16: 480x480 AMOLED. */
extern const geom_panel_t GEOM_PANEL_C6;

/*
 * Spec 2.2's thresholds, in millimetres of cap height at 50cm.
 *
 * These are the real constants of the design. Everything in pixels is derived.
 */
#define GEOM_LEGIBILITY_FLOOR_MM  2.8  /* minimum viable to read reliably */
#define GEOM_ABSOLUTE_FLOOR_MM    2.3  /* hard minimum, no exceptions */

/* Usable side length in millimetres (square panel, diagonal / sqrt 2). */
double geom_side_mm(const geom_panel_t *p);

/* Pixels per inch. */
double geom_ppi(const geom_panel_t *p);

/* Physical height of a pixel size on a given panel. */
double geom_px_to_mm(int px, const geom_panel_t *p);

/* Pixel size needed for a given physical height, rounded to nearest. */
int geom_mm_to_px(double mm, const geom_panel_t *p);

/*
 * Translate a pixel size between panels, preserving physical size.
 * This is the function that does the porting work.
 */
int geom_translate_px(int px, const geom_panel_t *from, const geom_panel_t *to);

/* Whether a size clears spec 2.2's floors on this panel. */
bool geom_meets_legibility_floor(int px, const geom_panel_t *p);
bool geom_meets_absolute_floor(int px, const geom_panel_t *p);

/* Usable text column: full width minus padding on both sides. */
int geom_text_column_px(const geom_panel_t *p, int pad_px);
