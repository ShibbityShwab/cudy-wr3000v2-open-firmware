#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# bake-final-image-2.sh - bake the FINAL IMAGE #2 (wifi-final-image-2, task-159).
# HOST-ONLY: nothing is flashed, written to a device or rebooted here.
#
# Successor of the wifi-hostapd-sae image B (container 8944740e...): same
# rootfs-only recipe (SAE-capable hostapd-mbedtls, iw/iwinfo/ucode/regdb, the
# clean-room cfg80211 + hi5622 kmods) with the CORRECTED FINAL ASSOCIATION
# driver module installed as kmod-hi5622-wifi, the WPA2/WPA3 hostapd fixtures
# and the hi5622 loader/init (autoload=0).  The hardware-validated kernel
# slice 13e81214... (slot 1794a531...) MUST stay byte-identical.
#
# Default driver pin (task-159):
#   344,372 B sha256 ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e
#   56 params; final association tree + wifi-sibling-gpl corrected mgmt-TX
#   carrier patch (HCC g0 id6 control message).
# A successor artifact (e.g. the rx-watermark rebuild) can be baked by pinning
# the env vars DRIVER_KO / DRIVER_SHA / DRIVER_SIZE / DRIVER_PARAMS /
# DRIVER_INSTALLED_SHA.  A DRIVER_SHA mismatch aborts.
#
# usage: ./tools/bake-final-image-2.sh [--skip-verify]
#   env: OWRT, JOBS, ART_DIR, DRIVER_KO, DRIVER_SHA, DRIVER_SIZE,
#        DRIVER_PARAMS, DRIVER_INSTALLED_SHA
set -euo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
IMAGE_INT=$(cd "$LANE/../wifi-image-integration" && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
JOBS=${JOBS:-$(nproc)}
SKIP_VERIFY=0
LOCKFILE=/tmp/cudy-openwrt-world-build.lock

# ---- build-environment pins (unchanged from the validated image line) ------
EXPECTED_TREE_HEAD=27663a0
EXPECTED_TARGET_KCONFIG=ecf1aae26a527737ac94ad62392181e92be1cffe5c03d0f46c16b05cdc9be41f
EXPECTED_KERNEL_CONFIG=08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c
EXPECTED_KERNEL_SLICE=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
EXPECTED_KERNEL_SLICE_SIZE=3372638
EXPECTED_KERNEL_SLOT=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
PREV_CONTAINER=8944740ebe4c708eca202241ec1fa3e01fb86cebe3778609335632ddd697aaa1

# ---- corrected association module pins (task-159 baseline) -----------------
DRIVER_KO=${DRIVER_KO:-/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko}
DRIVER_SHA=${DRIVER_SHA:-ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e}
DRIVER_SIZE=${DRIVER_SIZE:-344372}
DRIVER_PARAMS=${DRIVER_PARAMS:-56}
DRIVER_INSTALLED_SHA=${DRIVER_INSTALLED_SHA:-}
DRIVER_LABEL=${DRIVER_LABEL:-CORRECTED FINAL ASSOCIATION module (assoc tree + sibling HCC g0 id6 mgmt-TX carrier) + hostapd-mbedtls SAE}

WORK=$LANE/build-evidence/work
EVID=$LANE/build-evidence
TARGET_ROOT=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon
KDIR=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu
KERNEL_CFG=$KDIR/linux-6.18.60/.config
PKG_BUILD=$KDIR/hi5622-wifi
TARGETS=$OWRT/bin/targets/hisilicon/luofu
ART=${ART_DIR:-/home/shibbityshwab/wifi-final-image-2-artifacts}

die() { echo "bake-final-image-2: ERROR: $*" >&2; exit 1; }
log() { echo "bake-final-image-2: $*"; }

while [ $# -gt 0 ]; do
	case "$1" in
		--skip-verify) SKIP_VERIFY=1; shift ;;
		-h|--help) sed -n '2,32p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1 (try --help)" ;;
	esac
done

