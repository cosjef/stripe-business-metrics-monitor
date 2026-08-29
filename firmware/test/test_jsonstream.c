/*
 * Streaming JSON scanner: fold subscriptions into totals as bytes arrive.
 *
 *   cd firmware/test && make && ./test_jsonstream
 *
 * Why this exists. The device displays about 60 bytes of state and transfers
 * ~180KB to compute it -- 3,000:1. On the S3 that waste was free, because
 * PSRAM held the whole response. The C6 has 512KB of SRAM total and a ~15KB
 * largest contiguous block once mbedTLS holds its session, so buffer-then-parse
 * cannot work at any page size: at ~6KB per subscription it forces one
 * subscription per request and forty-plus round trips.
 *
 * Nothing requires reassembling the body. esp_http_client already delivers it
 * in ~512-byte chunks; concatenating them was a choice inherited from the S3.
 * Folding each subscription into a running total as it completes needs ~3KB
 * resident, which restores limit=100 and one request.
 *
 * This is deliberately NOT a general JSON parser. It walks data[] and pulls
 * eight fields per element. Anything it does not recognise, it skips.
 *
 * The failure modes worth guarding are all about chunk boundaries: a field
 * name split across two reads, a number split mid-digit, a string containing
 * braces. Those are what break hand-rolled streaming parsers, and they are
 * invisible in tests that feed the whole document at once.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../main/jsonstream.h"

static int failures = 0;
static int checks = 0;

static void check_i64(const char *what, int64_t got, int64_t want)
{
    checks++;
    if (got != want) {
        failures++;
        printf("  FAIL %s: got %lld, want %lld\n",
               what, (long long)got, (long long)want);
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

static void check_true(const char *what, bool cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/* A minimal but realistic two-subscription page. */
static const char *TWO_SUBS =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    "{\"id\":\"sub_a\",\"status\":\"active\",\"items\":{\"data\":["
    "{\"quantity\":1,\"price\":{\"unit_amount\":1000,\"currency\":\"usd\","
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
    "{\"id\":\"sub_b\",\"status\":\"active\",\"items\":{\"data\":["
    "{\"quantity\":2,\"price\":{\"unit_amount\":2500,\"currency\":\"usd\","
    "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}"
    "]}";

/* ---- the whole document at once ---- */

static void test_single_chunk(void)
{
    printf("a whole page in one chunk\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, TWO_SUBS, strlen(TWO_SUBS));
    jsonstream_finish(&js);

    check_i64("MRR is 1000 + 2*2500", jsonstream_totals(&js)->mrr_cents, 6000);
    check_int("two active", jsonstream_totals(&js)->active_count, 2);
    check_true("cursor is the last id",
               strcmp(jsonstream_last_id(&js), "sub_b") == 0);
    check_true("no more pages", !jsonstream_has_more(&js));
}

/* ---- chunk boundaries: the whole point ---- */

/*
 * Feed the same document one byte at a time. Every field name, every number
 * and every string is split at every possible position, so a parser that
 * assumes a token arrives whole cannot survive this.
 */
static void test_byte_at_a_time(void)
{
    printf("identical result when fed one byte at a time\n");

    jsonstream_t js;
    jsonstream_init(&js);
    for (const char *p = TWO_SUBS; *p; p++) {
        jsonstream_feed(&js, p, 1);
    }
    jsonstream_finish(&js);

    check_i64("same MRR", jsonstream_totals(&js)->mrr_cents, 6000);
    check_int("same count", jsonstream_totals(&js)->active_count, 2);
    check_true("same cursor", strcmp(jsonstream_last_id(&js), "sub_b") == 0);
}

/*
 * Every chunk size from 1 to the whole document. If any split produces a
 * different answer, the parser has a boundary bug -- and a single fixed chunk
 * size in a test would very likely miss it.
 */
static void test_every_chunk_size(void)
{
    printf("every chunk size gives the same answer\n");

    const size_t len = strlen(TWO_SUBS);
    bool all_match = true;

    for (size_t chunk = 1; chunk <= len; chunk++) {
        jsonstream_t js;
        jsonstream_init(&js);
        for (size_t off = 0; off < len; off += chunk) {
            const size_t n = (off + chunk > len) ? (len - off) : chunk;
            jsonstream_feed(&js, TWO_SUBS + off, n);
        }
        jsonstream_finish(&js);

        if (jsonstream_totals(&js)->mrr_cents != 6000 ||
            jsonstream_totals(&js)->active_count != 2) {
            all_match = false;
            printf("  FAIL chunk size %zu gave %lld cents, %d active\n",
                   chunk, (long long)jsonstream_totals(&js)->mrr_cents,
                   jsonstream_totals(&js)->active_count);
            break;
        }
    }
    check_true("all chunk sizes agree", all_match);
}

/* ---- what it must ignore ---- */

/*
 * Trials are counted, never summed -- a trial is not yet revenue. Cancelled
 * and incomplete are skipped entirely but must still advance the cursor.
 */
static void test_status_filtering(void)
{
    printf("statuses are filtered as mrr.c does\n");

    const char *mixed =
        "{\"object\":\"list\",\"has_more\":true,\"data\":["
        "{\"id\":\"sub_1\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"quantity\":1,\"price\":{\"unit_amount\":1000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
        "{\"id\":\"sub_2\",\"status\":\"trialing\",\"items\":{\"data\":["
        "{\"quantity\":1,\"price\":{\"unit_amount\":9999,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}},"
        "{\"id\":\"sub_3\",\"status\":\"canceled\",\"items\":{\"data\":[]}}"
        "]}";

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, mixed, strlen(mixed));
    jsonstream_finish(&js);

    check_i64("only the active one is summed",
              jsonstream_totals(&js)->mrr_cents, 1000);
    check_int("one active", jsonstream_totals(&js)->active_count, 1);
    check_int("one trial, counted not summed",
              jsonstream_totals(&js)->trial_count, 1);
    check_true("cursor advanced past the cancelled one",
               strcmp(jsonstream_last_id(&js), "sub_3") == 0);
    check_true("has_more seen", jsonstream_has_more(&js));
}

