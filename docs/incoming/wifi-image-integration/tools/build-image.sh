#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-image.sh - build the SELF-CONTAINED cfg80211/Wi-Fi BENCH image: iw +
# wireless-regdb + hostapd-mini (+ ucode) + the wifi-scripts ucode files + the
# target-tree cfg80211.ko and the clean-room hi5622_wifi.ko kmod. Records the
# evidence and restores the shared tree afterwards.
#
#   ./tools/build-image.sh [--owrt DIR] [--driver-ko FILE|--driver-src DIR]
#                          [--label TEXT] [--jobs N]
#                          [--keep-config] [--keep-installed] [--skip-verify]
#
# DEFAULT DRIVER: /tmp/wifi-ap-dp-combined-artifact/hi5622_wifi.ko - the
# combined dp-air + AP-start v7 module (242,572 B sha 44c6cc12..., documented in
# docs/incoming/wifi-ap-start/combined/README.md) - packaged as the PREBUILT
# module; v7-only AP-start (206,708 B sha fd8e1439...) or a --driver-src snapshot
# are also supported.
# The kernel slice is expected to stay UNCHANGED (13e81214...); this is a
# rootfs-only rebuild - verify-image.sh asserts the kernel hash.
#
# SERIALIZATION: one world build at a time in a shared OpenWrt tree. This
# script refuses to start if a make process is running in $OWRT, and holds
# an exclusive flock on /tmp/cudy-openwrt-world-build.lock for its duration.
# It restores $OWRT/.config byte-identically and restores the rootfs staging
# tree (build_dir/target-*/root-hisilicon) so a later build by another lane
# cannot inherit this bench package set.
#
# The image artefact is left in $OWRT/bin/targets/hisilicon/luofu/ as
# evidence; it no longer matches the restored .config (re-run this script or
# tools/install-into-tree.sh + config-delta.config to reproduce it).
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_DRIVER_KO=/tmp/wifi-ap-dp-combined-artifact/hi5622_wifi.ko
OWRT=$DEFAULT_OWRT
DRIVER_SRC=$HERE/../../wifi-cfg80211/src
DRIVER_KO=
LABEL="combined dp-air + AP-start v7 module (ap-start task-121, 242,572 B sha 44c6cc12; dp-air derived from the fired module ca32aec4, v7 AP path hardware-validated 2026-11-27)"
JOBS=$(nproc)
KEEP_CONFIG=0
KEEP_INSTALLED=0
SKIP_VERIFY=0

die() { echo "build-image: ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=$2; shift 2 ;;
		--driver-src) DRIVER_SRC=$2; DRIVER_KO=source; shift 2 ;;
		--driver-ko) DRIVER_KO=$2; shift 2 ;;
		--label) LABEL=$2; shift 2 ;;
		--jobs) JOBS=$2; shift 2 ;;
		--keep-config) KEEP_CONFIG=1; shift ;;
		--keep-installed) KEEP_INSTALLED=1; shift ;;
		--skip-verify) SKIP_VERIFY=1; shift ;;
		-h|--help) sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

[ -f "$OWRT/rules.mk" ] && [ -f "$OWRT/.config" ] || die "not a configured OpenWrt tree: $OWRT"

# Driver mode: default to the local ABI-validated prebuilt artifact when present.
if [ -z "$DRIVER_KO" ]; then
	if [ -f "$DEFAULT_DRIVER_KO" ]; then DRIVER_KO=$DEFAULT_DRIVER_KO; else DRIVER_KO=source; fi
fi
if [ "$DRIVER_KO" = source ]; then DRIVER_MODE=source; else
	DRIVER_MODE=prebuilt
	[ -f "$DRIVER_KO" ] || die "prebuilt driver not found: $DRIVER_KO (use --driver-src for source mode)"
fi

WORK=$LANE/build-evidence/work
mkdir -p "$WORK" "$LANE/build-evidence"
TARGET_ROOT=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon
KCONFIG=target/linux/hisilicon/luofu/config-6.18
KERNEL_CFG=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/.config
LOCKFILE=/tmp/cudy-openwrt-world-build.lock

echo "== wifi-image-integration: FINAL self-contained bench image build =="
echo "tree       : $OWRT"
echo "driver     : $DRIVER_MODE"
echo "driver path: $DRIVER_KO"
echo "jobs       : $JOBS"
echo "work       : $WORK"

# ---- serialization -------------------------------------------------------
if pgrep -x make >/dev/null 2>&1; then
	for pid in $(pgrep -x make); do
		cwd=$(readlink "/proc/$pid/cwd" 2>/dev/null || true)
		case "$cwd" in
			"$OWRT"*) die "another make is running in the shared tree (pid $pid, cwd $cwd); wait for it and retry" ;;
		esac
	done
fi
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq build-image; then
	die "a make command line references $OWRT; another lane is building - serialize and retry"
