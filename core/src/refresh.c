/*
 * Refresh scheduling. See refresh.h for why this is a named policy rather
 * than an inline condition in loop().
 */
#include "refresh.h"

bool refresh_due(uint32_t now_ms, uint32_t last_ms, bool have_data)
{
    /*
     * Unsigned subtraction, so a millis() wrap yields the true elapsed
     * time rather than a huge or negative number.
     */
    const uint32_t elapsed = now_ms - last_ms;

    /*
     * Having data chooses the cadence; it does not gate the retry. A deck
     * showing nothing has more reason to try again than one already
     * displaying good values.
     */
    return elapsed >= (have_data ? REFRESH_INTERVAL_MS : REFRESH_RETRY_MS);
}
