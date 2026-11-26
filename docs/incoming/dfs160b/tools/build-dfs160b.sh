#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-dfs160b.sh - lane dfs160b (task-245) reproducible host build.
#
# HOST-ONLY: no device access, no firmware write, no flash.  Builds the
# clean-room 6.18.60 module with the committed DFS/160 MHz/CAC integration
# patch (patches/0001-dfs160b.patch) on the pinned wifi-5g-dfs-relax base
# tree (the fired A-rung lineage: 0x0065 VAP start -> 0x07E7 ARM -> 0x01F7
# dfs_mode -> 0x0242 band start), strips it to the OpenWrt installed form,
# proves the artefact identity and runs the host selftest.  The .ko is LOCAL
# EVIDENCE ONLY (/tmp, never committed - this repo stores no binaries).
#
# Base tree: ${TREE_SRC:-/tmp/dfs-relax-build}, reproduced by the sibling
#   lane ../wifi-5g-dfs-relax/tools/build-dfs-relax.sh
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/dfs-relax-build
[ -n "${WORK}" ] || WORK=/tmp/dfs160b-build-work
[ -n "${ART}" ] || ART=/tmp/dfs160b-build-artifact
set -u

die() { echo "build-dfs160b: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
expect_sha() { # file expected label
	got=$(sha "$1")
	[ "$got" = "$2" ] || die "$3 drifted: $got != $2"
}

[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-dfs160b: base tree $TREE_SRC absent" >&2
	echo "  reproduce it with ../wifi-5g-dfs-relax/tools/build-dfs-relax.sh" >&2
	exit 2; }
[ -f "$LANE/patches/0001-dfs160b.patch" ] || die "the integration patch is absent"

# ---- source pins: the base lineage this patch was authored/tested on ----
expect_sha "$TREE_SRC/Kbuild" 8f97661898373f8b65befcc3a62b6b00407101430e2924d74f142904d518c9e9 "base Kbuild"
expect_sha "$TREE_SRC/hi5622_scan.c" 75b12c4dc4b60dc458153497b1fc4cac6927d5ede85b98f5fdb2b8efdbb71273 "base hi5622_scan.c"
expect_sha "$TREE_SRC/hi5622_scan.h" bce2eb14175a28b9776ffeafea7815f1914596be5aef0f0928cc45e6ecae8ec9 "base hi5622_scan.h"
expect_sha "$TREE_SRC/hi5622_cfg80211.c" 81d7084a712b6585afab1b8fa2c8cd550304f2392c4476a7a9b8be1fd6128eb1 "base hi5622_cfg80211.c"
expect_sha "$TREE_SRC/hi5622_cfg80211.h" 73998e99cf098dbc3e36e12f7c7427b39a1000e46cf97c10a2c48ec250f8e49e "base hi5622_cfg80211.h"
expect_sha "$TREE_SRC/hi5622_cfg80211_ops.c" 5bb68c77e7cf04d5b59ae2d35d8b4f65949072a5d34102e596bd4d1067dae6ac "base hi5622_cfg80211_ops.c"
expect_sha "$TREE_SRC/hi5622_frames_api.h" 393ad0af2d225a8cb66d5edc8292746875e7796507a9ee4dc6eceb9240d80bc3 "base hi5622_frames_api.h"
expect_sha "$TREE_SRC/hi5622_ap_frames.h" 07780062a3b4cd2a6da3cbadb461ec26c29a88ed576e477a60519e10f53fe638 "base hi5622_ap_frames.h"
expect_sha "$TREE_SRC/hi5622_ap_frames.c" 1bb16d359c5f838b0b4c15510c87f9e0e321f6567d53d924b8abe439bf39885f "base hi5622_ap_frames.c"
expect_sha "$TREE_SRC/hi5622_vapscan_provider.c" b66da889342e35fb868c154c49c467dd7a0e1b85df6599b5574ca85513a7b764 "base hi5622_vapscan_provider.c"
expect_sha "$TREE_SRC/cfg80211-provider.symvers" bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7 "base cfg80211-provider.symvers"

# ---- source pins: the lane sources and the patch ------------------------
expect_sha "$LANE/src/hi5622_dfsb.h" b254d29df2c9fbd08e813e3eb10dcf06c1f080a696ee82111bb48f2f4d458972 "src/hi5622_dfsb.h"
expect_sha "$LANE/src/hi5622_dfsb.c" 784f2e6f964187884e0bce275732de875f8837a7c4341bd9336d3a15cff0ece9 "src/hi5622_dfsb.c"
expect_sha "$LANE/src/hi5622_dfsb_k.h" cf354fe1c9c3adafcd67c148339ae2f4a0c246c16c002e00a095a9a1e639c52b "src/hi5622_dfsb_k.h"
expect_sha "$LANE/src/hi5622_dfsb_k.c" 3f3dd12b277c243a51376902e02f94bad295296e7293978b89cedb27e458f4b8 "src/hi5622_dfsb_k.c"
expect_sha "$LANE/patches/0001-dfs160b.patch" 61177c2a2e502d0858f0069e71923f49f520f2b07713b21468002d5091686c6d "patches/0001-dfs160b.patch"

# ---- kernel tree / cross toolchain (a tree with Module.symvers) --------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-selectorfix-img \
	    /home/shibbityshwab/openwrt-lab-upstream-target; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ] && [ -f "$c/Module.symvers" ]; then
		KDIR=$c
		STAGING_DIR=$root/staging_dir
		break
	fi