fi
exec 9>"$LOCKFILE"
if ! flock -n 9; then
	die "another world build holds $LOCKFILE (serialize world builds)"
fi
echo "build lock : $LOCKFILE held by pid $$"

TREE_HEAD=$(git -C "$OWRT" rev-parse --short HEAD 2>/dev/null || echo unknown)
KCFG_SHA_PRE=$(sha256sum "$OWRT/$KCONFIG" | cut -d' ' -f1)
if [ -f "$KERNEL_CFG" ]; then KERNEL_CFG_SHA_PRE=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1); else KERNEL_CFG_SHA_PRE=absent; fi
CONTAINER_BEFORE=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1 || true)
cp -p "$OWRT/.config" "$WORK/.config.pre-build"

# ---- install package + apply config delta --------------------------------
rm -f "$LANE/build-evidence/driver-artifact.txt"
if [ "$DRIVER_MODE" = prebuilt ]; then
	"$HERE/install-into-tree.sh" --owrt "$OWRT" --driver-ko "$DRIVER_KO" --label "$LABEL" --force
	DRIVER_MANIFEST=$OWRT/package/hi5622-wifi/prebuilt/prebuilt-sha256.txt
	DRIVER_SHA=$(grep '^sha256=' "$DRIVER_MANIFEST" | cut -d= -f2)
	DRIVER_SIZE=$(grep '^size_bytes=' "$DRIVER_MANIFEST" | cut -d= -f2)
	cp "$DRIVER_MANIFEST" "$LANE/build-evidence/driver-artifact.txt"
else
	"$HERE/install-into-tree.sh" --owrt "$OWRT" --driver-src "$DRIVER_SRC" --force
	DRIVER_MANIFEST=$OWRT/package/hi5622-wifi/src-sha256.txt
	DRIVER_SHA=$(sha256sum "$DRIVER_MANIFEST" | cut -d' ' -f1)
	DRIVER_SIZE="$(find "$OWRT/package/hi5622-wifi/src" -maxdepth 1 -type f | wc -l) source files"
	cp "$DRIVER_MANIFEST" "$LANE/build-evidence/driver-artifact.txt"
fi
DRIVER_MANIFEST_SHA=$(sha256sum "$DRIVER_MANIFEST" | cut -d' ' -f1)

{
	printf '\n# ---- wifi-image-integration bench delta (task-112) ----\n'
	cat "$LANE/config-delta.config"
} >> "$OWRT/.config"

if [ -d "$TARGET_ROOT" ]; then
	tar -C "$TARGET_ROOT" -cf "$WORK/target-root.pre-build.tar" .
	echo "snapshot   : rootfs staging tree saved ($(stat -c %s "$WORK/target-root.pre-build.tar") B)"
fi

START_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
START_EPOCH=$(date +%s)

echo
echo "-- make defconfig (resolving the delta) --"
( cd "$OWRT" && make defconfig ) 2>&1 | tee "$WORK/defconfig.log"

MISSING=0
for sym in CONFIG_PACKAGE_iw CONFIG_PACKAGE_wireless-regdb \
           CONFIG_PACKAGE_wifi-scripts CONFIG_WIFI_SCRIPTS_UCODE \
           CONFIG_PACKAGE_hostapd-mini CONFIG_PACKAGE_hostapd-common \
           CONFIG_PACKAGE_ucode-mod-nl80211 CONFIG_PACKAGE_ucode-mod-rtnl \
           CONFIG_PACKAGE_kmod-hi5622-cfg80211 CONFIG_PACKAGE_kmod-hi5622-wifi; do
	if ! grep -q "^$sym=y$" "$OWRT/.config"; then
		echo "MISSING config symbol: $sym"
		MISSING=$((MISSING + 1))
	fi
done
[ "$MISSING" -eq 0 ] || die "$MISSING config delta symbol(s) did not resolve; see $WORK/.config.resolved"
echo "config delta resolved: all selected symbols =y (self-contained image: iw+regdb+hostapd+ucode+wifi-scripts+kmods)"

# Carry over the release-line kernel symbols that defconfig drops (unknown to
# config/Config-kernel.in but mapped to CONFIG_* by include/kernel-defaults.mk).
# Without this the kernel is rebuilt with a changed config - see
# ../../kernel-symbols.config for the measured consequence.
cat "$LANE/kernel-symbols.config" >> "$OWRT/.config"
echo "kernel symbols carried over: $(grep -cE '^CONFIG_KERNEL_' "$LANE/kernel-symbols.config") line(s)"

cp "$OWRT/.config" "$WORK/.config.resolved"
diff -u "$WORK/.config.pre-build" "$WORK/.config.resolved" > "$WORK/config-delta-applied.diff" || true

