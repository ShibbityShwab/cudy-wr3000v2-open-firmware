#!/usr/bin/env bash
# tools/build.sh - host-only build driver for the wifi-hcc-service module.
#
# Copies the committed src/ tree to a scratch directory and builds ONE
# out-of-tree kernel module (hi5622_pciet_hcc.ko) against the project's built
# luofu 6.18.60 kernel tree.  It never builds an OpenWrt image, never touches
# the router, and never writes into the lane's src/ (so the committed tree
# stays clean).  The .ko is local evidence only and is not committed.
#
# Usage:
#   ./tools/build.sh
#   KDIR=... STAGING_DIR=... WORK=... ./tools/build.sh
set -eo pipefail

LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"

DEFAULT_KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
DEFAULT_STAGING=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
DEFAULT_WORK=/home/shibbityshwab/wifi-hcc-service-work/build

[ -n "$KDIR" ] || KDIR="$DEFAULT_KDIR"
[ -n "$STAGING_DIR" ] || STAGING_DIR="$DEFAULT_STAGING"
[ -n "$WORK" ] || WORK="$DEFAULT_WORK"
[ -n "$CROSS_COMPILE" ] || CROSS_COMPILE="$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-"
TC_DIR="$(dirname "$CROSS_COMPILE")"

[ -d "$KDIR" ] || { echo "kernel tree not found: $KDIR" >&2; exit 2; }
[ -x "$CROSS_COMPILE"gcc ] || { echo "toolchain not found: $CROSS_COMPILE"gcc >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$LANE_DIR/src/." "$WORK/"

make -C "$WORK" ARCH=arm \
     KDIR="$KDIR" STAGING_DIR="$STAGING_DIR" CROSS_COMPILE="$CROSS_COMPILE" \
     2>&1 | tee "$WORK/build.log"

KO="$WORK/hi5622_pciet_hcc.ko"
[ -f "$KO" ] || { echo "BUILD FAILED: $KO missing" >&2; exit 1; }

echo
echo "== build result"
ls -l "$KO"
sha256sum "$KO"
modinfo "$KO" | grep -E '^(license|description|name|vermagic|alias|parm):' || true

echo
echo "== hcc_* sysfs attributes present in the module:"
"$CROSS_COMPILE"nm "$KO" | grep -o 'dev_attr_hcc_[a-z_]*' | sort -u

echo
echo "== gated device writers:"
"$CROSS_COMPILE"nm "$KO" | grep -E 'hcc_writel|hcc_ete_writel' || true

echo
echo "BUILD OK (module only; no image build, no device contact)"
