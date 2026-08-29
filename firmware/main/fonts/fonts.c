/*
 * Font lookup. See fonts.h for why there are two ladders.
 */
#include "fonts.h"

const lv_font_t *font_for_size(int size_px)
{
    switch (size_px) {
    case 18: return &stripe_sans_18;
    case 20: return &stripe_sans_20;
    case 22: return &stripe_sans_22;
    case 24: return &stripe_sans_24;
    case 26: return &stripe_sans_26;
    case 29: return &stripe_sans_29;
    case 31: return &stripe_sans_31;
    case 32: return &stripe_sans_32;
    case 35: return &stripe_sans_35;
    case 40: return &stripe_sans_40;
    case 46: return &stripe_sans_46;
    case 52: return &stripe_sans_52;
    case 58: return &stripe_sans_58;
    case 60: return &stripe_sans_60;
    case 64: return &stripe_sans_64;
    case 74: return &stripe_sans_74;
    case 76: return &stripe_sans_76;
    case 86: return &stripe_sans_86;
    case 88: return &stripe_sans_88;
    case 92: return &stripe_sans_92;
    case 96: return &stripe_sans_96;
    case 108: return &stripe_sans_108;
    case 126: return &stripe_sans_126;
    case 137: return &stripe_sans_137;
    default: break;
    }

    /* Unknown size: fall back to the smallest rather than returning NULL,
     * which LVGL would dereference. A wrong-sized label is a visible bug; a
     * crash is not diagnosable from the glass. */
    return &stripe_sans_18;
}
