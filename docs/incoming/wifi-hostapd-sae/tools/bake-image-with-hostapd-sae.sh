#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# bake-image-with-hostapd-sae.sh - re-bake the FINAL bench image with an
# SAE-capable hostapd (wifi-hostapd-sae lane, task-148).  HOST-ONLY.
#
# The image is a ROOTFS-ONLY successor of the wifi-final-image bake
# (container 0efcd1f1..., rootfs 6196232d...): the hardware-validated kernel
# slice 13e81214... MUST stay byte-identical, only the rootfs changes:
#   * hostapd-mini (504,333 B, no SAE: "invalid key_mgmt 'SAE'") is replaced by
#     hostapd-mbedtls (OpenWrt full hostapd variant, CONFIG_SAE=y + mbedtls TLS;
#     libmbedtls21 is already part of the bench image);
#   * the three WPA3-SAE / WPA2-hardened fixtures from ../wifi-wpa3-sae are
#     staged into /etc/hi5622/ (the stale transition fixture is superseded);
#   * the pinned clean-room driver module is re-staged (no compile).
#
# Two flavours:
#   --variant A  driver = FINAL all-patch module  308,080 B 5b587949... (winner
#                of task-147; strict "final image + SAE hostapd" drop-in)
#   --variant B  driver = wpa3-sae PMF/IGTK module 311,108 B 08832e75... (the
#                WPA3 bench combination: hostapd-SAe + driver PMF mgmt key)
#
# The script snapshots and restores the shared OpenWrt tree, holds the world
# build lock, refuses a changed kernel slice, and records hashes + verification
# output in the lane evidence directory.  Nothing is flashed or booted.
#
# usage: ./tools/bake-image-with-hostapd-sae.sh --variant A|B [--skip-verify]
#   env: OWRT, JOBS, ART_DIR
set -euo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
FINAL_IMAGE=$(cd "$LANE/../wifi-final-image" && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
JOBS=${JOBS:-$(nproc)}
VARIANT=
SKIP_VERIFY=0
LOCKFILE=/tmp/cudy-openwrt-world-build.lock

EXPECTED_TREE_HEAD=27663a0
EXPECTED_TARGET_KCONFIG=ecf1aae26a527737ac94ad62392181e92be1cffe5c03d0f46c16b05cdc9be41f
EXPECTED_KERNEL_CONFIG=08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c
EXPECTED_KERNEL_SLICE=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SLICE_SIZE=3372638
PREV_CONTAINER=0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada

WORK=$LANE/build-evidence/work
EVID=$LANE/evidence
TARGET_ROOT=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
KERNEL_CFG=$KDIR/linux-6.18.60/.config
PKG_BUILD=$KDIR/hi5622-wifi
TARGETS=$OWRT/bin/targets/hisilicon/luofu

die() { echo "bake-image-with-hostapd-sae: ERROR: $*" >&2; exit 1; }
log() { echo "bake-image-with-hostapd-sae: $*"; }

while [ $# -gt 0 ]; do
	case "$1" in
		--variant) VARIANT=${2:-}; shift 2 ;;
		--variant=*) VARIANT=${1#*=}; shift ;;
		--skip-verify) SKIP_VERIFY=1; shift ;;
		-h|--help) sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

# ---- variant pins ---------------------------------------------------------
case "$VARIANT" in
A)
	DRIVER_KO=/tmp/wifi-allpatch-artifact/hi5622_wifi.ko
	DRIVER_SHA=5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
	DRIVER_SIZE=308080
	DRIVER_INSTALLED_SHA=f07dd505f1d7ac5dd62a826c440414bf2bc1662fe2fecb504dc4cec5b5e0d412
	VARIANT_LABEL="A: FINAL all-patch module (308,080 B 5b587949) + hostapd-mbedtls SAE"
	;;
