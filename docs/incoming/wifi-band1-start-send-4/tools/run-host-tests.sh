#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-band1-start-send-4 (task-211)
# HOST-ONLY: compiles the R4b-patched tree's real models (the key-0x0065
# builder/gates, the AP-start plan with the 0x0124-before-0x0065 order and the
# 0x0242 band-start model) together with the REAL sibling builders (key 0x0065
# from ../wifi-vap-start, key 0x0242 from ../wifi-band1-start-send) and runs
# the independent Python cross-check.  No device, no firmware, no wire.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
TREE=${TREE_OVERRIDE:-/tmp/r4border-build}
WORK=/tmp/r4border-hosttests
CC=${CC:-gcc}

if [ ! -d "$TREE" ]; then
	echo "run-host-tests: R4b-patched tree $TREE absent" >&2
	echo "run: $LANE/tools/build-r4b-order.sh   (host build, no device)" >&2
	exit 2
fi

rm -rf "$WORK"; mkdir -p "$WORK"
echo "== 1. compile the real R4b-patched-tree models + the real sibling builders"
"$CC" -std=c99 -Wall -Wextra -Werror -O2 \
	-I"$TREE" -I"$LANE/src" \
	-I"$DOCS/wifi-vap-start/src" \
	-I"$DOCS/wifi-band1-start-send/src" \
	-o "$WORK/vap_start_g0_selftest" \
	"$TREE/hi5622_vap_start_g0.c" \
	"$TREE/hi5622_scan_logic.c" \
	"$LANE/src/vap_start_g0_selftest.c" \
	"$DOCS/wifi-vap-start/src/vapstart_frames.c" \
	"$DOCS/wifi-band1-start-send/src/hi5622_band_start.c"
echo "== 2. run the C selftest (KAT, R4b plan order, fail-closed matrix, G8/G12/G5)"
"$WORK/vap_start_g0_selftest" | tee "$WORK/selftest.txt"
echo "== 3. independent Python re-derivation + source contract (R4b order)"
python3 "$LANE/tools/r4b_order_frame_test.py" "$TREE" | tee "$WORK/pytests.txt"

C=$(sed -n 's/^vap_start_g0_selftest: \([0-9]*\) checks.*/\1/p' "$WORK/selftest.txt")
P=$(sed -n 's/^r4b_order_frame_test: \([0-9]*\) checks.*/\1/p' "$WORK/pytests.txt")
CF=$(sed -n 's/^vap_start_g0_selftest: .*, \([0-9]*\) failures.*/\1/p' "$WORK/selftest.txt")
PF=$(sed -n 's/^r4b_order_frame_test: .*, \([0-9]*\) failures.*/\1/p' "$WORK/pytests.txt")
[ -n "$C" ] && [ -n "$P" ] || { echo "run-host-tests: missing check counts" >&2; exit 1; }
T=$((C + P)); F=$((CF + PF))
echo "== TOTAL: $T checks, $F failures (C $C/$CF, Python $P/$PF)"
[ "$F" = 0 ] || exit 1
echo "== ALL HOST TESTS PASS (no device contacted)"
