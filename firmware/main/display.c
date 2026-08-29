#include "display.h"

#include "orientation.h"
#include "board_config.h"
#include "layout.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_sh8601.h"
#include "esp_lvgl_port.h"

static const char *TAG = "display";

static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static lv_display_t *s_lvgl_disp = NULL;

lv_display_t *display_handle(void)
{
    return s_lvgl_disp;
}


void display_backlight(_Bool on)
{
    /*
     * No backlight pin on this board. AMOLED pixels emit their own light, so
     * "off" means driving the panel off rather than dimming a lamp behind it.
     * Kept as a no-op for now so callers written for the S3 still compile;
     * brightness control is a panel command and comes with the AXP2101 work.
     */
    (void)on;
}


/*
 * Bring up the QSPI bus and the AMOLED panel.
 *
 * Three differences from the S3's ST7789 worth knowing:
 *
 *   - QSPI, not SPI. Four data lines rather than one MOSI, and no DC pin --
 *     the SH8601 protocol carries command/data selection inside 32-bit
 *     commands, which is why LCD_CMD_BITS is 32 here and 8 on the S3.
 *   - No reset GPIO. Panel reset is a command over the bus.
 *   - No backlight GPIO. AMOLED pixels emit their own light; brightness is a
 *     panel command, and black pixels are simply off.
 */

/*
 * CO5300 vendor init sequence, from Waveshare's own example for this board
 * (09_LVGL_V9_Test/components/port_bsp/display_bsp.cpp).
 *
 * The generic SH8601 driver brings the panel up but sends none of these, and
 * one of them is the difference between a working display and a black one:
 *
 *     {0x51, 0xFF}  -- display brightness, full
 *
 * On an AMOLED, brightness defaults to zero. Without this the panel
 * initialises correctly, LVGL renders correctly, and the screen stays black
 * because no pixel is emitting light. There is no backlight to check, which
 * makes it a confusing failure: everything logs success.
 *
 * The rest sets sleep-out, pixel format (0x3A = RGB565), the address window
 * for 480x480 (0x2A/0x2B, 0x01DF = 479), and display-on.
 */
static const sh8601_lcd_init_cmd_t co5300_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 600},   /* sleep out, 600ms settle */
    {0xFE, (uint8_t[]){0x20}, 1, 0},     /* page select */
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},     /* back to user page */
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},     /* 16bpp RGB565 */
    {0x35, (uint8_t[]){0x00}, 1, 0},     /* tearing effect on */
    /*
     * MADCTL 0x00, not 0x30.
     *
     * Waveshare's sequence uses 0x30, which sets MV (transpose) and MX
     * (mirror-X). That rotates the image 90 degrees in the panel, and the
     * SH8601 driver then refuses swap_xy, so there is no way to undo it --
     * the deck rendered sideways.
     *
     * Clawdmeter reached the same conclusion independently: "we deliberately
     * do NOT restore the old MADCTL 0x30 (MV transpose)."
     */
    {0x36, (uint8_t[]){0x00}, 1, 0},     /* memory access control, no transpose */
    {0x53, (uint8_t[]){0x20}, 1, 0},     /* brightness control on */
    {0x51, (uint8_t[]){0xFF}, 1, 0},     /* brightness FULL -- see above */
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},  /* col 0..479 */
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},  /* row 0..479 */
    {0x29, (uint8_t[]){0x00}, 0, 100},   /* display on */
};

static esp_err_t lcd_init(void)
{
    const spi_bus_config_t bus_cfg = {
        .sclk_io_num = LCD_GPIO_PCLK,
        .data0_io_num = LCD_GPIO_DATA0,
        .data1_io_num = LCD_GPIO_DATA1,
        .data2_io_num = LCD_GPIO_DATA2,
        .data3_io_num = LCD_GPIO_DATA3,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "QSPI bus init failed");

    const esp_lcd_panel_io_spi_config_t io_cfg =
        SH8601_PANEL_IO_QSPI_CONFIG(LCD_GPIO_CS, NULL, NULL);

    ESP_RETURN_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM,
                                 &io_cfg, &s_lcd_io),
        TAG, "panel IO init failed");

    const sh8601_vendor_config_t vendor_cfg = {
        .init_cmds = co5300_init_cmds,
        .init_cmds_size = sizeof(co5300_init_cmds) / sizeof(co5300_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };

    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = LCD_GPIO_RST,   /* GPIO_NUM_NC: reset is a command */
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
        .vendor_config = (void *)&vendor_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_lcd_io, &panel_cfg, &s_lcd_panel),
                        TAG, "SH8601 panel init failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_lcd_panel), TAG, "panel reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_lcd_panel), TAG, "panel init failed");

    /*
     * No invert_color here. The S3's ST7789 needed invert_color(true), proven
     * on glass after five wrong attempts. Whether this panel needs it is
     * unknown until colortest runs -- do not copy the S3 value on faith.
     */
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_lcd_panel, true),
                        TAG, "display on failed");

    ESP_LOGI(TAG, "SH8601 QSPI panel up at %dx%d", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}

static esp_err_t lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 1024 * 10,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    const display_orientation_t orient =
        display_orientation(DISPLAY_ROTATION_DEGREES);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = s_lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        /*
         * The enclosure puts the USB-C port at the bottom, opposite the panel's
         * native scan order, so the image needs a half turn. Values come from
         * orientation.c rather than being written inline, so the intent is
         * asserted on the host -- swapping the axes here instead of mirroring
         * them transposes to landscape, which on a square panel looks plausible
         * enough to ship by accident.
         */
        .rotation = {
            .swap_xy = orient.swap_xy,
            .mirror_x = orient.mirror_x,
            .mirror_y = orient.mirror_y,
        },
        .flags = {
            .buff_dma = true,
            /*
             * Byte order. The S3's ST7789 needed this true, confirmed on
             * glass. Whether the SH8601 does is unverified -- colortest will
             * say. Starting from Waveshare's example, which does not swap.
             */
            /*
             * Byte-swapped. LVGL writes RGB565 little-endian; this panel wants
             * it the other way, and without the swap the high and low bytes
             * land in the wrong channels -- off-white renders magenta and the
             * green accent renders blue. Changing rgb_ele_order to BGR does
             * NOT fix that: the fault is byte order within the pixel, not
             * channel order within the field. Tried BGR first and it was
             * worse.
             */
            .swap_bytes = true,
        },
    };

    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_lvgl_disp != NULL, ESP_FAIL, TAG, "lvgl_port_add_disp failed");

    return ESP_OK;
}

esp_err_t display_init(void)
{
    ESP_RETURN_ON_ERROR(lcd_init(), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(lvgl_init(), TAG, "LVGL init failed");
    /* State the geometry actually compiled in. The per-target #ifdefs are
     * easy to get wrong and impossible to confirm from the glass, so the
     * device says which set it is using. */
    ESP_LOGI(TAG, "display ready: %dx%d, panel %d, hero cap %dpx, column %dpx",
             LCD_H_RES, LCD_V_RES, PANEL_PX, SIZE_HERO_MAX, TEXT_COLUMN_PX);
    return ESP_OK;
}
