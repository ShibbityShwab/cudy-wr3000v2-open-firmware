#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-app-ie-image3.sh - lane app-ie-rebase (task-235) reproducible
# host build of the wifi-app-ie integration REBASED onto the IMAGE #3 merged
# module tree (the a717c86d source, /tmp/wifi-image-3-module).
#
# HOST-ONLY: no device access, no firmware write, no flash.  Builds the
# clean-room 6.18.60 module with the committed rebase patch on the pinned
# IMAGE #3 base tree, strips it to the OpenWrt installed form, proves the
# artefact identity and runs the host KAT.  The .ko is LOCAL EVIDENCE ONLY
# (/tmp, never committed - this repo stores no binary artifacts).
#
# Base tree: ${TREE_SRC:-/tmp/wifi-image-3-module} (the IMAGE #3 merged module
# source tree of docs/incoming/wifi-image-3/build-evidence/module-build/).
# The evidence/base-tree.sha256 manifest pins all 97 source files; the base
# control build of that tree reproduces the shipped IMAGE #3 module exactly
# (installed 358,544 B sha256 a717c86d..., .text 494ccd42... - see
# evidence/BASE-CONTROL-BUILD.txt).
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/wifi-image-3-module
[ -n "${WORK}" ] || WORK=/tmp/app-ie-image3/build
[ -n "${ART}" ] || ART=/tmp/app-ie-image3
set -u

die() { echo "build-app-ie-image3: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
expect_sha() { # file expected label
	got=$(sha "$1")
	[ "$got" = "$2" ] || die "$3 drifted: $got != $2"
}

[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-app-ie-image3: base tree $TREE_SRC absent" >&2
	echo "  the IMAGE #3 module tree is /tmp/wifi-image-3-module (scratch, not committed)" >&2
	exit 2; }
[ -f "$LANE/patches/0001-app-ie-image3.patch" ] || die "the rebase patch is absent"

# ---- source pins: the IMAGE #3 merged module tree (a717c86d source) -------
expect_sha "$TREE_SRC/Kbuild" b4f98773547d0d7643be7c401d7d25cd40eae896b6ac49383647177f1731087e "base Kbuild"
expect_sha "$TREE_SRC/hi5622_scan.c" 8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7 "base hi5622_scan.c"
expect_sha "$TREE_SRC/hi5622_scan_logic.c" 75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c "base hi5622_scan_logic.c"
expect_sha "$TREE_SRC/hi5622_scan_logic.h" b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e "base hi5622_scan_logic.h"
expect_sha "$TREE_SRC/cfg80211-provider.symvers" bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7 "base cfg80211-provider.symvers"
( cd "$TREE_SRC" && sha256sum -c "$LANE/evidence/base-tree.sha256" >/dev/null 2>&1 ) ||
	die "the base tree manifest does not verify (wrong/changed IMAGE #3 tree)"

# ---- source pins: the portable module (byte-identical to task-234) --------
expect_sha "$LANE/src/hi5622_app_ie.h" e0f3b39f9b15d410ed70a62a7e7977433d3454158457a7b9c7c4e0902929432c "src/hi5622_app_ie.h"
expect_sha "$LANE/src/hi5622_app_ie.c" 19c9cbf7d822a05eac919ac4bf660058b38b86d67b4a912feac0470be5a306ba "src/hi5622_app_ie.c"
expect_sha "$LANE/patches/0001-app-ie-image3.patch" 09523f988b7a0256565278017a4e82687d7dd6363abbccc5722de595550adb7d "patches/0001-app-ie-image3.patch"

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

# ---- 1. clean copy of the pinned IMAGE #3 base + the rebase patch --------
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
rsync -a --exclude '*.o' --exclude '*.ko' --exclude '*.mod' \
	--exclude '*.mod.c' --exclude '.*.cmd' --exclude modules.order \
	--exclude Module.symvers --exclude build.log "$TREE_SRC/" "$WORK/"
( cd "$WORK" && patch -p1 -s < "$LANE/patches/0001-app-ie-image3.patch" ) ||
	die "the rebase patch does not apply to the pinned IMAGE #3 base"

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
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.unstripped.ko"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"
NO_RENAME=1 sh "$STRIP" "$ART/hi5622_wifi.ko" || die "strip-kmod.sh failed"

# ---- 4. artefact identity (the pins the fire recipe checks) -----------
expect_sha "$ART/hi5622_wifi.ko" 2a2fff3508bdbffad4a23d04c31c17ef91f7ce46ae190fdae090119bcb6263e2 "installed module"
size=$(stat -c %s "$ART/hi5622_wifi.ko")
[ "$size" = 364516 ] || die "installed size drifted: $size != 364516"
parms=$(modinfo "$ART/hi5622_wifi.ko" | grep -c '^parm:')
[ "$parms" = 66 ] || die "parameter count drifted: $parms != 66"
modinfo -p "$ART/hi5622_wifi.ko" | grep -q '^app_ie: ' || die "app_ie parameter missing"
modinfo -p "$ART/hi5622_wifi.ko" | grep -q '^app_ie_type: ' || die "app_ie_type parameter missing"
${CROSS}objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
expect_sha "$ART/.text.bin" c1ad137974b6866bbc7cdc1971288ade203bf9dfcca6421b73343ef3d50b9417 "installed .text"

# ---- 5. host KAT (the task-234 suite, unchanged portable module) ------
"$LANE/tools/run-host-tests.sh" | tee "$ART/host-tests.txt"

# ---- 6. summary --------------------------------------------------------
echo "build-app-ie-image3: PASS"
echo "  base      : IMAGE #3 merged module tree (a717c86d source)"
echo "  installed : $(sha "$ART/hi5622_wifi.ko")  $size B  $parms params"
echo "  .text     : $(sha "$ART/.text.bin")  $(stat -c %s "$ART/.text.bin") B"
echo "  unstripped: $(sha "$ART/hi5622_wifi.unstripped.ko")  $(stat -c %s "$ART/hi5622_wifi.unstripped.ko") B"
echo "  work tree : $WORK  (installed sha is pinned for WORK=$WORK: module"
echo "  strings embed the build path; another WORK gives another file hash but"
echo "  the same .text hash.)"
