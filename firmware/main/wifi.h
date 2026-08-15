/*
 * WiFi: setup AP and station mode.
 */
#pragma once

#include "esp_err.h"
#include "provision.h"

#include <stdbool.h>

typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_AP,          /* setup mode, portal reachable */
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED,      /* credentials stored but the join failed */
} wifi_state_t;

/* Bring up the WiFi stack. Does not start AP or station mode by itself. */
esp_err_t wifi_init(void);

/*
 * Start setup mode: an open AP named "Setup-XXXX" (spec 9.1 step 1).
 *
 * The AP is deliberately open. Asking a customer for a password before they
 * can reach the setup page is a step that earns nothing -- the only thing on
 * the AP is the portal itself.
 *
 * `out_ssid` receives the advertised name so the setup screen can show exactly
 * what the customer will see in their WiFi list; it must be at least
 * SETUP_SSID_LEN bytes.
 */
esp_err_t wifi_start_ap(char *out_ssid, size_t out_ssid_len);

/*
 * Join a network using stored credentials. Retries with backoff internally;
 * returns as soon as the attempt starts, not when it succeeds.
 */
esp_err_t wifi_start_sta(const char *ssid, const char *pass);

/*
 * Run AP and station modes at once.
 *
 * Needed for the key-entry phase: the customer's phone stays joined to the
 * setup AP looking at the portal, while the device simultaneously reaches
 * Stripe over their network to validate the key (spec 9.1 step 4).
 */
esp_err_t wifi_start_apsta(char *out_ap_ssid, size_t out_ap_ssid_len,
                           const char *sta_ssid, const char *sta_pass);

/* Current state, for the display to render. */
wifi_state_t wifi_get_state(void);

/* Dotted-quad IP once connected, or "0.0.0.0". Buffer should be >= 16 bytes. */
void wifi_get_ip(char *out, size_t out_len);
