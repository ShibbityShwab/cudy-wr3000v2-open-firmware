#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-abi-image.sh - serialized rebuild of the bench image from the CURRENT
# release-line OpenWrt tree, with the kernel-package ABI evidence the kmod .apk
# deploy needs (task-128: close the kmod kernel-ABI mismatch, P1 path).
#
# What it does
#   1. serializes: refuses to start while a make process works in the tree and
#      holds an exclusive flock on /tmp/cudy-openwrt-world-build.lock;
#   2. installs the clean-room driver package + the bench config delta and the
#      kernel-symbols carry-over (the same inputs the validated image used);
#   3. runs make defconfig + make -jN (one world build);
#   4. restores $OWRT/.config byte-identically and the rootfs staging tree, also
#      on failure (EXIT trap), so later lane builds cannot inherit the delta;
#   5. verifies the BUILT BYTES: kernel slice + full-slot hash, the kernel
#      package version in the image's apk db vs the tree .vermagic vs the kmod
#      .apk kernel= dependency, module vermagic, required bench files;
#   6. extracts the flashable kernel slice + rootfs UBI to the artifacts dir and
#      records text evidence under build-evidence/.
#
# usage: ./tools/build-abi-image.sh [--owrt DIR] [--jobs N]
#                                   [--driver-src DIR] [--artifacts DIR]
#                                   [--allow-tree-drift] [--skip-verify]
#                                   [-h|--help]
#
# Host-only: no device I/O, no flash command, no network fetch.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
REPO=$(cd "$LANE/../../../.." && pwd)
INTEG=$(cd "$LANE/../wifi-image-integration" && pwd)
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_DRIVER_SRC=$LANE/../wifi-cfg80211/src
EVID=$LANE/build-evidence
WORK=$REPO/build/abi-image-work
DEFAULT_ART=$REPO/build/abi-image-artifacts

OWRT=$DEFAULT_OWRT
DRIVER_SRC=$DEFAULT_DRIVER_SRC
ART=$DEFAULT_ART
JOBS=$(nproc)
ALLOW_DRIFT=0
SKIP_VERIFY=0

# Canonical inputs of the hardware-validated bench image (wifi-image-integration
# BUILD-NOTES.md). A mismatch means the "current tree" is not the tree the
# validated slice came from -> stop instead of silently producing a new ABI.
EXPECTED_PRE_CONFIG_SHA=9052d5e36fd30e6c59276910f2b286d5a109a5dffd03f3a8e1386aece0ed075d
EXPECTED_RESOLVED_CONFIG_SHA=b377f6c9edfcc41e51061ee6b0c8ad0702b82dc8d5bd50a449ff301a29a0f932
EXPECTED_DRIVER_MANIFEST_SHA=10582bb49af68c8360d569d88e59fd994d370e76e4440e48fc24cc856af948ca
EXPECTED_KERNEL_CONFIG_SHA=08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c
EXPECTED_CONTAINER_SHA=df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310
EXPECTED_ROOTFS_UBI_SHA=62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746
LOCKFILE=/tmp/cudy-openwrt-world-build.lock
TARGET_ROOT_REL=build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon
KERNEL_CFG_REL=build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/.config
KDIR_REL=build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu

die() { printf 'build-abi-image: ERROR: %s\n' "$*" >&2; exit 1; }
say() { printf '%s\n' "$*"; }

usage() { sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=$2; shift 2 ;;
		--jobs) JOBS=$2; shift 2 ;;
		--driver-src) DRIVER_SRC=$2; shift 2 ;;
		--artifacts) ART=$2; shift 2 ;;
		--allow-tree-drift) ALLOW_DRIFT=1; shift ;;
		--skip-verify) SKIP_VERIFY=1; shift ;;
		-h|--help) usage; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

[ -f "$OWRT/rules.mk" ] && [ -f "$OWRT/.config" ] || die "not a configured OpenWrt tree: $OWRT"
[ -x "$INTEG/tools/install-into-tree.sh" ] || die "missing integration tool: $INTEG/tools/install-into-tree.sh"
[ -f "$INTEG/config-delta.config" ] || die "missing $INTEG/config-delta.config"
[ -f "$INTEG/kernel-symbols.config" ] || die "missing $INTEG/kernel-symbols.config"
[ -f "$DRIVER_SRC/Kbuild" ] || die "missing driver source: $DRIVER_SRC/Kbuild"
mkdir -p "$WORK" "$EVID" "$ART"

