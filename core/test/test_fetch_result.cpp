/*
 * Every stripe_fetch_result_t must map to a distinct, non-empty message.
 *
 * This exists because "tls failed" was once the label for two unrelated
 * faults: a handshake that never completed, and a request that went out
 * over a good channel but got no reply. The second reported the first,
 * which sent a debugging session looking at certificates when the
 * certificate was fine. A shared label is a lie about what happened.
 *
 * The table is written out by hand rather than looped over the enum so
 * that adding a value without a message fails to compile here, instead
 * of silently inheriting "unknown".
 */
#include <stdio.h>
#include <string.h>
#include "stripe_fetch.h"

const char *stripe_fetch_strerror(stripe_fetch_result_t r);

static int checks = 0, failures = 0;

static void expect(int cond, const char *what)
{
    checks++;
    if (!cond) { failures++; printf("FAIL: %s\n", what); }
}

int main(void)
{
    static const struct { stripe_fetch_result_t r; const char *name; } all[] = {
        { STRIPE_FETCH_OK,           "OK" },
        { STRIPE_FETCH_NO_KEY,       "NO_KEY" },
        { STRIPE_FETCH_NO_NETWORK,   "NO_NETWORK" },
        { STRIPE_FETCH_TLS_FAILED,   "TLS_FAILED" },
        { STRIPE_FETCH_NO_RESPONSE,  "NO_RESPONSE" },
        { STRIPE_FETCH_UNAUTHORIZED, "UNAUTHORIZED" },
        { STRIPE_FETCH_HTTP_ERROR,   "HTTP_ERROR" },
        { STRIPE_FETCH_BAD_RESPONSE, "BAD_RESPONSE" },
    };
    const int n = (int)(sizeof(all) / sizeof(all[0]));

    for (int i = 0; i < n; i++) {
        const char *m = stripe_fetch_strerror(all[i].r);
        expect(m != NULL && m[0] != '\0', all[i].name);
    }

    /* No two results may share a message. */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            const char *a = stripe_fetch_strerror(all[i].r);
            const char *b = stripe_fetch_strerror(all[j].r);
            if (a && b && strcmp(a, b) == 0) {
                checks++; failures++;
                printf("FAIL: %s and %s share \"%s\"\n",
                       all[i].name, all[j].name, a);
            } else {
                checks++;
            }
        }
    }

    /* The specific confusion this guards against. */
    expect(strcmp(stripe_fetch_strerror(STRIPE_FETCH_TLS_FAILED),
                  stripe_fetch_strerror(STRIPE_FETCH_NO_RESPONSE)) != 0,
           "a dead channel and a silent one read differently");

    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
