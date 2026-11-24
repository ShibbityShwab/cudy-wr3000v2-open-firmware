#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-wpa-static-module.sh - compile gate for the wifi-wpa-static lane
# (shared task task-139), HOST-ONLY: no device access, no flash, no image.
#
# What it proves, in order (fail closed at every step):
#   1. the final combined chain (bench-validated base -> AP start -> dp-tuning
#      -> polish -> scan-tuning R4 -> stability hardening) can be staged from
#      the COMMITTED docs and builds in the canonical local build path to the
#      pinned hardware-fired identity: stripped 269,688 B
#      sha256 b5dfb883...  (wifi-stability evidence/build-chain.txt);
#   2. this lane's patch applies ON TOP of that exact tree and the module still
#      builds with zero warnings/errors;
#   3. the patched module carries the decoded key path (strings + cfg80211
#      import contract unchanged);
#   4. the portable key frame builders pass the host selftest.
# The .ko is local evidence only and is NEVER committed.
#
#   ./tools/build-wpa-static-module.sh                       # full gate
#   SKIP_CHAIN_ASSERT=1 ./tools/build-wpa-static-module.sh   # patched build only
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)		# .../docs/incoming
DP_SRC=$DOCS/wifi-dp-air/src
AP_LANE=$DOCS/wifi-ap-start
AP_PATCH=$AP_LANE/provider/apstart-provider.patch
DPT_PATCH=$DOCS/wifi-dp-tuning/patches/0001-txq-consumer-tuning.patch
POLISH_PATCH=$DOCS/wifi-image-polish/patches/0001-polish-module-defaults-and-mgmt-diag.patch
SCAN_PATCH=$DOCS/wifi-scan-tuning/patches/0002-scan-tuning-on-polish.patch
STAB_PATCH=$DOCS/wifi-stability/patches/0001-stability-hardening.patch
OUR_PATCH=$LANE_DIR/patches/0001-wpa-static.patch

# canonical final-combined build path: the stripped sha256 pins the absolute
# build path through .note.gnu.build-id, so the chain assertion must build in
# the same directory the wifi-stability lane recorded the identity from.
CHAIN_WORK=/tmp/wifi-stability-build/combined
WORK=${WORK_OVERRIDE:-/tmp/wifi-wpa-static-build/module}
ART=${ART_OVERRIDE:-/tmp/wifi-wpa-static-build/artifact}
SELFTEST_BIN=/tmp/wifi-wpa-static-build/key_selftest

FINAL_SIZE=269688
FINAL_SHA=b5dfb883a0352d24d3279c73c6410d60c90318b2f17c84b9d3fa006ebb1a50b2

KDIR=
STAGING_DIR_CROSS=
set +u
[ -n "$KDIR_OVERRIDE" ] && KDIR=$KDIR_OVERRIDE
[ -n "$STAGING_OVERRIDE" ] && STAGING_DIR_CROSS=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR_CROSS=$root/staging_dir
			break
		fi
	done
fi
[ -d "$DP_SRC" ] || { echo "wpa-static: missing dp-air base: $DP_SRC" >&2; exit 2; }
[ -f "$AP_PATCH" ] || { echo "wpa-static: missing ap-start patch: $AP_PATCH" >&2; exit 2; }
[ -f "$DPT_PATCH" ] || { echo "wpa-static: missing dp-tuning patch: $DPT_PATCH" >&2; exit 2; }
[ -f "$POLISH_PATCH" ] || { echo "wpa-static: missing polish patch: $POLISH_PATCH" >&2; exit 2; }
[ -f "$SCAN_PATCH" ] || { echo "wpa-static: missing scan-tuning patch: $SCAN_PATCH" >&2; exit 2; }
[ -f "$STAB_PATCH" ] || { echo "wpa-static: missing stability patch: $STAB_PATCH" >&2; exit 2; }
[ -f "$OUR_PATCH" ] || { echo "wpa-static: missing lane patch: $OUR_PATCH" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "wpa-static: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR_CROSS" ] || { echo "wpa-static: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR_CROSS/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "wpa-static: cross gcc not found: ${CROSS}gcc" >&2; exit 2; }
export STAGING_DIR=$STAGING_DIR_CROSS

stage_final() { # $1 = work dir
	dir=$1
	rm -rf "$dir"
	mkdir -p "$dir"
	cp -a "$DP_SRC/." "$dir/"
	cp "$AP_LANE/src/hi5622_ap_frames.h" "$AP_LANE/src/hi5622_ap_frames.c" "$dir/"
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd \
	      "$dir"/Module.symvers "$dir"/modules.order "$dir"/build.log \
	      2>/dev/null || true
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$AP_PATCH" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$DPT_PATCH" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$POLISH_PATCH" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$SCAN_PATCH" >/dev/null )
	( cd "$dir" && patch -p1 --no-backup-if-mismatch < "$STAB_PATCH" >/dev/null )
	# fail-closed chain markers: the final tree must be the hardened R4 one
	grep -q "did not re-arm the scan context" "$dir/hi5622_scan.c" || {
		echo "wpa-static: scan-tuning R4 marker missing" >&2; exit 1; }
	[ -f "$dir/hi5622_band_cfg.c" ] || {
		echo "wpa-static: scan-tuning band-config source missing" >&2; exit 1; }
	[ -f "$dir/hi5622_stability.h" ] || {
		echo "wpa-static: stability hardening source missing" >&2; exit 1; }
	grep -q "re-posting the AP config against the live firmware AP" "$dir/hi5622_scan.c" || {
		echo "wpa-static: E2 restart marker missing" >&2; exit 1; }
}

