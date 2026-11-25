#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-desc-table-send.sh - lane wifi-desc-table-send (task-188)
# HOST-ONLY build: compiles the 5 GHz AP module with the H2D 0x00F7
# descriptor-table send and the fail-closed desc_table parameter, against the
# project 6.18.60 kernel tree.  No device access, no firmware write, no image,
# no flash.  The .ko is LOCAL EVIDENCE ONLY and is never committed.
#
# Chain (the pinned base the Lead fires):
#   1. ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh
#      -> /tmp/beacon5g-build + /tmp/beacon5g-artifact/hi5622_wifi.ko
#         356,600 B sha256 bc1cc0ea...a1bbb, 59 params (beacon5g gate);
#   2. ../wifi-r1-band-diff/tools/build-r1-band.sh
#      -> /tmp/r1-band-build + /tmp/r1-band-artifact/hi5622_wifi.ko
#         356,612 B sha256 e8b17f72...4abd4, 59 params (the CW-0/R1 module;
#         the r1-band commit is the one-field VAP-start band fix);
#   3. this lane's patches/0001-desc-table-send.patch
#      -> hi5622_scan.c (param + fail-closed gate + 0x00F7 post + sysfs),
#         hi5622_scan_logic.{c,h} (ordering plan step), Kbuild, and the new
#         portable hi5622_desc_table.{c,h}.
#
# Usage:
#   ./tools/build-desc-table-send.sh
#   BASE_TREE=/tmp/r1-band-build ./tools/build-desc-table-send.sh
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-desc-table-send.patch
WORK=/tmp/desctable-build
ART=/tmp/desctable-artifact
set +u
if [ -z "${BASE_TREE}" ]; then BASE_TREE=/tmp/r1-band-build; fi
set -u
# /tmp/r1-band-build is the r1-band PATCHED tree: its hi5622_vapscan_provider.c
# is the beacon base (bc7e3ede... if /tmp/beacon5g-build is present) plus the
# one-field VAP-start band fix -> this patched sha.  The definitive pin is the
# stripped r1-band artifact the CW-0/R1 fires ran (e8b17f72...).
PIN_PROVIDER_PATCHED=c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab
PIN_PROVIDER_BASE=bc7e3ede24eaa21bc9ecf01dfb82687898a4e5fc378e77926f77619e200ce828
PIN_BASE_ART=/tmp/r1-band-artifact/hi5622_wifi.ko
PIN_BASE_SHA=e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4
PIN_BASE_SIZE=356612
PIN_BASE_PARAMS=59

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
[ -d "$KDIR" ] || { echo "desc-table-send: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "desc-table-send: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "desc-table-send: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "desc-table-send: missing patch $P" >&2; exit 2; }

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
	echo "desc-table-send: base provider sha $got != pinned $PIN_PROVIDER_PATCHED" >&2
	echo "desc-table-send: the r1-band base drifted - re-review the base lanes first" >&2
	exit 1
}
if [ -f /tmp/beacon5g-build/hi5622_vapscan_provider.c ]; then
	gb=$(sha256sum /tmp/beacon5g-build/hi5622_vapscan_provider.c | cut -d' ' -f1)
	[ "$gb" = "$PIN_PROVIDER_BASE" ] || {
		echo "desc-table-send: beacon5g base provider sha $gb != pinned $PIN_PROVIDER_BASE" >&2
		exit 1
	}
	echo "== beacon5g base pin OK, r1-band patched base pin OK"
fi
if [ -f "$PIN_BASE_ART" ]; then
	bs=$(sha256sum "$PIN_BASE_ART" | cut -d' ' -f1)
	bz=$(stat -c %s "$PIN_BASE_ART")
	[ "$bs" = "$PIN_BASE_SHA" ] && [ "$bz" = "$PIN_BASE_SIZE" ] || {
		echo "desc-table-send: base artifact $bs/$bz B != pinned $PIN_BASE_SHA/$PIN_BASE_SIZE" >&2
		exit 1
	}
	echo "== base artifact pinned: ${PIN_BASE_SHA%??????????????????????}... (${PIN_BASE_SIZE} B)"
fi

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 \
	hi5622_desc_table.c hi5622_desc_table.h 2>/dev/null ) || true
echo "== 1. pinned r1-band base staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply patches/0001-desc-table-send.patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "desc-table-send: patch does not apply to the pinned r1-band base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "desc-table-send: conflict markers survived - refusing build" >&2
	exit 1
fi
# The portable model is host-tested from src/; it must be byte-identical to
# the patched driver source (refuse source drift).
for f in hi5622_desc_table.c hi5622_desc_table.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "desc-table-send: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. src/ matches the patched tree (2 files)"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "desc-table-send: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "desc-table-send: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "desc-table-send: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "desc-table-send: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- artifact contract checks ----------------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^desc_table:' || {
	echo "desc-table-send: missing module param desc_table" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^ap_chan_wait_ms:' || {
	echo "desc-table-send: missing module param ap_chan_wait_ms" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^beacon_static:' || {
	echo "desc-table-send: missing module param beacon_static" >&2; exit 1; }
printf '%s\n' "$MI" | grep -q '^vap5g:' || {
	echo "desc-table-send: missing module param vap5g" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "60" ] || { echo "desc-table-send: param count $NP != 60 (59 base + desc_table)" >&2; exit 1; }
for s in "cfg80211: start_ap beacon_static frame %u/%u 'desc_table' posted (%zu B" \
	 "g0 id1 key 0x00F7, cc=%c%c, 2 records, band1 word=0x%08x armed, 5G bitmap=0x%08x" \
	 "cfg80211: desc_table post: module ctx=0x%08x bandobject=0x%08x (%s)" \
	 "desc_table=%u is not a valid descriptor-table parameter" \
	 "desc_table=1 needs the beacon_static=1 vendor order" \
	 "descriptor-table builder refused (%d)" \
	 "descriptor-table frame (g0 id1 key 0x00F7) not posted (%d)" \
	 "desc_table: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u" \
	 "desc_table"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "desc-table-send: feature string missing: $s" >&2
		exit 1
	}
done
# The 6.18 module loader drops MODULE_PARM_DESC description strings from
# .modinfo (only parmtype= remains), so the parameter description is checked
# in the patched source instead of the artifact.
grep -qF 'MODULE_PARM_DESC(desc_table,' "$WORK/hi5622_scan.c" || {
	echo "desc-table-send: MODULE_PARM_DESC(desc_table) missing from scan.c" >&2
	exit 1
}
grep -qF 'module_param_named(desc_table, hi5622_desc_table, uint, 0444)' "$WORK/hi5622_scan.c" || {
	echo "desc-table-send: desc_table is not a read-only uint parameter" >&2
	exit 1
}
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "desc-table-send: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 6. artifact contract PASS (60 params incl desc_table, feature strings, 12 symvers)"

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
	echo "params: $NP (r1-band 59 + desc_table)"
	echo "chain: pinned r1-band base (356,612 B sha ${PIN_BASE_SHA}) + patches/0001-desc-table-send.patch"
	echo "desc_table: default 0 (off, current behaviour); 1 = post the 68-B 0x00F7 fix frame on a band-1 beacon_static AP before 0x0124; >1 refused"
} | tee "$ART/build-artifact.txt"
echo "== DONE - host-built module with the H2D 0x00F7 descriptor-table send (NOT fired)"