echo
echo "-- make -j$JOBS (world + image) --"
BUILD_RC=0
if ! ( cd "$OWRT" && make -j"$JOBS" ) 2>&1 | tee "$WORK/build.log"; then
	BUILD_RC=1
	echo "build FAILED (log: $WORK/build.log); the shared tree is restored before exit"
fi
END_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
END_EPOCH=$(date +%s)
echo "build finished in $((END_EPOCH - START_EPOCH)) s"

KCFG_SHA_POST=$(sha256sum "$OWRT/$KCONFIG" | cut -d' ' -f1)
if [ -f "$KERNEL_CFG" ]; then KERNEL_CFG_SHA_POST=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1); else KERNEL_CFG_SHA_POST=absent; fi

# ---- verify the built bytes ---------------------------------------------
VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ] && [ "$BUILD_RC" -eq 0 ]; then
	echo
	"$HERE/verify-image.sh" --owrt "$OWRT" --work "$WORK/verify" > "$WORK/image-verify.txt" 2>&1 || VERIFY_RC=$?
	cat "$WORK/image-verify.txt"
fi

# ---- collect the summary -------------------------------------------------
CONTAINER=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
CONTAINER_SHA=unknown; CONTAINER_SIZE=unknown
if [ -n "$CONTAINER" ]; then
	CONTAINER_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
	CONTAINER_SIZE=$(stat -c %s "$CONTAINER")
fi
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
if [ -f "$KDIR/cudy_wr3000-v2-kernel.bin" ]; then
	KSIZE=$(stat -c %s "$KDIR/cudy_wr3000-v2-kernel.bin")
	KSHA=$(sha256sum "$KDIR/cudy_wr3000-v2-kernel.bin" | cut -d' ' -f1)
else
	KSIZE=absent; KSHA=absent
fi
if [ -f "$KDIR/cudy_wr3000-v2-rootfs.ubi" ]; then
	RSIZE=$(stat -c %s "$KDIR/cudy_wr3000-v2-rootfs.ubi")
	RSHA=$(sha256sum "$KDIR/cudy_wr3000-v2-rootfs.ubi" | cut -d' ' -f1)
else
	RSIZE=absent; RSHA=absent
fi
if [ -f "$KDIR/root.squashfs" ]; then
	SSIZE=$(stat -c %s "$KDIR/root.squashfs")
	SSHA=$(sha256sum "$KDIR/root.squashfs" | cut -d' ' -f1)
else
	SSIZE=absent; SSHA=absent
fi
CFG80211_KO=$KDIR/linux-6.18.60/net/wireless/cfg80211.ko
if [ -f "$CFG80211_KO" ]; then
	CFG_SIZE=$(stat -c %s "$CFG80211_KO"); CFG_SHA=$(sha256sum "$CFG80211_KO" | cut -d' ' -f1)
else
	CFG_SIZE=absent; CFG_SHA=absent
fi
DRV_KO=$(find "$OWRT/build_dir/target-arm_cortex-a9_musl_eabi" -maxdepth 4 -name hi5622_wifi.ko -path '*hi5622-wifi*' 2>/dev/null | head -1)
if [ -n "$DRV_KO" ]; then
	DRV_SIZE=$(stat -c %s "$DRV_KO"); DRV_SHA=$(sha256sum "$DRV_KO" | cut -d' ' -f1)
else
	DRV_SIZE=absent; DRV_SHA=absent
fi

