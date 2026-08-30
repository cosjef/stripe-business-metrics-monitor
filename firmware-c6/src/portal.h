/*
 * Captive portal for setup (spec 9.1).
 *
 * Two phases, because the Stripe key is validated against the live API the
 * moment it is entered rather than stored untested: first WiFi, then -- once
 * the device is actually on the network -- the key.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Called when the owner submits WiFi credentials. */
typedef void (*portal_creds_cb_t)(const char *ssid, const char *pass);

/*
 * Called when the owner submits a Stripe key.
 *
 * Return true if the key works. On false, write a short reason into `out_msg`
 * -- it is shown to the owner, so it must be plain language, not an error
 * code.
 */
typedef bool (*portal_key_cb_t)(const char *key, char *out_msg, size_t msg_len);

/*
 * Start the AP and HTTP server. `key_phase` selects what "/" serves: the WiFi
 * form, or the Stripe key form.
 */
bool portal_start(portal_creds_cb_t on_creds, portal_key_cb_t on_key,
                  const char *ap_ssid, bool key_phase);

/*
 * Stop serving but leave the AP advertising. Used between the wifi and key
 * phases so the owner's phone stays associated through the whole of setup.
 */
void portal_pause(void);

/*
 * Switch which page "/" serves, leaving the AP and HTTP server running.
 * Used to move from the wifi form to the key form without a restart that
 * would drop the owner's phone.
 */
void portal_set_phase(bool key_phase);

/* Stop serving and take the AP down. Used when setup is finished. */
void portal_stop(void);

/* Pump the DNS and HTTP servers. Call from loop(). */
void portal_tick(void);

/* The SSID the portal is advertising, for the setup screen. */
const char *portal_ssid(void);
