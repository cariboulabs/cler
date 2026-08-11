#!/usr/bin/env bash
# Regenerates app/src/fixtures/*.json from their source .cpp files.
# Run from anywhere; paths are resolved relative to the repo root.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
gui_dir="$repo_root/tools/flowgraph_gui"
manifest="$gui_dir/cler-graph/Cargo.toml"
fixtures_dir="$gui_dir/app/src/fixtures"

declare -A sources=(
  [adsb_receiver]=desktop_examples/adsb_receiver.cpp
  [hello_world]=desktop_examples/hello_world.cpp
  [mass_spring_damper]=desktop_examples/mass_spring_damper.cpp
  [plots]=desktop_examples/plots.cpp
  [polyphase_channelizer]=desktop_examples/polyphase_channelizer.cpp
  [spike]=desktop_examples/spike.cpp
  [uhd_device]=desktop_examples/uhd_device.cpp
  [type_conflict]=tools/flowgraph_gui/cler-graph/tests/data/type_conflict.cpp
)

cd "$repo_root"
cargo build -q --manifest-path "$manifest" --bin cler-graph

for name in "${!sources[@]}"; do
  src="${sources[$name]}"
  cargo run -q --manifest-path "$manifest" --bin cler-graph -- \
    parse "$src" --pretty --palette desktop_blocks > "$fixtures_dir/$name.json"
  echo "regenerated $name.json from $src"
done
