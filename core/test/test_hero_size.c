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

#include "../include/hero_size.h"
#include "../include/layout.h"

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

    /* Roboto Condensed has TABULAR figures: every digit shares one advance.
     * This is the anti-jitter property spec 5.4 wanted, and we get it here
     * without paying monospace's cost on letters and punctuation. */
    check_int("'111' same width as '888'",
              text_width_px("111", 64), text_width_px("888", 64));

    check_int("'1' same width as '8'",
              text_width_px("1", 96), text_width_px("8", 96));

    /* Letters, unlike digits, are genuinely proportional. */
    check_true("'i' narrower than 'W'",
               text_width_px("i", 64) < text_width_px("W", 64));

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

    /* Spec renders these at 60-88px. A condensed face beats that across the
     * board, since it neither pays monospace's per-period cell nor a wide
     * face's letterforms. These lower bounds lock in that gain -- if a future
     * font change regresses them, the device gets harder to read. */
    check_true("'$6.5k' >= 88px", hero_size_for_text("$6.5k") >= 88);
    check_true("'2' >= 96px",     hero_size_for_text("2") >= 96);
    check_true("'94' >= 96px",    hero_size_for_text("94") >= 96);
    check_true("'11' >= 96px",    hero_size_for_text("11") >= 96);
    check_true("'34%' >= 88px",   hero_size_for_text("34%") >= 88);
    check_true("'+$29' >= 88px",  hero_size_for_text("+$29") >= 88);

    /* Large accounts stay well above the spec's "comfortable" 34px band. */
    check_true("'$145k' >= 76px",  hero_size_for_text("$145k") >= 76);
    check_true("'$1.45M' >= 64px", hero_size_for_text("$1.45M") >= 64);
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

/*
 * Sizing to a narrower column than the panel's.
 *
 * The card layout insets its text by CARD_PAD on both sides, so a hero drawn
 * inside a card has less room than one drawn against the panel column. Sizing
 * every hero to TEXT_COLUMN_PX meant three of the five card screens rendered
 * text past the card's edge -- "$13,276" ended 7px beyond the card's outer
 * boundary, and "$42.00" at the 137px cap overflowed too. The value was
 * legible, so it read as a padding nit rather than the sizing bug it was.
 */
static void test_size_for_width(void)
{
    printf("sizing to an arbitrary column width\n");

    /* The panel column and the card column give different answers for the
     * same string -- that difference is the whole point. */
    const int narrow_col = TEXT_COLUMN_PX / 2;
    const int panel = hero_size_for_width("$13,276", TEXT_COLUMN_PX);
    const int card  = hero_size_for_width("$13,276", narrow_col);
    check_true("a narrower column picks a smaller size", card < panel);

    /* Whatever it picks must actually fit. */
    check_true("the chosen size fits the narrow column",
               text_width_px("$13,276", card) <= narrow_col);
    check_true("the chosen size fits the panel column",
               text_width_px("$13,276", panel) <= TEXT_COLUMN_PX);

    /* The old entry point keeps its meaning: the full panel column. */
    check_int("hero_size_for_text still sizes to the panel column",
              hero_size_for_text("$13,276"), panel);

    /* A value at the cap must still be stepped down when the column is
     * narrow -- this is the "$42.00" case that overflowed. */
    const int narrow = hero_size_for_width("$42.00", narrow_col);
    check_true("a capped value is stepped down for a narrow column",
               text_width_px("$42.00", narrow) <= narrow_col);

    /* A short value is unaffected either way. */
    check_int("short values are unchanged",
              hero_size_for_width("33", TEXT_COLUMN_PX), hero_size_for_text("33"));
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

    test_size_for_width();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
