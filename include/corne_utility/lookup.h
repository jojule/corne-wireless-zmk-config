/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <corne_utility/state.h>

#define CORNE_LOOKUP_LAYER 4
#define CORNE_LOOKUP_G_POSITION 5
#define CORNE_LOOKUP_NAV_POSITION 39

/* Physical transitions, not replayed ZMK events, own these bitsets. */
struct corne_lookup {
    uint64_t down;
    uint64_t swallowed;
    uint8_t mode;
    uint8_t key;
};

enum corne_lookup_transition {
    CORNE_LOOKUP_UNCHANGED,
    CORNE_LOOKUP_ENTER,
    CORNE_LOOKUP_EXIT,
};

/* Return whether the original position event must reach ZMK. While entry is
 * pending, old releases still reach their original behavior; new presses and
 * their matching releases are swallowed until every physical key is up.
 */
static inline bool corne_lookup_before_event(struct corne_lookup *lookup, uint32_t position,
                                             bool pressed) {
    if (position >= 42 || !corne_utility_physical_position(position)) {
        return true;
    }

    uint64_t bit = UINT64_C(1) << position;
    bool was_down = (lookup->down & bit) != 0;
    if (pressed) {
        lookup->down |= bit;
    } else {
        lookup->down &= ~bit;
    }

    if (lookup->mode != CORNE_UTILITY_LOOKUP_ENTERING) {
        return true;
    }
    if (pressed && !was_down) {
        lookup->swallowed |= bit;
    }
    if (lookup->swallowed & bit) {
        if (!pressed) {
            lookup->swallowed &= ~bit;
        }
        return false;
    }
    return true;
}

static inline void corne_lookup_request_entry(struct corne_lookup *lookup) {
    if (lookup->mode == CORNE_UTILITY_LOOKUP_OFF) {
        lookup->mode = CORNE_UTILITY_LOOKUP_ENTERING;
        lookup->key = CORNE_UTILITY_NO_KEY;
    }
}

static inline void corne_lookup_select(struct corne_lookup *lookup, uint32_t position) {
    if (lookup->mode != CORNE_UTILITY_LOOKUP_ACTIVE || position >= 42 ||
        !corne_utility_physical_position(position)) {
        return;
    }
    lookup->key = position;

    const uint64_t exit_keys = (UINT64_C(1) << CORNE_LOOKUP_G_POSITION) |
                               (UINT64_C(1) << CORNE_LOOKUP_NAV_POSITION);
    if ((lookup->down & exit_keys) == exit_keys) {
        lookup->mode = CORNE_UTILITY_LOOKUP_EXITING;
    }
}

/* Call only AFTER dispatching the original event. Its releases may resolve and
 * replay hold-taps/combos synchronously. Do not announce ACTIVE until those old
 * releases finish, or leave the opaque lookup layer before its final release.
 */
static inline enum corne_lookup_transition corne_lookup_after_event(struct corne_lookup *lookup) {
    if (lookup->down) {
        return CORNE_LOOKUP_UNCHANGED;
    }
    if (lookup->mode == CORNE_UTILITY_LOOKUP_ENTERING) {
        lookup->mode = CORNE_UTILITY_LOOKUP_ACTIVE;
        lookup->swallowed = 0;
        return CORNE_LOOKUP_ENTER;
    }
    if (lookup->mode == CORNE_UTILITY_LOOKUP_EXITING) {
        lookup->mode = CORNE_UTILITY_LOOKUP_OFF;
        lookup->swallowed = 0;
        lookup->key = CORNE_UTILITY_NO_KEY;
        return CORNE_LOOKUP_EXIT;
    }
    return CORNE_LOOKUP_UNCHANGED;
}
