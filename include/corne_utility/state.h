/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CORNE_UTILITY_STATE_VERSION 1
#define CORNE_UTILITY_NO_KEY UINT8_MAX

#define CORNE_UTILITY_FLAG_BT_CONNECTED (1U << 0)
#define CORNE_UTILITY_FLAG_BT_BONDED (1U << 1)
#define CORNE_UTILITY_FLAG_USB_SELECTED (1U << 2)
#define CORNE_UTILITY_FLAG_BLE_SELECTED (1U << 3)
#define CORNE_UTILITY_FLAG_HOST_CONNECTED (1U << 4)
#define CORNE_UTILITY_FLAG_USB_READY (1U << 5)

enum corne_utility_lookup_mode {
    CORNE_UTILITY_LOOKUP_OFF = 0,
    CORNE_UTILITY_LOOKUP_ENTERING = 1,
    CORNE_UTILITY_LOOKUP_ACTIVE = 2,
    CORNE_UTILITY_LOOKUP_EXITING = 3,
};

/* Exactly eight bytes, carried in one existing split behavior command.
 * Byte fields deliberately avoid compiler packing and wire endianness assumptions.
 */
struct corne_utility_state {
    uint8_t version;
    uint8_t layer_mask;
    uint8_t profile;
    uint8_t flags;
    uint8_t modifiers;
    uint8_t lookup;
    uint8_t key;
    uint8_t reserved;
};

struct corne_utility_snapshot {
    struct corne_utility_state state;
    bool valid;
    bool split_connected;
};

/* Copies a coherent snapshot. The lock protects only a small memory copy. */
void corne_utility_get_state(struct corne_utility_snapshot *out);

/* Central-only display state update: never invokes the selected key. */
void corne_utility_lookup_update(uint8_t mode, uint8_t key);

/* Explicit wire encoding is also usable by the native validation test. */
static inline uint32_t corne_utility_state_param1(const struct corne_utility_state *state) {
    return (uint32_t)state->version | ((uint32_t)state->layer_mask << 8) |
           ((uint32_t)state->profile << 16) | ((uint32_t)state->flags << 24);
}

static inline uint32_t corne_utility_state_param2(const struct corne_utility_state *state) {
    return (uint32_t)state->modifiers | ((uint32_t)state->lookup << 8) |
           ((uint32_t)state->key << 16) | ((uint32_t)state->reserved << 24);
}

static inline bool corne_utility_physical_position(uint8_t key) {
    return key < 42 && (key >= 36 || (key % 12 != 0 && key % 12 != 11));
}

static inline bool corne_utility_state_decode(uint32_t param1, uint32_t param2,
                                               struct corne_utility_state *out) {
    struct corne_utility_state state = {
        .version = param1 & 0xff,
        .layer_mask = (param1 >> 8) & 0xff,
        .profile = (param1 >> 16) & 0xff,
        .flags = (param1 >> 24) & 0xff,
        .modifiers = param2 & 0xff,
        .lookup = (param2 >> 8) & 0xff,
        .key = (param2 >> 16) & 0xff,
        .reserved = (param2 >> 24) & 0xff,
    };
    if (state.version != CORNE_UTILITY_STATE_VERSION || state.reserved != 0 ||
        !(state.layer_mask & 1) || (state.layer_mask & ~0x0f) || state.profile >= 5 ||
        (state.flags & ~0x3f) || state.lookup > CORNE_UTILITY_LOOKUP_EXITING ||
        (state.key != CORNE_UTILITY_NO_KEY && !corne_utility_physical_position(state.key))) {
        return false;
    }
    *out = state;
    return true;
}
