#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-dfs-relax.sh - lane wifi-5g-dfs-relax (shared task task-230)
# HOST-ONLY build: takes the dfs_order tree (/tmp/dfs-order-build, the tree of
# the host-built module a5ec150c), applies this lane's patches/0001-dfs-relax.patch
# (the +0x4A0 register-only-arm pre-gate relax + the postcheck/arm_kind/four-word
# probe pack), builds the 6.18.60 module, strips it and runs the host tests
# (this lane's extended C contract selftest + this lane's independent Python
# RELAX test + the sibling ORDER test as a regression).  No device access, no
# firmware write, no flash.  The .ko is LOCAL EVIDENCE ONLY (/tmp).
#
# Base provenance: /tmp/dfs-order-build is rebuilt by
# ../wifi-5g-dfs-order/tools/build-dfs-order.sh if absent (that lane rebuilds
# the dfs_set base itself).  This lane changes exactly three files:
# hi5622_dfs_set.c, hi5622_dfs_set.h, hi5622_scan.c.
# Outputs: /tmp/dfs-relax-build (patched tree), /tmp/dfs-relax-artifact, pins.
# Override with TREE_SRC / WORK / ART.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
ORDER=$DOCS/wifi-5g-dfs-order
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/dfs-order-build
[ -n "${WORK}" ] || WORK=/tmp/dfs-relax-build
[ -n "${ART}" ] || ART=/tmp/dfs-relax-artifact
[ -n "${ORDERCHK}" ] || ORDERCHK=/tmp/dfs-order-artifact/hi5622_wifi.ko
set -u

assert_sha() {
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "build-dfs-relax: $1 sha $got != $2" >&2; exit 1; }
}

# ---- 1. the dfs_order base tree (module a5ec150c) ----------------------
if [ ! -f "$TREE_SRC/hi5622_scan.c" ] || [ ! -f "$ORDERCHK" ]; then
	echo "build-dfs-relax: dfs_order base absent - rebuilding it with wifi-5g-dfs-order/tools/build-dfs-order.sh"
	[ -x "$ORDER/tools/build-dfs-order.sh" ] || {
		echo "build-dfs-relax: $ORDER/tools/build-dfs-order.sh absent" >&2; exit 2; }
	"$ORDER/tools/build-dfs-order.sh"
fi
assert_sha "$ORDERCHK" a5ec150cfc7d7b8df1b87bd59f5847f0e720b772e69524c76f2d26620babdb7e
assert_sha "$TREE_SRC/hi5622_scan.c" d2e894d4b24b6a6a0c4481a161eb012a5dc763455aa1e4cbc6045342fce12713
assert_sha "$TREE_SRC/hi5622_scan_logic.c" 4400d546f4da045643bad1588b005ca6ad4774ba82d2d80a6d8c4b904e355f3c
assert_sha "$TREE_SRC/hi5622_dfs_set.h" bb25e2faa4c962f55b0b79673029f267f00c3c3ee76efa3f866643b24be139c0
assert_sha "$TREE_SRC/hi5622_dfs_set.c" f7631f7cbfcd9fff77625ca556120f8a9152d1c129a6c1db9511a29460115e79

