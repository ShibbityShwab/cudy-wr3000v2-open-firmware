#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-host-hostapd.sh - build an x86_64 hostapd from the SAME source tarball
# the OpenWrt bench-image tree uses, purely to validate the bench config
# fixtures on the host (same hostapd version + config parser).  It is NOT a
# target artifact: nothing here is deployed to the router, and the bench
# package remains the OpenWrt cross build from ../BUILD-NOTES.md.
#
# The hostapd source tarball is the tree's prefetched dl/ download
# (hostapd-<date>-<hash>.tar.zst); no network access is needed.  The host build
# is configured with wired/none drivers only: the nl80211 driver is not needed
# for config-file validation, and the upstream offer/supplicant config
# validity gate is the config parser itself.
#
# usage: ./tools/build-host-hostapd.sh [--owrt DIR] [--out DIR] [--force]
#   prints the built binary path on stdout; build log in <out>/host-build.log
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
OUT=${HOSTAPD_HOST_OUT:-/tmp/hostapd-host}
FORCE=0
while [ $# -gt 0 ]; do
	case "$1" in
		--owrt) shift; OWRT=$1 ;;
		--owrt=*) OWRT=${1#*=} ;;
		--out) shift; OUT=$1 ;;
		--out=*) OUT=${1#*=} ;;
		--force) FORCE=1 ;;
		-h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
BIN="$OUT/src/hostapd/hostapd"
if [ -x "$BIN" ] && [ "$FORCE" -eq 0 ]; then
	printf '%s\n' "$BIN"
	exit 0
fi
TARBALL=$(ls "$OWRT"/dl/hostapd-*.tar.zst 2>/dev/null | head -n1)
if [ -z "$TARBALL" ]; then
	echo "build-host-hostapd: no hostapd source tarball under $OWRT/dl" >&2
	echo "  (fetch it with: make -C $OWRT package/network/services/hostapd/download)" >&2
	exit 1
fi
MIRROR_SHA=$(sha256sum "$TARBALL" | awk '{print $1}')
rm -rf "$OUT"
mkdir -p "$OUT"
{
	echo "source tarball: $TARBALL"
	echo "source sha256 : $MIRROR_SHA"
	echo "host gcc      : $(cc --version 2>/dev/null | head -n1)"
	echo "hostapd ver   : $(tar --use-compress-program=zstd -tf "$TARBALL" 2>/dev/null | head -n1 || true)"
	zstd -dc "$TARBALL" | tar -x -C "$OUT"
} >"$OUT/host-build.log" 2>&1 || { echo "build-host-hostapd: extract failed (see $OUT/host-build.log)" >&2; exit 1; }
SRC_DIR=$(ls -d "$OUT"/hostapd-2026.08.07* 2>/dev/null | head -n1)
[ -n "$SRC_DIR" ] && [ -d "$SRC_DIR" ] || { echo "build-host-hostapd: unexpected tarball layout" >&2; exit 1; }
cp -a "$SRC_DIR" "$OUT/src"
cd "$OUT/src/hostapd" || exit 1
cp defconfig .config
{
	echo 'CONFIG_DRIVER_WIRED=y'
	echo 'CONFIG_DRIVER_NONE=y'
	echo 'CONFIG_DEBUG_FILE=y'
} >> .config
if ! make -j"$(nproc 2>/dev/null || echo 4)" hostapd >>"$OUT/host-build.log" 2>&1; then
	echo "build-host-hostapd: build failed (see $OUT/host-build.log)" >&2
	tail -20 "$OUT/host-build.log" >&2
	exit 1
fi
[ -x "$BIN" ] || { echo "build-host-hostapd: binary not produced" >&2; exit 1; }
printf '%s\n' "$BIN"