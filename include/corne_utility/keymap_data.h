/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CORNE_LEGEND_LAYERS 4
#define CORNE_LEGEND_POSITIONS 42
#define CORNE_PHYSICAL_KEYS_PER_HALF 18
#define CORNE_PROFILE_COUNT 5

enum corne_legend_flags {
    CORNE_LEGEND_UNASSIGNED = 1 << 0,
    CORNE_LEGEND_TRANSPARENT = 1 << 1,
    CORNE_LEGEND_HOLD_TAP = 1 << 2,
    CORNE_LEGEND_MOMENTARY = 1 << 3,
};

/* All strings are ASCII. Detail lines fit an 11-column, 5x7 pixel font.
 * Three-character short labels are for thumbs or a two-line main-key cell.
 * Lowercase a:/o: and uppercase A:/O: represent umlaut macros.
 */
struct corne_key_legend {
    char short_label[4];
    char hold_label[4];
    char lines[3][12];
    uint8_t flags;
};

extern const char *const corne_layer_names[CORNE_LEGEND_LAYERS];
extern const char *const corne_profile_names[CORNE_PROFILE_COUNT];
/* Main rows from left to right, followed by the three thumb keys. */
extern const uint8_t corne_physical_positions[2][CORNE_PHYSICAL_KEYS_PER_HALF];

/* Raw entries preserve transparent and unassigned distinctions. */
const struct corne_key_legend *corne_legend_raw(uint8_t layer, uint8_t position);
/* Resolve the live active-layer stack, always including base layer zero. */
const struct corne_key_legend *corne_legend_resolve(uint8_t layer_mask, uint8_t position);
/* Explain an individual named layer over base, independent of currently held layers. */
const struct corne_key_legend *corne_legend_lookup(uint8_t layer, uint8_t position);
bool corne_legend_is_physical(uint8_t position);
