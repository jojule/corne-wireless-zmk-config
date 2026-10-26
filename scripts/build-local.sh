#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Official ARM-host image used for the verified build; includes Zephyr SDK 0.16.9.
image='zmkfirmware/zmk-dev-arm:4.1-branch@sha256:77b62cbc6c62ca0026ed9eb511dd7fc2a13692d1b9ddbaf92bca510612390db8'
mkdir -p "$repo_dir/.local-build" "$repo_dir/firmware"

docker run --rm -i \
  --mount "type=bind,source=$repo_dir,target=/workspace" \
  --workdir /workspace/.local-build \
  --entrypoint /bin/bash "$image" -s 2>&1 <<'BUILD' | tee "$repo_dir/.local-build/build.log"
set -euo pipefail
mkdir -p config
cp /workspace/config/west.yml config/west.yml
if [ ! -f .west/config ]; then
  west init -l config
fi
west update --narrow -o=--depth=1
west zephyr-export

python3 - <<'PY' > targets.tsv
import yaml
with open('/workspace/build.yaml') as source:
    for target in yaml.safe_load(source)['include']:
        print(target['board'], target['shield'], sep='\t')
PY

while IFS=$'\t' read -r board shield; do
  name="${shield%% *}"
  west build -p always -s zmk/app -d "build/$name" -b "$board" -- \
    -DZMK_CONFIG=/workspace/config -DSHIELD="$shield"
done < targets.tsv

# Publish artifacts only after every target has built successfully.
while IFS=$'\t' read -r board shield; do
  name="${shield%% *}"
  cp "build/$name/zephyr/zmk.uf2" "/workspace/firmware/$name.uf2"
done < targets.tsv
west manifest --freeze --active-only > /workspace/firmware/west-manifest.yml
mkdir -p /workspace/firmware/config
cp /workspace/config/* /workspace/firmware/config/
cp /workspace/build.yaml /workspace/firmware/build.yaml
git -C zmk rev-parse HEAD > /workspace/firmware/zmk-revision.txt
cd /workspace/firmware
sha256sum ./*.uf2 > SHA256SUMS
cat SHA256SUMS
BUILD

printf '\nFirmware: %s/firmware\nBuild log: %s/.local-build/build.log\n' "$repo_dir" "$repo_dir"
