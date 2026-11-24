#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+module verification for the wifi-scan-tuning
# lane (shared task-133).  Device-free; no vendor byte is read or copied.
#
#   1. assemble a build tree: BASE_TREE (the pinned combined AP+DP tree that
#      produced the fired 44c6cc12 module) + this lane's four-file delta (src/);
#   2. host selftests: new scan-tuning suite + inherited scan-complete,
#      scan-logic, cfg80211 and H2D regressions;
#   3. out-of-tree kernel module build against the project 6.18.60 tree and
#      artifact reporting (stripped size/sha256/vermagic/imports/warnings/params);
#   4. request-dump regeneration (the 520-B g0 id0xC frame of recipes/).
#
# Env overrides: BASE_TREE, KDIR, STAGING_DIR, CROSS_COMPILE.
set -o pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
BASE_TREE=${BASE_TREE:-/tmp/wifi-combined-build}
WORK=${WORK:-/tmp/wifi-scan-tuning-build}
KDIR=${KDIR:-/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}
STAGING_DIR=${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir}
CROSS_COMPILE=${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
CC=${CC:-cc}
export LC_ALL=C STAGING_DIR
die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$BASE_TREE" ] || die "BASE_TREE not found: $BASE_TREE (set BASE_TREE to the pinned combined tree)"
[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "$CROSS_COMPILE"gcc ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"
command -v "$CC" >/dev/null || die "host compiler not found: $CC"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$BASE_TREE"/*.c "$BASE_TREE"/*.h "$BASE_TREE"/Kbuild \
   "$BASE_TREE"/Makefile "$BASE_TREE"/*.inc "$BASE_TREE"/*.symvers "$WORK"/ 2>/dev/null || true
cp "$LANE"/src/*.c "$LANE"/src/*.h "$WORK"/ || die "src/ overlay failed"

HOST_CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -I$WORK"
echo "== 1. host scan-tuning selftest =="
"$CC" $HOST_CFLAGS -o "$WORK/tuning_test" "$LANE/tools/scan_tuning_selftest.c" "$WORK/hi5622_scan_logic.c" || die "tuning selftest compile"
"$WORK/tuning_test" || die "tuning selftest failed"
echo
echo "== 2. inherited host regressions =="
"$CC" $HOST_CFLAGS -o "$WORK/complete_test" "$LANE/tools/scan_complete_selftest.c" "$WORK/hi5622_scan_logic.c" "$WORK/hi5622_scan_d2h.c" "$WORK/hi5622_vapscan_map.c" "$WORK/vapscan_frames.c" || die "scan-complete selftest compile"
"$WORK/complete_test" | tail -1 || die "scan-complete selftest failed"
"$CC" $HOST_CFLAGS -o "$WORK/logic_test" "$LANE/tools/scan_logic_selftest.c" "$WORK/hi5622_scan_logic.c" "$WORK/hi5622_vapscan_map.c" "$WORK/hi5622_scan_d2h.c" "$WORK/vapscan_frames.c" "$WORK/h2d_frames.c" || die "scan-logic selftest compile"
"$WORK/logic_test" | tail -1 || die "scan-logic selftest failed"
"$CC" $HOST_CFLAGS -o "$WORK/cfg_test" "$LANE/tools/cfg80211_logic_selftest.c" "$WORK/hi5622_cfg80211_logic.c" || die "cfg80211 selftest compile"
"$WORK/cfg_test" | tail -1 || die "cfg80211 selftest failed"
"$CC" $HOST_CFLAGS -o "$WORK/h2d_test" "$LANE/tools/h2d_logic_selftest.c" "$WORK/h2d_send_logic.c" "$WORK/h2d_frames.c" "$WORK/hcc_core.c" || die "h2d selftest compile"
"$WORK/h2d_test" | tail -1 || die "h2d selftest failed"

echo
echo "== 3. kernel module build =="
LOG=/tmp/hi5622-wifi-scan-tuning-build.log
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules >"$LOG" 2>&1 || { tail -40 "$LOG"; die "module build failed"; }
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || die "no hi5622_wifi.ko"
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG")
SIZE=$(stat -c %s "$KO"); SHA=$(sha256sum "$KO" | cut -d' ' -f1)
STRIP=${CROSS_COMPILE}strip
if [ -x "$STRIP" ]; then "$STRIP" --strip-debug -o "$KO.stripped" "$KO"; SSIZE=$(stat -c %s "$KO.stripped"); SSHA=$(sha256sum "$KO.stripped" | cut -d' ' -f1); else SSIZE=0; SSHA=n/a; fi
IMPORTS=$("$CROSS_COMPILE"nm -u "$KO" 2>/dev/null | grep -cE ' (wiphy_|cfg80211_)')
echo "module      : $KO"
echo "unopt size  : $SIZE  sha256 $SHA"
echo "stripped    : $SSIZE  sha256 $SSHA"
echo "vermagic    : $(modinfo -F vermagic "$KO" 2>/dev/null)"
echo "depends     : $(modinfo -F depends "$KO" 2>/dev/null)"
echo "cfg imports : $IMPORTS (9 expected)"
echo "warnings    : $WARN (0 expected)"
modinfo "$KO" | grep -E 'parm:.*scan_(chan_time|req_batch|batch_gap)' || true
[ "$WARN" -eq 0 ] || die "build not clean: $WARN warning/error lines"
[ "$IMPORTS" -ge 9 ] || die "expected >=9 cfg80211 imports, got $IMPORTS"

echo
echo "== 4. request dump (regenerates recipes/REQUEST-BYTES.md frame) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -I"$WORK" -o "$WORK/request_dump" "$LANE/tools/request_dump.c" "$WORK/vapscan_frames.c" || die "request dump compile"
"$WORK/request_dump" | head -4
echo "PASS: wifi-scan-tuning host verification complete"
