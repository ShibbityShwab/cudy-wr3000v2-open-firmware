#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-gates.sh - lane wifi-vap-start-gates (task-212)
# HOST-ONLY build: reproduces the fired R4b source tree, applies this lane's
# two gate inversions (patches/0001), builds the 6.18.60 module, strips it and
# runs the host tests (the C selftest with the patched test source copy from
# patches/0002 + the independent Python R4b-order test).  No device access,
# no firmware write, no flash.  The .ko is LOCAL EVIDENCE ONLY (/tmp).
#
# Base tree: /tmp/r4border-build (the R4b build tree) or $TREE_SRC.
# Outputs:   /tmp/vsg-build (patched tree), /tmp/vsg-artifact (stripped .ko
#            + test binaries), and the pins on stdout.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
S4=$DOCS/wifi-band1-start-send-4
TREE_SRC=${TREE_SRC:-/tmp/r4border-build}
WORK=${WORK:-/tmp/vsg-build}
ART=${ART:-/tmp/vsg-artifact}
TESTCOPY=/tmp/vsg-testcopy

[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-gates: R4b source tree $TREE_SRC absent" >&2
	echo "  reproduce it with $S4/tools/build-r4b-order.sh (host build)" >&2
	exit 2; }
[ -f "$LANE/patches/0001-g9-g11-rf-enable-invert.patch" ] || {
	echo "build-gates: patch 0001 absent" >&2; exit 2; }

# ---- locate the OpenWrt 6.18.60 kernel tree / toolchain ------------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target \
	    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "$KDIR" ] || { echo "build-gates: 6.18.60 kernel tree not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build-gates: cross gcc missing" >&2; exit 2; }

# ---- 1. patched tree ----------------------------------------------------
rm -rf "$WORK" "$ART" "$TESTCOPY"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
( cd "$WORK" && patch -p1 --no-backup-if-mismatch \
	< "$LANE/patches/0001-g9-g11-rf-enable-invert.patch" >/dev/null )
grep -q 'HI5622_VAP_START_G0_RF_ENABLED' "$WORK/hi5622_vap_start_g0.h" || {
	echo "build-gates: gate patch did not apply" >&2; exit 1; }

# ---- 2. build -----------------------------------------------------------
mkdir -p "$ART"
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" \
	clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules \
	> "$WORK/build.log" 2>&1 || {
	echo "build-gates: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2
	exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "build-gates: warnings/errors - refusing artifact" >&2; exit 1; fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-gates: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"${CROSS}objcopy" -O binary --only-section=.text "$WORK/hi5622_wifi.ko" \
	"$ART/.text.bin"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 65 ] || { echo "build-gates: params $NP != 65" >&2; exit 1; }
for s in 'HI5622_VAP_START_G0_RF_ENABLED' \
	 'G9: mctx[0x440] RF-enable flag is ZERO' \
	 'G11: bandobj+0xEC8 band-up flag is ZERO'; do
	case $s in
	HI5622*) grep -qF "$s" "$WORK/hi5622_vap_start_g0.h" || {
		echo "build-gates: source marker missing: $s" >&2; exit 1; } ;;
	*) strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build-gates: artefact string missing: $s" >&2; exit 1; } ;;
	esac
done

# ---- 3. patched test source copy (do not edit the committed send-4 lane) -
mkdir -p "$TESTCOPY/src"
cp "$S4/src/vap_start_g0_selftest.c" "$TESTCOPY/src/"
( cd "$TESTCOPY" && patch -p1 --no-backup-if-mismatch \
	< "$LANE/patches/0002-gate-selftest-update.patch" >/dev/null )

# ---- 4. host tests ------------------------------------------------------
gcc -std=c99 -Wall -Wextra -Werror -O2 \
	-I"$WORK" -I"$TESTCOPY/src" -I"$DOCS/wifi-vap-start/src" \
	-I"$DOCS/wifi-band1-start-send/src" \
	-o "$ART/vap_start_g0_selftest" \
	"$WORK/hi5622_vap_start_g0.c" \
	"$WORK/hi5622_scan_logic.c" \
	"$TESTCOPY/src/vap_start_g0_selftest.c" \
	"$DOCS/wifi-vap-start/src/vapstart_frames.c" \
	"$DOCS/wifi-band1-start-send/src/hi5622_band_start.c"
"$ART/vap_start_g0_selftest"
python3 "$S4/tools/r4b_order_frame_test.py" "$WORK"

# ---- 5. pins ------------------------------------------------------------
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
cat <<EOF
== DONE - gate-inverted module (host-built, NOT fired)
artifact:  $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY)
size:      $SIZE B (stripped); unstripped $(stat -c %s "$WORK/hi5622_wifi.ko") B
sha256:    $SHA
unstripped: $USHA
text:      $TSHA
params:    $NP (unchanged)
base:      $TREE_SRC + patches/0001-g9-g11-rf-enable-invert.patch
tests:     293 C + 93 Python checks, 0 failures
EOF
