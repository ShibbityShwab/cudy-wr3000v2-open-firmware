#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel build/verify driver for the
# wifi-cfg80211 lane (shared task task-104).
#
#   1. host compile + run of the portable cfg80211 VAP/op-map logic selftest
#      (strict -Werror -pedantic, device-free);
#   2. host compile + run of the inherited H2D logic selftest (regression
#      gate for the working protocol tree this lane extends);
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
WORK=/tmp/wifi-cfg80211-build
CC=cc
KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
# The OpenWrt GCC wrapper requires STAGING_DIR in the environment.
export STAGING_DIR KDIR CROSS_COMPILE
export LC_ALL=C

LOG=/tmp/hi5622-wifi-cfg80211-build.log
KO=$SRC/hi5622_wifi.ko
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "$CROSS_COMPILE"gcc ] || die "cross compiler not found: $CROSS_COMPILE"gcc
command -v "$CC" >/dev/null || die "host compiler not found: $CC"
[ -x "$PY" ] || PY=python3
command -v "$PY" >/dev/null || die "python not found"

echo "== 1. host cfg80211 logic selftest (device-free) =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/cfg80211_logic_selftest" \
      "$HERE/cfg80211_logic_selftest.c" "$SRC/hi5622_cfg80211_logic.c" || die "host cfg80211 selftest compile failed"
"$WORK/cfg80211_logic_selftest" || die "host cfg80211 logic selftest failed"
echo "built: $WORK/cfg80211_logic_selftest ($(wc -c < "$WORK/cfg80211_logic_selftest") bytes)"

echo
echo "== 2. host H2D logic selftest (inherited protocol regression) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/h2d_logic_selftest" \
      "$HERE/h2d_logic_selftest.c" "$SRC/h2d_send_logic.c" \
      "$SRC/h2d_frames.c" "$SRC/hcc_core.c" || die "host H2D selftest compile failed"
"$WORK/h2d_logic_selftest" || die "host H2D logic selftest failed"
echo "built: $WORK/h2d_logic_selftest ($(wc -c < "$WORK/h2d_logic_selftest") bytes)"

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
echo "cfg imports : $IMPORTS (wiphy_*/cfg80211_* undefined)"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo
[ "$WARN" -eq 0 ] || die "build is not clean: $WARN warning/error line(s)"
[ "$IMPORTS" -ge 6 ] || die "expected >= 6 cfg80211 imports, found $IMPORTS"
case "$DEPS" in
	*cfg80211*) ;;
	*) die "module does not depend on cfg80211 (depends='$DEPS')" ;;
esac
modinfo -F parm "$KO" 2>/dev/null | grep -q '^cfg80211_auto:' || \
	die "cfg80211_auto module parameter missing from the module"
modinfo -F parm "$KO" 2>/dev/null | grep -q '^band_force:' || \
	die "band_force module parameter missing from the module"

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
echo "OK: host cfg80211+H2D selftests + clean kernel build ($SIZE B, sha256 $SHA, depends=$DEPS)"
