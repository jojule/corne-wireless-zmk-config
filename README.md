# Corne keyboard
![Picture of my Corne keyboard build](corne.webp)

## Config
![Keyboard layout](layout.svg)

## Kit
[Parts or pre-built](https://typeractive.xyz/pages/build)

## Firmware version

Official ZMK `main`, pinned at
[`4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85`](https://github.com/zmkfirmware/zmk/commit/4dd95d7840eb3d5a3f59bb29fbf1ab054715ef85),
the latest upstream commit checked on 2026-10-25. This is the development branch
with Zephyr 4.1; the latest tagged stable release at that time is v0.3.0.

The upgrade from `urob/mouse-3.2` preserves all 42 positions on all four layers,
the system-layer combo, umlaut macros, Bluetooth profile keys, and mouse
directions and speeds. The custom hold-taps retain their balanced flavor,
300 ms tapping term, and zero quick-tap period. The deprecated
`global-quick-tap` with a zero period becomes the equivalent
`require-prior-idle-ms = <0>`.

Mouse support uses the current `CONFIG_ZMK_POINTING`, `pointing.h`, and `&msc`
names. The hardware target is now `nice_nano@2.0.0//zmk`, with the same Corne
and nice!view shields. See the [ZMK migration guide](https://zmk.dev/blog/2025/12/09/zephyr-4-1)
and [mouse documentation](https://zmk.dev/docs/keymaps/behaviors/mouse-emulation).

## Local build

The local build has been verified on an Apple Silicon Mac. It requires Git,
Docker Desktop with its engine running, internet access, and several GB of free
disk space for the build image and dependencies. Python, CMake, West, and the
Zephyr SDK run inside the pinned Docker image; no host toolchain installation is
needed.

For a fresh checkout:

```sh
git clone https://github.com/jojule/corne-wireless-zmk-config.git
cd corne-wireless-zmk-config
```

From the repository directory, check that Docker is available and compile:

```sh
docker info
./scripts/build-local.sh
```

If `docker info` cannot connect to the daemon, start Docker Desktop and wait for
its engine to be ready before running the build script.

This downloads the pinned official build image and ZMK sources, then compiles both
halves locally. Every run requires network access; subsequent runs reuse
downloaded repositories. Build tools and caches stay in Docker and `.local-build/`;
no host Python packages are installed.
ZMK's imported Zephyr branch can advance between builds; the generated frozen
manifest records the exact dependency commits used for each build.

Outputs:

- `firmware/corne_left.uf2`
- `firmware/corne_right.uf2`
- `firmware/SHA256SUMS`, the frozen dependency manifest, and a config snapshot
- `.local-build/build.log` and per-half build directories

Both output directories are ignored by Git. The script reads the targets from
`build.yaml` and rebuilds each half from a clean build directory. After editing
`config/corne.keymap` or `config/corne.conf`, rerun the same command. Check that the
script exits successfully before using the outputs; a failed run can leave UF2
files from an earlier successful build.

To verify the generated firmware checksums on macOS:

```sh
(cd firmware && shasum -a 256 -c SHA256SUMS)
```

## Flash and check

Connect a half by USB and double-tap its reset button to open its UF2 bootloader
drive. Copy the matching left or right UF2 onto that drive, then repeat for the
other half. Update both halves together.

After flashing, check ordinary typing, Z/Shift and the other hold-taps, layers,
the two-thumb system-layer combo, umlaut macros, mouse movement/scrolling/clicks,
and Bluetooth. If Bluetooth mouse or keyboard reports stop working after the
upgrade, remove the host pairing, clear that keyboard profile using the existing
`BT_CLR` key, and pair again; upstream HID reports have changed since the old fork.

The intermittent Z/Shift and duplicate-letter problems were resolved by replacing
the affected switches after this upgrade. No debounce or hold-tap timing change
was needed. If similar symptoms return, check the switches and socket contacts
before changing hold-tap timing. See [ZMK debouncing](https://zmk.dev/docs/features/debouncing).
