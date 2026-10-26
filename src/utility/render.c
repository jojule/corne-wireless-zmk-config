/* SPDX-License-Identifier: MIT */
#include <corne_utility/render.h>
#include <corne_utility/keymap_data.h>
#include <stdio.h>
#include <string.h>
#include "font5x7.h"

static void pixel(uint8_t *frame, int x, int y) {
    if (x < 0 || x >= CORNE_DISPLAY_WIDTH || y < 0 || y >= CORNE_DISPLAY_HEIGHT) {
        return;
    }
    unsigned int index = (unsigned int)(y * CORNE_DISPLAY_WIDTH + x);
    frame[index / 8] |= 0x80U >> (index % 8);
}

static void text(uint8_t *frame, int x, int y, const char *str, int scale) {
    for (; *str; ++str, x += 6 * scale) {
        unsigned char c = (unsigned char)*str;
        if (c < 32 || c > 126) {
            c = '?';
        }
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if (!(font5x7[c - 32][row] & (16U >> col))) {
                    continue;
                }
                for (int dy = 0; dy < scale; ++dy) {
                    for (int dx = 0; dx < scale; ++dx) {
                        pixel(frame, x + col * scale + dx, y + row * scale + dy);
                    }
                }
            }
        }
    }
}

static void centered(uint8_t *frame, int x, int y, int width, const char *str, int scale) {
    int length = (int)strlen(str);
    int used = length ? (length * 6 - 1) * scale : 0;
    text(frame, x + (width - used) / 2, y, str, scale);
}

static void rule(uint8_t *frame, int y) {
    for (int x = 0; x < CORNE_DISPLAY_WIDTH; ++x) {
        pixel(frame, x, y);
    }
}

static void invert_row(uint8_t *frame, int y, int height) {
    for (int row = y; row < y + height; ++row) {
        for (int x = 0; x < CORNE_DISPLAY_WIDTH; ++x) {
            unsigned int bit = (unsigned int)(row * CORNE_DISPLAY_WIDTH + x);
            frame[bit / 8] ^= 0x80U >> (bit % 8);
        }
    }
}

static void box(uint8_t *frame, int x, int y, int w, int h) {
    for (int dx = 0; dx < w; ++dx) {
        pixel(frame, x + dx, y);
        pixel(frame, x + dx, y + h - 1);
    }
    for (int dy = 0; dy < h; ++dy) {
        pixel(frame, x, y + dy);
        pixel(frame, x + w - 1, y + dy);
    }
}

static void modifiers(uint8_t *frame, uint8_t mods) {
    static const char *const names[] = {"CTL", "SHF", "ALT", "CMD"};
    for (int i = 0; i < 4; ++i) {
        bool held = (mods & ((1U << i) | (1U << (i + 4)))) != 0;
        text(frame, i * 17, 13, held ? names[i] : "---", 1);
    }
}

static void status(uint8_t *frame, const struct corne_render_state *s) {
    const struct corne_utility_state *state = &s->snapshot.state;
    const char *link;
    if (!s->left) {
        link = s->snapshot.split_connected ? "LINK" : "OFF";
    } else if (state->flags & CORNE_UTILITY_FLAG_USB_SELECTED) {
        link = state->flags & CORNE_UTILITY_FLAG_HOST_CONNECTED ? "USB+" : "USB-";
    } else if (state->flags & CORNE_UTILITY_FLAG_BT_CONNECTED) {
        link = "BT+";
    } else {
        link = state->flags & CORNE_UTILITY_FLAG_BT_BONDED ? "BT-" : "PAIR";
    }
    char line[12];
    snprintf(line, sizeof(line), "%c%3u%%%c%s", s->left ? 'L' : 'R',
             s->battery <= 100 ? s->battery : 100, s->usb_powered ? '+' : ' ', link);
    text(frame, 1, 2, line, 1);
    if (!s->snapshot.valid) {
        text(frame, 1, 13, "No state", 1);
    } else if (s->left) {
        const char *host = (state->flags & CORNE_UTILITY_FLAG_USB_SELECTED)
                               ? "USB output"
                               : corne_profile_names[state->profile < CORNE_PROFILE_COUNT
                                                         ? state->profile : 0];
        text(frame, 1, 13, host, 1);
    } else if (state->lookup != CORNE_UTILITY_LOOKUP_OFF) {
        text(frame, 1, 13, "LOOKUP", 1);
    } else {
        modifiers(frame, state->modifiers);
    }
    rule(frame, 23);
}

