#include "wifi.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "wifi";

/* Setup AP: open, one client at a time. Only the customer's phone needs to
 * reach the portal, and a higher limit invites confusion when several devices
 * associate at once. */
#define AP_MAX_CONNECTIONS 1
#define AP_CHANNEL         1

/*
 * Station retry policy.
 *
 * Bounded rather than infinite: after this many failures the device shows the
 * failure on screen instead of retrying silently forever, which is spec 4's
 * "never lie" principle applied to connectivity.
 */
#define STA_MAX_RETRIES 5

static wifi_state_t s_state = WIFI_STATE_IDLE;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;
static int s_retries = 0;
static esp_ip4_addr_t s_ip = {0};

static void on_wifi_event(void *arg, esp_event_base_t base,
                          int32_t id, void *data)
{
    (void)arg;
    (void)data;

    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
            if (s_retries < STA_MAX_RETRIES) {
                s_retries++;
                ESP_LOGW(TAG, "disconnected, retry %d/%d", s_retries, STA_MAX_RETRIES);
                esp_wifi_connect();
                s_state = WIFI_STATE_CONNECTING;
            } else {
                ESP_LOGE(TAG, "giving up after %d attempts", STA_MAX_RETRIES);
                s_state = WIFI_STATE_FAILED;
            }
            break;

        case WIFI_EVENT_AP_STACONNECTED:
            ESP_LOGI(TAG, "client joined the setup AP");
            break;

        case WIFI_EVENT_AP_STADISCONNECTED:
            ESP_LOGI(TAG, "client left the setup AP");
            break;

        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *e = (const ip_event_got_ip_t *)data;
        s_ip = e->ip_info.ip;
        s_retries = 0;
        s_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&s_ip));
    }
}

esp_err_t wifi_init(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop failed");

    s_netif_ap = esp_netif_create_default_wifi_ap();
    s_netif_sta = esp_netif_create_default_wifi_sta();

    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            on_wifi_event, NULL, NULL),
        TAG, "wifi event handler failed");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            on_wifi_event, NULL, NULL),
        TAG, "ip event handler failed");

    return ESP_OK;
}

esp_err_t wifi_start_ap(char *out_ssid, size_t out_ssid_len)
{
    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read mac failed");

    char ssid[SETUP_SSID_LEN];
    setup_ssid_from_mac(mac, ssid, sizeof(ssid));

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(ssid);
    cfg.ap.channel = AP_CHANNEL;
    cfg.ap.max_connection = AP_MAX_CONNECTIONS;
    cfg.ap.authmode = WIFI_AUTH_OPEN;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set AP mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    s_state = WIFI_STATE_AP;

    if (out_ssid && out_ssid_len) {
        strncpy(out_ssid, ssid, out_ssid_len - 1);
        out_ssid[out_ssid_len - 1] = '\0';
    }

    ESP_LOGI(TAG, "setup AP \"%s\" started (open, channel %d)", ssid, AP_CHANNEL);
    return ESP_OK;
}

esp_err_t wifi_start_sta(const char *ssid, const char *pass)
{
    ESP_RETURN_ON_FALSE(ssid && ssid[0], ESP_ERR_INVALID_ARG, TAG, "empty ssid");

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    if (pass) {
        strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    }

    /* Accept whatever the access point offers. Pinning a minimum authmode here
     * would reject legitimate open and WEP networks that a customer may be
     * required to use. */
    cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    s_retries = 0;
    s_state = WIFI_STATE_CONNECTING;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set STA mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "STA config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    ESP_LOGI(TAG, "joining \"%s\"", ssid);
    return ESP_OK;
}

esp_err_t wifi_start_apsta(char *out_ap_ssid, size_t out_ap_ssid_len,
                           const char *sta_ssid, const char *sta_pass)
{
    ESP_RETURN_ON_FALSE(sta_ssid && sta_ssid[0], ESP_ERR_INVALID_ARG,
                        TAG, "empty sta ssid");

    uint8_t mac[6] = {0};
    ESP_RETURN_ON_ERROR(esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP), TAG, "read mac failed");

    char ap_ssid[SETUP_SSID_LEN];
    setup_ssid_from_mac(mac, ap_ssid, sizeof(ap_ssid));

    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    ap_cfg.ap.channel = AP_CHANNEL;
    ap_cfg.ap.max_connection = AP_MAX_CONNECTIONS;
    ap_cfg.ap.authmode = WIFI_AUTH_OPEN;

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, sta_ssid, sizeof(sta_cfg.sta.ssid) - 1);
    if (sta_pass) {
        strncpy((char *)sta_cfg.sta.password, sta_pass,
                sizeof(sta_cfg.sta.password) - 1);
    }
    sta_cfg.sta.threshold.authmode = WIFI_AUTH_OPEN;

    s_retries = 0;
    s_state = WIFI_STATE_CONNECTING;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "set APSTA failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg), TAG, "AP config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg), TAG, "STA config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    if (out_ap_ssid && out_ap_ssid_len) {
        strncpy(out_ap_ssid, ap_ssid, out_ap_ssid_len - 1);
        out_ap_ssid[out_ap_ssid_len - 1] = '\0';
    }

    ESP_LOGI(TAG, "AP \"%s\" up, joining \"%s\"", ap_ssid, sta_ssid);
    return ESP_OK;
}

wifi_state_t wifi_get_state(void)
{
    return s_state;
}

void wifi_get_ip(char *out, size_t out_len)
{
    if (!out || out_len == 0) {
        return;
    }
    snprintf(out, out_len, IPSTR, IP2STR(&s_ip));
}