/*
 * Annual plans normalise to monthly (spec 7.2). A yearly price divided by 12
 * is the whole reason MRR is not just "sum the amounts".
 */
static void test_interval_normalisation(void)
{
    printf("annual plans normalise to a monthly figure\n");

    const char *annual =
        "{\"object\":\"list\",\"has_more\":false,\"data\":["
        "{\"id\":\"sub_y\",\"status\":\"active\",\"items\":{\"data\":["
        "{\"quantity\":1,\"price\":{\"unit_amount\":12000,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"year\",\"interval_count\":1}}}]}}"
        "]}";

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, annual, strlen(annual));
    jsonstream_finish(&js);

    check_i64("$120/year is $10/month",
              jsonstream_totals(&js)->mrr_cents, 1000);
}

/*
 * A string containing braces or brackets must not be mistaken for structure.
 * Customer-supplied metadata routinely contains both.
 */
static void test_braces_inside_strings(void)
{
    printf("braces inside strings do not confuse the scanner\n");

    const char *tricky =
        "{\"object\":\"list\",\"has_more\":false,\"data\":["
        "{\"id\":\"sub_x\",\"status\":\"active\","
        "\"description\":\"weird }{ [] \\\"quoted\\\" name\","
        "\"items\":{\"data\":["
        "{\"quantity\":1,\"price\":{\"unit_amount\":500,\"currency\":\"usd\","
        "\"recurring\":{\"interval\":\"month\",\"interval_count\":1}}}]}}"
        "]}";

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, tricky, strlen(tricky));
    jsonstream_finish(&js);

    check_i64("still 500 cents", jsonstream_totals(&js)->mrr_cents, 500);
    check_int("still one active", jsonstream_totals(&js)->active_count, 1);
}

/* ---- memory ---- */

/*
 * The claim this whole design rests on. If the scanner grows with the
 * document it is no better than what it replaces.
 */
static void test_state_is_bounded(void)
{
    printf("scanner state does not grow with the document\n");

    check_true("under 3KB", sizeof(jsonstream_t) < 3072);

    /* A large document must not change that. */
    jsonstream_t js;
    jsonstream_init(&js);
    for (int i = 0; i < 200; i++) {
        jsonstream_feed(&js, TWO_SUBS, strlen(TWO_SUBS));
    }
    jsonstream_finish(&js);

    check_true("still bounded after 200 documents",
               sizeof(jsonstream_t) < 3072);
}

/*
 * Subscriber flow: how many joined and how many left in the window.
 *
 * A count of active subscriptions looks identical whether the month added
 * three or added ten and lost seven. The flow is what the PAID SUBS card
 * draws, and it is also what SCREEN_CANCELLATIONS will need.
 *
 * Timestamps are Unix seconds. The window is passed in rather than read from
 * a clock, because a parser that calls time() cannot be tested: these fixtures
 * would pass today and fail tomorrow.
 */
#define DAY 86400
#define NOW 1756400000              /* a fixed "now" for the fixtures */
#define WINDOW_START (NOW - 30 * DAY)

static const char *FLOW_PAGE =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    /* joined inside the window, still active -> counts as new */
    "{\"id\":\"sub_new\",\"status\":\"active\",\"created\":1755000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":1000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* joined long before the window -> active, but not new */
    "{\"id\":\"sub_old\",\"status\":\"active\",\"created\":1700000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":2000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* ended inside the window -> churned, and NOT summed into MRR */
    "{\"id\":\"sub_gone\",\"status\":\"canceled\",\"created\":1700000000,"
    "\"ended_at\":1755500000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":3000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* ended long ago -> outside the window, counts for nothing */
    "{\"id\":\"sub_ancient\",\"status\":\"canceled\",\"created\":1600000000,"
    "\"ended_at\":1650000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":4000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}}"
    "]}";

