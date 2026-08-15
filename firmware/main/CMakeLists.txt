file(GLOB FONT_SRCS "fonts/*.c")

idf_component_register(
    SRCS "main.c" "display.c" "hero_size.c" "baseline.c" "screens.c" "imu.c" "tapdetect.c" "tapstatus.c" "colortest.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
    REQUIRES esp_driver_i2c esp_timer
)
