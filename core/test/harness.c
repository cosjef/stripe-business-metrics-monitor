#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * LVGL renders into this buffer in the same RGB565 format the panel uses, so
 * what the harness inspects has been through the same quantization the device
 * applies. Byte order matches the host, not the panel: `swap_bytes` on the
 * device is a wire-format concern below this layer.
 */
static uint16_t s_fb[HARNESS_W * HARNESS_H];
static uint8_t s_draw_buf[HARNESS_W * HARNESS_H * 2];

static lv_display_t *s_disp = NULL;
static _Bool s_inited = 0;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t w = area->x2 - area->x1 + 1;
    const uint16_t *src = (const uint16_t *)px_map;

    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            if (x >= 0 && x < HARNESS_W && y >= 0 && y < HARNESS_H) {
                s_fb[y * HARNESS_W + x] = src[(y - area->y1) * w + (x - area->x1)];
            }
        }
    }

    lv_display_flush_ready(disp);
}

void harness_init(void)
{
    if (s_inited) {
        return;
    }

    lv_init();

    s_disp = lv_display_create(HARNESS_W, HARNESS_H);
    lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(s_disp, flush_cb);
    lv_display_set_buffers(s_disp, s_draw_buf, NULL, sizeof(s_draw_buf),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    s_inited = 1;
}

void harness_render(void)
{
    /* LVGL needs its tick to advance for timers to run. Nothing here is
     * animated, so a fixed step is enough to flush pending redraws. */
    for (int i = 0; i < 4; i++) {
        lv_tick_inc(16);
        lv_timer_handler();
    }
    lv_refr_now(s_disp);
}

lv_obj_t *harness_screen(void)
{
    harness_init();
    return lv_screen_active();
}

uint32_t harness_pixel(int x, int y)
{
    if (x < 0 || x >= HARNESS_W || y < 0 || y >= HARNESS_H) {
        return 0;
    }

    const uint16_t v = s_fb[y * HARNESS_W + x];
    const int r5 = (v >> 11) & 0x1F;
    const int g6 = (v >> 5) & 0x3F;
    const int b5 = v & 0x1F;

    /* Expand back to 8 bits per channel the same way a panel would. */
    const uint32_t r = (uint32_t)((r5 << 3) | (r5 >> 2));
    const uint32_t g = (uint32_t)((g6 << 2) | (g6 >> 4));
    const uint32_t b = (uint32_t)((b5 << 3) | (b5 >> 2));

    return (r << 16) | (g << 8) | b;
}

int harness_r(uint32_t rgb) { return (int)((rgb >> 16) & 0xFF); }
int harness_g(uint32_t rgb) { return (int)((rgb >> 8) & 0xFF); }
int harness_b(uint32_t rgb) { return (int)(rgb & 0xFF); }

static int iabs(int v) { return v < 0 ? -v : v; }

_Bool harness_color_near(uint32_t a, uint32_t b, int tol)
{
    return iabs(harness_r(a) - harness_r(b)) <= tol &&
           iabs(harness_g(a) - harness_g(b)) <= tol &&
           iabs(harness_b(a) - harness_b(b)) <= tol;
}

int harness_count_near(int x, int y, int w, int h, uint32_t rgb, int tol)
{
    int n = 0;
    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= HARNESS_W || yy < 0 || yy >= HARNESS_H) {
                continue;
            }
            if (harness_color_near(harness_pixel(xx, yy), rgb, tol)) {
                n++;
            }
        }
    }
    return n;
}

_Bool harness_ink_bounds(int x, int y, int w, int h, uint32_t bg, int tol,
                         int *out_x0, int *out_y0, int *out_x1, int *out_y1)
{
    int x0 = HARNESS_W, y0 = HARNESS_H, x1 = -1, y1 = -1;

    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= HARNESS_W || yy < 0 || yy >= HARNESS_H) {
                continue;
            }
            if (!harness_color_near(harness_pixel(xx, yy), bg, tol)) {
                if (xx < x0) x0 = xx;
                if (yy < y0) y0 = yy;
                if (xx > x1) x1 = xx;
                if (yy > y1) y1 = yy;
            }
        }
    }

    if (x1 < 0) {
        return 0;
    }

    if (out_x0) *out_x0 = x0;
    if (out_y0) *out_y0 = y0;
    if (out_x1) *out_x1 = x1;
    if (out_y1) *out_y1 = y1;
    return 1;
}

void harness_dump_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }

    fprintf(f, "P6\n%d %d\n255\n", HARNESS_W, HARNESS_H);
    for (int y = 0; y < HARNESS_H; y++) {
        for (int x = 0; x < HARNESS_W; x++) {
            const uint32_t p = harness_pixel(x, y);
            const uint8_t rgb[3] = {
                (uint8_t)harness_r(p), (uint8_t)harness_g(p), (uint8_t)harness_b(p),
            };
            fwrite(rgb, 1, 3, f);
        }
    }

    fclose(f);
}
