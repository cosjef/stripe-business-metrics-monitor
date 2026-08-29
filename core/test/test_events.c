/*
 * Host tests for event classification and today's deltas (spec 7.3, 7.4).
 *
 *   cd firmware/test && make && ./test_events
 *
 * The timezone handling here is the part spec 7.4 step 4 warns about: Stripe
 * returns UTC, and local midnight is not UTC midnight. Get it wrong and
 * "today" resets mid-afternoon.
 */
#include <stdio.h>
#include <string.h>

#include "../include/events.h"

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

static void test_event_classification(void)
{
    printf("event type classification\n");

    check_int("created", event_kind_from_type("customer.subscription.created"),
              EVENT_SUB_CREATED);
    check_int("updated", event_kind_from_type("customer.subscription.updated"),
              EVENT_SUB_UPDATED);
    check_int("deleted", event_kind_from_type("customer.subscription.deleted"),
              EVENT_SUB_DELETED);
    check_int("invoice paid", event_kind_from_type("invoice.payment_succeeded"),
              EVENT_INVOICE_PAID);
    check_int("trial ending",
              event_kind_from_type("customer.subscription.trial_will_end"),
              EVENT_TRIAL_ENDING);

    /* Stripe emits well over a hundred event types; everything we do not act
     * on must classify as OTHER rather than being mistaken for one we do. */
    check_int("unrelated", event_kind_from_type("charge.refunded"), EVENT_OTHER);
    check_int("payout", event_kind_from_type("payout.paid"), EVENT_OTHER);
    check_int("NULL", event_kind_from_type(NULL), EVENT_OTHER);
    check_int("empty", event_kind_from_type(""), EVENT_OTHER);

    /* A prefix match would misclassify this as a subscription event. */
    check_int("similar prefix",
              event_kind_from_type("customer.subscription.pending_update_applied"),
              EVENT_OTHER);
}

/*
 * Spec 7.4 step 4. US Eastern is UTC-5 (EST) or UTC-4 (EDT).
 */
static void test_local_day_start(void)
{
    printf("local day start, not UTC midnight (spec 7.4 step 4)\n");

    /* 2026-08-15 21:00:00 UTC = 17:00 EDT (UTC-4), same day locally.
     * Local midnight was 2026-08-15 04:00:00 UTC. */
    const int64_t now = 1786000800;   /* 2026-08-15 22:00:00 UTC */
    const int32_t edt = -4 * 3600;

    const int64_t start = local_day_start_utc(now, edt);

    /* The start must be before now, and within the last 24 hours. */
    check_true("day start is in the past", start < now);
    check_true("within 24 hours", now - start < 24 * 3600);

    /* And it must land exactly on a local midnight: (start + offset) should be
     * a multiple of 86400. */
    check_i64("lands on local midnight", (start + edt) % 86400, 0);
}

static void test_day_start_across_utc_midnight(void)
{
    printf("the UTC-midnight trap\n");

    /*
     * 2026-08-15 02:00:00 UTC is 22:00 EDT on the 14th -- still "yesterday"
     * locally. A naive UTC-midnight calculation would have already rolled over
     * and reported an empty "today" for the whole evening, which is exactly
     * the failure spec 7.4 step 4 describes.
     */
    const int64_t now = 1786276800 - 79200;  /* 2026-08-15 02:00 UTC approx */
    const int32_t edt = -4 * 3600;

    const int64_t start = local_day_start_utc(now, edt);

    check_true("day start precedes now", start <= now);
    check_true("more than 2 hours of the local day have passed",
               now - start > 2 * 3600);
    check_i64("still a local midnight", (start + edt) % 86400, 0);
}

static void test_utc_offset_zero(void)
{
    printf("UTC offset zero behaves like plain UTC\n");

    const int64_t now = 1786000800;
    const int64_t start = local_day_start_utc(now, 0);
    check_i64("aligned to UTC midnight", start % 86400, 0);
}

/* ---- today's totals ---- */

static stripe_event_t ev(event_kind_t k, int64_t created, int64_t amount)
{
    stripe_event_t e = { .kind = k, .created = created, .amount_cents = amount };
    return e;
}

