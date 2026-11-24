#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - host-only build driver for the wifi-hcc-core lane (task-92).
#
# What it does:
#   1. compiles the portable core + the deterministic selftest with the host
#      compiler (-std=c99 -Wall -Wextra -Werror -pedantic) and runs it;
#   2. unless --no-kernel is given, assembles a scratch kernel-module tree
#      (hcc_core.c + hcc_core_kernel.c + kmod/hcc_core_check.c) and builds it
#      against the project's 6.18.60 kernel tree, proving the library is
#      compile-ready for the in-tree hcc service module.
#
# It never builds an OpenWrt image, never touches the router, never writes into
# the committed src/ tree, and commits nothing (all output goes to $WORK).
#
# Usage:
#   ./tools/build.sh              # host selftest + kernel compile check
#   ./tools/build.sh --no-kernel  # host selftest only
#   KDIR=... STAGING_DIR=... WORK=... ./tools/build.sh
set -eo pipefail

LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$WORK" ]; then WORK=/tmp/wifi-hcc-core-build; fi
if [ -z "$CC" ]; then CC=gcc; fi
DO_KERNEL=1
if [ "$1" = "--no-kernel" ]; then DO_KERNEL=0; fi

DEFAULT_KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
DEFAULT_STAGING=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
if [ -z "$KDIR" ]; then KDIR="$DEFAULT_KDIR"; fi
if [ -z "$STAGING_DIR" ]; then STAGING_DIR="$DEFAULT_STAGING"; fi
if [ -z "$CROSS_COMPILE" ]; then
	CROSS_COMPILE="$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-"
fi

rm -rf "$WORK"
mkdir -p "$WORK"

echo "== 1. host build (portable core + deterministic selftest)"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic \
	-I"$LANE_DIR/src" \
	"$LANE_DIR/src/hcc_core.c" "$LANE_DIR/tests/hcc_core_test.c" \
	-o "$WORK/hcc_core_test" 2>&1 | tee "$WORK/host-build.log"
"$WORK/hcc_core_test" | tee "$WORK/selftest.log"
grep -q 'RESULT: PASS' "$WORK/selftest.log"
echo "host selftest: PASS"

if [ "$DO_KERNEL" -ne 1 ]; then echo "kernel compile check skipped (--no-kernel)"; exit 0; fi

echo
echo "== 2. kernel compile check (6.18.60 module build)"
if [ ! -d "$KDIR" ]; then
	echo "kernel tree not found: $KDIR" >&2
	echo "host build PASSED; kernel compile check SKIPPED" >&2
	exit 0
fi
if [ ! -x "$CROSS_COMPILE"gcc ]; then
	echo "cross compiler not found: $CROSS_COMPILE"gcc >&2
	echo "host build PASSED; kernel compile check SKIPPED" >&2
	exit 0
fi

mkdir -p "$WORK/kmod"
cp "$LANE_DIR/src/hcc_core.c" "$LANE_DIR/src/hcc_core.h" \
   "$LANE_DIR/src/hcc_core_kernel.c" "$LANE_DIR/src/hcc_core_kernel.h" \
   "$LANE_DIR/src/kmod/hcc_core_check.c" "$LANE_DIR/src/kmod/Kbuild" \
   "$WORK/kmod/"

make -C "$KDIR" M="$WORK/kmod" ARCH=arm \
     CROSS_COMPILE="$CROSS_COMPILE" \
     STAGING_DIR="$STAGING_DIR" \
     2>&1 | tee "$WORK/kernel-build.log"

KO="$WORK/kmod/hcc_core_kmod.ko"
if [ ! -f "$KO" ]; then echo "BUILD FAILED: $KO missing" >&2; exit 1; fi
echo
echo "== kernel compile check result"
ls -l "$KO"
sha256sum "$KO"
modinfo "$KO" | grep -E '^(license|description|name|vermagic):' || true
echo "kernel compile check: PASS (device-free compile/link only; the module is not for the device path)"
