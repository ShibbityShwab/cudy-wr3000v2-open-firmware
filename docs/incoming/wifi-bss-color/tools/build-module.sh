#!/bin/sh
# Build the patched module on the image #3 module base (host-only; no bench).
#
# usage: tools/build-module.sh <image-3-module-source-dir> <work-dir> [kernel-dir]
#
# The base is the module source tree used for image #3 (the flashed+accepted
# build; snapshot /tmp/wifi-image-3-module on the bench host). The patch is
# applied to a COPY; the base is never modified. Artifact contract: module
# .ko builds with 0 warnings; the lane's evidence/BUILD.txt records size,
# sha256, param count and the compiler line.
set -eu
cd "$(dirname "$0")/.."
BASE="${1:?base module source dir}"
WORK="${2:?work dir (will be recreated)}"
KDIR="${3:-/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}"
STAGING="${STAGING_DIR:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir}"
CROSS="$STAGING/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-"

rm -rf "$WORK"
cp -a "$BASE" "$WORK"
patch -d "$WORK" -p1 < patches/0001-bss-color.patch
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules
ls -l "$WORK/hi5622_wifi.ko"
sha256sum "$WORK/hi5622_wifi.ko"
strings -a "$WORK/hi5622_wifi.ko" | grep -c '^parm=' || true
