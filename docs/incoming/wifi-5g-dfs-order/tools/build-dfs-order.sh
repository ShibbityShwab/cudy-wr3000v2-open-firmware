#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-dfs-order.sh - lane wifi-5g-dfs-order (task-228)
# HOST-ONLY build: takes the dfs_set tree (/tmp/dfs-set-build, the tree of the
# host-built module 1b47f0da of lane wifi-5g-dfs-set), applies this lane's
# patches/0001-dfs-order.patch (0x07E7 DFS-SET ARM BEFORE the 0x01F7 dfs_mode),
# builds the 6.18.60 module, strips it and runs the host tests (the sibling C
# contract selftest + this lane's independent Python ORDER test).
# No device access, no firmware write, no flash.  The .ko is LOCAL EVIDENCE
# ONLY (/tmp).
#
# Base provenance: /tmp/dfs-set-build is rebuilt by
# ../wifi-5g-dfs-set/tools/build-dfs-set.sh (which re-derives the byte-exact
# R5 base 24f7221b if absent).  This lane changes three files: hi5622_scan.c
# (block order + comments), hi5622_scan_logic.c (plan order + comments) and
# hi5622_dfs_set.h (docs only).
# Outputs: /tmp/dfs-order-build (patched tree), /tmp/dfs-order-artifact, pins.
# Override with TREE_SRC / WORK / ART.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
SET=$DOCS/wifi-5g-dfs-set
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/dfs-set-build
[ -n "${WORK}" ] || WORK=/tmp/dfs-order-build
[ -n "${ART}" ] || ART=/tmp/dfs-order-artifact
[ -n "${SETCHK}" ] || SETCHK=/tmp/dfs-set-artifact/hi5622_wifi.ko
set -u

assert_sha() {
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "build-dfs-order: $1 sha $got != $2" >&2; exit 1; }
}

# ---- 1. the dfs_set base tree (module 1b47f0da) ------------------------
if [ ! -f "$TREE_SRC/hi5622_scan.c" ] || [ ! -f "$SETCHK" ]; then
	echo "build-dfs-order: dfs_set base absent - rebuilding it with wifi-5g-dfs-set/tools/build-dfs-set.sh"
	[ -x "$SET/tools/build-dfs-set.sh" ] || {
		echo "build-dfs-order: $SET/tools/build-dfs-set.sh absent" >&2; exit 2; }
	"$SET/tools/build-dfs-set.sh"
fi
assert_sha "$SETCHK" 1b47f0daff7e2e48d0ac6dcbc34b4135872f6a16088e8d20b88c71d47c684acb
assert_sha "$TREE_SRC/hi5622_scan.c" 08d3aefecfac2f5e3cb93cb1632705f55e2f703ae29cfe50a19b465eee49bb98
assert_sha "$TREE_SRC/hi5622_scan_logic.c" 15c50d34fa7ed0f9bd5262e5b317c13a97228207dcc599673b72788e2b9bcfea
assert_sha "$TREE_SRC/hi5622_dfs_set.h" 18f951b90811b27f1e9c8e976279bee094e26656ec1581dadf39ccf99565f171

# ---- 2. apply the order patch -----------------------------------------
assert_sha "$LANE/patches/0001-dfs-order.patch" 978c3d5b5795357183398f33e1af510129d504904479c59a30cb98250d361fd8
rm -rf "$WORK" "$ART"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-dfs-order.patch" >/dev/null )
if ls "$WORK"/*.rej "$WORK"/*.orig >/dev/null 2>&1; then
	echo "build-dfs-order: patch left .rej/.orig files" >&2; exit 1; fi
assert_sha "$WORK/hi5622_scan.c" d2e894d4b24b6a6a0c4481a161eb012a5dc763455aa1e4cbc6045342fce12713
assert_sha "$WORK/hi5622_scan_logic.c" 4400d546f4da045643bad1588b005ca6ad4774ba82d2d80a6d8c4b904e355f3c
assert_sha "$WORK/hi5622_dfs_set.h" bb25e2faa4c962f55b0b79673029f267f00c3c3ee76efa3f866643b24be139c0
# the ARM block must be ahead of the dfs_mode block in the patched source
python3 - "$WORK/hi5622_scan.c" <<'PY'
import sys
s = open(sys.argv[1], encoding="utf-8").read()
i_set = s.index("lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7")
i_mode = s.index("MOVED BY THE task-228 ORDER FIX")
i_band = s.index("\t\t/* lane wifi-band1-start-send (task-206): the H2D g0-id1 key\n")
assert i_set < i_mode < i_band, (i_set, i_mode, i_band)
print("order: dfs_set < dfs_mode < band_start (byte offsets %d < %d < %d)" % (i_set, i_mode, i_band))
PY

# ---- 3. locate the OpenWrt 6.18.60 kernel tree / toolchain -------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "$KDIR" ] || { echo "build-dfs-order: 6.18.60 kernel tree not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build-dfs-order: cross gcc missing" >&2; exit 2; }

# ---- 4. build ----------------------------------------------------------
mkdir -p "$ART"
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "build-dfs-order: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE "warning:|error:|undefined!" "$WORK/build.log"; then
	echo "build-dfs-order: warnings/errors - refusing artifact" >&2; exit 1; fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-dfs-order: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 66 ] || { echo "build-dfs-order: params $NP != 66" >&2; exit 1; }
for s in "dfs_set pre:" "dfs_set arm post:" "dfs_set clear post:" \
	 "dfs_set: param=" \
	 "after the 0x0065 VAP start and before the 0x01F7 dfs_mode / 0x0242 band start" \
	 "after the 0x0065 VAP start and the 0x07E7 dfs_set step" \
	 "the firmware armed the band-1 SMAC sub-base + CAC flag"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build-dfs-order: artefact string missing: $s" >&2; exit 1; }
done

# ---- 5. host tests -----------------------------------------------------
gcc -std=c99 -Wall -Wextra -Werror -O2 -I"$WORK" -o "$ART/dfs_set_selftest" \
	"$WORK/hi5622_dfs_set.c" "$SET/src/dfs_set_selftest.c"
"$ART/dfs_set_selftest" > "$ART/host-selftest-c.txt"
python3 "$LANE/tools/dfs_order_test.py" "$WORK" > "$ART/host-test-order.txt"
tail -1 "$ART/host-selftest-c.txt"
tail -1 "$ART/host-test-order.txt"

# ---- 6. pins -----------------------------------------------------------
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
BASEART=$(sha256sum "$SETCHK" | cut -d' ' -f1)
cat <<EOF
== DONE - dfs_set ORDER module (host-built, NOT fired)
artifact:  $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY)
size:      $SIZE B (stripped); unstripped $(stat -c %s "$WORK/hi5622_wifi.ko") B
sha256:    $SHA
unstripped: $USHA
text:      $TSHA
params:    $NP (66 = the R5 65 + dfs_set; the order patch adds no parameter)
base:      the dfs_set tree 1b47f0da (this tree = $BASEART)
order:     dfs_set(0x07E7) BEFORE dfs_mode(0x01F7), after vap_start_g0(0x0065)
tests:     C contract selftest + Python ORDER test (see $ART/*.txt)
EOF
