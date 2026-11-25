#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-host-tests.sh - host-only selftest for the wifi-rx-vendor-diff lane
# (shared task task-162).  Compiles the portable parity/observation logic and
# the vendor RX-init program model; no kernel, no device, no vendor artifact.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC=$LANE_DIR/src
OUT=${OUT:-/tmp/rx-vendor-diff-host-test}
CC=${CC:-gcc}
mkdir -p "$OUT"
$CC -std=c99 -Wall -Wextra -Werror -pedantic -O2 -I "$SRC" \
    -o "$OUT/rx_parity_selftest" \
    "$SRC/rx_parity_selftest.c" "$SRC/hi5622_rx_diff.c" \
    "$SRC/hi5622_rx_live.c" "$SRC/dp_rings.c"
"$OUT/rx_parity_selftest" | tee "$OUT/host-test.txt"
grep -q 'RESULT: PASS' "$OUT/host-test.txt"
echo "host test log: $OUT/host-test.txt"
