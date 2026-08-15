file(GLOB FONT_SRCS "fonts/*.c")

idf_component_register(
    SRCS "main.c" "display.c" "hero_size.c" "baseline.c" "screens.c" "imu.c" "tapdetect.c" "tapstatus.c" "provision.c" "settings.c" "wifi.c" "portal.c" "stripe_key.c" "stripe_api.c" "colortest.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "fonts"
    REQUIRES esp_driver_i2c esp_timer nvs_flash esp_wifi esp_http_server esp_netif esp_http_client esp-tls mbedtls json
)
