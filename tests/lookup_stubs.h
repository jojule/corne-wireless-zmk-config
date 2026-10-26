/* SPDX-License-Identifier: MIT
 * Native test adapter for the real lookup driver and pinned ZMK dispatch loop.
 */
#pragma once
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CONFIG_ZMK_SPLIT 1
#define CONFIG_ZMK_SPLIT_ROLE_CENTRAL 1
#define CONFIG_KERNEL_INIT_PRIORITY_DEFAULT 0
#define CONFIG_ZMK_LOG_LEVEL 0
#define ZMK_KEYMAP_LAYERS_LEN 5
#define ZMK_KEYMAP_LEN 42
#define ZMK_KEYMAP_LAYER_ID_INVAL UINT8_MAX
#define ZMK_BEHAVIOR_OPAQUE 0
#define ZMK_BEHAVIOR_TRANSPARENT 1
#define K_FOREVER 0
#define IS_ENABLED(x) (x)
#define BUILD_ASSERT(condition, message) _Static_assert(condition, message)
#define K_MUTEX_DEFINE(name) int name
#define K_WORK_DEFINE(name, callback) struct k_work name = {.handler = callback}
#define LOG_MODULE_DECLARE(...)
#define LOG_WRN(...)
#define LOG_DBG(...)
#define BEHAVIOR_DT_INST_DEFINE(...)
#define LAYER_ID_TO_INDEX(layer) (layer)
#define LAYER_INDEX_TO_ID(layer) (layer)

static inline void k_mutex_lock(int *depth, int timeout) { (void)timeout; ++*depth; }
static inline void k_mutex_unlock(int *depth) { assert(*depth > 0); --*depth; }

struct k_work {
    void (*handler)(struct k_work *);
    bool pending;
};

static inline int k_work_submit(struct k_work *work) {
    work->pending = true;
    return 1;
}

typedef uint8_t zmk_keymap_layer_id_t;

struct zmk_behavior_binding {
    const char *behavior_dev;
    uint32_t param1, param2;
};

struct zmk_behavior_binding_event {
    int layer;
    uint32_t position;
    int64_t timestamp;
    uint8_t source;
};

struct zmk_position_state_changed {
    uint8_t source;
    uint32_t position;
    bool state;
    int64_t timestamp;
};

struct behavior_driver_api {
    int (*binding_pressed)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
    int (*binding_released)(struct zmk_behavior_binding *, struct zmk_behavior_binding_event);
};

int zmk_keymap_layer_activate(zmk_keymap_layer_id_t layer, bool locking);
int zmk_keymap_layer_deactivate(zmk_keymap_layer_id_t layer, bool locking);
int zmk_keymap_position_state_changed(uint8_t source, uint32_t position, bool pressed,
                                      int64_t timestamp);
