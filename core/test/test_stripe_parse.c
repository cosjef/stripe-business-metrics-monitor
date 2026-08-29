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

#include "../include/stripe_parse.h"

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

/*
 * The pagination cursor.
 *
 * Stripe pages with starting_after=<last object id>, so the fetch loop needs
 * the id of the final subscription in each page. The parser previously kept
 * only the eight fields MRR needs and discarded ids entirely, which made
 * pagination impossible -- and pagination is not optional on the C6, where the
 * heap allows only two subscriptions per request.
 *
 * The rules that matter: the cursor must be the LAST id in the page (Stripe
 * orders newest-first and starting_after means "after this one"), it must be
 * empty when there is nothing to page from, and it must never be a truncated
 * id -- a short id would silently skip subscriptions and under-report MRR.
 */
static void test_pagination_cursor(void)
{
    printf("the last subscription's id is captured for starting_after\n");

    stripe_subs_t out;

    /* Three subscriptions: the cursor must be the third. */
    const char *body =
        "{\"object\":\"list\",\"has_more\":true,\"data\":["
        "{\"id\":\"sub_aaa\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"price\":{\"unit_amount\":1000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}},"
        "\"quantity\":1}]}},"
        "{\"id\":\"sub_bbb\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"price\":{\"unit_amount\":2000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}},"
        "\"quantity\":1}]}},"
        "{\"id\":\"sub_ccc\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"price\":{\"unit_amount\":3000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}},"
        "\"quantity\":1}]}}]}";

    check_true("parses", stripe_parse_subscriptions(body, &out));
    check_int("three subscriptions", out.sub_count, 3);
    check_str("cursor is the LAST id, not the first", out.last_id, "sub_ccc");
    check_true("has_more surfaced", out.has_more);

    /* An empty page leaves no cursor: paging from "" would restart the list. */
    stripe_subs_t empty;
    check_true("empty list parses",
               stripe_parse_subscriptions(
                   "{\"object\":\"list\",\"has_more\":false,\"data\":[]}", &empty));
    check_int("no subscriptions", empty.sub_count, 0);
    check_str("and no cursor", empty.last_id, "");
}

/*
 * A subscription that is skipped (cancelled, incomplete) still advances the
 * cursor.
 *
 * The parser drops statuses that do not count toward MRR. If the last object
 * in a page is one of those, taking the cursor from the last STORED
 * subscription rather than the last SEEN one would ask Stripe to resume from
 * an earlier point, and the same page would come back forever.
 */
static void test_cursor_advances_past_skipped(void)
{
    printf("skipped subscriptions still advance the cursor\n");

    const char *body =
        "{\"object\":\"list\",\"has_more\":true,\"data\":["
        "{\"id\":\"sub_kept\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"price\":{\"unit_amount\":1000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}},"
        "\"quantity\":1}]}},"
        "{\"id\":\"sub_cancelled\",\"status\":\"canceled\",\"items\":{\"data\":[]}}]}";

    stripe_subs_t out;
    check_true("parses", stripe_parse_subscriptions(body, &out));
    check_int("only the active one counts", out.sub_count, 1);
    check_str("but the cursor is the last SEEN id",
              out.last_id, "sub_cancelled");
}

/*
 * Ids must never be truncated. Stripe ids are short today, but a silently
 * clipped cursor would ask to resume from an id that does not exist, and
 * Stripe would return an error or the wrong page -- under-reporting MRR with
 * no visible symptom.
 */
static void test_long_id_is_not_silently_truncated(void)
{
    printf("an over-long id is rejected rather than clipped\n");

    char body[1024];
    char longid[STRIPE_ID_LEN + 40];
    memset(longid, 'x', sizeof(longid) - 1);
    longid[sizeof(longid) - 1] = '\0';
    memcpy(longid, "sub_", 4);

    snprintf(body, sizeof(body),
        "{\"object\":\"list\",\"has_more\":true,\"data\":["
        "{\"id\":\"%s\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"price\":{\"unit_amount\":1000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}},"
        "\"quantity\":1}]}}]}", longid);

    stripe_subs_t out;
    stripe_parse_subscriptions(body, &out);

    /* Either it fits or the cursor is empty -- never a half id. */
    checks++;
    if (out.last_id[0] != '\0' && strcmp(out.last_id, longid) != 0) {
        failures++;
        printf("  FAIL cursor was truncated to \"%s\"\n", out.last_id);
    }
}

int main(void)
{
    printf("Stripe response parsing tests (spec 7.1, 7.2)\n\n");

    test_single_active();
    test_filters_by_status();
    test_tiered_detected();
    test_non_recurring_price();
    test_has_more();
    test_pagination_cursor();
    test_cursor_advances_past_skipped();
    test_long_id_is_not_silently_truncated();
    test_malformed_input();
    test_missing_fields();
    test_empty_account();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
