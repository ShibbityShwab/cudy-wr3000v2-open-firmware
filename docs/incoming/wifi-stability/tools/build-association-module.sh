#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-association-module.sh - FINAL ASSOCIATION module (host-only).
# Chain: FINAL all-patch module (5b587949: base 44c6cc12 -> polished d145a205
# -> dp-tuning 175a4617 -> scan R4 -> stability -> rx/beacon/wpa/sta static)
# then, in order:
#   wifi-wpa3-sae  0001 (AKM/cipher/mgmt_stypes tx=0x3c2a, IGTK/mgmt key)
#   wifi-ap-mgmt   0001 (.mgmt_tx WAL 0x01F2 + .mgmt RX D2H g1 id13)
#   wifi-sta-ops   0001 (.add/change/del/get/dump_station + per-STA context)
#   wifi-data-tx   0001 (ndo_start_xmit -> dp TX, EAPOL msg1/msg3)
# Per-file 3-way merges; the two overlapping files are replaced by
# integration/assoc-resolved/ (Kbuild object union; cfg80211.c teardown-hook
# union).  wpa3-sae owns wiphy->mgmt_stypes (AP tx=0x3c2a) - ap-mgmt does not
# modify it.  The .ko is local evidence only and is never committed.
#
#   ./tools/build-association-module.sh
#   ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-association-module.sh
set -eu
LANE_DIR=$(cd "$(dirname "${0}")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
RESOLVED=$LANE_DIR/integration/assoc-resolved
WORK=/tmp/wifi-assoc-build
ART=/tmp/wifi-assoc-artifact
ARTWORK=$ART.work
ALLPATCH_TREE=${ALLPATCH_TREE:-}

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
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "assoc: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "assoc: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "assoc: cross gcc missing" >&2; exit 2; }
command -v git >/dev/null || { echo "assoc: git required" >&2; exit 2; }

