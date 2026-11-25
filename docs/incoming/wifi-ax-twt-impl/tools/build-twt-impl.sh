#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-twt-impl.sh - lane wifi-ax-twt-impl (shared task task-199)
#
# HOST-ONLY build.  Reconstructs the bench-proven merged 64-param module base
# (the IMAGE #3 module source: the F2 5 GHz chain + the RX watermark + the BA
# skeleton + the HE caps), applies this lane's committed patch
# (patches/0001-twt-enable.patch: the portable TWT model + the fail-closed
# twt parameter + the g0-id1 key 0x035D post with field B from the live
# AP-start module context) and compiles it against the project 6.18.60 kernel
# tree.  No device access, no firmware write, no image, no flash.  The .ko is
# LOCAL EVIDENCE ONLY and is never committed.
#
# The base chain is rebuilt from the committed sibling-lane patches, never
# from a vendor artifact:
#   1. F2 5 GHz chain tip (the bench-fired F2 module source)
#        = r1-band base + wifi-desc-table-send/0001 + wifi-dfs-mode-send/0001
#          + wifi-fw-dfs-worker/0001-fieldb-context (field B = 0x00 fix)
#   2. merged IMAGE #3 base (/tmp/ax-twt-merged-base when built here)
#        = F2 + wifi-rx-watermark/0001 + wifi-ba-agg/0001 + wifi-ax-he-caps/0001
#   3. merged + this lane's patch (= what this script builds and reports)
#
# Usage:
#   ./tools/build-twt-impl.sh
#   F2_TREE=/tmp/dfsmode-build ./tools/build-twt-impl.sh   # local fixture
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
DOCS=$(cd "$LANE/.." && pwd)
PATCH=$LANE/patches/0001-twt-enable.patch
F2=${F2_TREE:-/tmp/dfsmode-build}
PRISTINE=${PRISTINE_TREE:-/tmp/ax-twt-merged-base}
WORK=${WORK_TREE:-/tmp/ax-twt-build}
ART=${ART_DIR:-/tmp/ax-twt-artifact}

# ---- artifact/source pins (see BUILD-NOTES.md) --------------------------
PIN_F2_SCAN=8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7
PIN_F2_PROVIDER=c306e75fb4d640812278dc1ccdd59c8c197ba6e3f42da2b7f5ff98010d8b79ab
PIN_F2_DFSMODE_H=cd3b76aa75c5153654ca1e981d0f767fc100e7cb8e83406c55a43d0b4ce3c70b
PIN_F2_DFSMODE_C=977f285c58ef1ffdfca7812c4014da7602dc6a3f3b6fbb7839e1caedfdd13892
PIN_BASE_SCAN_LOGIC_C=75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c
PIN_BASE_SCAN_LOGIC_H=b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e
PIN_BASE_KBUILD=b4f98773547d0d7643be7c401d7d25cd40eae896b6ac49383647177f1731087e
PIN_BASE_CFG80211_C=1af4d7f3facbf19ffad945ea0c4c7e9b11d5b1b04eace6421b9e52b45ec384e5
PIN_BASE_DP_TXRX_C=76da6d4b3bd5ad6e165656bb9c9ade5a9ed6ef1df97a2dcf810bd0a2638b6d6e
PIN_BASE_RX_LIVE_C=4bf35afbb496bd35d3db455ea648bd06e3fb158cd20d7a9e569d75d4728ff36a
PIN_BASE_RX_LIVE_H=afa37eccfbbb322087da680f877c2003eb8bf159fb8a802c208791452b9d0b4c
PIN_BASE_BA_C=ae51aa12dae45252bac774c8d7a86cf039ccfd59bdf3ffc3ed9f62388936c777
PIN_BASE_HE_C=de2d78b5da48d9774099a7e03adc522a431a1ee8a029a70005393ae353273b9b
# module identities: the merged base == IMAGE #3 .text (path-independent), and
# the TWT module's .text (path-independent); the full .ko hash is
# path-dependent (DWARF comp_dir) and is reported, not pinned.
PIN_BASE_TEXT=494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1
PIN_TWT_TEXT=1d7d6ddae70a9044c917381c52a124fed6adf6546489fece2b0ab6de9f5eb8e4
PIN_TWT_PARAMS=65

