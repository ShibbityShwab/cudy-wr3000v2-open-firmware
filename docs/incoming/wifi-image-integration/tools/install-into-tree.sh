#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# install-into-tree.sh - copy the hi5622-wifi OpenWrt package and the
# clean-room driver source snapshot into an OpenWrt tree.
#
#   ./tools/install-into-tree.sh [--owrt DIR] [--driver-src DIR] [--force]
#
# The negotiated destination is $OWRT/package/hi5622-wifi/ :
#   Makefile            from ./package/hi5622-wifi/Makefile (tracked here)
#   README.md           from ./package/hi5622-wifi/README.md (tracked here)
#   src/                source snapshot of the driver tree (NOT tracked here)
#   src-sha256.txt      sha256 of every snapshot file (recorded evidence)
#
# The snapshot is filtered to source files only (Kbuild, Makefile, *.c, *.h,
# *.inc, *.symvers): a stray *.o/*.ko/*.cmd/modules.order in the driver
# directory can never leak into the package build.
#
# Nothing in the OpenWrt tree is modified except package/hi5622-wifi/; the
# package is only built when selected in .config (see ../config-delta.config).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PKGDIR=$HERE/../package/hi5622-wifi
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_DRIVER_SRC=$HERE/../../wifi-cfg80211/src

OWRT=$DEFAULT_OWRT
DRIVER_SRC=$DEFAULT_DRIVER_SRC
FORCE=0

die() { echo "install-into-tree: ERROR: $*" >&2; exit 1; }
usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt)      OWRT=$2; shift 2 ;;
		--driver-src) DRIVER_SRC=$2; shift 2 ;;
		--force)     FORCE=1; shift ;;
		-h|--help)   usage; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

[ -f "$OWRT/rules.mk" ] && [ -d "$OWRT/include" ] || die "not an OpenWrt tree: $OWRT"
[ -f "$OWRT/target/linux/hisilicon/luofu/config-6.18" ] || \
	die "luofu target config missing in $OWRT (wrong tree?)"
grep -q '^CONFIG_CFG80211=m$' "$OWRT/target/linux/hisilicon/luofu/config-6.18" || \
	die "target kernel config does not set CONFIG_CFG80211=m; the driver package would be unavailable"

[ -f "$PKGDIR/Makefile" ] || die "package Makefile missing: $PKGDIR/Makefile"
[ -f "$DRIVER_SRC/Kbuild" ] || die "driver Kbuild missing: $DRIVER_SRC/Kbuild"
[ -f "$DRIVER_SRC/Makefile" ] || die "driver Makefile missing: $DRIVER_SRC/Makefile"
grep -q '^obj-m := hi5622_wifi.o' "$DRIVER_SRC/Kbuild" || die "unexpected driver Kbuild (obj-m not found)"

# Snapshot predicate: source files only. A kbuild tree also contains
# hi5622_wifi.mod.c (a generated artifact that matches *.c) and a build-product
# Module.symvers; neither may go into the package.
snapshot_files() {
	find "$DRIVER_SRC" -maxdepth 1 -type f \
		\( -name 'Kbuild' -o -name 'Makefile' -o -name 'cfg80211-provider.symvers' \
		   -o \( \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) ! -name '*.mod.c' \) \)
}
SRC_COUNT=$(snapshot_files | wc -l)
[ "$SRC_COUNT" -ge 20 ] || die "driver source snapshot looks incomplete ($SRC_COUNT files) in $DRIVER_SRC"

DEST=$OWRT/package/hi5622-wifi
if [ -e "$DEST" ] && [ "$FORCE" -ne 1 ]; then
	die "$DEST already exists (use --force to replace)"
fi
rm -rf "$DEST"
mkdir -p "$DEST/src"

cp -p "$PKGDIR/Makefile" "$PKGDIR/README.md" "$DEST/"
while IFS= read -r srcfile; do
	cp -p "$srcfile" "$DEST/src/"
done < <(snapshot_files)

# Hard fail if any build product leaked into the snapshot. NOTE: the driver
# tree also contains a build-product Module.symvers; only the committed
# cfg80211-provider.symvers is source/evidence and must be copied.
if find "$DEST/src" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod' \
	-o -name '*.mod.c' -o -name 'modules.order' -o -name 'Module.symvers' -o -name 'Module.symvers.tmp' \) | grep -q .; then
	die "build artefacts leaked into $DEST/src (snapshot filter bug)"
fi

( cd "$DEST/src" && sha256sum $(find . -maxdepth 1 -type f ! -name 'src-sha256.txt' | sed 's|^./||' | sort) ) > "$DEST/src-sha256.txt"
DEST_FILES=$(find "$DEST/src" -maxdepth 1 -type f | wc -l)
DEST_MANIFEST_SHA=$(sha256sum "$DEST/src-sha256.txt" | cut -d' ' -f1)

echo "package installed : $DEST"
echo "driver source     : $DRIVER_SRC"
echo "snapshot files    : $DEST_FILES"
echo "snapshot manifest : $DEST/src-sha256.txt sha256 $DEST_MANIFEST_SHA"
echo "next: append ../config-delta.config to $OWRT/.config, run make defconfig, then make -jN"
