#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-vap-bound.sh - lane wifi-band1-start-send-2 (task-207)
# HOST-ONLY build: applies the explicit VAP/module-index fix on the pinned
# wifi-band1-start-send tree (the exact 0x0242 base the Lead fires) and builds
# the 6.18.60 module.  No device access, no firmware write, no flash.
# The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed).
#
# Base chain (pinned):
#   ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
#   = /tmp/bandstart-build, artifact /tmp/bandstart-artifact/hi5622_wifi.ko
#   371,212 B sha256 e2312402...a028, 63 params.
# This lane adds the explicit vap_add_index parameter + the data[4]/hdr[3]
# agreement + the post-add readback (64 params).
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-vap-add-index.patch
WORK=/tmp/vapbound-build
ART=/tmp/vapbound-artifact
set +u
[ -n "${BASE_TREE}" ] && BASE=$BASE_TREE || BASE=/tmp/bandstart-build
set -u
# ---- pins: the fired 0x0242 base (must not drift) ----------------------
PIN_BASE_SCAN_SHA=272e47ac5c457174b94637782d0781367fcb715c0c5da860f4f2889e3f3d0c3b
PIN_BASE_PROV_SHA=c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab
PIN_BASE_FRAMES_SHA=7dc7bc3597e107238a0c9f6641d0b08943d30f331c3d04a4c556961ff5a07121
PIN_BASE_FRAMES_H_SHA=0f1d13c9cd9326cd8d45c38397f2c9d976e90d4050f0ee9868fee5eafb0ef5ac
PIN_BASE_BANDSTART_C_SHA=71019718aec88ebc305b0eed6a4288db27e0b826c5ab8b9da11a064ef0baa57c
PIN_BASE_BANDSTART_H_SHA=dac835ae4f902fd34a54aa65465591b1650581433776a0b1fcb5397e421a3f9b
PIN_BASE_ART=/tmp/bandstart-artifact/hi5622_wifi.ko
PIN_BASE_ART_SHA=e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028
PIN_BASE_ART_SIZE=371212
PIN_BASE_ART_PARAMS=63
PIN_PATCH_SHA=968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE}" ] && KDIR=${KDIR_OVERRIDE}
[ -n "${STAGING_OVERRIDE}" ] && STAGING_DIR=${STAGING_OVERRIDE}
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "vap-bound: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "vap-bound: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "vap-bound: cross gcc missing" >&2; exit 2; }
[ -d "$BASE" ] || { echo "vap-bound: base tree $BASE absent (the fired 0x0242 tree)" >&2; exit 2; }

# ---- base pins ---------------------------------------------------------
for f in hi5622_scan.c hi5622_vapscan_provider.c vapstart_frames.c \
	 vapstart_frames.h hi5622_band_start.c hi5622_band_start.h; do
	got=$(sha256sum "$BASE/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN_BASE_SCAN_SHA ;;
	hi5622_vapscan_provider.c) want=$PIN_BASE_PROV_SHA ;;
	vapstart_frames.c) want=$PIN_BASE_FRAMES_SHA ;;
	vapstart_frames.h) want=$PIN_BASE_FRAMES_H_SHA ;;
	hi5622_band_start.c) want=$PIN_BASE_BANDSTART_C_SHA ;;
	*) want=$PIN_BASE_BANDSTART_H_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "vap-bound: base $f sha $got != pinned $want" >&2
		echo "vap-bound: the 0x0242 base drifted - re-review ../wifi-band1-start-send" >&2
		exit 1
	}
done
if [ -f "$PIN_BASE_ART" ]; then
	bs=$(sha256sum "$PIN_BASE_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_BASE_ART")
	bp=$(modinfo -p "$PIN_BASE_ART" | wc -l)
	[ "$bs" = "$PIN_BASE_ART_SHA" ] && [ "$bz" = "$PIN_BASE_ART_SIZE" ] && \
		[ "$bp" = "$PIN_BASE_ART_PARAMS" ] || {
		echo "vap-bound: fired 0x0242 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== fired base artifact pinned: ${PIN_BASE_ART_SHA%??????????????????????}... ($PIN_BASE_ART_SIZE B, 63 params)"
fi
got=$(sha256sum "$P" | cut -d' ' -f1)
[ "$got" = "$PIN_PATCH_SHA" ] || {
	echo "vap-bound: lane patch sha $got != pinned $PIN_PATCH_SHA" >&2; exit 1; }
echo "== 0x0242 base + lane patch pins OK"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
echo "== 1. pinned 0x0242 base staged: $(ls "$WORK" | wc -l) files"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "vap-bound: lane patch does not apply to the pinned base" >&2; exit 1; }
grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1 && {
	echo "vap-bound: conflict markers survived - refusing build" >&2; exit 1; }
echo "== 2. lane patch applied"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "vap-bound: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "vap-bound: warnings/errors - refusing artifact" >&2; exit 1; fi
echo "== 3. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "vap-bound: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "vap-bound: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
echo "== 4. debug-strip keeps .text identical"

MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^vap_add_index:' || {
	echo "vap-bound: missing vap_add_index" >&2; exit 1; }
for p in vap_start_module band_start band_start_fallback dfs_mode desc_table \
	 ap_chan_wait_ms beacon_static vap5g; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || {
		echo "vap-bound: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "64" ] || { echo "vap-bound: param count $NP != 64 (63 + vap_add_index)" >&2; exit 1; }
for s in "cfg80211: vap_add post: index=%u band=%u" \
	 "vap_add_index=%u is outside the module table" \
	 "vap_add_index=%u != vap_start_module=%u" \
	 "cfg80211: band_start post: module=%u bandobject=%s"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "vap-bound: feature string missing: $s" >&2; exit 1; }
done
grep -qF 'MODULE_PARM_DESC(vap_add_index,' "$WORK/hi5622_vapscan_provider.c" || {
	echo "vap-bound: MODULE_PARM_DESC(vap_add_index) missing" >&2; exit 1; }
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "vap-bound: cfg80211 symvers != 12" >&2; exit 1; }
echo "== 5. artifact contract PASS (64 params, VAP-index gates, 0x0242 lines, 12 symvers)"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
TEXT_SHA=$(sha256sum "$WORK/.t1" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
{
	echo "artifact: $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY - never committed)"
	echo "size: $SIZE B (stripped); unstripped $USIZE B"
	echo "sha256: $SHA"
	echo "unstripped sha256: $USHA"
	echo "text sha256: $TEXT_SHA"
	echo "$VERMAGIC"
	echo "params: $NP (63 fired 0x0242 base + vap_add_index)"
	echo "chain: pinned 0x0242 base (/tmp/bandstart-build, artifact $PIN_BASE_ART_SHA, 63 params) + patches/0001-vap-add-index.patch"
	echo "vap_add_index: read-only uint, default 0 (the single AP); <= 0x12 and == vap_start_module enforced before any frame"
	echo "vap_add post: body data[4] + hdr[3] field B = vap_add_index; read-only band-list readback after the post"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built module with the explicit VAP/module-index contract (NOT fired)"
