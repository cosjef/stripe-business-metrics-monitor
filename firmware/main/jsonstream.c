/*
 * Streaming JSON scanner. See jsonstream.h for why this exists.
 *
 * The scanner is a character-at-a-time state machine, which is what makes it
 * indifferent to chunk boundaries: there is no point at which it needs the
 * "rest" of anything. Tokens accumulate into a fixed buffer and are consumed
 * when their terminator arrives, whichever chunk that lands in.
 */
#include "jsonstream.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void reset_current(jsonstream_t *js)
{
    js->cur_id[0] = '\0';
    js->cur_status[0] = '\0';
    js->cur_currency[0] = '\0';
    js->cur_interval[0] = '\0';
    js->cur_unit_amount = 0;
    js->cur_quantity = 1;
    js->cur_interval_count = 1;
    js->cur_subtotal = 0;
    js->cur_tiered = false;
    js->item_has_price = false;
}

void jsonstream_init(jsonstream_t *js)
{
    memset(js, 0, sizeof(*js));
    reset_current(js);
}

/* Monthly value of the item just completed, normalising the interval. */
static int64_t item_monthly(const jsonstream_t *js)
{
    if (js->cur_tiered || js->cur_unit_amount <= 0) {
        return 0;
    }

    const int64_t line = js->cur_unit_amount * js->cur_quantity;
    const int32_t n = js->cur_interval_count > 0 ? js->cur_interval_count : 1;

    if (strcmp(js->cur_interval, "month") == 0) {
        return line / n;
    }
    if (strcmp(js->cur_interval, "year") == 0) {
        return line / (12 * n);
    }
    if (strcmp(js->cur_interval, "week") == 0) {
        /* 52 weeks / 12 months. Integer maths throughout, as elsewhere. */
        return (line * 52) / (12 * n);
    }
    if (strcmp(js->cur_interval, "day") == 0) {
        return (line * 365) / (12 * n);
    }
    return 0;   /* unknown or non-recurring */
}

/* An item finished: fold it into the subscription's subtotal. */
static void finish_item(jsonstream_t *js)
{
    if (!js->item_has_price) {
        return;
    }

    js->cur_subtotal += item_monthly(js);

    /* Reset only the item-scoped fields; id and status belong to the
     * subscription and outlive its items. */
    js->cur_unit_amount = 0;
    js->cur_quantity = 1;
    js->cur_interval_count = 1;
    js->cur_interval[0] = '\0';
    js->item_has_price = false;
}

/* A subscription finished: fold it into the totals and forget it. */
static void finish_subscription(jsonstream_t *js)
{
    if (!js->in_subscription) {
        return;
    }

    /* The cursor advances for every subscription seen, including statuses
     * that do not count -- otherwise a page ending in a cancelled one would
     * resume from too early a point and loop. */
    if (js->cur_id[0] != '\0') {
        snprintf(js->last_id, sizeof(js->last_id), "%s", js->cur_id);
    }

    if (strcmp(js->cur_status, "trialing") == 0) {
        /* Counted, never summed: a trial is not yet revenue. */
        js->totals.trial_count++;
    } else if (strcmp(js->cur_status, "active") == 0 ||
               strcmp(js->cur_status, "past_due") == 0) {
        js->totals.active_count++;

        if (js->cur_currency[0] != '\0') {
            if (js->totals.currency[0] == '\0') {
                snprintf(js->totals.currency, sizeof(js->totals.currency),
                         "%s", js->cur_currency);
            } else if (strcmp(js->totals.currency, js->cur_currency) != 0) {
                /* No rate table on the device: flag rather than add dollars
                 * to euros. */
                js->totals.mixed_currency = true;
                js->in_subscription = false;
                reset_current(js);
                return;
            }
        }

        js->totals.mrr_cents += js->cur_subtotal;
    }

    if (js->cur_tiered) {
        js->totals.has_tiered = true;
        js->totals.tiered_count++;
    }

    js->in_subscription = false;
    reset_current(js);
}

