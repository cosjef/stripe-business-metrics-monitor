/*
 * Waveshare ESP32-C6-Touch-AMOLED-2.16 pin map.
 *
 * These match firmware/main/board_config.h on the ESP-IDF side and
 * Clawdmeter's board.h for the same board, which is running hardware. The
 * facts worth not re-deriving:
 *
 *   - LCD_CS is GPIO15, not GPIO5. Waveshare's own ESP-IDF example says 5 and
 *     is wrong for this board; it has CS and touch-INT the other way round.
 *     A wrong CS is silent -- QSPI init, the vendor sequence and the
 *     brightness write all report success because nothing is listening.
 *
 *   - There is no LCD reset GPIO. Reset is the AXP2101's ALDO3 rail; cycling
 *     that rail is the reset (see power_up() in main.cpp).
 *
 *   - There is no backlight GPIO. This is an emissive panel: brightness is a
 *     panel command, black pixels are off, and power scales with lit area.
 */
#pragma once

#define BOARD_NAME     "Waveshare ESP32-C6-Touch-AMOLED-2.16"

#define LCD_WIDTH      480
#define LCD_HEIGHT     480

/* QSPI display pins (CO5300). */
#define LCD_CS         15
#define LCD_SCLK       0
#define LCD_SDIO0      1
#define LCD_SDIO1      2
#define LCD_SDIO2      3
#define LCD_SDIO3      4

/* Shared I2C bus: touch, PMIC, IMU, RTC. */
#define IIC_SDA        8
#define IIC_SCL        7

/* Touch (CST9217/CST9220). Not used yet; recorded for the tap gesture. */
#define TP_INT         5
#define TP_RST         11
#define CST9220_ADDR   0x5A

/* Power management. */
#define AXP2101_ADDR   0x34

/* Side buttons. BOOT is the primary; PWR is the AXP2101 PKEY IRQ. */
#define BTN_BACK_GPIO  9
#define BTN_FWD_GPIO   10
