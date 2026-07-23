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
// LiPo cells on nice!nano v2, measured through the board's own divider/ADC so
// any systematic measurement offset is already baked in. Anchors: 4050mV is
// the resting voltage of a fully saturated (overnight) charge; 3630mV is where
// battery reporting historically dropped out, so 0% means "charge now", with
// remaining cell capacity below it kept as unobservable reserve. The shape
// between anchors is a published 1S Li-ion OCV curve rescaled to that span.
static const struct {
    int16_t mv;
    uint8_t pct;
} battery_curve[] = {
    {4050, 100}, {4000, 94}, {3950, 83}, {3900, 69}, {3850, 57},
    {3800, 44},  {3750, 31}, {3700, 17}, {3660, 7},  {3630, 0},
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