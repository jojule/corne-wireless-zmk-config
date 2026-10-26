/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>

#include <corne_utility/lookup.h>

static struct corne_lookup fresh(void) {
    return (struct corne_lookup){.key = CORNE_UTILITY_NO_KEY};
}

static enum corne_lookup_transition physical(struct corne_lookup *lookup, unsigned position,
                                              bool pressed, bool should_dispatch) {
    assert(corne_lookup_before_event(lookup, position, pressed) == should_dispatch);
    return corne_lookup_after_event(lookup);
}

static void enter(struct corne_lookup *lookup, bool nav_released_first) {
    assert(physical(lookup, CORNE_LOOKUP_NAV_POSITION, true, true) == CORNE_LOOKUP_UNCHANGED);
    assert(corne_lookup_before_event(lookup, CORNE_LOOKUP_G_POSITION, true));
    corne_lookup_request_entry(lookup);
    assert(corne_lookup_after_event(lookup) == CORNE_LOOKUP_UNCHANGED);
    assert(lookup->mode == CORNE_UTILITY_LOOKUP_ENTERING);
    unsigned first = nav_released_first ? CORNE_LOOKUP_NAV_POSITION : CORNE_LOOKUP_G_POSITION;
    unsigned last = nav_released_first ? CORNE_LOOKUP_G_POSITION : CORNE_LOOKUP_NAV_POSITION;
    assert(physical(lookup, first, false, true) == CORNE_LOOKUP_UNCHANGED);
    /* Old release must execute before layer activation. */
    assert(corne_lookup_before_event(lookup, last, false));
    assert(lookup->mode == CORNE_UTILITY_LOOKUP_ENTERING);
    assert(corne_lookup_after_event(lookup) == CORNE_LOOKUP_ENTER);
    assert(lookup->mode == CORNE_UTILITY_LOOKUP_ACTIVE);
}

static void test_entry_preserves_old_releases(void) {
    for (unsigned order = 0; order < 2; order++) {
        struct corne_lookup lookup = fresh();
        /* A pre-existing held modifier must keep its original release route. */
        physical(&lookup, 36, true, true);
        physical(&lookup, CORNE_LOOKUP_NAV_POSITION, true, true);
        physical(&lookup, CORNE_LOOKUP_G_POSITION, true, true);
        corne_lookup_request_entry(&lookup);
        unsigned first = order ? CORNE_LOOKUP_NAV_POSITION : CORNE_LOOKUP_G_POSITION;
        unsigned last = order ? CORNE_LOOKUP_G_POSITION : CORNE_LOOKUP_NAV_POSITION;
        assert(physical(&lookup, first, false, true) == CORNE_LOOKUP_UNCHANGED);
        assert(physical(&lookup, last, false, true) == CORNE_LOOKUP_UNCHANGED);
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_ENTERING);
        assert(corne_lookup_before_event(&lookup, 36, false));
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_ENTERING);
        assert(corne_lookup_after_event(&lookup) == CORNE_LOOKUP_ENTER);
    }
}

static void test_entry_rollover_is_swallowed(void) {
    struct corne_lookup lookup = fresh();
    physical(&lookup, CORNE_LOOKUP_NAV_POSITION, true, true);
    physical(&lookup, CORNE_LOOKUP_G_POSITION, true, true);
    corne_lookup_request_entry(&lookup);
    /* New presses cannot invoke BT_CLR, a macro, mouse, or an ordinary key. */
    const unsigned rollover[] = {26, 32, 6, 1};
    for (unsigned i = 0; i < sizeof(rollover) / sizeof(rollover[0]); i++) {
        assert(physical(&lookup, rollover[i], true, false) == CORNE_LOOKUP_UNCHANGED);
        /* Duplicate down cannot accidentally reopen the original release route. */
        assert(physical(&lookup, rollover[i], true, false) == CORNE_LOOKUP_UNCHANGED);
    }
    physical(&lookup, CORNE_LOOKUP_G_POSITION, false, true);
    physical(&lookup, CORNE_LOOKUP_NAV_POSITION, false, true);
    for (unsigned i = 0; i < sizeof(rollover) / sizeof(rollover[0]); i++) {
        enum corne_lookup_transition expected = i == 3 ? CORNE_LOOKUP_ENTER : CORNE_LOOKUP_UNCHANGED;
        assert(physical(&lookup, rollover[i], false, false) == expected);
    }
    assert(lookup.swallowed == 0);
    assert(lookup.down == 0);
}

static void test_all_keys_can_be_inspected(void) {
    struct corne_lookup lookup = fresh();
    enter(&lookup, false);
    unsigned count = 0;
    for (unsigned position = 0; position < 42; position++) {
        if (!corne_utility_physical_position(position)) {
            continue;
        }
        count++;
        assert(corne_lookup_before_event(&lookup, position, true));
        /* The highest layer invokes only this selection action. */
        corne_lookup_select(&lookup, position);
        assert(lookup.key == position);
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_ACTIVE);
        assert(corne_lookup_after_event(&lookup) == CORNE_LOOKUP_UNCHANGED);
        assert(physical(&lookup, position, false, true) == CORNE_LOOKUP_UNCHANGED);
        assert(lookup.key == position); /* Keep the last key visible after release. */
    }
    assert(count == 36);
}

static void test_exit_release_orders_and_rollover(void) {
    for (unsigned order = 0; order < 2; order++) {
        struct corne_lookup lookup = fresh();
        enter(&lookup, order);
        unsigned first = order ? CORNE_LOOKUP_NAV_POSITION : CORNE_LOOKUP_G_POSITION;
        unsigned last = order ? CORNE_LOOKUP_G_POSITION : CORNE_LOOKUP_NAV_POSITION;
        physical(&lookup, first, true, true);
        corne_lookup_select(&lookup, first);
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_ACTIVE);
        physical(&lookup, last, true, true);
        corne_lookup_select(&lookup, last);
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_EXITING);

        /* Rollover continues through display-only bindings until all keys lift. */
        physical(&lookup, 25, true, true);
        corne_lookup_select(&lookup, 25);
        assert(lookup.key == last); /* No selection flicker while exit is pending. */
        assert(physical(&lookup, first, false, true) == CORNE_LOOKUP_UNCHANGED);
        assert(physical(&lookup, last, false, true) == CORNE_LOOKUP_UNCHANGED);
        assert(corne_lookup_before_event(&lookup, 25, false));
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_EXITING);
        assert(corne_lookup_after_event(&lookup) == CORNE_LOOKUP_EXIT);
        assert(lookup.mode == CORNE_UTILITY_LOOKUP_OFF);
        assert(lookup.key == CORNE_UTILITY_NO_KEY);
        assert(physical(&lookup, 25, true, true) == CORNE_LOOKUP_UNCHANGED);
        assert(physical(&lookup, 25, false, true) == CORNE_LOOKUP_UNCHANGED);
    }
}

static void test_invalid_positions_do_not_block_exit(void) {
    struct corne_lookup lookup = fresh();
    for (unsigned position = 0; position < 42; position++) {
        if (!corne_utility_physical_position(position)) {
            physical(&lookup, position, true, true);
            assert(lookup.down == 0);
        }
    }
    physical(&lookup, UINT32_MAX, true, true);
    assert(lookup.down == 0);
}

int main(void) {
    test_entry_preserves_old_releases();
    test_entry_rollover_is_swallowed();
    test_all_keys_can_be_inspected();
    test_exit_release_orders_and_rollover();
    test_invalid_positions_do_not_block_exit();
    puts("lookup transitions: PASS (36 keys, both release orders, rollover, old releases)");
    return 0;
}