say "== wifi-abi-image: serialized bench-image rebuild (task-128) =="
say "tree       : $OWRT"
say "driver src : $DRIVER_SRC"
say "jobs       : $JOBS"
say "work       : $WORK"
say "artifacts  : $ART"

# ---- serialization -------------------------------------------------------
if pgrep -x make >/dev/null 2>&1; then
	for pid in $(pgrep -x make); do
		cwd=$(readlink "/proc/$pid/cwd" 2>/dev/null || true)
		case "$cwd" in
			"$OWRT"*) die "another make is running in the shared tree (pid $pid, cwd $cwd); wait and retry" ;;
		esac
	done
fi
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq build-abi-image; then
	die "a make command line references $OWRT; another lane is building - serialize and retry"
fi
exec 9>"$LOCKFILE"
flock -n 9 || die "another world build holds $LOCKFILE (serialize world builds)"
say "build lock : $LOCKFILE held by pid $$"

# ---- pre-state -----------------------------------------------------------
TREE_HEAD=$(git -C "$OWRT" rev-parse --short HEAD 2>/dev/null || echo unknown)
PRE_CONFIG_SHA=$(sha256sum "$OWRT/.config" | cut -d' ' -f1)
KCONFIG=target/linux/hisilicon/luofu/config-6.18
KCONFIG_SHA_PRE=$(sha256sum "$OWRT/$KCONFIG" | cut -d' ' -f1)
KC_SHA_PRE=absent
[ -f "$OWRT/$KERNEL_CFG_REL" ] && KC_SHA_PRE=$(sha256sum "$OWRT/$KERNEL_CFG_REL" | cut -d' ' -f1)
CONTAINER_BEFORE=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1 || true)
CONTAINER_BEFORE_SHA=absent; CONTAINER_BEFORE_SIZE=absent
if [ -n "$CONTAINER_BEFORE" ]; then
	CONTAINER_BEFORE_SHA=$(sha256sum "$CONTAINER_BEFORE" | cut -d' ' -f1)
	CONTAINER_BEFORE_SIZE=$(stat -c %s "$CONTAINER_BEFORE")
fi
if [ "$ALLOW_DRIFT" -eq 0 ] && [ "$PRE_CONFIG_SHA" != "$EXPECTED_PRE_CONFIG_SHA" ]; then
	die "pre-build $OWRT/.config sha $PRE_CONFIG_SHA != canonical $EXPECTED_PRE_CONFIG_SHA; the tree is not at the validated baseline (use --allow-tree-drift only if that is intended)"
fi
say "tree HEAD  : $TREE_HEAD"
say ".config    : $PRE_CONFIG_SHA"
say "kernel cfg : $KC_SHA_PRE"
say "container  : ${CONTAINER_BEFORE_SHA} ${CONTAINER_BEFORE_SIZE} B"

# ---- restore discipline --------------------------------------------------
RESTORE_CONFIG=0
RESTORE_STAGING=0
restore_tree() {
	[ "$RESTORE_CONFIG" -eq 1 ] || return 0
	RESTORE_CONFIG=0
	if [ -f "$WORK/.config.pre-build" ]; then
		cp -p "$WORK/.config.pre-build" "$OWRT/.config"
		if cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then
			say "restored   : $OWRT/.config byte-identical to pre-build"
		else
			say "WARNING    : FAILED to restore $OWRT/.config"
		fi
	fi
}
restore_staging() {
	[ "$RESTORE_STAGING" -eq 1 ] || return 0
	RESTORE_STAGING=0
	TARGET_ROOT="$OWRT/$TARGET_ROOT_REL"
	case "$TARGET_ROOT" in
		"$OWRT"/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon) ;;
		*) say "WARNING    : refusing to remove unexpected rootfs staging dir: $TARGET_ROOT"; return 0 ;;
	esac
	rm -rf "$TARGET_ROOT"
	mkdir -p "$TARGET_ROOT"
	tar -C "$TARGET_ROOT" -xf "$WORK/target-root.pre-build.tar"
	say "restored   : rootfs staging tree (no bench packages left installed)"
}
on_exit() {
	rc=$?
	trap - EXIT
	restore_tree
	restore_staging
	exit $rc
}
trap on_exit EXIT