done
[ -d "${KDIR:-}" ] || die "no 6.18.60 kernel tree with Module.symvers found"
[ -n "${STAGING_DIR:-}" ] || die "staging dir not found"
export STAGING_DIR
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || die "the OpenWrt cross gcc is missing"
export CROSS

# ---- 1. clean copy of the pinned base + the integration patch -----------
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
rsync -a --exclude '*.o' --exclude '*.ko' --exclude '*.mod' \
	--exclude '*.mod.c' --exclude '.*.cmd' --exclude '*.cmd' \
	--exclude modules.order --exclude Module.symvers \
	--exclude build.log "$TREE_SRC/" "$WORK/"
( cd "$WORK" && patch -p1 -s < "$LANE/patches/0001-dfs160b.patch" ) ||
	die "the integration patch does not apply to the pinned base"
# the patch carries the lane sources verbatim: prove it
for f in hi5622_dfsb.h hi5622_dfsb.c hi5622_dfsb_k.h hi5622_dfsb_k.c; do
	expect_sha "$WORK/$f" "$(sha "$LANE/src/$f")" "patched $f != src/$f"
done

# ---- 2. build ----------------------------------------------------------
make -j4 ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules \
	> "$ART/build.log" 2>&1 || die "the module build failed (see $ART/build.log)"
warn=$(grep -c 'warning:' "$ART/build.log" || true)
err=$(grep -c 'error:' "$ART/build.log" || true)
[ "$err" = 0 ] || die "the module build reported errors"
[ "$warn" = 0 ] || die "the module build reported $warn warning(s)"

# ---- 3. installed form (OpenWrt strip-kmod.sh, NO_RENAME=1) -----------
STRIP=$(dirname "$STAGING_DIR")/scripts/strip-kmod.sh
[ -f "$STRIP" ] || die "OpenWrt strip-kmod.sh not found at $STRIP"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.unstripped.ko"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"
NO_RENAME=1 sh "$STRIP" "$ART/hi5622_wifi.ko" >/dev/null || die "strip-kmod.sh failed"

# ---- 4. artefact identity (the pins the fire recipe checks) -----------
expect_sha "$ART/hi5622_wifi.ko" 006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183 "installed module"
size=$(stat -c %s "$ART/hi5622_wifi.ko")
[ "$size" = 383020 ] || die "installed size drifted: $size != 383020"
parms=$(modinfo "$ART/hi5622_wifi.ko" | grep -c '^parm:')
[ "$parms" = 68 ] || die "parameter count drifted: $parms != 68"
for p in ht_bw dfs_cac; do
	modinfo -p "$ART/hi5622_wifi.ko" | grep -q "^$p: " ||
		die "$p parameter missing"
done
"$CROSS"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
expect_sha "$ART/.text.bin" 5e14008fab4707afdf55ab3ea864129abfc65979c3e4b0b2e572f6593ead5faa "installed .text"
tsize=$(stat -c %s "$ART/.text.bin")
[ "$tsize" = 164980 ] || die "installed .text size drifted: $tsize != 164980"
echo "build-dfs160b: module $ART/hi5622_wifi.ko ($size B, 68 params, .text $tsize B)"

# ---- 5. host selftest --------------------------------------------------
"$LANE/tools/run-host-tests.sh" > "$ART/host-tests.txt" 2>&1 ||
	die "the host selftest FAILED (see $ART/host-tests.txt)"
grep -q 'RESULT: 269 checks, 0 failure(s)' "$ART/host-tests.txt" ||
	die "the host selftest count/result drifted"
echo "build-dfs160b: host selftest 269/269 PASS ($ART/host-tests.txt)"
echo "build-dfs160b: PASS - pins: installed 383020 B sha256 006a94cf...; .text 164980 B sha256 5e14008f...; vermagic 6.18.60; 68 params"
