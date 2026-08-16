/*
 * Host tests for MRR computation (spec 7.2).
 *
 *   cd firmware/test && make && ./test_mrr
 *
 * Spec 7.5 predicts this will generate more support contact than anything
 * technical, because customers compare the number against their Stripe
 * dashboard and disagree. These tests pin the definition so at least the
 * device is consistently wrong-by-definition rather than wrong-by-accident.
 */
#include <stdio.h>
#include <string.h>

#include "../main/mrr.h"

static int failures = 0;
static int checks = 0;

static void check_i64(const char *what, int64_t got, int64_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %lld, want %lld\n", what,
               (long long)got, (long long)want);
    }
}

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

static void check_false(const char *what, int cond)
{
    checks++;
    if (cond) {
        failures++;
        printf("  FAIL %s: expected false\n", what);
    }
}

static mrr_item_t item(int64_t amount, int64_t qty, mrr_interval_t iv, int32_t n)
{
    mrr_item_t i = {
        .unit_amount = amount, .quantity = qty, .interval = iv,
        .interval_count = n, .recurring = true, .tiered = false,
    };
    strcpy(i.currency, "usd");
    return i;
}

/* ---- interval conversion ---- */

static void test_interval_parsing(void)
{
    printf("interval parsing\n");

    check_int("month", mrr_interval_from_str("month"), MRR_INTERVAL_MONTH);
    check_int("year", mrr_interval_from_str("year"), MRR_INTERVAL_YEAR);
    check_int("week", mrr_interval_from_str("week"), MRR_INTERVAL_WEEK);
    check_int("day", mrr_interval_from_str("day"), MRR_INTERVAL_DAY);
    check_int("unknown", mrr_interval_from_str("fortnight"), MRR_INTERVAL_UNKNOWN);
    check_int("NULL", mrr_interval_from_str(NULL), MRR_INTERVAL_UNKNOWN);
}

static void test_monthly_conversion(void)
{
    printf("interval to monthly conversion (spec 7.2)\n");

    /* $29/month */
    mrr_item_t m = item(2900, 1, MRR_INTERVAL_MONTH, 1);
    check_i64("monthly plan", mrr_item_monthly_cents(&m), 2900);

    /* $290/year amortized: 29000 / 12 = 2416.67 -> 2416 */
    mrr_item_t y = item(29000, 1, MRR_INTERVAL_YEAR, 1);
    check_i64("annual plan amortized", mrr_item_monthly_cents(&y), 2416);

    /* $10/week: 1000 * 52 / 12 = 4333.33 -> 4333 */
    mrr_item_t w = item(1000, 1, MRR_INTERVAL_WEEK, 1);
    check_i64("weekly plan", mrr_item_monthly_cents(&w), 4333);

    /* $1/day: 100 * 365 / 12 = 3041.67 -> 3041 */
    mrr_item_t d = item(100, 1, MRR_INTERVAL_DAY, 1);
    check_i64("daily plan", mrr_item_monthly_cents(&d), 3041);

    /* Quantity multiplies. */
    mrr_item_t q = item(2900, 5, MRR_INTERVAL_MONTH, 1);
    check_i64("quantity 5", mrr_item_monthly_cents(&q), 14500);

    /* interval_count divides: every 3 months means a third per month. */
    mrr_item_t q3 = item(9000, 1, MRR_INTERVAL_MONTH, 3);
    check_i64("every 3 months", mrr_item_monthly_cents(&q3), 3000);

    /* Every 2 years: 24000 / (12*2) = 1000 */
    mrr_item_t y2 = item(24000, 1, MRR_INTERVAL_YEAR, 2);
    check_i64("every 2 years", mrr_item_monthly_cents(&y2), 1000);
}

/*
 * One-time prices are not recurring revenue. Including them would inflate MRR
 * by whatever the customer happened to buy that month.
 */
static void test_non_recurring_skipped(void)
{
    printf("one-time prices are skipped (spec 7.2)\n");

    mrr_item_t once = item(50000, 1, MRR_INTERVAL_MONTH, 1);
    once.recurring = false;
    check_i64("non-recurring contributes nothing",
              mrr_item_monthly_cents(&once), 0);
}

/*
 * Tiered prices carry no unit_amount. Spec 7.2 step 3 is explicit that they
 * must be flagged rather than computed -- this is the line that breaks when
 * usage pricing is added.
 */
static void test_tiered_flagged_not_guessed(void)
{
    printf("tiered prices are flagged, not guessed (spec 7.2 step 3)\n");

    mrr_item_t t = item(0, 1, MRR_INTERVAL_MONTH, 1);
    t.tiered = true;
    check_i64("tiered contributes nothing", mrr_item_monthly_cents(&t), 0);

    /* Even with a unit_amount set, tiered must not be trusted. */
    mrr_item_t t2 = item(9999, 1, MRR_INTERVAL_MONTH, 1);
    t2.tiered = true;
    check_i64("tiered ignores unit_amount", mrr_item_monthly_cents(&t2), 0);

    const mrr_item_t items[] = {t};
    const mrr_subscription_t subs[] = {{ .trialing = false, .items = items,
                                         .item_count = 1 }};
    const mrr_totals_t r = mrr_compute(subs, 1);

    check_true("tiered is surfaced", r.has_tiered);
    check_int("tiered counted", r.tiered_count, 1);
}

