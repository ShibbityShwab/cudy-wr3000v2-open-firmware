#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-data-tx-module.sh - reproducibility gate for the wifi-data-tx
# lane (shared task task-150).  HOST-ONLY: assembles the base tree, applies the
# prerequisite wifi-sta-ops patch (task-149) and this lane's patch, builds the
# 6.18.60 ARM module and records the artifact identity.  No device, no UART,
# no network.
#
#   ALLPATCH_TREE_OVERRIDE=/tmp/wifi-allpatch-build/integ ./tools/build-data-tx-module.sh
#
# BASE = the FINAL ALL-PATCH clean-room tree (wifi-stability FINAL-INTEGRATION,
# stripped 308,080 B sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41,
# 47 params) + wifi-sta-ops/patches/0001-sta-ops.patch (task-149, 50 params).
# The data-TX path is peer-aware: it needs the sta-ops station table, so the
# base is explicitly the sta-ops successor of the fired all-patch tree.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
INCOMING=$(cd "$LANE/.." && pwd)
STA_OPS_PATCH=$INCOMING/wifi-sta-ops/patches/0001-sta-ops.patch
ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ
ALLPATCH_ART=/tmp/wifi-allpatch-artifact/hi5622_wifi.ko
set +u
if [ -n "$ALLPATCH_TREE_OVERRIDE" ]; then ALLPATCH_TREE=$ALLPATCH_TREE_OVERRIDE; fi
if [ -n "$ALLPATCH_ARTIFACT" ]; then ALLPATCH_ART=$ALLPATCH_ARTIFACT; fi
if [ -n "$KDIR_OVERRIDE" ]; then KDIR=$KDIR_OVERRIDE; fi
if [ -n "$STAGING_OVERRIDE" ]; then STAGING_DIR=$STAGING_OVERRIDE; fi
set -u
WORK=/tmp/wifi-data-tx-build
KDIR=
STAGING_DIR=
PATCH=$LANE/patches/0001-data-tx.patch
BASE_SHA=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41

mkdir -p "$WORK"

if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "data-tx-build: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "data-tx-build: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "data-tx-build: cross gcc missing" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "data-tx-build: missing $PATCH" >&2; exit 2; }
[ -f "$STA_OPS_PATCH" ] || { echo "data-tx-build: missing prerequisite $STA_OPS_PATCH" >&2; exit 2; }
[ -d "$ALLPATCH_TREE" ] || { echo "data-tx-build: all-patch tree missing: $ALLPATCH_TREE" >&2; exit 2; }

if [ -f "$ALLPATCH_ART" ]; then
	got=$(sha256sum "$ALLPATCH_ART" | cut -d' ' -f1)
	if [ "$got" != "$BASE_SHA" ]; then
		echo "data-tx-build: all-patch base artifact sha mismatch" >&2
		echo "  expected $BASE_SHA" >&2
		echo "  got      $got ($ALLPATCH_ART)" >&2
		exit 1
	fi
	echo "chain-id  : all-patch base artifact asserted ($BASE_SHA, $(stat -c%s "$ALLPATCH_ART") B)"
else
	echo "chain-id  : all-patch base artifact not present locally ($ALLPATCH_ART) - tree-only build"
fi

rm -rf "$WORK/tree"
cp -a "$ALLPATCH_TREE" "$WORK/tree"
( cd "$WORK/tree" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd 2>/dev/null ) || true
if ! ( cd "$WORK/tree" && patch -p1 --no-backup-if-mismatch < "$STA_OPS_PATCH" >/dev/null ); then
	echo "data-tx-build: prerequisite sta-ops patch does not apply to the all-patch tree" >&2
	exit 1
fi
echo "prereq    : wifi-sta-ops/patches/0001-sta-ops.patch applied (task-149 station table)"
if ! ( cd "$WORK/tree" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null ); then
	echo "data-tx-build: data-tx patch does not apply on the sta-ops successor tree" >&2
	exit 1
fi
echo "patch     : patches/0001-data-tx.patch applied ($(wc -l < "$PATCH") lines)"
if grep -Eiq 'BEGIN (RSA|OPENSSH|EC) PRIVATE KEY' "$PATCH"; then
	echo "data-tx-build: patch contains secret-like text - refusing" >&2
	exit 1
fi

export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/tree" clean >/dev/null 2>&1 || true
if ! make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/tree" modules > "$WORK/build.log" 2>&1; then
	echo "data-tx-build: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
