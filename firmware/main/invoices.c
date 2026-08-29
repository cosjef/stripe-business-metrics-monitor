/*
 * Streaming invoice scanner. See invoices.h.
 */
#include "invoices.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void copy_field(char *dst, size_t dst_len, const char *src)
{
    snprintf(dst, dst_len, "%s", src);
}

static void reset_current(invoices_t *iv)
{
    iv->cur_status[0] = '\0';
    iv->cur_amount = 0;
    iv->cur_attempts = 0;
    iv->cur_retry = 0;
    iv->cur_paid = false;
}

void invoices_init(invoices_t *iv)
{
    memset(iv, 0, sizeof(*iv));
}

/*
 * An invoice completed: count it if the money is still collectable.
 *
 * Three conditions, and each excludes a real case seen on the live account:
 *
 *   status == "open"   -- a VOID invoice has been cancelled, so its money is
 *                         not coming and cannot be chased. Counting void
 *                         failures reported $58.13 at stake where only
 *                         $29.00 actually was.
 *   !paid              -- a paid invoice may still carry a failed attempt
 *                         from before it succeeded. That is history.
 *   attempt_count > 0  -- an open invoice never attempted is a bill that has
 *                         not come due, not a failure.
 */
static void finish_invoice(invoices_t *iv)
{
    if (!iv->in_invoice) {
        return;
    }

    if (strcmp(iv->cur_status, "open") == 0 && !iv->cur_paid &&
        iv->cur_attempts > 0) {
        iv->failed_count++;
        iv->failed_cents += iv->cur_amount;

        if (iv->cur_retry > 0 &&
            (iv->next_retry == 0 || iv->cur_retry < iv->next_retry)) {
            iv->next_retry = iv->cur_retry;
        }
    }

    iv->in_invoice = false;
    reset_current(iv);
}

static void apply_token(invoices_t *iv)
{
    const char *k = iv->key;
    const char *v = iv->token;

    if (!iv->in_data_array) {
        return;
    }

    /*
     * Only fields at the invoice's OWN depth.
     *
     * A real Stripe invoice nests automatic_tax, payment_settings, line
     * items and more, and several of those carry a "status" of their own.
     * The first one in the document belongs to automatic_tax and is null --
     * latching it left every invoice with no status, so nothing ever matched
     * "open" and the screen reported zero failures against a live $29.00.
     *
     * Guarding on "first one wins" is not enough when the wrong one comes
     * first; depth is what actually distinguishes them.
     */
    if (iv->depth != iv->data_depth + 1) {
        return;
    }

    if (strcmp(k, "status") == 0 && iv->cur_status[0] == '\0') {
        copy_field(iv->cur_status, sizeof(iv->cur_status), v);
    } else if (strcmp(k, "amount_due") == 0) {
        iv->cur_amount = strtoll(v, NULL, 10);
    } else if (strcmp(k, "attempt_count") == 0) {
        iv->cur_attempts = (int32_t)strtol(v, NULL, 10);
    } else if (strcmp(k, "paid") == 0) {
        iv->cur_paid = (strcmp(v, "true") == 0);
    } else if (strcmp(k, "next_payment_attempt") == 0) {
        /* null when Stripe has stopped retrying; strtoll gives 0, which is
         * what "no retry scheduled" means here. */
        iv->cur_retry = strtoll(v, NULL, 10);
    }
}

void invoices_feed(invoices_t *iv, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];

        if (iv->in_string) {
            if (iv->escaped) {
                iv->escaped = false;
            } else if (c == '\\') {
                iv->escaped = true;
                continue;
            } else if (c == '"') {
                iv->in_string = false;
                iv->token[iv->token_len] = '\0';

                if (iv->expect_value) {
                    apply_token(iv);
                    iv->expect_value = false;
                } else {
                    copy_field(iv->key, sizeof(iv->key), iv->token);
                }
                iv->token_len = 0;
                continue;
            }

            if (iv->token_len < INVOICES_TOKEN_MAX - 1) {
                iv->token[iv->token_len++] = c;
            }
            continue;
        }

        switch (c) {
        case '"':
            iv->in_string = true;
            iv->token_len = 0;
            break;

        case ':':
            iv->expect_value = true;
            break;

        case '[':
            /* The list's data array; invoices are its direct children. */
            if (strcmp(iv->key, "data") == 0 && !iv->in_data_array) {
                iv->in_data_array = true;
                iv->data_depth = iv->depth;
            }
            iv->expect_value = false;
            break;

        case ']':
            if (iv->token_len > 0) {
                iv->token[iv->token_len] = '\0';
                if (iv->expect_value) {
                    apply_token(iv);
                }
                iv->token_len = 0;
                iv->expect_value = false;
            }
            if (iv->in_data_array && iv->depth == iv->data_depth) {
                finish_invoice(iv);
                iv->in_data_array = false;
            }
            break;

        case '{':
            iv->depth++;
            if (iv->in_data_array && iv->depth == iv->data_depth + 1) {
                iv->in_invoice = true;
                reset_current(iv);
            }
            iv->expect_value = false;
            break;

        case '}':
            /* Flush a bare number that ended at this brace. */
            if (iv->token_len > 0) {
                iv->token[iv->token_len] = '\0';
                if (iv->expect_value) {
                    apply_token(iv);
                }
                iv->token_len = 0;
                iv->expect_value = false;
            }
            if (iv->in_data_array && iv->depth == iv->data_depth + 1) {
                finish_invoice(iv);
            }
            iv->depth--;
            break;

        case ',':
            if (iv->token_len > 0) {
                iv->token[iv->token_len] = '\0';
                if (iv->expect_value) {
                    apply_token(iv);
                }
                iv->token_len = 0;
                iv->expect_value = false;
            }
            break;

        default:
            /* Bare literals: numbers, true, false, null. */
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                if (iv->token_len < INVOICES_TOKEN_MAX - 1) {
                    iv->token[iv->token_len++] = c;
                }
            }
            break;
        }
    }
}

void invoices_finish(invoices_t *iv)
{
    finish_invoice(iv);
}
