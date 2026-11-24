#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# bake-final-image.sh - build the FINAL bench image (task-147, host-only).
#
# WHAT IT DOES
#   Rebuilds the self-contained bench image (the wifi-image-integration image
#   container ae5e28bf) as a ROOTFS-ONLY successor:
#     * packages the FINAL ALL-PATCH clean-room module as kmod-hi5622-wifi
#       (prebuilt: /tmp/wifi-allpatch-artifact/hi5622_wifi.ko,
#        308,080 B sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41);
#     * stages this lane's rootfs payload (autoload=0 loader/init policy +
#       hostapd open/WPA2/WPA2-WPA3 fixtures);
#     * rebuilds the rootfs and the sysupgrade container;
#     * proves the kernel slice is UNCHANGED (13e81214... / slot 1794a531...);
#     * verifies the built bytes with tools/verify-final-image.sh;
#     * records the new container/rootfs/squashfs/module identities;
#     * restores the shared tree (.config, rootfs staging tree, files/ overlay).
#
# The kernel slice is NEVER rebuilt: the same config delta + kernel-symbols
# carry-over as wifi-image-integration is used, so the build reuses the
# hardware-validated kernel.  The script FAILS if the kernel hash changes.
#
# SERIALIZATION: one world build at a time in the shared OpenWrt tree.  Uses
# the same flock as the wifi-image-integration tooling:
# /tmp/cudy-openwrt-world-build.lock.
#
#   ./tools/bake-final-image.sh [--owrt DIR] [--driver-ko FILE] [--jobs N]
#                               [--skip-verify] [--skip-build]
#
# Everything the script writes outside this lane is in the (untracked) OpenWrt
# tree and is restored before exit; build logs/evidence land in
# build-evidence/ (work/ is gitignored, the curated text copies are committed).
set -euo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DRIVER_KO=/tmp/wifi-allpatch-artifact/hi5622_wifi.ko
JOBS=$(nproc)
SKIP_VERIFY=0
SKIP_BUILD=0

# --- pinned identities ------------------------------------------------------
EXPECTED_DRIVER_SHA=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
EXPECTED_DRIVER_SIZE=308080
EXPECTED_DRIVER_PARAMS=47
EXPECTED_KERNEL_SLICE_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SLICE_SIZE=3372638
EXPECTED_TREE_HEAD=27663a0
EXPECTED_TARGET_KCONFIG=ecf1aae26a527737ac94ad62392181e92be1cffe5c03d0f46c16b05cdc9be41f
EXPECTED_KERNEL_CONFIG=08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c
SLOT_HASH_PREFIX=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
REMOTE_PREV_CONTAINER=ae5e28bfef591fad7d9b4da726128ec2307273aebc41008e0ad9c3867bde6994
ART_DIR=${ART_DIR:-/home/shibbityshwab/wifi-final-image-artifacts}

die() { echo "bake-final-image: ERROR: $*" >&2; exit 1; }
log() { echo "bake-final-image: $*"; }

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=${2:-}; shift 2 ;;
		--owrt=*) OWRT=${1#*=}; shift ;;
		--driver-ko) DRIVER_KO=${2:-}; shift 2 ;;
		--driver-ko=*) DRIVER_KO=${1#*=}; shift ;;
		--jobs) JOBS=${2:-}; shift 2 ;;
		--skip-verify) SKIP_VERIFY=1; shift ;;
		--skip-build) SKIP_BUILD=1; shift ;;
		-h|--help) sed -n '3,45p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

WORK=$LANE/build-evidence/work
mkdir -p "$WORK" "$LANE/build-evidence"
TARGET_ROOT=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
KERNEL_CFG=$KDIR/linux-6.18.60/.config
LOCKFILE=/tmp/cudy-openwrt-world-build.lock
PKG_BUILD=$KDIR/hi5622-wifi

