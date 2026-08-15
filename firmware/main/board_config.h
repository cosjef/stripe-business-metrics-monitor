/*
 * Board configuration for Waveshare ESP32-S3-LCD-1.54 (non-touch variant).
 *
 * Pin assignments and panel settings verified against Waveshare's own ESP-IDF
 * example: examples/ESP32-S3-LCD-1.54-demo/ESP-IDF-5.5.1/05_lvgl_example
 *
 * Panel: ST7789, 240x240 IPS, 4-wire SPI.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"

/* Panel geometry */
#define LCD_H_RES (240)
#define LCD_V_RES (240)

/* SPI / panel settings */
#define LCD_SPI_NUM         (SPI3_HOST)
#define LCD_PIXEL_CLK_HZ    (40 * 1000 * 1000)  /* 40MHz; spec 5.3 timing assumes this */
#define LCD_CMD_BITS        (8)
#define LCD_PARAM_BITS      (8)
#define LCD_BITS_PER_PIXEL  (16)                /* RGB565 */
#define LCD_BL_ON_LEVEL     (1)                 /* backlight is active high */

/* LCD pins */
#define LCD_GPIO_SCLK (GPIO_NUM_38)
#define LCD_GPIO_MOSI (GPIO_NUM_39)
#define LCD_GPIO_RST  (GPIO_NUM_40)
#define LCD_GPIO_DC   (GPIO_NUM_45)
#define LCD_GPIO_CS   (GPIO_NUM_21)
#define LCD_GPIO_BL   (GPIO_NUM_46)

/* LVGL draw buffer: 50 lines, double buffered, matching Waveshare's example */
#define LCD_DRAW_BUFF_HEIGHT (50)
#define LCD_DRAW_BUFF_DOUBLE (1)
