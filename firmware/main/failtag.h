/*
 * Short failure tags for the stale screen's footer.
 *
 * The stale screen says the data is old and that the device is still retrying.
 * It never said WHY, so every non-auth failure -- network down, TLS failure,
 * Stripe 5xx, rate limiting -- looked the same to the reader.
 *
 * This deliberately does NOT add a screen. For nearly every cause the reader's
 * action is identical (wait), and a screen drawing distinctions you cannot act
 * on dilutes a deck whose whole discipline is that each screen earns its five
 * seconds. Compare the two states that do get their own screens: auth error
 * means "re-issue your key", battery means "plug it in". Both demand something.
 *
 * The one genuinely useful distinction is whether the fault is local or
 * upstream. "stripe down" is positive evidence -- the device reached Stripe and
 * Stripe answered badly -- which tells the reader their own network is fine and
 * there is nothing to fix at their end.
 *
 * So the tag rides in the footer beside the retry countdown, where the eye
 * already goes for status. No extra screen, no rotation time.
 *
 * Pure string handling, no ESP-IDF, host-tested.
 */
#pragma once

#include <stddef.h>

#include "stripe_result.h"

/*
 * Longest footer is "retry in 900s / stripe down" at 27 characters. 40 gives
 * room without inviting a tag long enough to overflow the column -- which
 * test_failtag measures rather than assumes.
 */
#define FAILTAG_FOOTER_LEN 40

typedef enum {
    FAILTAG_NONE = 0,  /* nothing to report; footer omits the tag entirely */
    FAILTAG_NETWORK,   /* could not reach Stripe -- probably local */
    FAILTAG_TLS,       /* handshake or certificate failure */
    FAILTAG_RATE,      /* 429 */
    FAILTAG_SERVER,    /* 5xx -- reached Stripe, Stripe is unwell */
    FAILTAG_BAD,       /* 2xx but unparseable */
} failtag_t;

/* The tag text. Empty string for FAILTAG_NONE. Never NULL. */
const char *failtag_for(failtag_t t);

/*
 * Map a stripe_result_t to a tag.
 *
 * Auth failures map to FAILTAG_NONE: they have their own screen and never
 * reach the stale footer.
 */
failtag_t failtag_from_result(stripe_result_t r);

/*
 * Build the footer: the retry countdown, plus the tag when there is one.
 *
 *   retry in 60s / network
 *   retry in 240s / stripe down
 *   retry in 60s            (no known cause)
 *   retrying / network      (a retry is in flight)
 */
void failtag_build_footer(char *out, size_t out_len, int retry_secs,
                          failtag_t tag);
