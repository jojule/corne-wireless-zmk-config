/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include "lookup_stubs.h"

/* Compile the production driver unchanged, with lightweight Zephyr adapters. */
#include "../src/utility/lookup.c"

static uint32_t _zmk_keymap_layer_state;
static uint8_t _zmk_keymap_layer_default;
static uint32_t zmk_keymap_active_behavior_layer[ZMK_KEYMAP_LEN];
static unsigned normal_presses[ZMK_KEYMAP_LEN], normal_releases[ZMK_KEYMAP_LEN];
static unsigned selected_updates;
static bool capture, replay_on_next_dispatch;
static struct zmk_position_state_changed captured[16];
static unsigned captured_count;

int zmk_keymap_layer_activate(zmk_keymap_layer_id_t layer, bool locking) {
    (void)locking;
    _zmk_keymap_layer_state |= UINT32_C(1) << layer;
    return 0;
}

int zmk_keymap_layer_deactivate(zmk_keymap_layer_id_t layer, bool locking) {
    (void)locking;
    _zmk_keymap_layer_state &= ~(UINT32_C(1) << layer);
    return 0;
}

void corne_utility_lookup_update(uint8_t mode, uint8_t key) {
    assert(mode <= CORNE_UTILITY_LOOKUP_EXITING);
    assert(key == CORNE_UTILITY_NO_KEY || corne_utility_physical_position(key));
    selected_updates++;
}

static bool zmk_keymap_layer_active_with_state(uint8_t layer, uint32_t state) {
    return layer == 0 || (state & (UINT32_C(1) << layer));
}

/* Behaviors are test probes; press-layer selection/release routing below uses
 * the actual function extracted from the pinned ZMK source, not a reimplementation.
 */
static int zmk_keymap_apply_position_state(uint8_t source, uint8_t layer, uint32_t position,
                                           bool pressed, int64_t timestamp) {
    struct zmk_behavior_binding_event event = {
        .source = source, .layer = layer, .position = position, .timestamp = timestamp,
    };
    if (layer == CORNE_LOOKUP_LAYER || (layer == 1 && position == CORNE_LOOKUP_G_POSITION)) {
        struct zmk_behavior_binding binding = {.param1 = layer == CORNE_LOOKUP_LAYER ? 1 : 0};
        return pressed ? lookup_api.binding_pressed(&binding, event)
                       : lookup_api.binding_released(&binding, event);
    }
    /* Thumb modifiers/layer taps inherit their base binding. */
    if (layer > 0 && position >= 36) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }
    if (pressed) {
        normal_presses[position]++;
    } else {
        normal_releases[position]++;
    }
    if (position == CORNE_LOOKUP_NAV_POSITION) {
        return pressed ? zmk_keymap_layer_activate(1, false)
                       : zmk_keymap_layer_deactivate(1, false);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

#include "pinned_keymap_dispatch.inc"

static int dispatch(struct zmk_position_state_changed event) {
    return zmk_keymap_position_state_changed(event.source, event.position, event.state,
                                              event.timestamp);
}

int __real_raise_zmk_position_state_changed(struct zmk_position_state_changed event) {
    if (capture) {
        assert(captured_count < sizeof(captured) / sizeof(captured[0]));
        captured[captured_count++] = event;
        return 0;
    }
    if (replay_on_next_dispatch) {
        replay_on_next_dispatch = false;
        /* Exactly as hold-tap/combo replay: bypass the typed physical raiser. */
        for (unsigned i = 0; i < captured_count; i++) {
            assert(dispatch(captured[i]) == 0);
        }
        captured_count = 0;
    }
    return dispatch(event);
}

static void raw(unsigned position, bool pressed) {
    struct zmk_position_state_changed event = {
        .source = UINT8_MAX, .position = position, .state = pressed,
    };
    assert(__wrap_raise_zmk_position_state_changed(event) == 0);
}

static void reset(void) {
    lookup = (struct corne_lookup){.key = CORNE_UTILITY_NO_KEY};
    _zmk_keymap_layer_state = 0;
    _zmk_keymap_layer_default = 0;
    memset(zmk_keymap_active_behavior_layer, 0, sizeof(zmk_keymap_active_behavior_layer));
    memset(normal_presses, 0, sizeof(normal_presses));
    memset(normal_releases, 0, sizeof(normal_releases));
    captured_count = 0;
    capture = false;
    replay_on_next_dispatch = false;
    selected_updates = 0;
    finish_replay_work.pending = false;
    assert(lookup_mutex == 0);
}

static void test_deferred_replay_after_physical_all_up(void) {
    reset();
    capture = true;
    raw(CORNE_LOOKUP_NAV_POSITION, true);
    raw(CORNE_LOOKUP_G_POSITION, true);
    raw(6, true);
    raw(6, false);
    raw(CORNE_LOOKUP_G_POSITION, false);
    raw(CORNE_LOOKUP_NAV_POSITION, false);
    capture = false;
    assert(lookup.down == 0);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_OFF);

    /* Replay from a deferred work item, outside every physical wrapper. */
    for (unsigned i = 0; i < captured_count; i++) {
        assert(dispatch(captured[i]) == 0);
    }
    captured_count = 0;
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_ENTERING);
    assert(finish_replay_work.pending);
    assert(normal_presses[6] == 0 && normal_releases[6] == 0);
    assert(normal_presses[CORNE_LOOKUP_NAV_POSITION] == 1);
    assert(normal_releases[CORNE_LOOKUP_NAV_POSITION] == 1);

    /* The next queued work item finalizes entry without any new key event. */
    finish_replay_work.pending = false;
    finish_replay_work.handler(&finish_replay_work);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_ACTIVE);
    assert(_zmk_keymap_layer_state == (UINT32_C(1) << CORNE_LOOKUP_LAYER));
    assert(lookup.down == 0);
    assert(lookup_mutex == 0);
}

