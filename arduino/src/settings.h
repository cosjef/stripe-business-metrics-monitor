/*
 * Persistent settings: WiFi credentials and the Stripe key.
 *
 * Backed by Arduino Preferences (the same NVS partition the ESP-IDF build
 * used, via a different API). The namespace and key names are kept identical
 * to firmware/main/settings.c so a device flashed either way reads the same
 * stored values -- which matters because the ESP-IDF build is still on `main`
 * and a board may be moved between them.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* Sizes match the ESP-IDF side. 64/64 covers the 802.11 maxima (32-byte SSID,
 * 63-byte PSK) with room for the terminator. */
#define SETTINGS_SSID_LEN 64
#define SETTINGS_PASS_LEN 64
#define SETTINGS_KEY_LEN  256

bool settings_init(void);

bool settings_have_wifi(void);
bool settings_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len);
bool settings_set_wifi(const char *ssid, const char *pass);
bool settings_clear_wifi(void);

bool settings_have_stripe_key(void);
bool settings_get_stripe_key(char *key, size_t key_len);
bool settings_set_stripe_key(const char *key);

/* Wipe everything: the factory reset behind a long press. */
bool settings_clear_all(void);
