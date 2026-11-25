#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - host test for the portable MAC RX ring-entry +
# 36-byte device-header format model (lane wifi-rx-format, task-163).
# No kernel, no device, no vendor bytes: plain C99 + this lane's src/.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${OUT:-/tmp/rxfmt-selftest}
"$CC" -std=c99 -Wall -Wextra -Werror -O2 \
	-o "$OUT" "$LANE_DIR/src/hi5622_rx_format.c" \
	"$LANE_DIR/tools/rx_format_selftest.c"
"$OUT"
