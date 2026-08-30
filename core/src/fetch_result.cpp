/*
 * Result-to-message mapping for the Stripe fetch.
 *
 * Split out of stripe_fetch.cpp, which pulls in Arduino networking and so
 * cannot be built on the host. This part is pure logic and is worth testing
 * directly -- see core/test/test_fetch_result.cpp for why the distinction
 * between "tls failed" and "no reply" matters.
 */
#include "stripe_fetch.h"

const char *stripe_fetch_strerror(stripe_fetch_result_t r)
{
    switch (r) {
    case STRIPE_FETCH_OK:           return "ok";
    case STRIPE_FETCH_NO_KEY:       return "no key";
    case STRIPE_FETCH_NO_NETWORK:   return "no network";
    case STRIPE_FETCH_TLS_FAILED:   return "tls failed";
    case STRIPE_FETCH_NO_RESPONSE:  return "no reply";
    case STRIPE_FETCH_UNAUTHORIZED: return "unauthorized";
    case STRIPE_FETCH_HTTP_ERROR:   return "http error";
    case STRIPE_FETCH_BAD_RESPONSE: return "bad response";
    }
    return "unknown";
}
