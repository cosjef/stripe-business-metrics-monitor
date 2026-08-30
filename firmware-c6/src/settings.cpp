/*
 * Settings on Arduino Preferences.
 *
 * Preferences is a thin wrapper over the same NVS the ESP-IDF build used, so
 * the namespace and key names below are copied exactly from
 * firmware/main/settings.c. A board provisioned under one build reads its
 * credentials correctly under the other; changing these strings would
 * silently orphan a device's stored key and send the owner back through
 * setup with no explanation.
 */
#include "settings.h"

#include <Arduino.h>
#include <Preferences.h>

#define NVS_NAMESPACE "stripedev"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define KEY_STRIPE    "stripe_key"
#define KEY_HISTORY   "mrr_history"
#define KEY_CACHE     "last_good"   /* matches the ESP-IDF build */

/*
 * Every read guards with isKey() before getString().
 *
 * A missing key is the normal first-boot state, but Preferences logs it at
 * ERROR level ("nvs_get_str len fail: wifi_ssid NOT_FOUND"). Left alone, an
 * unprovisioned device prints errors that are not errors -- and a real NVS
 * fault would then be indistinguishable from routine noise.
 */

static Preferences s_prefs;

/*
 * Preferences handles are opened per operation rather than held.
 *
 * A long-lived read-write handle keeps an NVS page open across the portal's
 * lifetime, and the portal is exactly when a power cut is most likely -- the
 * owner is handling the device. Open, write, close keeps the window small.
 */
static bool open_ro(void)
{
    return s_prefs.begin(NVS_NAMESPACE, /* readOnly */ true);
}

static bool open_rw(void)
{
    return s_prefs.begin(NVS_NAMESPACE, /* readOnly */ false);
}

bool settings_init(void)
{
    /* Preferences initialises NVS on first begin(). Prove it works now rather
     * than discovering a corrupt partition at the moment we try to save the
     * customer's key. */
    if (!open_rw()) {
        Serial.println("settings: NVS unavailable");
        return false;
    }
    s_prefs.end();
    return true;
}

bool settings_have_wifi(void)
{
    if (!open_ro()) {
        return false;
    }
    const bool have = s_prefs.isKey(KEY_WIFI_SSID) &&
                      s_prefs.getString(KEY_WIFI_SSID, "").length() > 0;
    s_prefs.end();
    return have;
}

bool settings_get_wifi(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    if (ssid == NULL || pass == NULL || !open_ro()) {
        return false;
    }

    if (!s_prefs.isKey(KEY_WIFI_SSID)) {
        s_prefs.end();
        return false;
    }
    const String s = s_prefs.getString(KEY_WIFI_SSID, "");
    const String p = s_prefs.isKey(KEY_WIFI_PASS)
                         ? s_prefs.getString(KEY_WIFI_PASS, "") : String("");
    s_prefs.end();

    if (s.length() == 0) {
        return false;
    }

    snprintf(ssid, ssid_len, "%s", s.c_str());
    snprintf(pass, pass_len, "%s", p.c_str());
    return true;
}

bool settings_set_wifi(const char *ssid, const char *pass)
{
    if (ssid == NULL || ssid[0] == '\0' || !open_rw()) {
        return false;
    }

    const bool ssid_ok = s_prefs.putString(KEY_WIFI_SSID, ssid) > 0;

    /* An open network has an empty password. putString returns the number of
     * bytes written, so an empty value returns 0 -- which is success here,
     * not failure, and must not be treated as an error. */
    s_prefs.putString(KEY_WIFI_PASS, pass ? pass : "");

    s_prefs.end();
    return ssid_ok;
}

bool settings_clear_wifi(void)
{
    if (!open_rw()) {
        return false;
    }
    s_prefs.remove(KEY_WIFI_SSID);
    s_prefs.remove(KEY_WIFI_PASS);
    s_prefs.end();
    return true;
}

bool settings_have_stripe_key(void)
{
    if (!open_ro()) {
        return false;
    }
    const bool have = s_prefs.isKey(KEY_STRIPE) &&
                      s_prefs.getString(KEY_STRIPE, "").length() > 0;
    s_prefs.end();
    return have;
}

bool settings_get_stripe_key(char *key, size_t key_len)
{
    if (key == NULL || !open_ro()) {
        return false;
    }

    if (!s_prefs.isKey(KEY_STRIPE)) {
        s_prefs.end();
        return false;
    }
    const String k = s_prefs.getString(KEY_STRIPE, "");
    s_prefs.end();

    if (k.length() == 0) {
        return false;
    }
    snprintf(key, key_len, "%s", k.c_str());
    return true;
}

bool settings_set_stripe_key(const char *key)
{
    if (key == NULL || key[0] == '\0' || !open_rw()) {
        return false;
    }
    const bool ok = s_prefs.putString(KEY_STRIPE, key) > 0;
    s_prefs.end();
    return ok;
}

bool settings_save_history(const void *blob, size_t len)
{
    if (blob == NULL || len == 0 || !open_rw()) {
        return false;
    }
    const bool ok = s_prefs.putBytes(KEY_HISTORY, blob, len) == len;
    s_prefs.end();
    return ok;
}

bool settings_load_history(void *out, size_t len)
{
    if (out == NULL || len == 0 || !open_ro()) {
        return false;
    }
    if (!s_prefs.isKey(KEY_HISTORY)) {
        s_prefs.end();
        return false;
    }
    /*
     * Size must match exactly. A short read would leave the tail of the
     * struct uninitialised -- including count and head, which index the ring
     * -- and a history_t with a garbage head reads samples from nowhere. If
     * the layout ever changes, treat the stored blob as absent rather than
     * reinterpreting it.
     */
    const size_t got = s_prefs.getBytes(KEY_HISTORY, out, len);
    s_prefs.end();
    return got == len;
}

bool settings_save_cache(const void *blob, size_t len)
{
    if (blob == NULL || len == 0 || !open_rw()) {
        return false;
    }
    const bool ok = s_prefs.putBytes(KEY_CACHE, blob, len) == len;
    s_prefs.end();
    return ok;
}

bool settings_load_cache(void *out, size_t len)
{
    if (out == NULL || len == 0 || !open_ro()) {
        return false;
    }
    if (!s_prefs.isKey(KEY_CACHE)) {
        s_prefs.end();
        return false;
    }
    /* Exact size only. A short read would leave part of the struct
     * uninitialised, and cache_is_valid cannot catch a field that was never
     * written -- it would be validating garbage that happens to look sane. */
    const size_t got = s_prefs.getBytes(KEY_CACHE, out, len);
    s_prefs.end();
    return got == len;
}

bool settings_clear_all(void)
{
    if (!open_rw()) {
        return false;
    }
    const bool ok = s_prefs.clear();
    s_prefs.end();
    return ok;
}
