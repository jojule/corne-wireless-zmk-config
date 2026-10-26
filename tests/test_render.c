/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <corne_utility/render.h>
#include <corne_utility/keymap_data.h>

static void save(const char *directory, const char *name, const uint8_t *frame) {
    if (!directory) {
        return;
    }
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pgm", directory, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P5\n68 160\n255\n");
    for (unsigned bit = 0; bit < 68 * 160; ++bit) {
        fputc(frame[bit / 8] & (0x80U >> (bit % 8)) ? 0 : 255, file);
    }
    assert(!fclose(file));
}

int main(int argc, char **argv) {
    struct {
        uint8_t before[16];
        uint8_t frame[CORNE_FRAME_BYTES];
        uint8_t after[16];
    } guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    struct corne_render_state state = {
        .snapshot = {.state = {.version = 1, .layer_mask = 1, .profile = 0,
                              .flags = CORNE_UTILITY_FLAG_BT_CONNECTED |
                                       CORNE_UTILITY_FLAG_BT_BONDED |
                                       CORNE_UTILITY_FLAG_BLE_SELECTED |
                                       CORNE_UTILITY_FLAG_HOST_CONNECTED,
                              .key = CORNE_UTILITY_NO_KEY},
                     .valid = true, .split_connected = true},
        .battery = 82, .left = true,
    };
    unsigned renders = 0;
    for (unsigned side = 0; side < 2; ++side) {
        state.left = side == 0;
        for (unsigned mask = 1; mask < 16; mask += 2) {
            state.snapshot.state.layer_mask = mask;
            for (unsigned mode = 0; mode <= 3; ++mode) {
                state.snapshot.state.lookup = mode;
                for (unsigned pos = 0; pos < 42; ++pos) {
                    state.snapshot.state.key = pos;
                    for (unsigned profile = 0; profile < 5; ++profile) {
                        state.snapshot.state.profile = profile;
                        corne_utility_render(guarded.frame, &state);
                        ++renders;
                    }
                }
            }
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        assert(guarded.before[i] == 0xa5 && guarded.after[i] == 0xa5);
    }

    /* Lost relay state must not leave a misleading live keymap visible. */
    state.left = false;
    state.snapshot.valid = false;
    state.snapshot.split_connected = false;
    state.snapshot.state.layer_mask = 1;
    uint8_t disconnected[CORNE_FRAME_BYTES];
    corne_utility_render(disconnected, &state);
    state.snapshot.state.layer_mask = 15;
    state.snapshot.state.key = 25;
    state.snapshot.state.lookup = CORNE_UTILITY_LOOKUP_ACTIVE;
    corne_utility_render(guarded.frame, &state);
    assert(!memcmp(disconnected, guarded.frame, sizeof(disconnected)));
    save(argc > 1 ? argv[1] : NULL, "right-disconnected", guarded.frame);

    state.snapshot.valid = true;
    state.snapshot.split_connected = true;
    state.snapshot.state.profile = 1;
    state.snapshot.state.lookup = CORNE_UTILITY_LOOKUP_OFF;
    for (unsigned layer = 0; layer < 4; ++layer) {
        state.snapshot.state.layer_mask = 1U | (1U << layer);
        for (unsigned side = 0; side < 2; ++side) {
            state.left = side == 0;
            state.battery = side == 0 ? 82 : 76;
            char name[32];
            snprintf(name, sizeof(name), "%s-layer%u", state.left ? "left" : "right", layer);
            corne_utility_render(guarded.frame, &state);
            save(argc > 1 ? argv[1] : NULL, name, guarded.frame);
        }
    }
    const uint8_t keys[] = {25, 20, 27, 14, 39, 10, 32, 26};
    state.snapshot.state.lookup = CORNE_UTILITY_LOOKUP_ACTIVE;
    for (unsigned i = 0; i < sizeof(keys); ++i) {
        state.snapshot.state.key = keys[i];
        for (unsigned side = 0; side < 2; ++side) {
            state.left = side == 0;
            char name[32];
            snprintf(name, sizeof(name), "%s-key%u", state.left ? "left" : "right", keys[i]);
            corne_utility_render(guarded.frame, &state);
            save(argc > 1 ? argv[1] : NULL, name, guarded.frame);
        }
    }
    printf("Renderer: %u states, canaries, disconnected-state isolation passed.\n", renders);
    return 0;
}