B)
	DRIVER_KO=/tmp/wifi-wpa3-sae-artifact/hi5622_wifi.ko
	DRIVER_SHA=08832e750725980893962940e1b792f8361a6b49b5f3faea5c1777c70dca2960
	DRIVER_SIZE=311108
	DRIVER_INSTALLED_SHA=""       # stripped-down image copy is recorded, not pre-pinned
	VARIANT_LABEL="B: wpa3-sae PMF/IGTK module (311,108 B 08832e75) + hostapd-mbedtls SAE (WPA3 bench combination)"
	;;
*) die "--variant A|B is required" ;;
esac
ART=${ART_DIR:-/home/shibbityshwab/wifi-hostapd-sae-artifacts/$VARIANT}

[ -f "$DRIVER_KO" ] || die "driver artifact not found: $DRIVER_KO"
D_SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
D_SIZE=$(stat -c %s "$DRIVER_KO")
[ "$D_SHA" = "$DRIVER_SHA" ] || die "driver sha $D_SHA != pinned $DRIVER_SHA"
[ "$D_SIZE" = "$DRIVER_SIZE" ] || die "driver size $D_SIZE != pinned $DRIVER_SIZE"
[ "$(modinfo -F name "$DRIVER_KO")" = "hi5622_wifi" ] || die "driver artifact is not hi5622_wifi"

[ -f "$OWRT/rules.mk" ] && [ -f "$OWRT/.config" ] || die "not an OpenWrt tree: $OWRT"
TREE_HEAD=$(git -C "$OWRT" rev-parse --short HEAD)
[ "$TREE_HEAD" = "$EXPECTED_TREE_HEAD" ] || die "tree HEAD $TREE_HEAD != pinned $EXPECTED_TREE_HEAD"
[ -f "$OWRT/target/linux/hisilicon/luofu/config-6.18" ]
TK_SHA=$(sha256sum "$OWRT/target/linux/hisilicon/luofu/config-6.18" | cut -d' ' -f1)
[ "$TK_SHA" = "$EXPECTED_TARGET_KCONFIG" ] || die "target kernel config sha $TK_SHA changed"
[ -f "$KERNEL_CFG" ] || die "built kernel .config missing"
KCFG_PRE=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
[ "$KCFG_PRE" = "$EXPECTED_KERNEL_CONFIG" ] || die "built kernel .config $KCFG_PRE != validated $EXPECTED_KERNEL_CONFIG"
[ -d "$TARGET_ROOT" ] || die "rootfs staging tree missing: $TARGET_ROOT"
[ -e "$OWRT/files" ] && die "$OWRT/files already exists - another lane is staging an overlay"
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq 'bake-image-with-hostapd-sae'; then
	die "a make command line references $OWRT; another lane is building - retry"
fi
mkdir -p "$WORK" "$ART" "$EVID"
exec 9>"$LOCKFILE"
flock -n 9 || die "another world build holds $LOCKFILE - retry later"
log "lock acquired"

CREATED_FILES=0
CLEANED=0
cleanup() {
	rc=$?
	trap - EXIT
	if [ "$CLEANED" -eq 0 ]; then
		CLEANED=1
		set +e
		log "restoring shared-tree state"
		if [ "$CREATED_FILES" -eq 1 ]; then
			rm -rf "$OWRT/files"
		fi
		[ -f "$WORK/.config.pre-build" ] && cp -p "$WORK/.config.pre-build" "$OWRT/.config"
		[ -f "$WORK/kernel-config.pre-build" ] && cp -p "$WORK/kernel-config.pre-build" "$KERNEL_CFG"
		if [ -f "$WORK/target-root.pre-build.tar" ]; then
			rm -rf "$TARGET_ROOT"; mkdir -p "$TARGET_ROOT"
			tar -C "$TARGET_ROOT" -xf "$WORK/target-root.pre-build.tar"
		fi
		set -e
		if [ -f "$WORK/.config.pre-build" ] && cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then
			log "restored: .config byte-identical"
		else
			echo "WARN: .config restore differs" >&2
		fi
		if [ -f "$WORK/kernel-config.pre-build" ] && cmp -s "$WORK/kernel-config.pre-build" "$KERNEL_CFG"; then
			log "restored: built kernel .config byte-identical"
		else
			echo "WARN: kernel .config restore differs" >&2
		fi
		[ ! -e "$OWRT/files" ] && log "restored: files/ overlay removed"
	fi
	exit $rc
}
trap cleanup EXIT

