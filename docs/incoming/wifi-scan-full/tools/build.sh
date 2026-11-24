#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host + module verification for wifi-scan-full
# (shared task-138).  Device-free; the local vendor firmware/vendor module are
# only read in place (never copied).
#
#   1. assemble a build tree: BASE_TREE (R4/hardened tree with the g0 id10
#      builder) + this lane's two-file delta (src/);
#   2. host selftest (scan_full_selftest) + inherited scan-tuning suites;
#   3. out-of-tree kernel module build against the project 6.18.60 tree;
#   4. static evidence regeneration (fw_scan_full_re.py, firmware in place).
#
# Env overrides: BASE_TREE, KDIR, STAGING_DIR, CROSS_COMPILE, PYTHON.
set -o pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
REPO=$(cd "$LANE/../../../.." && pwd)
BASE_TREE=${BASE_TREE:-/tmp/wifi-stability-patchcheck}
WORK=${WORK:-/tmp/wifi-scan-full-build}
KDIR=${KDIR:-/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}
STAGING_DIR=${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir}
CROSS_COMPILE=${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
CC=${CC:-cc}
PYTHON=${PYTHON:-$REPO/.provision-venv/bin/python}
export LC_ALL=C STAGING_DIR
die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$BASE_TREE" ] || die "BASE_TREE not found: $BASE_TREE (set BASE_TREE to the R4/hardened tree)"
[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CROSS_COMPILE}gcc" ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"

rm -rf "$WORK"; mkdir -p "$WORK"
cp "$BASE_TREE"/*.c "$BASE_TREE"/*.h "$BASE_TREE"/Kbuild "$BASE_TREE"/Makefile \
   "$BASE_TREE"/*.inc "$BASE_TREE"/*.symvers "$WORK"/ 2>/dev/null || true
cp "$LANE"/src/*.c "$LANE"/src/*.h "$WORK"/ || die "src/ overlay failed"
[ -f "$WORK/hi5622_scan.c" ] || die "BASE_TREE is not the combined AP+DP tree (hi5622_scan.c missing)"

HOST_CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -I$WORK"

echo "== 1. host selftest (refined g0 id10 builder) =="
"$CC" $HOST_CFLAGS -o "$WORK/scan_full_test" "$LANE/tools/scan_full_selftest.c" \
    "$WORK/hi5622_band_cfg.c" || die "selftest compile"
"$WORK/scan_full_test" || die "selftest failed"

echo "== 2. inherited scan-tuning regressions (logical scan model) =="
"$CC" $HOST_CFLAGS -o "$WORK/tuning_test" \
    "$LANE/../wifi-scan-tuning/tools/scan_tuning_selftest.c" "$WORK/hi5622_scan_logic.c" \
    || die "inherited tuning test compile"
"$WORK/tuning_test" | tail -1 || die "inherited tuning test failed"
"$CC" $HOST_CFLAGS -o "$WORK/complete_test" \
    "$LANE/../wifi-scan-tuning/tools/scan_complete_selftest.c" "$WORK/hi5622_scan_logic.c" \
    "$WORK/hi5622_scan_d2h.c" "$WORK/hi5622_vapscan_map.c" "$WORK/vapscan_frames.c" \
    || die "inherited complete test compile"
"$WORK/complete_test" | tail -1 || die "inherited complete test failed"

echo "== 3. kernel module build =="
LOG=/tmp/hi5622-wifi-scan-full-build.log
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules >"$LOG" 2>&1 \
    || { tail -40 "$LOG"; die "module build failed"; }
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || die "no hi5622_wifi.ko"
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG")
SIZE=$(stat -c %s "$KO"); SHA=$(sha256sum "$KO" | cut -d' ' -f1)
STRIP=${CROSS_COMPILE}strip
if [ -x "$STRIP" ]; then "$STRIP" --strip-debug -o "$KO.stripped" "$KO"; fi
IMPORTS=$("$CROSS_COMPILE"nm -u "$KO" 2>/dev/null | grep -cE ' (wiphy_|cfg80211_)')
echo "module      : $KO"
echo "unstripped  : $SIZE  sha256 $SHA"
[ -f "$KO.stripped" ] && echo "stripped    : $(stat -c %s "$KO.stripped")  sha256 $(sha256sum "$KO.stripped" | cut -d' ' -f1)"
echo "vermagic    : $(/usr/sbin/modinfo -F vermagic "$KO" 2>/dev/null)"
echo "depends     : $(/usr/sbin/modinfo -F depends "$KO" 2>/dev/null)"
echo "cfg imports : $IMPORTS"
echo "warnings    : $WARN (0 expected)"
/usr/sbin/modinfo "$KO" 2>/dev/null | grep -E 'parm:.*scan_band_cfg' || true
[ "$WARN" -eq 0 ] || die "build not clean: $WARN warning/error lines"

echo "== 4. static evidence (firmware read in place) =="
if [ -x "$PYTHON" ]; then
    "$PYTHON" "$LANE/tools/fw_scan_full_re.py" ${HI5622_WIFI_KO:+--ko "$HI5622_WIFI_KO"} | tail -2 \
        || die "static evidence tool failed"
else
    echo "SKIP: project venv python not found at $PYTHON"
fi
echo "PASS: wifi-scan-full host verification complete"