/*
 * Setup-mode provisioning: validation and formatting of the values a customer
 * enters, plus the SSID the device advertises.
 *
 * Deliberately free of ESP-IDF, WiFi, and NVS dependencies so it can be tested
 * on the host. The parts that touch hardware live in wifi.c and nvs_store.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Field limits.
 *
 * SSID and passphrase limits come from 802.11: an SSID is at most 32 bytes,
 * and a WPA2 passphrase is 8-63 ASCII characters (or a 64-hex-digit PSK,
 * which this does not accept -- customers type passphrases).
 */
#define WIFI_SSID_MAX_LEN 32
#define WIFI_PASS_MIN_LEN 8
#define WIFI_PASS_MAX_LEN 63

/*
 * The setup AP's SSID: "Setup-XXXX" where XXXX is derived from the device MAC
 * (spec 9.1 step 1). Four hex digits plus the prefix and terminator.
 */
#define SETUP_SSID_PREFIX "Setup-"
#define SETUP_SSID_LEN    (sizeof(SETUP_SSID_PREFIX) + 4)

typedef enum {
    WIFI_CRED_OK = 0,
    WIFI_CRED_SSID_EMPTY,
    WIFI_CRED_SSID_TOO_LONG,
    WIFI_CRED_PASS_TOO_SHORT,
    WIFI_CRED_PASS_TOO_LONG,
    WIFI_CRED_PASS_NOT_ASCII,
} wifi_cred_result_t;

/*
 * Validate a WiFi SSID and passphrase as a customer would type them.
 *
 * An empty passphrase is accepted: open networks are legitimate, and rejecting
 * them would strand anyone on a captive-portal guest network. A passphrase
 * that is present must be 8-63 printable ASCII characters.
 */
wifi_cred_result_t wifi_creds_validate(const char *ssid, const char *pass);

/* Human-readable reason for a validation failure, for the portal to show. */
const char *wifi_cred_result_str(wifi_cred_result_t r);

/*
 * Build the setup SSID from the device MAC into `out`, which must be at least
 * SETUP_SSID_LEN bytes. Uses the low two bytes, uppercase hex, so the name on
 * the screen matches the name in the WiFi list exactly.
 */
void setup_ssid_from_mac(const uint8_t mac[6], char *out, size_t out_len);
