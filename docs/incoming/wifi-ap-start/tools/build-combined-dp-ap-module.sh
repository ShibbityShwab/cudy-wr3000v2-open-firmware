#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-combined-dp-ap-module.sh - ONE module for the last air-TX
# milestone: wifi-dp-air (data-plane TX/RX) + wifi-ap-start v7 (AP start).
#
# Patch stack (no unresolved conflicts; the only shared file is Kbuild, which
# patches with a 3-line offset):
#   1. base tree  : opensource/docs/incoming/wifi-dp-air/src
#                   (= wifi-scan-complete/src + patches/0001-merge-dp-txrx-
#                      into-provider.patch rev2, module 886,864 B ca32aec4...)
#   2. this lane  : opensource/docs/incoming/wifi-ap-start/provider/
#                   apstart-provider.patch (v7, applies cleanly to the dp tree)
#   3. this lane  : src/hi5622_ap_frames.{h,c} copied into the tree
#
# Host-only: builds against the project 6.18.60 tree, strips debug for a
# path-independent artifact and prints the identity.  The .ko is local
# evidence only (NOT committed); deploy it with tools/rscp.sh.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
DP_SRC=$DOCS/wifi-dp-air/src
PATCH=$LANE_DIR/provider/apstart-provider.patch
WORK=/tmp/wifi-combined-build
ART=/tmp/wifi-ap-dp-combined-artifact

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
[ -d "$DP_SRC" ] || { echo "combined: missing dp-air base: $DP_SRC" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "combined: missing apstart patch: $PATCH" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "combined: 6.18.60 kernel tree not found" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$DP_SRC/." "$WORK/"
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/*.mod "$WORK"/*.mod.c "$WORK"/.*.cmd "$WORK"/Module.symvers
cp "$LANE_DIR/src/hi5622_ap_frames.h" "$LANE_DIR/src/hi5622_ap_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
     2>&1 | tee "$WORK/build.log" >/dev/null
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "combined: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:' "$WORK/build.log"; then
	echo "combined: warnings/errors found - refusing artifact" >&2
	exit 1
fi

# Deployable artifact: debug-stripped (path-independent sha), .text identical.
STRIP="$CROSS_COMPILE"strip
OBJCOPY="$CROSS_COMPILE"objcopy
"$STRIP" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$OBJCOPY" -O binary --only-section=.text "$KO" "$WORK/.t1"
"$OBJCOPY" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
if ! cmp -s "$WORK/.t1" "$WORK/.t2"; then
	echo "combined: STRIP CHANGED .text - refusing artifact" >&2
	exit 1
fi
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"

# Contract checks: AP start + data-plane symbols and sysfs attribute names.
NM="$CROSS_COMPILE"nm
ap_syms=$($NM "$ART/hi5622_wifi.ko" | grep -cE 'vapscan_ap_start|hi5622_cfg11_mgmt_tx|hi5622_mgmt_stypes|hi5622_scan_post_vap_base_once')
dp_syms=$($NM "$ART/hi5622_wifi.ko" | grep -cE 'hi5622_dp_probe|hi5622_dp_setup|hi5622_dp_txrx|dp_tx_frame')
attrs=$(strings -a "$ART/hi5622_wifi.ko" | grep -cxE 'dp_probe|dp_setup|h2d_txq_addr_sync|dp_tx_frame|dp_tx_status|dp_rx_poll|dp_rx_capture|dp_status|dp_rings|h2d_status|cfg80211_status' | tail -1)
missing=""
for a in dp_probe dp_setup h2d_txq_addr_sync dp_tx_frame dp_tx_status dp_rx_poll; do
	if ! strings -a "$ART/hi5622_wifi.ko" | grep -qx "$a"; then missing="$missing $a"; fi
done
[ -z "$missing" ] || { echo "combined: missing attrs:$missing" >&2; exit 1; }

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$WORK/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
echo "== combined dp-air + ap-start module"
echo "base     : wifi-dp-air/src (886,864 B ca32aec4... canonical dp module)"
echo "delta    : ap-start v7 provider patch (applies cleanly; Kbuild +3 offset)"
echo "artifact : $ART/hi5622_wifi.ko (debug-stripped, path-independent)"
echo "size     : $SIZE"
echo "sha256   : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
VERMAGIC=$("$CROSS_COMPILE"strings "$KO" | grep -m1 '^vermagic=')
echo "$VERMAGIC"
DEPENDS=$(strings -a "$KO" | grep -m1 '^depends=')
echo "$DEPENDS"
echo "ap symbols: $ap_syms  dp symbols: $dp_syms"
echo "attrs    : dp_probe dp_setup h2d_txq_addr_sync dp_tx_frame dp_tx_status dp_rx_poll present"
echo "note: .ko is local evidence only (never committed); deploy with tools/rscp.sh"
