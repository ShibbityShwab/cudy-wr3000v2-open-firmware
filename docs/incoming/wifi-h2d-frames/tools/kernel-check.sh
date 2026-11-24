#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# tools/kernel-check.sh - OPTIONAL kernel-context compile check (host-only,
# module-only, no image build, no device contact).
#
# Proves h2d_frames.c/h compile as part of the real 6.18.60 out-of-tree
# transport module under CONFIG_WERROR.  It copies the COMMITTED
# wifi-hcc-service src tree to a scratch directory (never modifies another
# lane's files), adds h2d_frames.o to the module object list and builds.
# The .ko is local evidence only and is NOT committed.
#
#   ./tools/kernel-check.sh
#   KDIR=... STAGING_DIR=... ./tools/kernel-check.sh
set -eo pipefail

LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOCS="$(cd "$LANE_DIR/.." && pwd)"
KO_SRC="${KO_SRC:-$DOCS/wifi-hcc-service/src}"
KERNEL_ROOT="${KERNEL_ROOT:-/home/shibbityshwab/openwrt-lab-selectorfix-img}"
KDIR="${KDIR:-$KERNEL_ROOT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60}"
STAGING_DIR="${STAGING_DIR:-$KERNEL_ROOT/staging_dir}"
CROSS_COMPILE="${CROSS_COMPILE:-$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}"
WORK="${WORK:-/tmp/wifi-h2d-frames-kernel}"

[ -d "$KO_SRC" ] || { echo "kernel-check: missing reference module src: $KO_SRC" >&2; exit 2; }
[ -d "$KDIR" ] || { echo "kernel-check: kernel tree not found: $KDIR" >&2; exit 2; }
[ -x "$CROSS_COMPILE"gcc ] || { echo "kernel-check: toolchain not found: $CROSS_COMPILE"gcc >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$KO_SRC/." "$WORK/"
cp "$LANE_DIR/src/h2d_frames.h" "$LANE_DIR/src/h2d_frames.c" \
   "$LANE_DIR/src/h2d_catalogue.inc" "$WORK/"

python3 - "$WORK/Kbuild" <<'PYEOF'
import sys
p = sys.argv[1]
s = open(p).read()
old = "hi5622_pciet_hcc-y := pciet_main.o pciet_ete.o pciet_hcc_service.o"
new = old + " h2d_frames.o"
if old not in s:
    raise SystemExit("kernel-check: Kbuild anchor not found")
open(p, "w").write(s.replace(old, new))
PYEOF

export STAGING_DIR
make -C "$KDIR" M="$WORK" ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" modules \
     2>&1 | tee "$WORK/build.log"

KO="$WORK/hi5622_pciet_hcc.ko"
[ -f "$KO" ] || { echo "kernel-check: BUILD FAILED: $KO missing" >&2; exit 1; }

echo
echo "== kernel-context compile result"
ls -l "$KO"
sha256sum "$KO"
"$CROSS_COMPILE"nm "$KO" | grep -cE ' T h2d_| t h2d_| r h2d_' | \
	sed 's/^/h2d symbols in module: /'
grep -iE 'warning:|error:' "$WORK/build.log" && { echo "kernel-check: WARNINGS FOUND" >&2; exit 1; }
echo "kernel-check: OK (zero warnings; module-only build; no image, no device)"
