/*
 * Refresh scheduling.
 *
 * The case that matters is the one that shipped broken: a device with no
 * data must still retry. The old inline condition gated the retry on
 * having data, so a unit whose first fetch failed and had no cache would
 * sit on an empty deck forever.
 */
#include <stdio.h>
#include "../include/refresh.h"

static int checks = 0, failures = 0;

static void expect(bool got, bool want, const char *what)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL: %s (got %s, want %s)\n", what,
               got ? "true" : "false", want ? "true" : "false");
    }
}

int main(void)
{
    printf("\nrefresh scheduling\n");

    /* With data: the normal five-minute cycle. */
    expect(refresh_due(0, 0, true), false, "no fetch the instant one just ran");
    expect(refresh_due(REFRESH_INTERVAL_MS - 1, 0, true), false,
           "no fetch a millisecond early");
    expect(refresh_due(REFRESH_INTERVAL_MS, 0, true), true,
           "fetch exactly on the interval");
    expect(refresh_due(REFRESH_INTERVAL_MS * 3, 0, true), true,
           "fetch when long overdue");

    /*
     * Without data: the regression. A device that has never had a
     * successful fetch must retry, and sooner than the normal cycle.
     */
    expect(refresh_due(REFRESH_RETRY_MS, 0, false), true,
           "a device with no data retries");
    expect(refresh_due(REFRESH_RETRY_MS - 1, 0, false), false,
           "but not faster than the retry cadence");
    expect(refresh_due(REFRESH_INTERVAL_MS, 0, false), true,
           "and is still due at the normal interval");

    /* The retry must be sooner than the full cycle, or it is not a retry. */
    checks++;
    if (REFRESH_RETRY_MS >= REFRESH_INTERVAL_MS) {
        failures++;
        printf("FAIL: retry cadence is not shorter than the poll interval\n");
    }

    /*
     * millis() wraps every ~49 days. Done in signed or wider arithmetic the
     * comparison goes wrong at the boundary and stalls the deck for weeks.
     */
    const uint32_t near_wrap = 0xFFFFFFFFu - 1000;
    expect(refresh_due(near_wrap + REFRESH_INTERVAL_MS, near_wrap, true), true,
           "fetch is due across the millis() wrap");
    expect(refresh_due(near_wrap + 10, near_wrap, true), false,
           "and not due early across the wrap");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
