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

failtag_t failtag_from_result(stripe_result_t r)
{
    /*
     * Switched on the enum without a default, so -Werror=switch flags any new
     * result code added to stripe_result.h rather than letting it fall through
     * to "no tag" unnoticed.
     */
    switch (r) {
    case STRIPE_ERR_NETWORK:      return FAILTAG_NETWORK;
    case STRIPE_ERR_TLS:          return FAILTAG_TLS;
    case STRIPE_ERR_RATE_LIMITED: return FAILTAG_RATE;
    case STRIPE_ERR_SERVER:       return FAILTAG_SERVER;
    case STRIPE_ERR_BAD_RESPONSE: return FAILTAG_BAD;

    /* Deliberately untagged: OK has nothing to report, UNAUTHORIZED has its
     * own screen, and NO_MEMORY is not something the reader can act on. */
    case STRIPE_OK:
    case STRIPE_ERR_UNAUTHORIZED:
    case STRIPE_ERR_NO_MEMORY:
        break;
    }

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