# ===========================================================================
# 0. preflight: pinned artifact, pinned tree, free tree
# ===========================================================================
log "preflight: pinned driver artifact"
[ -f "$DRIVER_KO" ] || die "driver artifact not found: $DRIVER_KO"
D_SIZE=$(stat -c %s "$DRIVER_KO")
D_SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
[ "$D_SIZE" = "$EXPECTED_DRIVER_SIZE" ] || die "driver size $D_SIZE != pinned $EXPECTED_DRIVER_SIZE ($DRIVER_KO)"
[ "$D_SHA" = "$EXPECTED_DRIVER_SHA" ] || die "driver sha256 $D_SHA != pinned $EXPECTED_DRIVER_SHA ($DRIVER_KO)"
D_VERMAGIC=$(modinfo -F vermagic "$DRIVER_KO" 2>/dev/null || true)
case "$D_VERMAGIC" in *"6.18.60"*) ;; *) die "driver vermagic '$D_VERMAGIC' is not 6.18.60" ;; esac
D_DEPENDS=$(modinfo -F depends "$DRIVER_KO" 2>/dev/null || true)
case "$D_DEPENDS" in *cfg80211*) ;; *) die "driver depends='$D_DEPENDS' does not include cfg80211" ;; esac
[ "$(modinfo -F name "$DRIVER_KO" 2>/dev/null || true)" = "hi5622_wifi" ] || die "driver module name is not hi5622_wifi"
D_PARAMS=$(modinfo -F parm "$DRIVER_KO" 2>/dev/null | wc -l)
[ "$D_PARAMS" = "$EXPECTED_DRIVER_PARAMS" ] || die "driver parameter count $D_PARAMS != pinned $EXPECTED_DRIVER_PARAMS"
log "driver OK: $D_SIZE B sha256 $D_SHA, $D_PARAMS params, vermagic $D_VERMAGIC"

[ -f "$OWRT/rules.mk" ] && [ -f "$OWRT/.config" ] || die "not a configured OpenWrt tree: $OWRT"
TREE_HEAD=$(git -C "$OWRT" rev-parse --short HEAD 2>/dev/null || echo unknown)
[ "$TREE_HEAD" = "$EXPECTED_TREE_HEAD" ] || die "tree HEAD $TREE_HEAD != pinned $EXPECTED_TREE_HEAD ($OWRT)"
[ -f "$OWRT/target/linux/hisilicon/luofu/config-6.18" ] || die "luofu target config missing in $OWRT"
TK_SHA=$(sha256sum "$OWRT/target/linux/hisilicon/luofu/config-6.18" | cut -d' ' -f1)
[ "$TK_SHA" = "$EXPECTED_TARGET_KCONFIG" ] || die "target kernel config sha $TK_SHA != pinned $EXPECTED_TARGET_KCONFIG"
[ -f "$KERNEL_CFG" ] || die "built kernel .config missing: $KERNEL_CFG"
KCFG_SHA_PRE=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
[ "$KCFG_SHA_PRE" = "$EXPECTED_KERNEL_CONFIG" ] || die "built kernel .config sha $KCFG_SHA_PRE != pinned $EXPECTED_KERNEL_CONFIG (the validated kernel config changed)"
[ -f "$KERNEL_CFG" ] && [ -d "$KDIR" ] || die "kernel build dir missing: $KDIR"
log "tree OK: HEAD $TREE_HEAD, target config $TK_SHA, kernel .config $KCFG_SHA_PRE"

if pgrep -x make >/dev/null 2>&1; then
	for pid in $(pgrep -x make); do
		cwd=$(readlink "/proc/$pid/cwd" 2>/dev/null || true)
		case "$cwd" in "$OWRT"*) die "another make is running in the shared tree (pid $pid, cwd $cwd)" ;; esac
	done
fi
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq bake-final-image; then
	die "a make command line references $OWRT; another lane is building - serialize and retry"
