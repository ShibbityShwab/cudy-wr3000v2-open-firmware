#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-sta-join-hardening.sh - host-only compile verification of the
# wifi-sta-join hardening patch on the FINAL ALL-PATCH module tree.
#
# The patch applies on top of the all-patch integration tree produced by
#   ../wifi-stability/tools/build-all-patch-module.sh
# (final module of opensource commit 008aca5: the static-RE wave's lane
#  patches already merged).  Because that script owns /tmp/wifi-allpatch-*
# and the Lead may be using it, this script does NOT rebuild it by default:
#   INTEG_TREE=/path/to/allpatch/integ ./tools/build-sta-join-hardening.sh
# or, if the standard tree exists:
#   ./tools/build-sta-join-hardening.sh
# Add --rebuild to run the stability builder first (takes over its /tmp dirs).
#
# Outputs (host-only, never committed): /tmp/sta-join-hardening-build/
#   build.log            full kbuild log (must be warning-free)
#   artifact/hi5622_wifi.ko            debug-stripped
#   artifact/hi5622_wifi.ko.unstripped
#   artifact/identity.txt
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
WORK=/tmp/sta-join-hardening-build
ART=$WORK/artifact
INTEG_TREE=${INTEG_TREE:-/tmp/wifi-allpatch-build/integ}
REBUILD=0
[ "${1:-}" = "--rebuild" ] && REBUILD=1

if [ "$REBUILD" = 1 ] || [ ! -d "$INTEG_TREE" ]; then
	if [ -x "$DOCS/wifi-stability/tools/build-all-patch-module.sh" ]; then
		echo "== rebuilding the all-patch integration tree (stability builder)"
		"$DOCS/wifi-stability/tools/build-all-patch-module.sh"
		INTEG_TREE=/tmp/wifi-allpatch-build/integ
	else
		echo "build: all-patch integration tree not found and no stability builder" >&2
		echo "       set INTEG_TREE=/path/to/integ or run the stability builder first" >&2
		exit 2
	fi
fi
[ -d "$INTEG_TREE" ] || { echo "build: INTEG_TREE missing: $INTEG_TREE" >&2; exit 2; }
[ -f "$LANE/patches/0001-sta-join-hardening.patch" ] || {
	echo "build: lane patch missing" >&2; exit 2; }

KDIR=""
STAGING_DIR=""
if [ -z "${KDIR_OVERRIDE:-}" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
else
	KDIR=$KDIR_OVERRIDE
	STAGING_DIR=${STAGING_OVERRIDE:-}
fi
[ -d "$KDIR" ] || { echo "build: 6.18.60 kernel tree not found (KDIR_OVERRIDE)" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "build: staging dir not found (STAGING_OVERRIDE)" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build: cross gcc missing" >&2; exit 2; }
export STAGING_DIR

rm -rf "$WORK"
mkdir -p "$WORK/tree" "$ART"
cp -a "$INTEG_TREE/." "$WORK/tree/"
( cd "$WORK/tree" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order .modules.order.cmd 2>/dev/null ) || true

echo "== apply lane patch on the all-patch integration tree"
( cd "$WORK/tree" && patch -p1 --no-backup-if-mismatch --dry-run \
	< "$LANE/patches/0001-sta-join-hardening.patch" >/dev/null )
( cd "$WORK/tree" && patch -p1 --no-backup-if-mismatch \
	< "$LANE/patches/0001-sta-join-hardening.patch" >/dev/null )
echo "   files changed: $(grep -c '^--- a/' "$LANE/patches/0001-sta-join-hardening.patch")"

echo "== clean build against $KDIR"
( cd "$WORK/tree" && make -s ARCH=arm CROSS_COMPILE="$CROSS" \
	-C "$KDIR" M="$WORK/tree" clean >/dev/null 2>&1 ) || true
if ! make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/tree" modules \
	> "$WORK/build.log" 2>&1; then
	echo "build: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
fi
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "build: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
KO=$WORK/tree/hi5622_wifi.ko
[ -f "$KO" ] || { echo "build: no .ko" >&2; exit 1; }

echo "== strip + assert debug-strip only removed debug sections"
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "build: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"

echo "== artifact assertions"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" 2>/dev/null | wc -l)
[ "$NP" = "47" ] || { echo "build: module param count $NP != 47" >&2; exit 1; }
for p in sta_join allow_frames; do
	modinfo -p "$ART/hi5622_wifi.ko" 2>/dev/null | grep -q "^$p:" || {
		echo "build: module param $p missing" >&2; exit 1; }
done
for s in "STA D2H g2 id3 assoc-completion" \
	 "one join per load" "mask CA 0x400008" "key NOT copied"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build: expected feature string missing: $s" >&2; exit 1; }
done
modinfo "$ART/hi5622_wifi.ko" 2>/dev/null | grep -E '^(depends|vermagic):' || true
USIZE=$(stat -c %s "$ART/hi5622_wifi.ko.unstripped")
USHA=$(sha256sum "$ART/hi5622_wifi.ko.unstripped" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
{
	echo "== wifi-sta-join hardening module (host-built, NOT fired)"
	echo "date        : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
	echo "integ tree  : $INTEG_TREE"
	echo "kernel      : $KDIR"
	echo "artifact    : $ART/hi5622_wifi.ko (debug-stripped)"
	echo "size        : $SIZE"
	echo "sha256      : $SHA"
	echo "unstripped  : $USIZE B sha256 $USHA"
	echo "params      : $NP"
	echo "patch       : $(sha256sum "$LANE/patches/0001-sta-join-hardening.patch" | cut -d' ' -f1)  $(grep -c '^--- a/' "$LANE/patches/0001-sta-join-hardening.patch") files"
	echo "chain       : all-patch 008aca5 (AP+DP+scan+stability+rx-static+beacon-static+wpa-static+sta-static) + sta-join hardening"
} > "$ART/identity.txt"
cat "$ART/identity.txt"

echo "== host selftests"
"$LANE/tools/selftest.sh"

echo "BUILD-VERIFY-PASS"
