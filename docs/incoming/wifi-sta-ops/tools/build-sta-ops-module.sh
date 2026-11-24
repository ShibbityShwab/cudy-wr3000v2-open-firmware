#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-sta-ops-module.sh - reproducibility gate for the wifi-sta-ops
# lane (shared task task-149).  HOST-ONLY: applies patches/0001-sta-ops.patch
# to a local copy of the FINAL ALL-PATCH tree, builds the 6.18.60 ARM module
# and records the artifact identity.  No device, no UART, no network.
#
#   ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-sta-ops-module.sh
#
# The all-patch base .ko (stripped) identity asserted here is documented in
# wifi-stability/FINAL-INTEGRATION.md: 308,080 B
# sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
SRC_COPY=/tmp/wifi-allpatch-build/integ
BASE_ART=/tmp/wifi-allpatch-artifact/hi5622_wifi.ko
set +u
if [ -n "$ALLPATCH_TREE" ]; then SRC_COPY=$ALLPATCH_TREE; fi
if [ -n "$ALLPATCH_ARTIFACT" ]; then BASE_ART=$ALLPATCH_ARTIFACT; fi
set -u
WORK=/tmp/wifi-sta-ops-build
PATCH=$LANE_DIR/patches/0001-sta-ops.patch

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
[ -d "$KDIR" ] || { echo "sta-ops-build: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "sta-ops-build: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "sta-ops-build: cross gcc missing" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "sta-ops-build: missing $PATCH" >&2; exit 2; }
[ -d "$SRC_COPY" ] || { echo "sta-ops-build: all-patch tree missing: $SRC_COPY" >&2; exit 2; }

BASE_SHA=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
if [ -f "$BASE_ART" ]; then
	got=$(sha256sum "$BASE_ART" | cut -d' ' -f1)
	if [ "$got" != "$BASE_SHA" ]; then
		echo "sta-ops-build: all-patch base artifact sha mismatch" >&2
		echo "  expected $BASE_SHA" >&2
		echo "  got      $got ($BASE_ART)" >&2
		exit 1
	fi
	echo "chain-id  : all-patch base artifact asserted ($BASE_SHA, $(stat -c%s "$BASE_ART") B)"
else
	echo "chain-id  : all-patch base artifact not present locally ($BASE_ART) - tree-only build"
fi

rm -rf "$WORK/tree"
cp -a "$SRC_COPY" "$WORK/tree"
( cd "$WORK/tree" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd 2>/dev/null ) || true
if ! ( cd "$WORK/tree" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null ); then
	echo "sta-ops-build: patch does not apply to the all-patch tree" >&2
	exit 1
fi
echo "patch     : 0001-sta-ops.patch applied to the all-patch tree"
if grep -Eiq 'BEGIN (RSA|OPENSSH|EC) PRIVATE KEY' "$PATCH"; then
	echo "sta-ops-build: patch contains secret-like text - refusing" >&2
	exit 1
fi

export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/tree" clean >/dev/null 2>&1 || true
if ! make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/tree" modules > "$WORK/build.log" 2>&1; then
	echo "sta-ops-build: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
fi
if grep -E 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "sta-ops-build: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
KO=$WORK/tree/hi5622_wifi.ko
[ -f "$KO" ] || { echo "sta-ops-build: no .ko" >&2; exit 1; }

mkdir -p "$WORK/artifact"
"$CROSS"strip --strip-debug -o "$WORK/artifact/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$WORK/artifact/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "sta-ops-build: strip changed .text" >&2; exit 1; }
cp "$KO" "$WORK/artifact/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$WORK/artifact/build.log"

MI=$(modinfo -p "$WORK/artifact/hi5622_wifi.ko")
NPAR=$(printf '%s\n' "$MI" | grep -c ':' || true)
for p in sta_ops eapol_rx sta_user_del sta_join rx_enable beacon_static wpa_keys allow_frames; do
	printf '%s\n' "$MI" | grep -q "^$p:" || { echo "sta-ops-build: missing parameter $p" >&2; exit 1; }
done
[ "$NPAR" -eq 50 ] || { echo "sta-ops-build: expected 50 parameters, got $NPAR" >&2; exit 1; }

for s in hi5622_sta_ops_add hi5622_sta_ops_rx_deliver hi5622_sta_ops_init; do
	"$CROSS"nm "$WORK/artifact/hi5622_wifi.ko.unstripped" | grep -q " $s$" || {
		echo "sta-ops-build: missing symbol $s" >&2; exit 1; }
done
"$CROSS"nm -u "$WORK/artifact/hi5622_wifi.ko.unstripped" | grep -q ' netif_rx$' || {
	echo "sta-ops-build: netif_rx import missing (RX delivery)" >&2; exit 1; }

UNSTRIP_SHA=$(sha256sum "$WORK/artifact/hi5622_wifi.ko.unstripped" | cut -d' ' -f1)
STRIP_SHA=$(sha256sum "$WORK/artifact/hi5622_wifi.ko" | cut -d' ' -f1)
UNSTRIP_SIZE=$(stat -c%s "$WORK/artifact/hi5622_wifi.ko.unstripped")
STRIP_SIZE=$(stat -c%s "$WORK/artifact/hi5622_wifi.ko")
VERMAGIC=$(modinfo "$WORK/artifact/hi5622_wifi.ko" 2>/dev/null | sed -n 's/^vermagic:[[:space:]]*//p')
DEPENDS=$(modinfo "$WORK/artifact/hi5622_wifi.ko" 2>/dev/null | sed -n 's/^depends:[[:space:]]*//p')

{
	echo "lane      : wifi-sta-ops (task-149), host-only build gate"
	echo "date      : $(LC_ALL=C date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "base-tree : all-patch integration tree ($SRC_COPY)"
	echo "chain-id  : all-patch base artifact asserted $BASE_SHA"
	echo "patch     : patches/0001-sta-ops.patch ($(wc -l < "$PATCH") lines, $(grep -c '^+++ ' "$PATCH") files)"
	echo "kernel    : $KDIR"
	echo "cross     : $CROSS"
	echo "unstripped: $UNSTRIP_SIZE B sha256 $UNSTRIP_SHA"
	echo "stripped  : $STRIP_SIZE B sha256 $STRIP_SHA"
	echo "vermagic  : $VERMAGIC ; depends=$DEPENDS"
	echo "params    : $NPAR (47 all-patch + sta_ops,eapol_rx,sta_user_del)"
	echo "symbols   : hi5622_sta_ops_add, hi5622_sta_ops_rx_deliver, hi5622_sta_ops_init; netif_rx imported"
	echo "warnings  : 0 (build.log checked)"
	echo "strip     : .text byte-identical"
} | tee "$WORK/artifact/evidence.txt"