static void test_flow_counts(void)
{
    printf("subscriber flow over a window\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_set_window(&js, WINDOW_START);
    jsonstream_feed(&js, FLOW_PAGE, strlen(FLOW_PAGE));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);

    check_int("two still active", t->active_count, 2);
    check_int("one joined in the window", t->new_count, 1);
    check_int("one left in the window", t->churned_count, 1);

    /* The cancelled subscriptions must not reach MRR, however recently they
     * ended -- churn is a count, not revenue. */
    check_i64("only active revenue is summed", t->mrr_cents, 3000);
}

/*
 * The same page fed one byte at a time.
 *
 * Timestamps are the longest numbers in the document, so they are the most
 * likely to be split across a chunk boundary -- exactly the failure this
 * parser exists to survive.
 */
static void test_flow_byte_at_a_time(void)
{
    printf("flow counts survive byte-at-a-time feeding\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_set_window(&js, WINDOW_START);
    for (const char *p = FLOW_PAGE; *p; p++) {
        jsonstream_feed(&js, p, 1);
    }
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);
    check_int("same new count", t->new_count, 1);
    check_int("same churned count", t->churned_count, 1);
    check_i64("same MRR", t->mrr_cents, 3000);
}

/*
 * With no window set the counts stay zero rather than counting everything.
 *
 * A caller without a synced clock has no honest window, and counting every
 * subscription ever created as "new this month" would be worse than showing
 * nothing.
 */
static void test_flow_without_window(void)
{
    printf("no window means no flow counts\n");

    jsonstream_t js;
    jsonstream_init(&js);
    /* deliberately no jsonstream_set_window */
    jsonstream_feed(&js, FLOW_PAGE, strlen(FLOW_PAGE));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);
    check_int("no new counted", t->new_count, 0);
    check_int("no churn counted", t->churned_count, 0);
    check_int("but actives are still counted", t->active_count, 2);
}

/*
 * At-risk revenue: subscriptions that have given notice but have not left.
 *
 * This is the only genuinely actionable figure the device can show. Everything
 * else reports what already happened; these subscriptions are still paying and
 * their departure date has not arrived. The parser has to separate them from
 * both the healthy actives (they still count toward MRR today) and the
 * already-churned (those are history).
 */
static const char *RISK_PAGE =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    /* giving notice: still active, still paying, but leaving */
    "{\"id\":\"sub_leaving\",\"status\":\"active\",\"created\":1700000000,"
    "\"cancel_at_period_end\":true,\"current_period_end\":1757000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":2900,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* a second one, leaving sooner -- the soonest date is what gets shown */
    "{\"id\":\"sub_leaving2\",\"status\":\"active\",\"created\":1700000000,"
    "\"cancel_at_period_end\":true,\"current_period_end\":1756600000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":1300,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* healthy: not leaving */
    "{\"id\":\"sub_ok\",\"status\":\"active\",\"created\":1700000000,"
    "\"current_period_end\":1757000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":4900,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}}"
    "]}";

static void test_at_risk(void)
{
    printf("subscriptions that have given notice\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, RISK_PAGE, strlen(RISK_PAGE));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);

    check_int("two are leaving", t->at_risk_count, 2);
    check_i64("their combined monthly value", t->at_risk_cents, 4200);

    /*
     * They are still active and still paying, so they must remain in both the
     * active count and MRR. Excluding them would understate today's revenue
     * for money that is still arriving.
     */
    check_int("all three still count as active", t->active_count, 3);
    check_i64("at-risk revenue is still in MRR", t->mrr_cents, 9100);

    /* The soonest departure is the one worth naming: it is the deadline. */
    check_i64("soonest period end", t->at_risk_soonest, 1756600000);
}

static void test_at_risk_byte_at_a_time(void)
{
    printf("at-risk survives byte-at-a-time feeding\n");

    jsonstream_t js;
    jsonstream_init(&js);
    for (const char *p = RISK_PAGE; *p; p++) {
        jsonstream_feed(&js, p, 1);
    }
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);
    check_int("same count", t->at_risk_count, 2);
    check_i64("same value", t->at_risk_cents, 4200);
    check_i64("same soonest", t->at_risk_soonest, 1756600000);
}