# ---- 2. apply the relax patch ------------------------------------------
assert_sha "$LANE/patches/0001-dfs-relax.patch" a7e1a286277689eaad3fb3cf3852cba408acb23b222b1b9b2fd84dfecbd0a9b9
rm -rf "$WORK" "$ART"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-dfs-relax.patch" >/dev/null )
if ls "$WORK"/*.rej "$WORK"/*.orig >/dev/null 2>&1; then
	echo "build-dfs-relax: patch left .rej/.orig files" >&2; exit 1; fi
assert_sha "$WORK/hi5622_dfs_set.c" 9c64594dc4a56487828d88385244d0d37b2503877520d05427ea826f18083330
assert_sha "$WORK/hi5622_dfs_set.h" f8bb83b3d5a926952bd9bf4bf7ca1109f77d160d03f69ccf3e21589f043168f7
assert_sha "$WORK/hi5622_scan.c" 75b12c4dc4b60dc458153497b1fc4cac6927d5ede85b98f5fdb2b8efdbb71273
# the ladder order of task-228 must survive the relax
python3 - "$WORK" <<'PY'
import sys
s = open(sys.argv[1] + "/hi5622_scan.c", encoding="utf-8").read()
i_0056 = s.index("cfg80211: vap_start_g0 post:")
i_set = s.index("lane wifi-5g-dfs-set (task-227): the H2D g0-id1 key 0x07E7")
i_mode = s.index("MOVED BY THE task-228 ORDER FIX")
i_band = s.index("\t\t/* lane wifi-band1-start-send (task-206): the H2D g0-id1 key\n")
assert i_0056 < i_set < i_mode < i_band, (i_0056, i_set, i_mode, i_band)
i_pre0 = s.index("cfg80211: dfs_ctx pre0x0065:")
assert i_pre0 < i_0056, (i_pre0, i_0056)
print("order: 0x0065 < pre0x0065-line < 0x07E7 < 0x01F7 < 0x0242 (byte offsets ok)")
PY

# ---- 3. locate the OpenWrt 6.18.60 kernel tree / toolchain -------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "$KDIR" ] || { echo "build-dfs-relax: 6.18.60 kernel tree not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build-dfs-relax: cross gcc missing" >&2; exit 2; }

# ---- 4. build ----------------------------------------------------------
mkdir -p "$ART"
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "build-dfs-relax: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE "warning:|error:|undefined!" "$WORK/build.log"; then
	echo "build-dfs-relax: warnings/errors - refusing artifact" >&2; exit 1; fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-dfs-relax: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 66 ] || { echo "build-dfs-relax: params $NP != 66" >&2; exit 1; }
for s in "dfs_ctx pre0x0065:" \
	 "ctx_present=%u arm_kind=%s" \
	 "the 0x0065 already allocated the CAC context" \
	 "register-only arm" \
	 "dfs_set pre:" "dfs_set arm post:" "dfs_set clear post:" \
	 "the firmware armed the band-1 SMAC sub-base + CAC flag" \
	 "after the 0x0065 VAP start and before the 0x01F7 dfs_mode / 0x0242 band start" \
	 "after the 0x0065 VAP start and the 0x07E7 dfs_set step"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build-dfs-relax: artefact string missing: $s" >&2; exit 1; }
done

# ---- 5. host tests -----------------------------------------------------
gcc -std=c99 -Wall -Wextra -Werror -O2 -I"$WORK" -o "$ART/dfs_set_selftest" \
	"$WORK/hi5622_dfs_set.c" "$LANE/src/dfs_set_selftest.c"
"$ART/dfs_set_selftest" > "$ART/host-selftest-c.txt"
python3 "$LANE/tools/dfs_relax_test.py" "$WORK" > "$ART/host-test-relax.txt"
python3 "$ORDER/tools/dfs_order_test.py" "$WORK" > "$ART/host-test-order.txt"
tail -1 "$ART/host-selftest-c.txt"
tail -1 "$ART/host-test-relax.txt"
tail -1 "$ART/host-test-order.txt"

# ---- 6. pins -----------------------------------------------------------
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$WORK/hi5622_wifi.ko")
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
BASEART=$(sha256sum "$ORDERCHK" | cut -d' ' -f1)
cat <<EOF
== DONE - dfs_set RELAX module (host-built, NOT fired)
artifact:  $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY)
size:      $SIZE B (stripped); unstripped $USIZE B
sha256:    $SHA
unstripped: $USHA
text:      $TSHA
params:    $NP (66 = the R5 65 + dfs_set; the relax adds no parameter)
base:      the dfs_order tree a5ec150c (this tree = $BASEART)
relax:     the ARM/ARM_CLEAR +0x4A0 pre-gate allows the firmware's
           register-only silent-success arm; the postcheck grades the six
           band-1 SMAC sub-base pattern 0x2D + the CAC flag 1 (full) / 0
           (register-only), and the dmesg prints ctx_present/arm_kind.
order:     vap_start_g0(0x0065) -> dfs_ctx line -> dfs_set(0x07E7) -> dfs_mode(0x01F7) -> band_start(0x0242)
tests:     C contract selftest + Python RELAX test + Python ORDER regression
EOF
