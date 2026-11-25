#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# bake-image-3.sh - bake IMAGE #3, the full sysupgrade container for the
# Cudy WR3000 v2.0 bench line (lane wifi-image-3, shared task task-196).
#
# HOST-ONLY: nothing here is flashed, written to a device, loaded onto a device
# or rebooted.  The Lead owns every fire.
#
# IMAGE #3 = the VERIFIED IMAGE #2 container (kernel slice UNCHANGED
# 13e81214..., slot 1794a531...) + the verified wifi-image-update rootfs
# (the bench-proven 5 GHz regdomain cfg80211.ko + the 802.11ax hostapd) +
# the IMAGE #3 driver module (the 5 GHz F2 chain + he_caps + the BA skeleton)
# + the image #3 text policy payload (loader/uci/modprobe/hostapd fixtures).
#
# The rootfs is a squashfs surgery on the wifi-image-update root.squashfs
# (the same proven path as wifi-image-update: unsquashfs - replacement -
# mksquashfs with the image's exact options - ubinize with the checked-in
# luofu-ubinize.cfg geometry), then the sysupgrade container is re-assembled
# around the UNCHANGED kernel slice.  No OpenWrt world build is performed and
# the shared build tree is never touched.
#
# usage: ./tools/bake-image-3.sh [--skip-verify]
#
# Inputs are pinned below; a drifted input aborts before any work.
set -euo pipefail
export LC_ALL=C

BUILD_START=$(date -u +%Y-%m-%dT%H:%M:%SZ)
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
SKIP_VERIFY=0
while [ $# -gt 0 ]; do
	case "$1" in
		--skip-verify) SKIP_VERIFY=1; shift ;;
		-h|--help) sed -n '2,30p' "$0" | sed 's/^# //'; exit 0 ;;
		*) echo "bake-image-3: unknown argument: $1" >&2; exit 2 ;;
	esac
done

# ---- pinned inputs (local, never committed) --------------------------------
BASE_SQUASHFS=/home/shibbityshwab/wifi-image-update-artifacts/root.squashfs
BASE_SQUASHFS_SHA=b461df0cbc804587fdb789030b368b79d3544c4caa7c5f329a4ab0828e8cb77a
BASE_SQUASHFS_SIZE=3997696
BASE_UBI=/home/shibbityshwab/wifi-image-update-artifacts/rootfs.ubi
BASE_UBI_SHA=d3c665f16e76811d741bcaa7a670a6467e6a8c3ad0d49f61363a0157e2832d57
BASE_UBI_SIZE=4456448
PREV_CONTAINER=/home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin
PREV_CONTAINER_SHA=8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8
PREV_CONTAINER_SIZE=7188799
KERNEL_SLICE_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
KERNEL_SLICE_SIZE=3372638
KERNEL_SLOT_SHA=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
MODULE_ACT=/home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.activity.ko
MODULE_ACT_SHA=545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
MODULE_ACT_SIZE=379092
MODULE_INST=/home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.installed.ko
MODULE_INST_SHA=a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed
MODULE_INST_SIZE=358544
MODULE_PARAMS=64
UBINIZE_CFG=$OWRT/target/linux/hisilicon/image/luofu-ubinize.cfg
UBINIZE=$OWRT/staging_dir/host/bin/ubinize
UBINIZE_Q=1003
IMAGE_EPOCH='2026-11-27 00:00:00 +0700'
IMAGE_EPOCH_SECS=1794002400
ART=/home/shibbityshwab/wifi-image-3-artifacts
WORK=$LANE/build-evidence/work
EVID=$LANE/build-evidence

