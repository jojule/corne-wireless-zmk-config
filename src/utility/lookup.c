/* SPDX-License-Identifier: MIT */
#define DT_DRV_COMPAT corne_lookup

#include <drivers/behavior.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include <corne_utility/lookup.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)

BUILD_ASSERT(ZMK_KEYMAP_LAYERS_LEN == CORNE_LOOKUP_LAYER + 1,
             "Lookup must be the highest keymap layer");

static struct corne_lookup lookup = {.key = CORNE_UTILITY_NO_KEY};
K_MUTEX_DEFINE(lookup_mutex);

static void publish(void) { corne_utility_lookup_update(lookup.mode, lookup.key); }

static void finish_transition(void) {
    enum corne_lookup_transition transition = corne_lookup_after_event(&lookup);
    if (transition == CORNE_LOOKUP_ENTER) {
        publish();
    } else if (transition == CORNE_LOOKUP_EXIT) {
        int layer_ret = zmk_keymap_layer_deactivate(CORNE_LOOKUP_LAYER, false);
        if (layer_ret < 0) {
            LOG_WRN("Lookup layer deactivation event returned %d", layer_ret);
        }
        publish();
    }
}

static void finish_replay(struct k_work *work) {
    k_mutex_lock(&lookup_mutex, K_FOREVER);
    finish_transition();
    k_mutex_unlock(&lookup_mutex);
}

static K_WORK_DEFINE(finish_replay_work, finish_replay);

int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed event);

/* A normal event subscription is too late: hold-tap and combo listeners can
 * consume events before the module sees them. Link wrapping the typed raiser
 * observes exactly the two physical sources in pinned ZMK (physical_layouts.c
 * and split/central.c). Replays use ZMK_EVENT_RAISE/AT/RELEASE directly, so they
 * cannot double-count the physical down bits. This wrapper is linked centrally
 * only; the peripheral keeps forwarding every original key event.
 *
 * The mutex is recursive: dispatch can invoke the lookup behavior below. It
 * serializes local and peripheral transitions without depending on listener
 * ordering. The lookup layer is armed as soon as entry is requested, so a
 * subsequently replayed captured press cannot execute an ordinary behavior.
 * ZMK remembers the layer state of each earlier executed press, so its release
 * still goes to the original behavior. Only the visible ACTIVE/OFF transition
 * happens after all keys are released and their release dispatch has finished.
 */
int __wrap_raise_zmk_position_state_changed(struct zmk_position_state_changed event) {
    k_mutex_lock(&lookup_mutex, K_FOREVER);

    bool dispatch = corne_lookup_before_event(&lookup, event.position, event.state);
    int ret = dispatch ? __real_raise_zmk_position_state_changed(event) : 0;

    finish_transition();

    k_mutex_unlock(&lookup_mutex);
    return ret;
}

static int lookup_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    k_mutex_lock(&lookup_mutex, K_FOREVER);
    if (binding->param1 == 0) {
        bool entering = lookup.mode == CORNE_UTILITY_LOOKUP_OFF;
        corne_lookup_request_entry(&lookup);
        if (entering) {
            /* Capture/replay bypasses the physical raiser above. Arm the opaque
             * layer NOW so a press replayed after this entry request cannot
             * reach BT_CLR, macros, mouse actions, or another hold-tap. Earlier
             * presses retain their release binding in ZMK's per-position cache.
             */
            int layer_ret = zmk_keymap_layer_activate(CORNE_LOOKUP_LAYER, false);
            if (layer_ret < 0) {
                LOG_WRN("Lookup layer activation event returned %d", layer_ret);
            }
            /* A deferred replay can request entry after the physical all-up
             * event has already returned. Finish after this system-workqueue
             * replay batch, without requiring another key to be pressed.
             */
            k_work_submit(&finish_replay_work);
        }
    } else {
        corne_lookup_select(&lookup, event.position);
    }
    publish();
    k_mutex_unlock(&lookup_mutex);
    return ZMK_BEHAVIOR_OPAQUE;
}

#else

/* Lookup runs on the central. Defining the same harmless behavior on both
 * halves keeps the common keymap valid without ever sending host input here.
 */
static int lookup_pressed(struct zmk_behavior_binding *binding,
                           struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

#endif

static int lookup_released(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api lookup_api = {
    .binding_pressed = lookup_pressed,
    .binding_released = lookup_released,
};

BEHAVIOR_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL,
                        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &lookup_api);
