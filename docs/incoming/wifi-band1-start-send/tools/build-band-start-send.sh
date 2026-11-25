#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-band-start-send.sh - lane wifi-band1-start-send (task-206)
# HOST-ONLY build: compiles the 5 GHz AP module with the H2D g0-id1 key
# 0x0242 band-start send, its optional 0x0242-forced/0x0243 fallback rungs and
# the fail-closed band_start / band_start_fallback parameters, against the
# project 6.18.60 kernel tree.  No device access, no firmware write, no image,
# no flash.  The .ko is LOCAL EVIDENCE ONLY and is never committed.
#
# Chain (the pinned base the Lead fires):
#   1. ../wifi-dfs-mode-send/tools/build-dfs-mode-send.sh
#      -> /tmp/dfsmode-build + the 0x00F7 table + 0x01F7 mode steps
#         (61 params; the F2 lineage after step 2)
#   2. ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch
#      -> the field-B 0x00 fix (the F2 fire module; artifact 364,184 B sha
#         12427b86..., 61 params, fired 2026-11-27 23:30: the 5 GHz RF was
#         PROGRAMMED)
#   3. this lane's patches/0001-band-start-send.patch
#      -> the H2D 0x0242 band-1 MAC-start send + the optional fallback rungs
#         (63 params = 61 + band_start + band_start_fallback).
#
# Usage:
#   ./tools/build-band-start-send.sh
#   BASE_TREE=/tmp/dfsmode-build ./tools/build-band-start-send.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-band-start-send.patch
F2_PATCH=$DOCS/wifi-fw-dfs-worker/patches/0001-fieldb-context.patch
WORK=/tmp/bandstart-build
ART=/tmp/bandstart-artifact
set +u
if [ -z "${BASE_TREE}" ]; then BASE_TREE=/tmp/dfsmode-build; fi
set -u
# The definitive base pins: the F2 tree that built the bench-fired F2 module.
PIN_F2_ART=/tmp/f2-module.ko
PIN_F2_SHA=12427b8675b9151598d66c0b75410752bcc142178908542ff7d82aa54f059468
PIN_F2_SIZE=364184
PIN_F2_PARAMS=61
PIN_F2_SCAN_SHA=8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7
PIN_F2_LOGIC_C_SHA=75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c
PIN_F2_LOGIC_H_SHA=b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e
PIN_F2_KBUILD_SHA=900fb204ad817e787a0cde9201a02c6dc34d69d893ef0e4f06d140630fcb0759
PIN_F2_DFS_H_SHA=cd3b76aa75c5153654ca1e981d0f767fc100e7cb8e83406c55a43d0b4ce3c70b
PIN_F2_DESC_C_SHA=5af935f29aa9a329aba365da1753c5edd896b74cfe1c0c50665de13b39797810
# The sibling F2 field-B patch (the exact fix that makes the pinned tree F2).
PIN_F2_PATCH_SHA=0530ca45528e0601af5a6f8b8523e3ed8e43cd2237c2361f6cfd03aa3504f926
# This lane's patch + portable sources.
PIN_PATCH_SHA=3ddda8524b782063837fd23a4b2fab1c666f4826b8fa76da394275e6a9de308b
PIN_SRC_C_SHA=71019718aec88ebc305b0eed6a4288db27e0b826c5ab8b9da11a064ef0baa57c
PIN_SRC_H_SHA=dac835ae4f902fd34a54aa65465591b1650581433776a0b1fcb5397e421a3f9b

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
[ -d "$KDIR" ] || { echo "band-start-send: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "band-start-send: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "band-start-send: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "band-start-send: missing patch $P" >&2; exit 2; }
[ -f "$F2_PATCH" ] || { echo "band-start-send: missing F2 field-B patch $F2_PATCH" >&2; exit 2; }

# Rebuild the pinned base chain only when it is absent (same recipe the Lead
# used); never silently substitute another base.
if [ ! -d "$BASE_TREE" ] || [ ! -f "$BASE_TREE/hi5622_scan.c" ]; then
	echo "== 0. base tree absent - rebuilding the pinned chain"
	"$DOCS/wifi-dfs-mode-send/tools/build-dfs-mode-send.sh"
	got=$(sha256sum "$F2_PATCH" | cut -d' ' -f1)
	[ "$got" = "$PIN_F2_PATCH_SHA" ] || {
		echo "band-start-send: F2 field-B patch sha $got != pinned" >&2
		exit 1
	}
	( cd "$BASE_TREE" && patch -p1 --no-backup-if-mismatch < "$F2_PATCH" >/dev/null ) || {
		echo "band-start-send: F2 field-B patch does not apply" >&2
		exit 1
	}
	BASE_TREE=/tmp/dfsmode-build
fi
# The F2 base pins: refuse a drifted tree (the F2 fire module lineage).
for f in hi5622_scan.c hi5622_scan_logic.c hi5622_scan_logic.h Kbuild; do
	got=$(sha256sum "$BASE_TREE/$f" | cut -d' ' -f1)
	case $f in
	hi5622_scan.c) want=$PIN_F2_SCAN_SHA ;;
	hi5622_scan_logic.c) want=$PIN_F2_LOGIC_C_SHA ;;
	hi5622_scan_logic.h) want=$PIN_F2_LOGIC_H_SHA ;;
	Kbuild) want=$PIN_F2_KBUILD_SHA ;;
	esac
	[ "$got" = "$want" ] || {
		echo "band-start-send: base $f sha $got != pinned $want" >&2
		echo "band-start-send: the F2 tree drifted - re-review the base lanes first" >&2
		exit 1
	}
