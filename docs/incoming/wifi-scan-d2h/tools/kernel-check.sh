#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/kernel-check.sh - OPTIONAL kernel-context compile check (host-only,
# module-only build, no image, no device contact).
#
# Proves the scan D2H decoder + its wifi-scan-ap integration compile inside the
# real 6.18.60 out-of-tree module with zero warnings.  It copies the COMMITTED
# wifi-scan-ap src tree to a scratch directory (never modifies another lane
# files), drops in src/hi5622_scan_d2h.c/.h and applies
# patches/0001-hi5622-scan-d2h-decoder.patch.  The .ko stays in /tmp (local
# evidence only, never committed).
#
#   ./tools/kernel-check.sh
#   HI5622_KERNEL_ROOT=... KDIR=... STAGING_DIR=... ./tools/kernel-check.sh
set -eu
LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOCS="$(cd "$LANE_DIR/.." && pwd)"
KO_SRC="$DOCS/wifi-scan-ap/src"
KERNEL_ROOT="/home/shibbityshwab/openwrt-lab-selectorfix-img"
if [ -n "$ENV_KERNEL_ROOT" ]; then KERNEL_ROOT="$ENV_KERNEL_ROOT"; fi
KDIR="$KERNEL_ROOT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60"
STAGING_DIR="$KERNEL_ROOT/staging_dir"
CROSS_COMPILE="$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-"
WORK="/tmp/wifi-scan-d2h-kernel"

[ -d "$KO_SRC" ] || { echo "kernel-check: missing reference module src: $KO_SRC" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "kernel-check: kernel tree not found: $KDIR" >&2; exit 2; }
[ -x "$CROSS_COMPILE"gcc ] || { echo "kernel-check: toolchain not found: $CROSS_COMPILE"gcc >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$KO_SRC/." "$WORK/"
cp "$LANE_DIR/src/hi5622_scan_d2h.h" "$LANE_DIR/src/hi5622_scan_d2h.c" "$WORK/"
# clean any committed build artifacts so the check builds from source
( cd "$WORK" && rm -f *.o .*.cmd *.ko *.mod *.mod.c .*.d *.order Module.symvers 2>/dev/null || true )

( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$LANE_DIR/patches/0001-hi5622-scan-d2h-decoder.patch" )

export STAGING_DIR
make -C "$KDIR" M="$WORK" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules 2>&1 | tee "$WORK/build.log"

KO="$WORK/hi5622_wifi.ko"
[ -f "$KO" ] || { echo "kernel-check: BUILD FAILED: $KO missing" >&2; exit 1; }

echo
echo "== kernel-context compile result"
ls -l "$KO"
sha256sum "$KO"
"$CROSS_COMPILE"nm "$KO" | grep -cE " [Tt] hi5622_scan_d2h" | sed "s/^/scan_d2h symbols in module: /"
if grep -iE "warning:|error:" "$WORK/build.log"; then
	echo "kernel-check: WARNINGS FOUND" >&2
	exit 1
fi
echo "kernel-check: OK (zero warnings; module-only build; no image, no device)"
