#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# install-into-tree.sh - copy the hi5622-wifi OpenWrt package into an OpenWrt
# tree, with either the ABI-validated PREBUILT module or a driver source
# snapshot.
#
#   ./tools/install-into-tree.sh --driver-ko FILE  [--label TEXT] [--owrt DIR] [--force]
#   ./tools/install-into-tree.sh --driver-src DIR  [--owrt DIR] [--force]
#
# Destination: $OWRT/package/hi5622-wifi/ (untracked in the tree)
#   Makefile                  from ./package/hi5622-wifi/Makefile (tracked here)
#   README.md                 from ./package/hi5622-wifi/README.md (tracked here)
#
# PREBUILT mode (--driver-ko): the local, ABI-validated bench artifact is copied
# to prebuilt/hi5622_wifi.ko and its identity (sha256, size, modinfo) recorded in
# prebuilt/prebuilt-sha256.txt. Nothing is compiled; the exact validated bytes
# are installed. No binary is stored in this repository - the artifact is
# copied from a local path into the (untracked) OpenWrt tree only.
#
# SOURCE mode (--driver-src): a filtered source snapshot (Kbuild, Makefile,
# *.c, *.h, *.inc, cfg80211-provider.symvers) is copied to src/ and the package
# compiles it with the kernel's own kbuild; src-sha256.txt records the snapshot.
# A stray *.o/*.ko/*.cmd/*.mod.c/Module.symvers from the driver directory can
# never leak into the package.
#
# Nothing else in the OpenWrt tree is modified; the package is only built when
# selected in .config (see ../config-delta.config).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
PKGDIR=$HERE/../package/hi5622-wifi
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_DRIVER_SRC=$HERE/../../wifi-cfg80211/src

OWRT=$DEFAULT_OWRT
DRIVER_SRC=$DEFAULT_DRIVER_SRC
DRIVER_KO=
LABEL=
FORCE=0
MODE=

die() { echo "install-into-tree: ERROR: $*" >&2; exit 1; }
usage() { sed -n '2,31p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt)       OWRT=$2; shift 2 ;;
		--driver-ko)  DRIVER_KO=$2; MODE=prebuilt; shift 2 ;;
		--driver-src) DRIVER_SRC=$2; MODE=source; shift 2 ;;
		--label)      LABEL=$2; shift 2 ;;
		--force)      FORCE=1; shift ;;
		-h|--help)    usage; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done
[ -n "$MODE" ] || MODE=source

[ -f "$OWRT/rules.mk" ] && [ -d "$OWRT/include" ] || die "not an OpenWrt tree: $OWRT"
[ -f "$OWRT/target/linux/hisilicon/luofu/config-6.18" ] || \
	die "luofu target config missing in $OWRT (wrong tree?)"
grep -q '^CONFIG_CFG80211=m$' "$OWRT/target/linux/hisilicon/luofu/config-6.18" || \
	die "target kernel config does not set CONFIG_CFG80211=m; the driver package would be unavailable"
[ -f "$PKGDIR/Makefile" ] || die "package Makefile missing: $PKGDIR/Makefile"

DEST=$OWRT/package/hi5622-wifi
if [ -e "$DEST" ] && [ "$FORCE" -ne 1 ]; then
	die "$DEST already exists (use --force to replace)"
fi
rm -rf "$DEST"
mkdir -p "$DEST"
cp -p "$PKGDIR/Makefile" "$PKGDIR/README.md" "$DEST/"