done
got=$(sha256sum "$BASE_TREE/hi5622_dfs_mode.h" | cut -d' ' -f1)
[ "$got" = "$PIN_F2_DFS_H_SHA" ] || {
	echo "band-start-send: F2 hi5622_dfs_mode.h drifted" >&2; exit 1; }
got=$(sha256sum "$BASE_TREE/hi5622_desc_table.c" | cut -d' ' -f1)
[ "$got" = "$PIN_F2_DESC_C_SHA" ] || {
	echo "band-start-send: F2 hi5622_desc_table.c drifted" >&2; exit 1; }
grep -q 'HI5622_DFS_MODE_FB		0x00u' "$BASE_TREE/hi5622_dfs_mode.h" || {
	echo "band-start-send: base is not the F2 lineage (0x01F7 field B != 0)" >&2
	exit 1
}
grep -q 'b\[3\] = 0x00u;' "$BASE_TREE/hi5622_desc_table.c" || {
	echo "band-start-send: base is not the F2 lineage (0x00F7 b[3] != 0)" >&2
	exit 1
}
echo "== F2 base tree pins OK (the field-B fix is in the base)"
if [ -f "$PIN_F2_ART" ]; then
	bs=$(sha256sum "$PIN_F2_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_F2_ART")
	bp=$(modinfo -p "$PIN_F2_ART" | wc -l)
	[ "$bs" = "$PIN_F2_SHA" ] && [ "$bz" = "$PIN_F2_SIZE" ] && \
		[ "$bp" = "$PIN_F2_PARAMS" ] || {
		echo "band-start-send: F2 artifact $bs/$bz B/$bp params != pinned" >&2
		exit 1
	}
	echo "== F2 artifact pinned: ${PIN_F2_SHA%??????????????????????}... (${PIN_F2_SIZE} B, 61 params)"
fi
# This lane's patch + portable sources (refuse a drifted lane).
got=$(sha256sum "$P" | cut -d' ' -f1)
[ "$got" = "$PIN_PATCH_SHA" ] || {
	echo "band-start-send: lane patch sha $got != pinned $PIN_PATCH_SHA" >&2; exit 1; }
got=$(sha256sum "$LANE/src/hi5622_band_start.c" | cut -d' ' -f1)
[ "$got" = "$PIN_SRC_C_SHA" ] || {
	echo "band-start-send: src/hi5622_band_start.c drifted" >&2; exit 1; }
got=$(sha256sum "$LANE/src/hi5622_band_start.h" | cut -d' ' -f1)
[ "$got" = "$PIN_SRC_H_SHA" ] || {
	echo "band-start-send: src/hi5622_band_start.h drifted" >&2; exit 1; }
