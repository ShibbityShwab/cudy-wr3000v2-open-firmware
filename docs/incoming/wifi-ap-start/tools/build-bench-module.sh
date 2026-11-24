#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-bench-module.sh - build the deployable AP-start bench module and
# print its identity (host-only: no image build, no device contact).
#
# Base = committed wifi-scan-ap src tree + wifi-vap-start builder library and
# provider patch + this lane's builder library and provider patch (v2, with the
# hostapd capability gates).  The .ko is LOCAL EVIDENCE only and is NOT
# committed; copy it to the bench router with tools/rscp.sh.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
KO_SRC=$DOCS/wifi-scan-ap/src
BASE_PATCH=$DOCS/wifi-vap-start/provider/vapstart-provider.patch
PATCH=$LANE_DIR/provider/apstart-provider.patch
WORK=/tmp/wifi-ap-start-bench
ART=/tmp/wifi-ap-start-bench-artifact

KDIR=
STAGING_DIR=
set +u
if [ -n "$KDIR_OVERRIDE" ]; then KDIR=$KDIR_OVERRIDE; fi
if [ -n "$STAGING_OVERRIDE" ]; then STAGING_DIR=$STAGING_OVERRIDE; fi
set -u
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

if [ -z "$KDIR" ]; then echo "build-bench: kernel tree not found" >&2; exit 2; fi
[ -d "$KO_SRC" ] || { echo "build-bench: missing $KO_SRC" >&2; exit 2; }
[ -f "$BASE_PATCH" ] || { echo "build-bench: missing $BASE_PATCH" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "build-bench: missing $PATCH" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$KO_SRC/." "$WORK/"
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/.*.cmd
cp "$LANE_DIR/src/hi5622_ap_frames.h" "$LANE_DIR/src/hi5622_ap_frames.c" "$WORK/"
cp "$DOCS/wifi-vap-start/src/vapstart_frames.h" "$DOCS/wifi-vap-start/src/vapstart_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$BASE_PATCH" )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
     2>&1 | tee "$WORK/build.log" >/dev/null

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "build-bench: BUILD FAILED" >&2; tail -30 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:' "$WORK/build.log"; then
	echo "build-bench: warnings/errors found - refusing artifact" >&2
	exit 1
fi
# The kernel tree has DEBUG_INFO on, so the unstripped .ko embeds the build
# directory path and its sha256 changes with $WORK.  Strip only the debug
# sections for the deployable artifact: the result is path-independent, keeps
# .symtab/.rel.* (module loading and EXPORT_SYMBOL need them) and is what the
# bench deploys.
STRIP="$CROSS_COMPILE"strip
"$STRIP" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
VERMAGIC=$("$CROSS_COMPILE"strings "$KO" | grep -m1 '^vermagic=')
echo "== AP-start bench module"
KO_ART=$ART/hi5622_wifi.ko
echo "artifact : $KO_ART (debug-stripped, path-independent)"
echo "size     : $SIZE"
echo "sha256   : $SHA"
echo "$VERMAGIC"
echo "ap builder : $("$CROSS_COMPILE"nm "$KO_ART" | grep -c ' vapscan_ap_start') symbol(s)"
echo "connect gate: $("$CROSS_COMPILE"nm "$KO_ART" | grep -c ' hi5622_cfg11_connect') symbol(s)"
echo "haf_ syms : $("$CROSS_COMPILE"nm "$KO_ART" | grep -cE ' [Tt] haf_')"
echo "depends    : $(strings -a "$KO_ART" | grep -m1 '^depends=' || true)"
echo "note: .ko is local evidence only (never committed); copy with tools/rscp.sh"