die() { echo "bake-image-3: ERROR: $*" >&2; exit 1; }
log() { echo "bake-image-3: $*"; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
size() { stat -c %s "$1"; }

# ---- input assertions ------------------------------------------------------
[ "$(id -u)" -ne 0 ] || die "run as the normal user (the script uses sudo only where needed)"
sudo -n true 2>/dev/null || die "passwordless sudo is required for the faithful rootfs extraction (device nodes)"
for f in "$BASE_SQUASHFS" "$BASE_UBI" "$PREV_CONTAINER" "$MODULE_ACT" "$MODULE_INST" "$UBINIZE_CFG" "$UBINIZE"; do
	[ -f "$f" ] || die "input missing: $f"
done
command -v mksquashfs >/dev/null || die "mksquashfs missing"
command -v unsquashfs >/dev/null || die "unsquashfs missing"
[ -d "$LANE/payload/etc/hi5622" ] || die "lane payload missing"
for f in etc/config/hi5622 usr/sbin/hi5622-load etc/modprobe.d/hi5622-wifi.conf etc/init.d/hi5622-wifi etc/hi5622/hostapd-open-ax.conf etc/hi5622/hostapd-open-5g40.conf etc/hi5622/IMAGE-3-NOTES.txt; do
	[ -f "$LANE/payload/$f" ] || die "payload missing $f"
done

[ "$(sha "$BASE_SQUASHFS")" = "$BASE_SQUASHFS_SHA" ] || die "base squashfs sha changed: $(sha "$BASE_SQUASHFS")"
[ "$(size "$BASE_SQUASHFS")" = "$BASE_SQUASHFS_SIZE" ] || die "base squashfs size changed"
[ "$(sha "$BASE_UBI")" = "$BASE_UBI_SHA" ] || die "base UBI sha changed"
[ "$(size "$BASE_UBI")" = "$BASE_UBI_SIZE" ] || die "base UBI size changed"
[ "$(sha "$PREV_CONTAINER")" = "$PREV_CONTAINER_SHA" ] || die "image #2 container sha changed"
[ "$(size "$PREV_CONTAINER")" = "$PREV_CONTAINER_SIZE" ] || die "image #2 container size changed"
[ "$(sha "$MODULE_ACT")" = "$MODULE_ACT_SHA" ] || die "module activity sha changed"
[ "$(size "$MODULE_ACT")" = "$MODULE_ACT_SIZE" ] || die "module activity size changed"
[ "$(sha "$MODULE_INST")" = "$MODULE_INST_SHA" ] || die "module installed sha changed"
[ "$(size "$MODULE_INST")" = "$MODULE_INST_SIZE" ] || die "module installed size changed"
[ "$(modinfo -F name "$MODULE_INST")" = "hi5622_wifi" ] || die "module installed is not hi5622_wifi"
case "$(modinfo -F vermagic "$MODULE_INST")" in *6.18.60*) ;; *) die "module vermagic is not 6.18.60" ;; esac
case "$(modinfo -F depends "$MODULE_INST")" in *cfg80211*) ;; *) die "module does not depend on cfg80211" ;; esac
[ "$(modinfo -p "$MODULE_INST" | wc -l)" = "$MODULE_PARAMS" ] || die "module installed param count != $MODULE_PARAMS"

mkdir -p "$WORK" "$ART" "$EVID"
sudo rm -rf "$WORK/base" "$WORK/root" "$WORK/verify" "$WORK/ctr"
mkdir -p "$WORK/base" "$WORK/root" "$WORK/verify" "$WORK/ctr"
tar -xOf "$PREV_CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin"
[ "$(sha "$WORK/kernel.bin")" = "$KERNEL_SLICE_SHA" ] || die "image #2 container kernel slice sha changed"
[ "$(size "$WORK/kernel.bin")" = "$KERNEL_SLICE_SIZE" ] || die "image #2 container kernel slice size changed"

log "inputs verified (base squashfs $BASE_SQUASHFS_SHA, module $MODULE_INST_SHA, kernel $KERNEL_SLICE_SHA)"

