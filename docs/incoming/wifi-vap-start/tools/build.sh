#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build.sh - host-only build + selftest + frame emit for the wifi-vap-start
# lane.  Compiles with strict flags; exits nonzero on the first real failure.
set -eu
LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${OUT:-/tmp/wifi-vap-start-build}"

CC="${CC:-cc}"
mkdir -p "$OUT"

echo "== compile (C99 -Wall -Wextra -Werror -pedantic) =="
"$CC" -std=c99 -Wall -Wextra -Werror -pedantic -O2 -I"$LANE_DIR/src" \
	"$LANE_DIR/src/vapstart_frames.c" "$LANE_DIR/src/vapstart_selftest.c" \
	-o "$OUT/vapstart_selftest"

echo "== selftest =="
"$OUT/vapstart_selftest"

echo "== emit bench frames =="
"$OUT/vapstart_selftest" --emit-raw

echo "build: OK ($OUT/vapstart_selftest)"
