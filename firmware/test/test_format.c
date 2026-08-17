/*
 * Host tests for display formatting.
 *
 *   cd firmware/test && make && ./test_format
 *
 * These matter because the output feeds hero_size_for_text(): a format that
 * produces one glyph too many drops the hero a size step, and spec 2.2 is
 * built around that budget.
 */
#include <stdio.h>
#include <string.h>

#include "../main/format.h"
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

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

static void money(int64_t cents, char *buf)
{
    format_money_compact(cents, buf, FORMAT_MONEY_LEN);
}

static void test_small_amounts(void)
{
    printf("amounts under $1000 show exact dollars\n");

    char b[FORMAT_MONEY_LEN];
    money(0, b);       check_str("zero", b, "$0");
    money(100, b);     check_str("$1", b, "$1");
    money(2900, b);    check_str("$29", b, "$29");
    money(99900, b);   check_str("$999", b, "$999");

    /* Cents are dropped: precision below a dollar is not decision-relevant
     * at a glance, and the glyph budget is tight. */
    money(2950, b);    check_str("$29.50 rounds down", b, "$29");
}

/*
 * The decimal is dropped once it would cost a hero size step.
 *
 * "$11.2k" is six glyphs and needs 268px at 96px -- well past the 208px column,
 * so the sizer falls to 64px while every neighbouring screen sits at 96px. The
 * ARR screen looked conspicuously small on the device for exactly this reason.
 *
 * "$11k" is four glyphs, fits at 96px, and matches the rest of the deck. The
 * lost precision is not real precision: ARR is MRR x 12, a projection, and the
 * exact figure is always derivable from the MRR screen.
 *
 * The rule already existed at 100k for this same reason (spec 2.2); the
 * threshold was simply set too high for this column width.
 */
static void test_decimal_dropped_when_it_costs_a_size_step(void)
{
    printf("decimal dropped above $10k so the hero stays at full size\n");

    char b[FORMAT_MONEY_LEN];

    /* The case from the device: ARR on a $970/mo account. */
    money(1164396, b);  check_str("$11,643.96 -> $11k", b, "$11k");
    money(1120000, b);  check_str("$11,200 -> $11k", b, "$11k");

    /* Below the threshold the decimal still earns its place: "$9.4k" is five
     * glyphs and fits at 88px, close enough to its neighbours. */
    money(940000, b);   check_str("$9,400 keeps its decimal", b, "$9.4k");
    money(999900, b);   check_str("just under $10k keeps it", b, "$9.9k");

    /* The boundary itself. */
    money(1000000, b);  check_str("exactly $10k drops it", b, "$10k");

    /* And the size step this is all in service of. */
    check_true("$11k renders at full hero size",
               hero_size_for_text("$11k") == SIZE_HERO_MAX);
    check_true("$11.2k would not have",
               hero_size_for_text("$11.2k") < SIZE_HERO_MAX);
}

static void test_thousands(void)
{
    printf("thousands abbreviate with one decimal (spec 6.1)\n");

    char b[FORMAT_MONEY_LEN];
    money(100000, b);   check_str("$1000 -> $1.0k", b, "$1.0k");
    money(651200, b);   check_str("the spec's example", b, "$6.5k");
    /* Above $10k the decimal is dropped so the hero keeps its full size --
     * see test_decimal_dropped_when_it_costs_a_size_step. */
    money(1234500, b);  check_str("$12,345 -> $12k", b, "$12k");
    /* $99,999 is NOT $100k. Rounding up would overstate revenue, which a
     * display whose whole point is honesty must never do. Truncation, not
     * rounding, is what guarantees that -- and it still holds without the
     * decimal. */
    money(9999900, b);  check_str("$99,999 stays under 100k", b, "$99k");
    money(10000000, b); check_str("$100,000 -> $100k", b, "$100k");
    money(14500000, b); check_str("$145,000 -> $145k", b, "$145k");
}

static void test_millions(void)
{
    printf("millions abbreviate with M\n");

    char b[FORMAT_MONEY_LEN];
    money(100000000, b);  check_str("$1M", b, "$1.00M");
    money(145000000, b);  check_str("$1.45M", b, "$1.45M");
    money(1234500000, b); check_str("$12.3M", b, "$12.3M");
}

static void test_negative(void)
{
    printf("negative amounts keep their sign\n");

    char b[FORMAT_MONEY_LEN];
    money(-2900, b);   check_str("-$29", b, "-$29");
    money(-651200, b); check_str("-$6.5k", b, "-$6.5k");
}

/*
 * The formatted hero must fit the 208px column at a legible size. This is the
 * check that ties formatting back to spec 2.2's legibility floor.
 */
static void test_output_fits_the_column(void)
{
    printf("every formatted value fits the hero column\n");

    const int64_t values[] = {
        0, 100, 2900, 99900, 100000, 651200, 1234500, 9999900,
        14500000, 100000000, 145000000, 1234500000,
        -2900, -651200, -145000000,
    };

    char b[FORMAT_MONEY_LEN];
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        money(values[i], b);
        const int size = hero_size_for_text(b);

        char what[96];
        snprintf(what, sizeof(what), "\"%s\" fits the column", b);
        check_true(what, text_fits(b, size));

        snprintf(what, sizeof(what), "\"%s\" stays above the legibility floor", b);
        check_true(what, size >= LEGIBILITY_FLOOR_PX);
    }
}

/*
 * The spec's own example must render at the size the spec expects.
 */
static void test_spec_example_size(void)
{
    printf("the spec's $6.5k example is glanceable\n");

    char b[FORMAT_MONEY_LEN];
    money(651200, b);
    check_str("formats as expected", b, "$6.5k");
    check_true("renders at 88px or better", hero_size_for_text(b) >= 88);
}

static void test_deltas(void)
{
    printf("deltas carry an explicit sign\n");

    char b[FORMAT_MONEY_LEN];
    format_money_delta(11800, b, sizeof(b));  check_str("+$118", b, "+$118");
    format_money_delta(-4000, b, sizeof(b));  check_str("-$40", b, "-$40");
    format_money_delta(0, b, sizeof(b));      check_str("zero has no sign", b, "$0");
    format_money_delta(651200, b, sizeof(b)); check_str("+$6.5k", b, "+$6.5k");
}

static void test_counts(void)
{
    printf("counts\n");

    char b[FORMAT_MONEY_LEN];
    format_count(0, b, sizeof(b));    check_str("zero", b, "0");
    format_count(94, b, sizeof(b));   check_str("94", b, "94");
    format_count(1204, b, sizeof(b)); check_str("thousands separated", b, "1,204");
}

/* A short buffer must truncate safely rather than overflow. */
static void test_buffer_safety(void)
{
    printf("small buffers are respected\n");

    char tiny[4];
    format_money_compact(145000000, tiny, sizeof(tiny));
    check_true("stays in bounds", strlen(tiny) < sizeof(tiny));

    format_count(123456, tiny, sizeof(tiny));
    check_true("count stays in bounds", strlen(tiny) < sizeof(tiny));
}

int main(void)
{
    printf("display formatting tests (spec 6.1)\n\n");

    test_small_amounts();
    test_thousands();
    test_decimal_dropped_when_it_costs_a_size_step();
    test_millions();
    test_negative();
    test_output_fits_the_column();
    test_spec_example_size();
    test_deltas();
    test_counts();
    test_buffer_safety();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
