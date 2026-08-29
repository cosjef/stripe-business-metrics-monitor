/*
 * Display formatting. See format.h.
 */
#include "format.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void format_money_compact(int64_t cents, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    const char *sign = cents < 0 ? "-" : "";
    const int64_t abs_cents = cents < 0 ? -cents : cents;
    const int64_t dollars = abs_cents / 100;

    if (dollars < 1000) {
        /* Exact dollars. Cents are dropped: sub-dollar precision is not
         * decision-relevant at a glance and costs two glyphs. */
        snprintf(out, out_len, "%s$%" PRId64, sign, dollars);
        return;
    }

    if (dollars < 1000000) {
        const int64_t k_whole = dollars / 1000;
        const int64_t k_tenth = (dollars % 1000) / 100;

        /*
         * Past 10k the decimal costs a hero size step, so it is dropped.
         *
         * "$11.2k" is six glyphs needing 268px at 96px, well past the 208px
         * column, so the sizer falls to 64px while every neighbouring screen
         * sits at 96px -- the ARR screen looked visibly small on the device for
         * exactly this reason. "$11k" fits at 96px and matches the deck.
         *
         * The precision is not really lost: figures this size are projections
         * (ARR is MRR x 12), and the exact value is on the MRR screen. Spec 2.2
         * already called for this tradeoff; the threshold was just set too high
         * for this column width.
         */
        if (k_whole >= 10) {
            snprintf(out, out_len, "%s$%" PRId64 "k", sign, k_whole);
        } else {
            snprintf(out, out_len, "%s$%" PRId64 ".%" PRId64 "k",
                     sign, k_whole, k_tenth);
        }
        return;
    }

    const int64_t m_whole = dollars / 1000000;
    const int64_t m_frac = (dollars % 1000000) / 10000;  /* two decimals */

    if (m_whole >= 10) {
        /* Two significant decimals would overflow the glyph budget here. */
        snprintf(out, out_len, "%s$%" PRId64 ".%" PRId64 "M",
                 sign, m_whole, m_frac / 10);
    } else {
        snprintf(out, out_len, "%s$%" PRId64 ".%02" PRId64 "M",
                 sign, m_whole, m_frac);
    }
}

void format_money_delta(int64_t cents, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (cents == 0) {
        /* "+$0" reads as a statement about nothing; plain "$0" is honest. */
        snprintf(out, out_len, "$0");
        return;
    }

    if (cents > 0) {
        char body[FORMAT_MONEY_LEN];
        format_money_compact(cents, body, sizeof(body));
        snprintf(out, out_len, "+%s", body);
        return;
    }

    /* format_money_compact already emits the minus sign. */
    format_money_compact(cents, out, out_len);
}

void format_count(int64_t n, char *out, size_t out_len)
{
    if (out == NULL || out_len == 0) {
        return;
    }

    if (n > -1000 && n < 1000) {
        snprintf(out, out_len, "%" PRId64, n);
        return;
    }

    /* Group thousands with a comma. Spec 5.4 rules out a middot as a
     * separator -- at 22px it is about 1mm of ink and disappears. */
    const char *sign = n < 0 ? "-" : "";
    const int64_t a = n < 0 ? -n : n;
    snprintf(out, out_len, "%s%" PRId64 ",%03" PRId64, sign, a / 1000, a % 1000);
}
