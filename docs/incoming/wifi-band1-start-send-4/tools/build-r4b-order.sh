#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-r4b-order.sh - lane wifi-band1-start-send-4 (task-211)
# HOST-ONLY build: reproduces the pinned band1-start-send-3 tree from the
# committed 0x0242 base + the committed send-2 patch + the committed send-3
# patch, applies THIS lane's R4b order patch (the 0x0124 channel post moved
# before the key-0x0065 VAP start and the 0x0242 band start) and builds the
# 6.18.60 module.  No device access, no firmware write, no flash.
# The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed).
#
# Chain (pinned):
#   ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
#     = /tmp/bandstart-build, artifact 371,212 B sha e2312402...a028, 63 params
#   ../wifi-band1-start-send-2/patches/0001-vap-add-index.patch
#     = the send-2 tree (373,056 B sha b8f6fe35...5b71, 64 params)
#   ../wifi-band1-start-send-3/patches/0001-vap-start-g0.patch
#     = the send-3 tree (382,072 B sha bb0434c2...c9711, 65 params; the R4
#       module that refused at its own G8 gate)
#   this lane's patches/0001-r4b-order.patch
#     = the R4b order (the 0x0124 step before the 0x0065/0x0242 steps)
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P2=$DOCS/wifi-band1-start-send-2/patches/0001-vap-add-index.patch
P3=$DOCS/wifi-band1-start-send-3/patches/0001-vap-start-g0.patch
P4=$LANE/patches/0001-r4b-order.patch
WORK=/tmp/r4border-build
ART=/tmp/r4border-artifact
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
# ---- the send-2 patch + the tree it must produce -----------------------
PIN_P2_SHA=968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb
PIN2_SCAN_SHA=52a5bc0b0938c994ac0d84ae4d871dee207876ab4b26592f586e7582224a727a
PIN2_PROV_SHA=b66da889342e35fb868c154c49c467dd7a0e1b85df6599b5574ca85513a7b764
PIN2_LOGIC_C_SHA=562ff6847ee9565b795a4901203ad4e6869b78f8cd53e419cd6240daa5ad07b5
PIN2_LOGIC_H_SHA=98c4025f645709c12f5a73c88bdbf4618b814ef78be6095d124004af2feaad77
PIN2_KBUILD_SHA=fa58f92cffa3a7ea63c842bfdc93ce23c3e94fc34451d7d11f9a9ee169b6b3fb
PIN2_ART=/tmp/vapbound-artifact/hi5622_wifi.ko
PIN2_ART_SHA=b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71
PIN2_ART_SIZE=373056
PIN2_ART_PARAMS=64
# ---- the send-3 patch + the tree it must produce (the direct base) -----
PIN_P3_SHA=0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c
PIN3_SCAN_SHA=da488e3cd69db6c9d6c9f661806eecf21ccc6f44f4f0dd08ab5571c7d1f95370
PIN3_LOGIC_C_SHA=7e5edbc42a3c4abd2c619672226870fb13db48c3045fd49252b3a200d321326c
PIN3_LOGIC_H_SHA=fc4e43d1d55a36f0d92deafd144f4488515515dc4e7e516a1f3f04c1f6c994d8
PIN3_BANDSTART_H_SHA=3fd2c6088af4381736854e5ca1e6c176e0ae659b72a1b9f051f2bd1fef972aea
PIN3_VSG_C_SHA=ece5c0d60701d05ce0c1ab5afbb75229cfb2a2734bf9c17fb99706e0eb2e81ff
PIN3_VSG_H_SHA=68c2eb79cb77cf33bae6559659b28f8ff58bf6d396e9cd4bec26b7d2e6e4eef8
PIN3_KBUILD_SHA=1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d
PIN3_ART=/tmp/g0start-artifact/hi5622_wifi.ko
PIN3_ART_SHA=bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711
PIN3_ART_SIZE=382072
PIN3_ART_PARAMS=65
# ---- this lane's patch + the resulting tree shas -----------------------
PIN_P4_SHA=3628899843628984b1373f9c1e2ccd6539aff8920ac3188401a24a83b1c21775
PIN4_SCAN_SHA=ab14fa07f53ef199c503178ce8ae8c0966f6b0cf3ee5b435b2c72e54618b2375
PIN4_LOGIC_C_SHA=ab7b8bac84f8a1f57d3ccd0b2f6ceca730b33aa67d77244b4df9a54ec7a3d97b
PIN4_LOGIC_H_SHA=3bfe123919f4d1eb466173b8f3c811cb5f02e3a10f6f5139af8b14f797e4058e
PIN4_BANDSTART_H_SHA=443b071d3ce131c21d46c54e9f7f510221cf75d31a39603c8e68907394bd1ddc
PIN4_VSG_H_SHA=fe165a82f87b745c54354976e52b387ee1d3d0e537f1806a86aa29712903df21

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
[ -d "$KDIR" ] || { echo "r4b-order: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "r4b-order: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "r4b-order: cross gcc missing" >&2; exit 2; }

