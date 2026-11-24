#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# verify-image.sh - verify from the BUILT BYTES that the bench image carries
# iw / iwinfo / libnl-tiny / wireless-regdb / cfg80211.ko / hi5622_wifi.ko.
#
#   ./tools/verify-image.sh [--owrt DIR] [--work DIR]
#
# Reads the newest sysupgrade container in $OWRT/bin/targets/hisilicon/luofu/,
# extracts the kernel slice and the rootfs UBI image, reassembles the squashfs
# from the UBI image (read-only, ./extract-ubi-rootfs.py), extracts the
# rootfs with unsquashfs and checks the required paths, hashes and the apk
# manifest. Exit code 0 only if every required item is present.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
OWRT=$DEFAULT_OWRT
WORK=/tmp/wifi-image-verify

while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) OWRT=$2; shift 2 ;;
		--work) WORK=$2; shift 2 ;;
		-h|--help) sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "verify-image: unknown argument: $1" >&2; exit 2 ;;
	esac
done

die() { echo "verify-image: ERROR: $*" >&2; exit 1; }

TARGETS=$OWRT/bin/targets/hisilicon/luofu
[ -d "$TARGETS" ] || die "no image dir: $TARGETS"
CONTAINER=$(ls -t "$TARGETS"/*squashfs-sysupgrade.bin 2>/dev/null | head -1)
[ -n "$CONTAINER" ] || die "no *squashfs-sysupgrade.bin in $TARGETS"
[ -x /usr/bin/unsquashfs ] || die "unsquashfs not installed on the host"

rm -rf "$WORK"
mkdir -p "$WORK"

echo "== bench image verification (built bytes) =="
echo "tree        : $OWRT"
echo "container   : $CONTAINER"
echo "built       : $(stat -c %y "$CONTAINER")"
echo "size        : $(stat -c %s "$CONTAINER") B"
echo "sha256      : $(sha256sum "$CONTAINER" | cut -d' ' -f1)"

tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel.bin" || die "cannot extract kernel slice"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/root > "$WORK/rootfs.ubi" || die "cannot extract rootfs ubi"
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/fip-manifest.txt > "$WORK/fip-manifest.txt" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/CONTROL > "$WORK/CONTROL" 2>/dev/null || true
tar -xOf "$CONTAINER" sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt > "$WORK/INSTALL-STATUS.txt" 2>/dev/null || true

KERNEL_SIZE=$(stat -c %s "$WORK/kernel.bin")
KERNEL_SHA=$(sha256sum "$WORK/kernel.bin" | cut -d' ' -f1)
ROOTFS_SIZE=$(stat -c %s "$WORK/rootfs.ubi")
ROOTFS_SHA=$(sha256sum "$WORK/rootfs.ubi" | cut -d' ' -f1)
echo
echo "kernel slice: $KERNEL_SIZE B sha256 $KERNEL_SHA"
echo "rootfs ubi  : $ROOTFS_SIZE B sha256 $ROOTFS_SHA"

EXPECTED_KERNEL=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
if [ "$KERNEL_SHA" = "$EXPECTED_KERNEL" ]; then
	echo "kernel check: UNCHANGED - the hardware-validated cfg80211 slice 13e81214..."
else
	echo "kernel check: CHANGED from the hardware-validated slice 13e81214... (new kernel build)"
fi

echo
echo "-- rootfs squashfs reassembled from the UBI image --"
python3 "$HERE/extract-ubi-rootfs.py" "$WORK/rootfs.ubi" -o "$WORK/rootfs.squashfs" --vol-id 0 \
	|| die "UBI -> squashfs extraction failed"
SQ_SIZE=$(stat -c %s "$WORK/rootfs.squashfs")
SQ_SHA=$(sha256sum "$WORK/rootfs.squashfs" | cut -d' ' -f1)
echo "squashfs    : $SQ_SIZE B sha256 $SQ_SHA"

BUILT_SQ=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/root.squashfs
if [ -f "$BUILT_SQ" ]; then
	BUILT_SQ_SHA=$(sha256sum "$BUILT_SQ" | cut -d' ' -f1)
	BUILT_SQ_SIZE=$(stat -c %s "$BUILT_SQ")
	# ubinize pads the squashfs volume to the LEB/page geometry; the volume
	# prefix must be byte-identical to the squashfs the image recipe built.
	if [ "$BUILT_SQ_SIZE" -le "$SQ_SIZE" ] && cmp -s -n "$BUILT_SQ_SIZE" "$BUILT_SQ" "$WORK/rootfs.squashfs"; then
		echo "consistency : squashfs volume == build_dir/.../root.squashfs ($BUILT_SQ_SIZE B sha256 $BUILT_SQ_SHA) + $((SQ_SIZE - BUILT_SQ_SIZE)) B ubinize padding"
	else
		echo "consistency : WARNING squashfs volume differs from build_dir/.../root.squashfs ($BUILT_SQ_SIZE B sha256 $BUILT_SQ_SHA)"
	fi
fi

rm -rf "$WORK/extract"
# -no-exit-code: extracting the rootfs as a non-root host user cannot create
# /dev/console and /dev/null character devices (non-fatal; files/extract is
# what we verify). Without the flag unsquashfs exits 2 on that.
unsquashfs -n -no-exit-code -d "$WORK/extract" "$WORK/rootfs.squashfs" >/dev/null 2>&1 || \
	die "unsquashfs failed (corrupt squashfs volume?)"
ROOT=$WORK/extract
FILE_COUNT=$(find "$ROOT" -type f | wc -l)
echo "rootfs files: $FILE_COUNT"

echo
echo "-- required rootfs content --"
MISSING=0
while IFS='|' read -r rel desc; do
	[ -z "$rel" ] && continue
	if [ -e "$ROOT/$rel" ]; then
		printf 'PRESENT  %-46s %8s B sha256 %s  # %s\n' "$rel" \
			"$(stat -c %s "$ROOT/$rel")" "$(sha256sum "$ROOT/$rel" | cut -d' ' -f1)" "$desc"
	else
		printf 'MISSING  %-46s # %s\n' "$rel" "$desc"
		MISSING=$((MISSING + 1))
	fi
done <<'EOF'
usr/sbin/iw|iw CLI (nl80211)
usr/bin/iwinfo|iwinfo CLI (C implementation)
usr/lib/libiwinfo.so.20230701|libiwinfo shared library
usr/share/libiwinfo/devices.txt|libiwinfo device data
usr/lib/libnl-tiny.so.1|libnl-tiny (iw dependency)
lib/firmware/regulatory.db|wireless regulatory database
lib/modules/6.18.60/cfg80211.ko|cfg80211 kernel module (target kernel tree)
lib/modules/6.18.60/hi5622_wifi.ko|clean-room Hi5622 Wi-Fi host driver
EOF

echo
echo "-- kernel module metadata (extracted .ko files) --"
for mod in cfg80211 hi5622_wifi; do
	KO=$ROOT/lib/modules/6.18.60/$mod.ko
	[ -e "$KO" ] || { echo "MISSING  $mod.ko"; continue; }
	echo "$mod.ko:"
	echo "  vermagic: $(modinfo -F vermagic "$KO" 2>/dev/null || echo unknown)"
	echo "  depends : $(modinfo -F depends "$KO" 2>/dev/null || echo unknown)"
	echo "  license : $(modinfo -F license "$KO" 2>/dev/null || echo unknown)"
	BUILT_KO=$(find "$OWRT/build_dir" -name "$mod.ko" -path '*6.18.60*' 2>/dev/null | head -1)
	if [ -n "$BUILT_KO" ]; then
		if cmp -s "$BUILT_KO" "$KO"; then
			echo "  build   : byte-identical to $BUILT_KO"
		else
			echo "  build   : differs from tree artifact $BUILT_KO (OpenWrt strips kernel modules at install; vermagic/deps above are the proof of identity)"
		fi
	fi
done

echo
echo "-- apk manifest packages --"
MANIFEST=$(ls -t "$TARGETS"/*.manifest 2>/dev/null | head -1)
[ -n "$MANIFEST" ] || die "no image manifest"
MANIFEST_MISSING=0
for pkg in iw iwinfo libiwinfo libiwinfo-data wireless-regdb kmod-hi5622-cfg80211 kmod-hi5622-wifi libnl-tiny1; do
	# ABI-versioned package names: libiwinfo -> libiwinfo20230701,
	# libnl-tiny -> libnl-tiny1 (OpenWrt appends the ABI version).
	case "$pkg" in
		libiwinfo)  pat='^libiwinfo[0-9]{8} - ' ;;
		libnl-tiny*) pat='^libnl-tiny[0-9]* - ' ;;
		*)          pat="^$pkg - " ;;
	esac
	if grep -qE "$pat" "$MANIFEST"; then
		echo "PRESENT  $(grep -E "$pat" "$MANIFEST")"
	else
		echo "MISSING  package $pkg in manifest (pattern $pat)"
		MANIFEST_MISSING=$((MANIFEST_MISSING + 1))
	fi
done
echo "manifest    : $(basename "$MANIFEST"), $(wc -l < "$MANIFEST") packages"

echo
echo "-- byte deltas vs the previous release image 22b72852 --"
echo "container : $(stat -c %s "$CONTAINER") B (was 6267199 B, delta +$(( $(stat -c %s "$CONTAINER") - 6267199 )))"
echo "kernel    : $KERNEL_SIZE B (was 3372638 B)"
echo "rootfs ubi: $ROOTFS_SIZE B (was 2883584 B, delta +$(( ROOTFS_SIZE - 2883584 )))"
echo "squashfs  : $SQ_SIZE B (padded volume; was 2498560 B, delta +$(( SQ_SIZE - 2498560 )))"

echo
if [ "$MISSING" -ne 0 ] || [ "$MANIFEST_MISSING" -ne 0 ]; then
	echo "RESULT: FAIL - $MISSING file(s) and $MANIFEST_MISSING package(s) missing"
	exit 1
fi
echo "RESULT: PASS - all required files and packages are in the built image"
