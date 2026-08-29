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

typedef struct {
    bool trialing;              /* status == "trialing" */
    const mrr_item_t *items;
    int item_count;
} mrr_subscription_t;

typedef struct {
    int64_t mrr_cents;          /* excludes trials */
    int active_count;           /* paying subscriptions */
    int trial_count;            /* trialing subscriptions */
    int tiered_count;           /* items that could not be computed */
    /*
     * Subscriber flow over the caller's window. Zero unless a window was set
     * -- see jsonstream_set_window.
     */
    int new_count;              /* created inside the window, still active */
    int churned_count;          /* ended inside the window */

    /*
     * The monthly revenue those subscriptions carried.
     *
     * Counts alone cannot tell whether a month went well: ten signups at $13
     * do not replace seven cancellations at $49. churned_cents is what left,
     * even though that money is no longer in mrr_cents -- the figure measures
     * what the window cost, not what is running now.
     */
    int64_t new_cents;
    int64_t churned_cents;

    /*
     * Subscriptions that have given notice but have not left.
     *
     * These are still active and still counted in mrr_cents -- the money is
     * still arriving. They are tracked separately because they are the only
     * figure on the device that can still be changed by acting on it.
     */
    int at_risk_count;
    int64_t at_risk_cents;      /* their combined monthly value */
    int64_t at_risk_soonest;    /* earliest period end, Unix seconds; 0 if none */
    bool has_tiered;            /* any tiered item present */
    bool mixed_currency;        /* more than one currency seen */
    char currency[MRR_CURRENCY_LEN];  /* the currency summed */
} mrr_totals_t;

/*
 * Fold one page's totals into a running accumulator.
 *
 * Pagination means MRR is computed per page and summed, rather than over the
 * whole account at once -- on a board with no PSRAM, holding every
 * subscription to compute once at the end costs more memory than the response
 * buffer it displaces.
 *
 * Most fields add. Two do not:
 *
 *   mixed_currency  latches, and must also compare currencies BETWEEN pages:
 *                   a USD first page and a EUR second page is a mixed account
 *                   even though neither page alone looks mixed.
 *   has_tiered      latches; a later clean page must not clear it.
 *
 * An empty page contributes nothing and must not blank the currency.
 */
void mrr_totals_merge(mrr_totals_t *acc, const mrr_totals_t *page);

/*
 * Monthly value of a single item, in cents.
 * Returns 0 for non-recurring, tiered, or unknown-interval items.
 */
int64_t mrr_item_monthly_cents(const mrr_item_t *item);


/* Compute totals across a set of subscriptions. */
mrr_totals_t mrr_compute(const mrr_subscription_t *subs, int count);

/* Parse a Stripe interval string ("month", "year", ...). */
mrr_interval_t mrr_interval_from_str(const char *s);

/*
 * Annual run rate: monthly revenue projected over a year.
 *
 * A projection, not a measurement -- it assumes today's MRR holds for twelve
 * months, which it will not. Shown because it is the figure most people quote
 * for a subscription business, but it is arithmetic on MRR rather than new
 * information.
 */
int64_t mrr_arr_cents(int64_t mrr_cents);

/*
 * Average revenue per paying customer.
 *
 * Returns 0 when there are no active subscriptions: an average over zero
 * customers is undefined, and showing 0 is more honest than dividing by zero
 * or inventing a number.
 *
 * Note this is per SUBSCRIPTION, not per customer -- one customer with two
 * subscriptions counts twice. Stripe's subscription list does not tell us
 * which belong to the same customer without expanding, and at typical
 * one-subscription-per-customer accounts the distinction does not arise.
 */
int64_t mrr_arpu_cents(int64_t mrr_cents, int active_count);