# ---- rebuild the pinned chain only when it is absent (never substitute) -
if [ ! -d "$BASE" ] || [ ! -f "$BASE/hi5622_scan.c" ]; then
	echo "== 0. pinned base chain absent - rebuilding (send -> send-2 -> send-3)"
	"$DOCS/wifi-band1-start-send/tools/build-band-start-send.sh"
	"$DOCS/wifi-band1-start-send-2/tools/build-vap-bound.sh"
	"$DOCS/wifi-band1-start-send-3/tools/build-vap-start-g0.sh"
fi
[ -d "$BASE" ] || { echo "r4b-order: base tree $BASE absent (the fired 0x0242 tree)" >&2; exit 2; }

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
		echo "r4b-order: base $f sha $got != pinned $want" >&2
		echo "r4b-order: the 0x0242 base drifted - re-review ../wifi-band1-start-send" >&2
		exit 1
	}
done
if [ -f "$PIN_BASE_ART" ]; then
	bs=$(sha256sum "$PIN_BASE_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_BASE_ART")
	bp=$(modinfo -p "$PIN_BASE_ART" | wc -l)
	[ "$bs" = "$PIN_BASE_ART_SHA" ] && [ "$bz" = "$PIN_BASE_ART_SIZE" ] && \
		[ "$bp" = "$PIN_BASE_ART_PARAMS" ] || {
		echo "r4b-order: fired 0x0242 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== fired base artifact pinned ($PIN_BASE_ART_SIZE B, 63 params)"
fi
for p in "$P2" "$P3" "$P4"; do
	[ -f "$p" ] || { echo "r4b-order: patch absent: $p" >&2; exit 2; }
done
got=$(sha256sum "$P2" | cut -d' ' -f1); [ "$got" = "$PIN_P2_SHA" ] || {
	echo "r4b-order: send-2 patch sha $got != pinned" >&2; exit 1; }
got=$(sha256sum "$P3" | cut -d' ' -f1); [ "$got" = "$PIN_P3_SHA" ] || {
	echo "r4b-order: send-3 patch sha $got != pinned" >&2; exit 1; }
got=$(sha256sum "$P4" | cut -d' ' -f1); [ "$got" = "$PIN_P4_SHA" ] || {
	echo "r4b-order: lane patch sha $got != pinned $PIN_P4_SHA" >&2; exit 1; }
echo "== 0x0242 base + send-2 patch + send-3 patch + this lane's patch pins OK"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
rm -rf "$WORK/.git"
echo "== 1. pinned 0x0242 base staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the send-2 patch (the vap_add_index base)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P2" >/dev/null ) || {
	echo "r4b-order: send-2 patch does not apply to the pinned base" >&2; exit 1; }
for f in hi5622_scan.c hi5622_vapscan_provider.c hi5622_scan_logic.c \
	 hi5622_scan_logic.h Kbuild; do
	got=$(sha256sum "$WORK/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN2_SCAN_SHA ;;
	hi5622_vapscan_provider.c) want=$PIN2_PROV_SHA ;;
	hi5622_scan_logic.c) want=$PIN2_LOGIC_C_SHA ;;
	hi5622_scan_logic.h) want=$PIN2_LOGIC_H_SHA ;;
	*) want=$PIN2_KBUILD_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "r4b-order: send-2 tree $f sha $got != pinned $want" >&2; exit 1; }
