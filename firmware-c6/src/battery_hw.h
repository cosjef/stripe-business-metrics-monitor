/*
 * Battery reading on the C6, from the AXP2101 over I2C.
 *
 * The classification lives in core/src/battery.c; this only supplies the
 * millivolts and the charging flag.
 */
#pragma once

#include <stdbool.h>

extern "C" {
#include "battery.h"
}

typedef struct {
    int cell_mv;
    bool charging;
    battery_level_t level;
} battery_reading_t;

/* Read the cell. False if the PMIC did not answer. */
bool battery_hw_read(battery_reading_t *out);
