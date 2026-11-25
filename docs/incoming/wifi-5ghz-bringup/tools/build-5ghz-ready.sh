#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-5ghz-ready.sh - build the 5 GHz one-radio/two-band module
# (lane wifi-5ghz-bringup, shared task task-154).  HOST-ONLY: compiles against
# the project 6.18.60 kernel tree; no device access, no firmware write, no
# image, no flash.  The .ko is local evidence only and is never committed.
#
# Chain (all patches are in the repository):
#   1. FINAL ASSOCIATION tree (wifi-stability/tools/build-association-module.sh)
#      = all-patch 5b587949 + wpa3-sae + ap-mgmt + sta-ops + data-tx  [56 params]
#   2. wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
#      = the corrected device mgmt-TX carrier (the bench module under test)
#   3. this lane's patches/0001-band5g-assoc.patch  (the wifi-5ghz lane band5g
#      profile, rebased onto the association tree: param + tables + AP/H1 gates)
#   4. this lane's patches/0002-5ghz-primary-wiphy.patch  (the fix: one-radio/
#      two-band wiphy plan on the primary endpoint; secondary host view gets no
#      wiphy; band-aware AP-start/scan band bytes)
#
# Usage:
#   ./tools/build-5ghz-ready.sh
#   ASSOC_TREE=/tmp/wifi-assoc-build/integ ./tools/build-5ghz-ready.sh
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P1=$LANE/patches/0001-band5g-assoc.patch
P2=$LANE/patches/0002-5ghz-primary-wiphy.patch
PSIB=$DOCS/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
WORK=/tmp/wifi-5ghz-ready-build
ART=/tmp/wifi-5ghz-ready-artifact
ASSOC_TREE=${ASSOC_TREE:-}

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
[ -d "$KDIR" ] || { echo "5ghz-ready: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "5ghz-ready: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "5ghz-ready: cross gcc missing" >&2; exit 2; }
for f in "$P1" "$P2" "$PSIB"; do
	[ -f "$f" ] || { echo "5ghz-ready: missing patch $f" >&2; exit 2; }
done

if [ -z "$ASSOC_TREE" ]; then
	if [ -d /tmp/wifi-assoc-build/integ ]; then
		ASSOC_TREE=/tmp/wifi-assoc-build/integ
	else
		echo "== 0. build the FINAL ASSOCIATION tree (wifi-stability lane)"
		"$DOCS/wifi-stability/tools/build-association-module.sh"
		ASSOC_TREE=/tmp/wifi-assoc-build/integ
	fi
fi
[ -d "$ASSOC_TREE" ] || { echo "5ghz-ready: assoc tree missing: $ASSOC_TREE" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$ASSOC_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd build.log 2>/dev/null ) || true
echo "== 1. association tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. sibling mgmt-TX carrier patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PSIB" >/dev/null )
echo "== 3. band5g profile patch (0001)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P1" >/dev/null )
echo "== 4. one-radio/two-band wiphy patch (0002)"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P2" >/dev/null )
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "5ghz-ready: conflict markers survived - refusing build" >&2; exit 1
fi

# The portable helpers under src/ are host-tested; they must be byte-identical
# to the driver sources this build produces (refuse source drift).
for f in hi5622_band5g.c hi5622_band5g.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "5ghz-ready: src/$f drifted from the patched tree" >&2; exit 1; }
done
echo "== 5. src/ matches the patched driver sources"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "5ghz-ready: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "5ghz-ready: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1; fi
echo "== 6. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "5ghz-ready: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "5ghz-ready: strip changed .text" >&2; exit 1; }
echo "== 7. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- contract checks on the artifact -------------------------------------
PARMS="band5g cfg80211_auto band_force mgmt_tx mgmt_rx sta_ops data_tx"
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
for p in $PARMS; do
	printf '%s\n' "$MI" | grep -q "^$p:" || {
		echo "5ghz-ready: missing module param $p" >&2; exit 1; }
done
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "57" ] || { echo "5ghz-ready: param count $NP != 57" >&2; exit 1; }
for s in "ONE-RADIO/TWO-BAND profile active" \
	 "SECONDARY host view of the same Wi-Fi chip" \
	 "band5g=1 5GHz wiphy profile active" \
	 "scan batch mixes 2.4 GHz and 5 GHz" \
	 "set_default_mgmt_key posted" \
	 "data-tx tx: posts=" \
	 "cfg80211: mgmt RX delivered" \
	 "hi5622: mgmt_tx op reached"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "5ghz-ready: feature string missing: $s" >&2; exit 1; }
done
SV=$WORK/cfg80211-provider.symvers
[ "$(wc -l < "$SV")" = "12" ] || { echo "5ghz-ready: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 8. artifact contract PASS (57 params, feature strings, 12 symvers)"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
TEXT_SHA=$(sha256sum "$WORK/.t1" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
echo "== 5 GHz one-radio/two-band module (host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE B"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B"
echo ".text sha : $TEXT_SHA"
echo "$VERMAGIC"
echo "params    : $NP (57: 56 association + band5g)"
echo "chain     : assoc (5b587949 base) + sibling mgmt carrier + band5g + 5ghz-ready"
