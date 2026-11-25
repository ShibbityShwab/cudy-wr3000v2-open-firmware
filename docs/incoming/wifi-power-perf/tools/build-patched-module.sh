#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-patched-module.sh - HOST-ONLY build of the wifi-power-perf safe-subset
# module: FINAL association module base (56 params) + patches/*.patch.
# The resulting .ko is local evidence only and is NEVER committed.
#
#   ./tools/build-patched-module.sh
#   BASE_TREE=/tmp/wifi-assoc-build/integ ./tools/build-patched-module.sh
#
# Base-tree resolution:
#   1. $BASE_TREE if set;
#   2. /tmp/sibling-mgmt-fix (the final association module work tree, if the
#      lane that built it left it behind);
#   3. rebuild it with wifi-stability/tools/build-association-module.sh
#      (tracked, reproducible) and use /tmp/wifi-assoc-build/integ.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
WORK=${WORK:-/tmp/wifi-power-perf-build}
ART=$WORK/artifact

BASE=${BASE_TREE:-}
if [ -z "$BASE" ]; then
	if [ -d /tmp/sibling-mgmt-fix ]; then
		BASE=/tmp/sibling-mgmt-fix
		echo "== base: /tmp/sibling-mgmt-fix (final association work tree)"
	else
		echo "== base: rebuilding the association module (tracked tooling)"
		"$DOCS/wifi-stability/tools/build-association-module.sh"
		BASE=/tmp/wifi-assoc-build/integ
	fi
fi
[ -d "$BASE" ] || { echo "build: base tree missing: $BASE" >&2; exit 2; }

# Gate 1: the base must be the FINAL association module (56 params).
n=$(grep -h 'module_param_named' "$BASE"/*.c 2>/dev/null | wc -l | tr -d ' ')
[ "$n" = 56 ] || {
	echo "build: base has $n module params, expected the final association module (56) - refusing" >&2
	exit 2
}
[ -f "$BASE/hi5622_data_tx.c" ] || { echo "build: base lacks hi5622_data_tx.c (not the final association tree)" >&2; exit 2; }

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE:-}" ] && KDIR=$KDIR_OVERRIDE
[ -n "${STAGING_OVERRIDE:-}" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "build: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "build: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build: cross gcc missing" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK" "$ART"
cp -a "$BASE/." "$WORK/integ/"
( cd "$WORK/integ" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order 2>/dev/null ) || true

for p in "$LANE_DIR"/patches/*.patch; do
	[ -f "$p" ] || { echo "build: no patches found" >&2; exit 2; }
	echo "== apply $(basename "$p")"
	( cd "$WORK/integ" && patch -p1 --no-backup-if-mismatch < "$p" >/dev/null )
done

m=$(grep -h 'module_param_named' "$WORK/integ"/*.c 2>/dev/null | wc -l | tr -d ' ')
echo "== patched params: $m"

export STAGING_DIR
( cd "$WORK/integ" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/integ" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK/integ" modules > "$WORK/build.log" 2>&1 || {
	echo "build: BUILD FAILED" >&2; tail -60 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "build: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/integ/hi5622_wifi.ko
[ -f "$KO" ] || { echo "build: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.stripped.ko" "$KO"
sz=$(wc -c < "$ART/hi5622_wifi.stripped.ko" | tr -d ' ')
sha=$(sha256sum "$ART/hi5622_wifi.stripped.ko" | cut -d' ' -f1)
echo "build: OK stripped=$sz sha256=$sha params=$m"
echo "build: artifact $ART/hi5622_wifi.stripped.ko (LOCAL evidence only, never committed)"
echo "build: log $WORK/build.log"
