#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel build/verify driver for the
# wifi-scan-ap lane (shared task task-113).
#
#   1. host compile + run of the portable scan/AP/provider selftest
#      (strict -Werror -pedantic, device-free);
#   2. host regression: the inherited cfg80211 VAP/op-map selftest and the
#      inherited H2D protocol selftest (the working trees this lane extends);
#   3. out-of-tree kernel module build against the project 6.18.60 tree and
#      artifact evidence (size, sha256, vermagic, cfg80211 imports, params,
#      compiler+modpost warning count);
#   4. cfg80211 provider-symbols check against the kernel tree exports;
#   5. device-free driver selftest (source/contract checks).
#
# Never builds an image, never touches the device, never copies vendor
# firmware/calibration bytes: only the sources under src/ are compiled.
set -o pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
WORK=/tmp/wifi-scan-ap-build
CC=cc
KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
export LC_ALL=C

[ -n "$KDIR_OVERRIDE" ] && KDIR=$KDIR_OVERRIDE
[ -n "$STAGING_DIR_OVERRIDE" ] && STAGING_DIR=$STAGING_DIR_OVERRIDE && \
	CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -n "$CROSS_COMPILE_OVERRIDE" ] && CROSS_COMPILE=$CROSS_COMPILE_OVERRIDE
# The OpenWrt GCC wrapper requires STAGING_DIR in the environment.
export STAGING_DIR KDIR CROSS_COMPILE

LOG=/tmp/hi5622-wifi-scan-ap-build.log
KO=$SRC/hi5622_wifi.ko
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "$CROSS_COMPILE"gcc ] || die "cross compiler not found: $CROSS_COMPILE"gcc
command -v "$CC" >/dev/null || die "host compiler not found: $CC"
[ -x "$PY" ] || PY=python3
command -v "$PY" >/dev/null || die "python not found"

mkdir -p "$WORK"
CCFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes -I$SRC"

echo "== 1. host scan/AP/provider logic selftest (device-free) =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" $CCFLAGS -o "$WORK/scan_logic_selftest" \
	"$HERE/scan_logic_selftest.c" "$SRC/hi5622_scan_logic.c" \
	"$SRC/hi5622_vapscan_map.c" "$SRC/hi5622_scan_d2h.c" \
	"$SRC/vapscan_frames.c" "$SRC/h2d_frames.c" \
	|| die "host scan selftest compile failed"
"$WORK/scan_logic_selftest" || die "host scan logic selftest failed"
echo "built: $WORK/scan_logic_selftest ($(wc -c < "$WORK/scan_logic_selftest") bytes)"

echo
echo "== 2a. host inherited cfg80211 VAP/op-map regression =="
"$CC" $CCFLAGS -o "$WORK/cfg80211_logic_selftest" \
	"$HERE/cfg80211_logic_selftest.c" "$SRC/hi5622_cfg80211_logic.c" \
	|| die "host cfg80211 selftest compile failed"
"$WORK/cfg80211_logic_selftest" || die "host cfg80211 logic selftest failed"

echo
echo "== 2b. host inherited H2D protocol regression =="
"$CC" $CCFLAGS -o "$WORK/h2d_logic_selftest" \
	"$HERE/h2d_logic_selftest.c" "$SRC/h2d_send_logic.c" \
	"$SRC/h2d_frames.c" "$SRC/hcc_core.c" || die "host H2D selftest compile failed"
"$WORK/h2d_logic_selftest" || die "host H2D logic selftest failed"

echo
echo "== 3. kernel module build (project 6.18.60 tree) =="
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" clean >/dev/null 2>&1 || true
rm -f "$KO"
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" modules >"$LOG" 2>&1; then
	tail -40 "$LOG"
	die "kernel module build failed (full log: $LOG)"
fi
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG")
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
DEPS=$(modinfo -F depends "$KO" 2>/dev/null)
IMPORTS=$("$CROSS_COMPILE"nm -u "$KO" 2>/dev/null | grep -cE ' (wiphy_|cfg80211_)')
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "depends     : $DEPS"
echo "cfg imports : $IMPORTS (wiphy_*/cfg80211_* undefined; 9 expected)"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo
[ "$WARN" -eq 0 ] || die "build is not clean: $WARN warning/error line(s)"
[ "$IMPORTS" -ge 9 ] || die "expected >= 9 cfg80211 imports, found $IMPORTS"
case "$DEPS" in
	*cfg80211*) ;;
	*) die "module does not depend on cfg80211 (depends='$DEPS')" ;;
esac
for P in allow_frames frame_gap_ms cfg80211_auto band_force h2d_send_budget; do
	modinfo -F parm "$KO" 2>/dev/null | grep -q "^$P:" || \
		die "module parameter missing from the module: $P"
done

echo
echo "== 4. cfg80211 provider-symbols check (kernel tree exports) =="
"$PY" "$HERE/gen_cfg80211_symvers.py" --check --kdir "$KDIR" --src "$SRC" || \
	die "cfg80211-provider.symvers does not match the module/tree exports"

RUN_SELFTEST_VALUE="$RUN_SELFTEST"
[ -n "$RUN_SELFTEST_VALUE" ] || RUN_SELFTEST_VALUE=1
if [ "$RUN_SELFTEST_VALUE" = "1" ]; then
	echo
	echo "== 5. driver selftest (source-level, device-free) =="
	"$PY" "$HERE/selftest.py" --kdir "$KDIR" || die "driver selftest failed"
fi

echo
echo "OK: host scan+inherited selftests + clean kernel build ($SIZE B, sha256 $SHA, depends=$DEPS)"