# ---- install package + apply config delta --------------------------------
cp -p "$OWRT/.config" "$WORK/.config.pre-build"
RESTORE_CONFIG=1
"$INTEG/tools/install-into-tree.sh" --owrt "$OWRT" --driver-src "$DRIVER_SRC" --force 2>&1 | tee "$WORK/install.log"
DRIVER_MANIFEST_SHA=$(sha256sum "$OWRT/package/hi5622-wifi/src-sha256.txt" | cut -d' ' -f1)
DRIVER_SNAPSHOT_FILES=$(find "$OWRT/package/hi5622-wifi/src" -maxdepth 1 -type f | wc -l)
say "driver snapshot: $DRIVER_SNAPSHOT_FILES files, manifest sha $DRIVER_MANIFEST_SHA"
if [ "$ALLOW_DRIFT" -eq 0 ] && [ "$DRIVER_MANIFEST_SHA" != "$EXPECTED_DRIVER_MANIFEST_SHA" ]; then
	die "driver source snapshot manifest $DRIVER_MANIFEST_SHA != expected $EXPECTED_DRIVER_MANIFEST_SHA (source changed; use --allow-tree-drift if intended)"
fi
{
	printf '\n# ---- wifi-image-integration bench delta (task-112, reused by task-128) ----\n'
	cat "$INTEG/config-delta.config"
} >> "$OWRT/.config"

TARGET_ROOT="$OWRT/$TARGET_ROOT_REL"
if [ -d "$TARGET_ROOT" ]; then
	tar -C "$TARGET_ROOT" -cf "$WORK/target-root.pre-build.tar" .
	RESTORE_STAGING=1
	say "snapshot   : rootfs staging tree saved ($(stat -c %s "$WORK/target-root.pre-build.tar") B)"
fi

START_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
START_EPOCH=$(date +%s)

say ""
say "-- make defconfig (resolving the delta) --"
( cd "$OWRT" && make defconfig ) 2>&1 | tee "$WORK/defconfig.log"

MISSING=0
for sym in CONFIG_PACKAGE_iw CONFIG_PACKAGE_iwinfo CONFIG_PACKAGE_libiwinfo \
           CONFIG_PACKAGE_libiwinfo-data CONFIG_PACKAGE_wireless-regdb \
           CONFIG_PACKAGE_kmod-hi5622-cfg80211 CONFIG_PACKAGE_kmod-hi5622-wifi; do
	grep -q "^$sym=y$" "$OWRT/.config" || { say "MISSING config symbol: $sym"; MISSING=$((MISSING + 1)); }
done
grep -q '^# CONFIG_WIFI_SCRIPTS_UCODE is not set$' "$OWRT/.config" || { say "MISSING: WIFI_SCRIPTS_UCODE=n"; MISSING=$((MISSING + 1)); }
[ "$MISSING" -eq 0 ] || die "$MISSING bench config symbol(s) did not resolve (see $WORK/defconfig.log)"
say "config delta resolved: all bench symbols =y / ucode CLI disabled"

cat "$INTEG/kernel-symbols.config" >> "$OWRT/.config"
say "kernel symbols carried over: $(grep -cE '^CONFIG_KERNEL_' "$INTEG/kernel-symbols.config") line(s)"

cp "$OWRT/.config" "$WORK/.config.resolved"
RESOLVED_CONFIG_SHA=$(sha256sum "$WORK/.config.resolved" | cut -d' ' -f1)
diff -u "$WORK/.config.pre-build" "$WORK/.config.resolved" > "$WORK/config-delta-applied.diff" || true
say "resolved .config: $RESOLVED_CONFIG_SHA"
if [ "$ALLOW_DRIFT" -eq 0 ] && [ "$RESOLVED_CONFIG_SHA" != "$EXPECTED_RESOLVED_CONFIG_SHA" ]; then
	die "resolved .config sha $RESOLVED_CONFIG_SHA != expected $EXPECTED_RESOLVED_CONFIG_SHA (config drift; use --allow-tree-drift if intended)"
