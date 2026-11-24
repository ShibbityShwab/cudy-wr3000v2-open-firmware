#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-package.sh - build-verify the hi5622-pciet-hcc OpenWrt package in the
# project tree and record the artifacts/sha256 evidence.  HOST-ONLY: it never
# touches the device, never builds an image and never flashes anything.
#
#   ./tools/build-package.sh [--owrt DIR] [--work DIR]
#
# Steps:
#   1. install the package directory into $OWRT/package/hi5622-pciet-hcc
#      (tools/install-into-tree.sh --force);
#   2. back up $OWRT/.config, temporarily select
#      CONFIG_PACKAGE_kmod-hi5622-pciet-hcc=m, and restore it on exit
#      (the package is opt-in; the .config is byte-identical afterwards);
#   3. make package/hi5622-pciet-hcc/clean && compile V=s (tee full log);
#   4. print + record: module sha256/size/vermagic/params, seed-helper
#      sha256, apk sha256, installed file list, full-log sha256.
#
# The driver source is the local snapshot in package/hi5622-pciet-hcc/src
# (no download, no vendor blob); see BUILD-NOTES.md/REPORT.md.
set -o pipefail

LANE=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
[ -n "$OWRT" ] || OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
[ -n "$WORK" ] || WORK=/home/shibbityshwab/wifi-openwrt-pkg-work
PKG_NAME=hi5622-pciet-hcc
KCONFIG_SYM=CONFIG_PACKAGE_kmod-hi5622-pciet-hcc
MODULE_NAME=hi5622_pciet_hcc

while [ $# -gt 0 ]; do
	case "$1" in
	--owrt) OWRT=$2; shift 2 ;;
	--work) WORK=$2; shift 2 ;;
	-h|--help)
		sed -n '3,25p' "$0" | sed 's/^# \{0,1\}//'
		exit 0 ;;
	*) echo "error: unknown argument '$1'" >&2; exit 2 ;;
	esac
done

[ -f "$OWRT/rules.mk" ] || { echo "error: $OWRT is not an OpenWrt tree" >&2; exit 2; }
mkdir -p "$WORK"
STAMP=$(LC_ALL=C date +%Y%m%dT%H%M%SZ)
LOG=$WORK/build-$STAMP.log
SUMMARY=$WORK/build-summary-$STAMP.txt

"$LANE/tools/install-into-tree.sh" --owrt "$OWRT" --force || {
	echo "error: installing the package into $OWRT failed" >&2
	exit 2
}

cp "$OWRT/.config" "$WORK/.config.pre-build"
restore_config() {
	cp "$WORK/.config.pre-build" "$OWRT/.config"
}
trap restore_config EXIT INT TERM

if ! grep -q "^$KCONFIG_SYM=" "$OWRT/.config"; then
	cat >> "$OWRT/.config" <<EOF

$KCONFIG_SYM=m
EOF
	echo "note: temporarily selected $KCONFIG_SYM=m (restored on exit)"
fi

cd "$OWRT"
make package/$PKG_NAME/clean >/dev/null 2>&1 || true
echo "== make package/$PKG_NAME/compile V=s  (log: $LOG)"
make package/$PKG_NAME/compile V=s 2>&1 | tee "$LOG"
rc=$?

# restore the caller's .config before reporting (the package is opt-in)
restore_config
trap - EXIT INT TERM

KO=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/$PKG_NAME/$MODULE_NAME.ko
SEED=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/$PKG_NAME/hi5622-seed
APK=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/packages/kmod-$PKG_NAME-*.apk 2>/dev/null | head -1 || true)
IPKG=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/$PKG_NAME/ipkg-arm_cortex-a9/kmod-$PKG_NAME

{
	echo "hi5622-pciet-hcc OpenWrt package build record"
	echo "date:        $STAMP"
	echo "openwrt:     $OWRT"
	echo "make rc:     $rc"
	echo "full log:    $LOG (sha256 $(sha256sum "$LOG" | cut -d' ' -f1))"
	echo
	echo "== target kernel"
	echo "linux:       $(cat "$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/include/config/kernel.release" 2>/dev/null)"
	echo "profile:     $(grep '^CONFIG_TARGET_PROFILE=' "$OWRT/.config" | cut -d= -f2-)"
	echo
	echo "== artifacts"
	if [ -f "$KO" ]; then
		echo "module:      $KO"
		echo "module size: $(stat -c %s "$KO") B"
		echo "module sha:  $(sha256sum "$KO" | cut -d' ' -f1)"
		modinfo "$KO" 2>/dev/null | grep -E '^(name|vermagic|license|alias|parm):' || true
	fi
	if [ -f "$SEED" ]; then
		echo "seed tool:   $SEED"
		echo "seed size:   $(stat -c %s "$SEED") B"
		echo "seed sha:    $(sha256sum "$SEED" | cut -d' ' -f1)"
		file "$SEED"
	fi
	if [ -n "$APK" ] && [ -f "$APK" ]; then
		echo "apk:         $APK"
		echo "apk size:    $(stat -c %s "$APK") B"
		echo "apk sha:     $(sha256sum "$APK" | cut -d' ' -f1)"
	fi
	if [ -d "$IPKG" ]; then
		echo
		echo "== package payload (installed tree)"
		( cd "$IPKG" && find . \( -type f -o -type l \) -printf '%M %s %p\n' | sort )
	fi
	echo
	echo "== .config"
	if diff -q "$WORK/.config.pre-build" "$OWRT/.config" >/dev/null 2>&1; then
		echo "restored:    byte-identical to the pre-build .config"
	else
		echo "WARNING:     .config differs from the pre-build copy"
	fi
} | tee "$SUMMARY"

echo
echo "== summary saved: $SUMMARY"
exit "$rc"
