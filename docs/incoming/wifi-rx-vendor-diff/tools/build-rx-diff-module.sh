#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-rx-diff-module.sh - rebuild the RX VENDOR-DIFF module on the staged
# FINAL association + corrected mgmt-TX carrier + RX watermark tree (lane
# wifi-rx-vendor-diff, shared task task-162).  HOST-ONLY: builds a module, never
# touches the bench.  The .ko is local evidence and is never committed.
#
#   ASSOC_TREE=/tmp/wifi-rx-watermark-build/rebuild ./tools/build-rx-diff-module.sh
#
# If ASSOC_TREE is absent, regenerate the base with
#   ( cd ../wifi-rx-watermark && REBUILD_ASSOC=1 ./tools/build-final-module.sh )
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
BASE=${ASSOC_TREE:-/tmp/wifi-rx-watermark-build/rebuild}
WORK=${WORK:-/tmp/rx-vendor-diff-build}
ART=${ART:-/tmp/rx-vendor-diff-artifact}
PATCH=$LANE_DIR/patches/0001-rx-vendor-diff.patch

[ -d "$BASE" ] || { echo "base tree missing: $BASE (see header)" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "patch missing: $PATCH" >&2; exit 2; }

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE:-}" ] && KDIR=$KDIR_OVERRIDE
[ -n "${STAGING_OVERRIDE:-}" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
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
[ -x "${CROSS}gcc" ] || { echo "cross gcc missing" >&2; exit 2; }

echo "== 1. stage the base tree"
rm -rf "$WORK" "$ART"
mkdir -p "$WORK"
cp -a "$BASE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order \
	  .module-common.o module-common.o build.log 2>/dev/null ) || true

 echo "== 2. apply the patch (dry-run first)"
( cd "$WORK" && patch -p1 --dry-run < "$PATCH" >/dev/null )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null )
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "conflict markers survived - refusing build" >&2; exit 1
fi

export STAGING_DIR
echo "== 3. build against $KDIR"
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "no .ko" >&2; exit 1; }

echo "== 4. strip + identity checks"
mkdir -p "$ART"
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.stripped.ko" "$KO"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
modinfo -p "$ART/hi5622_wifi.stripped.ko" > "$ART/params.txt"
NP=$(wc -l < "$ART/params.txt")
[ "$NP" = "59" ] || { echo "param count $NP != 59" >&2; exit 1; }
for p in rx_drain_write rx_status_service rx_enable rx_refill_wm; do
	grep -q "^${p}:" "$ART/params.txt" || { echo "missing param $p" >&2; exit 1; }
done
for s in "DP rx_parity: read-only vendor-state diff" "DP rx_observe: OBSERVE-ONLY"; do
	strings -a "$ART/hi5622_wifi.stripped.ko" | grep -qF "$s" || {
		echo "missing feature string: $s" >&2; exit 1; }
done
${CROSS}objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
${CROSS}objcopy -O binary --only-section=.text "$ART/hi5622_wifi.stripped.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "strip changed .text" >&2; exit 1; }
sha256sum "$ART/hi5622_wifi.stripped.ko" "$ART/hi5622_wifi.ko.unstripped" > "$ART/sha256.txt"

echo
echo "== RX VENDOR-DIFF module (host-built, NOT fired)"
cat "$ART/sha256.txt"
echo "size      : $(stat -c %s "$ART/hi5622_wifi.stripped.ko")"
echo "params    : $NP"
echo "vermagic  : $(${CROSS}strings "$KO" | grep -m1 '^vermagic=')"
echo "fire      : see FIRE-RECIPE.md (read-only rungs first)"
