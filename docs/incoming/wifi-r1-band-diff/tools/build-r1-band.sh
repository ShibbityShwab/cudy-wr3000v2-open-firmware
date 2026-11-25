#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-r1-band.sh - lane wifi-r1-band-diff (task-183) host build.
#
# HOST-ONLY: compiles the patched module against the project 6.18.60 kernel
# tree; no device access, no firmware write, no image, no flash.  The .ko is
# LOCAL EVIDENCE ONLY and is never committed.
#
# Chain:
#   1. BASE_TREE (default /tmp/beacon5g-build) = the wifi-5ghz-beacon lane's
#      build tree; base artifact hi5622_wifi.ko 356,600 B sha256
#      bc1cc0ea...a1bbb, 59 params (the CW-0/R1 module).  The base provider
#      source is pinned below and the build refuses on drift.
#   2. patches/0001-vap-start-band.patch = the one-field fix.
#
# Usage: ./tools/build-r1-band.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
P=$LANE/patches/0001-vap-start-band.patch
WORK=/tmp/r1-band-build
ART=/tmp/r1-band-artifact
set +u
if [ -z "$BASE_TREE" ]; then BASE_TREE=/tmp/beacon5g-build; fi
set -u
PIN_PROVIDER=bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828

KDIR=
STAGING_DIR=
set +u
if [ -n "$KDIR_OVERRIDE" ]; then KDIR=$KDIR_OVERRIDE; fi
if [ -n "$STAGING_OVERRIDE" ]; then STAGING_DIR=$STAGING_OVERRIDE; fi
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "r1-band-diff: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "r1-band-diff: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "r1-band-diff: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "r1-band-diff: missing patch $P" >&2; exit 2; }
[ -d "$BASE_TREE" ] || { echo "r1-band-diff: base tree missing: $BASE_TREE" >&2; exit 2; }

echo "== 0. pin the base tree source"
got=$(sha256sum "$BASE_TREE/hi5622_vapscan_provider.c" | cut -d' ' -f1)
[ "$got" = "$PIN_PROVIDER" ] || {
	echo "r1-band-diff: base provider sha $got != pinned $PIN_PROVIDER" >&2
	echo "r1-band-diff: the beacon-lane base drifted - re-review it first" >&2
	exit 1
}
echo "   base pinned (hi5622_vapscan_provider.c)"

rm -rf "$WORK" "$ART"
mkdir -p "$ART" "$LANE/evidence"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order modules.order.cmd build.log 2>/dev/null ) || true
echo "== 1. base tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the one-field VAP-start band patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "r1-band-diff: patch does not apply to the base tree" >&2; exit 1; }
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "r1-band-diff: conflict markers survived - refusing build" >&2; exit 1; fi
grep -q 'gp.cfg_a = p->band;' "$WORK/hi5622_vapscan_provider.c" || {
	echo "r1-band-diff: patched field missing" >&2; exit 1; }
echo "== 3. patched provider: cfg_a = p->band"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "r1-band-diff: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "r1-band-diff: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1; fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "r1-band-diff: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "r1-band-diff: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^ap_chan_wait_ms:' || { echo "r1-band-diff: missing ap_chan_wait_ms" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap5g:' || { echo "r1-band-diff: missing vap5g" >&2; exit 1; }
pcount=$(printf '%s\n' "$MI" | wc -l)
[ "$pcount" = 59 ] || { echo "r1-band-diff: $pcount params != 59 (base drift)" >&2; exit 1; }
echo "== 6. contract PASS: 59 params, ap_chan_wait_ms/vap5g present"

{
	echo "artifact: $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY - never committed)"
	sha256sum "$ART/hi5622_wifi.ko"
	echo "unstripped sha256: $(sha256sum "$ART/hi5622_wifi.ko.unstripped" | cut -d' ' -f1)"
	echo "size: $(wc -c < "$ART/hi5622_wifi.ko") B; params: $pcount"
	echo "base: pinned beacon tree $BASE_TREE"
	echo "patch: patches/0001-vap-start-band.patch"
} | tee "$LANE/evidence/build.txt"
