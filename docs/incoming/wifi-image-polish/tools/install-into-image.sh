#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# install-into-image.sh - stage this lane's rootfs payload + kmod policy into
# an OpenWrt build tree's files/ overlay (host-only; no image build here, no
# device contact).
#
# The payload lives in ./rootfs/ and mirrors the target root filesystem:
#   etc/config/hi5622                     uci policy (autoload=0 bench default)
#   etc/init.d/hi5622-wifi                rc.common service (START=96)
#   etc/rc.d/S96hi5622-wifi -> init link  creates the symlink when staging
#   etc/modprobe.d/hi5622-wifi.conf       packaged defaults mirror
#   etc/hi5622/hostapd-*.conf             hardened AP configs
#   etc/hi5622/FIRMWARE-POLICY.txt        manual-push firmware policy
#   usr/sbin/hi5622-load                  explicit-insmod one-shot loader
#
# usage:
#   ./tools/install-into-image.sh --tree ~/openwrt-lab-selectorfix-img
#       dry run: prints every target path + sha256 and the symlink action
#   ./tools/install-into-image.sh --tree DIR --apply [--force]
#       copies into DIR/files/ (the standard OpenWrt rootfs overlay)
#   options:
#     --module PATH   verify the staged hi5622_wifi.ko against the pinned
#                     identities (44c6cc12 fired prebuilt, d145a205 polished
#                     NOT fired) and print its vermagic
#     --force         overwrite differing files in files/ (default: refuse)
#
# The kmod itself is installed by the image-integration lane's package
# (docs/incoming/wifi-image-integration/tools/install-into-tree.sh --driver-ko)
# or by its own staged prebuilt; this script only adds the lane's policy +
# userspace payload.  Firmware binaries are refused by design.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=$LANE_DIR/rootfs
TREE=
APPLY=0
FORCE=0
MODULE=
while [ $# -gt 0 ]; do
	case "$1" in
		--tree) shift; TREE=${1:-} ;;
		--tree=*) TREE=${1#*=} ;;
		--module) shift; MODULE=${1:-} ;;
		--module=*) MODULE=${1#*=} ;;
		--apply) APPLY=1 ;;
		--force) FORCE=1 ;;
		-h|--help) sed -n '3,31p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "install-into-image: unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
[ -n "$TREE" ] || { echo "install-into-image: --tree is required" >&2; exit 2; }
[ -d "$TREE" ] || { echo "install-into-image: not a directory: $TREE" >&2; exit 2; }
[ -f "$TREE/Makefile" ] && [ -d "$TREE/package" ] || {
	echo "install-into-image: $TREE does not look like an OpenWrt tree (Makefile+package/)" >&2
	exit 2
}
TARGET=$TREE/files

# --- policy guard: no firmware/calibration bytes ever staged -----------------
BAD=$(find "$ROOTFS" -type f \( -name 'FIRMWARE*.bin' -o -name 'wifi_block*.bin' \
	-o -name '*.bin' -o -name '*.ko' -o -name '*.fw' \) -print)
if [ -n "$BAD" ]; then
	echo "install-into-image: REFUSING - firmware/module binaries in the payload:" >&2
	printf '%s\n' "$BAD" >&2
	exit 1
fi
[ -d "$ROOTFS" ] || { echo "install-into-image: missing payload dir $ROOTFS" >&2; exit 2; }

echo "lane payload : $ROOTFS"
echo "openwrt tree : $TREE"
echo "overlay      : $TARGET"
echo "mode         : $([ "$APPLY" -eq 1 ] && echo APPLY || echo dry-run)"
echo

# --- optional module identity check -----------------------------------------
if [ -n "$MODULE" ]; then
	[ -f "$MODULE" ] || { echo "install-into-image: module not found: $MODULE" >&2; exit 2; }
	SIZE=$(stat -c %s "$MODULE")
	SHA=$(sha256sum "$MODULE" | cut -d' ' -f1)
	VERMAGIC=$(strings -a "$MODULE" | grep -m1 '^vermagic=' || true)
	echo "module       : $MODULE"
	echo "  size       : $SIZE"
	echo "  sha256     : $SHA"
	echo "  $VERMAGIC"
	case "$SIZE:$SHA" in
	"242572:44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b")
		echo "  status     : hardware-fired prebuilt artifact (safe: validated on the bench)" ;;
	"242920:d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6")
		echo "  status     : POLISHED host-built artifact - NOT fired; one attended fire required before deployment" ;;
	*)
		echo "install-into-image: WARNING - unstaged/unknown module identity; not a pinned artifact" >&2 ;;
	esac
	echo
fi

# --- stage files ------------------------------------------------------------
FAIL=0
TMPLIST=$(mktemp /tmp/hi5622-payload-files.XXXXXX)
trap 'rm -f "$TMPLIST"' EXIT INT TERM
find "$ROOTFS" -type f | sort >"$TMPLIST"
while read -r src; do
	rel=${src#"$ROOTFS/"}
	dst="$TARGET/$rel"
	sha=$(sha256sum "$src" | cut -d' ' -f1)
	if [ -e "$dst" ] && ! cmp -s "$src" "$dst" && [ "$FORCE" -eq 0 ]; then
		echo "DIFF $rel (existing differs; use --force to overwrite)" >&2
		FAIL=1
		continue
	fi
	echo "file $rel  sha256=$sha"
	if [ "$APPLY" -eq 1 ]; then
		mkdir -p "$(dirname "$dst")"
		cp -a "$src" "$dst"
	fi
done <"$TMPLIST"

# the enable symlink is created here (a files/ overlay does not auto-enable)
REL_LINK=etc/rc.d/S96hi5622-wifi
echo "link $REL_LINK -> ../init.d/hi5622-wifi"
if [ "$APPLY" -eq 1 ]; then
	mkdir -p "$TARGET/etc/rc.d"
	ln -sfn ../init.d/hi5622-wifi "$TARGET/$REL_LINK"
fi

echo
if [ "$APPLY" -eq 1 ]; then
	echo "APPLIED - next steps (image lane owns the build/flash; this script never touches a device):"
	echo "  1. install the kmod with docs/incoming/wifi-image-integration/tools/install-into-tree.sh --driver-ko <ko>"
	echo "  2. rebuild the rootfs-only image (docs/incoming/wifi-image-integration/tools/build-image.sh)"
	echo "  3. after flashing: push PD4 firmware + wifi_block manually (see PACKAGING.md)"
	echo "     then /etc/init.d/hi5622-wifi start (bench policy) or enable autoload for production."
else
	echo "DRY RUN - nothing written. Re-run with --apply to stage into $TARGET."
fi
exit "$FAIL"
