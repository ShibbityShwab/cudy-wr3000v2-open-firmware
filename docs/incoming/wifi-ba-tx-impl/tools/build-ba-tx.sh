#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-ba-tx.sh - lane wifi-ba-tx-impl (task-217)
#
# HOST-ONLY build of the BA TX-direction cut on the current merged-module
# base (the F2/R5 lineage: the R5 order tree, 65 params, sha pin list below):
# applies the committed integration patch to a COPY of the base tree, builds
# the 6.18.60 module, strips it, checks the artefact contract and runs the
# host tests (standalone + against the driver's own H2D decode gate).
#
# No device access, no firmware write, no flash.  The .ko is LOCAL EVIDENCE
# ONLY (/tmp, never committed - this repo stores no binary artifacts).
#
# Base tree: ${TREE_SRC:-/tmp/r5-build}, reproduce with
#   ../wifi-band1-start-send-5/tools/build-r5-order.sh
# (or ../wifi-band1-start-send-4 -> ...-3 -> ...); the pins below prove the
# lineage.  Override TREE_SRC / WORK / ART / OUT for a different setup.
set -eu
set +u
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
[ -n "${TREE_SRC}" ] || TREE_SRC=/tmp/r5-build
[ -n "${WORK}" ] || WORK=/tmp/ba-tx-build
[ -n "${ART}" ] || ART=/tmp/batx-artifact
[ -n "${OUT}" ] || OUT=/tmp/batx-artifact
DEFAULT_WORK=/tmp/ba-tx-build
DEFAULT_ART=/tmp/batx-artifact
set -u

die() { echo "build-ba-tx: $*" >&2; exit 1; }
assert_sha() {
	got=$(sha256sum "$1" | cut -d' ' -f1)
	[ "$got" = "$2" ] || die "$1 sha $got != $2"
}
[ -f "$TREE_SRC/hi5622_scan.c" ] || {
	echo "build-ba-tx: base tree $TREE_SRC absent" >&2
	echo "  reproduce it with $DOCS/wifi-band1-start-send-5/tools/build-r5-order.sh" >&2
	exit 2; }
[ -f "$LANE/patches/0001-ba-tx-impl.patch" ] || die "the integration patch is absent"
[ -f "$TREE_SRC/cfg80211-provider.symvers" ] || die "cfg80211-provider.symvers absent in the base tree"

# ---- locate the OpenWrt 6.18.60 kernel tree / toolchain -----------------
KDIR=
STAGING_DIR=
for root in /home/shibbityshwab/openwrt-lab-upstream-target \
	    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
	c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
done
[ -d "${KDIR:-}" ] || die "the 6.18.60 kernel tree was not found"
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || die "the OpenWrt cross gcc is missing"

# ---- pins ---------------------------------------------------------------
# the merged-module base (the F2/R5 lineage; R5 build notes pins)
assert_sha "$TREE_SRC/Kbuild" 1bb510853b305577d36e19a09507407f8f10d0a3d3531a5b4946a7d9b1fc141d
assert_sha "$TREE_SRC/main.c" 605545acf39e985ebeb016a6b8759669b30abaf7cb7495ad4b98e23fdfe0f13c
assert_sha "$TREE_SRC/hcc.c" 7481791dcb803893d3e2bb095544690e0c6e1c3e4816ca9468d615e61a2bb4f3
assert_sha "$TREE_SRC/hi5622_mgmt.c" 7491cf3236ce7b461fa446d470bca3a9eb1dde8767f7fe59fe990b1d0e73dd8e
assert_sha "$TREE_SRC/hi5622_data_tx.c" 2bc0aa826d29ec1815a86771016a74a9f90928df8d8318c51d40bdea6c44f8a1
assert_sha "$TREE_SRC/hi5622_scan.c" 86d6c57ecc98d80906af1eeee1e5d6641c21a7ed5c1b2b53c70b85236b5b9373
assert_sha "$TREE_SRC/hi5622_vap_start_g0.h" 81533d9ce2e1c984486d6ad8e44a4a09dc33ba97830a94861651f805dee70330
assert_sha "$TREE_SRC/hi5622_scan_logic.c" 15c50d34fa7ed0f9bd5262e5b317c13a97228207dcc599673b72788e2b9bcfea
assert_sha "$TREE_SRC/hi5622_scan_logic.h" 6af774ef76b730c8f45d621bf338d12b041b96a3792be985df59ac5092a603e8
assert_sha "$LANE/patches/0001-ba-tx-impl.patch" \
	0964fbf77d53e9bde6b19216f43d268b0f0c7a19356d567a43e5678cdd6401ac
