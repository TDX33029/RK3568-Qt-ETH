#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"

cd "$ROOT"
chmod +x build_linux_fb.sh
./build_linux_fb.sh

echo
echo "Starting framebuffer UI..."
echo "Controls: W/S move, A/D change, Enter run, M menu, R rerun, Q quit"
echo

./build_linux/fb_tracker_ui
