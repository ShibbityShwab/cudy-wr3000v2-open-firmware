#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-all-patch-module.sh - FINAL ALL-PATCH module build (host-only).
# Chain: base 44c6cc12 -> polished d145a205 -> dp-tuning 175a4617 ->
# scan-tuning R4 -> stability hardening (b5dfb883 chain), then:
#   wifi-rx-static 0001 + wifi-beacon-static 0001 (incl. 72dabe8 guard fix)
#   + wifi-wpa-static 0001 + wifi-sta-static 0001.
# Overlapping files are merged per-file with git merge-file; the six known
# overlaps are replaced by integration/resolved/ (union semantics + WPA cap
# bits renumbered 12..14 to avoid the beacon bit-9 collision).
# The .ko is local evidence only (never committed).  HOST-ONLY.
#
#   ./tools/build-all-patch-module.sh
#   CHAIN_TREE=/tmp/wifi-stability-build/combined ./tools/build-all-patch-module.sh
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
RESOLVED=$LANE_DIR/integration/resolved
WORK=/tmp/wifi-allpatch-build
ART=/tmp/wifi-allpatch-artifact
ARTWORK=$ART.work
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
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "allpatch: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "allpatch: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "allpatch: cross gcc missing" >&2; exit 2; }
command -v git >/dev/null || { echo "allpatch: git required (3-way merge)" >&2; exit 2; }

