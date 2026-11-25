#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-host-tests.sh - device-free host tests for the wifi-rate-ctl lane.
#
#   1. the static decode selftest against the local vendor module (SKIP when
#      the local artifact is absent; the module is never committed);
#   2. the portable logic selftest (wire KATs + config-sync parse + fail-closed
#      matrix), compiled from src/ only;
#   3. the frame cross-check: apply patches/0001-rate-ctl.patch to a staged
#      copy of the wifi-h2d-send src tree and assert that the rate-control
#      logic and the driver's exact frame library agree byte-for-byte on the
#      g0 id2 / key-0x0101 config event (header, blob, SR node word).
#
# No kernel tree, no device access, no vendor artifact is written anywhere.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
H2D=$DOCS/wifi-h2d-send/src
PATCH=$LANE/patches/0001-rate-ctl.patch
if [ -z "$WORK" ]; then WORK=/tmp/wifi-rate-ctl-host; fi
if [ -z "$CC" ]; then CC=cc; fi

echo "== 1. static decode selftest (vendor module must exist locally) =="
if [ -z "$HI5622_WIFI_KO" ]; then
	HI5622_WIFI_KO=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
fi
if [ -f "$HI5622_WIFI_KO" ]; then
	python3 "$LANE/tools/decode_alg_engine.py" --selftest
else
	echo "SKIP: vendor module not present at $HI5622_WIFI_KO"
fi

echo
echo "== 2. portable logic selftest =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes -I"$LANE/src" \
	-o "$WORK/rate_ctl_logic_selftest" \
	"$LANE/src/rate_ctl_logic_selftest.c" "$LANE/src/hi5622_rate_ctl_logic.c"
"$WORK/rate_ctl_logic_selftest"

echo
echo "== 3. frame cross-check against the patched driver tree =="
[ -d "$H2D" ] || { echo "SKIP: wifi-h2d-send src tree not present at $H2D"; exit 0; }
[ -f "$PATCH" ] || { echo "missing patch $PATCH" >&2; exit 2; }
mkdir -p "$WORK/stage/src"
for f in "$H2D"/*.c "$H2D"/*.h "$H2D"/Kbuild "$H2D"/*.inc; do
	[ -f "$f" ] && cp "$f" "$WORK/stage/src/"
done
patch -p1 --batch --forward -s -d "$WORK/stage" -i "$PATCH"
echo "files touched: $(grep -c '^+++' "$PATCH")"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes \
	-I"$WORK/stage/src" -I"$LANE/src" \
	-o "$WORK/frame_crosscheck_selftest" \
	"$LANE/tools/frame_crosscheck_selftest.c" \
	"$WORK/stage/src/hi5622_rate_ctl_logic.c" \
	"$WORK/stage/src/h2d_send_logic.c" \
	"$WORK/stage/src/h2d_frames.c" \
	"$WORK/stage/src/hcc_core.c"
"$WORK/frame_crosscheck_selftest"

echo
echo "OK: host tests PASS (decode + logic + frame cross-check)"