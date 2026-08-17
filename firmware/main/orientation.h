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

/* The orientation this enclosure ships with: USB-C at the bottom. */
#define DISPLAY_ROTATION_DEGREES 180
