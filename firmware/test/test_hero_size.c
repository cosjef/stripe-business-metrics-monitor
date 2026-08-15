/*
 * Host-side tests for hero value auto-sizing and column-width math.
 *
 *   cd firmware/test && make && ./test_hero_size
 *
 * NOTE ON THE SPEC: spec 2.3/2.4 compute width as `len * size * 0.6em`, valid
 * only for a monospace face. This build uses SF Compact Bold (proportional) for
 * legibility, so width is measured per glyph instead. The spec's *constraint*
 * -- nothing wider than the 208px column, nothing below the legibility floor --
 * is unchanged and is what these tests enforce.
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#include "../main/hero_size.h"
#include "../main/layout.h"

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

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s: expected true\n", what);
    }
}

/*
 * Width must reflect actual glyph advances, not a uniform per-character value.
 * "111" is far narrower than "888" in this face; a monospace model would call
 * them equal and overflow on the wide one.
 */
static void test_width_is_per_glyph(void)
{
    printf("width measurement is per-glyph\n");

    check_true("'111' narrower than '888'",
               text_width_px("111", 64) < text_width_px("888", 64));

    check_true("'1' narrower than '8'",
               text_width_px("1", 96) < text_width_px("8", 96));

    /* '.' is much narrower than a digit -- the whole reason monospace looked
     * bad here (it reserved a full digit cell for the period). */
    check_true("'.' narrower than '0'",
               text_width_px(".", 64) < text_width_px("0", 64));

    check_int("empty string has zero width", text_width_px("", 64), 0);

    /* Width scales linearly with size. */
    int w32 = text_width_px("$6.5k", 32);
    int w64 = text_width_px("$6.5k", 64);
    check_true("width doubles when size doubles",
               w64 >= 2 * w32 - 2 && w64 <= 2 * w32 + 2);
}

/*
 * The column constraint from spec 2.3 still holds, measured properly.
 */
static void test_column_constraint(void)
{
    printf("208px column constraint\n");

    check_true("'$6.5k' at 64px fits", text_fits("$6.5k", 64));
    check_true("'$6.5k' at 96px overflows", !text_fits("$6.5k", 96));

    /* Whatever size we pick must fit, by construction. */
    const char *values[] = {"$6.5k", "94", "100", "11", "34%", "+$29", "2"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int size = hero_size_for_text(values[i]);
        char what[80];
        snprintf(what, sizeof(what), "'%s' at chosen %dpx fits", values[i], size);
        check_true(what, text_fits(values[i], size));
    }
}

/*
 * Real values from the spec's screen deck (6.1) must stay large enough to read
 * across a room, which is the entire point of the device.
 */
static void test_real_screen_values(void)
{
    printf("real values from the screen deck\n");

    /* Spec renders these at 60-88px; with a proportional face we should meet or
     * beat that, since we are no longer paying for a full cell per period. */
    check_true("'$6.5k' >= 60px", hero_size_for_text("$6.5k") >= 60);
    check_true("'2' >= 88px",     hero_size_for_text("2") >= 88);
    check_true("'94' >= 88px",    hero_size_for_text("94") >= 88);
    check_true("'11' >= 88px",    hero_size_for_text("11") >= 88);
    check_true("'34%' >= 64px",   hero_size_for_text("34%") >= 64);
    check_true("'+$29' >= 52px",  hero_size_for_text("+$29") >= 52);
}

/*
 * The overflow case the sizing rule exists to prevent (spec 2.4).
 */
static void test_large_accounts(void)
{
    printf("large account values do not overflow\n");

    const char *values[] = {
        "$145k", "$1.45M", "$999.9k", "$12,345", "-$1,234", "$1,234,567",
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int size = hero_size_for_text(values[i]);
        char what[80];

        snprintf(what, sizeof(what), "'%s' fits column", values[i]);
        check_true(what, text_fits(values[i], size));

        snprintf(what, sizeof(what), "'%s' at/above legibility floor", values[i]);
        check_true(what, size >= LEGIBILITY_FLOOR_PX);
    }
}

static void test_bounds(void)
{
    printf("size bounds\n");

    check_int("empty string -> 0", hero_size_for_text(""), 0);
    check_true("single char capped at 96", hero_size_for_text("1") <= SIZE_HERO_MAX);

    /* Even an absurdly long string must not drop below the absolute floor --
     * we abbreviate upstream rather than render unreadable text (spec 6.1). */
    check_true("very long string at/above absolute floor",
               hero_size_for_text("$1,234,567,890.12") >= ABSOLUTE_FLOOR_PX);
}

static void test_snaps_to_available_sizes(void)
{
    printf("snapping to available bitmap sizes\n");

    const char *values[] = {"1", "94", "$6.5k", "$145k", "$1.45M", "34%", "+$29"};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int size = hero_size_for_text(values[i]);
        int found = 0;
        for (size_t j = 0; j < hero_font_sizes_count; j++) {
            if (hero_font_sizes[j] == size) {
                found = 1;
                break;
            }
        }
        char what[80];
        snprintf(what, sizeof(what), "'%s' -> %dpx is an available size",
                 values[i], size);
        check_true(what, found);
    }
}

/*
 * Adding characters must never make the font larger.
 */
static void test_monotonic(void)
{
    printf("monotonicity\n");

    check_true("'$6' >= '$6.5'",       hero_size_for_text("$6") >= hero_size_for_text("$6.5"));
    check_true("'$6.5' >= '$6.5k'",    hero_size_for_text("$6.5") >= hero_size_for_text("$6.5k"));
    check_true("'$6.5k' >= '$6.55k'",  hero_size_for_text("$6.5k") >= hero_size_for_text("$6.55k"));
    check_true("'94' >= '941'",        hero_size_for_text("94") >= hero_size_for_text("941"));
}

/*
 * Unknown characters must not silently measure as zero -- that would let a
 * string overflow undetected. They fall back to a conservative width.
 */
static void test_unknown_glyphs(void)
{
    printf("unknown glyph handling\n");

    /* Outside the generated 0x20-0x7A range. */
    check_true("unknown glyph has nonzero width", text_width_px("~", 64) > 0);
    check_true("string with unknown glyph still sized",
               hero_size_for_text("~~~") >= ABSOLUTE_FLOOR_PX);
}

int main(void)
{
    printf("hero sizing tests (spec 2.3/2.4, adapted for proportional face)\n\n");

    test_width_is_per_glyph();
    test_column_constraint();
    test_real_screen_values();
    test_large_accounts();
    test_bounds();
    test_snaps_to_available_sizes();
    test_monotonic();
    test_unknown_glyphs();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
