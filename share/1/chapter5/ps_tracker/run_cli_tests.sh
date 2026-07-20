#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN="$ROOT/build_linux/tracker_demo"
OUT_DIR="$ROOT/build_linux/results"
mkdir -p "$OUT_DIR"

if [ ! -x "$BIN" ]; then
  echo "Binary not found, building first..."
  "$ROOT/build_linux_cli.sh"
fi

echo "== Case 1: TDOA only =="
"$BIN" --scene straight --tdoa --steps 80 --seed 1 --csv "$OUT_DIR/straight_tdoa.csv"
echo

echo "== Case 2: AOA only =="
"$BIN" --scene turn --aoa --steps 60 --seed 123 --csv "$OUT_DIR/turn_aoa.csv"
echo

echo "== Case 3: TOA only =="
"$BIN" --scene climb --toa --steps 40 --seed 7 --csv "$OUT_DIR/climb_toa.csv"
echo

echo "== Case 4: RSS only =="
"$BIN" --scene climb --rss --steps 40 --seed 9 --csv "$OUT_DIR/climb_rss.csv"
echo

echo "All CLI tests finished."
echo "CSV outputs are in: $OUT_DIR"
