/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT corne_utility_relay

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/stdlib.h>

#include <corne_utility/state.h>

BUILD_ASSERT(sizeof(struct corne_utility_state) == 8, "Split display state must fit two params");
BUILD_ASSERT(DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 1, "One internal display receiver is required");
BUILD_ASSERT(sizeof(DEVICE_DT_NAME(DT_DRV_INST(0))) <= 9,
             "Split behavior transport allows at most eight name characters");

static K_MUTEX_DEFINE(state_mutex);
static struct corne_utility_snapshot snapshot = {
    .state = {
        .version = CORNE_UTILITY_STATE_VERSION,
        .layer_mask = 1,
        .key = CORNE_UTILITY_NO_KEY,
    },
};

/* A heartbeat is only sent while the split is connected. This also repairs a
 * dropped initial update without adding a new BLE service or connection.
 */
#define HEARTBEAT_MS 30000
#define STALE_MS (3 * HEARTBEAT_MS)

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

#include <zephyr/bluetooth/conn.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#include <zmk/usb.h>

static struct corne_utility_state last_attempted;
static bool have_attempted;
static atomic_t force_send;
static atomic_t retries_left;

static void refresh_state(struct k_work *work);
static void retry_state(struct k_work *work);
static void heartbeat(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(refresh_work, refresh_state);
static K_WORK_DELAYABLE_DEFINE(retry_work, retry_state);
static K_WORK_DELAYABLE_DEFINE(heartbeat_work, heartbeat);

/* Public transport registrations let us identify actual split peers without
 * guessing Bluetooth roles, depending on private connection pointers, or
 * attempting to send the snapshot to any host profile.
 */
static int connected_sources(uint8_t *sources) {
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, transport) {
        if (!transport->api || !transport->api->get_available_source_ids) {
            continue;
        }
        if (transport->api->get_status) {
            const struct zmk_split_transport_status status = transport->api->get_status();
            if (!status.available || !status.enabled) {
                continue;
            }
        }
        int count = transport->api->get_available_source_ids(sources);
        if (count > 0) {
            return MIN(count, ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT);
        }
    }
    return 0;
}

static void schedule_refresh(void) {
    /* Run after the originating event finishes, so the HID report includes
     * the modifier change regardless of subscriber order. Coalesce rollover.
     */
    k_work_schedule(&refresh_work, K_MSEC(20));
}

static void refresh_state(struct k_work *work) {
    uint8_t sources[ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT];
    const int count = connected_sources(sources);
    const struct zmk_endpoint_instance endpoint = zmk_endpoint_get_selected();
    uint8_t flags = 0;
    if (zmk_ble_active_profile_is_connected()) {
        flags |= CORNE_UTILITY_FLAG_BT_CONNECTED;
    }
    if (!zmk_ble_active_profile_is_open()) {
        flags |= CORNE_UTILITY_FLAG_BT_BONDED;
    }
    if (endpoint.transport == ZMK_TRANSPORT_USB) {
        flags |= CORNE_UTILITY_FLAG_USB_SELECTED;
    } else if (endpoint.transport == ZMK_TRANSPORT_BLE) {
        flags |= CORNE_UTILITY_FLAG_BLE_SELECTED;
    }
    if (endpoint.transport != ZMK_TRANSPORT_NONE) {
        flags |= CORNE_UTILITY_FLAG_HOST_CONNECTED;
    }
    if (zmk_usb_is_hid_ready()) {
        flags |= CORNE_UTILITY_FLAG_USB_READY;
    }

    struct corne_utility_state current;
    k_mutex_lock(&state_mutex, K_FOREVER);
    /* Preserve lookup/key, which may have changed since this work was queued. */
    snapshot.state.layer_mask = (zmk_keymap_layer_state() & 0x0f) | 1;
    snapshot.state.profile = zmk_ble_active_profile_index();
    snapshot.state.flags = flags;
    snapshot.state.modifiers = zmk_hid_get_keyboard_report()->body.modifiers;
    snapshot.valid = true;
    const bool reconnected = count > 0 && !snapshot.split_connected;
    snapshot.split_connected = count > 0;
    current = snapshot.state;
    k_mutex_unlock(&state_mutex);

    if (!count) {
        have_attempted = false;
        return;
    }

    const bool changed = !have_attempted || memcmp(&current, &last_attempted, sizeof(current)) != 0;
    const bool forced = atomic_set(&force_send, 0) != 0;
    if (!changed && !forced && !reconnected) {
        return;
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_DRV_INST(0)),
        .param1 = corne_utility_state_param1(&current),
        .param2 = corne_utility_state_param2(&current),
    };
    const struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };

    for (int i = 0; i < count; i++) {
        (void)zmk_split_central_invoke_behavior(sources[i], &binding, event, true);
    }
    /* Record attempts even if queuing fails, keeping retries bounded. */
    last_attempted = current;
    have_attempted = true;
    /* The stock command API acknowledges queuing, not remote delivery. Short
     * retries cover GATT discovery on reconnect and a temporarily full queue.
     * Each retry sends the latest complete state, never a stale queued copy.
     */
    if (changed || reconnected) {
        atomic_set(&retries_left, reconnected ? 8 : 2);
        k_work_reschedule(&retry_work, K_MSEC(250));
    }
}

