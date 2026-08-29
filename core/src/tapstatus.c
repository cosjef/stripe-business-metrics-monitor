/*
 * QMI8658 TAP_STATUS decoding. See tapstatus.h for the register layout.
 */
#include "tapstatus.h"

tap_status_t tap_status_decode(uint8_t raw)
{
    tap_status_t s;

    /* Bits 6:4 -- THREE bits. Masking with 0x03 truncates the field, which
     * corrupts any value with bit 6 set and made real taps decode as "no
     * axis". That bug silently dropped taps for several sessions. */
    s.axis = (tap_axis_t)((raw >> 4) & 0x07);
    s.num = (tap_num_t)(raw & 0x03);
    s.negative = (raw & 0x80) != 0;

    /* The datasheet defines only 0-3 for the axis field; treat anything else
     * as "none" rather than passing an out-of-range value to callers. */
    if (s.axis > TAP_AXIS_Z) {
        s.axis = TAP_AXIS_NONE;
    }

    return s;
}

_Bool tap_status_is_real(tap_status_t s)
{
    if (s.axis == TAP_AXIS_NONE) {
        return 0;
    }

    /* TAP_NUM 3 is documented as N/A, and 0 means no tap. */
    return s.num == TAP_NUM_SINGLE || s.num == TAP_NUM_DOUBLE;
}
