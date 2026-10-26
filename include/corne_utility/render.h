/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <corne_utility/state.h>

#define CORNE_DISPLAY_WIDTH 68
#define CORNE_DISPLAY_HEIGHT 160
#define CORNE_FRAME_BYTES (CORNE_DISPLAY_WIDTH * CORNE_DISPLAY_HEIGHT / 8)

struct corne_render_state {
    struct corne_utility_snapshot snapshot;
    uint8_t battery;
    bool usb_powered;
    bool left;
};

/* Portrait, packed MSB-first, set bit = black. No LVGL or Zephyr dependencies. */
void corne_utility_render(uint8_t frame[CORNE_FRAME_BYTES],
                          const struct corne_render_state *state);
