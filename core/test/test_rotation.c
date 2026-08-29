/*
 * Host tests for conditional screen rotation (spec 6.1).
 *
 *   cd firmware/test && make && ./test_rotation
 *
 * Spec 6.1 on churn: "render it into rotation only when nonzero for the
 * period, so it does not occupy 8 seconds displaying a permanent zero." These
 * tests apply that principle to every metric an account may not use.
 */
#include <stdio.h>
#include <string.h>

#include "../include/rotation.h"

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

static void check_false(const char *what, int cond)
{
    checks++;
    if (cond) {
        failures++;
        printf("  FAIL %s: expected false\n", what);
    }
}

static bool contains(const screen_id_t *list, int n, screen_id_t id)
{
    for (int i = 0; i < n; i++) {
        if (list[i] == id) {
            return true;
        }
    }
    return false;
}

/*
 * An account that does not use trials: the two trial screens are dead weight.
 */
static void test_no_trials_drops_two_screens(void)
{
    printf("an account with no trials drops both trial screens\n");

    const rotation_state_t st = {
        .have_data = true, .trial_count = 0, .churned_30d = 0,
        .have_conversion = false, 
    };

    screen_id_t list[SCREEN_COUNT];
    const int n = rotation_build(&st, list);

    check_false("no trials screen", contains(list, n, SCREEN_TRIALS));
    check_false("no conversion screen", contains(list, n, SCREEN_CONVERSION));

    check_true("MRR present", contains(list, n, SCREEN_MRR));
    check_true("new paid present", contains(list, n, SCREEN_NEW_PAID));
    check_true("paid subs present", contains(list, n, SCREEN_PAID_SUBS));
    check_true("cancellations present", contains(list, n, SCREEN_CANCELLATIONS));

    check_int("seven screens", n, 7);
}

static void test_trials_appear_when_present(void)
{
    printf("trials appear once the account has them\n");

    const rotation_state_t st = {
        .have_data = true, .trial_count = 11, .churned_30d = 0,
        .have_conversion = false, 
    };

    screen_id_t list[SCREEN_COUNT];
    const int n = rotation_build(&st, list);

    check_true("trials shown", contains(list, n, SCREEN_TRIALS));
    /* Conversion still needs history, which trials alone do not supply. */
    check_false("conversion still hidden", contains(list, n, SCREEN_CONVERSION));
}

static void test_conversion_needs_history(void)
{
    printf("conversion appears only with history behind it\n");

    rotation_state_t st = {
        .have_data = true, .trial_count = 11, .churned_30d = 0,
        .have_conversion = true, 
    };

    screen_id_t list[SCREEN_COUNT];
    int n = rotation_build(&st, list);
    check_true("shown with history", contains(list, n, SCREEN_CONVERSION));

    st.have_conversion = false;
    n = rotation_build(&st, list);
    check_false("hidden without history", contains(list, n, SCREEN_CONVERSION));
}

/*
 * Cancellations are shown even at zero: unlike a daily churn count, "none in
 * 30 days" is information worth having.
 */
static void test_cancellations_always_shown(void)
{
    printf("cancellations show even at zero\n");

    rotation_state_t st = {
        .have_data = true, .trial_count = 0, .churned_30d = 0,
        .have_conversion = false,
    };

    screen_id_t list[SCREEN_COUNT];
    int n = rotation_build(&st, list);
    check_true("shown at zero", contains(list, n, SCREEN_CANCELLATIONS));

    st.churned_30d = 3;
    n = rotation_build(&st, list);
    check_true("shown when nonzero", contains(list, n, SCREEN_CANCELLATIONS));
}

/*
 * Before the first fetch the device must still show something, or it looks
 * broken while it starts up.
 */
