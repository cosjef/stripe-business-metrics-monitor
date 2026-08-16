/*
 * Display state selection. See state.h for the precedence rationale.
 */
#include "state.h"

display_state_t display_state(const device_status_t *status)
{
    if (!status->provisioned) {
        return DISPLAY_SETUP;
    }

    if (status->auth_failed) {
        return DISPLAY_AUTH_ERROR;
    }

    if (status->stale) {
        return DISPLAY_STALE;
    }

    return DISPLAY_ROTATION;
}

_Bool display_state_rotates(display_state_t state)
{
    return state == DISPLAY_ROTATION || state == DISPLAY_STALE;
}
