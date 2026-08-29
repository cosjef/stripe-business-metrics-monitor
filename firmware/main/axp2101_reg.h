/*
 * AXP2101 register map and voltage encoding.
 *
 * Split from the I2C driver so the arithmetic is host-testable. Getting a
 * voltage encoding wrong is worse than a blank screen -- an over-volted rail
 * can damage the panel -- so this is checked rather than trusted.
 *
 * Register addresses from XPowersLib's AXP2101Constants.h; the board's chosen
 * voltages from Waveshare's own power_bsp.cpp for this product.
 */
#pragma once

/* One register holds the on/off bit for every LDO rail. */
#define AXP_REG_LDO_ONOFF_CTRL0  (0x90)

/* Per-rail voltage registers are consecutive from here. */
#define AXP_REG_ALDO1_VOL        (0x92)

/* ALDO rails: 500mV to 3500mV in 100mV steps. */
#define AXP_ALDO_MV_MIN   (500)
#define AXP_ALDO_MV_MAX   (3500)
#define AXP_ALDO_MV_STEP  (100)

/*
 * What this board wants. Waveshare set DC1 and ALDO1-4 all to 3.3V; the panel
 * rails are among them, and their boot log shows the PMIC coming up before the
 * display bus for exactly that reason.
 */
#define AXP_BOARD_ALDO_MV (3300)

/*
 * Which rail is wired to the panel's reset pin.
 *
 * The CO5300's RST is not connected to an MCU GPIO on this board -- it hangs
 * off ALDO3. Cycling that rail IS the panel reset, and skipping it leaves the
 * controller indeterminate with a black screen despite every init step
 * reporting success.
 */
#define AXP_PANEL_RESET_ALDO (3)

/*
 * Encode a millivolt value as a register step, or -1 if invalid.
 *
 * Refuses rather than clamps. A caller asking for 5V has a bug that should
 * surface, and silently turning it into 3.5V hides it; likewise a request that
 * is not a whole step is a misunderstanding worth reporting.
 */
int axp_aldo_encode(int millivolts);

/* Voltage register for ALDO n (1-4), or -1 if out of range. */
int axp_aldo_vol_reg(int n);

/* Enable bit mask for ALDO n (1-4) within AXP_REG_LDO_ONOFF_CTRL0, or 0. */
int axp_aldo_enable_bit(int n);