static void test_captured_following_press(unsigned following, bool old_modifier) {
    reset();
    if (old_modifier) {
        raw(36, true);
    }
    raw(CORNE_LOOKUP_NAV_POSITION, true);
    capture = true;
    raw(CORNE_LOOKUP_G_POSITION, true);
    raw(following, true);
    raw(following, false);
    raw(CORNE_LOOKUP_G_POSITION, false);
    capture = false;
    replay_on_next_dispatch = true;
    raw(CORNE_LOOKUP_NAV_POSITION, false);
    assert(normal_presses[following] == 0);
    assert(normal_releases[following] == 0);
    assert(normal_presses[CORNE_LOOKUP_NAV_POSITION] == 1);
    assert(normal_releases[CORNE_LOOKUP_NAV_POSITION] == 1);
    assert(_zmk_keymap_layer_state == (UINT32_C(1) << CORNE_LOOKUP_LAYER));
    if (old_modifier) {
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_ENTERING);
        raw(36, false);
        assert(normal_presses[36] == 1);
        assert(normal_releases[36] == 1);
    }
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_ACTIVE);
    assert(lookup.down == 0);
    assert(lookup_mutex == 0);
}

static void test_opaque_layer_and_exit(void) {
    reset();
    raw(CORNE_LOOKUP_NAV_POSITION, true);
    raw(CORNE_LOOKUP_G_POSITION, true);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_ENTERING);
    assert(_zmk_keymap_layer_state & (UINT32_C(1) << CORNE_LOOKUP_LAYER));
    raw(CORNE_LOOKUP_G_POSITION, false);
    raw(CORNE_LOOKUP_NAV_POSITION, false);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_ACTIVE);
    unsigned before[ZMK_KEYMAP_LEN];
    memcpy(before, normal_presses, sizeof(before));
    for (unsigned key = 0; key < 42; key++) {
        if (!corne_utility_physical_position(key)) {
            continue;
        }
        raw(key, true);
        assert(lookup.key == key);
        raw(key, false);
    }
    assert(memcmp(before, normal_presses, sizeof(before)) == 0);
    raw(CORNE_LOOKUP_NAV_POSITION, true);
    raw(CORNE_LOOKUP_G_POSITION, true);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_EXITING);
    raw(25, true); /* Rollover during exit stays display-only. */
    raw(CORNE_LOOKUP_G_POSITION, false);
    raw(CORNE_LOOKUP_NAV_POSITION, false);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_EXITING);
    raw(25, false);
    assert(lookup.mode == CORNE_UTILITY_LOOKUP_OFF);
    assert(_zmk_keymap_layer_state == 0);
    assert(memcmp(before, normal_presses, sizeof(before)) == 0);
    raw(25, true);
    raw(25, false);
    assert(normal_presses[25] == before[25] + 1);
    assert(lookup_mutex == 0);
}

int main(void) {
    /* Pointer, Enter/Shift, macro and Bluetooth-clear positions. Their later
     * captured presses must never reach any underlying behavior after entry.
     */
    const unsigned sensitive[] = {6, 41, 32, 26};
    for (unsigned i = 0; i < sizeof(sensitive) / sizeof(sensitive[0]); i++) {
        test_captured_following_press(sensitive[i], false);
        test_captured_following_press(sensitive[i], true);
    }
    test_opaque_layer_and_exit();
    test_deferred_replay_after_physical_all_up();
    puts("lookup integration: PASS (production driver, pinned ZMK release routing, captured replay)");
    return 0;
}
