#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-band1-start-send-3 (task-209)
# HOST-ONLY: compiles the patched tree's real models (the key-0x0065
# builder + gates and the AP-start plan) together with the REAL sibling
# builders (key 0x0065 from ../wifi-vap-start, key 0x0242 from
# ../wifi-band1-start-send) and runs the independent Python cross-check.
# No device, no firmware, no wire.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
TREE=${TREE_OVERRIDE:-/tmp/g0start-build}
WORK=/tmp/g0start-hosttests
CC=${CC:-gcc}

if [ ! -d "$TREE" ]; then
	echo "run-host-tests: patched tree $TREE absent" >&2
	echo "run: $LANE/tools/build-vap-start-g0.sh   (host build, no device)" >&2
	exit 2
fi

rm -rf "$WORK"; mkdir -p "$WORK"
echo "== 1. compile the real patched-tree models + the real sibling builders"
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
echo "== 2. run the C selftest (KAT, plan order, fail-closed matrix, G5/G12)"
"$WORK/vap_start_g0_selftest" | tee "$WORK/selftest.txt"
echo "== 3. independent Python re-derivation + source contract"
python3 "$LANE/tools/vap_start_g0_frame_test.py" "$TREE" | tee "$WORK/pytests.txt"

C=$(sed -n 's/^vap_start_g0_selftest: \([0-9]*\) checks.*/\1/p' "$WORK/selftest.txt")
P=$(sed -n 's/^vap_start_g0_frame_test: \([0-9]*\) checks.*/\1/p' "$WORK/pytests.txt")
CF=$(sed -n 's/^vap_start_g0_selftest: .*, \([0-9]*\) failures.*/\1/p' "$WORK/selftest.txt")
PF=$(sed -n 's/^vap_start_g0_frame_test: .*, \([0-9]*\) failures.*/\1/p' "$WORK/pytests.txt")
[ -n "$C" ] && [ -n "$P" ] || { echo "run-host-tests: missing check counts" >&2; exit 1; }
T=$((C + P)); F=$((CF + PF))
echo "== TOTAL: $T checks, $F failures (C $C/$CF, Python $P/$PF)"
[ "$F" = 0 ] || exit 1
echo "== ALL HOST TESTS PASS (no device contacted)"
