#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel build/verify driver for the
# wifi-h2d-send lane (shared task task-98).
#
#   1. host compile of the portable H2D policy/emission logic + its selftest
#      (strict -Werror -pedantic) and run it (device-free);
#   2. out-of-tree kernel module build against the project 6.18.60 tree and
#      artifact evidence (size, sha256, vermagic, h2d_* symbols, params,
#      compiler+modpost warning count);
#   3. device-free driver selftest (contract/policy/source-layout checks).
#
# Never builds an image, never touches the device, never copies vendor
# firmware/calibration bytes: only the module sources under src/ are compiled.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
WORK=${WORK:-/tmp/wifi-h2d-send-build}
CC=${CC:-cc}
KDIR=${KDIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}
STAGING_DIR=${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir}
CROSS_COMPILE=${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
# The OpenWrt GCC wrapper requires STAGING_DIR in the environment.

# The historical default KDIR (openwrt-lab-selectorfix-img) may have been
# cleaned; fall back to another configured 6.18.60 luofu build dir so the
# canonical build keeps working.  Override KDIR/STAGING_DIR/CROSS_COMPILE to
# pin a specific tree for an evidence hash.
if [ ! -f "$KDIR/include/generated/autoconf.h" ]; then
	for cand in /home/shibbityshwab/openwrt-lab-verify \
		        /home/shibbityshwab/openwrt-lab-selectorfix-k \
		        /home/shibbityshwab/openwrt-lab; do
		k="$cand/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60"
		if [ -f "$k/include/generated/autoconf.h" ] &&
		   [ -x "$cand/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc" ]; then
			KDIR=$k
			STAGING_DIR=$cand/staging_dir
			CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
			break
		fi
		done
	export STAGING_DIR KDIR CROSS_COMPILE
fi
export STAGING_DIR KDIR CROSS_COMPILE

LOG=${LOG:-/tmp/hi5622-wifi-h2d-send-build.log}
KO=$SRC/hi5622_wifi.ko

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CROSS_COMPILE}gcc" ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"
command -v "$CC" >/dev/null || die "host compiler not found: $CC"

echo "== 1. host H2D logic selftest (device-free) =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/h2d_logic_selftest" \
      "$HERE/h2d_logic_selftest.c" "$SRC/h2d_send_logic.c" \
      "$SRC/h2d_frames.c" "$SRC/hcc_core.c" || die "host selftest compile failed"
"$WORK/h2d_logic_selftest" || die "host H2D logic selftest failed"
echo "built: $WORK/h2d_logic_selftest ($(wc -c < "$WORK/h2d_logic_selftest") bytes)"

echo
echo "== 2. kernel module build (project 6.18.60 tree) =="
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" clean >/dev/null 2>&1 || true
rm -f "$KO"
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" modules >"$LOG" 2>&1; then
	tail -40 "$LOG"
	die "kernel module build failed (full log: $LOG)"
fi
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG" || true)
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
SYMS=$("${CROSS_COMPILE}nm" "$KO" | grep -cE ' T h2d_' || true)
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "h2d syms    : $SYMS (T h2d_*)"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo
echo "== params =="
modinfo -F parm "$KO" 2>/dev/null | sed 's/^/  /'
echo
[ "$WARN" -eq 0 ] || die "build is not clean: $WARN warning/error line(s)"
modinfo -F parm "$KO" 2>/dev/null | grep -q '^h2d_send_budget:' || \
	die "h2d_send_budget module parameter missing from the module"

if [ "${RUN_SELFTEST:-1}" = "1" ]; then
	echo "== 3. driver selftest (source-level, device-free) =="
	PY=${PY:-/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python}
	[ -x "$PY" ] || PY=python3
	"$PY" "$HERE/selftest.py" || die "driver selftest failed"
fi

echo
echo "OK: host H2D selftest + clean kernel build ($SIZE B, sha256 $SHA)"