# ---- snapshot -------------------------------------------------------------
cp -p "$OWRT/.config" "$WORK/.config.pre-build"
cp -p "$KERNEL_CFG" "$WORK/kernel-config.pre-build"
tar -C "$TARGET_ROOT" -cf "$WORK/target-root.pre-build.tar" .
CONTAINER_BEFORE=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1 || true)
CB_SHA=absent; [ -n "$CONTAINER_BEFORE" ] && CB_SHA=$(sha256sum "$CONTAINER_BEFORE" | cut -d' ' -f1)
log "snapshot taken; previous container: $CB_SHA"

# ---- 1. rootfs payload: final-image payload + this lane's fixtures --------
log "stage: rootfs payload (wifi-final-image payload + WPA3-SAE fixtures)"
mkdir -p "$OWRT/files"
CREATED_FILES=1
cp -a "$FINAL_IMAGE/payload/." "$OWRT/files/"
cp -a "$LANE/payload/." "$OWRT/files/"
mkdir -p "$OWRT/files/etc/rc.d"
ln -sfn ../init.d/hi5622-wifi "$OWRT/files/etc/rc.d/S96hi5622-wifi"
(
	cd "$OWRT/files"
	find . -type f -o -type l | sed 's|^\./||' | sort | while read -r rel; do
		if [ -L "$rel" ]; then
			printf '%s %s\n' "$rel" "symlink:$(readlink "$rel")"
		else
			printf '%s %s\n' "$rel" "$(sha256sum "$rel" | cut -d' ' -f1)"
		fi
	done
) > "$WORK/payload-manifest.txt"
log "payload: $(wc -l < "$WORK/payload-manifest.txt") paths staged"

# ---- 2. prebuilt driver package ------------------------------------------
log "stage: kmod-hi5622-wifi (PREBUILT $D_SIZE B $D_SHA)"
"$IMAGE_INT/tools/install-into-tree.sh" --owrt "$OWRT" --driver-ko "$DRIVER_KO" \
	--label "$VARIANT_LABEL - wifi-hostapd-sae task-148" --force > "$WORK/install-package.log" 2>&1 || \
	{ cat "$WORK/install-package.log" >&2; die "driver package staging failed"; }
rm -rf "$PKG_BUILD"

# ---- 3. image config delta -----------------------------------------------
log "config: append bench delta (hostapd-mbedtls replaces hostapd-mini)"
{
	echo
	echo "# ---- wifi-hostapd-sae bench delta (task-148; rootfs-only successor of 0efcd1f1) ----"
	sed -e 's/^CONFIG_PACKAGE_hostapd-mini=y$/# [wifi-hostapd-sae] hostapd-mini replaced by hostapd-mbedtls (SAE)/' \
	    -e 's/^# The AP fixture used for the 2026-11-27 hostapd AP-ENABLED milestone; v2.12,/# (replaced) The AP fixture used for the 2026-11-27 hostapd AP-ENABLED milestone; v2.12,/' \
	    "$IMAGE_INT/config-delta.config"
	echo "CONFIG_PACKAGE_hostapd-mbedtls=y"
} > "$WORK/delta.config"
cat "$WORK/delta.config" >> "$OWRT/.config"
( cd "$OWRT" && make defconfig ) > "$WORK/defconfig.log" 2>&1 || { cat "$WORK/defconfig.log" >&2; die "make defconfig failed"; }
for sym in CONFIG_PACKAGE_iw CONFIG_PACKAGE_wireless-regdb CONFIG_PACKAGE_wifi-scripts \
           CONFIG_WIFI_SCRIPTS_UCODE CONFIG_PACKAGE_hostapd-mbedtls CONFIG_PACKAGE_hostapd-common \
           CONFIG_PACKAGE_libmbedtls CONFIG_PACKAGE_ucode-mod-nl80211 CONFIG_PACKAGE_ucode-mod-rtnl \
           CONFIG_PACKAGE_kmod-hi5622-cfg80211 CONFIG_PACKAGE_kmod-hi5622-wifi; do
	grep -q "^$sym=y$" "$OWRT/.config" || die "config symbol did not resolve: $sym"