{
	echo "# BUILD SUMMARY - wifi-image-integration bench image (task-112)"
	echo "# generated by tools/build-image.sh; host-only build, nothing flashed"
	echo
	echo "tree                       : $OWRT"
	echo "tree git HEAD              : $TREE_HEAD"
	echo "work dir (full logs)       : $WORK"
	echo "start (UTC)                : $START_TS"
	echo "end   (UTC)                : $END_TS"
	echo "duration                   : $((END_EPOCH - START_EPOCH)) s"
	echo "make -j                    : $JOBS"
	echo "build rc                   : $BUILD_RC"
	echo "driver source              : $DRIVER_SRC"
	echo "driver mode                : $DRIVER_MODE"
	echo "driver path                : $DRIVER_KO"
	echo "driver size / sha          : $DRIVER_SIZE / $DRIVER_SHA"
	echo "driver manifest            : $(basename "$DRIVER_MANIFEST") sha256 $DRIVER_MANIFEST_SHA"
	echo "kernel symbols carried over: kernel-symbols.config sha256 $(sha256sum "$LANE/kernel-symbols.config" | cut -d' ' -f1)"
	echo
	echo "== config =="
	echo "pre-build .config sha256   : $(sha256sum "$WORK/.config.pre-build" | cut -d' ' -f1)"
	echo "resolved .config sha256    : $(sha256sum "$WORK/.config.resolved" | cut -d' ' -f1)"
	echo "target kernel config       : $KCONFIG"
	echo "  sha256 before/after      : $KCFG_SHA_PRE / $KCFG_SHA_POST"
	echo "built kernel .config       : $KERNEL_CFG_SHA_PRE / $KERNEL_CFG_SHA_POST"
	echo "resolved package delta     :"
	grep -E '^CONFIG_PACKAGE_(iw|wireless-regdb|wifi-scripts|hostapd-mini|hostapd-common|ucode-mod-nl80211|ucode-mod-rtnl|kmod-hi5622-cfg80211|kmod-hi5622-wifi)=' "$WORK/.config.resolved" | sed 's/^/  /'
	grep -E '^CONFIG_WIFI_SCRIPTS_UCODE=' "$WORK/.config.resolved" | sed 's/^/  /'
	echo
	echo "== image artifacts =="
	echo "container                  : $CONTAINER"
	echo "container size / sha256    : $CONTAINER_SIZE B / $CONTAINER_SHA"
	echo "kernel slice size / sha256 : $KSIZE B / $KSHA"
	echo "rootfs ubi size / sha256   : $RSIZE B / $RSHA"
	echo "rootfs squashfs size / sha256: $SSIZE B / $SSHA"
	echo "cfg80211.ko size / sha256  : $CFG_SIZE B / $CFG_SHA"
	echo "hi5622_wifi.ko size / sha256: $DRV_SIZE B / $DRV_SHA"
	echo
	echo "== verification =="
	echo "verify rc                  : $VERIFY_RC"
	if [ -f "$WORK/image-verify.txt" ]; then
		grep -E 'RESULT:|kernel check:|consistency :|PRESENT|MISSING' "$WORK/image-verify.txt" | sed 's/^/  /'
	fi
} > "$WORK/build-summary.txt"

# ---- evidence copies (committed; text only) ------------------------------
cp "$WORK/build-summary.txt" "$LANE/build-evidence/build-summary.txt"
cp "$WORK/image-verify.txt" "$LANE/build-evidence/image-verify.txt" 2>/dev/null || true
cp "$WORK/config-delta-applied.diff" "$LANE/build-evidence/config-delta-applied.diff"
cp "$WORK/.config.resolved" "$LANE/build-evidence/config-resolved.txt"
if [ -f "$WORK/build.log" ]; then
	LOG_SIZE=$(stat -c %s "$WORK/build.log")
	if [ "$LOG_SIZE" -le 300000 ]; then
		cp "$WORK/build.log" "$LANE/build-evidence/build-log.txt"
	else
		tail -c 300000 "$WORK/build.log" > "$LANE/build-evidence/build-log-tail.txt"
		echo "full build log ($LOG_SIZE B) stayed local: $WORK/build.log (committed tail as build-log-tail.txt)"
	fi
fi
{
	echo "kernel config sha256 before/after build:"
	echo "  $KCONFIG: $KCFG_SHA_PRE -> $KCFG_SHA_POST"
	echo "  built kernel .config: $KERNEL_CFG_SHA_PRE -> $KERNEL_CFG_SHA_POST"
	echo "container before this build: $CONTAINER_BEFORE"
	echo "container after  this build: $CONTAINER"
} > "$LANE/build-evidence/kernel-config-sha.txt"

# ---- restore the shared tree --------------------------------------------
if [ "$KEEP_CONFIG" -eq 0 ]; then
	cp -p "$WORK/.config.pre-build" "$OWRT/.config"
	if cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then
		echo "restored   : $OWRT/.config byte-identical to pre-build"
	else
		die "failed to restore $OWRT/.config"
	fi
else
	echo "kept       : $OWRT/.config now carries the bench delta (--keep-config)"
fi

if [ "$KEEP_INSTALLED" -eq 0 ] && [ -f "$WORK/target-root.pre-build.tar" ]; then
	case "$TARGET_ROOT" in
		"$OWRT"/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon) ;;
		*) die "refusing to remove unexpected rootfs staging dir: $TARGET_ROOT" ;;
	esac
	rm -rf "$TARGET_ROOT"
	mkdir -p "$TARGET_ROOT"
	tar -C "$TARGET_ROOT" -xf "$WORK/target-root.pre-build.tar"
	echo "restored   : rootfs staging tree (no bench packages left installed in the tree)"
else
	echo "kept       : rootfs staging tree with the bench packages installed (--keep-installed)"
fi

echo
echo "evidence   : $LANE/build-evidence/"
echo "build rc   : $BUILD_RC"
echo "verify rc  : $VERIFY_RC"
[ "$BUILD_RC" -eq 0 ] || die "build FAILED (full log: $WORK/build.log; evidence dir updated, tree restored)"
[ "$VERIFY_RC" -eq 0 ] || die "image built but verification FAILED (see $WORK/image-verify.txt)"
echo "RESULT: PASS - bench image built, verified and evidence recorded"
