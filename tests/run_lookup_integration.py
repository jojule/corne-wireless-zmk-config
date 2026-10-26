#!/usr/bin/env python3
"""Run the production lookup driver against pinned ZMK's actual dispatch loop."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
source = root / ".local-build/zmk/app/src/keymap.c"
if not source.exists():
    raise SystemExit("Run the local firmware build first to download pinned ZMK sources.")

text = source.read_text()
start = text.index("int zmk_keymap_position_state_changed(")
end = text.index("\n#if ZMK_KEYMAP_HAS_SENSORS", start)
dispatch = text[start:end]
out = root / ".local-build/lookup-integration"
out.mkdir(parents=True, exist_ok=True)
(out / "pinned_keymap_dispatch.inc").write_text(dispatch)
for header in ["drivers/behavior.h", "zephyr/kernel.h", "zephyr/logging/log.h",
               "zmk/behavior.h", "zmk/events/position_state_changed.h", "zmk/keymap.h"]:
    path = out / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "lookup_stubs.h"\n')

subprocess.run([
    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
    "-I", str(out), "-I", str(root / "tests"), "-I", str(root / "include"),
    str(root / "tests/test_lookup_integration.c"), "-o", str(out / "test"),
], check=True)
subprocess.run([str(out / "test")], check=True)