die() { echo "build-twt-impl: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
expect_sha() { # file expected label
	got=$(sha "$1")
	[ "$got" = "$2" ] || die "$3 drifted: $got != $2"
}
text_sha() { # artifact
	${CROSS}objcopy -O binary --only-section=.text "$1" "$ART/.text.bin"
	sha "$ART/.text.bin"
}

# ---- toolchain / kernel tree -------------------------------------------
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
[ -d "$KDIR" ] || die "6.18.60 kernel tree not found"
[ -n "$STAGING_DIR" ] || die "staging dir not found"
export STAGING_DIR
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || die "cross gcc missing"
[ -f "$PATCH" ] || die "missing patch $PATCH"
mkdir -p "$ART"

sync_src() { # src dst - sources only, no build products
	rsync -a --exclude '*.o' --exclude '*.ko' --exclude '*.mod' \
		--exclude '*.mod.c' --exclude '.*.cmd' --exclude 'modules.order' \
		--exclude 'Module.symvers' --exclude 'build.log' "$1/" "$2/"
}

# ---- 1. F2 5 GHz chain tip --------------------------------------------
build_f2() {
	echo "== 1. F2 base tree absent - rebuilding the committed r1-band chain"
	"$DOCS/wifi-5ghz-beacon/tools/build-beacon5g-ready.sh"
	"$DOCS/wifi-r1-band-diff/tools/build-r1-band.sh"
	rm -rf "$F2"
	mkdir -p "$F2"
	sync_src /tmp/r1-band-build "$F2"
	cp /tmp/r1-band-build/cfg80211-provider.symvers "$F2/" 2>/dev/null || true
	( cd "$F2" &&
	  patch -p1 -s < "$DOCS/wifi-desc-table-send/patches/0001-desc-table-send.patch" &&
	  patch -p1 -s < "$DOCS/wifi-dfs-mode-send/patches/0001-dfs-mode-send.patch" &&
	  patch -p1 -s < "$DOCS/wifi-fw-dfs-worker/patches/0001-fieldb-context.patch" ) ||
		die "the committed F2 chain patches do not apply"
}
[ -f "$F2/hi5622_scan.c" ] || build_f2
expect_sha "$F2/hi5622_scan.c" "$PIN_F2_SCAN" "F2 hi5622_scan.c"
expect_sha "$F2/hi5622_vapscan_provider.c" "$PIN_F2_PROVIDER" "F2 provider"
expect_sha "$F2/hi5622_dfs_mode.h" "$PIN_F2_DFSMODE_H" "F2 dfs_mode.h (field-B fix)"
expect_sha "$F2/hi5622_dfs_mode.c" "$PIN_F2_DFSMODE_C" "F2 dfs_mode.c"
[ -f "$F2/cfg80211-provider.symvers" ] || die "F2 cfg80211-provider.symvers missing"

# ---- 2. merged IMAGE #3 base ------------------------------------------
build_pristine() {
	echo "== 2. rebuilding the merged 64-param base from the committed lanes"
	rm -rf "$PRISTINE"
	mkdir -p "$PRISTINE"
	sync_src "$F2" "$PRISTINE"
	cp "$F2/cfg80211-provider.symvers" "$PRISTINE/"
	( cd "$PRISTINE" &&
	  patch -p1 -s < "$DOCS/wifi-rx-watermark/patches/0001-rx-refill-watermark.patch" &&
	  patch -p1 -s < "$DOCS/wifi-ba-agg/patches/0001-ba-skeleton.patch" ) ||
		die "the RX-watermark or BA patch does not apply"
	# The HE caps patch was written against the pre-F2 Kbuild; its Kbuild
	# hunk alone fails there (the F2 tree already carries the desc_table/
	# dfs_mode object lines).  Apply it, then add the object line exactly as
	# the IMAGE #3 lane did (after hi5622_dfs_mode.o).
	( cd "$PRISTINE" &&
	  patch -p1 -s < "$DOCS/wifi-ax-he-caps/patches/0001-he-caps.patch" ) ||
		echo "== note: HE patch Kbuild hunk failed as expected - adding the object line"
	rm -f "$PRISTINE/Kbuild.rej"
	cp "$DOCS/wifi-ax-he-caps/src/hi5622_he_caps.c" \
	   "$DOCS/wifi-ax-he-caps/src/hi5622_he_caps.h" "$PRISTINE/"
	( cd "$PRISTINE" &&
	  patch -p1 -s < "$LANE/patches/0000-merge-he-kbuild-fix.patch" ) ||
		die "the HE Kbuild merge-fix patch does not apply"
}
[ -f "$PRISTINE/Kbuild" ] || build_pristine
expect_sha "$PRISTINE/hi5622_scan.c" "$PIN_F2_SCAN" "merged hi5622_scan.c"
expect_sha "$PRISTINE/hi5622_scan_logic.c" "$PIN_BASE_SCAN_LOGIC_C" "merged scan_logic.c"
expect_sha "$PRISTINE/hi5622_scan_logic.h" "$PIN_BASE_SCAN_LOGIC_H" "merged scan_logic.h"
expect_sha "$PRISTINE/Kbuild" "$PIN_BASE_KBUILD" "merged Kbuild"
expect_sha "$PRISTINE/hi5622_cfg80211.c" "$PIN_BASE_CFG80211_C" "merged cfg80211.c (HE)"
expect_sha "$PRISTINE/hi5622_dp_txrx.c" "$PIN_BASE_DP_TXRX_C" "merged dp_txrx.c (RX wm)"
expect_sha "$PRISTINE/hi5622_rx_live.c" "$PIN_BASE_RX_LIVE_C" "merged rx_live.c"
expect_sha "$PRISTINE/hi5622_rx_live.h" "$PIN_BASE_RX_LIVE_H" "merged rx_live.h"
expect_sha "$PRISTINE/hi5622_ba.c" "$PIN_BASE_BA_C" "merged ba.c"
expect_sha "$PRISTINE/hi5622_he_caps.c" "$PIN_BASE_HE_C" "merged he_caps.c"

build_tree() { # tree label
	echo "== build: $2 ($1)"
	( cd "$1" && make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$PWD" \
		modules ) 2>&1 | tee "$ART/$2-build.log" | tail -3
	grep -q 'warning:' "$ART/$2-build.log" && {
		grep -c 'warning:' "$ART/$2-build.log" | xargs echo "warnings:"; }
	[ -f "$1/hi5622_wifi.ko" ] || die "$2 build produced no hi5622_wifi.ko"
}

# ---- 3. base artifact: prove the reconstruction is the IMAGE #3 module --
build_tree "$PRISTINE" base
BASE=$ART/hi5622_wifi.base.ko
cp "$PRISTINE/hi5622_wifi.ko" "$BASE"
B_TXT=$(text_sha "$BASE")
[ "$B_TXT" = "$PIN_BASE_TEXT" ] || \
	die "merged base .text drifted: $B_TXT != $PIN_BASE_TEXT"
echo "merged base: .text $B_TXT (== IMAGE #3), unstripped $(stat -c%s "$BASE") B"

# ---- 4. this lane: merged base + the TWT patch ------------------------
echo "== 4. applying $PATCH"
rm -rf "$WORK"
mkdir -p "$WORK"
sync_src "$PRISTINE" "$WORK"
cp "$PRISTINE/cfg80211-provider.symvers" "$WORK/"
( cd "$WORK" && patch -p1 -s < "$PATCH" ) || die "the TWT patch does not apply"
build_tree "$WORK" twt

OK=$ART/hi5622_wifi.twt.ko
cp "$WORK/hi5622_wifi.ko" "$OK"
ACT=$ART/hi5622_wifi.twt.activity.ko
${CROSS}strip --strip-debug -o "$ACT" "$OK"
T_TXT=$(text_sha "$OK")
[ "$T_TXT" = "$PIN_TWT_TEXT" ] || die "TWT module .text drifted: $T_TXT != $PIN_TWT_TEXT"
PARAMS=$(modinfo -p "$OK" | wc -l)
[ "$PARAMS" -eq "$PIN_TWT_PARAMS" ] || die "param count $PARAMS != $PIN_TWT_PARAMS"
modinfo -p "$OK" | grep -q '^twt:' || die "twt parameter missing from modinfo"

echo ""
echo "== ARTIFACT (local evidence only; never committed)"
echo "   tree            $WORK"
echo "   unstripped      $(stat -c%s "$OK") B  sha256 $(sha "$OK")"
echo "   activity strip  $(stat -c%s "$ACT") B  sha256 $(sha "$ACT")"
echo "   .text           $(stat -c%s "$ART/.text.bin") B  sha256 $T_TXT"
echo "   params          $PARAMS (merged base 64 + twt)"
echo "   vermagic        $(modinfo "$OK" | sed -n 's/^vermagic: *//p')"
echo "   TWT symbols     $(${CROSS}nm "$ACT" | grep -c ' T hi5622_ax_twt_') (build/field_b/mode_name/precheck/status_line)"
echo "   TWT .text pin   $PIN_TWT_TEXT"
