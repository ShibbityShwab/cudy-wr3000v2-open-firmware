#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-rate-ctl.sh - apply the wifi-rate-ctl patch to a staged copy of the
# wifi-h2d-send driver sources, run the host tests, build the kernel module
# against the project 6.18.60 tree and record the artifact identity.
#
# HOST-ONLY: no device access, no firmware, no image, no flash.  The .ko is
# local evidence only and is never committed.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
H2D=$DOCS/wifi-h2d-send/src
PATCH=$LANE/patches/0001-rate-ctl.patch
if [ -z "$WORK" ]; then WORK=/tmp/wifi-rate-ctl-build; fi
SRC=$WORK/src
ART=$WORK/artifact

if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -f "$c/include/generated/autoconf.h" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
if [ -z "$KDIR" ] || [ -z "$STAGING_DIR" ]; then
	echo "build-rate-ctl: 6.18.60 kernel/staging tree not found (set KDIR/STAGING_DIR)" >&2
	exit 2
fi
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "build-rate-ctl: cross gcc missing: $CROSS"gcc >&2; exit 2; }
[ -d "$H2D" ] || { echo "build-rate-ctl: wifi-h2d-send src missing: $H2D" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "build-rate-ctl: patch missing: $PATCH" >&2; exit 2; }

echo "== 0. stage + apply the patch =="
rm -rf "$WORK"
mkdir -p "$WORK/stage/src" "$ART"
for f in "$H2D"/*.c "$H2D"/*.h "$H2D"/Kbuild "$H2D"/*.inc; do
	[ -f "$f" ] && cp "$f" "$WORK/stage/src/"
done
patch -p1 --batch --forward -s -d "$WORK/stage" -i "$PATCH"
echo "files touched: $(grep -c '^+++' "$PATCH")"
mv "$WORK/stage/src" "$SRC"
rm -rf "$WORK/stage"

echo
echo "== 1. host tests =="
WORK="$WORK/host" "$HERE/run-host-tests.sh" | tail -6

echo
echo "== 2. kernel module build (project 6.18.60) =="
export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$SRC" clean >/dev/null 2>&1 || true
LOG=$WORK/build.log
if ! make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$SRC" modules >"$LOG" 2>&1; then
	tail -30 "$LOG"
	echo "build-rate-ctl: module build FAILED (log: $LOG)" >&2
	exit 1
fi
WARN=$(grep -cE 'warning:|error:' "$LOG" || true)
KO=$SRC/hi5622_wifi.ko
[ -f "$KO" ] || { echo "build-rate-ctl: no .ko produced" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko"
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
PARMS=$(modinfo -F parm "$KO" 2>/dev/null | wc -l)
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $(modinfo -F vermagic "$KO" 2>/dev/null)"
echo "params      : $PARMS"
echo "rate params : $(modinfo -F parm "$KO" 2>/dev/null | grep -c '^rate_ctl')"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
if [ "$WARN" -ne 0 ]; then
	echo "build-rate-ctl: build is not clean ($WARN warning/error lines)" >&2
	exit 1
fi
echo
echo "OK: patched module built clean ($SIZE B, sha256 $SHA); artifact: $ART/hi5622_wifi.ko"