#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-vap-start-g0.sh - lane wifi-band1-start-send-3 (task-209)
# HOST-ONLY build: reproduces the pinned band1-start-send-2 tree from the
# committed 0x0242 base + the committed send-2 patch, applies THIS lane's
# patch (the H2D g0-id1 key 0x0065 VAP-start step, R4) and builds the 6.18.60
# module.  No device access, no firmware write, no flash.
# The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed).
#
# Chain (pinned):
#   ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
#     = /tmp/bandstart-build, artifact 371,212 B sha e2312402...a028, 63 params
#   ../wifi-band1-start-send-2/patches/0001-vap-add-index.patch
#     = the send-2 tree (373,056 B sha b8f6fe35...5b71, 64 params)
#   this lane's patches/0001-vap-start-g0.patch
#     = the 0x0065 VAP-start step + the G1..G14 gates (65 params)
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P_BASE2=$DOCS/wifi-band1-start-send-2/patches/0001-vap-add-index.patch
P_THIS=$LANE/patches/0001-vap-start-g0.patch
WORK=/tmp/g0start-build
ART=/tmp/g0start-artifact
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
# the send-2 patch + the tree it must produce (the direct base of this lane)
PIN_PATCH2_SHA=968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb
PIN2_SCAN_SHA=52a5bc0b0938c994ac0d84ae4d871dee207876ab4b26592f586e7582224a727a
PIN2_PROV_SHA=b66da889342e35fb868c154c49c467dd7a0e1b85df6599b5574ca85513a7b764
PIN2_LOGIC_C_SHA=562ff6847ee9565b795a4901203ad4e6869b78f8cd53e419cd6240daa5ad07b5
PIN2_LOGIC_H_SHA=98c4025f645709c12f5a73c88bdbf4618b814ef78be6095d124004af2feaad77
PIN2_KBUILD_SHA=fa58f92cffa3a7ea63c842bfdc93ce23c3e94fc34451d7d11f9a9ee169b6b3fb
PIN2_ART=/tmp/vapbound-artifact/hi5622_wifi.ko
PIN2_ART_SHA=b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71
PIN2_ART_SIZE=373056
PIN2_ART_PARAMS=64
# this lane's patch + the resulting tree shas
PIN_PATCH_SHA=0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c
PIN_SCAN_SHA=da488e3cd69db6c9d6c9f661806eecf21ccc6f44f4f0dd08ab5571c7d1f95370
PIN_LOGIC_C_SHA=7e5edbc42a3c4abd2c619672226870fb13db48c3045fd49252b3a200d321326c
PIN_LOGIC_H_SHA=fc4e43d1d55a36f0d92deafd144f4488515515dc4e7e516a1f3f04c1f6c994d8
PIN_BANDSTART_H_SHA=3fd2c6088af4381736854e5ca1e6c176e0ae659b72a1b9f051f2bd1fef972aea
PIN_VSG_C_SHA=ece5c0d60701d05ce0c1ab5afbb75229cfb2a2734bf9c17fb99706e0eb2e81ff
PIN_VSG_H_SHA=68c2eb79cb77cf33bae6559659b28f8ff58bf6d396e9cd4bec26b7d2e6e4eef8
PIN_KBUILD_SHA=1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d

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
[ -d "$KDIR" ] || { echo "vap-start-g0: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "vap-start-g0: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "vap-start-g0: cross gcc missing" >&2; exit 2; }
[ -d "$BASE" ] || { echo "vap-start-g0: base tree $BASE absent (the fired 0x0242 tree)" >&2; exit 2; }

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
		echo "vap-start-g0: base $f sha $got != pinned $want" >&2
		echo "vap-start-g0: the 0x0242 base drifted - re-review ../wifi-band1-start-send" >&2
		exit 1
	}
done
if [ -f "$PIN_BASE_ART" ]; then
	bs=$(sha256sum "$PIN_BASE_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_BASE_ART")
	bp=$(modinfo -p "$PIN_BASE_ART" | wc -l)
	[ "$bs" = "$PIN_BASE_ART_SHA" ] && [ "$bz" = "$PIN_BASE_ART_SIZE" ] && \
		[ "$bp" = "$PIN_BASE_ART_PARAMS" ] || {
		echo "vap-start-g0: fired 0x0242 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== fired base artifact pinned: ${PIN_BASE_ART_SHA%??????????????????????}... ($PIN_BASE_ART_SIZE B, 63 params)"
fi
[ -f "$P_BASE2" ] || { echo "vap-start-g0: send-2 patch absent: $P_BASE2" >&2; exit 2; }
got=$(sha256sum "$P_BASE2" | cut -d' ' -f1)
[ "$got" = "$PIN_PATCH2_SHA" ] || {
	echo "vap-start-g0: send-2 patch sha $got != pinned" >&2; exit 1; }
got=$(sha256sum "$P_THIS" | cut -d' ' -f1)
[ "$got" = "$PIN_PATCH_SHA" ] || {
	echo "vap-start-g0: lane patch sha $got != pinned $PIN_PATCH_SHA" >&2; exit 1; }
echo "== 0x0242 base + send-2 patch + this lane's patch pins OK"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P_BASE2" >/dev/null ) || {
	echo "vap-start-g0: send-2 patch does not apply to the pinned base" >&2; exit 1; }
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
		echo "vap-start-g0: send-2 tree $f sha $got != pinned $want" >&2; exit 1; }
