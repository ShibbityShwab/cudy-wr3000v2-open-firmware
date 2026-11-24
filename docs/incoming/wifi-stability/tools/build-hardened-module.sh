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
#   3. + docs/incoming/wifi-dp-tuning/patches/0001-txq-consumer-tuning.patch
#      -> 251,924 B sha256 175a4617...  (bench-validated TXQ publish/completion)
#   4. + this lane's patches/0001-stability-hardening.patch
#      -> hardened final combined module (per-ctx VAP/AP latches, controlled
#         stop_ap + restart, stale-VAP readback guard, stale-reload refusal,
#         remove ordering, hcc_trace hot-path suppression; NOT fired - one
#         attended final validation is required)
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
DPT_LANE=$DOCS/wifi-dp-tuning
SCAN_LANE=$DOCS/wifi-scan-tuning
AP_LANE=$DOCS/wifi-ap-start
POLISH_LANE=$DOCS/wifi-image-polish
AP_PATCH=$AP_LANE/provider/apstart-provider.patch
POLISH_PATCH=$POLISH_LANE/patches/0001-polish-module-defaults-and-mgmt-diag.patch
DPT_PATCH=$DPT_LANE/patches/0001-txq-consumer-tuning.patch
SCAN_PATCH=$SCAN_LANE/patches/0002-scan-tuning-on-polish.patch
OUR_PATCH=$LANE_DIR/patches/0001-stability-hardening.patch
BASEWORK=/tmp/wifi-combined-build
PWORK=/tmp/wifi-image-polish-build
DPTWORK=/tmp/wifi-dp-tuning-build
HWORK=/tmp/wifi-stability-build/combined
ART=/tmp/wifi-stability-artifact
ARTWORK=$ART.work

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
[ -f "$DPT_PATCH" ] || { echo "stability: missing dp-tuning patch: $DPT_PATCH" >&2; exit 2; }
[ -f "$SCAN_PATCH" ] || { echo "stability: missing scan-tuning patch: $SCAN_PATCH" >&2; exit 2; }
[ -f "$OUR_PATCH" ] || { echo "stability: missing hardening patch: $OUR_PATCH" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "stability: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "stability: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "stability: cross gcc not found: ${CROSS}gcc" >&2; exit 2; }

BASE_SIZE=242572
BASE_SHA=44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b
POLISHED_SIZE=242920
POLISHED_SHA=d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6
DPT_SIZE=251924
DPT_SHA=175a4617f208ac7fb67cbd9074cb4eac18876ebf973d905d650f185eed7cc425

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

rm -rf "$BASEWORK" "$PWORK" "$HWORK" "$ART" "$ARTWORK"
mkdir -p "$ARTWORK"

echo "== stack 1: proven combined module (hardware-validated identity)"
stage_proven "$BASEWORK"
build_module "$BASEWORK"
strip_artifact "$BASEWORK" "$ARTWORK/hi5622_wifi.base.ko"
B_SIZE=$(stat -c %s "$ARTWORK/hi5622_wifi.base.ko")
B_SHA=$(sha256sum "$ARTWORK/hi5622_wifi.base.ko" | cut -d' ' -f1)
if [ "$B_SIZE" != "$BASE_SIZE" ] || [ "$B_SHA" != "$BASE_SHA" ]; then
	echo "stability: BASE MISMATCH expected $BASE_SIZE/$BASE_SHA got $B_SIZE/$B_SHA" >&2
	exit 1
fi
echo "BASE-ID-OK $B_SIZE $B_SHA"

echo "== stack 2: polished image module (wifi-image-polish identity)"
stage_proven "$PWORK"
( cd "$PWORK" && patch -p1 --no-backup-if-mismatch < "$POLISH_PATCH" >/dev/null )
build_module "$PWORK"
strip_artifact "$PWORK" "$ARTWORK/hi5622_wifi.polished.ko"
P_SIZE=$(stat -c %s "$ARTWORK/hi5622_wifi.polished.ko")
P_SHA=$(sha256sum "$ARTWORK/hi5622_wifi.polished.ko" | cut -d' ' -f1)
if [ "$P_SIZE" != "$POLISHED_SIZE" ] || [ "$P_SHA" != "$POLISHED_SHA" ]; then
	echo "stability: POLISHED MISMATCH expected $POLISHED_SIZE/$POLISHED_SHA got $P_SIZE/$P_SHA" >&2
	echo "stability: the wifi-image-polish artifact changed; re-pin this lane first" >&2
	exit 1
