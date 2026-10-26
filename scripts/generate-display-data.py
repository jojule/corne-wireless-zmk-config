#!/usr/bin/env python3
"""Generate readable, display-only legends from the four real keymap layers.

This intentionally accepts the binding vocabulary used by this keymap. Unknown
behaviors/keycodes fail instead of producing a plausible but incorrect legend.
Run with --check during builds; run without it after an intentional keymap edit.
The separate lookup layer is not itself an action layer and is not described.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
LAYER_NODES = ("default_layer", "nav_layer", "sym_layer", "system_layer")
LAYER_NAMES = ("Typing", "Navigate", "Symbols", "System")
PROFILE_NAMES = ('MBP 14"', 'Vision Pro', 'iPhone', 'iPad Pro', 'MBP 16"')
PROFILE_SHORT = ('14', 'VP', 'PH', 'IP', '16')
PHYSICAL = (
    (1, 2, 3, 4, 5, 13, 14, 15, 16, 17, 25, 26, 27, 28, 29, 36, 37, 38),
    (6, 7, 8, 9, 10, 18, 19, 20, 21, 22, 30, 31, 32, 33, 34, 39, 40, 41),
)
UNASSIGNED, TRANSPARENT, HOLD_TAP, MOMENTARY = 1, 2, 4, 8


@dataclass(frozen=True)
class Legend:
    short: str
    hold: str = ""
    lines: tuple[str, ...] = ()
    flags: int = 0

    def __post_init__(self):
        for value, limit in ((self.short, 3), (self.hold, 3)):
            if not value.isascii() or len(value) > limit:
                raise ValueError(f"Invalid short legend: {value!r}")
        if len(self.lines) > 3 or any(not v.isascii() or len(v) > 11 for v in self.lines):
            raise ValueError(f"Detail does not fit display: {self.lines!r}")


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def parse_layers(text: str) -> list[list[str]]:
    """Read DTS binding tokens without preprocessing away symbolic key names."""
    text = strip_comments(text)
    layers = []
    for name in LAYER_NODES:
        matches = re.findall(r"\b" + name + r"\s*\{[^{}]*?\bbindings\s*=\s*<([^>]+)>\s*;", text, re.S)
        if len(matches) != 1:
            raise ValueError(f"Expected one binding list for {name}, found {len(matches)}")
        body = matches[0]
        # Ampersands delimit behavior invocations; each behavior's arity is
        # checked by legend_for_binding, including zero-argument behaviors.
        if body.split("&", 1)[0].strip():
            raise ValueError(f"Unexpected text before first binding in {name}")
        bindings = [" ".join(part.split()) for part in body.split("&")[1:]]
        if len(bindings) != 42:
            raise ValueError(f"{name}: expected 42 bindings, got {len(bindings)}")
        layers.append(bindings)
    return layers


def validate_behavior_definitions(text: str) -> None:
    """Reject changes to custom behavior meaning that the labels do not explain."""
    text = strip_comments(text)
    for name, expected in (("NAV_LAYER", 1), ("SYM_LAYER", 2), ("SYSTEM_LAYER", 3)):
        if not re.search(r"^\s*#define\s+" + name + r"\s+" + str(expected) + r"\s*$", text, re.M):
            raise ValueError(f"{name} no longer maps to display layer {expected}")
    hold_tap = re.search(r"\bht:\s*holdtap\s*\{([^{}]*)\}", text, re.S)
    if not hold_tap or not re.search(r"\bbindings\s*=\s*<\s*&kp\s*>\s*,\s*<\s*&kp\s*>\s*;", hold_tap[1]):
        raise ValueError("Custom ht behavior is no longer key-press/key-press")
    for name, letter in (("ae", "A"), ("oe", "O"), ("cae", "A"), ("coe", "O")):
        macro = re.search(r"\b" + name + r":\s*" + name + r"\s*\{([^{}]*)\}", text, re.S)
        if not macro:
            raise ValueError(f"Missing umlaut macro {name}")
        bindings = re.search(r"\bbindings\s*=\s*(.*?);", macro[1], re.S)
        expected = ["macro_press", "kp LALT", "macro_tap", "kp U", "macro_release", "kp LALT"]
        if name.startswith("c"):
            expected += ["macro_press", "kp LSHFT"]
        expected += ["macro_tap", "kp " + letter]
        if name.startswith("c"):
            expected += ["macro_release", "kp LSHFT"]
        actual = [] if not bindings else [" ".join(part.strip(' <>\n\t,').split()) for part in bindings[1].split('&')[1:]]
        if actual != expected:
            raise ValueError(f"Macro {name} changed; verify its display label before rebuilding")


# Compact labels describe actions, never firmware identifiers. Keep pointer,
# scrolling, cursor, keypad, host-selection and destructive BT clearing distinct.
KEYS: dict[str, tuple[str, tuple[str, ...]]] = {
    "SEMI": (";", (";",)), "COMMA": (",", (",",)), "DOT": (".", (".",)),
    "FSLH": ("/", ("/",)), "BSLH": ("\\", ("\\",)),
    "TILDE": ("~", ("~",)), "GRAVE": ("`", ("`",)), "PIPE": ("|", ("|",)),
    "LBRC": ("{", ("{",)), "RBRC": ("}", ("}",)),
    "LBKT": ("[", ("[",)), "RBKT": ("]", ("]",)),
    "LPAR": ("(", ("(",)), "RPAR": (")", (")",)),
    "EXCL": ("!", ("!",)), "AT": ("@", ("@",)), "HASH": ("#", ("#",)),
    "DLLR": ("$", ("$",)), "PRCNT": ("%", ("%",)), "CARET": ("^", ("^",)),
    "AMPS": ("&", ("&",)), "KP_MULTIPLY": ("K*", ("KEYPAD", "MULTIPLY")),
    "UNDER": ("_", ("_",)), "MINUS": ("-", ("-",)), "PLUS": ("+", ("+",)),
    "EQUAL": ("=", ("=",)), "APOS": ("'", ("'",)), "DQT": ('"', ('"',)),
    "TAB": ("TAB", ("TAB",)), "ESC": ("ESC", ("ESC",)),
    "BSPC": ("BSP", ("BACKSPACE",)), "SPACE": ("SPC", ("SPACE",)),
    "RET": ("RET", ("ENTER",)),
    "LEFT": ("<", ("LEFT ARROW",)), "DOWN": ("v", ("DOWN ARROW",)),
    "UP": ("^", ("UP ARROW",)), "RIGHT": (">", ("RIGHT ARROW",)),
    "HOME": ("HM", ("HOME",)), "END": ("EN", ("END",)),
    "PG_DN": ("PD", ("PAGE DOWN",)), "PG_UP": ("PU", ("PAGE UP",)),
    "LG(TAB)": ("AP", ("CMD + TAB", "SWITCH APP")),
    "LSHFT": ("SH", ("LEFT SHIFT",)), "RSHFT": ("SH", ("RIGHT SHIFT",)),
    "LCTRL": ("CT", ("LEFT CTRL",)), "RCTRL": ("CT", ("RIGHT CTRL",)),
    "LALT": ("AL", ("LEFT ALT",)), "RALT": ("AL", ("RIGHT ALT",)),
    "LGUI": ("CM", ("LEFT CMD",)), "RGUI": ("CM", ("RIGHT CMD",)),
    "C_AC_DESKTOP_SHOW_ALL_WINDOWS": ("WN", ("SHOW ALL", "WINDOWS")),
    "C_BRIGHTNESS_INC": ("B^", ("BRIGHTNESS", "UP")),
    "C_BRIGHTNESS_DEC": ("Bv", ("BRIGHTNESS", "DOWN")),
    "C_VOL_UP": ("V^", ("VOLUME UP",)), "C_VOL_DN": ("Vv", ("VOLUME DOWN",)),
    "C_PLAY_PAUSE": ("PP", ("PLAY /", "PAUSE")),
}
MODIFIERS = {
    "LSHFT": ("SHF", "SHIFT"), "RSHFT": ("SHF", "SHIFT"),
    "LCTRL": ("CTL", "CTRL"), "RCTRL": ("CTL", "CTRL"),
    "LALT": ("ALT", "ALT"), "RALT": ("ALT", "ALT"),
    "LGUI": ("CMD", "CMD"), "RGUI": ("CMD", "CMD"),
}


def keycode_legend(code: str) -> Legend:
    if re.fullmatch(r"[A-Z]", code):
        return Legend(code, lines=(code,))
    if re.fullmatch(r"N[0-9]", code):
        return Legend(code[1], lines=(code[1],))
    if re.fullmatch(r"F(?:[1-9]|1[0-2])", code):
        return Legend(code, lines=(code,))
    if code not in KEYS:
        raise ValueError(f"Unknown keycode {code!r}; add an explicit legend")
    short, lines = KEYS[code]
    return Legend(short, lines=lines)


def legend_for_binding(binding: str) -> Legend:
    words = binding.split()
    behavior, args = words[0], words[1:]
    if behavior == "none" and not args:
        return Legend("--", lines=("UNASSIGNED",), flags=UNASSIGNED)
    if behavior == "trans" and not args:
        return Legend("", lines=("INHERITED",), flags=TRANSPARENT)
    if behavior == "kp" and len(args) == 1:
        return keycode_legend(args[0])
    if behavior == "ht" and len(args) == 2 and args[0] in MODIFIERS:
        tap = keycode_legend(args[1])
        hold_short, hold_name = MODIFIERS[args[0]]
        tap_name = {"BSPC": "BSPC", "RET": "ENTER"}.get(args[1], tap.lines[0])
        return Legend(tap.short, hold_short, (f"TAP {tap_name}", f"HOLD {hold_name}"), HOLD_TAP)
    if behavior == "lt" and args == ["NAV_LAYER", "SPACE"]:
        return Legend("SPC", "NAV", ("TAP SPACE", "HOLD", "NAVIGATE"), HOLD_TAP)
    if behavior == "mo" and args == ["SYM_LAYER"]:
        return Legend("SYM", "SYM", ("HOLD", "SYMBOLS"), MOMENTARY)
    if behavior == "caps_word" and not args:
        return Legend("CW", lines=("CAPS WORD",))
    if behavior == "mkp" and args in (["RCLK"], ["LCLK"]):
        return Legend("RC" if args[0] == "RCLK" else "LC", lines=("RIGHT CLICK" if args[0] == "RCLK" else "LEFT CLICK",))
    directions = {"UP": ("^", "UP"), "DOWN": ("v", "DOWN"), "LEFT": ("<", "LEFT"), "RIGHT": (">", "RIGHT")}
    if behavior == "mmv" and len(args) == 1 and args[0].startswith("MOVE_"):
        short, name = directions[args[0][5:]]
        return Legend("M" + short, lines=("POINTER", name))
    if behavior == "msc" and len(args) == 1 and args[0] in ("SCROLL_UP", "SCROLL_DOWN"):
        short, name = directions[args[0][7:]]
        return Legend("S" + short, lines=("SCROLL", name))
    if behavior == "bt" and len(args) == 2 and args[0] == "BT_SEL":
        slot = int(args[1])
        if not 0 <= slot < len(PROFILE_NAMES):
            raise ValueError(f"Bluetooth slot out of range: {slot}")
        return Legend(PROFILE_SHORT[slot], lines=("SELECT", PROFILE_NAMES[slot]))
    if behavior == "bt" and args == ["BT_CLR"]:
        return Legend("BC", lines=("CLEAR THIS", "BT PAIRING"))
    if behavior == "out" and args == ["OUT_TOG"]:
        return Legend("IO", lines=("TOGGLE", "USB / BT"))
    if behavior in ("ae", "oe", "cae", "coe") and not args:
        upper = behavior.startswith("c")
        letter = "A" if behavior.endswith("ae") else "O"
        return Legend((letter if upper else letter.lower()) + ":", lines=("UPPERCASE" if upper else "LOWERCASE", letter + " UMLAUT", "MACRO"))
    # The lookup entry is the sole new action on an otherwise unused Nav key.
    if behavior == "lookup" and args == ["0"]:
        return Legend("?", lines=("OPEN", "KEY LOOKUP"))
    raise ValueError(f"Unknown binding &{binding}; add an explicit legend")


def c_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def generate(text: str) -> str:
    validate_behavior_definitions(text)
    bindings = parse_layers(text)
    digest = hashlib.sha256(json.dumps(bindings, separators=(",", ":")).encode()).hexdigest()
    rows = [
        "/* SPDX-License-Identifier: MIT */",
        "/* Generated by scripts/generate-display-data.py. Do not edit by hand.",
        f" * Keymap binding SHA256: {digest}",
        " */",
        '#include <corne_utility/keymap_data.h>',
        '#include <stddef.h>',
        '',
        'const char *const corne_layer_names[CORNE_LEGEND_LAYERS] = {' + ', '.join(map(c_string, LAYER_NAMES)) + '};',
        'const char *const corne_profile_names[CORNE_PROFILE_COUNT] = {' + ', '.join(map(c_string, PROFILE_NAMES)) + '};',
        'const uint8_t corne_physical_positions[2][CORNE_PHYSICAL_KEYS_PER_HALF] = {',
        *('    {' + ', '.join(map(str, positions)) + '},' for positions in PHYSICAL),
        '};',
        '',
        'static const struct corne_key_legend legends[CORNE_LEGEND_LAYERS][CORNE_LEGEND_POSITIONS] = {',
    ]
    for layer_name, layer_bindings in zip(LAYER_NAMES, bindings):
        rows.append('    { /* ' + layer_name + ' */')
        for position, binding in enumerate(layer_bindings):
            legend = legend_for_binding(binding)
            lines = list(legend.lines) + [''] * (3 - len(legend.lines))
            rows.append('        {' + c_string(legend.short) + ', ' + c_string(legend.hold) + ', {' + ', '.join(map(c_string, lines)) + '}, ' + str(legend.flags) + '}, /* ' + str(position) + ': &' + binding + ' */')
        rows.append('    },')
    rows += [
        '};',
        '',
        'static const struct corne_key_legend invalid = {"--", "", {"UNASSIGNED", "", ""}, CORNE_LEGEND_UNASSIGNED};',
        '',
        'const struct corne_key_legend *corne_legend_raw(uint8_t layer, uint8_t position) {',
        '    if (layer >= CORNE_LEGEND_LAYERS || position >= CORNE_LEGEND_POSITIONS) {',
        '        return &invalid;',
        '    }',
        '    return &legends[layer][position];',
        '}',
        '',
        'const struct corne_key_legend *corne_legend_resolve(uint8_t layer_mask, uint8_t position) {',
        '    if (position >= CORNE_LEGEND_POSITIONS) { return &invalid; }',
        '    layer_mask |= 1U;',
        '    for (int layer = CORNE_LEGEND_LAYERS - 1; layer >= 0; --layer) {',
        '        const struct corne_key_legend *entry = &legends[layer][position];',
        '        if ((layer_mask & (1U << layer)) && !(entry->flags & CORNE_LEGEND_TRANSPARENT)) {',
        '            return entry;',
        '        }',
        '    }',
        '    return &invalid;',
        '}',
        '',
        'const struct corne_key_legend *corne_legend_lookup(uint8_t layer, uint8_t position) {',
        '    if (layer >= CORNE_LEGEND_LAYERS) { return &invalid; }',
        '    return corne_legend_resolve(1U << layer, position);',
        '}',
        '',
        'bool corne_legend_is_physical(uint8_t position) {',
        '    if (position >= CORNE_LEGEND_POSITIONS) { return false; }',
        '    return position >= 36 || (position % 12 != 0 && position % 12 != 11);',
        '}',
        '',
    ]
    return '\n'.join(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--keymap', type=Path, default=ROOT / 'config/corne.keymap')
    parser.add_argument('--output', type=Path, default=ROOT / 'src/utility/keymap_data.c')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        result = generate(args.keymap.read_text())
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    if args.check:
        if not args.output.exists() or args.output.read_text() != result:
            print('Display legends are stale. Run python3 scripts/generate-display-data.py', file=sys.stderr)
            return 1
        print('Display legends match the four keymap layers.')
    else:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result)
        print(args.output)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
