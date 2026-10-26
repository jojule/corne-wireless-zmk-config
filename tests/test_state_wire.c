/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <corne_utility/state.h>

static void rejected(struct corne_utility_state malformed) {
    struct corne_utility_state decoded;
    memset(&decoded, 0xa5, sizeof(decoded));
    const struct corne_utility_state unchanged = decoded;
    assert(!corne_utility_state_decode(corne_utility_state_param1(&malformed),
                                      corne_utility_state_param2(&malformed), &decoded));
    assert(memcmp(&decoded, &unchanged, sizeof(decoded)) == 0);
}

int main(void) {
    _Static_assert(sizeof(struct corne_utility_state) == 8, "Wire size changed");
    /* Fixed command values catch a mutually wrong encoder/decoder and pin
     * compatibility between the separately built left and right images.
     */
    struct corne_utility_state golden;
    assert(corne_utility_state_decode(UINT32_C(0x1b040f01), UINT32_C(0x002903a5), &golden));
    assert(golden.version == 1 && golden.layer_mask == 15 && golden.profile == 4);
    assert(golden.flags == 0x1b && golden.modifiers == 0xa5 && golden.lookup == 3);
    assert(golden.key == 41 && golden.reserved == 0);
    assert(corne_utility_state_param1(&golden) == UINT32_C(0x1b040f01));
    assert(corne_utility_state_param2(&golden) == UINT32_C(0x002903a5));

    unsigned physical_keys = 0;
    for (unsigned key = 0; key <= UINT8_MAX; key++) {
        if (corne_utility_physical_position(key)) {
            physical_keys++;
        }
    }
    assert(physical_keys == 36);

    unsigned cases = 0;
    for (unsigned key = 0; key <= UINT8_MAX; key++) {
        if (key != CORNE_UTILITY_NO_KEY && !corne_utility_physical_position(key)) {
            continue;
        }
        for (unsigned profile = 0; profile < 5; profile++) {
            for (unsigned layers = 1; layers < 16; layers += 2) {
                for (unsigned lookup = 0; lookup <= CORNE_UTILITY_LOOKUP_EXITING; lookup++) {
                    for (unsigned modifiers = 0; modifiers <= UINT8_MAX; modifiers++) {
                        const struct corne_utility_state expected = {
                            .version = CORNE_UTILITY_STATE_VERSION,
                            .layer_mask = layers,
                            .profile = profile,
                            .flags = CORNE_UTILITY_FLAG_BT_CONNECTED |
                                     CORNE_UTILITY_FLAG_BT_BONDED |
                                     CORNE_UTILITY_FLAG_BLE_SELECTED |
                                     CORNE_UTILITY_FLAG_HOST_CONNECTED,
                            .modifiers = modifiers,
                            .lookup = lookup,
                            .key = key,
                        };
                        struct corne_utility_state actual = {0};
                        assert(corne_utility_state_decode(corne_utility_state_param1(&expected),
                                                         corne_utility_state_param2(&expected),
                                                         &actual));
                        assert(memcmp(&actual, &expected, sizeof(actual)) == 0);
                        cases++;
                    }
                }
            }
        }
    }

    const struct corne_utility_state valid = {
        .version = CORNE_UTILITY_STATE_VERSION,
        .layer_mask = 1,
        .key = CORNE_UTILITY_NO_KEY,
    };
    struct corne_utility_state invalid = valid;
    invalid.version++;
    rejected(invalid);
    invalid = valid;
    invalid.reserved = 1;
    rejected(invalid);
    invalid = valid;
    invalid.profile = 5;
    rejected(invalid);
    invalid = valid;
    invalid.layer_mask = 0;
    rejected(invalid);
    invalid = valid;
    invalid.layer_mask = 0x11; /* The display-only fifth layer is not a keymap legend. */
    rejected(invalid);
    invalid = valid;
    invalid.flags = 0x80;
    rejected(invalid);
    invalid = valid;
    invalid.lookup = 4;
    rejected(invalid);
    for (unsigned key = 0; key < UINT8_MAX; key++) {
        if (!corne_utility_physical_position(key)) {
            invalid = valid;
            invalid.key = key;
            rejected(invalid);
        }
    }
    printf("State wire: %u round trips, 36 physical positions, malformed packet rejection passed.\n",
           cases);
    return 0;
}
