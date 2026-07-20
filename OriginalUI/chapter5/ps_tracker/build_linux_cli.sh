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
  "$ROOT/main.c" \
  "$ROOT/tracker_app.c" \
  "$ROOT/tracker3d.c" \
  -lm \
  -o "$OUT_DIR/tracker_demo"

echo "Built: $OUT_DIR/tracker_demo"
