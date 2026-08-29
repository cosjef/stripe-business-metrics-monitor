/*
 * Board configuration for Waveshare ESP32-C6-Touch-AMOLED-2.16.
 *
 * Pin assignments taken from Waveshare's own ESP-IDF v5.5.3 example:
 *   02_Example/ESP-IDF-v5.5.3/09_LVGL_V9_Test/main/user_config.h
 * in waveshareteam/ESP32-C6-Touch-AMOLED-2.16.
 *
 * Panel: CO5300 driven by esp_lcd_sh8601, 480x480 AMOLED, QSPI.
 *
 * The LCD_ prefixes below are ESP-IDF's naming -- its display API is called
 * esp_lcd regardless of panel technology, and the driver logs "LCD panel
 * create success" for this AMOLED too. Kept for consistency with the API
 * rather than renamed, but this is an emissive panel, not a backlit one, and
 * that difference is real: there is no backlight to drive, black pixels are
 * off rather than dark, and power scales with how much of the screen is lit.
 * Touch: CST9220 driven by esp_lcd_touch_cst9217, I2C.
 * Power: AXP2101 PMIC at I2C 0x34.
 *
 * Two differences from the S3 board that matter for bring-up:
 *
 *   - There is no backlight GPIO. Brightness is a panel command through the
 *     driver, not PWM on a pin. AMOLED pixels emit their own light.
 *   - There is no reset GPIO. Panel reset goes over QSPI.
 *
 * The CO5300 and CST9220 part numbers come from Waveshare's product page and
 * appear nowhere in the example sources -- the only identifiers there are the
 * driver package names. Treat them as unverified until confirmed against the
 * silkscreen or an I2C scan.
 */
#pragma once
#include "driver/gpio.h"
#include "driver/spi_master.h"

/* Panel geometry */
#define LCD_H_RES (480)
#define LCD_V_RES (480)

/* QSPI. The panel uses four data lines, not the S3's single MOSI. */
#define LCD_SPI_NUM         (SPI2_HOST)
#define LCD_PIXEL_CLK_HZ    (40 * 1000 * 1000)
#define LCD_CMD_BITS        (32)   /* SH8601 protocol uses 32-bit commands */
#define LCD_PARAM_BITS      (8)
#define LCD_BITS_PER_PIXEL  (16)   /* RGB565 */

/* QSPI pins */
/*
 * CS is GPIO15, not GPIO5.
 *
 * Waveshare's own ESP-IDF example (user_config.h) says BSP_LCD_CS = 5 and
 * BSP_LCD_TOUCH_INT = 15. Clawdmeter, which demonstrably drives this exact
 * board, has them the other way round: LCD_CS = 15, TP_INT = 5, and states it
 * verified against the XiaoZhi BSP as well as the ESP-IDF examples.
 *
 * A wrong CS is consistent with everything we observed: QSPI init, the vendor
 * sequence and the brightness write all report success because nothing on the
 * bus is listening, and the panel stays black.
 *
 * Trying Clawdmeter'''s values, since theirs is running hardware and the
 * example is not.
 */
#define LCD_GPIO_CS    (GPIO_NUM_15)
#define LCD_GPIO_PCLK  (GPIO_NUM_0)
#define LCD_GPIO_DATA0 (GPIO_NUM_1)
#define LCD_GPIO_DATA1 (GPIO_NUM_2)
#define LCD_GPIO_DATA2 (GPIO_NUM_3)
#define LCD_GPIO_DATA3 (GPIO_NUM_4)

/* No backlight or reset pin on this board -- both are panel commands. */
#define LCD_GPIO_BL   (GPIO_NUM_NC)
#define LCD_GPIO_RST  (GPIO_NUM_NC)

/* Shared I2C bus: touch controller, PMIC, IMU, RTC. */
#define BSP_I2C_NUM   (I2C_NUM_0)
#define BSP_I2C_SCL   (GPIO_NUM_7)
#define BSP_I2C_SDA   (GPIO_NUM_8)

/* Touch */
#define TOUCH_GPIO_RST (GPIO_NUM_11)
#define TOUCH_GPIO_INT (GPIO_NUM_5)   /* swapped with CS, see above */

/* Power management */
#define AXP2101_I2C_ADDR (0x34)

/*
 * LVGL draw buffer.
 *
 * 60 lines double-buffered is 480*60*2*2 = 112KB of the C6's 512KB. A full
 * 480x480 framebuffer would be 450KB and leave nothing for WiFi, TLS or the
 * JSON parse. Revisit once the fetch path is paginated and its real footprint
 * is known.
 */
#define LCD_DRAW_BUFF_HEIGHT (100)   /* matches Waveshare's working example */
#define LCD_DRAW_BUFF_DOUBLE (1)