static void test_unknown_interval_skipped(void)
{
    printf("unknown intervals contribute nothing\n");

    mrr_item_t u = item(2900, 1, MRR_INTERVAL_UNKNOWN, 1);
    check_i64("unknown interval", mrr_item_monthly_cents(&u), 0);
}

/* ---- discounts ---- */

/*
 * Spec 7.2 step 1: discounts are applied BEFORE summing. Skipping this makes
 * a 50%-off annual plan read at double -- the exact error the spec names.
 */
static void test_discounts(void)
{
    printf("discounts applied before summing (spec 7.2 step 1)\n");

    mrr_discount_t none = { .present = false };
    check_i64("no discount", mrr_apply_discount(10000, &none), 10000);

    mrr_discount_t half = { .present = true, .percent_off_x100 = 5000 };
    check_i64("50 percent off", mrr_apply_discount(10000, &half), 5000);

    mrr_discount_t third = { .present = true, .percent_off_x100 = 3333 };
    check_i64("33.33 percent off", mrr_apply_discount(10000, &third), 6667);

    mrr_discount_t ten = { .present = true, .amount_off = 1000 };
    check_i64("$10 off", mrr_apply_discount(10000, &ten), 9000);

    /* A coupon larger than the subtotal must not go negative and start
     * subtracting from other subscriptions' revenue. */
    mrr_discount_t huge = { .present = true, .amount_off = 99999 };
    check_i64("oversized amount_off clamps to zero",
              mrr_apply_discount(10000, &huge), 0);

    mrr_discount_t full = { .present = true, .percent_off_x100 = 10000 };
    check_i64("100 percent off", mrr_apply_discount(10000, &full), 0);

    mrr_discount_t over = { .present = true, .percent_off_x100 = 15000 };
    check_i64("over-100 percent clamps to zero",
              mrr_apply_discount(10000, &over), 0);
}

/*
 * The specific scenario spec 7.2 warns about.
 */
static void test_discounted_annual_plan(void)
{
    printf("50%%-off annual plan does not read at double\n");

    /* $1200/year at 50% off = $600/year = $50/month */
    const mrr_item_t items[] = { item(120000, 1, MRR_INTERVAL_YEAR, 1) };
    const mrr_subscription_t subs[] = {{
        .trialing = false, .items = items, .item_count = 1,
        .discount = { .present = true, .percent_off_x100 = 5000 },
    }};

    const mrr_totals_t r = mrr_compute(subs, 1);
    check_i64("discounted annual", r.mrr_cents, 5000);
}

/* ---- trials ---- */

/*
 * Spec 7.2 step 2: trials are counted separately, never added to MRR. A trial
 * is not yet revenue, and the trial count is its own screen anyway.
 */
static void test_trials_excluded(void)
{
    printf("trials excluded from MRR, counted separately (spec 7.2 step 2)\n");

    const mrr_item_t items[] = { item(2900, 1, MRR_INTERVAL_MONTH, 1) };

    const mrr_subscription_t subs[] = {
        { .trialing = false, .items = items, .item_count = 1 },
        { .trialing = true,  .items = items, .item_count = 1 },
        { .trialing = true,  .items = items, .item_count = 1 },
    };

    const mrr_totals_t r = mrr_compute(subs, 3);

    check_i64("only the paying subscription counts", r.mrr_cents, 2900);
    check_int("active count", r.active_count, 1);
    check_int("trial count", r.trial_count, 2);
}

/* ---- currency ---- */

/*
 * Spec 7.2 step 4: sum only within a single currency. A mixed-currency account
 * needs a rate table the device does not have, so the honest response is to
 * flag it rather than add dollars to euros.
 */
static void test_mixed_currency_flagged(void)
{
    printf("mixed currency is flagged, not summed (spec 7.2 step 4)\n");

    mrr_item_t usd = item(2900, 1, MRR_INTERVAL_MONTH, 1);
    mrr_item_t eur = item(2900, 1, MRR_INTERVAL_MONTH, 1);
    strcpy(eur.currency, "eur");

    const mrr_item_t usd_items[] = {usd};
    const mrr_item_t eur_items[] = {eur};

    const mrr_subscription_t subs[] = {
        { .trialing = false, .items = usd_items, .item_count = 1 },
        { .trialing = false, .items = eur_items, .item_count = 1 },
    };

    const mrr_totals_t r = mrr_compute(subs, 2);

    check_true("mixed currency detected", r.mixed_currency);
    /* The first currency seen wins; the other is excluded rather than
     * silently converted at 1:1. */
    check_i64("only the primary currency summed", r.mrr_cents, 2900);
}

