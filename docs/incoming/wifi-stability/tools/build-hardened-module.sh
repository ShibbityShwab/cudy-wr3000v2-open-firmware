#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-hardened-module.sh - reproduce the hardware-fired combined AP+DP
# module AND the polished image module, then build the stability-hardened module
# on top of them (wifi-stability lane, shared task task-136).
#
# HOST-ONLY: builds against the project 6.18.60 tree; no device access, no image,
# no flash.  The .ko is local evidence only and is NEVER committed.
#
# Chain (each step asserts its identity, so a drift fails closed; the first two
# identities are external project pins, the third is this lane's artifact):
#   1. docs/incoming/wifi-dp-air/src + wifi-ap-start v7 patch + ap frames
#      -> 242,572 B sha256 44c6cc12...  (hardware-fired combined module)
#   2. + docs/incoming/wifi-image-polish/patches/0001-...patch
#      -> 242,920 B sha256 d145a205...  (polished image module, NOT fired)
#   3. + this lane's patches/0001-stability-hardening.patch
#      -> hardened module  (per-ctx VAP/AP latches, controlled stop_ap +
#         restart, stale-VAP readback guard, stale-reload refusal, remove
#         ordering; NOT fired - one attended ladder E2/E3 run is required)
#
# Build-dir note: .note.gnu.build-id hashes the debug info, which embeds the
# absolute build path.  Stack 1 and stack 2 are therefore built in exactly the
# directories the canonical identities were recorded from; a different path
# changes only the stripped sha256.
#
#   ./tools/build-hardened-module.sh
#   KDIR_OVERRIDE=... STAGING_OVERRIDE=... ./tools/build-hardened-module.sh
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)            # .../docs/incoming
DP_SRC=$DOCS/wifi-dp-air/src
AP_LANE=$DOCS/wifi-ap-start
POLISH_LANE=$DOCS/wifi-image-polish
AP_PATCH=$AP_LANE/provider/apstart-provider.patch
POLISH_PATCH=$POLISH_LANE/patches/0001-polish-module-defaults-and-mgmt-diag.patch
OUR_PATCH=$LANE_DIR/patches/0001-stability-hardening.patch
BASEWORK=/tmp/wifi-combined-build
PWORK=/tmp/wifi-image-polish-build
HWORK=/tmp/wifi-stability-build/hard
ART=/tmp/wifi-stability-artifact

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
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
[ -d "$DP_SRC" ] || { echo "stability: missing dp-air base: $DP_SRC" >&2; exit 2; }
[ -f "$AP_PATCH" ] || { echo "stability: missing ap-start patch: $AP_PATCH" >&2; exit 2; }
[ -f "$POLISH_PATCH" ] || { echo "stability: missing polish patch: $POLISH_PATCH" >&2; exit 2; }
[ -f "$OUR_PATCH" ] || { echo "stability: missing hardening patch: $OUR_PATCH" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "stability: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "stability: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "stability: cross gcc not found: ${CROSS}gcc" >&2; exit 2; }

BASE_SIZE=242572
BASE_SHA=44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b
POLISHED_SIZE=242920
POLISHED_SHA=d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6

stage_proven() { # $1 = work dir
	dir=$1
	rm -rf "$dir"
	mkdir -p "$dir"
	cp -a "$DP_SRC/." "$dir/"
	cp "$AP_LANE/src/hi5622_ap_frames.h" "$AP_LANE/src/hi5622_ap_frames.c" "$dir/"
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd "$dir"/Module.symvers 2>/dev/null || true
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$AP_PATCH" >/dev/null )
}

build_module() { # $1 = dir
	dir=$1
	export STAGING_DIR
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd "$dir"/Module.symvers 2>/dev/null || true
	make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$dir" clean >/dev/null 2>&1 || true
	make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$dir" modules \
	     2>&1 | tee "$dir/build.log" >/dev/null
	[ -f "$dir/hi5622_wifi.ko" ] || { echo "stability: BUILD FAILED ($dir)" >&2; tail -40 "$dir/build.log" >&2; exit 1; }
	if grep -iE 'warning:|error:' "$dir/build.log"; then
		echo "stability: warnings/errors found - refusing artifact" >&2
		exit 1
	fi
}

strip_artifact() { # $1 = dir, $2 = output
	"$CROSS"strip --strip-debug -o "$2" "$1/hi5622_wifi.ko"
	"$CROSS"objcopy -O binary --only-section=.text "$1/hi5622_wifi.ko" "$1/.t1"
	"$CROSS"objcopy -O binary --only-section=.text "$2" "$1/.t2"
	cmp -s "$1/.t1" "$1/.t2" || { echo "stability: STRIP CHANGED .text" >&2; exit 1; }
}

