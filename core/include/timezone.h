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
 * defaulting to UTC and silently shifting everyone's day boundary. Unlike a
 * resolved offset this one carries real DST rules, because it is written by
 * hand rather than derived from a single instant. */
#define TZ_FALLBACK "EST5EDT,M3.2.0/2,M11.1.0/2"

/*
 * Bounds on the UTC offset, in seconds.
 *
 * Real zones run from UTC-12 to UTC+14. Anything outside that is a
 * malformed or hostile response, not a place.
 */
#define TZ_OFFSET_MIN (-12 * 3600)
#define TZ_OFFSET_MAX (14 * 3600)

/*
 * True if seconds is a plausible UTC offset.
 *
 * The value reaches strftime and the day-bucket arithmetic, so it is
 * checked before use rather than trusted.
 */
bool tz_offset_is_valid(int seconds);

/*
 * Extract "offset" (seconds east of UTC) from a flat JSON object.
 *
 * Returns false and leaves out untouched if the field is absent, not a
 * number, or outside the plausible range.
 *
 * The offset is used rather than the "timezone" field because that field
 * carries an IANA name such as "America/New_York", and newlib on the ESP32
 * ships no IANA database: handing it one yields a zero-offset zone, which
 * is UTC wearing another zone's name. A numeric offset cannot fail that
 * way.
 */
bool tz_parse_offset(const char *json, int *out);

/*
 * Render a POSIX TZ string for a fixed UTC offset into out.
 *
 * The result has no DST rules, because a single offset cannot express one.
 * That is why the offset is re-checked periodically rather than cached
 * forever: a zone that changes offset in spring is wrong for at most a day
 * instead of half a year.
 *
 * POSIX signs are inverted from the everyday convention. UTC-4 is written
 * "UTC4"; this function handles that, so callers pass the offset in the
 * usual sense, positive for east of Greenwich.
 */
bool tz_format_offset(int seconds, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif /* TIMEZONE_H */
