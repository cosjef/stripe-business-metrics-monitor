#include "fonts.h"

const lv_font_t *font_for_size(int size_px)
{
    switch (size_px) {
    case 18: return &stripe_sans_18;
    case 20: return &stripe_sans_20;
    case 22: return &stripe_sans_22;
    case 24: return &stripe_sans_24;
    case 32: return &stripe_sans_32;
    case 40: return &stripe_sans_40;
    case 52: return &stripe_sans_52;
    case 60: return &stripe_sans_60;
    case 64: return &stripe_sans_64;
    case 76: return &stripe_sans_76;
    case 88: return &stripe_sans_88;
    case 96: return &stripe_sans_96;
    default: return NULL;
    }
}