static void test_before_first_fetch(void)
{
    printf("the device is never blank before data arrives\n");

    const rotation_state_t st = {0};   /* have_data false */

    screen_id_t list[SCREEN_COUNT];
    const int n = rotation_build(&st, list);

    check_true("at least one screen", n > 0);
    check_true("MRR is shown", contains(list, n, SCREEN_MRR));

    /* Conditional screens stay hidden: there is nothing to say yet, and a
     * cancellations screen reading zero before any fetch would be a claim we
     * cannot support. */
    check_false("no cancellations yet", contains(list, n, SCREEN_CANCELLATIONS));
    check_false("no conversion", contains(list, n, SCREEN_CONVERSION));
}

/* MRR is the anchor metric (spec 6.1) and must never be dropped. */
static void test_mrr_always_present(void)
{
    printf("MRR is never dropped\n");

    const rotation_state_t states[] = {
        {0},
        { .have_data = true },
        { .have_data = true, .trial_count = 5, .churned_30d = 3,
          .have_conversion = true },
    };

    screen_id_t list[SCREEN_COUNT];
    for (size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        const int n = rotation_build(&states[i], list);
        char what[64];
        snprintf(what, sizeof(what), "state %zu keeps MRR", i);
        check_true(what, contains(list, n, SCREEN_MRR));
        snprintf(what, sizeof(what), "state %zu is non-empty", i);
        check_true(what, n > 0);
    }
}

static void test_full_account(void)
{
    printf("an account using everything\n");

    const rotation_state_t st = {
        .have_data = true, .trial_count = 11, .churned_30d = 2,
        .have_conversion = true, 
    };

    screen_id_t list[SCREEN_COUNT];
    const int n = rotation_build(&st, list);

    check_int("nine without failed payments", n, 9);
}

/* Display order must be stable, so the rotation does not reshuffle. */
static void test_order_is_stable(void)
{
    printf("display order is stable\n");

    const rotation_state_t st = {
        .have_data = true, .trial_count = 5, .churned_30d = 1,
        .have_conversion = true, 
    };

    screen_id_t a[SCREEN_COUNT], b[SCREEN_COUNT];
    const int na = rotation_build(&st, a);
    const int nb = rotation_build(&st, b);

    check_int("same length", na, nb);
    checks++;
    if (memcmp(a, b, (size_t)na * sizeof(a[0])) != 0) {
        failures++;
        printf("  FAIL order differs between identical calls\n");
    }

    /* MRR leads: it is the anchor metric. */
    check_int("MRR first", a[0], SCREEN_MRR);
}

/* Failed payments appear only when the key can read them and some exist. */
static void test_failed_payments_conditional(void)
{
    printf("failed payments are conditional on permission and count\n");

    rotation_state_t st = {
        .have_data = true, .have_invoices = false, .failed_count = 0,
    };

    screen_id_t list[SCREEN_COUNT];
    int n = rotation_build(&st, list);
    check_false("hidden without permission", contains(list, n, SCREEN_FAILED));

    st.have_invoices = true;
    n = rotation_build(&st, list);
    check_false("hidden at zero", contains(list, n, SCREEN_FAILED));

    st.failed_count = 2;
    n = rotation_build(&st, list);
    check_true("shown when payments are failing", contains(list, n, SCREEN_FAILED));
}

/* ARR and ARPU ride on MRR being available. */
static void test_derived_metrics_follow_mrr(void)
{
    printf("ARR and ARPU appear with the data\n");

    rotation_state_t st = {0};
    screen_id_t list[SCREEN_COUNT];

    int n = rotation_build(&st, list);
    check_false("no ARR before data", contains(list, n, SCREEN_ARR));
    check_false("no ARPU before data", contains(list, n, SCREEN_ARPU));

    st.have_data = true;
    n = rotation_build(&st, list);
    check_true("ARR with data", contains(list, n, SCREEN_ARR));
    check_true("ARPU with data", contains(list, n, SCREEN_ARPU));
}

int main(void)
{
    printf("conditional rotation tests (spec 6.1)\n\n");

    test_no_trials_drops_two_screens();
    test_trials_appear_when_present();
    test_conversion_needs_history();
    test_cancellations_always_shown();
    test_before_first_fetch();
    test_mrr_always_present();
    test_full_account();
    test_order_is_stable();
    test_failed_payments_conditional();
    test_derived_metrics_follow_mrr();

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
