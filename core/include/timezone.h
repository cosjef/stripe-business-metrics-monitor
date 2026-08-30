/*
 * Timezone resolution for the daily history buckets.
 *
 * The device buckets MRR samples by local calendar day, so it needs to know
 * where it is. Getting this wrong is quiet: the figures stay right, but the
 * day boundary lands at the wrong hour and a sample can be filed under the
 * wrong date. Nothing on screen says so.
 *
 * The timezone is looked up once from the device's public IP and cached, so
 * there is no setup step for it. This file holds the parts that are pure
 * logic: pulling the POSIX TZ string out of the response, and deciding
 * whether what came back is safe to hand to configTzTime().
 */
#ifndef TIMEZONE_H
#define TIMEZONE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest POSIX TZ string we will store, plus the NUL. Real ones run well
 * under this; the cap is what stops a hostile response overrunning NVS. */
#define TZ_MAX_LEN 64

/* Used when the lookup fails or returns something unusable. The device was
 * developed in US Eastern, so this keeps its behaviour unchanged rather than
 * defaulting to UTC and silently shifting everyone's day boundary. */
#define TZ_FALLBACK "EST5EDT,M3.2.0/2,M11.1.0/2"

/*
 * True if s is a POSIX TZ string this device will accept.
 *
 * This is a gate on untrusted input, not a full POSIX parser. The string
 * goes to configTzTime() and into NVS, so it must be bounded, printable,
 * and shaped like a timezone rather than arbitrary text.
 */
bool tz_is_valid(const char *s);

/*
 * Extract the value of "timezone" from a flat JSON object into out.
 *
 * Returns false and leaves out untouched if the field is absent, empty,
 * would not fit, or fails tz_is_valid(). The parser is deliberately small:
 * it handles the one response shape we ask for, and rejects anything else
 * rather than trying to be general.
 */
bool tz_parse_response(const char *json, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* TIMEZONE_H */
