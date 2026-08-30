/*
 * Font lookup. See fonts.h for why there are two ladders.
 */
#include "fonts.h"

const lv_font_t *font_for_size(int size_px)
{
    switch (size_px) {
    case 26: return &stripe_sans_26;
    case 29: return &stripe_sans_29;
    case 31: return &stripe_sans_31;
    case 35: return &stripe_sans_35;
    case 46: return &stripe_sans_46;
    case 58: return &stripe_sans_58;
    case 74: return &stripe_sans_74;
    case 86: return &stripe_sans_86;
    case 92: return &stripe_sans_92;
    case 104: return &stripe_sans_104;
    case 108: return &stripe_sans_108;
    case 120: return &stripe_sans_120;
    case 126: return &stripe_sans_126;
    case 137: return &stripe_sans_137;
    default: break;
    }

    /* Unknown size: fall back to the smallest we ship rather than returning
     * NULL, which LVGL would dereference. A wrong-sized label is a visible
     * bug; a crash is not diagnosable from the glass. */
    return &stripe_sans_26;
}
