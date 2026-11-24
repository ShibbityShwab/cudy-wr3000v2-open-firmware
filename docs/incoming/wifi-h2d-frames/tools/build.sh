#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# tools/build.sh - host-only build/verify driver for the wifi-h2d-frames lane.
#
#   1. regenerate the catalogue data tables and fail if they are stale;
#   2. compile the pure-C frame library + selftest with -Werror -pedantic;
#   3. run the device-free selftest suite.
#
# No kernel tree, no image build, no device access, no vendor artifact.
#   ./tools/build.sh
#   CC=clang ./tools/build.sh
set -eo pipefail

LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORK="${WORK:-/tmp/wifi-h2d-frames-build}"
CC="${CC:-cc}"

command -v python3 >/dev/null || { echo "python3 not found" >&2; exit 2; }
command -v "$CC" >/dev/null || { echo "'$CC' not found" >&2; exit 2; }

echo "== 1. catalogue tables"
python3 "$LANE_DIR/tools/gen_catalogue.py"
python3 "$LANE_DIR/tools/gen_catalogue.py" --check

echo
echo "== 2. compile ($CC, C99, -Werror -pedantic)"
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes \
      -o "$WORK/h2d_selftest" \
      "$LANE_DIR/src/h2d_selftest.c" "$LANE_DIR/src/h2d_frames.c"
echo "built: $WORK/h2d_selftest ($(wc -c < "$WORK/h2d_selftest") bytes)"

echo
echo "== 3. selftest"
"$WORK/h2d_selftest"

echo
echo "BUILD OK (host-only; no device contact)"