static void retry_state(struct k_work *work) {
    if (atomic_dec(&retries_left) <= 0) {
        atomic_set(&retries_left, 0);
        return;
    }
    atomic_set(&force_send, 1);
    schedule_refresh();
    if (atomic_get(&retries_left) > 0) {
        k_work_reschedule(&retry_work, K_MSEC(250));
    }
}

static void heartbeat(struct k_work *work) {
    atomic_set(&force_send, 1);
    schedule_refresh();
    k_work_reschedule(&heartbeat_work, K_MSEC(HEARTBEAT_MS));
}

void corne_utility_lookup_update(uint8_t mode, uint8_t key) {
    if (mode > CORNE_UTILITY_LOOKUP_EXITING ||
        (key != CORNE_UTILITY_NO_KEY && !corne_utility_physical_position(key))) {
        return;
    }
    k_mutex_lock(&state_mutex, K_FOREVER);
    snapshot.state.lookup = mode;
    snapshot.state.key = key;
    k_mutex_unlock(&state_mutex);
    schedule_refresh();
}

static int central_event(const zmk_event_t *eh) {
    schedule_refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_utility_state, central_event);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_layer_state_changed);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_keycode_state_changed);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_usb_conn_state_changed);

static void connected(struct bt_conn *conn, uint8_t err) {
    if (!err) {
        atomic_set(&force_send, 1);
        atomic_set(&retries_left, 8);
        k_work_reschedule(&retry_work, K_MSEC(250));
    }
    schedule_refresh();
}

static void disconnected(struct bt_conn *conn, uint8_t reason) { schedule_refresh(); }

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err) {
    if (!err) {
        atomic_set(&force_send, 1);
        atomic_set(&retries_left, 8);
        k_work_reschedule(&retry_work, K_MSEC(250));
        schedule_refresh();
    }
}

BT_CONN_CB_DEFINE(corne_utility_connection_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

static int state_init(void) {
    k_work_reschedule(&heartbeat_work, K_MSEC(250));
    return 0;
}

SYS_INIT(state_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

static int receive_state(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    /* This internal receiver is never an input behavior on the central. */
    return ZMK_BEHAVIOR_OPAQUE;
}

#else

#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

static int64_t received_at;

static int receive_state(struct zmk_behavior_binding *binding,
                         struct zmk_behavior_binding_event event) {
    struct corne_utility_state received;
    /* Commands arrive only through ZMK's encrypted split behavior service.
     * This receiver has no keymap bindings and can only update display data.
     */
    if (!zmk_split_bt_peripheral_is_connected() ||
        !corne_utility_state_decode(binding->param1, binding->param2, &received)) {
        return -EINVAL;
    }
    k_mutex_lock(&state_mutex, K_FOREVER);
    snapshot.state = received;
    snapshot.valid = true;
    snapshot.split_connected = true;
    received_at = k_uptime_get();
    k_mutex_unlock(&state_mutex);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int peripheral_event(const zmk_event_t *eh) {
    const struct zmk_split_peripheral_status_changed *event =
        as_zmk_split_peripheral_status_changed(eh);
    if (event) {
        k_mutex_lock(&state_mutex, K_FOREVER);
        snapshot.split_connected = event->connected;
        /* Never display state from the previous connection as current. */
        snapshot.valid = false;
        k_mutex_unlock(&state_mutex);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_utility_state, peripheral_event);
ZMK_SUBSCRIPTION(corne_utility_state, zmk_split_peripheral_status_changed);

void corne_utility_lookup_update(uint8_t mode, uint8_t key) {}

#endif

void corne_utility_get_state(struct corne_utility_snapshot *out) {
    k_mutex_lock(&state_mutex, K_FOREVER);
    *out = snapshot;
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    out->valid = snapshot.valid && snapshot.split_connected &&
                 k_uptime_get() - received_at <= STALE_MS;
#endif
    k_mutex_unlock(&state_mutex);
}

static int ignore_release(struct zmk_behavior_binding *binding,
                          struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api relay_api = {
    .binding_pressed = receive_state,
    .binding_released = ignore_release,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &relay_api);
