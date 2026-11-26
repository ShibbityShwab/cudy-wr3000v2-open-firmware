#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-dfs-set.sh - lane wifi-5g-dfs-set (task-227)
# HOST-ONLY build: takes the byte-exact R5 base tree (/tmp/r5-build, the tree
# of the installed-form module 24f7221b), applies this lane's
# patches/0001-dfs-set.patch (the H2D 0x07E7 DFS-SET knob + frame builder +
# posting step + gates), builds the 6.18.60 module, strips it and runs the
# host tests (the C contract selftest + the independent Python integration
# test).  No device access, no firmware write, no flash.  The .ko is LOCAL
# EVIDENCE ONLY (/tmp).
#
# Base provenance: wifi-r5-rebuild (task-225) CHAIN.md - the same tree that
# reproduced 383,240 B / sha256 24f7221b... ; this lane's module adds one
# module parameter (65 -> 66) and the dfs_set code.
# Outputs: /tmp/dfs-set-build (patched tree), /tmp/dfs-set-artifact, pins.
# Override with TREE_SRC / WORK / ART.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
R5=$DOCS/wifi-r5-rebuild
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/r5-build
[ -n "${WORK}" ] || WORK=/tmp/dfs-set-build
[ -n "${ART}" ] || ART=/tmp/dfs-set-artifact
[ -n "${TREECHECK}" ] || TREECHECK=/tmp/r5-rebuilt/hi5622_wifi.ko
set -u

assert_sha() {
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "build-dfs-set: $1 sha $got != $2" >&2; exit 1; }
}

# ---- 1. the byte-exact R5 base -----------------------------------------
if [ ! -f "$TREE_SRC/hi5622_scan.c" ] || [ ! -f "$TREECHECK" ]; then
	echo "build-dfs-set: R5 base absent - rebuilding it with wifi-r5-rebuild/tools/reproduce.sh"
	[ -x "$R5/tools/reproduce.sh" ] || {
		echo "build-dfs-set: $R5/tools/reproduce.sh absent" >&2; exit 2; }
	"$R5/tools/reproduce.sh"
fi
assert_sha "$TREECHECK" 24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c
# the two pristine R5 files the patch touches (all other files are new)
assert_sha "$TREE_SRC/hi5622_scan.c" 86d6c57ecc98d80906af1eeee1e5d6641c21a7ed5c1b2b53c70b85236b5b9373
assert_sha "$TREE_SRC/Kbuild" 1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d

# ---- 2. apply the dfs_set patch ----------------------------------------
assert_sha "$LANE/patches/0001-dfs-set.patch" 5030f4076d03e1647fb575e499c19e46fd28512e0be840a6b82985f348fbd2d7
rm -rf "$WORK" "$ART"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-dfs-set.patch" >/dev/null )
if ls "$WORK"/*.rej "$WORK"/*.orig >/dev/null 2>&1; then
	echo "build-dfs-set: patch left .rej/.orig files" >&2; exit 1; fi
grep -q "dfs_set: param=" "$WORK/hi5622_dfs_set.c" || {
	echo "build-dfs-set: the dfs_set builder did not apply" >&2; exit 1; }
assert_sha "$WORK/hi5622_scan.c" 08d3aefecfac2f5e3cb93cb1632705f55e2f703ae29cfe50a19b465eee49bb98
assert_sha "$WORK/Kbuild" 8f97661898373f8b65befcc3a62b6b00407101430e2924d74f142904d518c9e9
assert_sha "$WORK/hi5622_dfs_set.c" f7631f7cbfcd9fff77625ca556120f8a9152d1c129a6c1db9511a29460115e79
assert_sha "$WORK/hi5622_dfs_set.h" 18f951b90811b27f1e9c8e976279bee094e26656ec1581dadf39ccf99565f171

# ---- 3. locate the OpenWrt 6.18.60 kernel tree / toolchain --------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "$KDIR" ] || { echo "build-dfs-set: 6.18.60 kernel tree not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build-dfs-set: cross gcc missing" >&2; exit 2; }

# ---- 4. build -----------------------------------------------------------
mkdir -p "$ART"
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "build-dfs-set: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE "warning:|error:|undefined!" "$WORK/build.log"; then
	echo "build-dfs-set: warnings/errors - refusing artifact" >&2; exit 1; fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-dfs-set: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 66 ] || { echo "build-dfs-set: params $NP != 66" >&2; exit 1; }
for s in "dfs_set: param=" "dfs_set pre:" "dfs_set arm post:" "dfs_set clear post:" \
	 "after the 0x0065 VAP start and before the 0x0242 band start" \
	 "the firmware armed the band-1 SMAC sub-base + CAC flag"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build-dfs-set: artefact string missing: $s" >&2; exit 1; }
done

# ---- 5. host tests ------------------------------------------------------
gcc -std=c99 -Wall -Wextra -Werror -O2 -I"$WORK" -o "$ART/dfs_set_selftest" \
	"$WORK/hi5622_dfs_set.c" "$LANE/src/dfs_set_selftest.c"
"$ART/dfs_set_selftest" > "$ART/selftest.txt"
python3 "$LANE/tools/dfs_set_frame_test.py" "$WORK" > "$ART/frame-test.txt"
tail -1 "$ART/selftest.txt"
tail -1 "$ART/frame-test.txt"

# ---- 6. pins ------------------------------------------------------------
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
BASEART=$(sha256sum "$TREECHECK" | cut -d' ' -f1)
cat <<EOF
== DONE - dfs_set module (host-built, NOT fired)
artifact:  $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY)
size:      $SIZE B (stripped); unstripped $(stat -c %s "$WORK/hi5622_wifi.ko") B
sha256:    $SHA
unstripped: $USHA
text:      $TSHA
params:    $NP (65 + dfs_set)
base:      the byte-exact R5 tree ($TREECHECK = $BASEART)
tests:     C contract selftest + Python integration test (see $ART/*.txt)
EOF