LANES="wifi-rx-static:0001-rx-live-enable wifi-beacon-static:0001-beacon-static wifi-wpa-static:0001-wpa-static wifi-sta-static:0001-sta-static"
for l in $LANES; do
	d=$DOCS/${l%%:*}/patches/${l##*:}.patch
	[ -f "$d" ] || { echo "allpatch: missing lane patch $d" >&2; exit 2; }
done
for f in hi5622_frames_api.h hi5622_scan_logic.c hi5622_vapscan_provider.c Kbuild hi5622_cfg80211.c hi5622_cfg80211_ops.c; do
	[ -f "$RESOLVED/$f" ] || { echo "allpatch: missing resolved $f" >&2; exit 2; }
done

if [ -z "$CHAIN_TREE" ]; then
	echo "== 0. build the b5dfb883 chain (tools/build-hardened-module.sh)"
	"$LANE_DIR/tools/build-hardened-module.sh"
	CHAIN_TREE=/tmp/wifi-stability-build/combined
fi
[ -d "$CHAIN_TREE" ] || { echo "allpatch: chain tree missing: $CHAIN_TREE" >&2; exit 2; }

rm -rf "$WORK" "$ART" "$ARTWORK"
mkdir -p "$WORK/base" "$ARTWORK"
cp -a "$CHAIN_TREE/." "$WORK/base/"
( cd "$WORK/base" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd 2>/dev/null ) || true
echo "chain tree staged: $(ls "$WORK/base" | wc -l) files"

for l in $LANES; do
	lane=${l%%:*}
	theirs=$WORK/theirs_$lane
	cp -a "$WORK/base" "$theirs"
	if ! ( cd "$theirs" && patch -p1 --no-backup-if-mismatch < "$DOCS/$lane/patches/${l##*:}.patch" >/dev/null 2>&1 ); then
		echo "allpatch: $lane patch no longer applies (upstream drift?)" >&2; exit 1
	fi
done
echo "all lane patches applied individually"

WORK_INT=$WORK/integ
cp -a "$WORK/base" "$WORK_INT"
for l in $LANES; do
	lane=${l%%:*}
	patchfile=$DOCS/$lane/patches/${l##*:}.patch
	theirs=$WORK/theirs_$lane
	for f in $(grep '^+++ ' "$patchfile" | sed -e 's|^+++ ||' -e 's|^[^/]*/||' -e 's|[[:space:]].*||' | sort -u); do
		f=$(basename "$f")
		[ "$f" = "/dev/null" ] && continue
		b=$WORK/base/$f; cur=$WORK_INT/$f; th=$theirs/$f
		[ -f "$th" ] || { echo "allpatch: $lane does not produce $f" >&2; exit 1; }
		if [ ! -f "$b" ]; then cp "$th" "$cur"; continue; fi
		git merge-file -p "$cur" "$b" "$th" > "$cur.merged" 2>/dev/null || true
		mv "$cur.merged" "$cur"
	done
done
for f in hi5622_frames_api.h hi5622_scan_logic.c hi5622_vapscan_provider.c Kbuild hi5622_cfg80211.c hi5622_cfg80211_ops.c; do
	cp "$RESOLVED/$f" "$WORK_INT/$f"
done
if grep -rln '^<<<<<<<' "$WORK_INT" >/dev/null 2>&1; then
	echo "allpatch: conflict markers survived - refusing build" >&2; exit 1
fi
echo "integration tree ready: $(ls "$WORK_INT" | wc -l) files"

export STAGING_DIR
( cd "$WORK_INT" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK_INT" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK_INT" modules > "$WORK/build.log" 2>&1 || {
	echo "allpatch: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "allpatch: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK_INT/hi5622_wifi.ko
[ -f "$KO" ] || { echo "allpatch: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ARTWORK/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ARTWORK/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "allpatch: strip changed .text" >&2; exit 1; }
cp "$KO" "$ARTWORK/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ARTWORK/build.log"

PARMS="fw_path wifi_block_path allow_frames frame_gap_ms h2d_send_budget hcc_poll_ms hcc_cece_mode vap_start_first vap_start_mode vap_start_module ap_start_mode scan_timeout_ms scan_dwell_ms scan_chan_time_ms scan_req_batch scan_batch_gap_ms rd_ab_write cfg80211_auto band_force hcc_trace stop_ap_mode mgmt_diag txq_publish tx_complete_ms mac_tail_writes rx_enable beacon_static beacon_vap_start wpa_keys wpa_key_module sta_join ht_mode vht_cap band_vendor"
MI=$(modinfo -p "$ARTWORK/hi5622_wifi.ko" 2>/dev/null)
missing=""
for p in $PARMS; do
	if ! printf '%s\n' "$MI" | grep -q "^$p:"; then missing="$missing $p"; fi
done
[ -z "$missing" ] || { echo "allpatch: missing params:$missing" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
CAPS=$(grep -oE 'HI5622_FRAME_CAP_[A-Z_]+[[:space:]]+\(1u << [0-9]+\)' "$WORK_INT/hi5622_frames_api.h" | awk '{print $NF}' | sort | uniq -d)
[ -z "$CAPS" ] || { echo "allpatch: duplicate cap bit(s): $CAPS" >&2; exit 1; }
SV=$WORK_INT/cfg80211-provider.symvers
[ "$(wc -l < "$SV")" = "10" ] || { echo "allpatch: cfg80211 symvers != 10" >&2; exit 1; }
"$CROSS"nm -u "$ARTWORK/hi5622_wifi.ko" | grep -q ' cfg80211_bss_iter$' || {
	echo "allpatch: cfg80211_bss_iter missing" >&2; exit 1; }
for s in "H1 band-config subtype1 sent" "did not re-arm the scan context" \
	 "re-posting the AP config against the live firmware AP" \
	 "DP rx_arm" "add_key" "beacon_static" "sta_join"; do
	strings -a "$ARTWORK/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "allpatch: expected feature string missing: $s" >&2; exit 1; }
done

rm -rf "$ART"; mv "$ARTWORK" "$ART"
[ -s "$ART/hi5622_wifi.ko" ] || { echo "allpatch: empty artifact" >&2; exit 1; }
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)
VERMAGIC=$("$CROSS"strings "$KO" | grep -m1 '^vermagic=')
echo "== ALL-PATCH module (final integration, host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "params    : $NP"
echo "$VERMAGIC"
echo "chain     : b5dfb883 chain + rx-static + beacon-static(+72dabe8) + wpa-static + sta-static"