fi
exec 9>"$LOCKFILE"
if ! flock -n 9; then
	die "another world build holds $LOCKFILE (serialize world builds)"
fi
log "build lock: $LOCKFILE held by pid $$"

if [ -e "$OWRT/files" ]; then
	die "$OWRT/files already exists; refusing to mix overlays (remove it first: tools/stage-payload.sh --remove or manual)"
fi
[ -d "$TARGET_ROOT" ] || die "rootfs staging tree missing: $TARGET_ROOT"

# ===========================================================================
# 1. snapshot the shared tree state
# ===========================================================================
cp -p "$OWRT/.config" "$WORK/.config.pre-build"
cp -p "$KERNEL_CFG" "$WORK/kernel-config.pre-build"
tar -C "$TARGET_ROOT" -cf "$WORK/target-root.pre-build.tar" .
CONTAINER_BEFORE=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1 || true)
CONTAINER_BEFORE_SHA=absent
if [ -n "$CONTAINER_BEFORE" ]; then CONTAINER_BEFORE_SHA=$(sha256sum "$CONTAINER_BEFORE" | cut -d' ' -f1); fi
log "snapshot: .config, kernel .config, rootfs staging tree ($(stat -c %s "$WORK/target-root.pre-build.tar") B tar)"
log "previous container: ${CONTAINER_BEFORE_SHA}"

CLEANED=0
cleanup() {
	rc=$?
	trap - EXIT
	if [ "$CLEANED" -eq 0 ]; then
		CLEANED=1
		set +e
		echo "bake-final-image: restoring the shared tree"
		if [ -f "$WORK/payload-manifest.txt" ] && [ -d "$OWRT/files" ]; then
			"$HERE/stage-payload.sh" --tree "$OWRT" --remove --manifest "$WORK/payload-manifest.txt" || echo "WARN: payload overlay removal failed" >&2
		fi
		if [ -f "$WORK/.config.pre-build" ]; then
			cp -p "$WORK/.config.pre-build" "$OWRT/.config"
			if cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then echo "restored: .config byte-identical"; else echo "WARN: .config restore differs" >&2; fi
		fi
		if [ -f "$WORK/kernel-config.pre-build" ]; then
			cp -p "$WORK/kernel-config.pre-build" "$KERNEL_CFG"
			if cmp -s "$WORK/kernel-config.pre-build" "$KERNEL_CFG"; then echo "restored: built kernel .config byte-identical"; else echo "WARN: kernel .config restore differs" >&2; fi
		fi
		if [ -f "$WORK/target-root.pre-build.tar" ]; then
			rm -rf "$TARGET_ROOT"
			mkdir -p "$TARGET_ROOT"
			tar -C "$TARGET_ROOT" -xf "$WORK/target-root.pre-build.tar"
			echo "restored: rootfs staging tree"
		fi
		set -e
	fi
	exit $rc
}
trap cleanup EXIT

# ===========================================================================
# 2. stage the payload + the prebuilt package, then rebuild rootfs/container
# ===========================================================================
START_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
START_EPOCH=$(date +%s)

log "stage: rootfs payload overlay"
"$HERE/stage-payload.sh" --tree "$OWRT" --apply --manifest "$WORK/payload-manifest.txt"
cp "$WORK/payload-manifest.txt" "$LANE/build-evidence/payload-manifest.txt"

log "stage: kmod-hi5622-wifi package (PREBUILT, no compile)"
"$IMAGE_INT/tools/install-into-tree.sh" --owrt "$OWRT" --driver-ko "$DRIVER_KO" \
	--label "FINAL all-patch module (opensource 008aca5; 308,080 B sha 5b587949; 47 params; hardware-validated one-boot AP+WPA2-GTK+TX+RX-arm) - wifi-final-image task-147" \
	--force | tee "$WORK/install-package.log"