static void test_counts_todays_events(void)
{
    printf("today's events are counted\n");

    const int64_t day = 1786000000;

    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day + 100, 0),
        ev(EVENT_SUB_CREATED, day + 200, 0),
        ev(EVENT_INVOICE_PAID, day + 300, 2900),
        ev(EVENT_SUB_DELETED, day + 400, 0),
    };

    const event_totals_t t = events_summarize(events, 4, day);

    check_int("two new paid", t.new_paid, 2);
    check_int("one churned", t.churned, 1);
    check_i64("revenue", t.revenue_cents, 2900);
}

/*
 * Yesterday's events must not count toward today, or "new paid today" becomes
 * "new paid recently" and the number never resets.
 */
static void test_excludes_earlier_days(void)
{
    printf("events before today are excluded\n");

    const int64_t day = 1786000000;

    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day - 3600, 0),     /* yesterday */
        ev(EVENT_SUB_CREATED, day + 100, 0),      /* today */
        ev(EVENT_INVOICE_PAID, day - 7200, 9999), /* yesterday */
    };

    const event_totals_t t = events_summarize(events, 3, day);

    check_int("only today's creation", t.new_paid, 1);
    check_i64("yesterday's revenue excluded", t.revenue_cents, 0);
}

/*
 * The Last Event screen is the heartbeat -- it confirms the device is live
 * (spec 6.1). So it tracks the most recent event regardless of day.
 */
static void test_last_event_ignores_day_boundary(void)
{
    printf("last event is tracked across days (spec 6.1 heartbeat)\n");

    const int64_t day = 1786000000;

    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day - 86400, 0),  /* yesterday, but most recent */
    };

    const event_totals_t t = events_summarize(events, 1, day);

    check_true("last event recorded", t.have_last);
    check_int("kind", t.last_kind, EVENT_SUB_CREATED);
    check_int("but not counted toward today", t.new_paid, 0);
}

static void test_last_event_is_the_newest(void)
{
    printf("last event is the newest, whatever the order\n");

    const int64_t day = 1786000000;

    /* Stripe returns newest first, but do not depend on ordering. */
    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day + 100, 0),
        ev(EVENT_INVOICE_PAID, day + 500, 2900),
        ev(EVENT_SUB_DELETED, day + 300, 0),
    };

    const event_totals_t t = events_summarize(events, 3, day);

    check_int("newest kind", t.last_kind, EVENT_INVOICE_PAID);
    check_i64("newest timestamp", t.last_created, day + 500);
    check_i64("its amount", t.last_amount_cents, 2900);
}

static void test_ignores_uninteresting_events(void)
{
    printf("uninteresting events do not become the heartbeat\n");

    const int64_t day = 1786000000;

    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day + 100, 0),
        ev(EVENT_OTHER, day + 999, 0),   /* newest, but not shown */
    };

    const event_totals_t t = events_summarize(events, 2, day);

    check_int("last is the subscription event", t.last_kind, EVENT_SUB_CREATED);
    check_i64("not the ignored one", t.last_created, day + 100);
}

static void test_empty(void)
{
    printf("no events\n");

    const event_totals_t t = events_summarize(NULL, 0, 1786000000);
    check_int("no new paid", t.new_paid, 0);
    check_int("no churn", t.churned, 0);
    check_i64("no revenue", t.revenue_cents, 0);
    check_true("no last event", !t.have_last);
}

/* ---- display strings ---- */

static void test_labels(void)
{
    printf("event labels\n");

    check_str("created", event_kind_label(EVENT_SUB_CREATED), "new paid");
    check_str("deleted", event_kind_label(EVENT_SUB_DELETED), "churned");
    check_str("invoice", event_kind_label(EVENT_INVOICE_PAID), "payment");
    check_str("trial", event_kind_label(EVENT_TRIAL_ENDING), "trial ending");

    /* Every label must fit the 22px subtitle line. */
    const event_kind_t kinds[] = {
        EVENT_SUB_CREATED, EVENT_SUB_UPDATED, EVENT_SUB_DELETED,
        EVENT_INVOICE_PAID, EVENT_TRIAL_ENDING, EVENT_OTHER,
    };
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        const char *l = event_kind_label(kinds[i]);
        char what[64];
        snprintf(what, sizeof(what), "\"%s\" is short enough", l);
        check_true(what, l != NULL && strlen(l) <= 14);
    }
}

