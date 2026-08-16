/*
 * MRR computation. See mrr.h for the definition this implements and why.
 *
 * Integer cents throughout. Floating point has no place in money: it would
 * make the displayed figure depend on accumulation order.
 */
#include "mrr.h"

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

int64_t mrr_apply_discount(int64_t subtotal_cents, const mrr_discount_t *d)
{
    if (d == NULL || !d->present) {
        return subtotal_cents;
    }

    int64_t out = subtotal_cents;

    if (d->percent_off_x100 > 0) {
        /* percent_off_x100 is hundredths of a percent, so 33.33% is 3333.
         * Rounding to nearest keeps a half-cent from vanishing every time. */
        const int64_t off =
            (subtotal_cents * (int64_t)d->percent_off_x100 + 5000) / 10000;
        out -= off;
    }

    if (d->amount_off > 0) {
        out -= d->amount_off;
    }

    /* A coupon larger than the subscription must not go negative: that would
     * subtract from other subscriptions' revenue in the account total. */
    return out > 0 ? out : 0;
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

        /* The discount belongs to the subscription, so it applies once to the
         * summed items rather than to each of them. */
        out.mrr_cents += mrr_apply_discount(subtotal, &sub->discount);
    }

    return out;
}
