#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-sta-static.sh - reproduce the wifi-sta-static lane artifact on
# top of the final combined AP+DP+scan+stability driver chain and compile-verify
# it against the project 6.18.60 tree.
#
# HOST-ONLY: no device access, no image build, no flash.  The .ko is local
# evidence only and is never committed.  The chain below is the same one the
# wifi-stability lane pinned; this lane adds patches/0001-sta-static.patch.
#
#   ./tools/build-sta-static.sh
#   KDIR_OVERRIDE=... STAGING_OVERRIDE=... ./tools/build-sta-static.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
DP_SRC=$DOCS/wifi-dp-air/src
AP_LANE=$DOCS/wifi-ap-start
POLISH=$DOCS/wifi-image-polish/patches/0001-polish-module-defaults-and-mgmt-diag.patch
DPT=$DOCS/wifi-dp-tuning/patches/0001-txq-consumer-tuning.patch
SCAN=$DOCS/wifi-scan-tuning/patches/0002-scan-tuning-on-polish.patch
STAB=$DOCS/wifi-stability/patches/0001-stability-hardening.patch
OUR_PATCH=$LANE/patches/0001-sta-static.patch
WORK=${WORK:-/tmp/wifi-sta-static-build}
ART=${ART:-$WORK/artifact}

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
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
[ -d "$DP_SRC" ] || { echo "sta-static: missing dp-air base" >&2; exit 2; }
[ -f "$OUR_PATCH" ] || { echo "sta-static: missing lane patch" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "sta-static: 6.18.60 kernel tree not found (set KDIR_OVERRIDE)" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "sta-static: staging dir not found (set STAGING_OVERRIDE)" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "sta-static: cross gcc not found" >&2; exit 2; }

stage_chain() { # $1 = work dir
	dir=$1
	rm -rf "$dir"; mkdir -p "$dir"
	cp -a "$DP_SRC/." "$dir/"
	cp "$AP_LANE/src/hi5622_ap_frames.h" "$AP_LANE/src/hi5622_ap_frames.c" "$dir/"
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd "$dir"/Module.symvers 2>/dev/null || true
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$AP_LANE/provider/apstart-provider.patch" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$POLISH" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$DPT" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$SCAN" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$STAB" >/dev/null )
}

build_module() { # $1 = dir
	dir=$1
	export STAGING_DIR
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd "$dir"/Module.symvers 2>/dev/null || true
	make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$dir" modules \
	     2>&1 | tee "$dir/build.log" >/dev/null
	[ -f "$dir/hi5622_wifi.ko" ] || { echo "sta-static: BUILD FAILED" >&2; tail -40 "$dir/build.log" >&2; exit 1; }
	if grep -iE 'warning:|error:' "$dir/build.log"; then
		echo "sta-static: warnings/errors found - refusing artifact" >&2
		exit 1
	fi
}

rm -rf "$WORK"; mkdir -p "$ART"

echo "== stack 1: combined AP+DP+scan+stability chain (pre-lane baseline)"
BASE=$WORK/base
stage_chain "$BASE"
build_module "$BASE"
B_SHA=$(sha256sum "$BASE/hi5622_wifi.ko" | cut -d' ' -f1)
echo "BASE-BUILD-OK size=$(stat -c %s "$BASE/hi5622_wifi.ko") sha256=$B_SHA"

echo "== stack 2: + patches/0001-sta-static.patch"
P=$WORK/patched
cp -a "$BASE" "$P"
rm -f "$P"/*.o "$P"/*.ko "$P"/*.mod "$P"/*.mod.c "$P"/.*.cmd "$P"/Module.symvers "$P/build.log" 2>/dev/null || true
( cd "$P" && patch -p1 --no-backup-if-mismatch < "$OUR_PATCH" > "$WORK/lane-patch.log" )
for f in hi5622_sta.c hi5622_sta.h hi5622_sta_frames.c hi5622_sta_frames.h \
	 hi5622_sta_logic.c hi5622_sta_logic.h; do
	[ -f "$P/$f" ] || { echo "sta-static: lane patch did not create $f" >&2; exit 1; }
done
build_module "$P"

echo "== artifact assertions"
missing=""
for s in "sta_join" "WAL cfg-event opcode" "sta connect:" "sta disconnect:" \
	 "host STA model validated" "ht_mode" "band_vendor" "vht_cap" \
	 "HT20 word" "WAL join frame NOT posted" "wildcard BSSID"; do
	if ! strings -a "$P/hi5622_wifi.ko" | grep -qF "$s"; then missing="$missing '$s'"; fi
done
[ -z "$missing" ] || { echo "sta-static: missing lane strings:$missing" >&2; exit 1; }
# the WAL allowlist entry must be compiled in (validator table, not a string)
grep -q 'H2D_WAL_CMD_STA_CONNECT, 108u' "$P/h2d_frames.c" || {
	echo "sta-static: WAL allowlist entry missing from the patched tree" >&2; exit 1; }
grep -q 'hi5622_sta.o' "$P/Kbuild" || {
	echo "sta-static: hi5622_sta.o not in Kbuild" >&2; exit 1; }

"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.sta-static.ko" "$P/hi5622_wifi.ko"
"$CROSS"objcopy -O binary --only-section=.text "$P/hi5622_wifi.ko" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.sta-static.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "sta-static: strip changed .text" >&2; exit 1; }
cp "$P/hi5622_wifi.ko" "$ART/hi5622_wifi.sta-static.ko.unstripped"
cp "$P/build.log" "$ART/lane-build.log"
cp "$WORK/lane-patch.log" "$ART/lane-patch.log"
echo "PATCHED-BUILD-OK size=$(stat -c %s "$ART/hi5622_wifi.sta-static.ko") sha256=$(sha256sum "$ART/hi5622_wifi.sta-static.ko" | cut -d' ' -f1)"
echo "ARTIFACT $ART/hi5622_wifi.sta-static.ko"
"${CROSS}modinfo" "$ART/hi5622_wifi.sta-static.ko" 2>/dev/null | grep -E "vermagic|depends|parm.*(sta_join|ht_mode|vht_cap|band_vendor)" || true

echo "== host selftests"
STAGED=$P "$LANE/tools/selftest.sh"
echo "BUILD-EVIDENCE-COMPLETE"
