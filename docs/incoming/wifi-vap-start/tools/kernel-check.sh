#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/kernel-check.sh - OPTIONAL kernel-context compile check for the
# wifi-vap-start lane (host-only: no image build, no device contact).
#
# Copies the COMMITTED wifi-scan-ap src tree to a scratch directory (never
# modifies another lane's files), drops this lane's vapstart_frames.{c,h} in,
# applies provider/vapstart-provider.patch, adds the new object and builds the
# module against the real 6.18.60 tree with CONFIG_WERROR.  The .ko is local
# evidence only and is NOT committed.
#
#   ./tools/kernel-check.sh
#   KDIR=... STAGING_DIR=... ./tools/kernel-check.sh
set -eu
LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOCS="$(cd "$LANE_DIR/.." && pwd)"
KO_SRC="${KO_SRC:-$DOCS/wifi-scan-ap/src}"
PATCH="$LANE_DIR/provider/vapstart-provider.patch"
KFALLBACK=/home/shibbityshwab/openwrt-lab-upstream-target

KDIR="${KDIR:-}"
STAGING_DIR="${STAGING_DIR:-}"
WORK="${WORK:-/tmp/wifi-vap-start-kernel}"

if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c="$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60"
		if [ -d "$c" ]; then
			KDIR="$c"
			STAGING_DIR="$root/staging_dir"
			break
		fi
	done
fi
CROSS_COMPILE="${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}"

[ -d "$KO_SRC" ] || { echo "kernel-check: missing reference module src: $KO_SRC" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "kernel-check: missing provider patch: $PATCH" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "kernel-check: kernel tree not found: $KDIR" >&2; exit 2; }
[ -x "${CROSS_COMPILE}gcc" ] || { echo "kernel-check: toolchain not found: ${CROSS_COMPILE}gcc" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$KO_SRC/." "$WORK/"
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/.*.cmd
cp "$LANE_DIR/src/vapstart_frames.h" "$LANE_DIR/src/vapstart_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
     2>&1 | tee "$WORK/build.log"

KO="$WORK/hi5622_wifi.ko"
[ -f "$KO" ] || { echo "kernel-check: BUILD FAILED: $KO missing" >&2; exit 1; }

echo
echo "== kernel-context compile result"
ls -l "$KO"
sha256sum "$KO"
"${CROSS_COMPILE}nm" "$KO" | grep -cE ' [Tt] vsf_' | sed 's/^/vsf_ symbols in module: /'
if grep -iE 'warning:|error:' "$WORK/build.log"; then
	echo "kernel-check: WARNINGS FOUND" >&2
	exit 1
fi
echo "kernel-check: OK (zero warnings; module-only build; no image, no device)"
