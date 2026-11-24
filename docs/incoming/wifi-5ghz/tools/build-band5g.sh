#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-band5g.sh - build the FINAL ALL-PATCH module + the wifi-5ghz patch.
# HOST-ONLY: compiles against the project 6.18.60 kernel tree; no device
# access, no firmware write, no image, no flash.  The .ko is local evidence
# only and is never committed.
#
# Chain: the hardware-validated FINAL all-patch tree (opensource 008aca5,
# stripped 308,080 B sha256 5b587949...) produced by
#   docs/incoming/wifi-stability/tools/build-all-patch-module.sh
# then this lane's patches/0001-band5g.patch on top:
#   + hi5622_band5g.c/.h        (5 GHz band tables + band-config helpers)
#   + hi5622_cfg80211.c         (band5g param, 5 GHz wiphy profile)
#   + hi5622_scan.c             (5 GHz AP-start gate, H1 channel fix)
#   + hi5622_vapscan_map.c      (vendor 29-ch scan-request index)
#   + Kbuild                    (hi5622_band5g.o)
#
# Usage:
#   ./tools/build-band5g.sh
#   CHAIN_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-band5g.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
PATCH=$LANE/patches/0001-band5g.patch
WORK=/tmp/wifi-band5g-build
ART=/tmp/wifi-band5g-artifact
CHAIN_TREE=${CHAIN_TREE:-}

KDIR=
STAGING_DIR=
set +u
[ -n "$KDIR_OVERRIDE" ] && KDIR=$KDIR_OVERRIDE
[ -n "$STAGING_OVERRIDE" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ] && [ -x "$root/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
[ -d "$KDIR" ] || { echo "band5g: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "band5g: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "band5g: cross gcc missing" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "band5g: missing patch $PATCH" >&2; exit 2; }

if [ -z "$CHAIN_TREE" ]; then
	if [ -d /tmp/wifi-allpatch-build/integ ]; then
		CHAIN_TREE=/tmp/wifi-allpatch-build/integ
	else
		echo "== 0. build the final all-patch tree (wifi-stability lane)"
		"$DOCS/wifi-stability/tools/build-all-patch-module.sh"
		CHAIN_TREE=/tmp/wifi-allpatch-build/integ
	fi
fi
[ -d "$CHAIN_TREE" ] || { echo "band5g: chain tree missing: $CHAIN_TREE" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$CHAIN_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order 2>/dev/null ) || true
echo "== 1. chain tree staged: $(ls "$WORK" | wc -l) files"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "band5g: conflict markers survived - refusing build" >&2; exit 1
fi
echo "== 2. patch applied cleanly"

export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "band5g: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "band5g: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "band5g: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "band5g: strip changed .text" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

PARMS="band5g fw_path wifi_block_path allow_frames frame_gap_ms h2d_send_budget hcc_poll_ms hcc_cece_mode vap_start_first vap_start_mode vap_start_module ap_start_mode scan_timeout_ms scan_dwell_ms scan_chan_time_ms scan_req_batch scan_batch_gap_ms rd_ab_write cfg80211_auto band_force hcc_trace stop_ap_mode mgmt_diag txq_publish tx_complete_ms mac_tail_writes rx_enable beacon_static beacon_vap_start wpa_keys wpa_key_module sta_join ht_mode vht_cap band_vendor"
MI=$(modinfo -p "$ART/hi5622_wifi.ko" 2>/dev/null)
missing=
for p in $PARMS; do
	if ! printf '%s\n' "$MI" | grep -q "^$p:"; then missing="$missing $p"; fi
done
[ -z "$missing" ] || { echo "band5g: missing params:$missing" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "48" ] || { echo "band5g: param count $NP != 48 (47 chain + band5g)" >&2; exit 1; }
for s in "band5g=1 5GHz wiphy profile active" \
	 "H1 band-config channel unresolved" \
	 "5 GHz AP needs band5g=1" \
	 "29 vendor 5 GHz channels"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "band5g: expected feature string missing: $s" >&2; exit 1; }
done
SV=$WORK/cfg80211-provider.symvers
[ "$(wc -l < "$SV")" = "10" ] || { echo "band5g: cfg80211 symvers != 10" >&2; exit 1; }
"${CROSS}nm" -u "$ART/hi5622_wifi.ko" | grep -q ' cfg80211_bss_iter$' || {
	echo "band5g: cfg80211_bss_iter missing" >&2; exit 1; }

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
echo "== BAND5G module (final all-patch + wifi-5ghz, host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "params    : $NP (47 chain + band5g)"
echo "$VERMAGIC"
echo "chain     : final all-patch base (008aca5, 308,080 B 5b587949) + wifi-5ghz/0001"
