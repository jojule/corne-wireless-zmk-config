#!/usr/bin/env bash
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_dir"
mkdir -p .local-build/tests .local-build/screens
export PYTHONDONTWRITEBYTECODE=1
python3 scripts/generate-display-data.py --check
python3 -m unittest discover -s tests -p test_display_data.py -v
for name in lookup state_wire; do
  cc -std=c11 -Wall -Wextra -Werror -Iinclude "tests/test_$name.c" \
    -o ".local-build/tests/$name"
  ".local-build/tests/$name"
done
python3 tests/run_lookup_integration.py
cc -std=c11 -Wall -Wextra -Werror -Iinclude tests/test_render.c \
  src/utility/render.c src/utility/keymap_data.c -o .local-build/tests/render
.local-build/tests/render .local-build/screens
