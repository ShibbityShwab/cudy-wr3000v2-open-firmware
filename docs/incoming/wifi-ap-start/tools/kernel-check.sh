#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/kernel-check.sh - kernel-context compile check for the wifi-ap-start
# lane (host-only: no image build, no device contact).
#
# Copies the COMMITTED wifi-scan-ap src tree to a scratch directory (never
# modifies another lane's files), drops this lane's hi5622_ap_frames.{c,h} in,
# applies the wifi-vap-start provider patch (base) and this lane's
# provider/apstart-provider.patch, then builds the module against the real
# 6.18.60 tree with zero warnings.  The .ko is local evidence only and is NOT
# committed.
#
#   ./tools/kernel-check.sh
#   KDIR=... STAGING_DIR=... ./tools/kernel-check.sh
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
KO_SRC=$DOCS/wifi-scan-ap/src
BASE_PATCH=$DOCS/wifi-vap-start/provider/vapstart-provider.patch
PATCH=$LANE_DIR/provider/apstart-provider.patch

KDIR=
STAGING_DIR=
set +u
if [ -n "$KDIR_OVERRIDE" ]; then KDIR=$KDIR_OVERRIDE; fi
if [ -n "$STAGING_OVERRIDE" ]; then STAGING_DIR=$STAGING_OVERRIDE; fi
set -u
WORK=/tmp/wifi-ap-start-kernel

if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

if [ -z "$KDIR" ]; then
	echo "kernel-check: 6.18.60 kernel tree not found" >&2
	exit 2
fi
[ -d "$KO_SRC" ] || { echo "kernel-check: missing reference module src: $KO_SRC" >&2; exit 2; }
[ -f "$BASE_PATCH" ] || { echo "kernel-check: missing base patch: $BASE_PATCH" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "kernel-check: missing provider patch: $PATCH" >&2; exit 2; }
[ -x "$CROSS_COMPILE"gcc ] || { echo "kernel-check: toolchain not found: $CROSS_COMPILE"gcc >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$KO_SRC/." "$WORK/"
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/.*.cmd
cp "$LANE_DIR/src/hi5622_ap_frames.h" "$LANE_DIR/src/hi5622_ap_frames.c" "$WORK/"
# The wifi-vap-start provider patch (base) needs that lane's builder library.
VAP_SRC=$DOCS/wifi-vap-start/src
cp "$VAP_SRC/vapstart_frames.h" "$VAP_SRC/vapstart_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$BASE_PATCH" )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
     2>&1 | tee "$WORK/build.log"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "kernel-check: BUILD FAILED: $KO missing" >&2; exit 1; }

echo
echo "== kernel-context compile result"
ls -l "$KO"
sha256sum "$KO"
"$CROSS_COMPILE"nm "$KO" | grep -cE ' [Tt] haf_' | sed 's/^/haf_ symbols in module: /'
"$CROSS_COMPILE"nm "$KO" | grep -cE ' [Tt] vapscan_ap_start' | sed 's/^/ap-start builder in module: /'
if grep -iE 'warning:|error:' "$WORK/build.log"; then
	echo "kernel-check: WARNINGS/ERRORS FOUND" >&2
	exit 1
fi
echo "kernel-check: OK (zero warnings; module-only build; no image, no device)"