fi

# ---- world build ---------------------------------------------------------
say ""
say "-- make -j$JOBS (world + image) --"
BUILD_RC=0
if ! ( cd "$OWRT" && make -j"$JOBS" ) 2>&1 | tee "$WORK/build.log"; then
	BUILD_RC=1
	say "build FAILED (log: $WORK/build.log); restoring the shared tree before exit"
fi
END_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
END_EPOCH=$(date +%s)
say "build finished in $((END_EPOCH - START_EPOCH)) s (rc=$BUILD_RC)"

KCONFIG_SHA_POST=$(sha256sum "$OWRT/$KCONFIG" | cut -d' ' -f1)
KC_SHA_POST=absent
[ -f "$OWRT/$KERNEL_CFG_REL" ] && KC_SHA_POST=$(sha256sum "$OWRT/$KERNEL_CFG_REL" | cut -d' ' -f1)

# ---- restore the shared tree BEFORE verification (safe: image is built) --
restore_tree
restore_staging

if [ "$BUILD_RC" -ne 0 ]; then
	{
		printf '# BUILD SUMMARY - wifi-abi-image (task-128)\n'
		printf 'result: BUILD FAILED (rc=1)\n'
		printf 'tree: %s\nlog: %s\n' "$OWRT" "$WORK/build.log"
	} > "$WORK/build-summary.txt"
	cp "$WORK/build-summary.txt" "$EVID/build-summary.txt"
	exit 1
fi

# ---- verify the built bytes ---------------------------------------------
CONTAINER=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
[ -n "$CONTAINER" ] || die "build produced no sysupgrade container in $OWRT/bin/targets/hisilicon/luofu/"
CONTAINER_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
CONTAINER_SIZE=$(stat -c %s "$CONTAINER")

VERIFY_RC=0
INTEG_VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ]; then
	say ""
	say "-- integration verifier (wifi-image-integration) --"
	"$INTEG/tools/verify-image.sh" --owrt "$OWRT" --work "$WORK/integ-verify" > "$WORK/integ-verify.txt" 2>&1 || INTEG_VERIFY_RC=$?
	tail -6 "$WORK/integ-verify.txt" || true

	say ""
	say "-- kernel-ABI verifier (this lane) --"
	"$HERE/verify-abi-image.sh" --owrt "$OWRT" --container "$CONTAINER" \
		--artifacts "$ART" --work "$WORK/abi-verify" --evidence "$EVID/abi-verify.txt" || VERIFY_RC=$?
fi

