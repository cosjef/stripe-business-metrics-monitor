/*
 * Setup-mode provisioning logic. See provision.h.
 *
 * No ESP-IDF dependencies, so this builds and tests on the host.
 */
#include "provision.h"

#include <stdio.h>
#include <string.h>

wifi_cred_result_t wifi_creds_validate(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0') {
        return WIFI_CRED_SSID_EMPTY;
    }

    if (strlen(ssid) > WIFI_SSID_MAX_LEN) {
        return WIFI_CRED_SSID_TOO_LONG;
    }

    /* An absent passphrase means an open network, which is legitimate. */
    if (pass == NULL || pass[0] == '\0') {
        return WIFI_CRED_OK;
    }

    const size_t len = strlen(pass);

    if (len < WIFI_PASS_MIN_LEN) {
        return WIFI_CRED_PASS_TOO_SHORT;
    }

    if (len > WIFI_PASS_MAX_LEN) {
        return WIFI_CRED_PASS_TOO_LONG;
    }

    /* WPA2 passphrases are printable ASCII. A high byte usually means a smart
     * quote from a phone keyboard or a paste from a document -- worth naming
     * explicitly rather than letting the join fail later with no explanation. */
    for (const unsigned char *p = (const unsigned char *)pass; *p; p++) {
        if (*p < 0x20 || *p > 0x7E) {
            return WIFI_CRED_PASS_NOT_ASCII;
        }
    }

    return WIFI_CRED_OK;
}

const char *wifi_cred_result_str(wifi_cred_result_t r)
{
    switch (r) {
    case WIFI_CRED_OK:               return "OK";
    case WIFI_CRED_SSID_EMPTY:       return "Network name is required";
    case WIFI_CRED_SSID_TOO_LONG:    return "Network name is too long";
    case WIFI_CRED_PASS_TOO_SHORT:   return "Password must be at least 8 characters";
    case WIFI_CRED_PASS_TOO_LONG:    return "Password must be at most 63 characters";
    case WIFI_CRED_PASS_NOT_ASCII:   return "Password contains unsupported characters";
    }
    return "Unknown error";
}

void setup_ssid_from_mac(const uint8_t mac[6], char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    /* Uppercase hex, zero-padded: the name on the screen has to match the
     * name in the phone's WiFi list character for character. */
    snprintf(out, out_len, "%s%02X%02X", SETUP_SSID_PREFIX, mac[4], mac[5]);
}
