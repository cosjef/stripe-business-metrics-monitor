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

#include "../include/mrr.h"

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

static void check_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
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
    }

    const mrr_totals_t r = mrr_compute(many, 1000);
    check_i64("1000 x $10k", r.mrr_cents, 1000000000LL);
    check_int("all active", r.active_count, 1000);
}

/* ---- derived metrics ---- */

static void test_arr(void)
{
    printf("annual run rate\n");

    check_i64("zero", mrr_arr_cents(0), 0);
    check_i64("$941.33/mo -> $11,295.96/yr", mrr_arr_cents(94133), 1129596);
    check_i64("$1000/mo", mrr_arr_cents(100000), 1200000);

    /* Must not overflow on a large account: $10m/month is 1.2e9 cents/year,
     * which needs more than 32 bits. */
    check_i64("$10m/mo does not overflow",
              mrr_arr_cents(1000000000LL), 12000000000LL);
}

static void test_arpu(void)
{
    printf("average revenue per subscription\n");

    /* The real account: $941.33 over 28 subscriptions. */
    check_i64("$941.33 / 28", mrr_arpu_cents(94133, 28), 3361);

    check_i64("clean division", mrr_arpu_cents(100000, 10), 10000);

    /* An average over no customers is undefined; 0 is the honest answer. */
    check_i64("no customers", mrr_arpu_cents(94133, 0), 0);
    check_i64("negative count", mrr_arpu_cents(94133, -1), 0);

    check_i64("zero revenue", mrr_arpu_cents(0, 28), 0);

    /* Truncation, not rounding: overstating per-customer revenue would be the
     * wrong direction to err on a revenue readout. */
    check_i64("truncates rather than rounds up", mrr_arpu_cents(1099, 10), 109);
}

/*
 * Folding totals across pages.
 *
 * Pagination is not optional on the C6: the heap allows two subscriptions per
 * request. My first attempt accumulated every subscription and computed MRR
 * once at the end, which needed a 5KB array plus a 25KB parse buffer held for
 * the whole loop -- and that fragmentation dropped the largest free block from
 * 35KB to 14KB, so the response buffer could no longer be allocated at all.
 * The fix is to fold each page's totals into a running sum and keep nothing
 * else.
 *
 * Most fields simply add. Two do not, and they are what this tests:
 * mixed_currency must latch across pages, and the currency itself must be
 * compared BETWEEN pages, not just within one -- an account whose first page
 * is USD and second is EUR is mixed, even though neither page alone is.
 */
static void test_totals_merge_across_pages(void)
{
    printf("totals fold across pages\n");

    mrr_totals_t acc = {0};
    mrr_totals_t page1 = {
        .mrr_cents = 5000, .active_count = 2, .trial_count = 1,
        .currency = "usd",
    };
    mrr_totals_t page2 = {
        .mrr_cents = 3000, .active_count = 1, .trial_count = 0,
        .currency = "usd",
    };

    mrr_totals_merge(&acc, &page1);
    check_i64("first page sets the total", acc.mrr_cents, 5000);
    check_int("and the counts", acc.active_count, 2);

    mrr_totals_merge(&acc, &page2);
    check_i64("second page adds", acc.mrr_cents, 8000);
    check_int("counts add", acc.active_count, 3);
    check_int("trials add", acc.trial_count, 1);
    check_str("currency carried", acc.currency, "usd");
    check_true("not mixed", !acc.mixed_currency);
}

/*
 * The case a per-page computation cannot see on its own.
 */
static void test_mixed_currency_across_pages(void)
{
    printf("currencies differing BETWEEN pages counts as mixed\n");

    mrr_totals_t acc = {0};
    mrr_totals_t usd = { .mrr_cents = 1000, .active_count = 1, .currency = "usd" };
    mrr_totals_t eur = { .mrr_cents = 2000, .active_count = 1, .currency = "eur" };

    mrr_totals_merge(&acc, &usd);
    check_true("one currency is not mixed", !acc.mixed_currency);

    mrr_totals_merge(&acc, &eur);
    check_true("a second currency makes it mixed", acc.mixed_currency);

    /* And it stays mixed even if later pages agree with the first. */
    mrr_totals_merge(&acc, &usd);
    check_true("mixed latches", acc.mixed_currency);
}