rm -rf "$BASEWORK" "$PWORK" "$HWORK" "$ART"
mkdir -p "$ART"

echo "== stack 1: proven combined module (hardware-validated identity)"
stage_proven "$BASEWORK"
build_module "$BASEWORK"
strip_artifact "$BASEWORK" "$ART/hi5622_wifi.base.ko"
B_SIZE=$(stat -c %s "$ART/hi5622_wifi.base.ko")
B_SHA=$(sha256sum "$ART/hi5622_wifi.base.ko" | cut -d' ' -f1)
if [ "$B_SIZE" != "$BASE_SIZE" ] || [ "$B_SHA" != "$BASE_SHA" ]; then
	echo "stability: BASE MISMATCH expected $BASE_SIZE/$BASE_SHA got $B_SIZE/$B_SHA" >&2
	exit 1
fi
echo "BASE-ID-OK $B_SIZE $B_SHA"

echo "== stack 2: polished image module (wifi-image-polish identity)"
stage_proven "$PWORK"
( cd "$PWORK" && patch -p1 --no-backup-if-mismatch < "$POLISH_PATCH" >/dev/null )
build_module "$PWORK"
strip_artifact "$PWORK" "$ART/hi5622_wifi.polished.ko"
P_SIZE=$(stat -c %s "$ART/hi5622_wifi.polished.ko")
P_SHA=$(sha256sum "$ART/hi5622_wifi.polished.ko" | cut -d' ' -f1)
if [ "$P_SIZE" != "$POLISHED_SIZE" ] || [ "$P_SHA" != "$POLISHED_SHA" ]; then
	echo "stability: POLISHED MISMATCH expected $POLISHED_SIZE/$POLISHED_SHA got $P_SIZE/$P_SHA" >&2
	echo "stability: the wifi-image-polish artifact changed; re-pin this lane first" >&2
	exit 1
fi
echo "POLISHED-ID-OK $P_SIZE $P_SHA"

echo "== stack 3: stability-hardened module"
rm -rf "$HWORK"
cp -a "$PWORK" "$HWORK"
rm -f "$HWORK"/*.o "$HWORK"/*.ko "$HWORK"/*.mod "$HWORK"/*.mod.c "$HWORK"/.*.cmd "$HWORK"/Module.symvers "$HWORK/build.log" 2>/dev/null || true
( cd "$HWORK" && patch -p1 --no-backup-if-mismatch < "$OUR_PATCH" >/dev/null )
build_module "$HWORK"
strip_artifact "$HWORK" "$ART/hi5622_wifi.hardened.ko"
H_SIZE=$(stat -c %s "$ART/hi5622_wifi.hardened.ko")
H_SHA=$(sha256sum "$ART/hi5622_wifi.hardened.ko" | cut -d' ' -f1)
HU_SIZE=$(stat -c %s "$HWORK/hi5622_wifi.ko")
HU_SHA=$(sha256sum "$HWORK/hi5622_wifi.ko" | cut -d' ' -f1)
cp "$HWORK/hi5622_wifi.ko" "$ART/hi5622_wifi.hardened.ko.unstripped"
cp "$HWORK/build.log" "$ART/build.log"
cp "$OUR_PATCH" "$ART/0001-stability-hardening.patch"

missing=""
for s in "stale module reload" "ALREADY BOOTED from a previous module load" \
	 "VAP-base/AP latches cleared" "controlled stop_ap" "scan REFUSED" \
	 "start_ap REFUSED" "stale/dead VAP" "host-side stop_ap" "mgmt_diag"; do
	if ! strings -a "$ART/hi5622_wifi.hardened.ko" | grep -qF "$s"; then missing="$missing '$s'"; fi
done
[ -z "$missing" ] || { echo "stability: missing hardened strings:$missing" >&2; exit 1; }
VERMAGIC=$("${CROSS}strings" "$HWORK/hi5622_wifi.ko" | grep -m1 '^vermagic=')
DEPENDS=$(strings -a "$HWORK/hi5622_wifi.ko" | grep -m1 '^depends=')
echo "== stability hardened module"
echo "artifact  : $ART/hi5622_wifi.hardened.ko (debug-stripped)"
echo "size      : $H_SIZE"
echo "sha256    : $H_SHA"
echo "unstripped: $HU_SIZE B sha256 $HU_SHA"
echo "vermagic  : $VERMAGIC"
echo "depends   : $DEPENDS"
echo "chain     : $BASE_SIZE/$BASE_SHA -> $POLISHED_SIZE/$POLISHED_SHA -> hardened"
echo "host test : ./tools/run-hardening-check.sh"
echo "note: .ko is local evidence only (never committed); NOT fired - attended ladder E2/E3 required"
