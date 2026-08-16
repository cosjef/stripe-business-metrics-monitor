/*
 * MRR computation (spec 7.2).
 *
 * MRR is not a first-class Stripe field, so it is derived from subscription
 * items. Spec 7.5 warns this will generate more support contact than anything
 * technical: customers compare the screen against their Stripe dashboard,
 * ChartMogul or Baremetrics and get a different number, because "MRR" is a
 * genuinely contested definition.
 *
 * THE DEFINITION THIS IMPLEMENTS, which should ship in the box (spec 7.5):
 *
 *   - Active and trialing subscriptions are considered; everything else is
 *     ignored.
 *   - TRIALS ARE EXCLUDED from the MRR total and counted separately. A trial
 *     is not yet revenue.
 *   - Annual plans are amortized: yearly / 12, weekly * 52/12, daily * 365/12.
 *   - Discounts are applied BEFORE summing. Skipping this makes a 50%-off
 *     annual plan read at double.
 *   - One-time (non-recurring) prices are skipped entirely.
 *   - Tiered and metered prices are FLAGGED, not guessed at: they carry no
 *     unit_amount and cannot be computed from the price object alone.
 *   - Only one currency is summed. Mixed-currency accounts need a rate table
 *     the device does not have.
 *
 * All arithmetic is in integer cents. Floating point has no place in money.
 *
 * No ESP-IDF dependencies, so this is tested on the host.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MRR_CURRENCY_LEN 8

typedef enum {
    MRR_INTERVAL_UNKNOWN = 0,
    MRR_INTERVAL_DAY,
    MRR_INTERVAL_WEEK,
    MRR_INTERVAL_MONTH,
    MRR_INTERVAL_YEAR,
} mrr_interval_t;

/* One subscription item, flattened from the Stripe price object. */
typedef struct {
    int64_t unit_amount;        /* cents; ignored when tiered */
    int64_t quantity;
    mrr_interval_t interval;
    int32_t interval_count;     /* e.g. 3 for "every 3 months" */
    bool recurring;             /* false for one-time prices */
    bool tiered;                /* billing_scheme == "tiered" */
    char currency[MRR_CURRENCY_LEN];
} mrr_item_t;

/*
 * A subscription's discount, from sub.discount.coupon.
 *
 * Stripe gives either percent_off or amount_off, never both.
 */
typedef struct {
    bool present;
    int32_t percent_off_x100;   /* 50% -> 5000, so 33.33% is representable */
    int64_t amount_off;         /* cents */
} mrr_discount_t;

typedef struct {
    bool trialing;              /* status == "trialing" */
    const mrr_item_t *items;
    int item_count;
    mrr_discount_t discount;
} mrr_subscription_t;

typedef struct {
    int64_t mrr_cents;          /* excludes trials */
    int active_count;           /* paying subscriptions */
    int trial_count;            /* trialing subscriptions */
    int tiered_count;           /* items that could not be computed */
    bool has_tiered;            /* any tiered item present */
    bool mixed_currency;        /* more than one currency seen */
    char currency[MRR_CURRENCY_LEN];  /* the currency summed */
} mrr_totals_t;

/*
 * Monthly value of a single item, in cents, before any discount.
 * Returns 0 for non-recurring, tiered, or unknown-interval items.
 */
int64_t mrr_item_monthly_cents(const mrr_item_t *item);

/*
 * Apply a subscription-level discount to a monthly subtotal.
 * Never returns a negative value: a coupon larger than the subtotal zeroes it
 * rather than subtracting from the account total.
 */
int64_t mrr_apply_discount(int64_t subtotal_cents, const mrr_discount_t *d);

/* Compute totals across a set of subscriptions. */
mrr_totals_t mrr_compute(const mrr_subscription_t *subs, int count);

/* Parse a Stripe interval string ("month", "year", ...). */
mrr_interval_t mrr_interval_from_str(const char *s);
