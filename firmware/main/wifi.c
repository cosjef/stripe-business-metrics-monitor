#include "wifi.h"

#include "wifi_retry.h"

#include "esp_timer.h"

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
 * Station retry policy lives in wifi_retry.c, host-tested.
 *
 * It was previously a flat bound of 5 attempts here, with the counter reset
 * only on a successful join. That permanently disabled connectivity after five
 * disconnects accumulated over the device's lifetime -- the device sat on the
 * stale screen forever and only a power cycle recovered it. The bound is
 * correct for first-run provisioning and wrong for a device already proven on
 * the network; wifi_retry_t makes that distinction.
 */

static wifi_state_t s_state = WIFI_STATE_IDLE;
static esp_netif_t *s_netif_ap = NULL;
static esp_netif_t *s_netif_sta = NULL;
static wifi_retry_t s_retry;
static esp_ip4_addr_t s_ip = {0};

/* Timer that drives delayed reconnection, so backoff does not block the event
 * loop or spin the radio. */
static esp_timer_handle_t s_reconnect_timer = NULL;

static void reconnect_timer_cb(void *arg)
{
    (void)arg;
    esp_wifi_connect();
}

/*
 * Schedule the next association attempt after the policy's backoff.
 *
 * A zero delay connects immediately: most drops are momentary and recover at
 * once, so the first retry should not wait.
 */
static void schedule_reconnect(void)
{
    const int delay_ms = wifi_retry_delay_ms(&s_retry);

    if (delay_ms <= 0) {
        esp_wifi_connect();
        return;
    }

    if (s_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = reconnect_timer_cb,
            .name = "wifi_reconnect",
        };
        if (esp_timer_create(&args, &s_reconnect_timer) != ESP_OK) {
            /* Without a timer, fall back to connecting immediately rather
             * than not reconnecting at all. */
            esp_wifi_connect();
            return;
        }
    }

    esp_timer_stop(s_reconnect_timer);
    esp_timer_start_once(s_reconnect_timer, (uint64_t)delay_ms * 1000);
}

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
            wifi_retry_on_disconnect(&s_retry);

            if (wifi_retry_should_reconnect(&s_retry)) {
                const int delay_ms = wifi_retry_delay_ms(&s_retry);
                ESP_LOGW(TAG, "disconnected (fail %d%s), reconnecting in %dms",
                         s_retry.consecutive_fails,
                         wifi_retry_ever_connected(&s_retry) ? ", known-good creds"
                                                             : ", provisioning",
                         delay_ms);
                schedule_reconnect();
                s_state = WIFI_STATE_CONNECTING;
            } else {
                /* Only reachable during provisioning: a device that has held
                 * an IP never gives up. */
                ESP_LOGE(TAG, "giving up after %d attempts (never connected; "
                         "credentials are probably wrong)",
                         WIFI_PROVISION_MAX_RETRIES);
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
        wifi_retry_on_connected(&s_retry);
        s_state = WIFI_STATE_CONNECTED;
        ESP_LOGI(TAG, "connected, ip " IPSTR, IP2STR(&s_ip));
    }
}

void wifi_force_reconnect(void)
{
    ESP_LOGW(TAG, "forcing reconnect: repeated fetch failures suggest the "
             "link is down despite no disconnect event");

    /* Disconnect first. Calling connect() on a station that believes it is
     * already associated is a no-op, which is exactly the situation this
     * exists to break out of. The induced STA_DISCONNECTED then drives the
     * normal reassociation path through wifi_retry. */
    esp_wifi_disconnect();
    esp_wifi_connect();
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

    wifi_retry_init(&s_retry);
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

    wifi_retry_init(&s_retry);
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
