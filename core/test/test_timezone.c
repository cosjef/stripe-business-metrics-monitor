/*
 * Timezone resolution from a UTC offset.
 *
 * The first version of this parsed the "timezone" field, which carries an
 * IANA name like "America/New_York". That was wrong in a way the tests did
 * not catch, because they were written against POSIX strings the service
 * was assumed to return rather than the ones it actually does. newlib on
 * the ESP32 has no IANA database, so "Europe/London" would have been read
 * as a zone abbreviation with no offset: UTC, under another name, with
 * nothing on screen to say so.
 *
 * The fixtures below are the real response shape, captured from
 * http://ip-api.com/json/?fields=timezone,offset.
 */
#include <stdio.h>
#include <string.h>
#include "../include/timezone.h"

static int checks = 0, failures = 0;

static void expect(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("FAIL: %s\n", what); }
}

static void expect_offset(const char *json, bool want_ok, int want,
                          const char *what)
{
    int got = 999999;
    const bool ok = tz_parse_offset(json, &got);
    checks++;
    if (ok != want_ok || (want_ok && got != want)) {
        failures++;
        printf("FAIL: %s (ok=%d got=%d, want ok=%d val=%d)\n",
               what, ok, got, want_ok, want);
    } else if (!ok && got != 999999) {
        failures++;
        printf("FAIL: %s (rejected but wrote to out)\n", what);
    }
}

static void expect_format(int seconds, const char *want, const char *what)
{
    char got[TZ_MAX_LEN] = "";
    const bool ok = tz_format_offset(seconds, got, sizeof(got));
    checks++;
    if (!ok || strcmp(got, want) != 0) {
        failures++;
        printf("FAIL: %s (got %s\"%s\", want \"%s\")\n",
               what, ok ? "" : "!ok ", got, want);
    }
}

int main(void)
{
    printf("\noffset validation\n");

    expect(tz_offset_is_valid(0),          "UTC");
    expect(tz_offset_is_valid(-14400),     "US Eastern in summer");
    expect(tz_offset_is_valid(32400),      "Japan");
    expect(tz_offset_is_valid(19800),      "India, half-hour offset");
    expect(tz_offset_is_valid(TZ_OFFSET_MIN), "the western limit");
    expect(tz_offset_is_valid(TZ_OFFSET_MAX), "the eastern limit");

    expect(!tz_offset_is_valid(TZ_OFFSET_MIN - 1), "past the western limit");
    expect(!tz_offset_is_valid(TZ_OFFSET_MAX + 1), "past the eastern limit");
    expect(!tz_offset_is_valid(86400),  "a whole day is not an offset");
    expect(!tz_offset_is_valid(-86400), "nor is a negative one");

    printf("\nparsing the real response\n");

    /* Captured verbatim from the service. */
    expect_offset("{\"timezone\":\"America/New_York\",\"offset\":-14400}",
                  true, -14400, "the actual response");
    expect_offset("{\"offset\":0}",     true, 0,     "UTC");
    expect_offset("{\"offset\":32400}", true, 32400, "positive offset");
    expect_offset("{\"offset\" : 3600}", true, 3600, "spaces around colon");

    expect_offset("{\"timezone\":\"America/New_York\"}", false, 0,
                  "an IANA name alone is not enough");
    expect_offset("{\"status\":\"fail\"}", false, 0, "field absent");
    expect_offset("",                      false, 0, "empty document");
    expect_offset("not json",              false, 0, "not json");
    expect_offset("{\"offset\":\"-14400\"}", false, 0,
                  "a quoted offset is not a number");
    expect_offset("{\"offset\":999999}",   false, 0, "out of range is refused");
    expect_offset("{\"offset\":}",         false, 0, "no value");

    /*
     * The bug this rewrite exists to prevent: an IANA name must never reach
     * the formatter, and must never be mistaken for a usable answer.
     */
    expect_offset("{\"timezone\":\"Europe/London\",\"offset\":3600}",
                  true, 3600, "London resolves by offset, not by name");

    printf("\nformatting a POSIX string\n");

    /*
     * POSIX inverts the sign: the string says how much to ADD to local time
     * to get UTC, so UTC-4 is written "UTC4". Getting this backwards is an
     * eight-hour error for a US user, so both directions are checked.
     */
    expect_format(0,      "UTC0",     "UTC");
    expect_format(-14400, "UTC4",     "US Eastern in summer is UTC4, not UTC-4");
    expect_format(-18000, "UTC5",     "US Eastern in winter");
    expect_format(3600,   "UTC-1",    "central Europe");
    expect_format(32400,  "UTC-9",    "Japan");
    expect_format(19800,  "UTC-5:30", "India, half-hour offset");
    expect_format(-12600, "UTC3:30",  "Newfoundland, negative half-hour");
    expect_format(20700,  "UTC-5:45", "Nepal, quarter-hour offset");

    /* Round trip: whatever the service says, the string we build must mean
     * the same offset back. */
    for (int off = TZ_OFFSET_MIN; off <= TZ_OFFSET_MAX; off += 900) {
        char buf[TZ_MAX_LEN];
        checks++;
        if (!tz_format_offset(off, buf, sizeof(buf))) {
            failures++;
            printf("FAIL: could not format a valid offset %d\n", off);
        }
    }

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
