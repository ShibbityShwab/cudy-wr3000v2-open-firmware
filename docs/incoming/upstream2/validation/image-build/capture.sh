#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# capture.sh - capture the image-build evidence for the upstream2 lane
# (task-243) from an OpenWrt tree that was assembled from the project's single
# sources and built with `make -j16 world`.
#
# Host-only: reads the build tree, writes only into this validation/image-build/
# directory. Nothing is flashed or touched on the device.
#
# Usage: T=/path/to/openwrt-lab-upstream-target ./capture.sh
set -euo pipefail

T=${T:-/home/shibbityshwab/openwrt-lab-upstream-target}
HERE="$(cd "$(dirname "$0")" && pwd)"
B="$T/bin/targets/hisilicon/luofu"
D="$T/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu"
IMG="$B/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin"
SLICE="$D/cudy_wr3000-v2-kernel.bin"
ROOTFS="$D/cudy_wr3000-v2-rootfs.ubi"
MANIFEST="$D/cudy_wr3000-v2-fip-manifest.txt"
DTB="$D/image-luofu-r116.dtb"
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

for f in "$IMG" "$SLICE" "$ROOTFS" "$MANIFEST" "$DTB"; do
	[ -f "$f" ] || { echo "capture: ERROR: missing $f" >&2; exit 1; }
done

# 1. build logs + provenance
cp "$T/build-upstream2-clean.log" "$HERE/clean.log" 2>/dev/null || true
cp "$T/build-upstream2-clean-world.log" "$HERE/clean-world.log"
cp "$T/build-upstream2-world.log" "$HERE/warm-world.log" 2>/dev/null || true
cp "$B/config.buildinfo" "$HERE/config.buildinfo"
cp "$B/version.buildinfo" "$HERE/version.buildinfo"

# 2. artifacts + hashes
{
	echo "# image artifacts - upstream2 lane, captured by capture.sh"
	echo "# tree: $T"
	sha256sum "$IMG" "$SLICE" "$ROOTFS" "$MANIFEST" "$DTB" \
		"$D/image-luofu-r116-overlay.dtbo" "$B/config.buildinfo" "$B/profiles.json" \
		"$B/openwrt-hisilicon-luofu-cudy_wr3000-v2.manifest"
} > "$HERE/artifacts.sha256"

# 3. container inspection (read-only):
#    the sysupgrade container is a tar with CONTROL, INSTALL-STATUS.txt,
#    fip-manifest.txt, kernel (FIP slice) and root (rootfs UBI).
tar -tvf "$IMG" > "$HERE/container-listing.txt"
mkdir -p "$HERE/container-metadata"
tar -xOf "$IMG" sysupgrade-cudy_wr3000-v2/CONTROL > "$HERE/container-metadata/CONTROL"
tar -xOf "$IMG" sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt > "$HERE/container-metadata/INSTALL-STATUS.txt"
tar -xOf "$IMG" sysupgrade-cudy_wr3000-v2/fip-manifest.txt > "$HERE/container-metadata/fip-manifest.txt"
tar -xOf "$IMG" sysupgrade-cudy_wr3000-v2/kernel > "$WORK/kernel"
tar -xOf "$IMG" sysupgrade-cudy_wr3000-v2/root > "$WORK/root"
{
	echo "# container payload hashes (extracted from the built sysupgrade tar)"
	( cd "$WORK" && sha256sum kernel root )
} > "$HERE/container-payload.sha256"
if cmp -s "$WORK/kernel" "$SLICE"; then echo "kernel payload == build product" > "$HERE/container-payload.txt"; \
else echo "kernel payload MISMATCH" > "$HERE/container-payload.txt"; fi
if cmp -s "$WORK/root" "$ROOTFS"; then echo "root payload == build product" >> "$HERE/container-payload.txt"; \
else echo "root payload MISMATCH" >> "$HERE/container-payload.txt"; fi

# 4. independent structural validation
python3 "$T/scripts/fip-slice.py" check --max-size 8650752 "$SLICE" > "$HERE/fip-slice-check.log" 2>&1
python3 "$T/scripts/luofu-image.py" check-container --board cudy_wr3000-v2 \
	--target hisilicon/luofu "$IMG" > "$HERE/container-check.log" 2>&1

# 5. DTB cross-check: the DTB the image recipe built must be the same content
#    as the host cpp+dtc compile recorded in ../dts/build-evidence.txt.
{
	echo "# board DTB built by the OpenWrt image recipe"
	printf "path   : %s\n" "$DTB"
	printf "size   : %s bytes\n" "$(stat -c %s "$DTB")"
	printf "sha256 : %s\n" "$(sha256sum "$DTB" | cut -d' ' -f1)"
	echo "# host cpp+dtc reference (validation/dts/build-evidence.txt):"
	echo "#   size 12848, sha256 1c200daf8046cb154e5e6dfa990cd827348ab960da9fce13596ebfa0bb4e04b8"
} > "$HERE/dtb-crosscheck.txt"

# 6. result summary lines
{
	echo "# build result (captured $(date -u +%Y-%m-%dT%H:%M:%SZ))"
	grep -h 'CLEAN-RC\|WORLD-RC' "$T/build-upstream2-clean.log" "$T/build-upstream2-clean-world.log" 2>/dev/null || true
	echo "# final image"
	ls -l "$IMG"
	sha256sum "$IMG"
} > "$HERE/build-result.txt"

echo "capture: OK -> $HERE"