/*
 * LVGL configuration for the HOST TEST HARNESS only.
 *
 * The device build configures LVGL through Kconfig (see sdkconfig.defaults);
 * this file exists because a host build has no Kconfig. The settings that
 * affect rendering MUST match the device, or the harness would be asserting
 * against a different configuration than the one that ships:
 *
 *   LV_COLOR_DEPTH 16          <- CONFIG_LV_COLOR_DEPTH_16=y
 *   dark default theme         <- CONFIG_LV_THEME_DEFAULT_DARK=y
 *
 * Note the byte swap the panel needs (`swap_bytes` in the lvgl_port display
 * config) is deliberately NOT modeled here: it is a wire-format concern
 * between LVGL and the panel, below the level this harness inspects.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* --- must match the device --- */
#define LV_COLOR_DEPTH 16

#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_THEME_DEFAULT_GROW 1
#define LV_THEME_DEFAULT_TRANSITION_TIME 80

/* --- host memory: generous, we are not on an MCU --- */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING    LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (2 * 1024 * 1024)

/* Host tick comes from lv_tick_inc() in the harness. */
#define LV_USE_OS 0
#define LV_TICK_CUSTOM 0

/* --- fonts ---
 * Only Montserrat 14 is built in, as the default LVGL needs a valid pointer
 * for. All real text uses the generated Roboto Condensed faces, which are
 * compiled in as separate translation units. */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/* --- keep the build small and deterministic --- */
#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_DRAW_BUF_STRIDE_ALIGN 1
#define LV_DRAW_BUF_ALIGN 4

#endif /* LV_CONF_H */
