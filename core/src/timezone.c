/*
 * Timezone resolution. See timezone.h for why this is looked up rather than
 * configured, and why the validation is strict.
 */
#include "timezone.h"

#include <string.h>

/*
 * Characters a POSIX TZ string is allowed to contain.
 *
 * Letters and digits for the zone abbreviations and offsets, and the small
 * punctuation set the format actually uses: sign, colon, comma, dot, slash,
 * and the angle brackets that wrap numeric abbreviations like <+0530>.
 * Everything else, including whitespace and control bytes, is rejected.
 */
static bool tz_char_ok(char c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
        return true;
    }
    if (c >= '0' && c <= '9') {
        return true;
    }
    return c == '+' || c == '-' || c == ':' || c == ',' ||
           c == '.' || c == '/' || c == '<' || c == '>';
}

bool tz_is_valid(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return false;
    }

    size_t n = 0;
    for (const char *p = s; *p != '\0'; p++) {
        if (!tz_char_ok(*p)) {
            return false;
        }
        n++;
        if (n >= TZ_MAX_LEN) {
            /* At the cap rather than past it: the value still needs a NUL. */
            return false;
        }
    }

    /*
     * A TZ string starts with a zone abbreviation, which is either letters
     * or a bracketed numeric form. Requiring that rejects a response that is
     * all punctuation while still accepting every real zone.
     */
    return (s[0] >= 'A' && s[0] <= 'Z') ||
           (s[0] >= 'a' && s[0] <= 'z') ||
           s[0] == '<';
}

bool tz_parse_response(const char *json, char *out, size_t out_len)
{
    if (json == NULL || out == NULL || out_len == 0) {
        return false;
    }

    const char *key = strstr(json, "\"timezone\"");
    if (key == NULL) {
        return false;
    }

    /* Step over the key, then the colon, tolerating spaces on either side. */
    const char *p = key + strlen("\"timezone\"");
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
    if (*p != '"') {
        return false;
    }
    p++;

    const char *end = strchr(p, '"');
    if (end == NULL) {
        return false;
    }

    const size_t len = (size_t)(end - p);
    if (len == 0 || len >= out_len || len >= TZ_MAX_LEN) {
        /*
         * Refuse rather than truncate. A truncated TZ string is not a
         * malformed one, it is a different and plausible timezone, which
         * would file history under the wrong day with no sign of trouble.
         */
        return false;
    }

    char tmp[TZ_MAX_LEN];
    memcpy(tmp, p, len);
    tmp[len] = '\0';

    if (!tz_is_valid(tmp)) {
        return false;
    }

    memcpy(out, tmp, len + 1);
    return true;
}
