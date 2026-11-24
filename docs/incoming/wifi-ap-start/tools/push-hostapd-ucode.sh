#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/push-hostapd-ucode.sh - stage/deploy the OpenWrt wifi-scripts ucode
# files the bench image's hostapd-mini needs at /usr/share/hostap/.
#
# The bench image shipped hostapd-mini without the wifi-scripts package, so
# hostapd exits with:
#   Error loading ucode: Unable to resolve path for module 'common'
# Pushing common.uc / wdev.uc / wifi-detect.uc (from the OpenWrt source tree)
# fixes hostapd's ucode bootstrap.  BENCH WORKAROUND ONLY - the image should
# carry the wifi-scripts package (see ../image/wifi-scripts-ucode.md).
#
# Default: stage the files under /tmp/hostapd-ucode/ and print the exact
# commands (no device contact).  --apply additionally copies them to the
# router over SSH with tools/rscp.sh.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$LANE_DIR/../../../../.." && pwd)   # repo root
SRC=/home/shibbityshwab/openwrt-lab/package/network/config/wifi-scripts/files/usr/share/hostap
STAGE=/tmp/hostapd-ucode
APPLY=0
HOST=root@192.168.1.1
set +u
if [ -n "$WIFI_SCRIPTS_DIR" ]; then SRC=$WIFI_SCRIPTS_DIR; fi
if [ -n "$ROUTER_HOST" ]; then HOST=$ROUTER_HOST; fi
if [ -n "$1" ] && [ "$1" = "--apply" ]; then APPLY=1; fi
set -u

FILES="common.uc wdev.uc wifi-detect.uc"

for f in $FILES; do
	[ -f "$SRC/$f" ] || { echo "missing $SRC/$f (set WIFI_SCRIPTS_DIR)" >&2; exit 2; }
done

rm -rf "$STAGE"
mkdir -p "$STAGE"
for f in $FILES; do cp "$SRC/$f" "$STAGE/$f"; done
chmod 0644 "$STAGE"/*.uc

echo "== staged wifi-scripts ucode files"
ls -l "$STAGE"
sha256sum "$STAGE"/*.uc

echo
echo "== deploy command (bench workaround; attended session)"
echo "  $ROOT/tools/rsh.sh $HOST 'mkdir -p /usr/share/hostap'"
for f in $FILES; do
	echo "  $ROOT/tools/rscp.sh $STAGE/$f $HOST:/usr/share/hostap/$f"
done
echo "  $ROOT/tools/rsh.sh $HOST 'chmod 0644 /usr/share/hostap/*.uc; ls -l /usr/share/hostap'"

if [ "$APPLY" = "1" ]; then
	echo
	echo "== applying (attended)"
	"$ROOT/tools/rsh.sh" "$HOST" 'mkdir -p /usr/share/hostap'
	for f in $FILES; do
		"$ROOT/tools/rscp.sh" "$STAGE/$f" "$HOST:/usr/share/hostap/$f"
	done
	"$ROOT/tools/rsh.sh" "$HOST" 'chmod 0644 /usr/share/hostap/*.uc; ls -l /usr/share/hostap'
	echo "push-hostapd-ucode: deployed (bench workaround; image fix is the real solution)"
else
	echo
	echo "push-hostapd-ucode: staged only (pass --apply in the attended session)"
fi
