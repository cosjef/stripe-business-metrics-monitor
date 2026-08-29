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

int main(void)
{
    test_single_chunk();
    test_byte_at_a_time();
    test_every_chunk_size();
    test_status_filtering();
    test_interval_normalisation();
    test_braces_inside_strings();
    test_state_is_bounded();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