if [ "$MODE" = prebuilt ]; then
	# ---- prebuilt ABI-validated module ----------------------------------
	[ -n "$DRIVER_KO" ] || die "--driver-ko requires a file path"
	[ -f "$DRIVER_KO" ] || die "prebuilt module not found: $DRIVER_KO"
	KO_SIZE=$(stat -c %s "$DRIVER_KO")
	[ "$KO_SIZE" -ge 50000 ] || die "prebuilt module suspiciously small ($KO_SIZE B): $DRIVER_KO"
	if ! readelf -h "$DRIVER_KO" 2>/dev/null | grep -q 'Machine:.*ARM'; then
		die "prebuilt module is not an ARM ELF object: $DRIVER_KO"
	fi
	KO_VERMAGIC=$(modinfo -F vermagic "$DRIVER_KO" 2>/dev/null || true)
	KO_DEPENDS=$(modinfo -F depends "$DRIVER_KO" 2>/dev/null || true)
	KO_NAME=$(modinfo -F name "$DRIVER_KO" 2>/dev/null || true)
	case "$KO_VERMAGIC" in *6.18.60*) ;; *) die "prebuilt module vermagic '$KO_VERMAGIC' is not 6.18.60" ;; esac
	case "$KO_DEPENDS" in *cfg80211*) ;; *) die "prebuilt module does not depend on cfg80211 (depends='$KO_DEPENDS')" ;; esac
	[ "$KO_NAME" = "hi5622_wifi" ] || die "prebuilt module name is '$KO_NAME', expected hi5622_wifi"
	KO_SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
	mkdir -p "$DEST/prebuilt"
	cp -p "$DRIVER_KO" "$DEST/prebuilt/hi5622_wifi.ko"
	# The kernel-package symvers export hook expects kbuild manifests; a
	# prebuilt module exports no symbols, so empty/stub manifests are correct.
	: > "$DEST/prebuilt/Module.symvers"
	printf 'hi5622_wifi.o\n' > "$DEST/prebuilt/modules.order"
	{
		echo "# hi5622-wifi prebuilt module identity (staged by install-into-tree.sh)"
		echo "# source artifact : $DRIVER_KO"
		echo "# label           : ${LABEL:-unlabelled}"
		echo "# staged (host)   : $(date -u +%Y-%m-%dT%H:%M:%SZ)"
		echo "size_bytes=$KO_SIZE"
		echo "sha256=$KO_SHA"
		echo "modinfo_name=$KO_NAME"
		echo "modinfo_vermagic=$KO_VERMAGIC"
		echo "modinfo_depends=$KO_DEPENDS"
		echo "modinfo_license=$(modinfo -F license "$DRIVER_KO" 2>/dev/null || true)"
		modinfo -F parm "$DRIVER_KO" 2>/dev/null | sed 's/^/modinfo_param=/' || true
	} > "$DEST/prebuilt/prebuilt-sha256.txt"
	PREBUILT_SHA_MANIFEST=$(sha256sum "$DEST/prebuilt/prebuilt-sha256.txt" | cut -d' ' -f1)
	echo "package installed : $DEST"
	echo "mode              : PREBUILT (no compile; the validated bytes are installed)"
	echo "module artifact   : $DRIVER_KO"
	echo "module size/sha   : $KO_SIZE B / $KO_SHA"
	echo "module vermagic   : $KO_VERMAGIC"
	echo "module depends    : $KO_DEPENDS"
	echo "identity manifest : prebuilt/prebuilt-sha256.txt sha256 $PREBUILT_SHA_MANIFEST"
else
	# ---- compile from a source snapshot ---------------------------------
	[ -f "$DRIVER_SRC/Kbuild" ] || die "driver Kbuild missing: $DRIVER_SRC/Kbuild"
	[ -f "$DRIVER_SRC/Makefile" ] || die "driver Makefile missing: $DRIVER_SRC/Makefile"
	grep -q '^obj-m := hi5622_wifi.o' "$DRIVER_SRC/Kbuild" || die "unexpected driver Kbuild (obj-m not found)"
	# A kbuild tree also contains hi5622_wifi.mod.c (generated, matches *.c)
	# and a build-product Module.symvers; neither may go into the package.
	snapshot_files() {
		find "$DRIVER_SRC" -maxdepth 1 -type f \
			\( -name 'Kbuild' -o -name 'Makefile' -o -name 'cfg80211-provider.symvers' \
			   -o \( \( -name '*.c' -o -name '*.h' -o -name '*.inc' \) ! -name '*.mod.c' \) \)
	}
	SRC_COUNT=$(snapshot_files | wc -l)
	[ "$SRC_COUNT" -ge 20 ] || die "driver source snapshot looks incomplete ($SRC_COUNT files) in $DRIVER_SRC"
	mkdir -p "$DEST/src"
	while IFS= read -r srcfile; do
		cp -p "$srcfile" "$DEST/src/"
	done < <(snapshot_files)
	if find "$DEST/src" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.cmd' -o -name '*.mod' \
		-o -name '*.mod.c' -o -name 'modules.order' -o -name 'Module.symvers' -o -name 'Module.symvers.tmp' \) | grep -q .; then
		die "build artefacts leaked into $DEST/src (snapshot filter bug)"
	fi
	( cd "$DEST/src" && sha256sum $(find . -maxdepth 1 -type f ! -name 'src-sha256.txt' | sed 's|^./||' | sort) ) > "$DEST/src-sha256.txt"
	DEST_FILES=$(find "$DEST/src" -maxdepth 1 -type f | wc -l)
	DEST_MANIFEST_SHA=$(sha256sum "$DEST/src-sha256.txt" | cut -d' ' -f1)
	echo "package installed : $DEST"
	echo "mode              : SOURCE (compiled from the snapshot at package build)"
	echo "driver source     : $DRIVER_SRC"
	echo "snapshot files    : $DEST_FILES"
	echo "snapshot manifest : $DEST/src-sha256.txt sha256 $DEST_MANIFEST_SHA"
fi
echo "next: append ../config-delta.config to $OWRT/.config, run make defconfig, then make -jN"
