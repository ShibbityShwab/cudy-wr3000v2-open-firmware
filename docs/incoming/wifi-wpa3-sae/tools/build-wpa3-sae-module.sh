#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# build-wpa3-sae-module.sh - reproduce the wifi-wpa3-sae kernel patch on top of
# the pinned FINAL all-patch chain, HOST-ONLY (no device, no network).
#
# Base chain: opensource 008aca5 "FINAL all-patch module" built by
#   opensource/docs/incoming/wifi-stability/tools/build-all-patch-module.sh
#   (chain tree /tmp/wifi-allpatch-build/integ, stripped artifact
#    308,080 B sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41).
# This script verifies that pinned identity when the chain artifact is present,
# applies patches/0001-wpa3-sae.patch and builds the 6.18.60 ARM module through
# the project's own OpenWrt toolchain.  The .ko is local evidence only (never
# committed) and is NOT fired by this lane.
#
# usage: ./tools/build-wpa3-sae-module.sh
#   env: CHAIN_TREE=/path/to/final-allpatch-tree   (skips chain staging)
#        KDIR_OVERRIDE=... STAGING_OVERRIDE=...
#        WORK_OVERRIDE=... ART_OVERRIDE=...
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
BASE_SHA=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
WORK=${WORK_OVERRIDE:-/tmp/wifi-wpa3-sae-build}
ART=${ART_OVERRIDE:-/tmp/wifi-wpa3-sae-artifact}
CHAIN_TREE=${CHAIN_TREE:-}
PATCH=$LANE/patches/0001-wpa3-sae.patch
[ -f "$PATCH" ] || { echo "missing $PATCH" >&2; exit 2; }

if [ -z "$CHAIN_TREE" ]; then
	if [ ! -d /tmp/wifi-allpatch-build/integ ]; then
		echo "== staging the pinned final all-patch chain (wifi-stability)"
		"$DOCS/wifi-stability/tools/build-all-patch-module.sh" || {
			echo "chain staging failed" >&2; exit 1; }
	fi
	CHAIN_TREE=/tmp/wifi-allpatch-build/integ
fi
[ -d "$CHAIN_TREE" ] || { echo "chain tree missing: $CHAIN_TREE" >&2; exit 2; }

if [ -f /tmp/wifi-allpatch-artifact/hi5622_wifi.ko ]; then
	got=$(sha256sum /tmp/wifi-allpatch-artifact/hi5622_wifi.ko | cut -d' ' -f1)
	if [ "$got" != "$BASE_SHA" ]; then
		echo "CHAIN-ID-MISMATCH: $got (want $BASE_SHA)" >&2
		exit 1
	fi
	echo "CHAIN-ID-OK base stripped sha256 $BASE_SHA"
else
	echo "CHAIN-ID-SKIP (no /tmp/wifi-allpatch-artifact/hi5622_wifi.ko; tree assumed pinned)"
fi

KDIR=${KDIR_OVERRIDE:-}
STAGING_DIR=${STAGING_OVERRIDE:-}
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "cross gcc missing: ${CROSS}gcc" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$CHAIN_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd 2>/dev/null ) || true
echo "chain tree staged: $(ls "$WORK" | wc -l) files"
echo "== applying $PATCH"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" >"$ART/patch-apply.log" 2>&1 ) || {
	echo "PATCH FAILED to apply - chain drift?" >&2; cat "$ART/patch-apply.log" >&2; exit 1; }
echo "patch applied"

export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules >"$ART/build.log" 2>&1 || {
	echo "BUILD FAILED" >&2; tail -40 "$ART/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$ART/build.log"; then
	echo "warnings/errors in build log - refusing artifact" >&2; exit 1; fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "no .ko produced" >&2; exit 1; }
"${CROSS}"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"

NP=$(modinfo -p "$ART/hi5622_wifi.ko" 2>/dev/null | wc -l)
[ "$NP" = "47" ] || { echo "module params = $NP (expected 47, chain drift?)" >&2; exit 1; }
SV=$WORK/cfg80211-provider.symvers
[ "$(wc -l < "$SV")" = "10" ] || { echo "cfg80211 symvers != 10" >&2; exit 1; }
"${CROSS}"nm -u "$ART/hi5622_wifi.ko" | grep -q ' cfg80211_bss_iter$' || {
	echo "cfg80211_bss_iter missing" >&2; exit 1; }
for s in "set_default_mgmt_key" "AKM suites advertised" "key_default_mgmt" \
	 "mgmt-class cipher" "firmware worker rejects cipher byte 0"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "expected feature string missing: $s" >&2; exit 1; }
done

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}"strings "$KO" | grep -m1 '^vermagic=')
echo "== WIFI-WPA3-SAE module (host-built on the pinned final chain, NOT fired)"
echo "base chain: stripped 308080 sha256 $BASE_SHA"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "vermagic  : $VERMAGIC"
echo "params    : $NP (unchanged)"
echo "text .text: binary-identical stripped/unstripped"