done
grep -q '^# CONFIG_PACKAGE_hostapd-mini is not set$' "$OWRT/.config" || \
	die "hostapd-mini is still selected in the image config"
cat "$IMAGE_INT/kernel-symbols.config" >> "$OWRT/.config"
cp "$OWRT/.config" "$WORK/.config.resolved"
diff -u "$WORK/.config.pre-build" "$WORK/.config.resolved" > "$WORK/config-delta-applied.diff" || true
log "config resolved: hostapd-mbedtls=y, hostapd-mini unset, $(grep -c '^CONFIG_PACKAGE_' "$WORK/.config.resolved") package symbols"

# ---- 4. world build (rootfs/container only; kernel must not rebuild) -----
START_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
START_EPOCH=$(date +%s)
log "build: make -j$JOBS (world; the kernel compile must be skipped)"
BUILD_RC=0
if ! ( cd "$OWRT" && make -j"$JOBS" ) > "$WORK/build.log" 2>&1; then
	BUILD_RC=1
	log "build FAILED (log: $WORK/build.log)"
	tail -40 "$WORK/build.log" >&2 || true
fi
END_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
END_EPOCH=$(date +%s)
[ "$BUILD_RC" -eq 0 ] || die "world build failed"

CONTAINER=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
[ -n "$CONTAINER" ] || die "no sysupgrade container built"
CONTAINER_SIZE=$(stat -c %s "$CONTAINER")
CONTAINER_SHA=$(sha256sum "$CONTAINER" | cut -d' ' -f1)
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi"
KS_SIZE=$(stat -c %s "$WORK/kernel.bin"); KS_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1)
RS_SIZE=$(stat -c %s "$WORK/rootfs.ubi"); RS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1)
KCFG_POST=$(sha256sum "$KERNEL_CFG" | cut -d' ' -f1)
log "container: $CONTAINER_SIZE B sha256 $CONTAINER_SHA"
log "kernel slice: $KS_SIZE B sha256 $KS_SHA"
[ "$KS_SHA" = "$EXPECTED_KERNEL_SLICE" ] || die "kernel slice CHANGED (not a rootfs-only image)"
[ "$KS_SIZE" = "$EXPECTED_KERNEL_SLICE_SIZE" ] || die "kernel slice size CHANGED"
[ "$KCFG_POST" = "$EXPECTED_KERNEL_CONFIG" ] || die "built kernel .config changed during the build"
SQ=$KDIR/root.squashfs
SQ_SIZE=$(stat -c %s "$SQ")
SQ_SHA=$(sha256sum "$SQ" | cut -d' ' -f1)

# ---- 5. verify the built bytes -------------------------------------------
VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ]; then
	log "verify: tools/verify-image-with-hostapd-sae.sh"
	"$HERE/verify-image-with-hostapd-sae.sh" --variant "$VARIANT" --container "$CONTAINER" \
		--work "$WORK/verify-$VARIANT" --driver-installed-sha "$DRIVER_INSTALLED_SHA" \
		--root-squashfs "$SQ" \
		> "$WORK/verify.txt" 2>&1 || VERIFY_RC=$?
	cat "$WORK/verify.txt"
fi