echo "== lane patch + portable sources pinned"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
rm -rf "$WORK/.git"
echo "== 1. pinned F2 base staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply this lane's patches/0001-band-start-send.patch (the 0x0242 step)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "band-start-send: patch does not apply to the F2 base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "band-start-send: conflict markers survived - refusing build" >&2
	exit 1
fi
# The portable model is host-tested from src/; it must be byte-identical to
# the patched driver source (refuse source drift).
for f in hi5622_band_start.c hi5622_band_start.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "band-start-send: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. patch applied (2 portable sources match)"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "band-start-send: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "band-start-send: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "band-start-send: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "band-start-send: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- artifact contract checks ----------------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^band_start:' || {
	echo "band-start-send: missing module param band_start" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^band_start_fallback:' || {
	echo "band-start-send: missing module param band_start_fallback" >&2; exit 1; }
for p in dfs_mode desc_table ap_chan_wait_ms beacon_static vap5g; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || {
		echo "band-start-send: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "63" ] || { echo "band-start-send: param count $NP != 63 (61 F2 + 2)" >&2; exit 1; }
for s in "cfg80211: start_ap beacon_static frame %u/%u 'band_start' posted (%zu B, g0 id1 key 0x0242" \
	 "cfg80211: band_start post: module=%u bandobject=%s state[1]=%u state[4]=0x%02x counter=%u (%s)" \
	 "cfg80211: band_start fallback: start marker state[1]=%u for module=%u (%s)" \
	 "band_start=%u is not a valid band-start parameter" \
	 "band_start=1 needs the beacon_static=1 vendor order" \
	 "band_start_fallback=%u is not a valid fallback parameter" \
	 "band_start_fallback=1 needs band_start=1" \
	 "band-start builder refused (%d)" \
	 "band-start frame (g0 id1 key 0x0242) not posted (%d)" \
	 "band_start: param=%u (%s) fallback=%u beacon_static=%u last_rc=%d posted=%u forced=%u nudge=%u refused=%u" \
	 "hi5622_band_start" ; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "band-start-send: feature string missing: $s" >&2
		exit 1
	}
done
# The 6.18 module loader drops MODULE_PARM_DESC description strings from
# .modinfo (only parmtype= remains), so the parameter descriptions are
# checked in the patched source instead of the artifact.
grep -qF 'MODULE_PARM_DESC(band_start,' "$WORK/hi5622_scan.c" || {
	echo "band-start-send: MODULE_PARM_DESC(band_start) missing from scan.c" >&2
	exit 1
}
grep -qF 'module_param_named(band_start, hi5622_band_start, uint, 0444)' "$WORK/hi5622_scan.c" || {
	echo "band-start-send: band_start is not a read-only uint parameter" >&2
	exit 1
}
# The two key immediates the vendor emitter uses (0x0242 sync/forced start,
# 0x0243 nudge) must be present in the built unit.
"${CROSS}objdump" -d "$WORK/hi5622_band_start.o" | grep -q 'movw.*#578' || {
	echo "band-start-send: key 0x0242 immediate missing from the built unit" >&2
	exit 1
}
"${CROSS}objdump" -d "$WORK/hi5622_band_start.o" | grep -q 'movw.*#579' || {
	echo "band-start-send: key 0x0243 immediate missing from the built unit" >&2
	exit 1
}
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "band-start-send: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 6. artifact contract PASS (63 params incl band_start + band_start_fallback, key immediates, feature strings, 12 symvers)"

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
	echo "params: $NP (F2 61 + band_start + band_start_fallback)"
	echo "chain: pinned F2 base (364,184 B sha ${PIN_F2_SHA}) + patches/0001-band-start-send.patch"
	echo "band_start: default 0 (off, current behaviour); 1 = post the 33-B 0x0242 sync start after dfs_mode and before ap_chan; >1 refused"
	echo "band_start_fallback: default 0 (off); 1 = forced-start 0x0242 then the 0x0243 nudge if state[1] != 2; requires band_start=1"
	echo "keys: 0x0242 (primary 33 B, forced 33 B) + 0x0243 (nudge 29 B), g0 id1, field B = the AP-start module"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built module with the H2D 0x0242 band-1 MAC-start send (NOT fired)"