static void normal(uint8_t *frame, const struct corne_render_state *s) {
    uint8_t mask = s->snapshot.state.layer_mask;
    int layer = 0;
    for (int i = 1; i < CORNE_LEGEND_LAYERS; ++i) {
        if (mask & (1U << i)) {
            layer = i;
        }
    }
    text(frame, 1, 28, corne_layer_names[layer], 1);
    const uint8_t *positions = corne_physical_positions[s->left ? 0 : 1];
    for (int i = 0; i < 18; ++i) {
        const struct corne_key_legend *key = corne_legend_resolve(mask, positions[i]);
        int x, y, width, height;
        if (i < 15) {
            x = 1 + (i % 5) * 13;
            y = 40 + (i / 5) * 20;
            width = 13;
            height = 20;
        } else {
            x = 1 + (i - 15) * 22;
            y = 103;
            width = 22;
            height = 24;
        }
        if (i >= 15) {
            box(frame, x, y, width, height);
        } else {
            /* Two 5px glyphs need the full cell interior. Vertical frames
             * would touch their strokes and obscure F-keys and mouse labels.
             */
            for (int dx = 1; dx < width - 1; ++dx) {
                pixel(frame, x + dx, y + height - 1);
            }
        }
        if (i < 15 && strlen(key->short_label) > 2) {
            char first[2] = {key->short_label[0], '\0'};
            centered(frame, x, y + 2, width, first, 1);
            centered(frame, x, y + 11, width, key->short_label + 1, 1);
        } else {
            centered(frame, x, y + 3, width, key->short_label, 1);
            if (key->flags & CORNE_LEGEND_HOLD_TAP) {
                centered(frame, x, y + 12, width, i < 15 ? "^" : key->hold_label, 1);
            } else if (i >= 15 && key->flags & CORNE_LEGEND_MOMENTARY) {
                centered(frame, x, y + 12, width, "HLD", 1);
            }
        }
    }
    text(frame, 1, 131, "^ = hold", 1);
    text(frame, 1, 141, "Nav+G help", 1);
    text(frame, 1, 151, s->left && !s->snapshot.split_connected ? "Right lost" : "Nav+Sym Sys", 1);
}

static void lookup_picture(uint8_t *frame, uint8_t pos) {
    const struct corne_key_legend *base = corne_legend_lookup(0, pos);
    const struct corne_key_legend *nav = corne_legend_lookup(1, pos);
    const struct corne_key_legend *sym = corne_legend_lookup(2, pos);
    const struct corne_key_legend *sys = corne_legend_lookup(3, pos);
    char location[12];
    if (pos >= 36) {
        snprintf(location, sizeof(location), "%c thumb %u", pos < 39 ? 'L' : 'R',
                 (unsigned int)((pos - 36) % 3 + 1));
    } else {
        unsigned int column = pos % 12;
        snprintf(location, sizeof(location), "%c row%u c%u", column <= 5 ? 'L' : 'R',
                 (unsigned int)(pos / 12 + 1), column <= 5 ? column : column - 5);
    }
    text(frame, 1, 28, location, 1);
    /* Crisp fallback for the SVG's small legends: same central-letter concept,
     * with readable labels and no decorative key frame. The opposite screen
     * explains each layer and all tap/hold details without abbreviations.
     */
    text(frame, 2, 44, base->hold_label, 1);
    centered(frame, 43, 44, 23, sys->short_label, 1);
    int scale = strlen(base->short_label) <= 2 ? 4 : 3;
    centered(frame, 0, 61, 68, base->short_label, scale);
    centered(frame, 1, 99, 28, sym->short_label, 1);
    centered(frame, 39, 99, 28, nav->short_label, 1);
    text(frame, 1, 120, "View only", 1);
    text(frame, 1, 138, "Nav+G exit", 1);
    text(frame, 1, 151, "No typing", 1);
}

static void lookup_details(uint8_t *frame, uint8_t pos) {
    for (int layer = 0; layer < CORNE_LEGEND_LAYERS; ++layer) {
        int y = 26 + layer * 33;
        const struct corne_key_legend *key = corne_legend_lookup(layer, pos);
        const struct corne_key_legend *raw = corne_legend_raw(layer, pos);
        text(frame, 1, y, corne_layer_names[layer], 1);
        if (raw->flags & CORNE_LEGEND_TRANSPARENT) {
            text(frame, 62, y, "=", 1);
        }
        invert_row(frame, y, 7);
        for (int line = 0; line < 3; ++line) {
            text(frame, 1, y + 8 + line * 8, key->lines[line], 1);
        }
    }
}

void corne_utility_render(uint8_t frame[CORNE_FRAME_BYTES],
                          const struct corne_render_state *s) {
    memset(frame, 0, CORNE_FRAME_BYTES);
    status(frame, s);
    if (!s->snapshot.valid) {
        centered(frame, 0, 53, 68, "Waiting for", 1);
        centered(frame, 0, 65, 68, "left half", 1);
        centered(frame, 0, 92, 68, "Map unknown", 1);
        return;
    }
    uint8_t mode = s->snapshot.state.lookup;
    if (mode == CORNE_UTILITY_LOOKUP_OFF) {
        normal(frame, s);
    } else if (mode == CORNE_UTILITY_LOOKUP_ENTERING || mode == CORNE_UTILITY_LOOKUP_EXITING) {
        centered(frame, 0, 36, 68, "LOOKUP", 1);
        centered(frame, 0, 64, 68, "Release", 1);
        centered(frame, 0, 76, 68, "all keys", 1);
        centered(frame, 0, 103, 68, mode == CORNE_UTILITY_LOOKUP_ENTERING ? "Opening" : "Closing", 1);
    } else if (!corne_legend_is_physical(s->snapshot.state.key)) {
        centered(frame, 0, 36, 68, "LOOKUP", 1);
        centered(frame, 0, 64, 68, "Press a key", 1);
        centered(frame, 0, 76, 68, "to inspect", 1);
        centered(frame, 0, 138, 68, "Nav+G exit", 1);
    } else if (s->left) {
        lookup_picture(frame, s->snapshot.state.key);
    } else {
        lookup_details(frame, s->snapshot.state.key);
    }
}
