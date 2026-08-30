/*
 * Touch panel: tap the glass to move the deck.
 *
 * A tap on the left third goes back, the right two thirds advance. Deliberately
 * asymmetric -- advancing is the common intent, so it gets the larger target,
 * and a reader who taps without aiming gets the more likely action.
 *
 * The CST9217 is confirmed present at 0x5A on this board by bus ACK. It is
 * register-compatible with the CST9220 in the subset the driver uses.
 */
#pragma once

#include <stdbool.h>

/* Returns false if the controller did not answer; touch is then simply
 * unavailable and the buttons still work. */
bool touch_begin(void);

/*
 * Poll for a completed tap. Returns -1 to go back, +1 to advance, 0 for
 * nothing. Reports on RELEASE, so a resting finger cannot repeat.
 */
int touch_poll(void);
