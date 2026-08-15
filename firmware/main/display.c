#include "display.h"
#include "board_config.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
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
    gpio_set_level(LCD_GPIO_BL, on ? LCD_BL_ON_LEVEL : !LCD_BL_ON_LEVEL);
}

static esp_err_t lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    const gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << LCD_GPIO_BL,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&bk_gpio_config), TAG, "backlight gpio failed");

    ESP_LOGI(TAG, "initializing SPI bus");
    const spi_bus_config_t buscfg = {
        .sclk_io_num = LCD_GPIO_SCLK,
        .mosi_io_num = LCD_GPIO_MOSI,
        .miso_io_num = GPIO_NUM_NC,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_SPI_NUM, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "SPI init failed");

    ESP_LOGI(TAG, "installing panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_GPIO_DC,
        .cs_gpio_num = LCD_GPIO_CS,
        .pclk_hz = LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = LCD_CMD_BITS,
        .lcd_param_bits = LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_GOTO_ON_ERROR(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_NUM, &io_config, &s_lcd_io),
        err, TAG, "new panel IO failed");

    ESP_LOGI(TAG, "installing ST7789 driver");
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_GPIO_RST,
        .color_space = ESP_LCD_COLOR_SPACE_RGB,
        .bits_per_pixel = LCD_BITS_PER_PIXEL,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_st7789(s_lcd_io, &panel_config, &s_lcd_panel),
                      err, TAG, "new panel failed");

    ESP_ERROR_CHECK(esp_lcd_panel_reset(s_lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(s_lcd_panel));
    /* Required for this panel. Confirmed against Waveshare's own example, and
     * confirmed on glass: setting this false renders a fully inverted image
     * (white field, dark text). */
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(s_lcd_panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_lcd_panel, true));

    display_backlight(true);
    return ret;

err:
    if (s_lcd_panel) {
        esp_lcd_panel_del(s_lcd_panel);
        s_lcd_panel = NULL;
    }
    if (s_lcd_io) {
        esp_lcd_panel_io_del(s_lcd_io);
        s_lcd_io = NULL;
    }
    spi_bus_free(LCD_SPI_NUM);
    return ret;
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

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = s_lcd_panel,
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            /* RGB565 over SPI needs the byte order swapped for this panel.
             * This is the ONLY place it happens: CONFIG_LV_COLOR_16_SWAP was
             * an LVGL 8 option and does not exist in LVGL 9, so setting it in
             * sdkconfig has no effect. */
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
    ESP_LOGI(TAG, "display ready: %dx%d", LCD_H_RES, LCD_V_RES);
    return ESP_OK;
}
