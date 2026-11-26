#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-band1-start-send-5 (task-213).
# Host-only test rerun against an ALREADY-BUILT R5 tree (default
# /tmp/r5-build).  The full build+test chain is tools/build-r5-order.sh.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
S4=$DOCS/wifi-band1-start-send-4
GATES=$DOCS/wifi-vap-start-gates
[ -n "$WORK" ] || WORK=/tmp/r5-build
[ -n "$ART" ] || ART=/tmp/r5-artifact
[ -n "$TESTCOPY" ] || TESTCOPY=/tmp/r5-testcopy
set -u

[ -f "$WORK/hi5622_scan_logic.c" ] || {
	echo "run-host-tests: tree $WORK absent - run tools/build-r5-order.sh" >&2
	exit 2; }
rm -rf "$TESTCOPY"
mkdir -p "$TESTCOPY/src" "$ART"
cp "$S4/src/vap_start_g0_selftest.c" "$TESTCOPY/src/"
( cd "$TESTCOPY" && patch -p1 --no-backup-if-mismatch < "$GATES/patches/0002-gate-selftest-update.patch" >/dev/null )
( cd "$TESTCOPY" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0002-r5-selftest-update.patch" >/dev/null )

gcc -std=c99 -Wall -Wextra -Werror -O2 -I"$WORK" -I"$TESTCOPY/src" -I"$DOCS/wifi-vap-start/src" -I"$DOCS/wifi-band1-start-send/src" -o "$ART/vap_start_g0_selftest" "$WORK/hi5622_vap_start_g0.c" "$WORK/hi5622_scan_logic.c" "$TESTCOPY/src/vap_start_g0_selftest.c" "$DOCS/wifi-vap-start/src/vapstart_frames.c" "$DOCS/wifi-band1-start-send/src/hi5622_band_start.c"
"$ART/vap_start_g0_selftest"
python3 "$LANE/tools/r5_order_frame_test.py" "$WORK"