done
if [ -f "$PIN2_ART" ]; then
	bs=$(sha256sum "$PIN2_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN2_ART")
	bp=$(modinfo -p "$PIN2_ART" | wc -l)
	[ "$bs" = "$PIN2_ART_SHA" ] && [ "$bz" = "$PIN2_ART_SIZE" ] && \
		[ "$bp" = "$PIN2_ART_PARAMS" ] || {
		echo "r4b-order: send-2 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== send-2 tree reproduced (64-param artifact pinned)"
fi

echo "== 3. apply the send-3 patch (the 0x0065 VAP-start step = the R4 module)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P3" >/dev/null ) || {
	echo "r4b-order: send-3 patch does not apply to the send-2 tree" >&2; exit 1; }
grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1 && {
	echo "r4b-order: conflict markers survived - refusing build" >&2; exit 1; }
for f in hi5622_scan.c hi5622_scan_logic.c hi5622_scan_logic.h \
	 hi5622_band_start.h hi5622_vap_start_g0.c hi5622_vap_start_g0.h Kbuild; do
	got=$(sha256sum "$WORK/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN3_SCAN_SHA ;;
	hi5622_scan_logic.c) want=$PIN3_LOGIC_C_SHA ;;
	hi5622_scan_logic.h) want=$PIN3_LOGIC_H_SHA ;;
	hi5622_band_start.h) want=$PIN3_BANDSTART_H_SHA ;;
	hi5622_vap_start_g0.c) want=$PIN3_VSG_C_SHA ;;
	hi5622_vap_start_g0.h) want=$PIN3_VSG_H_SHA ;;
	*) want=$PIN3_KBUILD_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "r4b-order: send-3 tree $f sha $got != pinned $want" >&2; exit 1; }
done
if [ -f "$PIN3_ART" ]; then
	bs=$(sha256sum "$PIN3_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN3_ART")
	bp=$(modinfo -p "$PIN3_ART" | wc -l)
	[ "$bs" = "$PIN3_ART_SHA" ] && [ "$bz" = "$PIN3_ART_SIZE" ] && \
		[ "$bp" = "$PIN3_ART_PARAMS" ] || {
		echo "r4b-order: send-3 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== send-3 tree reproduced (65-param R4 artifact pinned)"
fi

echo "== 4. apply this lane's R4b order patch (0x0124 before 0x0065/0x0242)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P4" >/dev/null ) || {
	echo "r4b-order: lane patch does not apply to the send-3 tree" >&2; exit 1; }
grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1 && {
	echo "r4b-order: conflict markers survived - refusing build" >&2; exit 1; }
for f in hi5622_scan.c hi5622_scan_logic.c hi5622_scan_logic.h \
	 hi5622_band_start.h hi5622_vap_start_g0.h; do
	got=$(sha256sum "$WORK/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN4_SCAN_SHA ;;
	hi5622_scan_logic.c) want=$PIN4_LOGIC_C_SHA ;;
	hi5622_scan_logic.h) want=$PIN4_LOGIC_H_SHA ;;
	hi5622_band_start.h) want=$PIN4_BANDSTART_H_SHA ;;
	*) want=$PIN4_VSG_H_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "r4b-order: patched tree $f sha $got != pinned $want" >&2; exit 1; }
done
echo "== 5. R4b-patched tree pinned (the plan + post-site order)"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "r4b-order: BUILD FAILED" >&2; tail -60 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "r4b-order: warnings/errors - refusing artifact" >&2; exit 1; fi
echo "== 6. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "r4b-order: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "r4b-order: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
echo "== 7. debug-strip keeps .text identical"

MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^vap_start_g0:' || {
	echo "r4b-order: missing vap_start_g0" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap_add_index:' || {
	echo "r4b-order: missing vap_add_index (send-2 base not carried)" >&2; exit 1; }