/*
 * has_tiered and mixed_currency are latches: once any page reports one, the
 * account has it, and a later clean page must not clear the flag.
 */
static void test_flags_latch(void)
{
    printf("has_tiered and mixed_currency latch across pages\n");

    mrr_totals_t acc = {0};
    mrr_totals_t tiered = {
        .mrr_cents = 1000, .has_tiered = true, .tiered_count = 2,
        .currency = "usd",
    };
    mrr_totals_t clean = { .mrr_cents = 1000, .currency = "usd" };

    mrr_totals_merge(&acc, &tiered);
    mrr_totals_merge(&acc, &clean);

    check_true("has_tiered stays set", acc.has_tiered);
    check_int("tiered_count accumulates", acc.tiered_count, 2);
}

/*
 * An empty page contributes nothing and must not clobber what came before --
 * in particular it must not blank the currency.
 */
static void test_empty_page_is_harmless(void)
{
    printf("an empty page changes nothing\n");

    mrr_totals_t acc = {0};
    mrr_totals_t real = { .mrr_cents = 4200, .active_count = 2, .currency = "usd" };
    mrr_totals_t empty = {0};

    mrr_totals_merge(&acc, &real);
    mrr_totals_merge(&acc, &empty);

    check_i64("total unchanged", acc.mrr_cents, 4200);
    check_int("count unchanged", acc.active_count, 2);
    check_str("currency not blanked", acc.currency, "usd");
    check_true("not spuriously mixed", !acc.mixed_currency);
}

/*
 * Customer-mix quality: are the customers being won worth more than the ones
 * being lost?
 *
 * This is the one question no other screen answers. MRR says revenue grew,
 * PAID SUBS says the count grew, NET 30D says the money grew -- none of them
 * say whether the mix is improving. The comparison is two averages, so it is
 * only meaningful with enough customers on both sides.
 */
static void test_mix_quality(void)
{
    printf("customer mix quality\n");

    /* 10 joined worth $354, 7 left worth $175. */
    check_true("enough on both sides to compare",
               mrr_mix_comparable(10, 7));
    check_i64("ARPU of those joining", mrr_arpu_cents(35400, 10), 3540);
    check_i64("ARPU of those leaving", mrr_arpu_cents(17500, 7), 2500);

    /*
     * The gate. Below the minimum on EITHER side the comparison is two
     * averages over a handful of customers, where one unusual signup moves
     * the verdict by dollars. The device shows the plain average instead.
     */
    check_true("too few joining is not comparable",
               !mrr_mix_comparable(MRR_MIX_MIN - 1, 7));
    check_true("too few leaving is not comparable",
               !mrr_mix_comparable(10, MRR_MIX_MIN - 1));
    check_true("exactly the minimum on both sides is comparable",
               mrr_mix_comparable(MRR_MIX_MIN, MRR_MIX_MIN));

    /* A month with no churn cannot be compared either: there is no "lost"
     * average to compare against, and claiming improvement would be a
     * conclusion drawn from an empty set. */
    check_true("no churn means no comparison", !mrr_mix_comparable(10, 0));
    check_true("no signups means no comparison", !mrr_mix_comparable(0, 7));
}

int main(void)
{
    printf("MRR computation tests (spec 7.2)\n\n");

    test_interval_parsing();
    test_monthly_conversion();
    test_non_recurring_skipped();
    test_tiered_flagged_not_guessed();
    test_unknown_interval_skipped();
    test_trials_excluded();
    test_mixed_currency_flagged();
    test_single_currency_not_flagged();
    test_multi_item_subscription();
    test_mixed_realistic_account();
    test_empty_account();
    test_large_account_no_overflow();
    test_arr();
    test_arpu();

    test_totals_merge_across_pages();
    test_mixed_currency_across_pages();
    test_flags_latch();
    test_empty_page_is_harmless();

    test_mix_quality();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