# ---- 1. extract the verified wifi-image-update rootfs ----------------------
log "stage: extract the wifi-image-update root.squashfs (sudo, faithful)"
sudo unsquashfs -n -d "$WORK/root" "$BASE_SQUASHFS" > "$WORK/unsquashfs-base.log" 2>&1 || die "base squashfs extraction failed (see $WORK/unsquashfs-base.log)"
[ -f "$WORK/root/lib/modules/6.18.60/hi5622_wifi.ko" ] || die "base rootfs has no hi5622_wifi.ko"
[ "$(sha "$WORK/root/lib/modules/6.18.60/hi5622_wifi.ko")" = "b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e" ] || die "base rootfs module is not the image #2 installed copy"

python3 "$LANE/../wifi-image-integration/tools/extract-ubi-rootfs.py" "$BASE_UBI" -o "$WORK/base/rootfs.vol0.squashfs" > "$WORK/base-ubi-extract.txt" 2>&1 || die "base UBI extraction failed"
[ "$(sha "$WORK/base/rootfs.vol0.squashfs")" = "$BASE_SQUASHFS_SHA" ] || die "base UBI vol0 is not the base squashfs (base artifact inconsistency)"

# ---- 2. install the image #3 driver module --------------------------------
log "stage: install the IMAGE #3 module ($MODULE_INST_SIZE B $MODULE_INST_SHA, $MODULE_PARAMS params)"
sudo install -m 0644 -o root -g root "$MODULE_INST" "$WORK/root/lib/modules/6.18.60/hi5622_wifi.ko"
[ "$(sha "$WORK/root/lib/modules/6.18.60/hi5622_wifi.ko")" = "$MODULE_INST_SHA" ] || die "installed module sha mismatch"

# ---- 3. the image #3 text policy payload ----------------------------------
log "stage: text policy payload ($(cd "$LANE/payload" && find . -type f | wc -l) files)"
sudo cp -a "$LANE/payload/." "$WORK/root/"
sudo chmod 0755 "$WORK/root/usr/sbin/hi5622-load" "$WORK/root/etc/init.d/hi5622-wifi"
sudo chmod 0644 "$WORK/root/etc/config/hi5622" "$WORK/root/etc/modprobe.d/hi5622-wifi.conf"
sudo chmod 0644 "$WORK/root/etc/hi5622/hostapd-open-ax.conf" "$WORK/root/etc/hi5622/hostapd-open-5g40.conf" "$WORK/root/etc/hi5622/IMAGE-3-NOTES.txt"
sudo sh -c "cd '$WORK/root' && find . -type f -print0 | sort -z | xargs -0 sha256sum" > "$WORK/payload-manifest.txt"
log "payload: $(wc -l < "$WORK/payload-manifest.txt") files in the rootfs file manifest"

# ---- 4. rebuild the squashfs with the image's exact options ---------------
log "stage: mksquashfs (-comp xz -b 262144 -Xbcj arm -noappend)"
sudo mksquashfs "$WORK/root" "$WORK/root.squashfs" -comp xz -b 262144 -Xbcj arm -noappend -mkfs-time "$IMAGE_EPOCH_SECS" -all-time "$IMAGE_EPOCH_SECS" -quiet > "$WORK/mksquashfs.log" 2>&1 || die "mksquashfs failed (see $WORK/mksquashfs.log)"
SQ_SIZE=$(size "$WORK/root.squashfs"); SQ_SHA=$(sha "$WORK/root.squashfs")
log "root.squashfs: $SQ_SIZE B sha256 $SQ_SHA"
unsquashfs -s "$WORK/root.squashfs" > "$WORK/squashfs-superblock.txt" 2>&1
grep -q 'Compression xz' "$WORK/squashfs-superblock.txt" || die "new squashfs is not xz"
grep -q 'Dictionary size 262144' "$WORK/squashfs-superblock.txt" || die "new squashfs block size is not 262144"
grep -q 'Filters selected: arm' "$WORK/squashfs-superblock.txt" || die "new squashfs ARM BCJ filter missing"

