#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$ROOT/build_linux"
mkdir -p "$OUT_DIR"

cc \
  -std=c11 \
  -O2 \
  -Wall \
  -Wextra \
  -pedantic \
  "$ROOT/fb_linux_main.c" \
  "$ROOT/tracker3d.c" \
  "$ROOT/tracker_app.c" \
  "$ROOT/ui_draw.c" \
  -lm \
  -o "$OUT_DIR/fb_tracker_ui"

echo "Built: $OUT_DIR/fb_tracker_ui"
