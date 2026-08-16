/*
 * Persistent settings in NVS.
 *
 * SECURITY NOTE. NVS is unencrypted flash by default, and that is a deliberate
 * choice here rather than an oversight. Per spec 9.1 the device holds a
 * READ-ONLY Stripe restricted key, so the worst case from a stolen device is
 * that someone learns the owner's subscriber count -- not that they can move
 * money. ESP-IDF's NVS encryption requires flash encryption with an
 * eFuse-burned key, which is irreversible on the chip and complicates
 * development flashing. Revisit before selling hardware; see the note in
 * firmware-build-plan.md.
 */
#pragma once

#include "esp_err.h"
#include "provision.h"
#include "cache.h"
#include "stripe_key.h"

#include <stdbool.h>
#include <stddef.h>

/* Initialize NVS. Safe to call once at boot; handles a corrupt partition by
 * erasing and re-initializing rather than failing to start. */
esp_err_t settings_init(void);

/*
 * True if WiFi credentials have been stored, i.e. the device has been through
 * setup at least once. Determines whether we boot into setup mode or try to
 * join a network.
 */
bool settings_have_wifi(void);

/*
 * Read stored WiFi credentials. Buffers must be at least
 * WIFI_SSID_MAX_LEN + 1 and WIFI_PASS_MAX_LEN + 1 bytes.
 * Returns ESP_ERR_NVS_NOT_FOUND if setup has not been completed.
 */
esp_err_t settings_get_wifi(char *ssid, size_t ssid_len,
                            char *pass, size_t pass_len);

/*
 * Store WiFi credentials, replacing any existing pair. The caller is expected
 * to have validated them with wifi_creds_validate() first.
 */
esp_err_t settings_set_wifi(const char *ssid, const char *pass);

/*
 * Erase stored credentials, returning the device to setup mode. Used when the
 * customer asks to reconfigure, and by State B (spec 6.2) when Stripe access
 * is revoked.
 */
esp_err_t settings_clear_wifi(void);

/* True if a validated Stripe key has been stored. */
bool settings_have_stripe_key(void);

/* Read the stored Stripe key. Buffer should be STRIPE_KEY_MAX_LEN + 1. */
esp_err_t settings_get_stripe_key(char *key, size_t key_len);

/* Store a Stripe key. Should only be called after live validation. */
esp_err_t settings_set_stripe_key(const char *key);

/*
 * Last-good values, so the screen is never blank on boot (spec 7.4 step 1).
 *
 * Stored as a blob rather than individual keys: one atomic write, and a
 * partial update cannot leave the figures inconsistent with each other.
 */
esp_err_t settings_save_cache(const cache_t *c);

/* Returns ESP_ERR_NVS_NOT_FOUND when nothing has been cached yet. */
esp_err_t settings_load_cache(cache_t *out);