/* A complete token arrived; apply it against the key that preceded it. */
static void apply_token(jsonstream_t *js)
{
    const char *k = js->key;
    const char *v = js->token;

    if (strcmp(k, "has_more") == 0) {
        js->has_more = (strcmp(v, "true") == 0);
        return;
    }

    if (!js->in_data_array) {
        return;
    }

    if (strcmp(k, "id") == 0 && js->cur_id[0] == '\0') {
        /* First id inside a subscription is the subscription's own; ids on
         * nested objects (items, prices) arrive later and are ignored. */
        snprintf(js->cur_id, sizeof(js->cur_id), "%s", v);
    } else if (strcmp(k, "status") == 0 && js->cur_status[0] == '\0') {
        snprintf(js->cur_status, sizeof(js->cur_status), "%s", v);
    } else if (strcmp(k, "unit_amount") == 0) {
        js->cur_unit_amount = strtoll(v, NULL, 10);
        js->item_has_price = true;
    } else if (strcmp(k, "quantity") == 0) {
        js->cur_quantity = strtoll(v, NULL, 10);
    } else if (strcmp(k, "interval") == 0) {
        snprintf(js->cur_interval, sizeof(js->cur_interval), "%s", v);
        js->item_has_price = true;
    } else if (strcmp(k, "interval_count") == 0) {
        js->cur_interval_count = (int32_t)strtol(v, NULL, 10);
    } else if (strcmp(k, "currency") == 0 && js->cur_currency[0] == '\0') {
        snprintf(js->cur_currency, sizeof(js->cur_currency), "%s", v);
    } else if (strcmp(k, "billing_scheme") == 0) {
        js->cur_tiered = (strcmp(v, "tiered") == 0);
    }
}

void jsonstream_feed(jsonstream_t *js, const char *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const char c = data[i];

        /* Inside a string, structure characters are just text. This is what
         * stops customer metadata containing braces from being read as
         * nesting. */
        if (js->in_string) {
            if (js->escaped) {
                js->escaped = false;
            } else if (c == '\\') {
                js->escaped = true;
                continue;
            } else if (c == '"') {
                js->in_string = false;
                js->token[js->token_len] = '\0';

                if (js->expect_value) {
                    apply_token(js);
                    js->expect_value = false;
                } else {
                    snprintf(js->key, sizeof(js->key), "%s", js->token);
                }
                js->token_len = 0;
                continue;
            }

            if (js->token_len < JSONSTREAM_TOKEN_MAX - 1) {
                js->token[js->token_len++] = c;
            } else {
                js->truncated = true;
            }
            continue;
        }

        switch (c) {
        case '"':
            js->in_string = true;
            js->token_len = 0;
            break;

        case ':':
            js->expect_value = true;
            break;

        case '{':
            js->depth++;
            /* An object opening directly inside data[] is a subscription. */
            if (js->in_data_array && js->depth == js->data_depth + 1) {
                js->in_subscription = true;
                reset_current(js);
            }
            js->expect_value = false;
            break;

        case '}':
            /* Flush any bare number that ended at this brace. */
            if (js->token_len > 0) {
                js->token[js->token_len] = '\0';
                if (js->expect_value) { apply_token(js); }
                js->token_len = 0;
                js->expect_value = false;
            }
            if (js->in_data_array && js->depth == js->data_depth + 3) {
                /* Closing an item object (data[] > sub > items > data > item). */
                finish_item(js);
            }
            if (js->in_data_array && js->depth == js->data_depth + 1) {
                finish_item(js);
                finish_subscription(js);
            }
            js->depth--;
            break;

        case '[':
            js->depth++;
            if (strcmp(js->key, "data") == 0 && !js->in_data_array) {
                js->in_data_array = true;
                js->data_depth = js->depth;
            }
            js->expect_value = false;
            break;

        case ']':
            if (js->token_len > 0) {
                js->token[js->token_len] = '\0';
                if (js->expect_value) { apply_token(js); }
                js->token_len = 0;
                js->expect_value = false;
            }
            if (js->in_data_array && js->depth == js->data_depth) {
                js->in_data_array = false;
            }
            js->depth--;
            break;

        case ',':
            if (js->token_len > 0) {
                js->token[js->token_len] = '\0';
                if (js->expect_value) { apply_token(js); }
                js->token_len = 0;
            }
            js->expect_value = false;
            break;

        case ' ': case '\t': case '\n': case '\r':
            break;

        default:
            /* Bare token: a number, true, false or null. Accumulated until a
             * delimiter arrives, which may be in a later chunk. */
            if (js->token_len < JSONSTREAM_TOKEN_MAX - 1) {
                js->token[js->token_len++] = c;
            } else {
                js->truncated = true;
            }
            break;
        }
    }
}

void jsonstream_finish(jsonstream_t *js)
{
    finish_item(js);
    finish_subscription(js);
}

const mrr_totals_t *jsonstream_totals(const jsonstream_t *js)
{
    return &js->totals;
}

const char *jsonstream_last_id(const jsonstream_t *js)
{
    return js->last_id;
}

bool jsonstream_has_more(const jsonstream_t *js)
{
    return js->has_more;
}
