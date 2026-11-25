#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-patched-module.sh - rebuild the FINAL association module with the
# RX format hardening patch (lane wifi-rx-format, task-163).  HOST-ONLY: builds
# a module, never touches the bench.  The .ko is local evidence and is never
# committed.
#
# Chain (all committed; no vendor binaries):
#   1. final association tree 251d08f0  (../wifi-stability/tools/build-association-module.sh)
#   2. + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
#   3. + ../wifi-rx-watermark/patches/0001-rx-refill-watermark.patch
#        (steps 1-3 are exactly ../wifi-rx-watermark/tools/build-final-module.sh;
#         the resulting module .text is byte-identical to the fired
#         d2c608e7 artifact tree)
#   4. + patches/0001-rx-format-hardening.patch        <- this lane
#   = RX-format-hardened module (57 params, same module parameter set)
#
#   ./tools/build-patched-module.sh
#   REBUILD_CHAIN=1 ./tools/build-patched-module.sh   # regenerate steps 1-3
#   BASE_TREE=/tmp/wifi-rx-watermark-build/rebuild ./tools/build-patched-module.sh
#
# Artifact: ${ART:-/tmp/rxfmt-artifact}/hi5622_wifi.stripped.ko (+unstripped,
# build.log, params.txt).  Build-directory note: the GNU build-id hashes the
# pre-strip DWARF comp_dir, so the sha is path dependent; compare rebuilds by
# .text + params, exactly like the other lanes.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
PATCH=$LANE_DIR/patches/0001-rx-format-hardening.patch
WORK=${WORK:-/tmp/rxfmt-build}
ART=${ART:-/tmp/rxfmt-artifact}
BASE_TREE=${BASE_TREE:-/tmp/wifi-rx-watermark-build/rebuild}

[ -f "$PATCH" ] || { echo "rxfmt: missing patch $PATCH" >&2; exit 2; }

if [ "${REBUILD_CHAIN:-0}" = "1" ] || [ ! -d "$BASE_TREE" ]; then
	echo "== 0. regenerate the FINAL association + mgmt-carrier + watermark tree"
	( cd "$DOCS/wifi-rx-watermark" && REBUILD_ASSOC=1 ./tools/build-final-module.sh )
	BASE_TREE=/tmp/wifi-rx-watermark-build/rebuild
fi
[ -d "$BASE_TREE" ] || { echo "rxfmt: base tree missing: $BASE_TREE" >&2; exit 2; }

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
[ -d "$KDIR" ] || { echo "rxfmt: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "rxfmt: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "rxfmt: cross gcc missing" >&2; exit 2; }
export STAGING_DIR

echo "== 1. stage the base tree"
rm -rf "$WORK"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd .module-common.o 2>/dev/null ) || true

echo "== 2. apply the RX format hardening patch"
( cd "$WORK" && patch -p1 --dry-run < "$PATCH" >/dev/null && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null )
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "rxfmt: conflict markers survived - refusing build" >&2
	exit 1
fi

echo "== 3. build against $KDIR"
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "rxfmt: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "rxfmt: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "rxfmt: no .ko" >&2; exit 1; }

echo "== 4. strip and verify"
rm -rf "$ART" && mkdir -p "$ART"
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.stripped.ko" "$KO"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
modinfo -p "$ART/hi5622_wifi.stripped.ko" > "$ART/params.txt"
NP=$(wc -l < "$ART/params.txt")
[ "$NP" = "57" ] || { echo "rxfmt: param count $NP != 57 (the patch must not add a parameter)" >&2; exit 1; }
for s in "dp_rx_probe" "DEVICE-WROTE-FRAMES" "MAC-WROTE-NOTHING" \
	 "RX format self-check FAILED" "rx_refill_wm"; do
	strings -a "$ART/hi5622_wifi.stripped.ko" | grep -qF "$s" || {
		echo "rxfmt: feature string missing: $s" >&2; exit 1; }
done
for s in hif_rx_entry_encode hif_rx_hdr_parse hif_rx_buffer_witness \
	 hif_rx_refill_budget hi5622_dp_rx_probe_show; do
	"$CROSS"nm "$ART/hi5622_wifi.stripped.ko" | grep -q " $s\$" || {
		echo "rxfmt: expected symbol missing: $s" >&2; exit 1; }
done
echo "== RX-FORMAT-HARDENED module (host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.stripped.ko"
echo "size      : $(stat -c %s "$ART/hi5622_wifi.stripped.ko")"
echo "sha256    : $(sha256sum "$ART/hi5622_wifi.stripped.ko" | cut -d' ' -f1)"
echo "params    : $NP"
