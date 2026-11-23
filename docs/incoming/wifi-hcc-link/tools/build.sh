#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# build.sh - host-only build driver for the wifi-hcc-link B3 extension.
#
#   ./tools/build.sh                apply 0001 (h2d injector) + 0002 (this
#                                   lane) to a copy of the B3 reference tree
#                                   and build the out-of-tree module
#   ./tools/build.sh --gen-patches  regenerate patches/ first, then build
#   ./tools/build.sh --apply-only   stop after applying both patches
#
# WHAT THIS DOES: single external-module compile against the already-built
# 6.18.60 tree (same toolchain/tree as ../wifi-h2d-inject/tools/build.sh).
# It never runs an OpenWrt world/image build, never touches the bench router,
# and commits nothing (work happens under $WORK_DIR).
#
# Environment overrides: KDIR, STAGING_DIR, CROSS_COMPILE, WORK_DIR.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
src=$here/../wifi-transport-port/b3
p0001=$here/../wifi-h2d-inject/patches/0001-hi5622-pciet-h2d-inject-b3.patch
p0002=$here/patches/0002-hi5622-pciet-hcc-link-b3.patch

kd=$(printenv KDIR || true)
[ -n "$kd" ] || kd=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
staging=$(printenv STAGING_DIR || true)
[ -n "$staging" ] || staging=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
xc=$(printenv CROSS_COMPILE || true)
[ -n "$xc" ] || xc=$staging/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
work=$(printenv WORK_DIR || true)
[ -n "$work" ] || work=/tmp/wifi-hcc-link-build

do_build=1
do_gen=0
for arg in "$@"; do
	case "$arg" in
	--gen-patches) do_gen=1 ;;
	--apply-only) do_build=0 ;;
	-h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	*) echo "unknown argument: $arg" >&2; exit 2 ;;
	esac
done

if [ "$do_gen" = 1 ]; then
	"$here/tools/gen-patches.sh"
fi
[ -f "$p0002" ] || { echo "ERROR: patch missing: $p0002 (run --gen-patches)" >&2; exit 1; }
[ -f "$p0001" ] || { echo "ERROR: prerequisite patch missing: $p0001" >&2; exit 1; }
[ -d "$src" ] || { echo "ERROR: reference tree missing: $src" >&2; exit 1; }

mkdir -p "$work"
bdir=$work/tree
rm -rf "$bdir"
cp -a "$src" "$bdir"
rm -f "$bdir"/*.o "$bdir"/*.ko "$bdir"/*.mod "$bdir"/*.mod.c \
      "$bdir"/modules.order "$bdir"/Module.symvers "$bdir"/.*.cmd
echo "== apply 0001 (wifi-h2d-inject prerequisite) + 0002 (this lane)"
patch -p1 --fuzz=0 --no-backup-if-mismatch -s -d "$bdir" < "$p0001"
patch -p1 --fuzz=0 --no-backup-if-mismatch -s -d "$bdir" < "$p0002"
echo "== integrated file list:"
ls -1 "$bdir"/pciet_hcc_link.* "$bdir"/pciet_h2d.* 2>/dev/null || true

[ "$do_build" = 1 ] || exit 0
if [ ! -d "$kd" ]; then
	echo "SKIP: kernel tree not found: $kd (set KDIR)" >&2
	exit 0
fi
if [ ! -x "$xc"gcc ]; then
	echo "SKIP: cross compiler not found: $xc"gcc >&2
	exit 0
fi
echo "== make -C \$KDIR M=\$bdir modules ARCH=arm"
STAGING_DIR="$staging" make -C "$kd" M="$bdir" ARCH=arm \
	CROSS_COMPILE="$xc" modules
ko=$(ls -1 "$bdir"/*.ko | head -1)
echo "== build artifact (NOT committed; evidence only)"
ls -l "$ko"
sha256sum "$ko"
strings "$ko" | grep -m1 '^vermagic=' || true
echo "== hcc_link surface present in the module:"
strings "$ko" | grep -E 'hcc_link_(status|plan|wake)' | sort -u || {
	echo "ERROR: hcc_link sysfs names missing from the module" >&2; exit 1; }
echo "== OK"
