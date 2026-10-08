/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/util.h>

#include "battery_common.h"

int battery_channel_get(const struct battery_value *value, enum sensor_channel chan,
                        struct sensor_value *val_out) {
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = value->millivolts / 1000;
        val_out->val2 = (value->millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = value->state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

// Piecewise-linear discharge curve calibrated in-situ for generic 1500mAh 1S
// LiPo cells on nice!nano v2, measured through the board's own ADC so any
// systematic measurement offset is already baked in. Percent is share of
// remaining keyboard runtime, fitted from a full logged discharge cycle:
// 4050mV is the resting voltage after a saturated (overnight) charge, and the
// cell spends most of its life on a very flat plateau around 3.74-3.82V.
// Below 3630mV the curve is not yet measured; it is extended to a deliberately
// low 3000mV so the voltage at which the keyboard actually dies shows up as a
// non-zero reading and can be pinned down from the next cycle.
static const struct {
    int16_t mv;
    uint8_t pct;
} battery_curve[] = {
    {4050, 100}, {3940, 93}, {3900, 89}, {3850, 84}, {3820, 80}, {3810, 75}, {3800, 69},
    {3790, 64},  {3780, 57}, {3770, 50}, {3760, 42}, {3750, 32}, {3740, 22}, {3730, 15},
    {3720, 12},  {3700, 9},  {3630, 6},  {3550, 4},  {3450, 2},  {3300, 1},  {3000, 0},
};

uint8_t lithium_ion_mv_to_pct(int16_t bat_mv) {
    const size_t last = ARRAY_SIZE(battery_curve) - 1;

    if (bat_mv >= battery_curve[0].mv) {
        return 100;
    } else if (bat_mv <= battery_curve[last].mv) {
        return 0;
    }

    for (size_t i = 1; i <= last; i++) {
        if (bat_mv >= battery_curve[i].mv) {
            const int16_t v_lo = battery_curve[i].mv, v_hi = battery_curve[i - 1].mv;
            const uint8_t p_lo = battery_curve[i].pct, p_hi = battery_curve[i - 1].pct;
            return p_lo + (p_hi - p_lo) * (bat_mv - v_lo) / (v_hi - v_lo);
        }
    }

    return 0;
}
// On the plateau a 1mV change is worth almost 1%, while load sag while typing
// moves single readings by 10mV or more, so readings are smoothed with an
// exponential moving average over ~16 samples (one per minute while active).
// A jump larger than any load sag means USB power was connected or removed,
// so the average restarts from the new reading instead of lagging behind.
#define BATTERY_EMA_SHIFT 4
#define BATTERY_EMA_SNAP_MV 100

uint16_t battery_smooth_mv(struct battery_value *value, uint16_t raw_mv) {
    const int32_t avg = (value->mv_ema_scaled + (1 << (BATTERY_EMA_SHIFT - 1))) >> BATTERY_EMA_SHIFT;

    if (value->mv_ema_scaled == 0 || raw_mv > avg + BATTERY_EMA_SNAP_MV ||
        raw_mv < avg - BATTERY_EMA_SNAP_MV) {
        value->mv_ema_scaled = (int32_t)raw_mv << BATTERY_EMA_SHIFT;
    } else {
        value->mv_ema_scaled += raw_mv - avg;
    }

    return (value->mv_ema_scaled + (1 << (BATTERY_EMA_SHIFT - 1))) >> BATTERY_EMA_SHIFT;
}
