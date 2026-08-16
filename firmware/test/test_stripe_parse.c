/*
 * Host tests for parsing Stripe subscription responses.
 *
 *   cd firmware/test && make && ./test_stripe_parse
 *
 * These use realistic response shapes rather than minimal ones, because the
 * failures worth catching are field-name and nesting assumptions -- the things
 * that break when Stripe changes a response, and that a hand-simplified
 * fixture would hide.
 */
#include <stdio.h>
#include <string.h>

#include "../main/stripe_parse.h"

static int failures = 0;
static int checks = 0;

static void check_int(const char *what, int got, int want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void check_i64(const char *what, int64_t got, int64_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %lld, want %lld\n", what,
               (long long)got, (long long)want);
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

/* A realistic single active monthly subscription. */
static const char JSON_ONE_ACTIVE[] =
"{\"object\":\"list\",\"has_more\":false,\"data\":["
"{\"id\":\"sub_1M\",\"object\":\"subscription\",\"status\":\"active\","
"\"discount\":null,"
"\"items\":{\"object\":\"list\",\"data\":["
"{\"id\":\"si_1\",\"object\":\"subscription_item\",\"quantity\":1,"
"\"price\":{\"id\":\"price_1\",\"object\":\"price\",\"active\":true,"
"\"billing_scheme\":\"per_unit\",\"currency\":\"usd\",\"unit_amount\":2900,"
"\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}"
"]}}"
"]}";

static void test_single_active(void)
{
    printf("a single active monthly subscription\n");

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(JSON_ONE_ACTIVE, &out));
    check_int("one subscription", out.sub_count, 1);
    check_false("not trialing", out.subs[0].trialing);
    check_int("one item", out.subs[0].item_count, 1);
    check_i64("unit amount", out.subs[0].items[0].unit_amount, 2900);
    check_i64("quantity", out.subs[0].items[0].quantity, 1);
    check_int("interval", out.subs[0].items[0].interval, MRR_INTERVAL_MONTH);
    check_true("recurring", out.subs[0].items[0].recurring);
    check_false("not tiered", out.subs[0].items[0].tiered);
    check_false("no more pages", out.has_more);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("computed MRR", t.mrr_cents, 2900);
}

/*
 * Statuses other than active/trialing must be dropped. A canceled
 * subscription still appears in list responses and would inflate MRR.
 */
static void test_filters_by_status(void)
{
    printf("only active and trialing are kept (spec 7.2)\n");

    static const char json[] =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    "{\"status\":\"active\",\"discount\":null,\"items\":{\"data\":["
      "{\"quantity\":1,\"price\":{\"billing_scheme\":\"per_unit\","
      "\"currency\":\"usd\",\"unit_amount\":1000,"
      "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
    "{\"status\":\"canceled\",\"discount\":null,\"items\":{\"data\":["
      "{\"quantity\":1,\"price\":{\"billing_scheme\":\"per_unit\","
      "\"currency\":\"usd\",\"unit_amount\":9999,"
      "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
    "{\"status\":\"past_due\",\"discount\":null,\"items\":{\"data\":["
      "{\"quantity\":1,\"price\":{\"billing_scheme\":\"per_unit\","
      "\"currency\":\"usd\",\"unit_amount\":8888,"
      "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
    "{\"status\":\"trialing\",\"discount\":null,\"items\":{\"data\":["
      "{\"quantity\":1,\"price\":{\"billing_scheme\":\"per_unit\","
      "\"currency\":\"usd\",\"unit_amount\":2000,"
      "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}"
    "]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_int("canceled and past_due dropped", out.sub_count, 2);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("only the active one counts", t.mrr_cents, 1000);
    check_int("trial counted", t.trial_count, 1);
}

