#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-app-ie.sh - lane wifi-app-ie (task-234) reproducible host build.
#
# HOST-ONLY: no device access, no firmware write, no flash.  Builds the
# clean-room 6.18.60 module with the committed integration patch on the
# pinned R5-order base, strips it to the OpenWrt installed form, proves the
# artefact identity and runs the host tests.  The .ko is LOCAL EVIDENCE ONLY
# (/tmp, never committed - this repo stores no binary artifacts).
#
# Base tree: ${TREE_SRC:-/tmp/r5-build}, reproduced by the sibling lane
#   ../wifi-band1-start-send-5/tools/build-r5-order.sh
# (the same R5 order tree the BA/TWT lanes build on; the app-IE patch is
# orthogonal and applies to the merged IMAGE #3 base the same way).
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/r5-build
[ -n "${WORK}" ] || WORK=/tmp/app-ie-build
[ -n "${ART}" ] || ART=/tmp/app-ie-artifact
set -u

die() { echo "build-app-ie: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
expect_sha() { # file expected label
	got=$(sha "$1")
	[ "$got" = "$2" ] || die "$3 drifted: $got != $2"
}

[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-app-ie: base tree $TREE_SRC absent" >&2
	echo "  reproduce it with ../wifi-band1-start-send-5/tools/build-r5-order.sh" >&2
	exit 2; }
[ -f "$TREE_SRC/cfg80211-provider.symvers" ] || die "cfg80211-provider.symvers absent in the base tree"
[ -f "$LANE/patches/0001-app-ie.patch" ] || die "the integration patch is absent"

# ---- source pins (the lineage this patch was authored and tested on) ----
expect_sha "$TREE_SRC/Kbuild" 1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d "base Kbuild"
expect_sha "$TREE_SRC/hi5622_scan.c" 86d6c57ecc98d80906af1eeee1e5d6641c21a7ed5c1b2b53c70b85236b5b9373 "base hi5622_scan.c"
expect_sha "$TREE_SRC/hi5622_scan_logic.c" 15c50d34fa7ed0f9bd5262e5b317c13a97228207dcc599673b72788e2b9bcfea "base hi5622_scan_logic.c"
expect_sha "$TREE_SRC/hi5622_scan_logic.h" 6af774ef76b730c8f45d621bf338d12b041b96a3792be985df59ac5092a603e8 "base hi5622_scan_logic.h"
expect_sha "$TREE_SRC/cfg80211-provider.symvers" bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7 "base cfg80211-provider.symvers"
expect_sha "$LANE/src/hi5622_app_ie.h" e0f3b39f9b15d410ed70a62a7e7977433d3454158457a7b9c7c4e0902929432c "src/hi5622_app_ie.h"
expect_sha "$LANE/src/hi5622_app_ie.c" 19c9cbf7d822a05eac919ac4bf660058b38b86d67b4a912feac0470be5a306ba "src/hi5622_app_ie.c"
expect_sha "$LANE/patches/0001-app-ie.patch" 2c7f6621d49000934df74e608efa52dba93118366103fb96301b57e3c6126043 "patches/0001-app-ie.patch"

# ---- kernel tree / cross toolchain -------------------------------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target \
	    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "${KDIR:-}" ] || die "the 6.18.60 kernel tree was not found"
[ -n "${STAGING_DIR:-}" ] || die "staging dir not found"
export STAGING_DIR
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || die "the OpenWrt cross gcc is missing"
export CROSS

# ---- 1. clean copy of the pinned base + the committed patch ------------
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
rsync -a --exclude '*.o' --exclude '*.ko' --exclude '*.mod' \
	--exclude '*.mod.c' --exclude '.*.cmd' --exclude modules.order \
	--exclude Module.symvers --exclude build.log "$TREE_SRC/" "$WORK/"
( cd "$WORK" && patch -p1 -s < "$LANE/patches/0001-app-ie.patch" ) ||
	die "the integration patch does not apply to the pinned base"

# ---- 2. build ----------------------------------------------------------
make -j4 ARCH=arm CROSS_COMPILE="${CROSS}" -C "$KDIR" M="$WORK" modules \
	> "$ART/build.log" 2>&1 || die "the module build failed (see $ART/build.log)"
warn=$(grep -c 'warning:' "$ART/build.log" || true)
err=$(grep -c 'error:' "$ART/build.log" || true)
[ "$err" = 0 ] || die "the module build reported errors"
[ "$warn" = 0 ] || die "the module build reported $warn warning(s)"

# ---- 3. installed form (OpenWrt strip-kmod.sh, NO_RENAME=1) -----------
STRIP=$(dirname "$STAGING_DIR")/scripts/strip-kmod.sh
[ -f "$STRIP" ] || die "OpenWrt strip-kmod.sh not found at $STRIP"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"
NO_RENAME=1 sh "$STRIP" "$ART/hi5622_wifi.ko" || die "strip-kmod.sh failed"

# ---- 4. artefact identity (the pins the fire recipe checks) -----------
expect_sha "$ART/hi5622_wifi.ko" 0c8390e8b498b668a066f4e7c74ad9fdafa7aa5586db22fb81313409cfcb0bdb "installed module"
size=$(stat -c %s "$ART/hi5622_wifi.ko")
[ "$size" = 368348 ] || die "installed size drifted: $size != 368348"
parms=$(modinfo "$ART/hi5622_wifi.ko" | grep -c '^parm:')
[ "$parms" = 67 ] || die "parameter count drifted: $parms != 67"
modinfo -p "$ART/hi5622_wifi.ko" | grep -q '^app_ie: ' || die "app_ie parameter missing"
modinfo -p "$ART/hi5622_wifi.ko" | grep -q '^app_ie_type: ' || die "app_ie_type parameter missing"
${CROSS}objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
expect_sha "$ART/.text.bin" 7867d4f0294bae925c9118a4e4360fd1b4815f6d57c9abea8318a9e1caa9a207 "installed .text"

# ---- 5. host tests -----------------------------------------------------
"$LANE/tools/run-host-tests.sh" | tee "$ART/host-tests.txt"

# ---- 6. summary --------------------------------------------------------
echo "build-app-ie: PASS"
echo "  installed : $(sha "$ART/hi5622_wifi.ko")  $size B  $parms params"
echo "  .text     : $(sha "$ART/.text.bin")  $(stat -c %s "$ART/.text.bin") B"
echo "  unstripped: $(sha "$WORK/hi5622_wifi.ko")  $(stat -c %s "$WORK/hi5622_wifi.ko") B"
echo "  NOTE: the installed sha is pinned for WORK=$WORK (module strings embed
  the build path); a package build from another path gives another file
  hash but the same .text hash."
