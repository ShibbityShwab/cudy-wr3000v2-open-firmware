#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-ax-twt-impl (shared task task-199)
#
# HOST-ONLY test entry point (no kernel, no device, no toolchain):
#   1. the portable TWT model + its selftest (src/hi5622_ax_twt.{c,h},
#      src/twt_selftest.c): byte-exact frames, field-B derivation, the full
#      fail-closed matrix, the status line;
#   2. the AP-start plan ordering contract (src/plan_order_test.c compiled
#      against the *patched driver tree's* hi5622_scan_logic.c): the twt step
#      is LAST, only for twt 1/3, and dropped with the VAP start;
#   3. the independent Python re-derivation (tools/twt_frame_xcheck.py):
#      a second implementation of the frame bytes/decision table.
#
# The plan-order test needs the patched driver tree (run
# tools/build-twt-impl.sh first, or point TREE= at an equivalent tree); it is
# SKIPPED, loudly, when the tree is absent.  Everything else needs only cc+sh.
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
TREE=${TREE:-/tmp/ax-twt-build}
OUT=${OUT_DIR:-${TMPDIR:-/tmp}/wifi-ax-twt-impl-host}
CC=${CC:-cc}
PYTHON=${PYTHON:-python3}
mkdir -p "$OUT"

echo "== 1. portable model selftest"
"$CC" -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
	-o "$OUT/twt_selftest" \
	"$LANE/src/hi5622_ax_twt.c" "$LANE/src/twt_selftest.c"
"$OUT/twt_selftest" | tee "$OUT/selftest.txt"

echo "== 2. AP-start plan ordering (twt is the last step)"
if [ -f "$TREE/hi5622_scan_logic.c" ] && [ -f "$TREE/hi5622_scan_logic.h" ]; then
	"$CC" -std=c99 -Wall -Wextra -Werror -O2 -I"$TREE" -I"$LANE/src" \
		-o "$OUT/plan_order_test" \
		"$LANE/src/plan_order_test.c" "$TREE/hi5622_scan_logic.c"
	"$OUT/plan_order_test" | tee "$OUT/plan-order.txt"
else
	echo "SKIP: no patched driver tree at $TREE (run tools/build-twt-impl.sh)"
fi

echo "== 3. independent Python re-derivation"
"$PYTHON" "$LANE/tools/twt_frame_xcheck.py" --selftest | tee "$OUT/xcheck.txt"

echo "== HOST TESTS DONE ($OUT)"
