#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-beacon5g-ready.sh - build the 5 GHz AP module with the
# fail-closed channel-activation gate (lane wifi-5ghz-beacon, task-175).
# HOST-ONLY: compiles against the project 6.18.60 kernel tree; no device
# access, no firmware write, no image, no flash.  The .ko is local evidence
# only and is never committed.
#
# Chain:
#   1. ../wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh
#      = the E2 module under bench test (58 params, stripped sha256
#        13e7bf4c...2cc1abf, the vap5g=1 order fix)
#   2. this lane's patches/0001-beacon5g-gate.patch
#      = corrected module-table readback + the fail-closed 5 GHz channel
#        activation wait (ap_chan_wait_ms, default 0)
#
# Usage:
#   ./tools/build-beacon5g-ready.sh
#   BASE_TREE=/tmp/wifi-vapmsg5g-build ./tools/build-beacon5g-ready.sh
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-beacon5g-gate.patch
WORK=/tmp/beacon5g-build
ART=/tmp/beacon5g-artifact
BASE_TREE=${BASE_TREE:-/tmp/wifi-vapmsg5g-build}
BASE_ART=${BASE_ART:-/tmp/wifi-vapmsg5g-artifact/hi5622_wifi.ko}
BASE_SHA=13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf

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
[ -d "$KDIR" ] || { echo "beacon5g: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "beacon5g: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "beacon5g: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "beacon5g: missing patch $P" >&2; exit 2; }

if [ ! -d "$BASE_TREE" ]; then
	echo "== 0. build the E2 base module (wifi-5ghz-vap-msg lane)"
	"$DOCS/wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh"
fi
[ -d "$BASE_TREE" ] || { echo "beacon5g: base tree missing: $BASE_TREE" >&2; exit 2; }

# Pin the E2 module (the exact artifact the bench has fired).
if [ -f "$BASE_ART" ]; then
	BS=$(sha256sum "$BASE_ART" | cut -d' ' -f1)
	[ "$BS" = "$BASE_SHA" ] || {
		echo "beacon5g: base artifact sha $BS != pinned $BASE_SHA" >&2
		echo "beacon5g: the 5 GHz chain drifted - re-review the base lane first" >&2
		exit 1
	}
	echo "== base artifact pinned: ${BASE_SHA%??????????????????????}..."
fi

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log \
	hi5622_beacon5g.c hi5622_beacon5g.h 2>/dev/null ) || true
echo "== 1. base tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the channel-activation gate patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "beacon5g: patch does not apply to the E2 base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "beacon5g: conflict markers survived - refusing build" >&2
	exit 1
fi

# The portable helper is host-tested from src/; it must be byte-identical
# to the patched driver source (refuse source drift).
for f in hi5622_beacon5g.c hi5622_beacon5g.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "beacon5g: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. src/ matches the patched driver sources"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "beacon5g: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "beacon5g: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "beacon5g: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "beacon5g: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- contract checks on the artifact ---------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^ap_chan_wait_ms:' || { echo "beacon5g: missing module param ap_chan_wait_ms" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap5g:' || { echo "beacon5g: missing module param vap5g" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "59" ] || { echo "beacon5g: param count $NP != 59 (58 E2 + ap_chan_wait_ms)" >&2; exit 1; }
for s in "5GHz channel activation: waiting up to" \
	 "the firmware module channel context did not report channel" \
	 "5GHz channel activation: firmware channel context programmed" \
	 "band publish words: band0=" \
	 "ap_chan_wait_ms=%u is not a valid channel-activation parameter" \
	 "module table unresolved - sleeping"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "beacon5g: feature string missing: $s" >&2
		exit 1
	}
done
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "beacon5g: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 6. artifact contract PASS (59 params incl ap_chan_wait_ms, feature strings, 12 symvers)"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
TEXT_SHA=$(sha256sum "$WORK/.t1" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
echo "== 5 GHz AP module with the fail-closed channel-activation gate (host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE B"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B"
echo ".text sha : $TEXT_SHA"
echo "$VERMAGIC"
echo "params    : $NP (E2 vap5g module + ap_chan_wait_ms)"
echo "chain     : 5ghz-ready (assoc + band5g) + vap5g order patch + beacon5g gate patch"
echo "ap_chan_wait_ms: default 0 (legacy; 1..1000 band-1 wait, >1000 refused)"
