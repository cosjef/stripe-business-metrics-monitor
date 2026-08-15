/*
 * Verifies that every hero size the sizing logic can return has a corresponding
 * generated font file on disk, and that hero_font_sizes[] agrees with what
 * tools/gen_fonts.sh actually produced.
 *
 * This runs on the host and checks the generated .c files directly rather than
 * linking LVGL, so it catches the failure that matters: computing a size at
 * runtime for which no font was ever generated.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../main/hero_size.h"
#include "../main/layout.h"

static int failures = 0;
static int checks = 0;

static void check_true(const char *what, int cond)
{
    checks++;
    if (!cond) {
        failures++;
        printf("  FAIL %s\n", what);
    }
}

/* UI sizes referenced by layout.h that must also have fonts. */
static const int ui_sizes[] = {SIZE_FOOTER, SIZE_LABEL, SIZE_SUBTITLE};

static int font_file_exists(int size)
{
    char path[256];
    snprintf(path, sizeof(path), "../main/fonts/stripe_mono_%d.c", size);
    FILE *f = fopen(path, "r");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

/*
 * The font file must actually declare the symbol we expect to link against,
 * not merely exist -- a truncated or misnamed generation would otherwise pass.
 */
static int font_declares_symbol(int size)
{
    char path[256];
    char needle[64];
    snprintf(path, sizeof(path), "../main/fonts/stripe_mono_%d.c", size);
    snprintf(needle, sizeof(needle), "stripe_mono_%d", size);

    FILE *f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    char line[4096];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle) && strstr(line, "lv_font_t")) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

static void test_every_hero_size_has_a_font(void)
{
    printf("every hero size has a generated font\n");

    for (size_t i = 0; i < hero_font_sizes_count; i++) {
        int size = hero_font_sizes[i];
        char what[96];

        snprintf(what, sizeof(what), "stripe_mono_%d.c exists", size);
        check_true(what, font_file_exists(size));

        snprintf(what, sizeof(what), "stripe_mono_%d declares lv_font_t", size);
        check_true(what, font_declares_symbol(size));
    }
}

static void test_every_ui_size_has_a_font(void)
{
    printf("every UI size has a generated font\n");

    for (size_t i = 0; i < sizeof(ui_sizes) / sizeof(ui_sizes[0]); i++) {
        int size = ui_sizes[i];
        char what[96];

        snprintf(what, sizeof(what), "stripe_mono_%d.c exists (UI)", size);
        check_true(what, font_file_exists(size));
    }
}

/*
 * The real invariant: any size the sizing function can produce, for any value
 * the device might display, must resolve to a font that was actually generated.
 */
static void test_computed_sizes_always_resolve(void)
{
    printf("computed sizes always resolve to a font\n");

    const char *values[] = {
        "1", "2", "94", "100", "11", "34%", "+$29", "$6.5k", "$145k",
        "$1.45M", "$999.9k", "$12,345", "-$1,234", "$1,234,567",
        "$1,234,567,890.12",
    };

    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        int size = hero_size_for_text(values[i]);
        char what[96];
        snprintf(what, sizeof(what), "'%s' -> %dpx has a font", values[i], size);
        check_true(what, font_file_exists(size));
    }
}

int main(void)
{
    printf("font coverage tests (spec 5.4)\n\n");

    test_every_hero_size_has_a_font();
    test_every_ui_size_has_a_font();
    test_computed_sizes_always_resolve();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