static void test_age_strings(void)
{
    printf("relative age for the last-event subtitle\n");

    const int64_t now = 1786000000;
    char b[16];

    event_format_age(now, now, b, sizeof(b));            check_str("just now", b, "now");
    event_format_age(now - 120, now, b, sizeof(b));      check_str("2 minutes", b, "2m");
    event_format_age(now - 3600, now, b, sizeof(b));     check_str("1 hour", b, "1h");
    event_format_age(now - 7200, now, b, sizeof(b));     check_str("2 hours", b, "2h");
    event_format_age(now - 86400, now, b, sizeof(b));    check_str("1 day", b, "1d");
    event_format_age(now - 5 * 86400, now, b, sizeof(b));check_str("5 days", b, "5d");

    /* A clock that has not synced yet can make an event look future-dated;
     * that must not render as a negative age. */
    event_format_age(now + 600, now, b, sizeof(b));
    check_str("future events read as now", b, "now");
}

/*
 * A rolling 30-day cancellation count. Unlike a daily churn figure -- which
 * reads zero most days and so only surfaces on bad news -- this is always
 * meaningful and can be watched as a trend.
 */
static void test_30_day_cancellations(void)
{
    printf("rolling 30-day cancellations\n");

    const int64_t day = 1786000000;
    const int64_t window = day - (30 * 86400);

    const stripe_event_t events[] = {
        ev(EVENT_SUB_DELETED, day + 100, 0),          /* today */
        ev(EVENT_SUB_DELETED, day - 5 * 86400, 0),    /* 5 days ago */
        ev(EVENT_SUB_DELETED, day - 29 * 86400, 0),   /* 29 days ago */
        ev(EVENT_SUB_DELETED, day - 31 * 86400, 0),   /* outside the window */
        ev(EVENT_SUB_CREATED, day - 2 * 86400, 0),    /* not a cancellation */
    };

    const event_totals_t t =
        events_summarize_window(events, 5, day, window);

    check_int("three within 30 days", t.churned_30d, 3);
    check_int("today's churn is separate", t.churned, 1);
    check_int("creations not counted as churn", t.new_paid, 0);
}

static void test_30_day_window_boundary(void)
{
    printf("the 30-day boundary\n");

    const int64_t day = 1786000000;
    const int64_t window = day - (30 * 86400);

    const stripe_event_t just_inside[] = { ev(EVENT_SUB_DELETED, window, 0) };
    check_int("exactly at the boundary counts",
              events_summarize_window(just_inside, 1, day, window).churned_30d, 1);

    const stripe_event_t just_outside[] = { ev(EVENT_SUB_DELETED, window - 1, 0) };
    check_int("one second before does not",
              events_summarize_window(just_outside, 1, day, window).churned_30d, 0);
}

static void test_no_cancellations(void)
{
    printf("an account with no cancellations\n");

    const int64_t day = 1786000000;
    const stripe_event_t events[] = {
        ev(EVENT_SUB_CREATED, day + 100, 0),
        ev(EVENT_INVOICE_PAID, day + 200, 2900),
    };

    const event_totals_t t =
        events_summarize_window(events, 2, day, day - 30 * 86400);

    check_int("zero, which is worth showing", t.churned_30d, 0);
}

/* The original entry point must keep working, counting only today. */
static void test_summarize_defaults_to_today(void)
{
    printf("events_summarize keeps its today-only behaviour\n");

    const int64_t day = 1786000000;
    const stripe_event_t events[] = {
        ev(EVENT_SUB_DELETED, day + 100, 0),
        ev(EVENT_SUB_DELETED, day - 5 * 86400, 0),
    };

    const event_totals_t t = events_summarize(events, 2, day);
    check_int("today only", t.churned, 1);
}

int main(void)
{
    printf("event classification tests (spec 7.3, 7.4)\n\n");

    test_event_classification();
    test_local_day_start();
    test_day_start_across_utc_midnight();
    test_utc_offset_zero();
    test_counts_todays_events();
    test_excludes_earlier_days();
    test_last_event_ignores_day_boundary();
    test_last_event_is_the_newest();
    test_ignores_uninteresting_events();
    test_empty();
    test_labels();
    test_age_strings();
    test_30_day_cancellations();
    test_30_day_window_boundary();
    test_no_cancellations();
    test_summarize_defaults_to_today();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
