#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-chan-worker.sh - build the corrected 5 GHz channel-activation
# gate + read-only RF-programme gate pack module (lane wifi-fw-chan-worker,
# shared task task-180).
#
# HOST-ONLY: compiles against the project 6.18.60 kernel tree; no device
# access, no firmware write, no image, no flash.  The .ko is local evidence
# only and is never committed.
#
# Chain:
#   1. BASE_TREE (default /tmp/beacon5g-build) = the wifi-5ghz-beacon lane's
#      build tree (../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh); the
#      base artifact there is hi5622_wifi.ko 356,600 B sha256
#      bc1cc0ea...a1bbb, 59 params.  The base source files are pinned below
#      (sha256) and the build refuses on drift.
#   2. patches/0001-chan-gate-fix.patch = the changed/added sources.
#
# Usage:
#   ./tools/build-chan-worker.sh
#   BASE_TREE=/tmp/beacon5g-build ./tools/build-chan-worker.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
P=$LANE/patches/0001-chan-gate-fix.patch
WORK=/tmp/chanworker-build
ART=/tmp/chanworker-artifact
BASE_TREE=${BASE_TREE:-/tmp/beacon5g-build}

# pinned base sources (sha256) - the build refuses a drifted base tree
PIN_KBUILD=6659213e5621d75bd929820442bc7454eca73ad99895423ac8f3cf1a1077f5e7
PIN_SCAN=6a0695a510897a4766587ac93efcebbdb1f7a5f6cf36770196af9593a9b28af4
PIN_B5C=9ab8d1a6a73a88feccc1a7785198dd22184279a13fa9bbf57c996267cf018670
PIN_B5H=e150e0250270198e4d010fb90b8770fa6e9ba6cf280cd6e37a00f11e0d0fee47

KDIR=
STAGING_DIR=
set +u
[ -n "$KDIR_OVERRIDE" ] && KDIR=$KDIR_OVERRIDE
[ -n "$STAGING_OVERRIDE" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "chanworker: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "chanworker: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "chanworker: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "chanworker: missing patch $P" >&2; exit 2; }
[ -d "$BASE_TREE" ] || { echo "chanworker: base tree missing: $BASE_TREE" >&2; exit 2; }

echo "== 0. pin the base tree sources"
for pair in "Kbuild:$PIN_KBUILD" "hi5622_scan.c:$PIN_SCAN" \
	    "hi5622_beacon5g.c:$PIN_B5C" "hi5622_beacon5g.h:$PIN_B5H"; do
	f=$(echo "$pair" | cut -d: -f1)
	want=$(echo "$pair" | cut -d: -f2)
	got=$(sha256sum "$BASE_TREE/$f" | cut -d' ' -f1)
	[ "$got" = "$want" ] || {
		echo "chanworker: base $f sha $got != pinned $want" >&2
		echo "chanworker: the beacon-lane base drifted - re-review it first" >&2
		exit 1
	}
done
echo "   base pinned (scan/b5c/b5h)"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log 2>/dev/null ) || true
echo "== 1. base tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the channel-gate patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "chanworker: patch does not apply to the base tree" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "chanworker: conflict markers survived - refusing build" >&2
	exit 1
fi

# the committed portable model must be byte-identical to the patched tree
for f in hi5622_chan_gate.c hi5622_chan_gate.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "chanworker: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. src/ matches the patched driver sources"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "chanworker: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "chanworker: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "chanworker: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "chanworker: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- contract checks on the artifact ---------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^ap_chan_wait_ms:' || { echo "chanworker: missing ap_chan_wait_ms" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap5g:' || { echo "chanworker: missing vap5g" >&2; exit 1; }
pcount=$(printf '%s\n' "$MI" | wc -l)
[ "$pcount" = 59 ] || { echo "chanworker: $pcount params != 59 (base drift)" >&2; exit 1; }
"$CROSS"nm "$ART/hi5622_wifi.ko.unstripped" | grep -q 'hi5622_chan_gate_word_ready' || {
	echo "chanworker: the decoded predicate is not linked in" >&2; exit 1; }
echo "== 6. contract PASS: 59 params, ap_chan_wait_ms/vap5g present,"
echo "      hi5622_chan_gate_word_ready linked"
echo
echo "artifact: $ART/hi5622_wifi.ko"
sha256sum "$ART/hi5622_wifi.ko"
ls -l "$ART/hi5622_wifi.ko"
echo "NOTE: local evidence only - never committed."
