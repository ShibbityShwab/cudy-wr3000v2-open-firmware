#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-prepublish-module.sh - rebuild the phase-1 completion-consumer
# prepublish rung module (lane wifi-rx-completion-phase, shared task task-168).
# HOST-ONLY: builds a module, never touches the bench.  The .ko is local
# evidence only and is NEVER committed.
#
# Chain (all committed; no vendor binaries):
#   1. FINAL association tree (../wifi-stability/tools/build-association-module.sh)
#   2. + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
#   3. + ../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
#        (1-3 = the staged /tmp/wifi-rx-watermark-build/rebuild tree, i.e. the
#         exact source chain of the fired d2c608e7 module)
#   4. + ../wifi-rx-vendor-diff/patches/0001-rx-vendor-diff.patch
#        (the sibling candidate-2 stack: observe-only drain default,
#         rx_drain_write=0, 59 params)
#   5. + patches/0001-rx-cmp-phase-prepublish.patch        <- this lane
#   = PHASE-1 PREPUBLISH module (60 params, host-built, NOT fired)
#
# Usage:
#   ./tools/build-prepublish-module.sh
#   BASE_TREE=/tmp/wifi-rx-watermark-build/rebuild ./tools/build-prepublish-module.sh
#
# If BASE_TREE is absent, regenerate steps 1-3 first:
#   ( cd ../wifi-rx-watermark && REBUILD_ASSOC=1 ./tools/build-final-module.sh )
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
PATCH=$LANE_DIR/patches/0001-rx-cmp-phase-prepublish.patch
VENDOR_DIFF_PATCH=$DOCS/wifi-rx-vendor-diff/patches/0001-rx-vendor-diff.patch
WORK=${WORK:-/tmp/rx-cmp-phase-build}
ART=${ART:-/tmp/rx-cmp-phase-artifact}
BASE_TREE=${BASE_TREE:-/tmp/wifi-rx-watermark-build/rebuild}

[ -f "$PATCH" ] || { echo "cmp-phase: missing patch $PATCH" >&2; exit 2; }
[ -f "$VENDOR_DIFF_PATCH" ] || { echo "cmp-phase: missing sibling patch $VENDOR_DIFF_PATCH" >&2; exit 2; }
[ -d "$BASE_TREE" ] || { echo "cmp-phase: base tree missing: $BASE_TREE (see header)" >&2; exit 2; }

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE:-}" ] && KDIR=$KDIR_OVERRIDE
[ -n "${STAGING_OVERRIDE:-}" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "cmp-phase: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "cmp-phase: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "cmp-phase: cross gcc missing" >&2; exit 2; }
export STAGING_DIR

echo "== 1. stage the base tree"
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .module-common.o module-common.o build.log 2>/dev/null ) || true

echo "== 2. apply the sibling vendor-diff stack, then this lane's patch"
( cd "$WORK" && patch -p1 --dry-run < "$VENDOR_DIFF_PATCH" >/dev/null )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$VENDOR_DIFF_PATCH" >/dev/null )
( cd "$WORK" && patch -p1 --dry-run < "$PATCH" >/dev/null )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null )
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "cmp-phase: conflict markers survived - refusing build" >&2; exit 1
fi

echo "== 3. build against $KDIR"
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "cmp-phase: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "cmp-phase: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "cmp-phase: no .ko" >&2; exit 1; }

echo "== 4. strip + identity checks"
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.stripped.ko" "$KO"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
modinfo -p "$ART/hi5622_wifi.stripped.ko" > "$ART/params.txt"
NP=$(wc -l < "$ART/params.txt")
[ "$NP" = "60" ] || { echo "cmp-phase: param count $NP != 60" >&2; exit 1; }
for p in rx_cmp_prepublish rx_drain_write rx_status_service rx_enable; do
	grep -q "^$p:" "$ART/params.txt" || {
		echo "cmp-phase: missing param $p" >&2; exit 1; }
done
for s in "DP rx_cmp_prepublish" "PHASE1-REJECTED-BY-DEVICE" "PHASE1-ACCEPTED-IDLE" "RX completion-phase"; do
	strings -a "$ART/hi5622_wifi.stripped.ko" | grep -qF "$s" || {
		echo "cmp-phase: missing feature string: $s" >&2; exit 1; }
done
for s in hi5622_dp_rx_cmp_prepublish_store rxc_prepublish_gate rxc_pending rxc_phase1_looks_full; do
	"$CROSS"nm "$ART/hi5622_wifi.stripped.ko" | grep -q " $s\$" || {
		echo "cmp-phase: expected symbol missing: $s" >&2; exit 1; }
done
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.stripped.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "cmp-phase: strip changed .text" >&2; exit 1; }
rm -f "$WORK/.t1" "$WORK/.t2"
sha256sum "$ART/hi5622_wifi.stripped.ko" "$ART/hi5622_wifi.ko.unstripped" > "$ART/sha256.txt"

echo
echo "== PHASE-1 PREPUBLISH module (host-built, NOT fired)"
cat "$ART/sha256.txt"
echo "size      : $(stat -c %s "$ART/hi5622_wifi.stripped.ko")"
echo "params    : $NP"
echo "vermagic  : $("$CROSS"strings "$KO" | grep -m1 '^vermagic=')"
echo "fire      : see FIRE-RECIPE.md (R1 no-traffic probe FIRST)"
