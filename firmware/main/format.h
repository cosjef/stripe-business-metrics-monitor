/*
 * Formatting money and counts for the display.
 *
 * Spec 6.1 abbreviates $6,512 to "$6.5k" because five glyphs at 60px fit the
 * 208px column while six do not, and precision below $100 is not
 * decision-relevant at a glance. That trade-off is implemented here.
 *
 * No ESP-IDF dependencies, so this is tested on the host.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Longest output is something like "-$1.23M" plus a terminator. */
#define FORMAT_MONEY_LEN 16

/*
 * Format cents as a compact currency string for the hero value.
 *
 *      0        -> "$0"
 *    999_00     -> "$999"
 *   6512_00     -> "$6.5k"
 *  99999_00     -> "$100k"
 * 1450000_00    -> "$1.45M"
 *
 * Deliberately drops precision as the number grows: the spec's legibility
 * budget allows about five glyphs at hero size (2.3), and nobody reads a
 * seven-figure MRR to the dollar across a room.
 */
void format_money_compact(int64_t cents, char *out, size_t out_len);

/*
 * Format a signed delta, always with an explicit sign: "+$118", "-$40".
 * A delta of zero renders as "$0" with no sign, since "+$0" reads oddly.
 */
void format_money_delta(int64_t cents, char *out, size_t out_len);

/* Format a plain count: "94", "1,204". */
void format_count(int64_t n, char *out, size_t out_len);
