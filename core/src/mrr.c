/*
 * MRR computation. See mrr.h for the definition this implements and why.
 *
 * Integer cents throughout. Floating point has no place in money: it would
 * make the displayed figure depend on accumulation order.
 */
#include "mrr.h"

#include <stdio.h>

#include <string.h>

mrr_interval_t mrr_interval_from_str(const char *s)
{
    if (s == NULL) {
        return MRR_INTERVAL_UNKNOWN;
    }
    if (strcmp(s, "month") == 0) return MRR_INTERVAL_MONTH;
    if (strcmp(s, "year") == 0)  return MRR_INTERVAL_YEAR;
    if (strcmp(s, "week") == 0)  return MRR_INTERVAL_WEEK;
    if (strcmp(s, "day") == 0)   return MRR_INTERVAL_DAY;
    return MRR_INTERVAL_UNKNOWN;
}

int64_t mrr_item_monthly_cents(const mrr_item_t *item)
{
    if (item == NULL || !item->recurring) {
        return 0;
    }

    /* Tiered and metered prices carry no usable unit_amount. Guessing from
     * whatever happens to be in the field would silently miscompute, so they
     * contribute nothing and are surfaced separately (spec 7.2 step 3). */
    if (item->tiered) {
        return 0;
    }

    const int32_t n = item->interval_count > 0 ? item->interval_count : 1;
    const int64_t gross = item->unit_amount * item->quantity;

    /* Conversions from spec 7.2. Multiply before dividing to keep precision;
     * the truncation at the end is deliberate and consistent. */
    switch (item->interval) {
    case MRR_INTERVAL_MONTH:
        return gross / n;
    case MRR_INTERVAL_YEAR:
        return gross / (12 * (int64_t)n);
    case MRR_INTERVAL_WEEK:
        return (gross * 52) / (12 * (int64_t)n);
    case MRR_INTERVAL_DAY:
        return (gross * 365) / (12 * (int64_t)n);
    case MRR_INTERVAL_UNKNOWN:
    default:
        /* An interval Stripe has added since this was written. Contributing
         * nothing is wrong, but inventing a conversion is worse. */
        return 0;
    }
}

mrr_totals_t mrr_compute(const mrr_subscription_t *subs, int count)
{
    mrr_totals_t out = {0};

    if (subs == NULL || count <= 0) {
        return out;
    }

    for (int s = 0; s < count; s++) {
        const mrr_subscription_t *sub = &subs[s];

        if (sub->trialing) {
            /* Counted, never summed. A trial is not yet revenue, and the count
             * is its own screen (spec 7.2 step 2). */
            out.trial_count++;
            continue;
        }

        out.active_count++;

        int64_t subtotal = 0;

        for (int i = 0; i < sub->item_count; i++) {
            const mrr_item_t *it = &sub->items[i];

            if (it->tiered) {
                out.tiered_count++;
                out.has_tiered = true;
                continue;
            }

            if (!it->recurring) {
                continue;
            }

            /* Establish the account currency from the first item that carries
             * one; anything different is flagged rather than converted. */
            if (out.currency[0] == '\0' && it->currency[0] != '\0') {
                strncpy(out.currency, it->currency, MRR_CURRENCY_LEN - 1);
            } else if (it->currency[0] != '\0' &&
                       strcmp(out.currency, it->currency) != 0) {
                /* Mixed currency needs a rate table the device does not have
                 * (spec 7.2 step 4). Exclude rather than add dollars to
                 * euros, and surface it so the screen can say so. */
                out.mixed_currency = true;
                continue;
            }

            subtotal += mrr_item_monthly_cents(it);
        }

        /*
         * Coupons are not supported: this account does not use them, and the
         * unexpanded subscription object may carry only a discount id rather
         * than the nested coupon, which would have meant silently ignoring
         * some discounts while honouring others. Not applying any is at least
         * consistent and knowable.
         */
        out.mrr_cents += subtotal;
    }

    return out;
}

int64_t mrr_arr_cents(int64_t mrr_cents)
{
    return mrr_cents * 12;
}

_Bool mrr_mix_comparable(int new_count, int churned_count)
{
    return new_count >= MRR_MIX_MIN && churned_count >= MRR_MIX_MIN;
}

int64_t mrr_arpu_cents(int64_t mrr_cents, int active_count)
{
    /* An average over zero customers is undefined. Returning 0 is honest;
     * dividing would crash and inventing a figure would be worse. */
    if (active_count <= 0) {
        return 0;
    }

    /* Truncate rather than round: on a revenue readout, understating
     * per-customer value is the safer direction to err. */
    return mrr_cents / active_count;
}

void mrr_totals_merge(mrr_totals_t *acc, const mrr_totals_t *page)
{
    acc->mrr_cents    += page->mrr_cents;
    acc->active_count += page->active_count;
    acc->trial_count  += page->trial_count;
    acc->tiered_count += page->tiered_count;

    /* Latches: one page seeing tiered pricing means the account has it. */
    acc->has_tiered     = acc->has_tiered || page->has_tiered;
    acc->mixed_currency = acc->mixed_currency || page->mixed_currency;

    /* A page with no subscriptions carries no currency; adopting its empty
     * string would blank what earlier pages established. */
    if (page->currency[0] == '\0') {
        return;
    }

    if (acc->currency[0] == '\0') {
        snprintf(acc->currency, sizeof(acc->currency), "%s", page->currency);
        return;
    }

    /* Different currency from an earlier page: the account is mixed even
     * though no single page looked it. Keep the first currency seen so the
     * caller has something to name, but flag the sum as untrustworthy. */
    if (strcmp(acc->currency, page->currency) != 0) {
        acc->mixed_currency = true;
    }
}