LANES="wifi-wpa3-sae:0001-wpa3-sae wifi-ap-mgmt:0001-mgmt-tx-rx wifi-sta-ops:0001-sta-ops wifi-data-tx:0001-data-tx"
for l in $LANES; do
	d=$DOCS/${l%%:*}/patches/${l##*:}.patch
	[ -f "${d}" ] || { echo "assoc: missing lane patch ${d}" >&2; exit 2; }
done
for f in Kbuild hi5622_cfg80211.c; do
	[ -f "$RESOLVED/$f" ] || { echo "assoc: missing resolved $f" >&2; exit 2; }
done

if [ -z "$ALLPATCH_TREE" ]; then
	echo "== 0. build the FINAL all-patch tree (tools/build-all-patch-module.sh)"
	"$LANE_DIR/tools/build-all-patch-module.sh"
	ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ
fi
[ -d "$ALLPATCH_TREE" ] || { echo "assoc: all-patch tree missing: $ALLPATCH_TREE" >&2; exit 2; }

rm -rf "$WORK" "$ART" "$ARTWORK"
mkdir -p "$WORK/base" "$ARTWORK"
cp -a "$ALLPATCH_TREE/." "$WORK/base/"
( cd "$WORK/base" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd 2>/dev/null ) || true
echo "all-patch tree staged: $(ls "$WORK/base" | wc -l) files"

stage() { # $1 = lane, $2 = base dir, $3 = out dir
	lane=$1; base=$2; out=$3
	rm -rf "$out"; cp -a "$base" "$out"
	pname=${lane#wifi-}
	p=$(ls "$DOCS/$lane/patches/"0001-*.patch)
	if ! ( cd "$out" && patch -p1 --no-backup-if-mismatch < "$p" >/dev/null 2>&1 ); then
		echo "assoc: $lane patch no longer applies to its base" >&2; exit 1
	fi
}
P0=$WORK/base
cp -a "$P0" "$WORK/base_staops"
stage wifi-sta-ops "$WORK/base" "$WORK/base_staops"
echo "per-lane bases staged"

INT=$WORK/integ
cp -a "$P0" "$INT"
merge_lane() { # $1 lane, $2 base, $3 theirs
	lane=$1; base=$2; theirs=$3
	p=$(ls "$DOCS/$lane/patches/"0001-*.patch)
	for f in $(grep '^+++ ' "$p" | sed -e 's|^+++ ||' -e 's|^b/||' -e 's|[[:space:]].*||' | sort -u); do
		f=$(basename "$f")
		[ "$f" = "/dev/null" ] && continue
		b=$base/$f; cur=$INT/$f; t=$theirs/$f
		[ -f "$t" ] || { echo "assoc: $lane does not produce $f" >&2; exit 1; }
		if [ ! -f "$b" ]; then cp "$t" "$cur"; continue; fi
		git merge-file -p "$cur" "$b" "$t" > "$cur.merged" 2>/dev/null || true
		mv "$cur.merged" "$cur"
	done
}
T=$WORK/theirs
mkdir -p "$T"
stage wifi-wpa3-sae "$P0" "$T/wpa3";  merge_lane wifi-wpa3-sae "$P0" "$T/wpa3"
stage wifi-ap-mgmt "$P0" "$T/apmgmt"; merge_lane wifi-ap-mgmt "$P0" "$T/apmgmt"
stage wifi-sta-ops "$P0" "$T/staops"; merge_lane wifi-sta-ops "$P0" "$T/staops"
stage wifi-data-tx "$WORK/base_staops" "$T/datatx"; merge_lane wifi-data-tx "$WORK/base_staops" "$T/datatx"
for f in Kbuild hi5622_cfg80211.c; do cp "$RESOLVED/$f" "$INT/$f"; done
if grep -rln '^<<<<<<<' "$INT" >/dev/null 2>&1; then
	echo "assoc: conflict markers survived - refusing build" >&2; exit 1
fi
echo "association integration tree ready: $(ls "$INT" | wc -l) files"

export STAGING_DIR
( cd "$INT" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$INT" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$INT" modules > "$WORK/build.log" 2>&1 || {
	echo "assoc: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "assoc: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$INT/hi5622_wifi.ko
[ -f "$KO" ] || { echo "assoc: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ARTWORK/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ARTWORK/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "assoc: strip changed .text" >&2; exit 1; }
cp "$KO" "$ARTWORK/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ARTWORK/build.log"

PARMS="fw_path wifi_block_path allow_frames frame_gap_ms h2d_send_budget hcc_poll_ms hcc_cece_mode vap_start_first vap_start_mode vap_start_module ap_start_mode scan_timeout_ms scan_dwell_ms scan_chan_time_ms scan_req_batch scan_batch_gap_ms rd_ab_write cfg80211_auto band_force hcc_trace stop_ap_mode mgmt_diag txq_publish tx_complete_ms mac_tail_writes rx_enable beacon_static beacon_vap_start wpa_keys wpa_key_module sta_join ht_mode vht_cap band_vendor mgmt_tx mgmt_rx sta_ops eapol_rx sta_user_del data_tx data_tx_peer data_tx_protect data_tx_timeout_ms"
MI=$(modinfo -p "$ARTWORK/hi5622_wifi.ko" 2>/dev/null)
missing=""
for p in $PARMS; do
	if ! printf '%s\n' "$MI" | grep -q "^$p:"; then missing="$missing $p"; fi
done
[ -z "$missing" ] || { echo "assoc: missing params:$missing" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "56" ] || { echo "assoc: param count $NP != 56" >&2; exit 1; }
CAPS=$(grep -oE 'HI5622_FRAME_CAP_[A-Z_]+[[:space:]]+\(1u << [0-9]+\)' "$INT/hi5622_frames_api.h" | awk '{print $NF}' | sort | uniq -d)
[ -z "$CAPS" ] || { echo "assoc: duplicate cap bit(s): $CAPS" >&2; exit 1; }
SV=$INT/cfg80211-provider.symvers
[ "$(wc -l < "$SV")" = "12" ] || { echo "assoc: cfg80211 symvers != 12 lines" >&2; exit 1; }
for sym in cfg80211_bss_iter cfg80211_mgmt_tx_status_ext cfg80211_rx_mgmt_ext; do
	grep -q "${sym}" "$SV" || { echo "assoc: $sym missing from symvers" >&2; exit 1; }
	"$CROSS"nm -u "$ARTWORK/hi5622_wifi.ko" | grep -q " ${sym}\$" || {
		echo "assoc: $sym not imported by the .ko" >&2; exit 1; }
done
for s in "set_default_mgmt_key posted" "sta_ops: STA %pM added" \
	 "data-tx tx: posts=" "mgmt_tx: DMA frame buffer" "WAL cfg-event carrier" \
	 "DP rx_arm" "did not re-arm the scan context"; do
	strings -a "$ARTWORK/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "assoc: feature string missing: $s" >&2; exit 1; }
done

rm -rf "$ART"; mv "$ARTWORK" "$ART"
[ -s "$ART/hi5622_wifi.ko" ] || { echo "assoc: empty artifact" >&2; exit 1; }
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("$CROSS"strings "$KO" | grep -m1 '^vermagic=')
echo "== FINAL ASSOCIATION module (host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "params    : $NP (56)"
echo "$VERMAGIC"
echo "chain     : all-patch 5b587949 + wpa3-sae + ap-mgmt + sta-ops + data-tx"