# ---- input assertions ------------------------------------------------------
[ -f "$DRIVER_KO" ] || die "driver artifact not found: $DRIVER_KO (see BUILD-NOTES.md for the rebuild recipe)"
D_SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
D_SIZE=$(stat -c %s "$DRIVER_KO")
[ "$D_SHA" = "$DRIVER_SHA" ] || die "driver sha $D_SHA != pinned $DRIVER_SHA"
[ "$D_SIZE" = "$DRIVER_SIZE" ] || die "driver size $D_SIZE != pinned $DRIVER_SIZE"
D_PARAMS=$(modinfo -p "$DRIVER_KO" 2>/dev/null | wc -l)
[ "$D_PARAMS" = "$DRIVER_PARAMS" ] || die "driver param count $D_PARAMS != pinned $DRIVER_PARAMS"
[ "$(modinfo -F name "$DRIVER_KO")" = "hi5622_wifi" ] || die "driver artifact is not hi5622_wifi"
case "$(modinfo -F vermagic "$DRIVER_KO")" in *6.18.60*) ;; *) die "driver vermagic is not 6.18.60" ;; esac
case "$(modinfo -F depends "$DRIVER_KO")" in *cfg80211*) ;; *) die "driver does not depend on cfg80211" ;; esac
[ -d "$LANE/payload/etc/hi5622" ] || die "lane payload missing"
for f in etc/config/hi5622 etc/init.d/hi5622-wifi etc/modprobe.d/hi5622-wifi.conf \
         etc/hi5622/FIRMWARE-POLICY.txt etc/hi5622/hostapd-wpa2.conf \
         etc/hi5622/hostapd-wpa2-hardened.conf etc/hi5622/hostapd-wpa2-wpa3.conf \
         etc/hi5622/hostapd-wpa3-sae.conf usr/sbin/hi5622-load; do
	[ -f "$LANE/payload/$f" ] || die "lane payload missing $f"
done

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
if pgrep -af make 2>/dev/null | grep -F -- "$OWRT" | grep -vq 'bake-final-image-2'; then
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
		[ "$CREATED_FILES" -eq 1 ] && rm -rf "$OWRT/files"
		[ -f "$WORK/.config.pre-build" ] && cp -p "$WORK/.config.pre-build" "$OWRT/.config"
		[ -f "$WORK/kernel-config.pre-build" ] && cp -p "$WORK/kernel-config.pre-build" "$KERNEL_CFG"
		if [ -f "$WORK/target-root.pre-build.tar" ]; then
			rm -rf "$TARGET_ROOT"; mkdir -p "$TARGET_ROOT"
			tar -C "$TARGET_ROOT" -xf "$WORK/target-root.pre-build.tar"
		fi
		set -e
		if cmp -s "$WORK/.config.pre-build" "$OWRT/.config"; then log "restored: .config byte-identical"; else echo "WARN: .config restore differs" >&2; fi
		if cmp -s "$WORK/kernel-config.pre-build" "$KERNEL_CFG"; then log "restored: built kernel .config byte-identical"; else echo "WARN: kernel .config restore differs" >&2; fi
		[ ! -e "$OWRT/files" ] && log "restored: files/ overlay removed"
	fi
	exit $rc
}
trap cleanup EXIT

# ---- snapshot --------------------------------------------------------------
cp -p "$OWRT/.config" "$WORK/.config.pre-build"
cp -p "$KERNEL_CFG" "$WORK/kernel-config.pre-build"
tar -C "$TARGET_ROOT" -cf "$WORK/target-root.pre-build.tar" .
CONTAINER_BEFORE=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1 || true)
CB_SHA=absent; [ -n "$CONTAINER_BEFORE" ] && CB_SHA=$(sha256sum "$CONTAINER_BEFORE" | cut -d' ' -f1)
log "snapshot taken; previous container: $CB_SHA"