# ---- 5. ubinize with the checked-in luofu geometry ------------------------
log "stage: ubinize (-p 131072 -m 2048 -s 2048 -O 2048, image-seq $UBINIZE_Q)"
sed "s|@ROOTFS@|$WORK/root.squashfs|" "$UBINIZE_CFG" > "$WORK/luofu-ubinize.cfg"
grep -q '@ROOTFS@' "$WORK/luofu-ubinize.cfg" && die "ubinize cfg placeholder not substituted"
"$UBINIZE" -Q "$UBINIZE_Q" -o "$WORK/rootfs.ubi" -p 131072 -m 2048 -s 2048 -O 2048 "$WORK/luofu-ubinize.cfg" > "$WORK/ubinize.log" 2>&1 || die "ubinize failed (see $WORK/ubinize.log)"
RS_SIZE=$(size "$WORK/rootfs.ubi"); RS_SHA=$(sha "$WORK/rootfs.ubi")
log "rootfs.ubi: $RS_SIZE B sha256 $RS_SHA"
python3 "$LANE/../wifi-image-integration/tools/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/verify/rootfs.vol0.squashfs" > "$WORK/ubi-extract.txt" 2>&1 || die "new UBI extraction failed"
[ "$(sha "$WORK/verify/rootfs.vol0.squashfs")" = "$SQ_SHA" ] || die "new UBI vol0 != new squashfs (round-trip failed)"
MTD14_SIZE=24379392
[ "$RS_SIZE" -le "$MTD14_SIZE" ] || die "rootfs UBI $RS_SIZE B does not fit mtd14 ($MTD14_SIZE B)"

# ---- 6. re-assemble the sysupgrade container around the UNCHANGED kernel --
log "stage: sysupgrade container (kernel UNCHANGED + the new root)"
mkdir -p "$WORK/ctr/sysupgrade-cudy_wr3000-v2"
tar -xf "$PREV_CONTAINER" -C "$WORK/ctr" sysupgrade-cudy_wr3000-v2/CONTROL sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt sysupgrade-cudy_wr3000-v2/fip-manifest.txt
cp -p "$WORK/kernel.bin" "$WORK/ctr/sysupgrade-cudy_wr3000-v2/kernel"
cp -p "$WORK/rootfs.ubi" "$WORK/ctr/sysupgrade-cudy_wr3000-v2/root"
[ "$(sha "$WORK/ctr/sysupgrade-cudy_wr3000-v2/kernel")" = "$KERNEL_SLICE_SHA" ] || die "kernel changed in container staging"
( cd "$WORK/ctr" && tar --mtime="@$(date -d "$IMAGE_EPOCH" +%s)" --owner=0 --group=0 --numeric-owner --sort=name -cf "$WORK/image-3-sysupgrade.bin" sysupgrade-cudy_wr3000-v2 )
CT_SIZE=$(size "$WORK/image-3-sysupgrade.bin"); CT_SHA=$(sha "$WORK/image-3-sysupgrade.bin")
log "container: $CT_SIZE B sha256 $CT_SHA"

# ---- 7. verify the built bytes -------------------------------------------
VERIFY_RC=0
if [ "$SKIP_VERIFY" -eq 0 ]; then
	log "verify: tools/verify-image-3.sh"
	"$HERE/verify-image-3.sh" --container "$WORK/image-3-sysupgrade.bin" --module-installed "$MODULE_INST" --module-activity "$MODULE_ACT" > "$WORK/verify.txt" 2>&1 || VERIFY_RC=$?
	cat "$WORK/verify.txt"
fi

