#!/usr/bin/env bash
# tools/build.sh - host-only build driver for the wifi-d2h-direct lane.
#
# Assembles the wifi-hcc-service module sources (the committed lane deliverable,
# read-only) in a scratch tree, adds the direct-read D2H files from this lane,
# applies src/hcc-d2h-direct.patch, and builds ONE out-of-tree kernel module
# (hi5622_pciet_hcc.ko).  It never builds an OpenWrt image, never touches the
# router, and never writes into another lane's src/ (the committed trees stay
# clean).  The .ko is local evidence only and is not committed.
#
# Usage:
#   ./tools/build.sh
#   KDIR=... STAGING_DIR=... WORK=... ./tools/build.sh
set -eo pipefail

LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
HCC_SRC="$LANE_DIR/../wifi-hcc-service/src"

DEFAULT_KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
DEFAULT_STAGING=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
DEFAULT_WORK=/home/shibbityshwab/wifi-d2h-direct-work/build

[ -n "$KDIR" ] || KDIR="$DEFAULT_KDIR"
[ -n "$STAGING_DIR" ] || STAGING_DIR="$DEFAULT_STAGING"
[ -n "$WORK" ] || WORK="$DEFAULT_WORK"
[ -n "$CROSS_COMPILE" ] || CROSS_COMPILE="$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-"
TC_DIR="$(dirname "$CROSS_COMPILE")"

[ -d "$HCC_SRC" ] || { echo "wifi-hcc-service src not found: $HCC_SRC" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "kernel tree not found: $KDIR" >&2; exit 2; }
[ -x "$CROSS_COMPILE"gcc ] || { echo "toolchain not found: $CROSS_COMPILE"gcc >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$HCC_SRC/." "$WORK/"
cp "$LANE_DIR/src/hi5622_d2h_direct.h" "$LANE_DIR/src/pciet_d2h_direct.c" "$WORK/"

echo "== applying src/hcc-d2h-direct.patch in $WORK"
( cd "$WORK" && patch -p1 --batch --forward < "$LANE_DIR/src/hcc-d2h-direct.patch" )

echo
echo "== hcc_* / rd_* device writers and attrs in the assembled sources:"
grep -n "dev_attr_rd_" "$WORK"/pciet_d2h_direct.c | head -10

echo
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
echo "== rd_* sysfs attributes present in the module:"
"$CROSS_COMPILE"nm "$KO" | grep -o 'dev_attr_rd_[a-z_]*' | sort -u

echo
echo "== direct-read entry points present:"
"$CROSS_COMPILE"nm "$KO" | grep -E ' (T|t) (pciet_d2h_|pciet_hcc_rd)' || true

echo
echo "BUILD OK (module only; no image build, no device contact)"
