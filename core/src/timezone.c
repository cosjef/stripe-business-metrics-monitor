/*
 * Timezone resolution. See timezone.h for why the offset is used rather
 * than the IANA name the lookup also returns.
 */
#include "timezone.h"

#include <stdio.h>
#include <string.h>

bool tz_offset_is_valid(int seconds)
{
    return seconds >= TZ_OFFSET_MIN && seconds <= TZ_OFFSET_MAX;
}

bool tz_parse_offset(const char *json, int *out)
{
    if (json == NULL || out == NULL) {
        return false;
    }

    const char *key = strstr(json, "\"offset\"");
    if (key == NULL) {
        return false;
    }

    const char *p = key + strlen("\"offset\"");
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != ':') {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }

    /* A bare number. A quoted one is a different response shape than the
     * one this was written against, so it is refused rather than guessed
     * at. */
    bool negative = false;
    if (*p == '-') {
        negative = true;
        p++;
    }
    if (*p < '0' || *p > '9') {
        return false;
    }

    long value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        if (value > 100000) {          /* far past any real offset */
            return false;
        }
        p++;
    }
    if (negative) {
        value = -value;
    }

    if (!tz_offset_is_valid((int)value)) {
        return false;
    }

    *out = (int)value;
    return true;
}

bool tz_format_offset(int seconds, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0 || !tz_offset_is_valid(seconds)) {
        return false;
    }

    /*
     * POSIX states the offset as the value ADDED to local time to reach
     * UTC, which is the opposite sign from the everyday convention. New
     * York in summer is UTC-4 in conversation and "UTC4" in a TZ string.
     */
    const int posix = -seconds;

    const char *sign = posix < 0 ? "-" : "";
    const int abs_secs = posix < 0 ? -posix : posix;
    const int hours = abs_secs / 3600;
    const int minutes = (abs_secs % 3600) / 60;

    int n;
    if (minutes == 0) {
        n = snprintf(out, out_len, "UTC%s%d", sign, hours);
    } else {
        n = snprintf(out, out_len, "UTC%s%d:%02d", sign, hours, minutes);
    }

    return n > 0 && (size_t)n < out_len;
}
