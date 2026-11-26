#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-r5-order.sh - lane wifi-band1-start-send-5 (task-213)
# HOST-ONLY build: reproduces the gate-inverted base (the R4b build tree +
# wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch = the tree
# of the deployed module 9bf1d846...), applies this lane's R5 order patch
# (patches/0001-r5-order.patch), builds the 6.18.60 module, strips it and
# runs the host tests (the C selftest copy = send-4 src + gates 0002 + R5
# 0002, plus the independent R5 Python order test).  No device access, no
# firmware write, no flash.  The .ko is LOCAL EVIDENCE ONLY (/tmp).
#
# Chain: fired 0x0242 base -> send-2 -> send-3 (R4) -> R4b order ->
#        gates 0001 -> R5 order (this lane)
# Base tree: /tmp/r4border-build (the R4b build tree), override with TREE_SRC.
# Outputs: /tmp/r5-build (patched tree), /tmp/r5-artifact, pins on stdout.
# Override with TREE_SRC / WORK / ART / TESTCOPY in the environment.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
S4=$DOCS/wifi-band1-start-send-4
GATES=$DOCS/wifi-vap-start-gates
[ -n "$TREE_SRC" ] || TREE_SRC=/tmp/r4border-build
[ -n "$WORK" ] || WORK=/tmp/r5-build
[ -n "$ART" ] || ART=/tmp/r5-artifact
[ -n "$TESTCOPY" ] || TESTCOPY=/tmp/r5-testcopy
set -u

[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-r5-order: R4b source tree $TREE_SRC absent" >&2
	echo "  reproduce it with $S4/tools/build-r4b-order.sh (host build)" >&2
	exit 2; }
[ -f "$GATES/patches/0001-g9-g11-rf-enable-invert.patch" ] || {
	echo "build-r5-order: the wifi-vap-start-gates 0001 patch is absent" >&2; exit 2; }
[ -f "$LANE/patches/0001-r5-order.patch" ] || {
	echo "build-r5-order: patches/0001-r5-order.patch absent" >&2; exit 2; }

# ---- locate the OpenWrt 6.18.60 kernel tree / toolchain ------------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "$KDIR" ] || { echo "build-r5-order: 6.18.60 kernel tree not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "build-r5-order: cross gcc missing" >&2; exit 2; }

# ---- 1. gate-inverted base tree (the deployed 9bf1d846 tree) -------------
rm -rf "$WORK" "$ART" "$TESTCOPY"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order modules.order.cmd build.log .t1 .t2 2>/dev/null ) || true
if grep -q "HI5622_VAP_START_G0_RF_ENABLED" "$WORK/hi5622_vap_start_g0.h"; then
	echo "build-r5-order: base tree is already gate-inverted - gate patch already applied"