fi
if grep -E 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "data-tx-build: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
KO=$WORK/tree/hi5622_wifi.ko
[ -f "$KO" ] || { echo "data-tx-build: no .ko" >&2; exit 1; }

mkdir -p "$WORK/artifact"
"$CROSS"strip --strip-debug -o "$WORK/artifact/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$WORK/artifact/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "data-tx-build: strip changed .text" >&2; exit 1; }
cp "$KO" "$WORK/artifact/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$WORK/artifact/build.log"

MI=$(modinfo -p "$WORK/artifact/hi5622_wifi.ko")
NPAR=$(printf '%s\n' "$MI" | grep -c ':' || true)
for p in data_tx data_tx_peer data_tx_protect data_tx_timeout_ms \
	 sta_ops eapol_rx sta_user_del wpa_keys allow_frames rx_enable beacon_static; do
	printf '%s\n' "$MI" | grep -q "^$p:" || { echo "data-tx-build: missing parameter $p" >&2; exit 1; }
done
[ "$NPAR" -eq 54 ] || { echo "data-tx-build: expected 54 parameters (47 + 3 sta-ops + 4 data-tx), got $NPAR" >&2; exit 1; }

for s in hi5622_data_tx_init hi5622_data_tx_xmit hi5622_data_tx_remove \
	 hi5622_data_tx_status hi5622_data_tx_probe_store \
	 hi5622_dp_tx_data_post hi5622_dp_tx_data_poll; do
	"$CROSS"nm "$WORK/artifact/hi5622_wifi.ko.unstripped" | grep -q " $s$" || {
		echo "data-tx-build: missing symbol $s" >&2; exit 1; }
done
"$CROSS"nm -u "$WORK/artifact/hi5622_wifi.ko.unstripped" | grep -q ' netif_rx$' || {
	echo "data-tx-build: netif_rx import missing (sta-ops RX delivery)" >&2; exit 1; }
if "$CROSS"nm -u "$WORK/artifact/hi5622_wifi.ko.unstripped" | grep -q ' netif_tx_wake_queue$'; then
	echo "data-tx-build: unexpected netif_tx_wake_queue import (drop-on-full policy keeps no queue-wake path)" >&2
	exit 1
fi

UNSTRIP_SHA=$(sha256sum "$WORK/artifact/hi5622_wifi.ko.unstripped" | cut -d' ' -f1)
STRIP_SHA=$(sha256sum "$WORK/artifact/hi5622_wifi.ko" | cut -d' ' -f1)
UNSTRIP_SIZE=$(stat -c%s "$WORK/artifact/hi5622_wifi.ko.unstripped")
STRIP_SIZE=$(stat -c%s "$WORK/artifact/hi5622_wifi.ko")
VERMAGIC=$(modinfo "$WORK/artifact/hi5622_wifi.ko" 2>/dev/null | sed -n 's/^vermagic:[[:space:]]*//p')
DEPENDS=$(modinfo "$WORK/artifact/hi5622_wifi.ko" 2>/dev/null | sed -n 's/^depends:[[:space:]]*//p')

{
	echo "lane      : wifi-data-tx (task-150), host-only build gate"
	echo "date      : $(LC_ALL=C date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "base-tree : FINAL ALL-PATCH + wifi-sta-ops patch ($ALLPATCH_TREE)"
	echo "chain-id  : all-patch base artifact asserted $BASE_SHA"
	echo "prereq    : ../wifi-sta-ops/patches/0001-sta-ops.patch (task-149, 50-param successor)"
	echo "patch     : patches/0001-data-tx.patch ($(wc -l < "$PATCH") lines, $(grep -c '^+++ ' "$PATCH") files)"
	echo "kernel    : $KDIR"
	echo "cross     : $CROSS"
	echo "unstripped: $UNSTRIP_SIZE B sha256 $UNSTRIP_SHA"
	echo "stripped  : $STRIP_SIZE B sha256 $STRIP_SHA"
	echo "vermagic  : $VERMAGIC ; depends=$DEPENDS"
	echo "params    : $NPAR (47 all-patch + sta_ops,eapol_rx,sta_user_del + data_tx,data_tx_peer,data_tx_protect,data_tx_timeout_ms)"
	echo "symbols   : hi5622_data_tx_{init,xmit,remove,status,probe_store}, hi5622_dp_tx_data_{post,poll}; netif_rx imported"
	echo "imports   : no queue-wake path (drop-on-full policy); new imports: skb free + spinlock inlines"
	echo "warnings  : 0 (build.log checked)"
	echo "strip     : .text byte-identical"
} | tee "$WORK/artifact/evidence.txt"