assert_sha "$TREE_SRC/cfg80211-provider.symvers" \
	bd18897af88f58d550be31acf430e08af2b82f54e0fcdc780c8f1464e7f94df7
assert_sha "$LANE/src/hi5622_ba.h" \
	ed6d472c147bbbada50882e7e93d51735af7bbcea25c8f04ad855e63f2584566
assert_sha "$LANE/src/hi5622_ba.c" \
	d865a3e4afef2e651b48daddb2edd040af4e2803964565839c2a155806b01bd3
assert_sha "$LANE/src/hi5622_ba_glue.h" \
	3573b7debe90a8d5246d50b2ab1fb7b90ccca309e72ae0fdf3aaf0ef7f385504
assert_sha "$LANE/src/hi5622_ba_glue.c" \
	f08c79525e63aac3149a7771ece339b040c3bc5447ad8729e66e21f8eb00f2a8

# ---- 1. clean copy of the pinned base -----------------------------------
rm -rf "$WORK" "$ART" "$OUT"
mkdir -p "$ART" "$OUT"
cp -a "$TREE_SRC" "$WORK"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
	modules.order modules.order.cmd build.log 2>/dev/null ) || true

# ---- 2. apply the lane patch (no fuzz, no backups, no .rej) -------------
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE/patches/0001-ba-tx-impl.patch" >/dev/null )
if ls "$WORK"/*.rej "$WORK"/*.orig >/dev/null 2>&1; then
	die "patch left .rej/.orig files"
fi
assert_sha "$WORK/Kbuild" c50e86f0e52662a9ae7020e9f822ca34a34fc82367fe4db0154b659ebc62b6bc
assert_sha "$WORK/main.c" 37f14be9c9afc4cc7316bf401bc84e5317aa07080ead3923d6ad0bbbf139c493
assert_sha "$WORK/hcc.c" 07fa2a50b9bfef54cf4859b2efc93d209111f96f5b4368cfe8842bdb1e8a4e6c
assert_sha "$WORK/hi5622_mgmt.c" 2d6d791d014571ae994def348debf2e6fbb60c3f911daf98f89fd6e097b1fb42
assert_sha "$WORK/hi5622_data_tx.c" 2babf88e0eb832570b124211f55f1bfd7be50a69f21deefd0fab78561f3d0c09
assert_sha "$WORK/hi5622_scan.c" 0fc1d58a2ccfbbb7b9e9ccb6fb986d036818453d859e4ead64d259e637255653
assert_sha "$WORK/hi5622_ba.h" ed6d472c147bbbada50882e7e93d51735af7bbcea25c8f04ad855e63f2584566
assert_sha "$WORK/hi5622_ba.c" d865a3e4afef2e651b48daddb2edd040af4e2803964565839c2a155806b01bd3
assert_sha "$WORK/hi5622_ba_glue.h" 3573b7debe90a8d5246d50b2ab1fb7b90ccca309e72ae0fdf3aaf0ef7f385504
assert_sha "$WORK/hi5622_ba_glue.c" f08c79525e63aac3149a7771ece339b040c3bc5447ad8729e66e21f8eb00f2a8

# ---- 3. build -----------------------------------------------------------
export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "build-ba-tx: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE "warning:|error:|undefined!" "$WORK/build.log"; then
	die "warnings/errors - refusing the artefact"
fi
[ -f "$WORK/hi5622_wifi.ko" ] || die "no .ko produced"
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko"
"${CROSS}objcopy" -O binary --only-section=.text "$WORK/hi5622_wifi.ko" "$ART/.text.bin"

# ---- 4. artefact contract ----------------------------------------------
NP=$(modinfo -p "$ART/hi5622_wifi.ko" | wc -l)
[ "$NP" = 66 ] || die "params $NP != 66 (65 base + ba_enable)"
modinfo -p "$ART/hi5622_wifi.ko" | grep -q '^ba_enable: (uint)' || \
	die "no ba_enable param"
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
USIZE=$(stat -c %s "$WORK/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USHA=$(sha256sum "$WORK/hi5622_wifi.ko" | cut -d' ' -f1)
TSHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
for s in "TX-direction block-ack ARMED" "ba: TX-direction block-ack OFF" \
	 "in-kernel ADDBA originator" "ba_enable=%u (%s) peers=%u active=%u" \
	 "ba_relay" "ba_ampdu" "ba_notify" "ba_mgmt_tx" \
	 "ba: AP up recorded" "ba: AP down - BA sessions cleared"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || \
		die "artefact string missing: $s"
done
# the core is WIRED (not the skeleton any more): every glue entry point is
# referenced from the driver's own code paths, so these must find call sites.
for s in "hi5622_ba_glue_d2h" "hi5622_ba_glue_mgmt_rx" "hi5622_ba_glue_tx_data" \
	 "hi5622_ba_glue_ap_up" "hi5622_ba_glue_status_line"; do
	"${CROSS}nm" "$WORK/hi5622_wifi.ko" 2>/dev/null | grep -q " T $s" || \
		grep -q "$s" "$WORK/hi5622_wifi.ko" || die "entry point not linked: $s"
done
[ "$SIZE" = 406168 ] || die "stripped size $SIZE != 406168"
[ "$NP" = 66 ] || die "params $NP != 66"
if [ "$WORK" = "$DEFAULT_WORK" ] && [ "$ART" = "$DEFAULT_ART" ]; then
	[ "$SHA" = 234b3d4b961037e1a5465cc4cc38ab4603b2c71c451d06ac5ba7212b0104d841 ] || \
		die "artefact sha $SHA != 234b3d4b...4d841 (default-path reproducibility)"
	[ "$USIZE" = 1515740 ] || die "unstripped size $USIZE != 1515740"
	[ "$USHA" = 658a5887a17cbf69e12824bedff40222f9f6909eeda707f9bd13f715afeb8ddf ] || \
		die "unstripped sha $USHA != 658a5887..."
else
	echo "build-ba-tx: non-default WORK/ART - whole-file sha not pinned (build-id caveat); .text/size/params are"
fi
[ "$TSHA" = 702591233a117f0ccf7db2c6ce75ea0ee81c1e4307c10769bda4b7e2d20ec8fc ] || \
	die ".text sha $TSHA != 70259123..."

# ---- 5. host tests ------------------------------------------------------
OUT="$OUT" "$LANE/tools/run-host-tests.sh" "$WORK"
python3 "$LANE/tools/ba_tx_frame_test.py" "$WORK"

# ---- 6. pins ------------------------------------------------------------
echo "build-ba-tx: base tree        $TREE_SRC"
echo "build-ba-tx: patched tree     $WORK"
echo "build-ba-tx: artefact         $ART/hi5622_wifi.ko"
echo "build-ba-tx: stripped         $SIZE B sha256 $SHA"
echo "build-ba-tx: unstripped       $USIZE B sha256 $USHA"
echo "build-ba-tx: .text            sha256 $TSHA"
echo "build-ba-tx: params           $NP (ba_enable included)"
echo "build-ba-tx: PASS"