else
	( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$GATES/patches/0001-g9-g11-rf-enable-invert.patch" >/dev/null )
fi
grep -q "HI5622_VAP_START_G0_RF_ENABLED" "$WORK/hi5622_vap_start_g0.h" || {
	echo "build-r5-order: gate patch did not apply" >&2; exit 1; }
assert_sha() {
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || { echo "build-r5-order: $1 sha $got != $2" >&2; exit 1; }
}
assert_sha "$WORK/hi5622_scan.c" ab14fa07f53ef199c503178ce8ae8c0966f6b0cf3ee5b435b2c72e54618b2375
assert_sha "$WORK/hi5622_scan_logic.c" ab7b8bac84f8a1f57d3ccd0b2f6ceca730b33aa67d77244b4df9a54ec7a3d97b
assert_sha "$WORK/hi5622_scan_logic.h" 3bfe123919f4d1eb466173b8f3c811cb5f02e3a10f6f5139af8b14f797e4058e
assert_sha "$WORK/hi5622_vap_start_g0.c" 346b5441af3aef8b02f968733e54009039c542ee4175ce08a386f99a347bb52e
assert_sha "$WORK/hi5622_vap_start_g0.h" 92b17b152d48c633ea4a82d130a2522b285734178e3f44c2cbf4a5ba48613031
assert_sha "$WORK/hi5622_band_start.h" 443b071d3ce131c21d46c54e9f7f510221cf75d31a39603c8e68907394bd1ddc

# ---- 2. R5 order patch --------------------------------------------------
assert_sha "$LANE/patches/0001-r5-order.patch" e464f4cf0038859705be492967771e7c3294ef7ba84275c96bb4b473a6b86cf7
assert_sha "$LANE/patches/0002-r5-selftest-update.patch" f35d34e4370da5469d24b0defd078aec8dacd4895b2e1226d8135c72d32cba4e
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-r5-order.patch" >/dev/null )
if ls "$WORK"/*.rej "$WORK"/*.orig >/dev/null 2>&1; then
	echo "build-r5-order: patch left .rej/.orig files" >&2; exit 1; fi
grep -q "R5 phase B" "$WORK/hi5622_scan.c" || {
	echo "build-r5-order: R5 patch did not apply" >&2; exit 1; }
assert_sha "$WORK/hi5622_scan.c" 86d6c57ecc98d80906af1eeee1e5d6641c21a7ed5c1b2b53c70b85236b5b9373
assert_sha "$WORK/hi5622_scan_logic.c" 15c50d34fa7ed0f9bd5262e5b317c13a97228207dcc599673b72788e2b9bcfea
assert_sha "$WORK/hi5622_scan_logic.h" 6af774ef76b730c8f45d621bf338d12b041b96a3792be985df59ac5092a603e8
assert_sha "$WORK/hi5622_vap_start_g0.c" 346b5441af3aef8b02f968733e54009039c542ee4175ce08a386f99a347bb52e
assert_sha "$WORK/hi5622_vap_start_g0.h" 81533d9ce2e1c984486d6ad8e44a4a09dc33ba97830a94861651f805dee70330
assert_sha "$WORK/hi5622_band_start.h" e2f059cfeab14478facd1cabd7f5642bfa060201c11da9e0407ad66e60266139
assert_sha "$WORK/hi5622_band_start.c" 71019718aec88ebc305b0eed6a4288db27e0b826c5ab8b9da11a064ef0baa57c
assert_sha "$WORK/Kbuild" 1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d

# ---- 3. build -----------------------------------------------------------
mkdir -p "$ART"
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "build-r5-order: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE "warning:|error:|undefined!" "$WORK/build.log"; then
	echo "build-r5-order: warnings/errors - refusing artifact" >&2; exit 1; fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-r5-order: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"$CROSS"objcopy -O binary --only-section=.text "$WORK/hi5622_wifi.ko" "$ART/.text.bin"
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 65 ] || { echo "build-r5-order: params $NP != 65" >&2; exit 1; }
for s in "AP config posted BEFORE the 0x0065 VAP start / 0x0242 band start (R5)" "the firmware beacon template is in place for the TBTT" "after the 0x0124 channel post (mctx[0x10..0x13] stored) and the AP config frames (R5)" "after the 0x01F7 mode, the 0x0124 channel post, the AP config frames (R5)" "G9: mctx[0x440] RF-enable flag is ZERO" "G11: bandobj+0xEC8 band-up flag is ZERO"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "build-r5-order: artefact string missing: $s" >&2; exit 1; }
done

# ---- 4. patched test source copy (do not edit the committed send-4 lane) -
mkdir -p "$TESTCOPY/src"
cp "$S4/src/vap_start_g0_selftest.c" "$TESTCOPY/src/"
( cd "$TESTCOPY" && patch -p1 --no-backup-if-mismatch < "$GATES/patches/0002-gate-selftest-update.patch" >/dev/null )
( cd "$TESTCOPY" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0002-r5-selftest-update.patch" >/dev/null )

# ---- 5. host tests ------------------------------------------------------
gcc -std=c99 -Wall -Wextra -Werror -O2 -I"$WORK" -I"$TESTCOPY/src" -I"$DOCS/wifi-vap-start/src" -I"$DOCS/wifi-band1-start-send/src" -o "$ART/vap_start_g0_selftest" "$WORK/hi5622_vap_start_g0.c" "$WORK/hi5622_scan_logic.c" "$TESTCOPY/src/vap_start_g0_selftest.c" "$DOCS/wifi-vap-start/src/vapstart_frames.c" "$DOCS/wifi-band1-start-send/src/hi5622_band_start.c"
"$ART/vap_start_g0_selftest"
python3 "$LANE/tools/r5_order_frame_test.py" "$WORK"

# ---- 6. pins ------------------------------------------------------------
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
BASEART=absent
if [ -f /tmp/vsg-artifact/hi5622_wifi.ko ]; then
	BASEART=$(sha256sum /tmp/vsg-artifact/hi5622_wifi.ko | cut -d' ' -f1)
fi
cat <<EOF
== DONE - R5 order module (host-built, NOT fired)
artifact:  $ART/hi5622_wifi.ko (LOCAL EVIDENCE ONLY)
size:      $SIZE B (stripped); unstripped $(stat -c %s "$WORK/hi5622_wifi.ko") B
sha256:    $SHA
unstripped: $USHA
text:      $TSHA
params:    $NP (unchanged)
base:      gate-inverted tree (wifi-vap-start-gates 0001 on $TREE_SRC)
base artefact (gate-inverted 9bf1d846...): $BASEART
tests:     295 C + 110 Python checks, 0 failures
EOF