static void test_discount_percent(void)
{
    printf("percent_off discounts are read\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\","
    "\"discount\":{\"coupon\":{\"percent_off\":50.0,\"amount_off\":null}},"
    "\"items\":{\"data\":[{\"quantity\":1,"
    "\"price\":{\"billing_scheme\":\"per_unit\",\"currency\":\"usd\","
    "\"unit_amount\":10000,"
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_true("discount present", out.subs[0].discount.present);
    check_int("50 percent", out.subs[0].discount.percent_off_x100, 5000);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("half of $100", t.mrr_cents, 5000);
}

/* Fractional percentages must survive: 33.33% is a real coupon value. */
static void test_discount_fractional_percent(void)
{
    printf("fractional percent_off survives\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\","
    "\"discount\":{\"coupon\":{\"percent_off\":33.33,\"amount_off\":null}},"
    "\"items\":{\"data\":[{\"quantity\":1,"
    "\"price\":{\"billing_scheme\":\"per_unit\",\"currency\":\"usd\","
    "\"unit_amount\":10000,"
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_int("33.33 percent", out.subs[0].discount.percent_off_x100, 3333);
}

static void test_discount_amount(void)
{
    printf("amount_off discounts are read\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\","
    "\"discount\":{\"coupon\":{\"percent_off\":null,\"amount_off\":1500}},"
    "\"items\":{\"data\":[{\"quantity\":1,"
    "\"price\":{\"billing_scheme\":\"per_unit\",\"currency\":\"usd\","
    "\"unit_amount\":10000,"
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_i64("$15 off", out.subs[0].discount.amount_off, 1500);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("$100 minus $15", t.mrr_cents, 8500);
}

static void test_tiered_detected(void)
{
    printf("tiered billing_scheme is detected (spec 7.2 step 3)\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\",\"discount\":null,"
    "\"items\":{\"data\":[{\"quantity\":1,"
    "\"price\":{\"billing_scheme\":\"tiered\",\"currency\":\"usd\","
    "\"unit_amount\":null,"
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_true("tiered flagged", out.subs[0].items[0].tiered);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("contributes nothing", t.mrr_cents, 0);
    check_true("surfaced", t.has_tiered);
}

/* A one-time price has no `recurring` object at all. */
static void test_non_recurring_price(void)
{
    printf("prices with no recurring object are one-time\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\",\"discount\":null,"
    "\"items\":{\"data\":[{\"quantity\":1,"
    "\"price\":{\"billing_scheme\":\"per_unit\",\"currency\":\"usd\","
    "\"unit_amount\":50000,\"recurring\":null}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_false("not recurring", out.subs[0].items[0].recurring);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("contributes nothing", t.mrr_cents, 0);
}

static void test_has_more(void)
{
    printf("has_more is surfaced for pagination\n");

    static const char json[] = "{\"has_more\":true,\"data\":[]}";
    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_true("more pages", out.has_more);
}

/*
 * Malformed input must fail cleanly rather than crash. A truncated body is a
 * realistic outcome of a dropped connection.
 */
static void test_malformed_input(void)
{
    printf("malformed input fails cleanly\n");

    stripe_subs_t out;
    check_false("empty string", stripe_parse_subscriptions("", &out));
    check_false("not json", stripe_parse_subscriptions("<html>oops</html>", &out));
    check_false("truncated", stripe_parse_subscriptions("{\"data\":[{\"stat", &out));
    check_false("no data array", stripe_parse_subscriptions("{\"object\":\"list\"}", &out));
    check_false("data not an array",
                stripe_parse_subscriptions("{\"data\":\"nope\"}", &out));
    check_false("NULL json", stripe_parse_subscriptions(NULL, &out));

    /* A Stripe error response is valid JSON but has no data array. */
    check_false("stripe error body",
                stripe_parse_subscriptions(
                    "{\"error\":{\"type\":\"invalid_request_error\"}}", &out));
}

/* Missing optional fields must default sanely rather than read garbage. */
static void test_missing_fields(void)
{
    printf("missing optional fields default sanely\n");

    static const char json[] =
    "{\"data\":[{\"status\":\"active\","
    "\"items\":{\"data\":[{"
    "\"price\":{\"currency\":\"usd\",\"unit_amount\":2900,"
    "\"recurring\":{\"interval\":\"month\"}}}]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    /* Absent quantity means 1, absent interval_count means 1, absent discount
     * means none. */
    check_i64("quantity defaults to 1", out.subs[0].items[0].quantity, 1);
    check_int("interval_count defaults to 1",
              out.subs[0].items[0].interval_count, 1);
    check_false("no discount", out.subs[0].discount.present);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("computes", t.mrr_cents, 2900);
}

static void test_empty_account(void)
{
    printf("an account with no subscriptions\n");

    static const char json[] = "{\"object\":\"list\",\"has_more\":false,\"data\":[]}";
    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(json, &out));
    check_int("no subscriptions", out.sub_count, 0);

    const mrr_totals_t t = mrr_compute(out.subs, out.sub_count);
    check_i64("zero MRR", t.mrr_cents, 0);
}

int main(void)
{
    printf("Stripe response parsing tests (spec 7.1, 7.2)\n\n");

    test_single_active();
    test_filters_by_status();
    test_discount_percent();
    test_discount_fractional_percent();
    test_discount_amount();
    test_tiered_detected();
    test_non_recurring_price();
    test_has_more();
    test_malformed_input();
    test_missing_fields();
    test_empty_account();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
