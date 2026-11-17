/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

// Publishes the battery voltage behind the reported percentage, so a host can log
// the actual discharge curve instead of inverting rounded percentages.

#include <errno.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <zmk/battery_mv_gatt.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define BATTERY_MV_SERVICE_UUID                                                                    \
    BT_UUID_128_ENCODE(0x7a1c0001, 0x5b2e, 0x4d4b, 0x9a1e, 0x2f6c1b9e3d10)
#define BATTERY_MV_CHAR_UUID BT_UUID_128_ENCODE(0x7a1c0002, 0x5b2e, 0x4d4b, 0x9a1e, 0x2f6c1b9e3d10)

static struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(BATTERY_MV_SERVICE_UUID);
static struct bt_uuid_128 char_uuid = BT_UUID_INIT_128(BATTERY_MV_CHAR_UUID);

static uint16_t battery_mv;

static ssize_t read_battery_mv(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    uint16_t value = sys_cpu_to_le16(battery_mv);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, &value, sizeof(value));
}

static void battery_mv_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {}

BT_GATT_SERVICE_DEFINE(battery_mv_svc, BT_GATT_PRIMARY_SERVICE(&service_uuid),
                       BT_GATT_CHARACTERISTIC(&char_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ, read_battery_mv, NULL, NULL),
                       BT_GATT_CCC(battery_mv_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

void zmk_battery_mv_gatt_update(uint16_t millivolts) {
    if (millivolts == battery_mv) {
        return;
    }
    battery_mv = millivolts;

    uint16_t value = sys_cpu_to_le16(millivolts);
    int err = bt_gatt_notify(NULL, &battery_mv_svc.attrs[2], &value, sizeof(value));
    if (err && err != -ENOTCONN) {
        LOG_DBG("Failed to notify battery millivolts: %d", err);
    }
}