cp "$OWRT/package/hi5622-wifi/prebuilt/prebuilt-sha256.txt" "$WORK/driver-artifact.txt"
cp "$WORK/driver-artifact.txt" "$LANE/build-evidence/driver-artifact.txt"
# Force a real package rebuild: the build dir may hold the previous module.
rm -rf "$PKG_BUILD"

{
	echo
	echo "# ---- wifi-final-image bench delta (task-147; rootfs-only successor) ----"
	cat "$IMAGE_INT/config-delta.config"
} >> "$OWRT/.config"

log "config: make defconfig (resolving the delta)"
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
[ "$MISSING" -eq 0 ] || die "$MISSING config delta symbol(s) did not resolve"
cat "$IMAGE_INT/kernel-symbols.config" >> "$OWRT/.config"
cp "$OWRT/.config" "$WORK/.config.resolved"
diff -u "$WORK/.config.pre-build" "$WORK/.config.resolved" > "$WORK/config-delta-applied.diff" || true

BUILD_RC=0
if [ "$SKIP_BUILD" -eq 0 ]; then
	log "build: make -j$JOBS (rootfs/container only; kernel must stay unchanged)"
	if ! ( cd "$OWRT" && make -j"$JOBS" ) 2>&1 | tee "$WORK/build.log"; then
		BUILD_RC=1
		log "build FAILED (log: $WORK/build.log)"
	fi
fi
END_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
END_EPOCH=$(date +%s)
log "build finished in $((END_EPOCH - START_EPOCH)) s (rc=$BUILD_RC)"

CONTAINER=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
[ -n "$CONTAINER" ] || die "no sysupgrade container built"
CONTAINER_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
CONTAINER_SIZE=$(stat -c %s "$CONTAINER")
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi"
KS_SIZE=$(stat -c %s "$WORK/kernel.bin"); KS_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1)
RS_SIZE=$(stat -c %s "$WORK/rootfs.ubi"); RS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1)
KCFG_SHA_POST=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
log "container: $CONTAINER_SIZE B sha256 $CONTAINER_SHA"
log "kernel slice: $KS_SIZE B sha256 $KS_SHA (must be $EXPECTED_KERNEL_SLICE_SHA)"

KERNEL_OK=1
if [ "$KS_SHA" != "$EXPECTED_KERNEL_SLICE_SHA" ] || [ "$KS_SIZE" != "$EXPECTED_KERNEL_SLICE_SIZE" ]; then
	KERNEL_OK=0
	log "FATAL: kernel slice CHANGED - this build is NOT a rootfs-only successor"
fi
if [ "$CONTAINER_SHA" = "$REMOTE_PREV_CONTAINER" ]; then
	log "NOTE: container is byte-identical to the previous image ae5e28bf"
fi

VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ] && [ "$BUILD_RC" -eq 0 ]; then
	log "verify: tools/verify-final-image.sh (built bytes)"
	"$HERE/verify-final-image.sh" --owrt "$OWRT" --work "$WORK/verify" \
		--driver-ko "$DRIVER_KO" --lane "$LANE" > "$WORK/verify.txt" 2>&1 || VERIFY_RC=$?
	cat "$WORK/verify.txt"
fi
cp "$WORK/verify.txt" "$LANE/build-evidence/final-image-verify.txt" 2>/dev/null || true

# ===========================================================================
# 3. record identities (container/rootfs/squashfs/module) + local artefacts
# ===========================================================================
SQ_BUILT=$KDIR/root.squashfs
SQ_BUILT_SIZE=absent; SQ_BUILT_SHA=absent
if [ -f "$SQ_BUILT" ]; then SQ_BUILT_SIZE=$(stat -c %s "$SQ_BUILT"); SQ_BUILT_SHA=$(sha256sum "$SQ_BUILT" | cut -d' ' -f1); fi
SQ_IMAGE_SIZE=absent; SQ_IMAGE_SHA=absent
if [ -f "$WORK/verify/rootfs.squashfs" ]; then
	SQ_IMAGE_SIZE=$(stat -c %s "$WORK/verify/rootfs.squashfs")
	SQ_IMAGE_SHA=$(sha256sum "$WORK/verify/rootfs.squashfs" | cut -d' ' -f1)