/* Nobody leaving: the figures stay zero rather than reporting a false alarm. */
static void test_no_one_at_risk(void)
{
    printf("nobody leaving means nothing at risk\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_feed(&js, TWO_SUBS, strlen(TWO_SUBS));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);
    check_int("none at risk", t->at_risk_count, 0);
    check_i64("nothing at risk", t->at_risk_cents, 0);
    check_i64("no departure date", t->at_risk_soonest, 0);
}

/*
 * Revenue gained and lost in the window, not just the head count.
 *
 * "+10 / -7" and "+$354 / -$175" are different facts: ten $13 signups do not
 * replace seven $49 cancellations, and a deck that only counts heads cannot
 * tell the difference. NET 30D needs the money.
 */
static void test_flow_revenue(void)
{
    printf("revenue gained and lost in the window\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_set_window(&js, WINDOW_START);
    jsonstream_feed(&js, FLOW_PAGE, strlen(FLOW_PAGE));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);

    /* sub_new joined inside the window at $10/mo. */
    check_i64("revenue gained", t->new_cents, 1000);

    /* sub_gone left inside the window; it was worth $30/mo. Its value is
     * counted as lost even though it never reached mrr_cents -- the point of
     * the figure is what the window cost, not what is running now. */
    check_i64("revenue lost", t->churned_cents, 3000);

    /* sub_ancient ended long before the window and must not appear. */
    check_true("out-of-window departures are excluded",
               t->churned_cents == 3000);
}

static void test_flow_revenue_byte_at_a_time(void)
{
    printf("flow revenue survives byte-at-a-time feeding\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_set_window(&js, WINDOW_START);
    for (const char *p = FLOW_PAGE; *p; p++) {
        jsonstream_feed(&js, p, 1);
    }
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);
    check_i64("same gained", t->new_cents, 1000);
    check_i64("same lost", t->churned_cents, 3000);
}

/*
 * Signups in the period BEFORE the window, for pace comparison.
 *
 * A signup count answers "how many" but not "is this speeding up or slowing
 * down", which is the question it always raises. That needs the previous
 * period counted too: ten this month against six last is a different fact
 * from ten against fourteen.
 *
 * The prior window is [window_start - span, window_start), so the two periods
 * are the same length and adjacent -- comparing a 30-day window against a
 * 45-day one would flatter or punish the current period for no reason.
 */
static const char *PACE_PAGE =
    "{\"object\":\"list\",\"has_more\":false,\"data\":["
    /* inside the current window */
    "{\"id\":\"a\",\"status\":\"active\",\"created\":1755000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":1000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* inside the PRIOR window: created before window_start but within one
     * span of it */
    "{\"id\":\"b\",\"status\":\"active\",\"created\":1752000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":2000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}},"
    /* older than both windows */
    "{\"id\":\"c\",\"status\":\"active\",\"created\":1700000000,"
    "\"items\":{\"data\":[{\"quantity\":1,\"price\":{\"unit_amount\":3000,"
    "\"currency\":\"usd\",\"recurring\":{\"interval\":\"month\","
    "\"interval_count\":1}}}]}}"
    "]}";

static void test_prior_window(void)
{
    printf("signups in the previous period\n");

    jsonstream_t js;
    jsonstream_init(&js);
    /* Window start chosen so "a" is inside it and "b" is one span before. */
    jsonstream_set_window(&js, 1754000000);
    jsonstream_set_span(&js, 30 * 86400);
    jsonstream_feed(&js, PACE_PAGE, strlen(PACE_PAGE));
    jsonstream_finish(&js);

    const mrr_totals_t *t = jsonstream_totals(&js);

    check_int("one signup in the current window", t->new_count, 1);
    check_int("one signup in the prior window", t->prior_new_count, 1);
    check_int("all three still active", t->active_count, 3);
}

/* Without a span there is no prior window, so the count stays zero rather
 * than counting everything older as "last period". */
static void test_prior_window_needs_span(void)
{
    printf("no span means no prior count\n");

    jsonstream_t js;
    jsonstream_init(&js);
    jsonstream_set_window(&js, 1754000000);
    /* deliberately no jsonstream_set_span */
    jsonstream_feed(&js, PACE_PAGE, strlen(PACE_PAGE));
    jsonstream_finish(&js);

    check_int("current window still counted",
              jsonstream_totals(&js)->new_count, 1);
    check_int("prior window not counted",
              jsonstream_totals(&js)->prior_new_count, 0);
}

int main(void)
{
    test_single_chunk();
    test_byte_at_a_time();
    test_every_chunk_size();
    test_status_filtering();
    test_interval_normalisation();
    test_braces_inside_strings();
    test_state_is_bounded();

    test_flow_counts();
    test_flow_byte_at_a_time();
    test_flow_without_window();
    test_flow_revenue();
    test_flow_revenue_byte_at_a_time();
    test_prior_window();
    test_prior_window_needs_span();
    test_at_risk();
    test_at_risk_byte_at_a_time();
    test_no_one_at_risk();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
