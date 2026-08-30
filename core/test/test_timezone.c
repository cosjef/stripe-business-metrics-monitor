/*
 * Timezone parsing and validation.
 *
 * The input here comes from a third-party server over the network, so the
 * tests lean on what happens with bad input rather than good. A malformed
 * TZ string reaches configTzTime() and NVS, and an over-long one would
 * overrun the buffer it is copied into.
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

static void expect_parse(const char *json, const char *want, const char *what)
{
    char got[TZ_MAX_LEN] = "sentinel";
    const bool ok = tz_parse_response(json, got, sizeof(got));
    checks++;
    if (want == NULL) {
        if (ok) { failures++; printf("FAIL: %s (accepted, should reject)\n", what); }
        else if (strcmp(got, "sentinel") != 0) {
            failures++; printf("FAIL: %s (rejected but wrote to out)\n", what);
        }
    } else if (!ok || strcmp(got, want) != 0) {
        failures++;
        printf("FAIL: %s (got %s\"%s\", want \"%s\")\n",
               what, ok ? "" : "!ok ", got, want);
    }
}

int main(void)
{
    printf("\ntimezone parsing\n");

    /* Real POSIX strings, the shape the lookup returns. */
    expect(tz_is_valid("EST5EDT,M3.2.0/2,M11.1.0/2"), "US Eastern");
    expect(tz_is_valid("GMT0BST,M3.5.0/1,M10.5.0"),   "UK");
    expect(tz_is_valid("JST-9"),                       "Japan, no DST");
    expect(tz_is_valid("UTC0"),                        "UTC");
    expect(tz_is_valid("<+0530>-5:30"),                "India, bracketed numeric");

    /* Rejected input. */
    expect(!tz_is_valid(NULL), "NULL");
    expect(!tz_is_valid(""),   "empty");
    expect(!tz_is_valid(" "),  "whitespace only");

    /* Too long to store. Anything at or past the buffer must be refused
     * rather than truncated: a truncated TZ string is a different, valid
     * looking timezone. */
    char over[TZ_MAX_LEN + 32];
    memset(over, 'A', sizeof(over) - 1);
    over[sizeof(over) - 1] = '\0';
    expect(!tz_is_valid(over), "over-long string");

    /* Control and non-printable bytes reach configTzTime and NVS. */
    expect(!tz_is_valid("EST5EDT\n"),     "embedded newline");
    expect(!tz_is_valid("EST\0056EDT"),   "embedded control byte");
    expect(!tz_is_valid("EST5EDT;rm -rf"), "shell punctuation");

    printf("\ntimezone response parsing\n");

    expect_parse("{\"timezone\":\"JST-9\"}", "JST-9", "flat object");
    expect_parse("{\"status\":\"success\",\"timezone\":\"UTC0\"}", "UTC0",
                 "field after another");
    expect_parse("{\"timezone\" : \"UTC0\"}", "UTC0", "spaces around colon");

    expect_parse("{\"status\":\"fail\"}", NULL, "field absent");
    expect_parse("{\"timezone\":\"\"}",   NULL, "field empty");
    expect_parse("",                       NULL, "empty document");
    expect_parse("not json at all",        NULL, "not json");

    /* A hostile response must not overrun, and must not be truncated into
     * something that looks legitimate. */
    char big[512];
    snprintf(big, sizeof(big), "{\"timezone\":\"%.400s\"}",
             "EST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDT"
             "EST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDTEST5EDT");
    expect_parse(big, NULL, "over-long value is refused, not truncated");

    /* The fallback must itself be acceptable, or a failed lookup would
     * leave the device with a timezone it rejects. */
    expect(tz_is_valid(TZ_FALLBACK), "the fallback is a valid TZ string");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