fi
INST_KO=$WORK/verify/extract/lib/modules/6.18.60/hi5622_wifi.ko
INST_SIZE=absent; INST_SHA=absent
if [ -f "$INST_KO" ]; then INST_SIZE=$(stat -c %s "$INST_KO"); INST_SHA=$(sha256sum "$INST_KO" | cut -d' ' -f1); fi
PKG_KO=$OWRT/package/hi5622-wifi/prebuilt/hi5622_wifi.ko
PKG_KO_SIZE=absent; PKG_KO_SHA=absent
if [ -f "$PKG_KO" ]; then PKG_KO_SIZE=$(stat -c %s "$PKG_KO"); PKG_KO_SHA=$(sha256sum "$PKG_KO" | cut -d' ' -f1); fi
APK=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/packages/kmod-hi5622-wifi-*.apk 2>/dev/null | head -1 || true)
APK_SIZE=absent; APK_SHA=absent
if [ -n "$APK" ]; then APK_SIZE=$(stat -c %s "$APK"); APK_SHA=$(sha256sum "$APK" | cut -d' ' -f1); fi
CFG80211_KO=$KDIR/linux-6.18.60/net/wireless/cfg80211.ko
CFG80211_SIZE=absent; CFG80211_SHA=absent
if [ -f "$CFG80211_KO" ]; then CFG80211_SIZE=$(stat -c %s "$CFG80211_KO"); CFG80211_SHA=$(sha256sum "$CFG80211_KO" | cut -d' ' -f1); fi
MANIFEST=$(ls -t "$OWRT"/bin/targets/hisilicon/luofu/*.manifest 2>/dev/null | head -1 || true)
MANIFEST_PKGS=absent
if [ -n "$MANIFEST" ]; then MANIFEST_PKGS=$(wc -l < "$MANIFEST"); fi

{
	echo "# HASHES - FINAL bench image (wifi-final-image, task-147)"
	echo "# host-only build; nothing flashed/booted by this lane"
	echo "# generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "build_start_utc = $START_TS"
	echo "build_end_utc   = $END_TS"
	echo "build_duration_s = $((END_EPOCH - START_EPOCH))"
	echo "tree            = $OWRT"
	echo "tree_head       = $TREE_HEAD"
	echo "target_kconfig_sha256 = $TK_SHA"
	echo "kernel_config_sha256  = $KCFG_SHA_PRE"
	echo
	echo "== image container =="
	echo "container_path   = $CONTAINER"
	echo "container_size   = $CONTAINER_SIZE"
	echo "container_sha256 = $CONTAINER_SHA"
	echo "previous_container_sha256 = $REMOTE_PREV_CONTAINER  (ae5e28bf, wifi-image-integration)"
	echo
	echo "== kernel slice (UNCHANGED requirement) =="
	echo "kernel_size      = $KS_SIZE"
	echo "kernel_sha256    = $KS_SHA"
	echo "kernel_expected  = $EXPECTED_KERNEL_SLICE_SHA"
	echo "slot_hash_expected = $SLOT_HASH_PREFIX"
	echo "kernel_unchanged = $KERNEL_OK"
	echo
	echo "== rootfs =="
	echo "rootfs_ubi_size   = $RS_SIZE"
	echo "rootfs_ubi_sha256 = $RS_SHA"
	echo "squashfs_build_size = $SQ_BUILT_SIZE"
	echo "squashfs_build_sha256 = $SQ_BUILT_SHA"
	echo "squashfs_volume_size = $SQ_IMAGE_SIZE"
	echo "squashfs_volume_sha256 = $SQ_IMAGE_SHA"
	echo
	echo "== driver module =="
	echo "module_artifact_path = $DRIVER_KO"
	echo "module_artifact_size = $D_SIZE"
	echo "module_artifact_sha256 = $D_SHA"
	echo "module_artifact_params = $D_PARAMS"
	echo "module_artifact_vermagic = $D_VERMAGIC"
	echo "module_artifact_depends = $D_DEPENDS"
	echo "module_tree_package_path = $PKG_KO"
	echo "module_tree_package_size = $PKG_KO_SIZE"
	echo "module_tree_package_sha256 = $PKG_KO_SHA"
	echo "module_installed_path = /lib/modules/6.18.60/hi5622_wifi.ko"
	echo "module_installed_size = $INST_SIZE"
	echo "module_installed_sha256 = $INST_SHA"
	echo "module_apk_path = $APK"
	echo "module_apk_size = $APK_SIZE"
	echo "module_apk_sha256 = $APK_SHA"
	echo
	echo "== other =="
	echo "cfg80211_tree_size = $CFG80211_SIZE"
	echo "cfg80211_tree_sha256 = $CFG80211_SHA"
	echo "manifest_packages = $MANIFEST_PKGS"
	echo "previous_tree_container_before_build = ${CONTAINER_BEFORE_SHA}"
	echo
	echo "== build/verify rc =="
	echo "build_rc = $BUILD_RC"
	echo "verify_rc = $VERIFY_RC"
	echo "kernel_ok = $KERNEL_OK"
} > "$LANE/build-evidence/hashes.txt"

cp "$WORK/config-delta-applied.diff" "$LANE/build-evidence/config-delta-applied.diff"
if [ -f "$WORK/build.log" ]; then
	LOG_SIZE=$(stat -c %s "$WORK/build.log")
	if [ "$LOG_SIZE" -le 300000 ]; then cp "$WORK/build.log" "$LANE/build-evidence/build-log.txt"
	else tail -c 300000 "$WORK/build.log" > "$LANE/build-evidence/build-log-tail.txt"; fi
fi
{
	echo "kernel config sha256 before/after: $KCFG_SHA_PRE / $KCFG_SHA_POST"
	echo "target config sha256: $TK_SHA (unchanged)"
	echo "container before: $CONTAINER_BEFORE ($CONTAINER_BEFORE_SHA)"
	echo "container after : $CONTAINER ($CONTAINER_SHA)"
} > "$LANE/build-evidence/kernel-config-sha.txt"

# local convenience copy for the bench (NOT committed)
mkdir -p "$ART_DIR"
cp -f "$CONTAINER" "$ART_DIR/final-image-sysupgrade.bin"
cp -f "$WORK/kernel.bin" "$ART_DIR/kernel-slice.bin"
cp -f "$WORK/rootfs.ubi" "$ART_DIR/rootfs.ubi"
if [ -f "$SQ_BUILT" ]; then cp -f "$SQ_BUILT" "$ART_DIR/root.squashfs"; fi
if [ -n "$APK" ]; then cp -f "$APK" "$ART_DIR/"; fi
( cd "$ART_DIR" && sha256sum final-image-sysupgrade.bin kernel-slice.bin rootfs.ubi > SHA256SUMS )
log "local artefacts: $ART_DIR (not committed)"

# ===========================================================================
# 4. result
# ===========================================================================
log "build_rc=$BUILD_RC verify_rc=$VERIFY_RC kernel_ok=$KERNEL_OK"
[ "$BUILD_RC" -eq 0 ] || die "build FAILED (log: $WORK/build.log; tree restored)"
[ "$KERNEL_OK" -eq 1 ] || die "kernel slice CHANGED - refusing to certify a rootfs-only image"
[ "$VERIFY_RC" -eq 0 ] || die "image built but verification FAILED (see $WORK/verify.txt)"
echo "RESULT: PASS - FINAL bench image built, kernel slice unchanged, verified"
