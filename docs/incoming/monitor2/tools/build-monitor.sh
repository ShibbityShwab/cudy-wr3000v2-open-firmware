#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Host-only build driver for the monitor2 lane (task-246).
# Applies patches/0001-monitor-mirror.patch to a COPY of the IMAGE #3 merged
# module source tree and cross-builds the module against the pinned 6.18.60
# kernel tree with the OpenWrt toolchain (module build only - never an image,
# never a device).  The base tree is a scratch artifact, not committed.
#
# Usage: BASE=/tmp/wifi-image-3-module ./build-monitor.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
BASE=${BASE:-/tmp/wifi-image-3-module}
WORK=${WORK:-/tmp/monitor2-build}
KDIR=${KDIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}
STAGING_DIR=${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir}
CC=${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
export STAGING_DIR KDIR CROSS_COMPILE="$CC"
die() { echo "FAIL: $*" >&2; exit 1; }
[ -d "$BASE" ] || die "base tree not found: $BASE"
[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CC}gcc" ] || die "cross compiler not found: ${CC}gcc"
rm -rf "$WORK"; mkdir -p "$WORK"
cp -a "$BASE"/. "$WORK"/
( cd "$WORK" && patch -p1 < "$HERE/../patches/0001-monitor-mirror.patch" ) || die "patch does not apply to $BASE"
echo "== build =="
( cd "$WORK" && make ARCH=arm CROSS_COMPILE="$CC" -C "$KDIR" M="$WORK" modules ) || die "module build failed"
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || die "no hi5622_wifi.ko"
echo "== artifact =="
stat -c 'size_bytes: %s' "$KO"
sha256sum "$KO"
echo "params: $(modinfo -F parm "$KO" | wc -l)"
echo "depends: $(modinfo -F depends "$KO")"
echo "vermagic: $(modinfo -F vermagic "$KO")"