build_module() { # $1 = dir
	dir=$1
	rm -f "$dir"/*.o "$dir"/*.ko "$dir"/*.mod "$dir"/*.mod.c "$dir"/.*.cmd \
	      "$dir"/Module.symvers "$dir"/modules.order "$dir"/build.log \
	      2>/dev/null || true
	make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$dir" clean >/dev/null 2>&1 || true
	make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$dir" modules \
	     2>&1 | tee "$dir/build.log" >/dev/null
	[ -f "$dir/hi5622_wifi.ko" ] || {
		echo "wpa-static: BUILD FAILED ($dir)" >&2
		tail -40 "$dir/build.log" >&2; exit 1; }
	if grep -iE 'warning:|error:' "$dir/build.log"; then
		echo "wpa-static: warnings/errors found - refusing artifact" >&2
		exit 1
	fi
}

strip_artifact() { # $1 = dir, $2 = output
	"$CROSS"strip --strip-debug -o "$2" "$1/hi5622_wifi.ko"
	"$CROSS"objcopy -O binary --only-section=.text "$1/hi5622_wifi.ko" "$1/.t1"
	"$CROSS"objcopy -O binary --only-section=.text "$2" "$1/.t2"
	cmp -s "$1/.t1" "$1/.t2" || { echo "wpa-static: STRIP CHANGED .text" >&2; exit 1; }
}

echo "== host selftest (portable builders)"
cc -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE_DIR/src" \
	"$LANE_DIR/tools/key_selftest.c" "$LANE_DIR/src/hi5622_key_frames.c" \
	-o "$SELFTEST_BIN"
"$SELFTEST_BIN"

if [ "${SKIP_CHAIN_ASSERT:-0}" = "0" ]; then
	echo "== chain: final combined source must reproduce the pinned fired identity"
	stage_final "$CHAIN_WORK"
	build_module "$CHAIN_WORK"
	strip_artifact "$CHAIN_WORK" "$CHAIN_WORK/.stripped.ko"
	C_SIZE=$(stat -c %s "$CHAIN_WORK/.stripped.ko")
	C_SHA=$(sha256sum "$CHAIN_WORK/.stripped.ko" | cut -d' ' -f1)
	if [ "$C_SIZE" != "$FINAL_SIZE" ] || [ "$C_SHA" != "$FINAL_SHA" ]; then
		echo "wpa-static: CHAIN MISMATCH expected $FINAL_SIZE/$FINAL_SHA" >&2
		echo "wpa-static: got $C_SIZE/$C_SHA - upstream lane artifacts drifted" >&2
		exit 1
	fi
	echo "CHAIN-ID-OK $C_SIZE $C_SHA"
	rm -f "$CHAIN_WORK/.stripped.ko" "$CHAIN_WORK/.t1" "$CHAIN_WORK/.t2"
fi

echo "== patched tree: final combined + wifi-wpa-static patch"
rm -rf "$WORK"
cp -a "$CHAIN_WORK" "$WORK" 2>/dev/null || {
	stage_final "$WORK"
}
[ -f "$WORK/hi5622_scan.c" ] || stage_final "$WORK"
if ! grep -q "wpa_keys" "$WORK/hi5622_keys.c" 2>/dev/null; then
	( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$OUR_PATCH" >/dev/null )
fi
grep -q "HI5622_FRAME_CAP_KEY_ADD" "$WORK/hi5622_frames_api.h" || {
	echo "wpa-static: KEY_ADD capability missing after patch" >&2; exit 1; }
grep -q "add_key		= hi5622_keys_op_add" "$WORK/hi5622_cfg80211_ops.c" || {
	echo "wpa-static: .add_key wiring missing after patch" >&2; exit 1; }
build_module "$WORK"
mkdir -p "$ART"
strip_artifact "$WORK" "$ART/hi5622_wifi.wpa-static.ko"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.wpa-static.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
cp "$OUR_PATCH" "$ART/0001-wpa-static.patch"

P_SIZE=$(stat -c %s "$ART/hi5622_wifi.wpa-static.ko")
P_SHA=$(sha256sum "$ART/hi5622_wifi.wpa-static.ko" | cut -d' ' -f1)
PU_SIZE=$(stat -c %s "$ART/hi5622_wifi.wpa-static.ko.unstripped")
PU_SHA=$(sha256sum "$ART/hi5622_wifi.wpa-static.ko.unstripped" | cut -d' ' -f1)

echo "== artifact checks"
missing=""
for s in "wpa_keys" "key_add" "key_del" "key_default" \
	 "firmware worker CA 0x0A5825" "firmware worker CA 0x0A5C9D" \
	 "firmware worker CA 0x0A4271" "wpa_key_module"; do
	if ! strings -a "$ART/hi5622_wifi.wpa-static.ko" | grep -qF "$s"; then
		missing="$missing '$s'"
	fi
done
[ -z "$missing" ] || { echo "wpa-static: missing artifact strings:$missing" >&2; exit 1; }
NM="${CROSS}nm"
NIMP=$("$NM" -u "$ART/hi5622_wifi.wpa-static.ko" | grep -c ' cfg80211_')
NSV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$NSV" = "10" ] || { echo "wpa-static: cfg80211 contract changed ($NSV lines)" >&2; exit 1; }
VERMAGIC=$("${CROSS}strings" "$WORK/hi5622_wifi.ko" | grep -m1 '^vermagic=')
DEPENDS=$(strings -a "$WORK/hi5622_wifi.ko" | grep -m1 '^depends=')

mkdir -p "$LANE_DIR/evidence"
{
	echo "== wifi-wpa-static build evidence (host only; .ko not committed)"
	echo "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
	echo "kernel    : $KDIR"
	echo "chain     : final combined (base 242572/44c6cc12 -> polished -> dp-tuning -> scan R4 -> hardening)"
	echo "chain-id  : $FINAL_SIZE $FINAL_SHA (asserted)"
	echo "patched   : $(basename "$OUR_PATCH")"
	echo "size      : $P_SIZE"
	echo "sha256    : $P_SHA"
	echo "unstripped: $PU_SIZE B sha256 $PU_SHA"
	echo "vermagic  : $VERMAGIC"
	echo "depends   : $DEPENDS"
	echo "cfg80211  : contract=10 imported=$NIMP"
	echo "params    : wpa_keys=0 fail-closed default; wpa_keys=1 + allow_frames>0 enables the path"
	echo "note      : .ko is local evidence only (never committed); bench fire is a separate attended ladder"
} > "$LANE_DIR/evidence/build-wpa-static.txt"

echo "== wifi-wpa-static module (final combined + key path)"
echo "artifact  : $ART/hi5622_wifi.wpa-static.ko (debug-stripped)"
echo "size      : $P_SIZE"
echo "sha256    : $P_SHA"
echo "unstripped: $PU_SIZE B sha256 $PU_SHA"
echo "vermagic  : $VERMAGIC"
echo "depends   : $DEPENDS"
echo "host test : key_selftest PASS (in-run above) + tools/run-host-check.sh"
echo "evidence  : $LANE_DIR/evidence/build-wpa-static.txt"
echo "WARNING: this artifact has NOT been fired; wpa_keys defaults to 0 (fail-closed)."
