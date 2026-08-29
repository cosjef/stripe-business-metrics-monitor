/*
 * Panel orientation.
 *
 * Split out from display.h so it carries no ESP-IDF or LVGL dependency and can
 * be asserted on the host. display.c hands these values straight to
 * esp_lvgl_port.
 *
 * The enclosure puts the USB-C port at the bottom, but the panel's native scan
 * order assumes it at the top, so the image arrives upside down. Correcting it
 * is a 180 degree rotation: mirror both axes, swap neither.
 *
 * Swapping the axes transposes to landscape. On a square 240x240 panel that
 * still fills the screen and still looks like a rendered UI, so it reads as a
 * plausible mistake rather than an obvious failure -- the kind of bug that
 * survives a glance. That is why this is tested rather than eyeballed.
 */
#pragma once

typedef struct {
    _Bool swap_xy;
    _Bool mirror_x;
    _Bool mirror_y;
} display_orientation_t;

/*
 * Orientation for a rotation in degrees.
 *
 * 0 and 180 are the meaningful values for this enclosure. 90 and 270 are
 * defined for completeness and transpose to landscape. Anything else falls
 * back to native: an upside-down display is a visible bug, whereas a silently
 * transposed one is not.
 */
display_orientation_t display_orientation(int degrees);

/*
 * Next angle in the cycle: 0 -> 90 -> 180 -> 270 -> 0. Anything unsupported
 * restarts at 0.
 *
 * Backs the button-driven orientation picker. The USB-C port sits on the side
 * of this enclosure, and which quarter turn puts the text upright is a fact
 * about the physical case -- faster to settle by eye than to derive.
 */
int display_orientation_next(int degrees);

/*
 * The orientation this enclosure ships with.
 *
 * 0 on the C6. The S3 used 90 because its enclosure put the USB-C port on the
 * side, but that is a fact about that case and does not carry over.
 *
 * It also cannot: the SH8601 driver rejects swap_xy outright --
 *
 *     E (516) sh8601: swap_xy is not supported by this panel
 *
 * -- so 90 and 270 are unavailable on this hardware regardless of what the
 * case wants. Only 0 and 180 are reachable, and a half turn is a mirror on
 * both axes, which the panel does support.
 *
 * Set by eye once the enclosure is known. See the note in orientation.c about
 * the quarter turns being defined but unusable here.
 */
#define DISPLAY_ROTATION_DEGREES 0
