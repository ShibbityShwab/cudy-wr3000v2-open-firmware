#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-dfs-mode-send.sh - lane wifi-dfs-mode-send (task-190)
# HOST-ONLY build: compiles the 5 GHz AP module with the H2D g0-id1 key
# 0x01F7 DFS/RF mode send and the fail-closed dfs_mode parameter, against the
# project 6.18.60 kernel tree.  No device access, no firmware write, no image,
# no flash.  The .ko is LOCAL EVIDENCE ONLY and is never committed.
#
# Chain (the pinned base the Lead fires):
#   1. ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh
#      -> /tmp/beacon5g-build (356,600 B sha bc1cc0ea..., 59 params);
#   2. ../wifi-r1-band-diff/tools/build-r1-band.sh
#      -> /tmp/r1-band-build + /tmp/r1-band-artifact/hi5622_wifi.ko
#         356,612 B sha e8b17f72...4abd4, 59 params (CW-0/R1 module);
#   3. ../wifi-desc-table-send/patches/0001-desc-table-send.patch
#      -> the H2D 0x00F7 descriptor-table send + the fail-closed desc_table
#         parameter (60 params, 360,448 B sha aac6177c...e800e3);
#   4. this lane's patches/0001-dfs-mode-send.patch
#      -> the H2D 0x01F7 DFS/RF mode send (key 0x01F7, len 1, body {0x03})
#         + the fail-closed dfs_mode parameter (61 params).
#
# Usage:
#   ./tools/build-dfs-mode-send.sh
#   BASE_TREE=/tmp/r1-band-build ./tools/build-dfs-mode-send.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-dfs-mode-send.patch
P_DESC=$DOCS/wifi-desc-table-send/patches/0001-desc-table-send.patch
WORK=/tmp/dfsmode-build
ART=/tmp/dfsmode-artifact
set +u
if [ -z "${BASE_TREE}" ]; then BASE_TREE=/tmp/r1-band-build; fi
set -u
# The definitive base pin: the r1-band artifact the CW-0/R1 fires ran.
PIN_PROVIDER_PATCHED=c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab
PIN_PROVIDER_BASE=bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828
PIN_BASE_ART=/tmp/r1-band-artifact/hi5622_wifi.ko
PIN_BASE_SHA=e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4
PIN_BASE_SIZE=356612
PIN_BASE_PARAMS=59
# The sibling desc-table-send patch + its portable sources (the exact base
# this lane's patch applies to); refused on drift so a fire cannot use a
# different 0x00F7 build under this lane's name.
PIN_DESC_PATCH_SHA=9c42076f4f5faecf352f6aecf5d6ba8b88edb73a454e26872ec543233cdfce2f
PIN_DESC_SRC_C_SHA=976c544dbabfcb29f32530f083903ca6f708f7cbb8407576d578199a99664d9f
PIN_DESC_SRC_H_SHA=5321d1f7ff1f5e494ba65013fb815861d972e6f2161bf1ce8e0466665551f9d1

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
[ -d "$KDIR" ] || { echo "dfs-mode-send: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "dfs-mode-send: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "dfs-mode-send: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "dfs-mode-send: missing patch $P" >&2; exit 2; }
[ -f "$P_DESC" ] || { echo "dfs-mode-send: missing sibling patch $P_DESC" >&2; exit 2; }

# Rebuild the pinned base chain only when it is absent (same recipe the Lead
# used); never silently substitute another base.
if [ ! -d "$BASE_TREE" ] || [ ! -f "$BASE_TREE/hi5622_scan.c" ]; then
	echo "== 0. base tree absent - rebuilding the pinned chain"
	"$DOCS/wifi-5ghz-beacon/tools/build-beacon5g-ready.sh"
	"$DOCS/wifi-r1-band-diff/tools/build-r1-band.sh"
	BASE_TREE=/tmp/r1-band-build
fi
got=$(sha256sum "$BASE_TREE/hi5622_vapscan_provider.c" | cut -d' ' -f1)
[ "$got" = "$PIN_PROVIDER_PATCHED" ] || {
	echo "dfs-mode-send: base provider sha $got != pinned $PIN_PROVIDER_PATCHED" >&2
	echo "dfs-mode-send: the r1-band base drifted - re-review the base lanes first" >&2
	exit 1
}
if [ -f /tmp/beacon5g-build/hi5622_vapscan_provider.c ]; then
	gb=$(sha256sum /tmp/beacon5g-build/hi5622_vapscan_provider.c | cut -d' ' -f1)
	[ "$gb" = "$PIN_PROVIDER_BASE" ] || {
		echo "dfs-mode-send: beacon5g base provider sha $gb != pinned $PIN_PROVIDER_BASE" >&2
		exit 1
	}
	echo "== beacon5g base pin OK, r1-band patched base pin OK"
fi
if [ -f "$PIN_BASE_ART" ]; then
	bs=$(sha256sum "$PIN_BASE_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_BASE_ART")
	[ "$bs" = "$PIN_BASE_SHA" ] && [ "$bz" = "$PIN_BASE_SIZE" ] || {
		echo "dfs-mode-send: base artifact $bs/$bz B != pinned $PIN_BASE_SHA/$PIN_BASE_SIZE" >&2
		exit 1
	}
	echo "== base artifact pinned: ${PIN_BASE_SHA%??????????????????????}... (${PIN_BASE_SIZE} B)"
fi
# The sibling 0x00F7 patch + sources this lane's patch builds on.
got=$(sha256sum "$P_DESC" | cut -d' ' -f1)
[ "$got" = "$PIN_DESC_PATCH_SHA" ] || {
	echo "dfs-mode-send: sibling desc-table patch sha $got != pinned $PIN_DESC_PATCH_SHA" >&2
	exit 1
}
got=$(sha256sum "$DOCS/wifi-desc-table-send/src/hi5622_desc_table.c" | cut -d' ' -f1)
[ "$got" = "$PIN_DESC_SRC_C_SHA" ] || {
	echo "dfs-mode-send: sibling hi5622_desc_table.c drifted" >&2; exit 1; }
got=$(sha256sum "$DOCS/wifi-desc-table-send/src/hi5622_desc_table.h" | cut -d' ' -f1)
[ "$got" = "$PIN_DESC_SRC_H_SHA" ] || {
	echo "dfs-mode-send: sibling hi5622_desc_table.h drifted" >&2; exit 1; }
echo "== sibling 0x00F7 patch + sources pinned"

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 \
	hi5622_desc_table.c hi5622_desc_table.h 2>/dev/null ) || true
