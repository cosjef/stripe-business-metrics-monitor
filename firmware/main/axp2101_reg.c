/*
 * AXP2101 register encoding. See axp2101_reg.h.
 */
#include "axp2101_reg.h"

int axp_aldo_encode(int millivolts)
{
    if (millivolts < AXP_ALDO_MV_MIN || millivolts > AXP_ALDO_MV_MAX) {
        return -1;
    }

    /* Must land exactly on a step. Rounding here would silently accept a
     * caller's misunderstanding of the hardware's granularity. */
    if (((millivolts - AXP_ALDO_MV_MIN) % AXP_ALDO_MV_STEP) != 0) {
        return -1;
    }

    return (millivolts - AXP_ALDO_MV_MIN) / AXP_ALDO_MV_STEP;
}

int axp_aldo_vol_reg(int n)
{
    if (n < 1 || n > 4) {
        return -1;
    }
    return AXP_REG_ALDO1_VOL + (n - 1);
}

int axp_aldo_enable_bit(int n)
{
    if (n < 1 || n > 4) {
        return 0;
    }
    return 1 << (n - 1);
}
