file(GLOB FONT_SRCS "../../core/fonts/*.c")

idf_component_register(
    SRCS "main.c" "display.c" "../../core/src/hero_size.c" "../../core/src/baseline.c" "../../core/src/screens.c" "../../core/src/tapdetect.c" "../../core/src/tapstatus.c" "../../core/src/provision.c" "settings.c" "wifi.c" "portal.c" "../../core/src/stripe_key.c" "stripe_api.c" "../../core/src/mrr.c" "../../core/src/stripe_parse.c" "../../core/src/format.c" "../../core/src/events.c" "../../core/src/freshness.c" "../../core/src/rotation.c" "../../core/src/cache.c" "button.c" "../../core/src/state.c" "../../core/src/wifi_retry.c" "battery.c" "battery_hw.c" "../../core/src/orientation.c" "axp2101.c" "../../core/src/jsonstream.c" "axp2101_reg.c" "colortest.c" ${FONT_SRCS}
    INCLUDE_DIRS "." "../../core/include" "../../core/fonts"
    REQUIRES esp_driver_i2c esp_timer nvs_flash esp_wifi esp_http_server esp_netif esp_http_client esp-tls mbedtls json
)
