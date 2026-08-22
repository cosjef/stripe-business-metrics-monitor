/*
 * Failure tags for the stale footer. See failtag.h for why this is a footer
 * rather than a screen.
 */
#include "failtag.h"

#include <stdio.h>

const char *failtag_for(failtag_t t)
{
    switch (t) {
    case FAILTAG_NETWORK: return "network";
    case FAILTAG_TLS:     return "tls";
    case FAILTAG_RATE:    return "rate limit";
    case FAILTAG_SERVER:  return "stripe down";
    case FAILTAG_BAD:     return "bad reply";
    case FAILTAG_NONE:    break;
    }
    return "";
}

failtag_t failtag_from_result(int stripe_result)
{
    /* Mirrors stripe_result_t in stripe_api.h. Kept as literals so this file
     * does not pull in ESP-IDF headers; test_failtag pins the correspondence. */
    switch (stripe_result) {
    case 1: return FAILTAG_NETWORK;       /* STRIPE_ERR_NETWORK */
    case 2: return FAILTAG_TLS;           /* STRIPE_ERR_TLS */
    case 4: return FAILTAG_RATE;          /* STRIPE_ERR_RATE_LIMITED */
    case 5: return FAILTAG_SERVER;        /* STRIPE_ERR_SERVER */
    case 6: return FAILTAG_BAD;           /* STRIPE_ERR_BAD_RESPONSE */
    default: break;
    }

    /* STRIPE_OK, STRIPE_ERR_UNAUTHORIZED (own screen), STRIPE_ERR_NO_MEMORY
     * (nothing the reader can act on) all fall through untagged. */
    return FAILTAG_NONE;
}

void failtag_build_footer(char *out, size_t out_len, int retry_secs,
                          failtag_t tag)
{
    const char *t = failtag_for(tag);

    /* A zero or negative delay means a retry is in flight right now rather
     * than scheduled, which is worth saying differently: "retry in 0s" would
     * read as broken. */
    if (retry_secs > 0) {
        if (t[0] != '\0') {
            snprintf(out, out_len, "retry in %ds / %s", retry_secs, t);
        } else {
            snprintf(out, out_len, "retry in %ds", retry_secs);
        }
        return;
    }

    if (t[0] != '\0') {
        snprintf(out, out_len, "retrying / %s", t);
    } else {
        snprintf(out, out_len, "retrying");
    }
}
