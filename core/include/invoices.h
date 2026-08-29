/*
 * Streaming scanner for Stripe's invoice list.
 *
 * Separate from jsonstream.c deliberately. That scanner walks subscriptions
 * and knows about items, prices, intervals and subscription statuses;
 * invoices share none of that shape, and teaching one scanner both would mean
 * every field lookup first asking which document it is reading. This is a
 * few dozen bytes of state against that complexity.
 *
 * Same discipline as the subscription scanner: fold each invoice into running
 * totals as its bytes arrive and forget it, so memory does not scale with the
 * response. No ESP-IDF, so it is host-tested, and the tests feed every chunk
 * size because chunk boundaries are where hand-rolled streaming parsers break.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INVOICES_TOKEN_MAX 64

typedef struct {
    /* --- results --- */
    int failed_count;        /* open, attempted, unpaid invoices */
    int64_t failed_cents;    /* their combined amount due */
    int64_t next_retry;      /* soonest scheduled retry, 0 if none */

    /* --- the invoice being assembled --- */
    char cur_status[16];
    int64_t cur_amount;
    int32_t cur_attempts;
    int64_t cur_retry;
    bool cur_paid;
    bool in_invoice;

    /* --- scanner state --- */
    char token[INVOICES_TOKEN_MAX];
    int token_len;
    char key[INVOICES_TOKEN_MAX];
    int key_len;
    int depth;
    int data_depth;
    bool in_string;
    bool escaped;
    bool expect_value;
    bool in_data_array;
} invoices_t;

void invoices_init(invoices_t *iv);

/* Feed one chunk. Safe with any split of the document. */
void invoices_feed(invoices_t *iv, const char *data, size_t len);

/* Close out the final invoice, if the document ended mid-object. */
void invoices_finish(invoices_t *iv);