# ---- 1. rootfs payload ----------------------------------------------------
log "stage: rootfs payload (lane payload: $(	cd "$LANE/payload" && find . -type f | wc -l) files)"
mkdir -p "$OWRT/files"
CREATED_FILES=1
cp -a "$LANE/payload/." "$OWRT/files/"
mkdir -p "$OWRT/files/etc/rc.d"
ln -sfn ../init.d/hi5622-wifi "$OWRT/files/etc/rc.d/S96hi5622-wifi"
(
	cd "$OWRT/files"
	find . -type f -o -type l | sed 's|^\./||' | sort | while read -r rel; do
		if [ -L "$rel" ]; then printf '%s %s\n' "$rel" "symlink:$(readlink "$rel")"
		else printf '%s %s\n' "$rel" "$(sha256sum "$rel" | cut -d' ' -f1)"; fi
	done
) > "$WORK/payload-manifest.txt"
log "payload: $(wc -l < "$WORK/payload-manifest.txt") paths staged"

# ---- 2. prebuilt driver package ------------------------------------------
log "stage: kmod-hi5622-wifi (PREBUILT $D_SIZE B $D_SHA, $D_PARAMS params)"
"$IMAGE_INT/tools/install-into-tree.sh" --owrt "$OWRT" --driver-ko "$DRIVER_KO" \
	--label "$DRIVER_LABEL - wifi-final-image-2 task-159" --force > "$WORK/install-package.log" 2>&1 || \
	{ cat "$WORK/install-package.log" >&2; die "driver package staging failed"; }
rm -rf "$PKG_BUILD"

# ---- 3. image config delta (image-B recipe: hostapd-mbedtls SAE) ----------
log "config: append bench delta (hostapd-mbedtls replaces hostapd-mini)"
{
	echo
	echo "# ---- wifi-final-image-2 delta (task-159; rootfs-only successor of 8944740e) ----"
	sed -e 's/^CONFIG_PACKAGE_hostapd-mini=y$/# [wifi-final-image-2] hostapd-mini replaced by hostapd-mbedtls (SAE)/' \
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
grep -q '^# CONFIG_PACKAGE_hostapd-mini is not set$' "$OWRT/.config" || die "hostapd-mini is still selected"
cat "$IMAGE_INT/kernel-symbols.config" >> "$OWRT/.config"
cp "$OWRT/.config" "$WORK/.config.resolved"
diff -u "$WORK/.config.pre-build" "$WORK/.config.resolved" > "$WORK/config-delta-applied.diff" || true
log "config resolved: hostapd-mbedtls=y, hostapd-mini unset, $(grep -c '^CONFIG_PACKAGE_' "$WORK/.config.resolved") package symbols"

# ---- 4. world build (rootfs/container only; kernel must not rebuild) ------
START_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
START_EPOCH=$(date +%s)
log "build: make -j$JOBS (world; the kernel compile must be skipped)"
if ! ( cd "$OWRT" && make -j"$JOBS" ) > "$WORK/build.log" 2>&1; then
	log "build FAILED (log: $WORK/build.log)"
	tail -40 "$WORK/build.log" >&2 || true
	die "world build failed"
fi
END_TS=$(date -u +%Y-%m-%dT%H:%M:%SZ)
END_EPOCH=$(date +%s)

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
[ -f "$SQ" ] || die "build root.squashfs missing"
SQ_SIZE=$(stat -c %s "$SQ"); SQ_SHA=$(sha256sum "$SQ" | cut -d' ' -f1)

# installed module identity (from the built rootfs, not from the input file)
unsquashfs -no-exit-code -d "$WORK/rootfs-extract" "$SQ" >/dev/null 2>&1 || true
KO_INST=$WORK/rootfs-extract/lib/modules/6.18.60/hi5622_wifi.ko
[ -f "$KO_INST" ] || die "installed hi5622_wifi.ko missing from the built rootfs"
KO_INST_SIZE=$(stat -c %s "$KO_INST")
KO_INST_SHA=$(sha256sum "$KO_INST" | cut -d' ' -f1)
KO_INST_PARAMS=$(modinfo -p "$KO_INST" 2>/dev/null | wc -l)
log "installed module: $KO_INST_SIZE B sha256 $KO_INST_SHA ($KO_INST_PARAMS params)"
[ "$KO_INST_PARAMS" = "$D_PARAMS" ] || die "installed module param count $KO_INST_PARAMS != artifact $D_PARAMS"
if [ -n "$DRIVER_INSTALLED_SHA" ] && [ "$KO_INST_SHA" != "$DRIVER_INSTALLED_SHA" ]; then
	die "installed module sha $KO_INST_SHA != pinned $DRIVER_INSTALLED_SHA"
fi
# rootfs-only flash feasibility arithmetic (mtd14 rootfsb = 0x1740000)
MTD14_SIZE=24379392
[ "$RS_SIZE" -le "$MTD14_SIZE" ] || die "rootfs UBI $RS_SIZE B does not fit mtd14 ($MTD14_SIZE B)"

# ---- 5. verify the built bytes -------------------------------------------
VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ]; then
	log "verify: tools/verify-final-image-2.sh"
	VERIFY_ARGS=(--container "$CONTAINER" --work "$WORK/verify" --root-squashfs "$SQ" \
		--module-artifact "$DRIVER_KO" --module-artifact-sha "$DRIVER_SHA" \
		--module-artifact-size "$DRIVER_SIZE" --module-params "$D_PARAMS")
	[ -n "$DRIVER_INSTALLED_SHA" ] && VERIFY_ARGS+=(--module-installed-sha "$DRIVER_INSTALLED_SHA")
	"$HERE/verify-final-image-2.sh" "${VERIFY_ARGS[@]}" > "$WORK/verify.txt" 2>&1 || VERIFY_RC=$?
	cat "$WORK/verify.txt"
