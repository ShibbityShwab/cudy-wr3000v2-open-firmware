#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# stage-payload.sh - stage/remove this lane's rootfs policy payload into an
# OpenWrt tree's files/ overlay (host-only; never touches a device).
#
#   ./tools/stage-payload.sh --tree DIR [--apply] [--manifest FILE]
#   ./tools/stage-payload.sh --tree DIR --remove [--manifest FILE]
#
# Payload (payload/ mirrors the target root filesystem):
#   etc/config/hi5622                  uci policy (autoload=0 bench default)
#   etc/init.d/hi5622-wifi             rc.common service (START=96)
#   etc/rc.d/S96hi5622-wifi -> link    enable symlink (created here)
#   etc/modprobe.d/hi5622-wifi.conf    packaged-defaults mirror
#   etc/hi5622/hostapd-*.conf          AP fixtures (open proven/open/wpa2/...)
#   etc/hi5622/FIRMWARE-POLICY.txt     manual-push firmware policy
#   usr/sbin/hi5622-load               explicit-insmod one-shot loader
#
# The payload is TEXT ONLY.  Firmware/calibration bytes and .ko files are
# refused by design (they are local evidence, never packaged).
#
# --apply copies into DIR/files/ and writes a manifest (rel path + sha256).
# --remove deletes exactly the manifest paths plus the rc.d symlink, then
# prunes any now-empty directories it created (never a blanket rm -rf).
set -euo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOTFS=$LANE/payload
TREE=
APPLY=0
REMOVE=0
MANIFEST=

die() { echo "stage-payload: ERROR: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case "$1" in
		--tree) TREE=${2:-}; shift 2 ;;
		--tree=*) TREE=${1#*=}; shift ;;
		--apply) APPLY=1; shift ;;
		--remove) REMOVE=1; shift ;;
		--manifest) MANIFEST=${2:-}; shift 2 ;;
		--manifest=*) MANIFEST=${1#*=}; shift ;;
		-h|--help) sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) die "unknown argument: $1" ;;
	esac
done
[ -n "$TREE" ] || die "--tree is required"
[ -d "$TREE" ] || die "not a directory: $TREE"
[ -f "$TREE/Makefile" ] && [ -d "$TREE/package" ] || 	die "$TREE does not look like an OpenWrt tree (Makefile+package/)"
[ "$APPLY" -eq 1 ] && [ "$REMOVE" -eq 1 ] && die "--apply and --remove are exclusive"

TARGET=$TREE/files
[ -n "$MANIFEST" ] || MANIFEST=$LANE/build-evidence/work/payload-manifest.txt
REL_LINK=etc/rc.d/S96hi5622-wifi

# ---------------------------------------------------------------------------
# remove mode
# ---------------------------------------------------------------------------
if [ "$REMOVE" -eq 1 ]; then
	[ -f "$MANIFEST" ] || { echo "stage-payload: no manifest at $MANIFEST - nothing to remove"; exit 0; }
	echo "removing payload staged by manifest: $MANIFEST"
	while IFS=' ' read -r rel _sha; do
		[ -n "$rel" ] || continue
		case "$rel" in
			/*|*..*) die "refusing suspicious manifest path: $rel" ;;
		esac
		if [ -e "$TARGET/$rel" ] || [ -L "$TARGET/$rel" ]; then
			rm -f "$TARGET/$rel"
			echo "removed $rel"
		fi
		# prune empty parent dirs (only up to files/, never the tree root)
		dir=$(dirname "$TARGET/$rel")
		while [ "$dir" != "$TARGET" ] && [ -d "$dir" ] && [ -z "$(ls -A "$dir" 2>/dev/null)" ]; do
			rmdir "$dir"
			dir=$(dirname "$dir")
		done
	done <"$MANIFEST"
	if [ -L "$TARGET/$REL_LINK" ] || [ -e "$TARGET/$REL_LINK" ]; then
		rm -f "$TARGET/$REL_LINK"
		echo "removed $REL_LINK"
	fi
	if [ -d "$TARGET" ]; then
		# prune every now-empty directory (rc.d/etc parents left by the link)
		find "$TARGET" -mindepth 1 -depth -type d -empty -delete 2>/dev/null || true
	fi
	if [ -d "$TARGET" ] && [ -z "$(ls -A "$TARGET" 2>/dev/null)" ]; then
		rmdir "$TARGET"
		echo "removed empty files/ overlay"
	fi
	exit 0
fi

# ---------------------------------------------------------------------------
# stage mode: policy guard - text only, no firmware/module binaries
# ---------------------------------------------------------------------------
[ -d "$ROOTFS" ] || die "missing payload dir: $ROOTFS"
BAD=$(find "$ROOTFS" -type f \
	\( -name 'FIRMWARE*.bin' -o -name 'wifi_block*.bin' -o -name '*.bin' \
	    -o -name '*.ko' -o -name '*.o' -o -name '*.fw' \) -print)
[ -n "$BAD" ] && { echo "REFUSING - binary payload files:" >&2; printf '%s\n' "$BAD" >&2; exit 1; }
while IFS= read -r f; do
	if head -c 4 "$f" 2>/dev/null | grep -q $'\x7fELF'; then
		die "REFUSING - ELF binary in payload: $f"; fi
done < <(find "$ROOTFS" -type f)

if [ -d "$TARGET" ] && [ -n "$(ls -A "$TARGET" 2>/dev/null)" ]; then
	die "$TARGET already exists and is not empty; refusing to mix overlays (remove it first)"
fi

echo "payload      : $ROOTFS"
echo "openwrt tree : $TREE"
echo "overlay      : $TARGET"
echo "mode         : $([ "$APPLY" -eq 1 ] && echo APPLY || echo dry-run)"
echo
mkdir -p "$(dirname "$MANIFEST")"
: >"$MANIFEST.$$"
find "$ROOTFS" -type f | sort | while read -r src; do
	rel=${src#"$ROOTFS/"}
	sha=$(sha256sum "$src" | cut -d' ' -f1)
	printf '%s %s\n' "$rel" "$sha" >>"$MANIFEST.$$"
	echo "file $rel  sha256=$sha"
	if [ "$APPLY" -eq 1 ]; then
		mkdir -p "$(dirname "$TARGET/$rel")"
		cp -a "$src" "$TARGET/$rel"
	fi
done

echo "link $REL_LINK -> ../init.d/hi5622-wifi"
if [ "$APPLY" -eq 1 ]; then
	mkdir -p "$TARGET/etc/rc.d"
	ln -sfn ../init.d/hi5622-wifi "$TARGET/$REL_LINK"
	mv "$MANIFEST.$$" "$MANIFEST"
	echo
	echo "APPLIED - payload staged; next: the image lane's build (tools/bake-final-image.sh)"
else
	rm -f "$MANIFEST.$$"
	echo
	echo "DRY RUN - nothing written. Re-run with --apply."
fi
