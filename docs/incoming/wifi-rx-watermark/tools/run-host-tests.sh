#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - host-only selftest for the RX free-ring refill
# watermark (lane wifi-rx-watermark, shared task task-156).
#
#   ./tools/run-host-tests.sh
#
# Builds tools/rx_wm_selftest.c against the committed portable logic copies in
# src/ and runs it.  No kernel, no device, no vendor data.  Expected result:
# 127 checks, 0 failures, RESULT: PASS.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${OUT:-/tmp/rx_wm_selftest}
command -v "$CC" >/dev/null 2>&1 || {
	echo "run-host-tests: C compiler '$CC' not found" >&2
	exit 2
}
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I "$LANE_DIR/src" \
	"$LANE_DIR/tools/rx_wm_selftest.c" "$LANE_DIR/src/hi5622_rx_live.c" \
	-o "$OUT"
exec "$OUT"