# ---- summary + committed text evidence -----------------------------------
KDIR="$OWRT/$KDIR_REL"
KSIZE=absent; KSHA=absent; RSIZE=absent; RSHA=absent; SSIZE=absent; SSHA=absent
[ -f "$KDIR/cudy_wr3000-v2-kernel.bin" ] && { KSIZE=$(stat -c %s "$KDIR/cudy_wr3000-v2-kernel.bin"); KSHA=$(sha256sum "$KDIR/cudy_wr3000-v2-kernel.bin" | cut -d' ' -f1); }
[ -f "$KDIR/cudy_wr3000-v2-rootfs.ubi" ] && { RSIZE=$(stat -c %s "$KDIR/cudy_wr3000-v2-rootfs.ubi"); RSHA=$(sha256sum "$KDIR/cudy_wr3000-v2-rootfs.ubi" | cut -d' ' -f1); }
[ -f "$KDIR/root.squashfs" ] && { SSIZE=$(stat -c %s "$KDIR/root.squashfs"); SSHA=$(sha256sum "$KDIR/root.squashfs" | cut -d' ' -f1); }
{
	printf '# BUILD SUMMARY - wifi-abi-image bench image rebuild (task-128)\n'
	printf '# generated by tools/build-abi-image.sh; host-only build, nothing flashed\n'
	printf 'result                     : %s\n' "$([ "$VERIFY_RC" -eq 0 ] && [ "$INTEG_VERIFY_RC" -eq 0 ] && echo PASS || echo FAIL)"
	printf 'tree                       : %s\n' "$OWRT"
	printf 'tree git HEAD              : %s\n' "$TREE_HEAD"
	printf 'work dir (full logs)       : %s\n' "$WORK"
	printf 'start (UTC) / end (UTC)    : %s / %s\n' "$START_TS" "$END_TS"
	printf 'duration                   : %s s\n' "$((END_EPOCH - START_EPOCH))"
	printf 'make -j                    : %s\n' "$JOBS"
	printf 'build rc / verify rc       : %s / %s (integration verifier rc=%s)\n' "$BUILD_RC" "$VERIFY_RC" "$INTEG_VERIFY_RC"
	printf 'driver source              : %s\n' "$DRIVER_SRC"
	printf 'driver snapshot files/sha  : %s / %s\n' "$DRIVER_SNAPSHOT_FILES" "$DRIVER_MANIFEST_SHA"
	printf 'driver snapshot expected   : %s\n' "$EXPECTED_DRIVER_MANIFEST_SHA"
	printf 'pre-build .config sha      : %s (expected %s)\n' "$PRE_CONFIG_SHA" "$EXPECTED_PRE_CONFIG_SHA"
	printf 'resolved .config sha       : %s (expected %s)\n' "$RESOLVED_CONFIG_SHA" "$EXPECTED_RESOLVED_CONFIG_SHA"
	printf 'target kernel config       : %s\n' "$KCONFIG"
	printf '  sha256 before/after      : %s / %s\n' "$KCONFIG_SHA_PRE" "$KCONFIG_SHA_POST"
	printf 'built kernel .config sha   : %s / %s (expected %s)\n' "$KC_SHA_PRE" "$KC_SHA_POST" "$EXPECTED_KERNEL_CONFIG_SHA"
	printf 'container                  : %s\n' "$CONTAINER"
	printf 'container size / sha256    : %s B / %s (previous %s)\n' "$CONTAINER_SIZE" "$CONTAINER_SHA" "$EXPECTED_CONTAINER_SHA"
	printf 'kernel slice size / sha256 : %s B / %s\n' "$KSIZE" "$KSHA"
	printf 'rootfs ubi size / sha256   : %s B / %s (previous %s)\n' "$RSIZE" "$RSHA" "$EXPECTED_ROOTFS_UBI_SHA"
	printf 'rootfs squashfs size / sha : %s B / %s\n' "$SSIZE" "$SSHA"
	printf 'flashable artifacts        : %s (kernel-slice.bin, rootfs.ubi, SHA256SUMS)\n' "$ART"
} > "$WORK/build-summary.txt"
cp "$WORK/build-summary.txt" "$EVID/build-summary.txt"
cp "$WORK/config-delta-applied.diff" "$EVID/config-delta-applied.diff"
cp "$WORK/.config.resolved" "$EVID/config-resolved.txt"
if [ -f "$WORK/build.log" ]; then
	if [ "$(stat -c %s "$WORK/build.log")" -le 300000 ]; then
		cp "$WORK/build.log" "$EVID/build-log.txt"
	else
		tail -c 300000 "$WORK/build.log" > "$EVID/build-log-tail.txt"
	fi
fi
cp "$WORK/integ-verify.txt" "$EVID/integration-verifier.txt" 2>/dev/null || true
cp "$WORK/install.log" "$EVID/driver-install.txt" 2>/dev/null || true

say ""
say "evidence   : $EVID/"
say "artifacts  : $ART/"
say "build rc   : $BUILD_RC   verify rc: $VERIFY_RC (integration verifier: $INTEG_VERIFY_RC)"
if [ "$VERIFY_RC" -ne 0 ] || [ "$INTEG_VERIFY_RC" -ne 0 ]; then
	die "image built but verification FAILED (see $WORK/abi-verify, $WORK/integ-verify)"
fi
say "RESULT: PASS - bench image rebuilt from the current tree, verified from its bytes, ABI self-consistent"
