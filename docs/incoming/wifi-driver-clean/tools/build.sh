#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# Build the clean-room Hi5622V100 Wi-Fi transport driver against the project
# kernel tree and report the artifact evidence (size, sha256, vermagic,
# warnings).  This is the canonical build entry point for
# opensource/docs/incoming/wifi-driver-clean.
#
# Never builds an image, never touches the device, never copies vendor
# firmware/calibration bytes: only the module sources under src/ are compiled.
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
KDIR=${KDIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}
STAGING_DIR=${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir}
CROSS_COMPILE=${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
# The OpenWrt GCC wrapper requires STAGING_DIR in the environment.
export STAGING_DIR KDIR CROSS_COMPILE

LOG=${LOG:-/tmp/hi5622-wifi-build.log}
KO=$SRC/hi5622_wifi.ko

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CROSS_COMPILE}gcc" ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"

echo "== clean =="
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" clean >/dev/null 2>&1 || true
rm -f "$KO"

echo "== build =="
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" modules >"$LOG" 2>&1; then
	tail -40 "$LOG"
	die "kernel module build failed (full log: $LOG)"
fi

WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG" || true)
echo "== artifact =="
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo
echo "== params =="
modinfo -F parm "$KO" 2>/dev/null | sed 's/^/  /'
echo
if [ "$WARN" -ne 0 ]; then
	die "build is not clean: $WARN warning/error line(s)"
fi

if [ "${RUN_SELFTEST:-1}" = "1" ]; then
	echo "== selftest =="
	PY=${PY:-/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python}
	[ -x "$PY" ] || PY=python3
	"$PY" "$HERE/selftest.py" || die "selftest failed"
fi

echo "OK: clean build + selftest"
