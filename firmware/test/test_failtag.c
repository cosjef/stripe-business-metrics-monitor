/*
 * Short failure tags for the stale footer.
 *
 *   cd firmware/test && make && ./test_failtag
 *
 * The stale screen tells the reader their data is old and that the device is
 * still retrying. What it never said was WHY, so every non-auth failure --
 * network down, TLS failure, Stripe 5xx, rate limiting -- looked identical.
 *
 * This does not get its own screen. For nearly every cause the reader's action
 * is the same (wait), and a screen distinguishing things you cannot act on
 * would dilute the deck. The one genuinely different case is Stripe answering
 * with 5xx or 429: that is positive evidence the fault is upstream rather than
 * local, which is worth saying because it means the reader's own network is
 * fine.
 *
 * So the tag goes in the footer, beside the retry countdown, where the eye
 * already goes for status. It costs no rotation time and adds no screen.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "../main/failtag.h"
#include "../main/hero_size.h"
#include "../main/layout.h"

static int failures = 0;
static int checks = 0;

static void check_str(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("  FAIL %s: got \"%s\", want \"%s\"\n", what, got, want);
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

/* ---- the tags themselves ---- */

/*
 * Short enough for a footer, specific enough to act on. "network" and "stripe"
 * are the distinction that matters: is the problem mine or theirs?
 */
static void test_tags_are_distinguishable(void)
{
    printf("each failure class gets a distinct short tag\n");

    check_str("network unreachable", failtag_for(FAILTAG_NETWORK), "network");
    check_str("TLS failure",         failtag_for(FAILTAG_TLS),     "tls");
    check_str("Stripe 5xx",          failtag_for(FAILTAG_SERVER),  "stripe down");
    check_str("rate limited",        failtag_for(FAILTAG_RATE),    "rate limit");
    check_str("unparseable reply",   failtag_for(FAILTAG_BAD),     "bad reply");

    /* No tag when nothing has failed -- an empty string so the caller can
     * append unconditionally without a special case. */
    check_str("no failure yet", failtag_for(FAILTAG_NONE), "");
}

/*
 * Tags must be unique. Two classes sharing a tag would be worse than no tag at
 * all: the reader would draw a false distinction or miss a real one.
 */
static void test_tags_are_unique(void)
{
    printf("no two failure classes share a tag\n");

    const failtag_t all[] = {
        FAILTAG_NETWORK, FAILTAG_TLS, FAILTAG_SERVER,
        FAILTAG_RATE, FAILTAG_BAD,
    };
    const int n = (int)(sizeof(all) / sizeof(all[0]));

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            checks++;
            if (strcmp(failtag_for(all[i]), failtag_for(all[j])) == 0) {
                failures++;
                printf("  FAIL tags %d and %d are identical: \"%s\"\n",
                       (int)all[i], (int)all[j], failtag_for(all[i]));
            }
        }
    }
}

/* ---- the footer must still fit ---- */

/*
 * The reason this is measured rather than eyeballed. The footer is 18px in a
 * 208px column, and the retry countdown is already there. A tag that pushed the
 * line past the column would be silently clipped by LVGL -- no error, just a
 * truncated word, which is exactly the kind of thing that ships unnoticed.
 *
 * Worst case is the longest countdown with the longest tag.
 */
static void test_footer_fits_the_column(void)
{
    printf("the longest possible footer still fits the text column\n");

    const failtag_t all[] = {
        FAILTAG_NONE, FAILTAG_NETWORK, FAILTAG_TLS,
        FAILTAG_SERVER, FAILTAG_RATE, FAILTAG_BAD,
    };

    for (size_t i = 0; i < sizeof(all) / sizeof(all[0]); i++) {
        char footer[FAILTAG_FOOTER_LEN];

        /* 900s is the backoff cap in freshness.h -- the widest countdown. */
        failtag_build_footer(footer, sizeof(footer), 900, all[i]);

        const int w = text_width_px(footer, SIZE_FOOTER);

        checks++;
        if (w > TEXT_COLUMN_PX) {
            failures++;
            printf("  FAIL \"%s\" is %dpx, column is %dpx\n",
                   footer, w, TEXT_COLUMN_PX);
        }
    }
}

/* ---- footer composition ---- */

static void test_footer_composition(void)
{
    printf("footer pairs the countdown with the tag\n");

    char f[FAILTAG_FOOTER_LEN];

    failtag_build_footer(f, sizeof(f), 60, FAILTAG_NETWORK);
    check_str("countdown and tag", f, "retry in 60s / network");

    failtag_build_footer(f, sizeof(f), 240, FAILTAG_SERVER);
    check_str("stripe fault reads as theirs, not yours",
              f, "retry in 240s / stripe down");

    /* Without a known cause the footer is exactly what it was before this
     * change -- no dangling separator. */
    failtag_build_footer(f, sizeof(f), 60, FAILTAG_NONE);
    check_str("no tag, no separator", f, "retry in 60s");

    /* A zero delay means a retry is in flight right now. */
    failtag_build_footer(f, sizeof(f), 0, FAILTAG_NETWORK);
    check_str("retry in flight", f, "retrying / network");

    failtag_build_footer(f, sizeof(f), 0, FAILTAG_NONE);
    check_str("retry in flight, no cause", f, "retrying");
}

/*
 * The mapping from the API's result codes. Auth is deliberately absent: it has
 * its own screen and never reaches the stale footer.
 */
static void test_maps_from_result_codes(void)
{
    printf("stripe result codes map to tags\n");

    check_true("network maps",
               failtag_from_result(1) == FAILTAG_NETWORK);   /* ERR_NETWORK */
    check_true("tls maps",
               failtag_from_result(2) == FAILTAG_TLS);       /* ERR_TLS */
    check_true("rate limit maps",
               failtag_from_result(4) == FAILTAG_RATE);      /* ERR_RATE_LIMITED */
    check_true("server maps",
               failtag_from_result(5) == FAILTAG_SERVER);    /* ERR_SERVER */
    check_true("bad response maps",
               failtag_from_result(6) == FAILTAG_BAD);       /* ERR_BAD_RESPONSE */

    /* Success and auth both produce no tag: success has nothing to report, and
     * auth never reaches this screen. */
    check_true("success has no tag", failtag_from_result(0) == FAILTAG_NONE);
    check_true("auth has no tag -- it has its own screen",
               failtag_from_result(3) == FAILTAG_NONE);
}

int main(void)
{
    test_tags_are_distinguishable();
    test_tags_are_unique();
    test_footer_fits_the_column();
    test_footer_composition();
    test_maps_from_result_codes();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