fi
echo "POLISHED-ID-OK $P_SIZE $P_SHA"

echo "== stack 3: dp-tuning TXQ-publish module (bench-validated identity)"
stage_proven "$DPTWORK"
( cd "$DPTWORK" && patch -p1 --no-backup-if-mismatch < "$DPT_PATCH" >/dev/null )
build_module "$DPTWORK"
strip_artifact "$DPTWORK" "$ARTWORK/hi5622_wifi.dp-tuning.ko"
D_SIZE=$(stat -c %s "$ARTWORK/hi5622_wifi.dp-tuning.ko")
D_SHA=$(sha256sum "$ARTWORK/hi5622_wifi.dp-tuning.ko" | cut -d' ' -f1)
if [ "$D_SIZE" != "$DPT_SIZE" ] || [ "$D_SHA" != "$DPT_SHA" ]; then
	echo "stability: DP-TUNING MISMATCH expected $DPT_SIZE/$DPT_SHA got $D_SIZE/$D_SHA" >&2
	exit 1
fi
echo "DPT-ID-OK $D_SIZE $D_SHA"

echo "== stack 4: final combined = dp-tuning + polished defaults + stability hardening"
rm -rf "$HWORK"
cp -a "$DPTWORK" "$HWORK"
rm -f "$HWORK"/*.o "$HWORK"/*.ko "$HWORK"/*.mod "$HWORK"/*.mod.c "$HWORK"/.*.cmd "$HWORK"/Module.symvers "$HWORK/build.log" 2>/dev/null || true
( cd "$HWORK" && patch -p1 --no-backup-if-mismatch < "$POLISH_PATCH" >/dev/null )
( cd "$HWORK" && patch -p1 --no-backup-if-mismatch < "$SCAN_PATCH" >/dev/null )
( cd "$HWORK" && patch -p1 --no-backup-if-mismatch < "$OUR_PATCH" >/dev/null )
grep -q "re-posting the AP config against the live firmware AP" "$HWORK/hi5622_scan.c" || {
	echo "stability: E2 plain-restart path missing" >&2; exit 1; }
# scan-tuning must be in the SOURCE with its defaults (acceptance for scan-tuning
# lane): 3 new params, batch default 6, batch gap default 300 ms, split message.
grep -q "scan_chan_time_ms" "$HWORK/hi5622_scan.c" || {
	echo "stability: scan-tuning scan_chan_time_ms missing" >&2; exit 1; }
grep -q "hi5622_scan_req_batch = HI5622_SCAN_DEFAULT_BATCH" "$HWORK/hi5622_scan.c" || {
	echo "stability: scan-tuning scan_req_batch default missing" >&2; exit 1; }
grep -q "hi5622_scan_batch_gap_ms = HI5622_SCAN_BATCH_GAP_MS" "$HWORK/hi5622_scan.c" || {
	echo "stability: scan-tuning scan_batch_gap_ms default missing" >&2; exit 1; }
grep -q "split into %u g0 id0xC requests" "$HWORK/hi5622_scan.c" || {
	echo "stability: scan-tuning split message missing" >&2; exit 1; }
grep -q "define HI5622_SCAN_DEFAULT_BATCH[[:space:]]*6u" "$HWORK/hi5622_scan_logic.h" || {
	echo "stability: HI5622_SCAN_DEFAULT_BATCH != 6u" >&2; exit 1; }
grep -q "define HI5622_SCAN_BATCH_GAP_MS[[:space:]]*300u" "$HWORK/hi5622_scan_logic.h" || {
	echo "stability: HI5622_SCAN_BATCH_GAP_MS != 300u" >&2; exit 1; }
# hcc_trace gating must be in the SOURCE (the static helper inlines away, so
# the stripped artifact only carries the param name).
grep -q "hcc_kernel_event_hot" "$HWORK/hcc_core_kernel.c" || {
	echo "stability: hcc_trace gating missing from hcc_core_kernel.c" >&2; exit 1; }
grep -q "hi5622_hcc_trace" "$HWORK/hcc.c" || {
	echo "stability: hcc_trace param missing from hcc.c" >&2; exit 1; }
build_module "$HWORK"
strip_artifact "$HWORK" "$ARTWORK/hi5622_wifi.hardened.ko"
H_SIZE=$(stat -c %s "$ARTWORK/hi5622_wifi.hardened.ko")
H_SHA=$(sha256sum "$ARTWORK/hi5622_wifi.hardened.ko" | cut -d' ' -f1)
HU_SIZE=$(stat -c %s "$HWORK/hi5622_wifi.ko")
HU_SHA=$(sha256sum "$HWORK/hi5622_wifi.ko" | cut -d' ' -f1)
cp "$HWORK/hi5622_wifi.ko" "$ARTWORK/hi5622_wifi.hardened.ko.unstripped"
cp "$HWORK/build.log" "$ARTWORK/build.log"
cp "$OUR_PATCH" "$ARTWORK/0001-stability-hardening.patch"

missing=""
for s in "stale module reload" "ALREADY BOOTED from a previous module load" \
	 "VAP-base/AP latches cleared" "scan REFUSED" \
	 "start_ap REFUSED" "stale/dead VAP" "host-side stop_ap" "mgmt_diag" \
	 "hcc_trace" "scan_chan_time_ms" "scan_req_batch" \
	 "scan_batch_gap_ms" "split into %u g0 id0xC requests" \
	 "re-posting the AP config against the live firmware AP" \
	 "host-side stop_ap (stop_ap_mode=0 default)"; do
	if ! strings -a "$ARTWORK/hi5622_wifi.hardened.ko" | grep -qF "$s"; then missing="$missing '$s'"; fi
done
[ -z "$missing" ] || { echo "stability: missing hardened strings:$missing" >&2; exit 1; }
# Publish the artifact directory atomically: copying during a build must never
# see an empty/partial directory (bench E2 report: lead copied an empty dir).
rm -rf "$ART"
mv "$ARTWORK" "$ART"
[ -s "$ART/hi5622_wifi.hardened.ko" ] || {
	echo "stability: artifact publish produced an empty .ko" >&2; exit 1; }
echo "ARTIFACT-PUBLISHED $ART"
NM="$CROSS""nm"
# scan-tuning acceptance: cfg80211 import contract = 10 lines in the provided
# cfg80211-provider.symvers (the 10th is cfg80211_bss_iter) and the module must
# actually import cfg80211_bss_iter.
SV=$HWORK/cfg80211-provider.symvers
NSV=$(wc -l < "$SV" 2>/dev/null || echo 0)
if [ "$NSV" != "10" ]; then
	echo "stability: cfg80211-provider.symvers has $NSV lines, expected 10" >&2
	exit 1
fi
grep -q 'cfg80211_bss_iter' "$SV" || {
	echo "stability: cfg80211_bss_iter missing from provider.symvers" >&2; exit 1; }
"$NM" -u "$ART/hi5622_wifi.hardened.ko" | grep -q ' cfg80211_bss_iter$' || {
	echo "stability: cfg80211_bss_iter import missing from the .ko" >&2; exit 1; }
NIMP=$("$NM" -u "$ART/hi5622_wifi.hardened.ko" | grep -c ' cfg80211_')
echo "CFG80211-IMPORTS-OK contract=10 imported=$NIMP (cfg80211_bss_iter present)"
VERMAGIC=$("${CROSS}strings" "$HWORK/hi5622_wifi.ko" | grep -m1 '^vermagic=')
DEPENDS=$(strings -a "$HWORK/hi5622_wifi.ko" | grep -m1 '^depends=')
echo "== stability hardened module (final combined)"
echo "artifact  : $ART/hi5622_wifi.hardened.ko (debug-stripped)"
echo "size      : $H_SIZE"
echo "sha256    : $H_SHA"
echo "unstripped: $HU_SIZE B sha256 $HU_SHA"
echo "vermagic  : $VERMAGIC"
echo "depends   : $DEPENDS"
echo "chain     : $BASE_SIZE/$BASE_SHA -> polished $POLISHED_SIZE/$POLISHED_SHA -> dp-tuning $DPT_SIZE/$DPT_SHA -> scan-tuning ladder 247776/613989ff (new 0002) -> hardened"
echo "host test : ./tools/run-hardening-check.sh"
echo "note: .ko is local evidence only (never committed); the combined artifact is NOT fired - attended final validation required"
