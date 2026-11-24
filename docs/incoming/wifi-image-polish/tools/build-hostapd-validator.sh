#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# build-hostapd-validator.sh - build an x86_64 hostapd v2.12 from the SAME
# source tarball the OpenWrt bench tree uses, for CONFIG-FILE VALIDATION of
# this lane's fixtures.  It is not a target artifact and nothing is deployed.
#
# Why a lane-local build: the tree-source hostapd build used by the
# wifi-hostapd-deploy lane is configured without CONFIG_SAE, so it cannot
# parse the WPA3-SAE transition fixture ("invalid key_mgmt 'SAE'").  The image
# ships hostapd-mini, which DOES support SAE (evidence: the image binary
# contains the sae_require_mfp/sae_pwe/sae_anti_clogging_threshold config
# strings); this build enables CONFIG_SAE + CONFIG_IEEE80211W so the parse gate
# exercises the same config surface as the target userspace.
#
# usage: ./tools/build-hostapd-validator.sh [--force]
#   prints the binary path on stdout; build log in <out>/host-build.log
#   env: OWRT (tree with dl/hostapd-*.tar.zst), HOSTAPD_VALIDATOR_OUT
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
OWRT=${OWRT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}
OUT=${HOSTAPD_VALIDATOR_OUT:-/tmp/wifi-image-polish-hostapd}
FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1
BIN=$OUT/hostapd/hostapd
if [ -x "$BIN" ] && [ "$FORCE" -eq 0 ]; then
	printf '%s\n' "$BIN"
	exit 0
fi
TARBALL=$(ls "$OWRT"/dl/hostapd-*.tar.zst 2>/dev/null | head -n1)
if [ -z "$TARBALL" ]; then
	echo "build-hostapd-validator: no hostapd source tarball under $OWRT/dl" >&2
	echo "  (fetch with: make -C $OWRT package/network/services/hostapd/download)" >&2
	exit 1
fi
command -v zstd >/dev/null 2>&1 || { echo "build-hostapd-validator: zstd missing" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
{
	echo "source tarball: $TARBALL"
	echo "tarball sha256: $(sha256sum "$TARBALL" | cut -d' ' -f1)"
	echo "host gcc      : $(cc --version 2>/dev/null | head -n1)"
	zstd -dc "$TARBALL" | tar -x -C "$OUT"
} >"$OUT/host-build.log" 2>&1 || { echo "build-hostapd-validator: extract failed" >&2; exit 1; }
SRC_DIR=$(ls -d "$OUT"/hostapd-2026.08.07* 2>/dev/null | head -n1)
[ -n "$SRC_DIR" ] && [ -d "$SRC_DIR/hostapd" ] || { echo "build-hostapd-validator: unexpected tarball layout" >&2; exit 1; }
cd "$SRC_DIR/hostapd"
cp defconfig .config
{
	echo 'CONFIG_DRIVER_WIRED=y'
	echo 'CONFIG_DRIVER_NONE=y'
	echo 'CONFIG_DEBUG_FILE=y'
	echo 'CONFIG_SAE=y'
	echo 'CONFIG_IEEE80211W=y'
} >>.config
if ! make -j"$(nproc 2>/dev/null || echo 4)" hostapd >>"$OUT/host-build.log" 2>&1; then
	echo "build-hostapd-validator: build failed (see $OUT/host-build.log)" >&2
	tail -20 "$OUT/host-build.log" >&2
	exit 1
fi
[ -x "$BIN" ] || { echo "build-hostapd-validator: binary not produced" >&2; exit 1; }
ver=$("$BIN" -v 2>&1 | head -n1)
case "$ver" in
	*"hostapd v2.12"*) ;;
	*) echo "build-hostapd-validator: unexpected version: $ver" >&2; exit 1 ;;
esac
printf '%s\n' "$BIN"
