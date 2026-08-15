/*
 * Captive portal for setup mode (spec 9.1 step 1).
 *
 * Serves a single page with a WiFi credential form, and runs a DNS server that
 * answers every query with the device's own address so a phone's captive
 * portal detection opens the page without the customer typing an IP.
 */
#pragma once

#include "esp_err.h"

/*
 * Called when the customer submits valid credentials. The portal has already
 * validated them with wifi_creds_validate(). Strings are only valid for the
 * duration of the call.
 */
typedef void (*portal_creds_cb_t)(const char *ssid, const char *pass);

/* Start the HTTP server and DNS responder. Requires AP mode to be running. */
esp_err_t portal_start(portal_creds_cb_t on_creds);

/* Stop both, freeing their sockets and tasks. */
void portal_stop(void);