# ---- 6. record evidence + local artifacts --------------------------------
mkdir -p "$ART"
cp -f "$CONTAINER" "$ART/final-image-with-hostapd-sae-$VARIANT.bin"
cp -f "$WORK/kernel.bin" "$ART/kernel-slice.bin"
cp -f "$WORK/rootfs.ubi" "$ART/rootfs.ubi"
cp -f "$SQ" "$ART/root.squashfs"
( cd "$ART" && sha256sum final-image-with-hostapd-sae-$VARIANT.bin kernel-slice.bin rootfs.ubi root.squashfs > SHA256SUMS )

{
	echo "# wifi-hostapd-sae image $VARIANT ($VARIANT_LABEL)"
	echo "# host-only bake; nothing flashed/booted by this lane"
	echo "# generated: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "build_start_utc = $START_TS"
	echo "build_end_utc   = $END_TS"
	echo "build_duration_s = $((END_EPOCH - START_EPOCH))"
	echo "openwrt_tree = $OWRT"
	echo "tree_head = $TREE_HEAD"
	echo "target_kconfig_sha256 = $TK_SHA"
	echo "kernel_config_sha256 = $KCFG_PRE (pre) / $KCFG_POST (post)"
	echo
	echo "== image container (sysupgrade .bin, the flash unit) =="
	echo "container_path   = $CONTAINER"
	echo "container_size   = $CONTAINER_SIZE"
	echo "container_sha256 = $CONTAINER_SHA"
	echo "previous_container_sha256 = $PREV_CONTAINER (wifi-final-image 0efcd1f1)"
	echo
	echo "== kernel slice - UNCHANGED requirement =="
	echo "kernel_size   = $KS_SIZE"
	echo "kernel_sha256 = $KS_SHA"
	echo "kernel_unchanged = YES"
	echo
	echo "== rootfs (rebuilt) =="
	echo "rootfs_ubi_size   = $RS_SIZE"
	echo "rootfs_ubi_sha256 = $RS_SHA"
	echo "root_squashfs_size   = $SQ_SIZE"
	echo "root_squashfs_sha256 = $SQ_SHA"
	echo
	echo "== driver module =="
	echo "module_artifact_size   = $D_SIZE"
	echo "module_artifact_sha256 = $D_SHA"
	echo "module_artifact_vermagic = $(modinfo -F vermagic "$DRIVER_KO")"
	echo "module_artifact_params   = $(modinfo -F parm "$DRIVER_KO" | wc -l)"
	INST_SHA_REC=$(grep -o 'installed module size/sha256 = [0-9]* / [0-9a-f]*' "$WORK/verify.txt" 2>/dev/null | sed 's/.* \/ //' | head -1)
	[ -n "$INST_SHA_REC" ] || INST_SHA_REC=$DRIVER_INSTALLED_SHA
	echo "module_installed_sha256  = $INST_SHA_REC"
	echo
	echo "== build / verify =="
	echo "build_rc = $BUILD_RC"
	echo "verify_rc = $VERIFY_RC"
	echo "local_artifacts = $ART"
} > "$EVID/image-$VARIANT.txt"
cp "$WORK/config-delta-applied.diff" "$EVID/config-delta-$VARIANT.diff"
grep -E '^(PASS|FAIL|RECORD)' "$WORK/verify.txt" > "$EVID/image-$VARIANT-verify.txt" 2>/dev/null || true
{
	echo "== built-image sha256 =="
	( cd "$ART" && cat SHA256SUMS )
	echo
	echo "== manifest =="
	sed -n '1,200p' "$TARGETS"/*.manifest
} > "$WORK/manifest-$VARIANT.txt" 2>/dev/null || true

log "image $VARIANT container sha: $CONTAINER_SHA"
[ "$VERIFY_RC" -eq 0 ] || die "image built but verification FAILED (see $WORK/verify.txt)"
echo "RESULT: PASS - image variant $VARIANT baked, kernel slice unchanged, verified"
