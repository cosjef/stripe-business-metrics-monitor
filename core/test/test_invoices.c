/*
 * Streaming scanner for Stripe's invoice list.
 *
 *   cd firmware/test && make && ./test_invoices
 *
 * Separate from jsonstream.c rather than folded into it. That scanner walks
 * subscriptions and knows about prices, items, intervals and statuses;
 * invoices share none of that, and teaching one scanner both shapes would
 * mean every field lookup asking which document it is in. This is thirty
 * lines of state against that.
 *
 * What matters here is which failures are RECOVERABLE. A void invoice has
 * been cancelled -- the money is not coming and cannot be chased -- so
 * counting it would overstate what is at stake. On the live account that is
 * the difference between $29.00 and $58.13.
 */
#include <stdio.h>
#include <string.h>

#include "../include/invoices.h"

static int checks, failures;

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
    check_i64(what, got, want);
}

/*
 * Two failing invoices: one open and retrying, one void. Mirrors the live
 * account, where exactly this pair exists and only the first is collectable.
 */
static const char *PAGE =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    "{\"id\":\"in_open\",\"status\":\"open\",\"paid\":false,"
    "\"amount_due\":2900,\"attempt_count\":1,"
    "\"next_payment_attempt\":1788544039},"
    "{\"id\":\"in_void\",\"status\":\"void\",\"paid\":false,"
    "\"amount_due\":2913,\"attempt_count\":1,"
    "\"next_payment_attempt\":null},"
    "{\"id\":\"in_paid\",\"status\":\"paid\",\"paid\":true,"
    "\"amount_due\":4900,\"attempt_count\":1}"
    "]}";

static void test_only_recoverable_failures(void)
{
    printf("only open, attempted, unpaid invoices count\n");

    invoices_t iv;
    invoices_init(&iv);
    invoices_feed(&iv, PAGE, strlen(PAGE));
    invoices_finish(&iv);

    check_int("one recoverable failure", iv.failed_count, 1);
    check_i64("its amount", iv.failed_cents, 2900);

    /* The void one is cancelled: the money is not collectable and must not
     * inflate the figure. */
    check_i64("void invoices are excluded", iv.failed_cents, 2900);

    /* A paid invoice with an earlier failed attempt is not a failure now. */
    check_int("paid invoices are excluded", iv.failed_count, 1);

    check_i64("soonest retry", iv.next_retry, 1788544039);
}

static void test_byte_at_a_time(void)
{
    printf("survives byte-at-a-time feeding\n");

    invoices_t iv;
    invoices_init(&iv);
    for (const char *p = PAGE; *p; p++) {
        invoices_feed(&iv, p, 1);
    }
    invoices_finish(&iv);

    check_int("same count", iv.failed_count, 1);
    check_i64("same amount", iv.failed_cents, 2900);
    check_i64("same retry", iv.next_retry, 1788544039);
}

static void test_nothing_failing(void)
{
    printf("an account with nothing failing\n");

    const char *ok =
        "{\"object\":\"list\",\"has_more\":false,\"data\":["
        "{\"id\":\"in_1\",\"status\":\"paid\",\"paid\":true,"
        "\"amount_due\":2900,\"attempt_count\":1}"
        "]}";

    invoices_t iv;
    invoices_init(&iv);
    invoices_feed(&iv, ok, strlen(ok));
    invoices_finish(&iv);

    check_int("no failures", iv.failed_count, 0);
    check_i64("nothing owed", iv.failed_cents, 0);
    check_i64("no retry", iv.next_retry, 0);
}

/*
 * An open invoice that has never been attempted is not a failure -- it is a
 * bill that has not come due. Counting it would report a problem where none
 * exists.
 */
static void test_unattempted_is_not_failure(void)
{
    printf("an unattempted open invoice is not a failure\n");

    const char *pending =
        "{\"object\":\"list\",\"has_more\":false,\"data\":["
        "{\"id\":\"in_new\",\"status\":\"open\",\"paid\":false,"
        "\"amount_due\":2900,\"attempt_count\":0}"
        "]}";

    invoices_t iv;
    invoices_init(&iv);
    invoices_feed(&iv, pending, strlen(pending));
    invoices_finish(&iv);

    check_int("not counted as failing", iv.failed_count, 0);
}

/*
 * A realistic invoice, with the nested objects Stripe actually sends.
 *
 * automatic_tax carries a "status" of its own, and it appears BEFORE the
 * invoice's own status in the real response. A scanner that takes the first
 * "status" it sees reads null here and matches nothing -- which is exactly
 * what happened: the device reported zero failures against a live $29.00.
 */
static void test_nested_objects_do_not_shadow(void)
{
    printf("nested objects do not shadow the invoice's own fields\n");

    const char *nested =
        "{\"object\":\"list\",\"has_more\":false,\"data\":["
        "{\"id\":\"in_1\","
        "\"automatic_tax\":{\"enabled\":false,\"status\":null},"
        "\"amount_due\":2900,"
        "\"payment_settings\":{\"payment_method_options\":null},"
        "\"attempt_count\":1,"
        "\"lines\":{\"data\":[{\"amount\":2900,\"paid\":true}]},"
        "\"status\":\"open\",\"paid\":false,"
        "\"next_payment_attempt\":1788544039}"
        "]}";

    invoices_t iv;
    invoices_init(&iv);
    invoices_feed(&iv, nested, strlen(nested));
    invoices_finish(&iv);

    check_int("the invoice's own status wins", iv.failed_count, 1);
    check_i64("and its own amount", iv.failed_cents, 2900);

    /* The nested line item says paid:true; the invoice says paid:false. The
     * invoice's own value is the one that decides. */
    check_i64("nested paid does not shadow the invoice's", iv.failed_cents, 2900);
}

int main(void)
{
    test_only_recoverable_failures();
    test_byte_at_a_time();
    test_nothing_failing();
    test_unattempted_is_not_failure();
    test_nested_objects_do_not_shadow();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