static void test_single_currency_not_flagged(void)
{
    printf("a single currency is not flagged\n");

    const mrr_item_t items[] = { item(2900, 1, MRR_INTERVAL_MONTH, 1) };
    const mrr_subscription_t subs[] = {
        { .trialing = false, .items = items, .item_count = 1 },
        { .trialing = false, .items = items, .item_count = 1 },
    };

    const mrr_totals_t r = mrr_compute(subs, 2);
    check_false("not flagged", r.mixed_currency);
    check_i64("both summed", r.mrr_cents, 5800);
    checks++;
    if (strcmp(r.currency, "usd") != 0) {
        failures++;
        printf("  FAIL currency reported: got \"%s\", want \"usd\"\n", r.currency);
    }
}

/* ---- realistic accounts ---- */

static void test_multi_item_subscription(void)
{
    printf("multi-item subscriptions sum their items\n");

    const mrr_item_t items[] = {
        item(2900, 1, MRR_INTERVAL_MONTH, 1),   /* base plan */
        item(500, 3, MRR_INTERVAL_MONTH, 1),    /* 3 seats at $5 */
    };
    const mrr_subscription_t subs[] = {{
        .trialing = false, .items = items, .item_count = 2,
    }};

    const mrr_totals_t r = mrr_compute(subs, 1);
    check_i64("base plus seats", r.mrr_cents, 4400);
}

/*
 * A discount applies to the whole subscription, not per item -- so it must be
 * applied after the items are summed, not to each one.
 */
static void test_discount_applies_to_subscription_total(void)
{
    printf("discount applies to the subscription, not per item\n");

    const mrr_item_t items[] = {
        item(2000, 1, MRR_INTERVAL_MONTH, 1),
        item(2000, 1, MRR_INTERVAL_MONTH, 1),
    };
    const mrr_subscription_t subs[] = {{
        .trialing = false, .items = items, .item_count = 2,
        .discount = { .present = true, .amount_off = 1000 },
    }};

    /* 4000 - 1000 = 3000, not 4000 - 2000 (which per-item would give). */
    const mrr_totals_t r = mrr_compute(subs, 1);
    check_i64("one discount for the subscription", r.mrr_cents, 3000);
}

static void test_mixed_realistic_account(void)
{
    printf("a realistic mixed account\n");

    const mrr_item_t monthly[] = { item(2900, 1, MRR_INTERVAL_MONTH, 1) };
    const mrr_item_t annual[]  = { item(29000, 1, MRR_INTERVAL_YEAR, 1) };
    mrr_item_t tiered_item = item(0, 1, MRR_INTERVAL_MONTH, 1);
    tiered_item.tiered = true;
    const mrr_item_t tiered[] = { tiered_item };

    const mrr_subscription_t subs[] = {
        { .trialing = false, .items = monthly, .item_count = 1 },
        { .trialing = false, .items = monthly, .item_count = 1 },
        { .trialing = false, .items = annual,  .item_count = 1 },
        { .trialing = true,  .items = monthly, .item_count = 1 },
        { .trialing = false, .items = tiered,  .item_count = 1 },
    };

    const mrr_totals_t r = mrr_compute(subs, 5);

    /* 2900 + 2900 + 2416 = 8216; the trial and the tiered item add nothing. */
    check_i64("total", r.mrr_cents, 8216);
    check_int("active", r.active_count, 4);
    check_int("trials", r.trial_count, 1);
    check_true("tiered flagged", r.has_tiered);
}

static void test_empty_account(void)
{
    printf("an empty account\n");

    const mrr_totals_t r = mrr_compute(NULL, 0);
    check_i64("zero MRR", r.mrr_cents, 0);
    check_int("no actives", r.active_count, 0);
    check_int("no trials", r.trial_count, 0);
    check_false("no tiered", r.has_tiered);
    check_false("not mixed currency", r.mixed_currency);
}

/*
 * Integer cents must not overflow on a large account. A 32-bit accumulator
 * would wrap around $21m/month.
 */
static void test_large_account_no_overflow(void)
{
    printf("a large account does not overflow\n");

    /* 1000 subscriptions at $10,000/month = $10m/month = 1e9 cents. */
    static mrr_item_t big_items[1];
    big_items[0] = item(1000000, 1, MRR_INTERVAL_MONTH, 1);

    static mrr_subscription_t many[1000];
    for (int i = 0; i < 1000; i++) {
        many[i].trialing = false;
        many[i].items = big_items;
        many[i].item_count = 1;
        many[i].discount.present = false;
    }

    const mrr_totals_t r = mrr_compute(many, 1000);
    check_i64("1000 x $10k", r.mrr_cents, 1000000000LL);
    check_int("all active", r.active_count, 1000);
}

int main(void)
{
    printf("MRR computation tests (spec 7.2)\n\n");

    test_interval_parsing();
    test_monthly_conversion();
    test_non_recurring_skipped();
    test_tiered_flagged_not_guessed();
    test_unknown_interval_skipped();
    test_discounts();
    test_discounted_annual_plan();
    test_trials_excluded();
    test_mixed_currency_flagged();
    test_single_currency_not_flagged();
    test_multi_item_subscription();
    test_discount_applies_to_subscription_total();
    test_mixed_realistic_account();
    test_empty_account();
    test_large_account_no_overflow();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
