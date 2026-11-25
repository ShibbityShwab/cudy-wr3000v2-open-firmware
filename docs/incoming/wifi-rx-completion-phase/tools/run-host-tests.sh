#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-host-tests.sh - build + run the device-free completion-phase host test
# (lane wifi-rx-completion-phase, task-168).  No kernel, no device.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
CFLAGS="-std=c99 -Wall -Wextra -Werror -pedantic -O2"
OUT=${OUT:-${TMPDIR:-/tmp}/rx_cmp_phase_selftest}
echo "== compile: $CC $CFLAGS"
$CC $CFLAGS -I"$LANE_DIR/src" -o "$OUT" \
	"$LANE_DIR/tools/rx_cmp_phase_selftest.c" \
	"$LANE_DIR/src/hi5622_rx_cmp_phase.c"
echo "== run"
"$OUT"
