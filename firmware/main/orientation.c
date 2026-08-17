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
