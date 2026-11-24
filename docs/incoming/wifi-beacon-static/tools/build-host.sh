#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-host.sh - host-only compile + selftest for the wifi-beacon-static lane.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
set +u
if [ -n "$OUT_OVERRIDE" ]; then OUT=$OUT_OVERRIDE; else OUT=/tmp/wifi-beacon-static-build; fi
if [ -n "$CC_OVERRIDE" ]; then CC=$CC_OVERRIDE; else CC=cc; fi
if [ -n "$PY_OVERRIDE" ]; then PY=$PY_OVERRIDE; else PY=python3; fi
set -u
mkdir -p "$OUT"
echo '== C99 strict compile (-Wall -Wextra -Werror -pedantic) =='
$CC -std=c99 -Wall -Wextra -Werror -pedantic -O2 -I"$LANE/src" \
	"$LANE/src/hi5622_ap_frames.c" \
	"$LANE/src/hi5622_scan_logic.c" \
	"$LANE/src/beacon_selftest.c" \
	-o "$OUT/beacon_selftest"
echo '== host selftest =='
"$OUT/beacon_selftest"
echo '== static checks (fail-closed defaults + legacy path preserved) =='
P=$LANE/patches/0001-beacon-static.patch
grep -q '^+static unsigned int hi5622_beacon_static;' "$P" || { echo 'FAIL: beacon_static default is not 0'; exit 1; }
grep -q '^+static unsigned int hi5622_beacon_vap_start = 1u;' "$P" || { echo 'FAIL: beacon_vap_start default'; exit 1; }
grep -q 'haf_build_chan_frame' "$P" || { echo 'FAIL: channel builder missing'; exit 1; }
grep -q 'HI5622_FRAME_CAP_CHAN_CFG' "$P" || { echo 'FAIL: capability missing'; exit 1; }
grep -q 'if (hi5622_beacon_static) {' "$P" || { echo 'FAIL: AP path branch missing'; exit 1; }
echo 'static checks: PASS (beacon_static=0 default, legacy path branch preserved)'
$PY -c 'import ast; ast.parse(open("'$LANE'/tools/beacon_re.py").read()); print("beacon_re.py syntax: PASS")'
echo "build-host: OK ($OUT/beacon_selftest)"
