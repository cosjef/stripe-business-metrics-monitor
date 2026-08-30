/*
 * LVGL host harness.
 *
 * Boots LVGL against an in-memory 240x240 framebuffer so screen code can be
 * rendered and inspected on the dev machine, with no panel and no ESP-IDF.
 *
 * This exists because every visual bug in Stage 1 -- inverted colors, the
 * theme painting over the background, invisible rotation dots -- was found by
 * photographing the device. That does not scale to nine screens.
 */
#pragma once

#include <stdint.h>

#include "lvgl.h"

/*
 * Framebuffer size: the panel's, taken from the layout.
 *
 * It used to default to 240x240 with callers overriding to 480. That left
 * test_screens asserting pixel positions against a quarter of the screen the
 * layout was drawing into -- every check passed or failed for reasons that
 * had nothing to do with the code under test. Deriving it from PANEL_PX means
 * the harness cannot disagree with the layout about how big the panel is.
 */
#include "layout.h"

#ifndef HARNESS_W
#define HARNESS_W PANEL_PX
#endif
#ifndef HARNESS_H
#define HARNESS_H PANEL_PX
#endif

/* Initialize LVGL and create the offscreen display. Idempotent. */
void harness_init(void);

/* Run LVGL's render pipeline until the screen is fully drawn. */
void harness_render(void);

/* The active screen object to draw onto. */
lv_obj_t *harness_screen(void);

/*
 * Pixel at (x, y) as 0xRRGGBB. Out-of-bounds reads return 0 and are a test
 * bug, not a legitimate result.
 */
uint32_t harness_pixel(int x, int y);

/* Component accessors, for tolerance comparisons. */
int harness_r(uint32_t rgb);
int harness_g(uint32_t rgb);
int harness_b(uint32_t rgb);

/*
 * True if `a` and `b` match within `tol` per channel. Rendering is
 * antialiased and passes through RGB565, so exact equality is the wrong test
 * for anything but large flat areas.
 */
_Bool harness_color_near(uint32_t a, uint32_t b, int tol);

/*
 * Count pixels in the rectangle that match `rgb` within `tol`.
 * Used to assert a glyph or dot actually painted, without depending on exact
 * antialiasing behavior.
 */
int harness_count_near(int x, int y, int w, int h, uint32_t rgb, int tol);

/*
 * Bounding box of all pixels differing from `bg` by more than `tol`.
 * Returns false if the region is empty (nothing was drawn).
 * Used to assert text landed at the right baseline and within the column.
 */
_Bool harness_ink_bounds(int x, int y, int w, int h, uint32_t bg, int tol,
                         int *out_x0, int *out_y0, int *out_x1, int *out_y1);

/*
 * Write the framebuffer to a PPM file, for eyeballing a failure.
 * Best-effort: failures to write are ignored.
 */
void harness_dump_ppm(const char *path);