for p in vap_start_module band_start band_start_fallback dfs_mode desc_table \
	 ap_chan_wait_ms beacon_static vap5g; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || {
		echo "r4b-order: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "65" ] || { echo "r4b-order: param count $NP != 65 (unchanged from send-3)" >&2; exit 1; }
# The R4b placement strings: the 0x0065 step logs the 0x0124 post as its
# predecessor and the 0x0242 step logs all three predecessors.
for s in "cfg80211: vap_start_g0 pre: mctx=0x%08x" \
	 "cfg80211: vap_start_g0 post: mctx[0x0e]=%u" \
	 "cfg80211: start_ap beacon_static frame %u/%u 'vap_start_g0' posted" \
	 "after the 0x0124 channel post (mctx[0x10..0x13] stored) and before the 0x0242 band start" \
	 "subcmd=1 sync start, field B=%u) - after the 0x01F7 mode, the 0x0124 channel post and the 0x0065 VAP start" \
	 "vap_start_g0=%u is not a valid VAP-start parameter" \
	 "G5: u32[mctx+0x190] is ZERO" \
	 "the key-0x0065 worker would fault at CA 0x46470" \
	 "band_start REFUSED - vap_start_g0=1 but the G12 read" \
	 "vap_start_g0 REFUSED (%d)" \
	 "vap_start_g0: param=%u (%s)"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "r4b-order: feature string missing: $s" >&2; exit 1; }
done
grep -qF 'MODULE_PARM_DESC(vap_start_g0,' "$WORK/hi5622_scan.c" || {
	echo "r4b-order: MODULE_PARM_DESC(vap_start_g0) missing" >&2; exit 1; }
grep -qF '1=post the 40-byte frame after the 0x0124 channel post and before the 0x0242 band start' \
	"$WORK/hi5622_scan.c" || {
	echo "r4b-order: vap_start_g0 parameter description not reordered" >&2; exit 1; }
# The plan model must emit the R4b order: ap_chan before vap_start_g0 before
# band_start (the host tests check this in depth; this is the build gate).
PLAN_ORDER=$(awk '/out\[n\+\+\] = HI5622_BEACON_STEP_VAP_ADD;/,/out\[n\+\+\] = HI5622_BEACON_STEP_SSID;/' \
	"$WORK/hi5622_scan_logic.c" | \
	grep 'HI5622_BEACON_STEP_' | sed 's/.*HI5622_BEACON_STEP_//;s/;.*//' | \
	tr '\n' ' ')
case "$PLAN_ORDER" in
*"VAP_ADD DESC_TABLE DFS_MODE CHAN VAP_START_G0 BAND_START SSID "*) ;;
*)	echo "r4b-order: plan model is not the R4b order: $PLAN_ORDER" >&2; exit 1 ;;
esac
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "r4b-order: cfg80211 symvers != 12" >&2; exit 1; }
echo "== 8. artifact contract PASS (65 params, R4b order strings + plan order, 12 symvers)"

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
	echo "params: $NP (64 send-2 + vap_start_g0; unchanged by the reorder)"
	echo "chain: fired 0x0242 base ($PIN_BASE_ART_SHA, 63 params) + send-2 patch ($PIN_P2_SHA) + send-3 patch ($PIN_P3_SHA) + this order patch ($PIN_P4_SHA)"
	echo "R4b order: vap_add -> desc_table -> dfs_mode -> ap_chan -> vap_start_g0 -> band_start -> ap_ssid -> ap_cfg -> ap_vap_start"
	echo "vap_start_g0: read-only uint, default 0; G8 (mctx[0x11]==1) now passes after the ap_chan; G12 (mctx[0x0E]==1) gates the 0x0242; G5 (mctx[0x190]!=0) gates the 0x0065"
	echo "patched tree shas: scan=$PIN4_SCAN_SHA logic.c=$PIN4_LOGIC_C_SHA logic.h=$PIN4_LOGIC_H_SHA bstart.h=$PIN4_BANDSTART_H_SHA vsg.h=$PIN4_VSG_H_SHA (vsg.c + kbuild unchanged from send-3)"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built R4b-order module (NOT fired)"
