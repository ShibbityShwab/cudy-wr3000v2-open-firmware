#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-module.sh - compile-verify the wifi-ap-mgmt lane on the FINAL
# ALL-PATCH tree (stability commit 008aca5) and report the artifact identity.
# HOST-ONLY: builds a kernel module against the local luofu 6.18.60 tree and
# the local installed all-patch tree; the artifact is local evidence only and
# is never committed.
#
#   ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-module.sh
#
# The all-patch tree is the one produced by
# ../wifi-stability/tools/build-all-patch-module.sh (chain b5dfb883 + the four
# static-RE lane patches + integration/resolved).  This script stages a clean
# copy, applies patches/0001-mgmt-tx-rx.patch, builds, and refuses on any
# warning, any missing/renamed contract item, or a .text change from
# --strip-debug.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$LANE/../../.." && pwd)
ALLPATCH_TREE=${ALLPATCH_TREE:-/tmp/wifi-allpatch-build/integ}
WORK=${WORK:-/tmp/wifi-ap-mgmt-module}
ART=$WORK-artifact
PATCH=$LANE/patches/0001-mgmt-tx-rx.patch

[ -d "$ALLPATCH_TREE" ] || { echo "module: all-patch tree missing: $ALLPATCH_TREE (set ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ after running wifi-stability/tools/build-all-patch-module.sh)" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "module: lane patch missing" >&2; exit 2; }

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE}" ] && KDIR=$KDIR_OVERRIDE
[ -n "${STAGING_OVERRIDE}" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "module: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "module: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "module: cross gcc missing" >&2; exit 2; }
export STAGING_DIR

rm -rf "$WORK" "$ART" "$ART.work"
mkdir -p "$WORK" "$ART.work"
for f in "$ALLPATCH_TREE"/*.c "$ALLPATCH_TREE"/*.h "$ALLPATCH_TREE"/*.inc \
	 "$ALLPATCH_TREE"/Kbuild "$ALLPATCH_TREE"/cfg80211-provider.symvers; do
	[ -f "$f" ] && cp -a "$f" "$WORK"/
done
$( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null )
echo "== 1. all-patch tree staged ($(ls "$WORK" | wc -l) files) + lane patch applied"

echo "== 2. build"
$( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "module: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "module: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "module: no .ko" >&2; exit 1; }

echo "== 3. artifact gates"
${CROSS}strip --strip-debug -o "$ART.work/hi5622_wifi.ko" "$KO"
${CROSS}objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
${CROSS}objcopy -O binary --only-section=.text "$ART.work/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "module: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART.work/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART.work/build.log"

MI=$(modinfo -p "$ART.work/hi5622_wifi.ko" 2>/dev/null)
for p in mgmt_tx mgmt_rx mgmt_diag rx_enable beacon_static wpa_keys sta_join allow_frames; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || { echo "module: missing param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
DUP=$(grep -oE 'HI5622_FRAME_CAP_[A-Z_]+[[:space:]]+\(1u << [0-9]+\)' "$WORK/hi5622_frames_api.h" | awk '{print $NF}' | sort | uniq -d)
[ -z "$DUP" ] || { echo "module: duplicate cap bit(s): $DUP" >&2; exit 1; }
[ "$(wc -l < "$WORK/cfg80211-provider.symvers")" = "12" ] || { echo "module: cfg80211 symvers != 12" >&2; exit 1; }
${CROSS}nm -u "$ART.work/hi5622_wifi.ko" | grep -q ' cfg80211_rx_mgmt_ext$' || { echo "module: cfg80211_rx_mgmt_ext import missing" >&2; exit 1; }
${CROSS}nm -u "$ART.work/hi5622_wifi.ko" | grep -q ' cfg80211_mgmt_tx_status_ext$' || { echo "module: cfg80211_mgmt_tx_status_ext import missing" >&2; exit 1; }
strings -a "$ART.work/hi5622_wifi.ko" | grep -qF 'mgmt_tx=0 fail-closed' || { echo "module: mgmt_tx default string missing" >&2; exit 1; }
strings -a "$ART.work/hi5622_wifi.ko" | grep -qiF 'WAL cfg-event carrier cmd 0x' || { echo "module: WAL carrier string missing" >&2; exit 1; }
strings -a "$ART.work/hi5622_wifi.ko" | grep -qF 'cfg80211: mgmt RX delivered' || { echo "module: RX delivered string missing" >&2; exit 1; }

rm -rf "$ART"; mv "$ART.work" "$ART"
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("$CROSS"strings "$KO" | grep -m1 '^vermagic=')
echo "== AP-MGMT module (final all-patch tree + mgmt patch; host-built, NOT fired)"
echo "tree      : all-patch 008aca5 (47 params) + patches/0001-mgmt-tx-rx.patch"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "params    : $NP"
echo "$VERMAGIC"