# ---- 8. artifacts + evidence ---------------------------------------------
mkdir -p "$ART"
cp -f "$WORK/image-3-sysupgrade.bin" "$ART/image-3-sysupgrade.bin"
cp -f "$WORK/kernel.bin" "$ART/kernel-slice.bin"
cp -f "$WORK/rootfs.ubi" "$ART/rootfs.ubi"
cp -f "$WORK/root.squashfs" "$ART/root.squashfs"
cp -f "$WORK/root/lib/modules/6.18.60/hi5622_wifi.ko" "$ART/hi5622_wifi.installed.ko"
cp -f "$WORK/root/lib/modules/6.18.60/cfg80211.ko" "$ART/cfg80211.ko"
cp -f "$WORK/root/usr/sbin/hostapd" "$ART/hostapd"
( cd "$ART" && sha256sum image-3-sysupgrade.bin kernel-slice.bin rootfs.ubi root.squashfs hi5622_wifi.installed.ko cfg80211.ko hostapd > SHA256SUMS )
cp -f "$WORK/verify.txt" "$EVID/verify.txt" 2>/dev/null || true
cp -f "$WORK/squashfs-superblock.txt" "$EVID/squashfs-superblock.txt"
cp -f "$WORK/ubi-extract.txt" "$EVID/ubi-extract.txt"
cp -f "$WORK/base-ubi-extract.txt" "$EVID/base-ubi-extract.txt"
cp -f "$WORK/payload-manifest.txt" "$EVID/payload-manifest.txt"
cp -f "$WORK/mksquashfs.log" "$EVID/mksquashfs.log"
cp -f "$WORK/ubinize.log" "$EVID/ubinize.log"
{
	echo "# wifi-image-3 (task-196) build record"
	echo "build_start_utc=$BUILD_START"
	echo "build_end_utc=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	echo "base_image=wifi-image-update (image #2 + regdomain cfg80211 + ax hostapd)"
	echo "base_squashfs=$BASE_SQUASHFS_SHA ($BASE_SQUASHFS_SIZE B)"
	echo "base_ubi=$BASE_UBI_SHA ($BASE_UBI_SIZE B)"
	echo "prev_container=$PREV_CONTAINER_SHA ($PREV_CONTAINER_SIZE B)"
	echo "kernel_slice=$KERNEL_SLICE_SHA ($KERNEL_SLICE_SIZE B) UNCHANGED"
	echo "kernel_slot=$KERNEL_SLOT_SHA (not a container member; rootfs-only flash)"
	echo "module_activity=$MODULE_ACT_SHA ($MODULE_ACT_SIZE B, $MODULE_PARAMS params)"
	echo "module_installed=$MODULE_INST_SHA ($MODULE_INST_SIZE B, $MODULE_PARAMS params)"
	echo "root_squashfs=$SQ_SHA ($SQ_SIZE B)"
	echo "rootfs_ubi=$RS_SHA ($RS_SIZE B) fits mtd14=$MTD14_SIZE YES"
	echo "container=$CT_SHA ($CT_SIZE B)"
	echo "squashfs_mkfs_time=$IMAGE_EPOCH_SECS (pinned: reproducible squashfs bytes)"
	echo "ubinize_image_seq=$UBINIZE_Q"
	echo "container_member_epoch=$IMAGE_EPOCH"
	echo "verify_rc=$VERIFY_RC"
	echo "local_artifacts=$ART"
} > "$EVID/build-summary.txt"
{
	echo "# wifi-image-3 build runs (append-only)"
	echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) container=$CT_SHA rootfs_ubi=$RS_SHA squashfs=$SQ_SHA module_installed=$MODULE_INST_SHA verify_rc=$VERIFY_RC"
} >> "$EVID/build-runs.txt"
( cd "$ART" && sha256sum * ) > "$EVID/hashes.txt"
log "DONE: container $CT_SIZE B sha256 $CT_SHA"
log "      kernel slice UNCHANGED $KERNEL_SLICE_SHA"
log "      module installed $MODULE_INST_SIZE B sha256 $MODULE_INST_SHA ($MODULE_PARAMS params)"
log "      verify rc=$VERIFY_RC; evidence in $EVID; artifacts in $ART"
[ "$VERIFY_RC" -eq 0 ] || exit 1
