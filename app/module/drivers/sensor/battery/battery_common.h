/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/drivers/sensor.h>
#include <stdint.h>

struct battery_value {
    uint16_t adc_raw;
    uint16_t millivolts;
    uint8_t state_of_charge;
    int32_t mv_ema_scaled;
};

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out);

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv);

#define BATTERY_EMA_SHIFT 4

uint16_t battery_smooth_mv(struct battery_value *value, uint16_t raw_mv);

// The smoothed voltage is kept across deep sleep, which reboots the chip: the app saves
// battery_ema_current() before sleeping and passes it back to battery_ema_restore() after
// boot. Both use the fixed-point form (millivolts << BATTERY_EMA_SHIFT); zero means none.
void battery_ema_restore(int32_t mv_ema_scaled);
int32_t battery_ema_current(void);
