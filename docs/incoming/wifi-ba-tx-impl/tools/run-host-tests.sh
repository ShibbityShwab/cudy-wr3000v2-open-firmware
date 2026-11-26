#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-ba-tx-impl (task-217)
#
# HOST-ONLY: compiles the portable TX-direction BA core (src/hi5622_ba.c)
# with the extended selftest twice:
#   (1) standalone - no kernel, no tree, no vendor artifact;
#   (2) against the patched driver tree's h2d_frames.c, which re-validates
#       every built H2D wire with the exact decode gate hi5622_frame_post()
#       applies before a device write.
# No device access, no network, no vendor binary.
#
# Usage: ./tools/run-host-tests.sh [driver-tree]
#   driver-tree defaults to /tmp/ba-tx-build (built by build-ba-tx.sh); when
#   it is absent part (1) still runs and part (2) is skipped.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
TREE=${1:-/tmp/ba-tx-build}
OUT=${OUT:-/tmp/ba-tx-test}
CC=${CC:-gcc}
mkdir -p "$OUT"

echo "run-host-tests: $LANE"
$CC -std=c99 -Wall -Wextra -Werror -O2 -DHI5622_BA_HOST_TEST \
	-I"$LANE/src" -o "$OUT/ba_tx_selftest" \
	"$LANE/src/hi5622_ba.c" "$LANE/src/ba_tx_selftest.c"
"$OUT/ba_tx_selftest"
N1=$("$OUT/ba_tx_selftest" | sed -n 's/.*: \([0-9][0-9]*\) checks.*/\1/p')

if [ -f "$TREE/h2d_frames.c" ] && [ -f "$TREE/h2d_frames.h" ]; then
	$CC -std=c99 -Wall -Wextra -Werror -O2 -DHI5622_BA_HOST_TEST \
		-DHAVE_H2D_FRAMES -I"$LANE/src" -I"$TREE" \
		-o "$OUT/ba_tx_selftest_h2d" \
		"$LANE/src/hi5622_ba.c" "$LANE/src/ba_tx_selftest.c" \
		"$TREE/h2d_frames.c"
	"$OUT/ba_tx_selftest_h2d"
	N2=$("$OUT/ba_tx_selftest_h2d" | sed -n 's/.*: \([0-9][0-9]*\) checks.*/\1/p')
	echo "run-host-tests: PASS ($N1 standalone + $N2 with the driver H2D gate)"
else
	echo "run-host-tests: PASS ($N1 standalone; H2D-gate part skipped - tree $TREE absent)"
fi
