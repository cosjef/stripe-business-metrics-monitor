/*
 * Stripe API result codes.
 *
 * Split out of stripe_api.h so files that only need to classify a result do
 * not have to pull in esp_err.h and the rest of ESP-IDF. That matters for
 * failtag.c, which is host-tested.
 *
 * Before this split, failtag.c mapped these by literal value (case 1, case 2)
 * with the names only in comments -- magic numbers. Inserting a value here
 * would have silently shifted every mapping below it, and the test could not
 * have caught it because the test hardcoded the same literals. Now both sides
 * use the symbols and the compiler keeps them honest.
 */
#pragma once

typedef enum {
    STRIPE_OK = 0,
    STRIPE_ERR_NETWORK,      /* could not reach the API at all */
    STRIPE_ERR_TLS,          /* handshake or certificate failure */
    STRIPE_ERR_UNAUTHORIZED, /* 401: key revoked, wrong scope, or account gone */
    STRIPE_ERR_RATE_LIMITED, /* 429 */
    STRIPE_ERR_SERVER,       /* 5xx */
    STRIPE_ERR_BAD_RESPONSE, /* 2xx but unparseable */
    STRIPE_ERR_NO_MEMORY,
} stripe_result_t;