fi

# ---- 6. record evidence + local artifacts --------------------------------
mkdir -p "$ART"
cp -f "$CONTAINER" "$ART/final-image-2-sysupgrade.bin"
cp -f "$WORK/kernel.bin" "$ART/kernel-slice.bin"
cp -f "$WORK/rootfs.ubi" "$ART/rootfs.ubi"
cp -f "$SQ" "$ART/root.squashfs"
cp -f "$KO_INST" "$ART/hi5622_wifi.installed.ko"
( cd "$ART" && sha256sum final-image-2-sysupgrade.bin kernel-slice.bin rootfs.ubi root.squashfs hi5622_wifi.installed.ko > SHA256SUMS )

cp -f "$WORK/build.log" "$EVID/build-log.txt"
cp -f "$WORK/install-package.log" "$EVID/driver-stage.txt"
cp -f "$WORK/config-delta-applied.diff" "$EVID/config-delta-applied.diff"
cp -f "$WORK/payload-manifest.txt" "$EVID/payload-manifest.txt"
{
	echo "# driver artifact identity (wifi-final-image-2, task-159)"
	echo "artifact_path=$DRIVER_KO"
	echo "artifact_size=$D_SIZE"
	echo "artifact_sha256=$D_SHA"
	echo "artifact_params=$D_PARAMS"
	echo "artifact_vermagic=$(modinfo -F vermagic "$DRIVER_KO")"
	echo "artifact_depends=$(modinfo -F depends "$DRIVER_KO")"
	echo "artifact_license=$(modinfo -F license "$DRIVER_KO")"
	echo "installed_size=$KO_INST_SIZE"
	echo "installed_sha256=$KO_INST_SHA"
	echo "installed_params=$KO_INST_PARAMS"
	echo "# feature strings asserted by the verifier:"
	for s in himg_build_hcc_mgmt_tx hmac_mgmt_tx_event_status cfg80211_mgmt_tx_status_ext cfg80211_rx_mgmt_ext "set_default_mgmt_key" "data-tx tx: posts=" "DP rx_arm"; do
		if strings -a "$KO_INST" | grep -qF "$s"; then echo "feature_present=$s"; else echo "feature_MISSING=$s"; fi
	done
} > "$EVID/driver-artifact.txt"
{
	echo "# kernel-slice / kernel-config identity (wifi-final-image-2, task-159)"
	echo "target_kconfig_sha256=$TK_SHA"
	echo "kernel_config_pre=$KCFG_PRE"
	echo "kernel_config_post=$KCFG_POST"
	echo "kernel_slice_size=$KS_SIZE"
	echo "kernel_slice_sha256=$KS_SHA"
	echo "kernel_slot_sha256=$EXPECTED_KERNEL_SLOT (8,650,752 B slot, NOT written by a rootfs-only flash)"
	echo "container_before_sha256=$CB_SHA"
	echo "container_after_sha256=$CONTAINER_SHA"
} > "$EVID/kernel-config-sha.txt"
{
	echo "# wifi-final-image-2 (task-159) build record"
	echo "build_start_utc=$START_TS"
	echo "build_end_utc=$END_TS"
	echo "build_duration_s=$((END_EPOCH - START_EPOCH))"
	echo "openwrt_tree=$OWRT"
	echo "tree_head=$TREE_HEAD"
	echo "jobs=$JOBS"
	echo "target=hisilicon/luofu profile=DEVICE_cudy_wr3000-v2"
	echo "build_rc=0"
	echo
	echo "== image container (sysupgrade .bin, the flash unit) =="
	echo "container_path=$CONTAINER"
	echo "container_size=$CONTAINER_SIZE"
	echo "container_sha256=$CONTAINER_SHA"
	echo "previous_container_sha256=$CB_SHA (container present in the tree before this build; image B was 8944740e)"
	echo
	echo "== kernel slice - UNCHANGED requirement =="
	echo "kernel_size=$KS_SIZE"
	echo "kernel_sha256=$KS_SHA"
	echo "kernel_unchanged=YES"
	echo
	echo "== rootfs (rebuilt) =="
	echo "rootfs_ubi_size=$RS_SIZE"
	echo "rootfs_ubi_sha256=$RS_SHA"
	echo "root_squashfs_size=$SQ_SIZE"
	echo "root_squashfs_sha256=$SQ_SHA"
	echo "mtd14_rootfsb_size=$MTD14_SIZE"
	echo "rootfs_ubi_fits_mtd14=YES"
	echo
	echo "== driver module =="
	echo "module_artifact_size=$D_SIZE"
	echo "module_artifact_sha256=$D_SHA"
	echo "module_artifact_params=$D_PARAMS"
	echo "module_installed_size=$KO_INST_SIZE"
	echo "module_installed_sha256=$KO_INST_SHA"
	echo
	echo "== verify =="
	echo "verify_rc=$VERIFY_RC"
	echo "local_artifacts=$ART"
} > "$EVID/build-summary.txt"
{
	printf '%s' "$START_TS"
	printf ' duration_s=%s' "$((END_EPOCH - START_EPOCH))"
	printf ' prev_container=%s' "$CB_SHA"
	printf ' container=%s' "$CONTAINER_SHA"
	printf ' rootfs_ubi=%s' "$RS_SHA"
	printf ' squashfs=%s' "$SQ_SHA"
	printf ' module_installed=%s' "$KO_INST_SHA"
	printf ' verify_rc=%s\n' "$VERIFY_RC"
} >> "$EVID/build-runs.txt"
[ -f "$WORK/verify.txt" ] && cp -f "$WORK/verify.txt" "$EVID/final-image-verify.txt"
[ -f "$EVID/hashes.txt" ] && rm -f "$EVID/hashes.txt"
( cd "$ART" && sha256sum * ) > "$EVID/hashes.txt"

log "DONE: container $CONTAINER_SIZE B sha256 $CONTAINER_SHA"
log "      kernel slice UNCHANGED $KS_SHA"
log "      module installed $KO_INST_SIZE B sha256 $KO_INST_SHA ($KO_INST_PARAMS params)"
log "      verify rc=$VERIFY_RC; evidence in $EVID; artifacts in $ART"
[ "$VERIFY_RC" -eq 0 ] || exit 1
exit 0
