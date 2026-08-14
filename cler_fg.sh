#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/tools/flowgraph_gui/app"
npm install
exec npm run tauri dev
