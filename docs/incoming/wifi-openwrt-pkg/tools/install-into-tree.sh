#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
#
# install-into-tree.sh - copy this lane's package into the project OpenWrt
# tree (host-only; never touches the device and never builds an image).
#
#   ./tools/install-into-tree.sh [--owrt DIR] [--force] [--remove]
#
# The build itself is ./tools/build-package.sh (or "make package/.../compile"
# by hand).  This script only manages the package directory in the tree:
# $OWRT/package/hi5622-pciet-hcc/.
#
# Default tree: /home/shibbityshwab/openwrt-lab-selectorfix-img
# (the lab tree the driver lanes have been compiling against).
set -eu

LANE=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
PKG=$LANE/package/hi5622-pciet-hcc
OWRT=""
FORCE=0
REMOVE=0

while [ $# -gt 0 ]; do
	case "$1" in
	--owrt)   OWRT=$2; shift 2 ;;
	--force)  FORCE=1; shift ;;
	--remove) REMOVE=1; shift ;;
	-h|--help)
		sed -n '3,16p' "$0" | sed 's/^# \{0,1\}//'
		exit 0 ;;
	*)
		echo "error: unknown argument '$1'" >&2
		exit 2 ;;
	esac
done

if [ -z "$OWRT" ]; then
	OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
fi

DEST=$OWRT/package/hi5622-pciet-hcc

if [ ! -f "$OWRT/rules.mk" ] || [ ! -d "$OWRT/include" ]; then
	echo "error: '$OWRT' does not look like an OpenWrt tree (rules.mk/include missing)" >&2
	exit 2
fi
if [ ! -d "$OWRT/target/linux/hisilicon/luofu" ]; then
	echo "error: '$OWRT' has no target/linux/hisilicon/luofu (the package is target-gated)" >&2
	exit 2
fi

if [ "$REMOVE" = 1 ]; then
	rm -rf "$DEST"
	echo "removed $DEST"
	exit 0
fi

if [ -e "$DEST" ] && [ "$FORCE" != 1 ]; then
	echo "error: $DEST already exists (use --force to replace it)" >&2
	exit 2
fi

rm -rf "$DEST"
mkdir -p "$OWRT/package"
cp -a "$PKG" "$DEST"

echo "installed: $DEST"
echo "next:      $LANE/tools/build-package.sh --owrt $OWRT"
echo "note:      the package is NOT selected in .config by this script;"
echo "           build-package.sh selects CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m"
echo "           temporarily and restores .config afterwards."