done
if [ -f "$PIN2_ART" ]; then
	bs=$(sha256sum "$PIN2_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN2_ART")
	bp=$(modinfo -p "$PIN2_ART" | wc -l)
	[ "$bs" = "$PIN2_ART_SHA" ] && [ "$bz" = "$PIN2_ART_SIZE" ] && \
		[ "$bp" = "$PIN2_ART_PARAMS" ] || {
		echo "vap-start-g0: send-2 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== send-2 tree reproduced (64-param artifact pinned)"
fi
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P_THIS" >/dev/null ) || {
	echo "vap-start-g0: lane patch does not apply to the send-2 tree" >&2; exit 1; }
grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1 && {
	echo "vap-start-g0: conflict markers survived - refusing build" >&2; exit 1; }
for f in Kbuild hi5622_scan.c hi5622_scan_logic.c hi5622_scan_logic.h \
	 hi5622_band_start.h hi5622_vap_start_g0.c hi5622_vap_start_g0.h; do
	got=$(sha256sum "$WORK/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN_SCAN_SHA ;;
	hi5622_scan_logic.c) want=$PIN_LOGIC_C_SHA ;;
	hi5622_scan_logic.h) want=$PIN_LOGIC_H_SHA ;;
	hi5622_band_start.h) want=$PIN_BANDSTART_H_SHA ;;
	hi5622_vap_start_g0.c) want=$PIN_VSG_C_SHA ;;
	hi5622_vap_start_g0.h) want=$PIN_VSG_H_SHA ;;
	*) want=$PIN_KBUILD_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "vap-start-g0: patched tree $f sha $got != pinned $want" >&2; exit 1; }
done
echo "== 2. send-2 tree + lane patch applied (pinned result)"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "vap-start-g0: BUILD FAILED" >&2; tail -60 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "vap-start-g0: warnings/errors - refusing artifact" >&2; exit 1; fi
echo "== 3. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "vap-start-g0: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "vap-start-g0: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
echo "== 4. debug-strip keeps .text identical"

MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^vap_start_g0:' || {
	echo "vap-start-g0: missing vap_start_g0" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap_add_index:' || {
	echo "vap-start-g0: missing vap_add_index (send-2 base not carried)" >&2; exit 1; }
for p in vap_start_module band_start band_start_fallback dfs_mode desc_table \
	 ap_chan_wait_ms beacon_static vap5g; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || {
		echo "vap-start-g0: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "65" ] || { echo "vap-start-g0: param count $NP != 65 (64 send-2 + vap_start_g0)" >&2; exit 1; }
for s in "cfg80211: vap_start_g0 pre: mctx=0x%08x" \
	 "cfg80211: vap_start_g0 post: mctx[0x0e]=%u" \
	 "cfg80211: start_ap beacon_static frame %u/%u 'vap_start_g0' posted" \
	 "vap_start_g0=%u is not a valid VAP-start parameter" \
	 "G5: u32[mctx+0x190] is ZERO" \
	 "the key-0x0065 worker would fault at CA 0x46470" \
	 "band_start REFUSED - vap_start_g0=1 but the G12 read" \
	 "vap_start_g0 REFUSED (%d)" \
	 "vap_start_g0: param=%u (%s)"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "vap-start-g0: feature string missing: $s" >&2; exit 1; }
done
grep -qF 'MODULE_PARM_DESC(vap_start_g0,' "$WORK/hi5622_scan.c" || {
	echo "vap-start-g0: MODULE_PARM_DESC(vap_start_g0) missing" >&2; exit 1; }
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "vap-start-g0: cfg80211 symvers != 12" >&2; exit 1; }
echo "== 5. artifact contract PASS (65 params, 0x0065 step + G5/G12 gates, 12 symvers)"

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
	echo "params: $NP (64 send-2 + vap_start_g0)"
	echo "chain: fired 0x0242 base ($PIN_BASE_ART_SHA, 63 params) + send-2 patch ($PIN_PATCH2_SHA) + this lane patch ($PIN_PATCH_SHA)"
	echo "vap_start_g0: read-only uint, default 0; <= 1 with beacon_static=1 enforced before any frame; band-1 post only after G1..G13; G12 gates the 0x0242"
	echo "frame: 40 B g0 id1 key 0x0065 (field B=vap index), body {00,01,01,00,00,00,00,00,00,00,00,00}, total 0x28, node word1 0x00286d2b"
	echo "patched tree shas: scan=$PIN_SCAN_SHA logic.c=$PIN_LOGIC_C_SHA logic.h=$PIN_LOGIC_H_SHA bstart.h=$PIN_BANDSTART_H_SHA vsg.c=$PIN_VSG_C_SHA vsg.h=$PIN_VSG_H_SHA kbuild=$PIN_KBUILD_SHA"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built module with the key-0x0065 VAP-start step (NOT fired)"
