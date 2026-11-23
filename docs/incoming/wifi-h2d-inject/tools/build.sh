#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# build.sh - host-only build/test driver for the Hi5622V100 H2D injector.
#
#   ./tools/build.sh                  host selftest + out-of-tree module build
#   ./tools/build.sh --selftest-only  host selftest only (no kernel tree needed)
#   ./tools/build.sh --module-only    apply patch + build the module
#   ./tools/build.sh --tree b3        use the B3-instrument reference tree
#                                     (../wifi-transport-port/b3 -> hi5622_pciet_b3.ko)
#   ./tools/build.sh --tree base      reference transport tree (default)
#
# Environment overrides: KDIR, STAGING_DIR, CROSS_COMPILE, WORK_DIR, CC.
#
# WHAT THIS DOES: runs the userspace frame-model selftest against the SAME
# header the module compiles, applies the committed patch to a *copy* of the
# reference transport tree, and builds that copy as one out-of-tree kernel
# module against the already-built 6.18.60 tree.
#
# HOST-ONLY / NO IMAGE BUILD: this is a single external-module compile and a
# host userspace test.  It never runs an OpenWrt world/image build, never
# touches the bench router and commits nothing (work happens under $WORK_DIR).
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

tree=base
do_selftest=1
do_module=1
for arg in "$@"; do
	case "$arg" in
	--tree=b3|--tree|b3)
		tree=b3
		;;
	--tree=base|base)
		tree=base
		;;
	--selftest-only)
		do_module=0
		;;
	--module-only)
		do_selftest=0
		;;
	-h|--help)
		sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		echo "unknown argument: $arg" >&2
		exit 2
		;;
	esac
done

kd=$(printenv KDIR || true)
[ -n "$kd" ] || kd=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
staging=$(printenv STAGING_DIR || true)
[ -n "$staging" ] || staging=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
xc=$(printenv CROSS_COMPILE || true)
[ -n "$xc" ] || xc=$staging/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
hostcc=$(printenv CC || true)
[ -n "$hostcc" ] || hostcc=cc
work=$(printenv WORK_DIR || true)
[ -n "$work" ] || work=/tmp/wifi-h2d-inject-$tree

if [ "$tree" = b3 ]; then
	src=$here/../wifi-transport-port/b3
	pfile=$here/patches/0001-hi5622-pciet-h2d-inject-b3.patch
	ko=hi5622_pciet_b3.ko
else
	src=$here/../wifi-transport-port/build
	pfile=$here/patches/0001-hi5622-pciet-h2d-inject-base.patch
	ko=hi5622_pciet.ko
fi

mkdir -p "$work"
echo "== wifi-h2d-inject build driver: tree=$tree work=$work"

if [ "$do_selftest" = 1 ]; then
	echo "== [1/2] userspace selftest (shared frame model, no device)"
	"$hostcc" -O2 -Wall -Wextra -Werror -I"$here/files" \
		-o "$work/h2d_selftest" "$here/files/h2d_selftest.c"
	"$work/h2d_selftest"
fi

if [ "$do_module" = 1 ]; then
	echo "== [2/2] out-of-tree module build (module only, no image build)"
	if [ ! -d "$kd" ]; then
		echo "SKIP: kernel tree not found: $kd (set KDIR)" >&2
		exit 0
	fi
	if [ ! -x "$xc"gcc ]; then
		echo "SKIP: cross compiler not found: $xc"gcc >&2
		exit 0
	fi
	if [ ! -f "$pfile" ]; then
		echo "ERROR: patch not found: $pfile (run tools/gen-patches.sh)" >&2
		exit 1
	fi
	builddir=$work/tree
	rm -rf "$builddir"
	cp -a "$src" "$builddir"
	echo "-- applying $(basename "$pfile") to a copy of the reference tree"
	patch -p1 --fuzz=0 --no-backup-if-mismatch -d "$builddir" < "$pfile"
	echo "-- make -C \$KDIR M=\$builddir modules ARCH=arm"
	STAGING_DIR="$staging" make -C "$kd" M="$builddir" ARCH=arm \
		CROSS_COMPILE="$xc" modules
	echo "-- build artifacts (NOT committed; evidence only)"
	ls -l "$builddir/$ko"
	sha256sum "$builddir/$ko"
	strings "$builddir/$ko" | grep -m1 '^vermagic=' || true
fi

echo "== OK"
