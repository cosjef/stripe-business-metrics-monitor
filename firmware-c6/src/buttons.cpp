#include "buttons.h"

#include <Arduino.h>

#include "board.h"

/*
 * Debounce window. A tactile switch bounces for a few milliseconds on both
 * edges; 30ms is comfortably past that without being perceptible.
 */
#define DEBOUNCE_MS 30

/*
 * Both buttons are active low: pressed pulls the pin to ground. Verified on
 * hardware -- both read high at rest with the internal pull-ups enabled.
 */
typedef struct {
    uint8_t pin;
    bool last_stable;      /* debounced level, true = released */
    bool last_read;
    uint32_t changed_ms;
    bool fired;            /* press already reported; wait for release */
} btn_t;

static btn_t s_back = { BTN_BACK_GPIO, true, true, 0, false };
static btn_t s_fwd  = { BTN_FWD_GPIO,  true, true, 0, false };

void buttons_begin(void)
{
    pinMode(BTN_BACK_GPIO, INPUT_PULLUP);
    pinMode(BTN_FWD_GPIO, INPUT_PULLUP);
}

/*
 * True on the transition into "pressed", once per press.
 *
 * Reporting the edge rather than the level is what stops a held button from
 * flipping through the whole deck: it fires once and then waits for release.
 */
static bool pressed_edge(btn_t *b, uint32_t now)
{
    const bool level = (digitalRead(b->pin) != 0);   /* true = released */

    if (level != b->last_read) {
        b->last_read = level;
        b->changed_ms = now;
        return false;
    }

    if (now - b->changed_ms < DEBOUNCE_MS) {
        return false;
    }

    if (level == b->last_stable) {
        return false;
    }
    b->last_stable = level;

    if (!level) {          /* pulled low: pressed */
        if (!b->fired) {
            b->fired = true;
            return true;
        }
    } else {
        b->fired = false;  /* released: ready for the next press */
    }
    return false;
}

int buttons_poll(void)
{
    const uint32_t now = millis();

    /*
     * Forward is checked first. If both are somehow down, advancing is the
     * more likely intent and the less surprising outcome.
     */
    if (pressed_edge(&s_fwd, now)) {
        return 1;
    }
    if (pressed_edge(&s_back, now)) {
        return -1;
    }
    return 0;
}