echo "== 1. pinned r1-band base staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply sibling patches/0001-desc-table-send.patch (the 0x00F7 step)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P_DESC" >/dev/null ) || {
	echo "dfs-mode-send: the sibling desc-table patch does not apply to the pinned base" >&2
	exit 1
}
for f in hi5622_desc_table.c hi5622_desc_table.h; do
	cmp -s "$DOCS/wifi-desc-table-send/src/$f" "$WORK/$f" || {
		echo "dfs-mode-send: sibling src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. sibling 0x00F7 step applied (2 portable sources match)"

echo "== 4. apply this lane's patches/0001-dfs-mode-send.patch (the 0x01F7 step)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "dfs-mode-send: patch does not apply to the desc-table patched base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "dfs-mode-send: conflict markers survived - refusing build" >&2
	exit 1
fi
# The portable model is host-tested from src/; it must be byte-identical to
# the patched driver source (refuse source drift).
for f in hi5622_dfs_mode.c hi5622_dfs_mode.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "dfs-mode-send: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 5. src/ matches the patched tree (2 files)"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "dfs-mode-send: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "dfs-mode-send: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 6. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "dfs-mode-send: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "dfs-mode-send: strip changed .text" >&2; exit 1; }
echo "== 7. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- artifact contract checks ----------------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^dfs_mode:' || {
	echo "dfs-mode-send: missing module param dfs_mode" >&2; exit 1; }
for p in desc_table ap_chan_wait_ms beacon_static vap5g; do
	printf '%s\n' "$MI" | grep -q "^${p}:" || {
		echo "dfs-mode-send: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "61" ] || { echo "dfs-mode-send: param count $NP != 61 (60 base + dfs_mode)" >&2; exit 1; }
for s in "cfg80211: start_ap beacon_static frame %u/%u 'dfs_mode' posted (%zu B, g0 id1 key 0x01F7, body mode=3)" \
	 "cfg80211: dfs_mode post: bandobject=0x%08x published=0x%08x attach=%u (%s)" \
	 "dfs_mode=%u is not a valid DFS-mode parameter" \
	 "dfs_mode=1 needs the beacon_static=1 vendor order" \
	 "DFS-mode builder refused (%d)" \
	 "DFS-mode frame (g0 id1 key 0x01F7) not posted (%d)" \
	 "dfs_mode: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u" \
	 "hi5622_dfs_mode" ; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "dfs-mode-send: feature string missing: $s" >&2
		exit 1
	}
done
# The 6.18 module loader drops MODULE_PARM_DESC description strings from
# .modinfo (only parmtype= remains), so the parameter description is checked
# in the patched source instead of the artifact.
grep -qF 'MODULE_PARM_DESC(dfs_mode,' "$WORK/hi5622_scan.c" || {
	echo "dfs-mode-send: MODULE_PARM_DESC(dfs_mode) missing from scan.c" >&2
	exit 1
}
grep -qF 'module_param_named(dfs_mode, hi5622_dfs_mode, uint, 0444)' "$WORK/hi5622_scan.c" || {
	echo "dfs-mode-send: dfs_mode is not a read-only uint parameter" >&2
	exit 1
}
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "dfs-mode-send: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 8. artifact contract PASS (61 params incl dfs_mode, feature strings, 12 symvers)"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
TEXT_SHA=$(sha256sum "$WORK/.t1" | cut -d' ' -f1)
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
{
	echo "artifact: $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY - never committed)"
	echo "size: $SIZE B (stripped); unstripped $USIZE B"
	echo "sha256: $SHA"
	echo "unstripped sha256: $USHA"
	echo "text sha256: $TEXT_SHA"
	echo "$VERMAGIC"
	echo "params: $NP (desc-table-send 60 + dfs_mode)"
	echo "chain: pinned r1-band base (356,612 B sha ${PIN_BASE_SHA}) + sibling 0x00F7 patch + patches/0001-dfs-mode-send.patch"
	echo "dfs_mode: default 0 (off, current behaviour); 1 = post the 29-B 0x01F7 mode-3 frame after the 0x00F7 table and before 0x0124; >1 refused"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built module with the H2D 0x01F7 DFS/RF mode send (NOT fired)"
