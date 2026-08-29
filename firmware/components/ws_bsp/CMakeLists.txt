# Waveshare's own BSP for the ESP32-C6-Touch-AMOLED-2.16, vendored verbatim.
#
# These files are copied unmodified from their 09_LVGL_V9_Test example, which
# was flashed to this board and confirmed to drive the panel correctly. Every
# attempt to reimplement their bring-up in our own display.c produced an
# identical boot log and a dark screen, so the difference lives somewhere the
# logs do not show. Rather than keep hunting it, we use their code.
#
# Do not edit these files. If something needs changing, change how we call
# them -- the whole value here is that they are known-good and unmodified.
set(src_dirs ./ ./src)
set(include_dirs ./ ./src ./src/REG)

idf_component_register(
  SRC_DIRS ${src_dirs}
  INCLUDE_DIRS ${include_dirs}
  REQUIRES driver esp_timer esp_lcd esp_driver_i2c esp_driver_spi
           esp_lcd_touch esp_lcd_touch_cst9217 esp_lcd_sh8601
           esp_lvgl_adapter lvgl
)
add_compile_definitions(XPOWERS_CHIP_AXP2101 CONFIG_XPOWERS_ESP_IDF_NEW_API)
