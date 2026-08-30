/*
 * Side buttons: advance and reverse the deck.
 *
 * Two GPIOs with debounce, polled from the main loop. No interrupt and no
 * component dependency -- a rotation deck does not need sub-millisecond
 * response, and polling keeps the whole thing readable.
 *
 * WHY BUTTONS AND NOT THE IMU. Tap-to-advance was built for the S3 and
 * removed there: the I2C bus intermittently returns corrupt reads that decode
 * as large accelerations, a real tap lands in exactly one 20ms sample, and
 * the bus cannot be polled faster without tripping the watchdog. Every filter
 * that removed the false triggers also rejected real taps -- 8 phantom
 * advances in 60 seconds while the device sat untouched. A debounced digital
 * input has none of those failure modes.
 */
#pragma once

#include <stdbool.h>

void buttons_begin(void);

/*
 * Poll both buttons. Returns -1 to go back, +1 to advance, 0 for nothing.
 * Call every loop; it debounces internally.
 */
int buttons_poll(void);
