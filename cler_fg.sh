#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/tools/cler-fg/app"
npm install
exec npm run tauri dev
