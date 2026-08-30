/*
 * Panel orientation. See orientation.h for why this is separate and tested.
 */
#include "orientation.h"

display_orientation_t display_orientation(int degrees)
{
    switch (degrees) {
    case 180:
        /* Both axes mirrored, neither swapped: a true half turn. */
        return (display_orientation_t){
            .swap_xy = 0, .mirror_x = 1, .mirror_y = 1,
        };

    /*
     * The quarter turns transpose, which the SH8601 panel on the C6 does not
     * support -- it logs "swap_xy is not supported by this panel" and ignores
     * the request. They are kept defined because the logic is panel-agnostic
     * and the S3's ST7789 does support them, but on C6 hardware only 0 and 180
     * are actually reachable.
     */
    case 90:
        return (display_orientation_t){
            .swap_xy = 1, .mirror_x = 1, .mirror_y = 0,
        };

    case 270:
        return (display_orientation_t){
            .swap_xy = 1, .mirror_x = 0, .mirror_y = 1,
        };

    case 0:
    default:
        /* Native scan order, and the fallback for anything unsupported. */
        return (display_orientation_t){
            .swap_xy = 0, .mirror_x = 0, .mirror_y = 0,
        };
    }
}

int display_orientation_next(int degrees)
{
    switch (degrees) {
    case 0:   return 90;
    case 90:  return 180;
    case 180: return 270;
    case 270: return 0;
    default:  return 0;
    }
}
