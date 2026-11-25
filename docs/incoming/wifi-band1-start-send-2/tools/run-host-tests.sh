#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-band1-start-send-2 (task-207)
# HOST-ONLY: compiles the portable VAP-index contract model + the REAL
# sibling builders (key 0x0064 and key 0x0242) with a plain C99 host compiler
# and runs the independent Python cross-check.  No device, no firmware.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
TREE=${TREE_OVERRIDE:-/tmp/vapbound-build}
BASE=${BASE_TREE_OVERRIDE:-/tmp/bandstart-build}
WORK=/tmp/vapbound-hosttests
CC=${CC:-gcc}

if [ ! -d "$TREE" ]; then
	if [ -d "$BASE" ]; then
		echo "== patched tree absent - rebuilding it from the pinned base $BASE"
		rm -rf "$WORK" && cp -a "$BASE" "$WORK"
		( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd \
			Module.symvers modules.order build.log 2>/dev/null ) || true
		( cd "$WORK" && patch -p1 --no-backup-if-mismatch \
			< "$LANE/patches/0001-vap-add-index.patch" ) >/dev/null
		TREE=$WORK
	else
		echo "run-host-tests: neither $TREE nor the pinned base $BASE exists" >&2
		echo "run: $LANE/tools/build-vap-bound.sh   (host build, no device)" >&2
		exit 2
	fi
fi

OUT=$WORK
rm -rf "$OUT"; mkdir -p "$OUT"
echo "== 1. compile the model + the real sibling builders"
"$CC" -std=c99 -Wall -Wextra -Werror -O2 \
	-I"$LANE/src" -I"$DOCS/wifi-vap-start/src" \
	-I"$DOCS/wifi-band1-start-send/src" \
	-o "$OUT/vap_index_selftest" \
	"$LANE/src/hi5622_vap_index.c" \
	"$LANE/src/vap_index_selftest.c" \
	"$DOCS/wifi-vap-start/src/vapstart_frames.c" \
	"$DOCS/wifi-band1-start-send/src/hi5622_band_start.c"
echo "== 2. run the C selftest (gate/plan matrix, byte-exact frames)"
"$OUT/vap_index_selftest" | tee "$OUT/selftest.txt"
echo "== 3. independent Python re-derivation + source contract"
python3 "$LANE/tools/vap_add_frame_test.py" "$TREE" | tee "$OUT/pytests.txt"

# Aggregate check counters (the C selftest prints its own count).
C=$(sed -n 's/^vap_index_selftest: \([0-9]*\) checks.*/\1/p' "$OUT/selftest.txt")
P=$(sed -n 's/^vap_add_frame_test: \([0-9]*\) checks.*/\1/p' "$OUT/pytests.txt")
CF=$(sed -n 's/^vap_index_selftest: .*, \([0-9]*\) failures.*/\1/p' "$OUT/selftest.txt")
PF=$(sed -n 's/^vap_add_frame_test: .*, \([0-9]*\) failures.*/\1/p' "$OUT/pytests.txt")
[ -n "$C" ] && [ -n "$P" ] || { echo "run-host-tests: missing check counts" >&2; exit 1; }
T=$((C + P)); F=$((CF + PF))
echo "== TOTAL: $T checks, $F failures (C $C/$CF, Python $P/$PF)"
[ "$F" = 0 ] || exit 1
echo "== ALL HOST TESTS PASS (no device contacted)"
