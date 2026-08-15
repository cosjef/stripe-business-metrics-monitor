#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

static const char *TAG = "settings";

#define NVS_NAMESPACE "stripedev"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();

    /* A partition from an older layout, or one truncated by a failed write,
     * shows up here. Erasing loses stored credentials -- the device falls back
     * to setup mode -- which is much better than refusing to boot. */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing and reinitializing",
                 esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

bool settings_have_wifi(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        return false;
    }

    size_t len = 0;
    const esp_err_t err = nvs_get_str(h, KEY_WIFI_SSID, NULL, &len);
    nvs_close(h);

    /* An empty stored SSID counts as unconfigured: it cannot be joined, and
     * treating it as configured would strand the device out of setup mode. */
    return err == ESP_OK && len > 1;
}

esp_err_t settings_get_wifi(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = ssid_len;
    err = nvs_get_str(h, KEY_WIFI_SSID, ssid, &len);
    if (err != ESP_OK) {
        nvs_close(h);
        return err;
    }

    /* A missing passphrase is normal -- open networks store none. */
    len = pass_len;
    if (nvs_get_str(h, KEY_WIFI_PASS, pass, &len) != ESP_OK) {
        pass[0] = '\0';
    }

    nvs_close(h);
    return ESP_OK;
}

esp_err_t settings_set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(h, KEY_WIFI_SSID, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(h, KEY_WIFI_PASS, pass ? pass : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);

    if (err == ESP_OK) {
        /* Log the SSID but never the passphrase: this console output is not
         * private, and support sessions routinely involve sharing it. */
        ESP_LOGI(TAG, "stored credentials for \"%s\"", ssid);
    }

    return err;
}

esp_err_t settings_clear_wifi(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }

    /* NOT_FOUND is success here: the goal is "no credentials stored". */
    err = nvs_erase_key(h, KEY_WIFI_SSID);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        const esp_err_t perr = nvs_erase_key(h, KEY_WIFI_PASS);
        if (perr != ESP_OK && perr != ESP_ERR_NVS_NOT_FOUND) {
            err = perr;
        }
    }
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }

    nvs_close(h);
    ESP_LOGI(TAG, "cleared stored credentials");
    return err;
}
