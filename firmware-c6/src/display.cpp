/*
 * Display bring-up on Arduino_GFX, plus the LVGL flush hook.
 *
 * The ESP-IDF build reached exactly this point and stopped: panel init
 * reported success and the glass stayed dark. Two things in here are the
 * likely reason, and both are absent from the IDF path:
 *
 *   1. The manufacturer page-0x20 driving-voltage registers (0x19 / 0x1C).
 *      Arduino_CO5300::begin() issues SLPOUT, pixel format, brightness,
 *      DISPON and MADCTL, but not these. Clawdmeter's comment is explicit:
 *      without them "the panel stays black even with the rails up" -- which
 *      is our exact symptom.
 *
 *   2. The ALDO3 reset pulse, which happens in main.cpp before this runs.
 *
 * Until the glass is confirmed, treat every "success" here as meaningless:
 * on this board every layer reports OK even when nothing is reaching the
 * panel. Only the glass is evidence.
 */
#include "display.h"

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>

#include "board.h"

/* Quarter turns, 0-3. Set from the test pattern; see the constructor below. */
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 3
#endif

static Arduino_DataBus *s_bus = nullptr;
static Arduino_CO5300  *s_gfx = nullptr;

/* LVGL draw buffers.
 *
 * 40 lines, single-buffered: 480 * 40 * 2 = 38.4KB. The C6 has no PSRAM and
 * roughly 15KB in its largest free block once WiFi and TLS are up, so these
 * are allocated once at boot, before the network stack claims anything, and
 * never freed. Double-buffering would double this for no benefit at a 5s
 * refresh cadence. */
#define DRAW_LINES 40
static uint8_t *s_buf1 = nullptr;

static lv_display_t *s_disp = nullptr;

/*
 * Panel driving voltages, on manufacturer command page 0x20.
 *
 * Not part of Arduino_CO5300::begin(). Without these the panel stays black
 * with the rails up and every write still succeeding.
 */
static void send_panel_driving_init(Arduino_DataBus *bus)
{
    bus->beginWrite();
    bus->writeC8D8(0xFE, 0x20);   /* enter manufacturer command page 0x20 */
    bus->writeC8D8(0x19, 0x10);   /* panel driving voltage */
    bus->writeC8D8(0x1C, 0xA0);   /* panel driving voltage */
    bus->writeC8D8(0xFE, 0x00);   /* back to the user command page */
    bus->endWrite();
    delay(20);
}

/*
 * LVGL flush.
 *
 * The CO5300 needs even-aligned windows, so the area is rounded outward
 * before the blit. LVGL's own rounding callback is not used because the area
 * has to be widened, not narrowed -- clipping to an odd boundary drops a
 * column and shears the image.
 */
static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    const int32_t x1 = area->x1 & ~1;
    const int32_t y1 = area->y1 & ~1;
    const int32_t x2 = area->x2 | 1;
    const int32_t y2 = area->y2 | 1;

    s_gfx->draw16bitRGBBitmap(x1, y1, (uint16_t *)px,
                              x2 - x1 + 1, y2 - y1 + 1);
    lv_display_flush_ready(disp);
}

static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

bool display_init(void)
{
    s_bus = new Arduino_ESP32QSPI(LCD_CS, LCD_SCLK,
                                  LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);

    /* (bus, rst, rotation, w, h, col_off1, row_off1, col_off2, row_off2).
     * No reset GPIO on this board -- ALDO3 is the reset, pulsed in main.cpp.
     * The panel is full-width, so every offset is 0. */
    /*
     * Rotation is settled by the corner-marked test pattern, not by eye.
     *
     * At 0 the panel renders 90 degrees off the enclosure: a label placed at
     * the top-left appeared bottom-left with all text running vertically.
     * A square panel makes that easy to miss -- it still looks like a
     * rendered UI, which is exactly why this was measured rather than
     * glanced at.
     */
    s_gfx = new Arduino_CO5300(s_bus, GFX_NOT_DEFINED, DISPLAY_ROTATION,
                               LCD_WIDTH, LCD_HEIGHT, 0, 0, 0, 0);

    if (!s_gfx->begin()) {
        Serial.println("gfx begin failed");
        return false;
    }

    send_panel_driving_init(s_bus);
    s_gfx->fillScreen(0x0000);
    s_gfx->setBrightness(200);
    return true;
}

bool display_lvgl_init(void)
{
    lv_init();

    s_buf1 = (uint8_t *)heap_caps_malloc(LCD_WIDTH * DRAW_LINES * 2,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_buf1) {
        Serial.println("LVGL draw buffer alloc failed");
        return false;
    }

    s_disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    if (!s_disp) {
        return false;
    }
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_buf1, nullptr,
                           LCD_WIDTH * DRAW_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, nullptr);
    return true;
}

void display_fill(uint16_t color)
{
    if (s_gfx) {
        s_gfx->fillScreen(color);
    }
}

void display_brightness(uint8_t level)
{
    if (s_gfx) {
        s_gfx->setBrightness(level);
    }
}
