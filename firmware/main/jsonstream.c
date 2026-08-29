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

/*
 * Copy a token into a fixed field, refusing rather than truncating.
 *
 * The token buffer holds up to 64 bytes but these fields are much smaller --
 * a status is 24, a currency 8. A silently clipped value would be worse than
 * an absent one: "activ" matches no status, but "usd\0..." truncated from a
 * longer string could match the wrong currency. Anything too long is simply
 * not ours, so it is dropped.
 */
static void copy_field(char *dst, size_t dst_len, const char *src)
{
    if (strlen(src) >= dst_len) {
        return;
    }
    memcpy(dst, src, strlen(src) + 1);
}

static void reset_current(jsonstream_t *js)
{
    js->cur_id[0] = '\0';
    js->cur_status[0] = '\0';
    js->cur_currency[0] = '\0';
    js->cur_interval[0] = '\0';
    js->cur_unit_amount = 0;
    js->cur_created = 0;
    js->cur_ended = 0;
    js->cur_period_end = 0;
    js->cur_cancel_at_end = false;
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

    /* Reset only the item-scoped fields; id, status and the created/ended
     * timestamps belong to the subscription and outlive its items. Clearing
     * created here silently zeroed the flow counts for every subscription
     * that had a price -- which is all of them. */
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
        copy_field(js->last_id, sizeof(js->last_id), js->cur_id);
    }

    /*
     * Flow, before the status filter below returns.
     *
     * Counted for every subscription regardless of status, because churn
     * lives on the cancelled ones -- the active-only path never sees them.
     * Guarded on window_start so a device without a clock reports nothing
     * rather than counting every subscription ever created as new.
     */
    if (js->window_start > 0) {
        /*
         * The subscription's monthly value. cur_subtotal is folded by
         * finish_item as each price completes, so it is already correct here
         * for cancelled subscriptions too -- their items are parsed the same
         * way, they simply never reach mrr_cents.
         */
        const int64_t worth = js->cur_subtotal;

        if (js->cur_ended >= js->window_start) {
            js->totals.churned_count++;
            js->totals.churned_cents += worth;
        } else if (js->cur_created >= js->window_start &&
                   js->cur_ended == 0) {
            /*
             * New only if it is still running. A subscription created and
             * cancelled inside the same window is churn, not growth; counting
             * it both ways would make the two figures disagree with the net
             * the owner can see for themselves.
             */
            js->totals.new_count++;
            js->totals.new_cents += worth;
        } else if (js->window_span > 0 && js->cur_ended == 0 &&
                   js->cur_created >= js->window_start - js->window_span &&
                   js->cur_created < js->window_start) {
            /*
             * Signed up in the period before this one and still running.
             * Counted for pace only -- it is not new, and its revenue is
             * already inside mrr_cents like any other established customer.
             */
            js->totals.prior_new_count++;
        }
    }

    if (strcmp(js->cur_status, "trialing") == 0) {
        /* Counted, never summed: a trial is not yet revenue. */
        js->totals.trial_count++;
    } else if (strcmp(js->cur_status, "active") == 0 ||
               strcmp(js->cur_status, "past_due") == 0) {
        js->totals.active_count++;

        /*
         * Notice given, but not yet gone.
         *
         * Counted here, inside the active branch, because these subscriptions
         * are still paying: they stay in active_count and their revenue stays
         * in mrr_cents. Excluding them would understate today's income for
         * money that is still arriving. The subtotal is added after the items
         * are folded, below.
         */
        if (js->cur_cancel_at_end) {
            js->totals.at_risk_count++;
            if (js->cur_period_end > 0 &&
                (js->totals.at_risk_soonest == 0 ||
                 js->cur_period_end < js->totals.at_risk_soonest)) {
                /* The soonest departure is the deadline worth showing. */
                js->totals.at_risk_soonest = js->cur_period_end;
            }
        }

        if (js->cur_currency[0] != '\0') {
            if (js->totals.currency[0] == '\0') {
                copy_field(js->totals.currency, sizeof(js->totals.currency),
                       js->cur_currency);
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

        /*
         * Added after the currency check, so at-risk revenue is only summed
         * when it is in the same currency as everything else -- a mixed
         * account bails out above rather than adding dollars to euros here.
         */
        if (js->cur_cancel_at_end) {
            js->totals.at_risk_cents += js->cur_subtotal;
        }
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
        copy_field(js->cur_id, sizeof(js->cur_id), v);
    } else if (strcmp(k, "status") == 0 && js->cur_status[0] == '\0') {
        copy_field(js->cur_status, sizeof(js->cur_status), v);
    } else if (strcmp(k, "unit_amount") == 0) {
        js->cur_unit_amount = strtoll(v, NULL, 10);
        js->item_has_price = true;
    } else if (strcmp(k, "quantity") == 0) {
        js->cur_quantity = strtoll(v, NULL, 10);
    } else if (strcmp(k, "interval") == 0) {
        copy_field(js->cur_interval, sizeof(js->cur_interval), v);
        js->item_has_price = true;
    } else if (strcmp(k, "interval_count") == 0) {
        js->cur_interval_count = (int32_t)strtol(v, NULL, 10);
    } else if (strcmp(k, "currency") == 0 && js->cur_currency[0] == '\0') {
        copy_field(js->cur_currency, sizeof(js->cur_currency), v);
    } else if (strcmp(k, "created") == 0 && js->cur_created == 0) {
        /* First "created" inside a subscription is its own; prices and items
         * carry their own later, and those must not overwrite it. */
        js->cur_created = strtoll(v, NULL, 10);
    } else if (strcmp(k, "ended_at") == 0) {
        js->cur_ended = strtoll(v, NULL, 10);
    } else if (strcmp(k, "cancel_at_period_end") == 0) {
        js->cur_cancel_at_end = (strcmp(v, "true") == 0);
    } else if (strcmp(k, "current_period_end") == 0 &&
               js->cur_period_end == 0) {
        js->cur_period_end = strtoll(v, NULL, 10);
    } else if (strcmp(k, "billing_scheme") == 0) {
        js->cur_tiered = (strcmp(v, "tiered") == 0);
    }
}

void jsonstream_set_window(jsonstream_t *js, int64_t window_start)
{
    js->window_start = window_start;
}

void jsonstream_set_span(jsonstream_t *js, int64_t span_seconds)
{
    js->window_span = span_seconds;
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
                    copy_field(js->key, sizeof(js->key), js->token);
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
            /*
             * Item completion is driven by the data seen, not by depth.
             *
             * An earlier version closed an item when depth hit a fixed offset
             * from data_depth, which assumed Stripe nests price objects at a
             * constant level. On the real account it does not, and the result
             * was 33 subscriptions counted with zero cents: statuses parsed,
             * amounts silently dropped. A price is complete once it has both
             * an amount and an interval, so fold it then.
             */
            if (js->in_data_array && js->item_has_price &&
                js->cur_unit_amount > 0 && js->cur_interval[0] != '\0') {
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
