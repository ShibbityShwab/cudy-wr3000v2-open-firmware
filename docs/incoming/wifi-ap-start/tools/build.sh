#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build.sh - host-only build + selftest + frame emit (wifi-ap-start).
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
OUT=/tmp/wifi-ap-start-build
CC=cc
set +u
if [ -n "$OUT_OVERRIDE" ]; then OUT=$OUT_OVERRIDE; fi
if [ -n "$CC_OVERRIDE" ]; then CC=$CC_OVERRIDE; fi
set -u
mkdir -p "$OUT"

echo "== compile (C99 -Wall -Wextra -Werror -pedantic) =="
"$CC" -std=c99 -Wall -Wextra -Werror -pedantic -O2 -I"$LANE_DIR/src" \
	"$LANE_DIR/src/hi5622_ap_frames.c" \
	"$LANE_DIR/src/ap_frames_selftest.c" \
	-o "$OUT/ap_frames_selftest"

echo "== selftest =="
"$OUT/ap_frames_selftest"

echo "== emit frames =="
"$OUT/ap_frames_selftest" --emit > "$OUT/frames.txt"
head -8 "$OUT/frames.txt"
echo "build: OK ($OUT/ap_frames_selftest; frames: $OUT/frames.txt)"
