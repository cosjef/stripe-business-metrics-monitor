/*
 * Display state selection (spec 6.2).
 *
 * Which of the four screens the device shows is a pure decision over three
 * flags, so it lives here rather than inside render(): no LVGL, no ESP-IDF, no
 * globals, which means the host test harness can walk every transition.
 *
 * The precedence is the part worth protecting. Showing the wrong state is not
 * a cosmetic bug -- it sends the reader to debug the wrong problem.
 */
#pragma once

/* Current device conditions, as known at render time. */
typedef struct {
    _Bool provisioned;  /* WiFi credentials and a Stripe key are both stored */
    _Bool auth_failed;  /* Stripe rejected the key (401/403) */
    _Bool battery_warn; /* running on battery and low enough to act on */
    _Bool stale;        /* last successful poll is older than the threshold */
} device_status_t;

/*
 * The five screens, in precedence order: earlier members outrank later ones.
 */
typedef enum {
    DISPLAY_SETUP = 0,   /* State C -- nothing works until the owner acts */
    DISPLAY_AUTH_ERROR,  /* State B -- there will be no more numbers */
    DISPLAY_BATTERY,     /* running out of power, and it will only get worse */
    DISPLAY_STALE,       /* State A -- these numbers are old */
    DISPLAY_ROTATION,    /* normal -- live metrics */
} display_state_t;

/*
 * Pick the screen to show.
 *
 * Precedence, strongest first:
 *
 *   setup       An unprovisioned device has no key to reject and no data to
 *               age, so the other flags are meaningless. It also has nothing
 *               a reader could act on except finishing setup.
 *
 *   auth error  Outranks stale deliberately. Stale says "this number is old";
 *               an auth failure says "there will be no more numbers until you
 *               act". A revoked key drags the data stale behind it within the
 *               threshold, so the two nearly always appear together -- and
 *               surfacing stale would send the owner to debug their network
 *               instead of re-issuing the key.
 *
 *   battery     Below auth error: a rejected key outlives any battery state,
 *               and re-issuing it is unrelated to power. Above stale: a dying
 *               battery explains the staleness and gets worse on its own, so
 *               reporting age instead sends the owner to debug the wrong thing.
 *
 *   stale       The data is old but the device is otherwise healthy.
 *
 *   rotation    Everything is working.
 */
display_state_t display_state(const device_status_t *status);

/*
 * Whether the deck keeps advancing in this state.
 *
 * Stale is deliberately not a takeover screen (spec 6.2): the label and value
 * keep changing, but every value dims and the age shows in amber. A frozen
 * screen reads as a crashed device, which is a worse diagnosis than the true
 * one. Setup and auth error are takeovers -- there is exactly one thing to say
 * and cycling metrics behind it would be noise.
 */
_Bool display_state_rotates(display_state_t state);
