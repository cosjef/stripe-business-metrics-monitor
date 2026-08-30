#include "touch.h"

#include <Arduino.h>
#include <TouchDrvCSTXXX.hpp>
#include <Wire.h>

#include "board.h"

static TouchDrvCST92xx s_touch;
static bool s_ready;

/*
 * Where the last press landed, held until release.
 *
 * The gesture fires on release rather than on contact: a finger resting on
 * the glass would otherwise advance the deck repeatedly, and reporting on
 * release also lets a tap be abandoned by sliding off.
 */
static bool s_was_down;
static int16_t s_down_x;

bool touch_begin(void)
{
    s_touch.setPins(TP_RST, TP_INT);
    if (!s_touch.begin(Wire, CST9220_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("touch: controller did not answer");
        return false;
    }

    s_touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);

    /*
     * No swap, no mirror.
     *
     * This matters and is not a guess: the mapping depends on the panel's
     * MADCTL, and an earlier Waveshare orientation forced 0x30 (MV transpose)
     * which needed swap and mirror-X to compensate. The CO5300 class default
     * used here is MADCTL 0x00, where the raw controller coordinates map
     * straight through. Clawdmeter tap-tested exactly this pairing on this
     * board.
     */
    s_touch.setSwapXY(false);
    s_touch.setMirrorXY(false, false);

    /* Polled, not interrupt driven -- same reasoning as the buttons. A deck
     * that changes every five seconds does not need an ISR, and the pin is
     * still configured so the controller can assert it. */
    pinMode(TP_INT, INPUT_PULLUP);

    s_ready = true;
    Serial.println("touch: CST9217 ready");
    return true;
}

int touch_poll(void)
{
    if (!s_ready) {
        return 0;
    }

    int16_t x[5], y[5];
    const uint8_t n = s_touch.getPoint(x, y, 1);

    if (n > 0) {
        /* Contact: remember where it started and wait for release. */
        if (!s_was_down) {
            s_was_down = true;
            s_down_x = x[0];
        }
        return 0;
    }

    if (!s_was_down) {
        return 0;
    }
    s_was_down = false;

    /*
     * Left third goes back, the rest advances. Advancing is the common
     * intent, so it gets the larger target: an unaimed tap does the more
     * likely thing.
     */
    return (s_down_x < LCD_WIDTH / 3) ? -1 : 1;
}
