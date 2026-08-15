/*
 * Captive portal for setup mode (spec 9.1 step 1).
 *
 * Serves a single page with a WiFi credential form, and runs a DNS server that
 * answers every query with the device's own address so a phone's captive
 * portal detection opens the page without the customer typing an IP.
 */
#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>

/*
 * Called when the customer submits valid credentials. The portal has already
 * validated them with wifi_creds_validate(). Strings are only valid for the
 * duration of the call.
 */
typedef void (*portal_creds_cb_t)(const char *ssid, const char *pass);

/*
 * Called after WiFi is up, to validate a Stripe key against the live API
 * (spec 9.1 step 4). Fills `out_msg` with a human-readable result either way;
 * on success that is the customer's own account state, which the spec calls
 * the moment that converts skeptics. Returns true if the key was accepted.
 */
typedef bool (*portal_key_cb_t)(const char *key, char *out_msg, size_t msg_len);

/* Start the HTTP server and DNS responder. Requires AP mode to be running. */
/*
 * `key_phase` selects what "/" serves.
 *
 * false: the WiFi credential form (first-run).
 * true:  the Stripe key form, because WiFi is already configured. Without
 *        this the customer lands on the WiFi form again, re-enters details
 *        that are already stored, and setup appears to stall.
 */
esp_err_t portal_start(portal_creds_cb_t on_creds, portal_key_cb_t on_key,
                       bool key_phase);

/* Stop both, freeing their sockets and tasks. */
void portal_stop(void);
