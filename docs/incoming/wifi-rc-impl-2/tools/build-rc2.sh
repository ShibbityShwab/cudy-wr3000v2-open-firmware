#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-rc2.sh - host build of the RC-2 patched module (HOST-ONLY).
#
# Stages the pinned current base (the IMAGE #3 merged driver source tree,
# evidence/base-pin.txt), applies patches/0001-rate-ctl-2.patch, runs the three
# host suites and cross-builds the module against the project 6.18.60 kernel
# tree.  No device access, no firmware write, no image, no flash; the .ko is
# LOCAL EVIDENCE ONLY and is never committed.
#
#   BASE_TREE=/tmp/wifi-image-3-module ./tools/build-rc2.sh
#   KDIR_OVERRIDE=... STAGING_OVERRIDE=... ./tools/build-rc2.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
BASE_TREE=${BASE_TREE:-/tmp/wifi-image-3-module}
WORK=${WORK:-/tmp/rc2-build}
ART=${ART:-/tmp/rc2-artifact}
EV=$LANE/evidence
mkdir -p "$ART"

echo "== 1. base pin + deterministic patch application"
mkdir -p "$EV"
python3 "$LANE/tools/make-patch.py" --verify --dest "$WORK" | tee "$EV/patch-apply-log.txt"

echo "== 2. host suites on the patched tree"
TREE=$WORK "$LANE/tools/run-host-tests.sh"

echo "== 3. cross toolchain"
KDIR=${KDIR_OVERRIDE:-}
STAGING_DIR=${STAGING_OVERRIDE:-}
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "${KDIR:-}" ] || { echo "build-rc2: 6.18.60 kernel tree not found (KDIR_OVERRIDE)" >&2; exit 2; }
[ -d "${STAGING_DIR:-}" ] || { echo "build-rc2: staging dir not found (STAGING_OVERRIDE)" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "build-rc2: cross gcc missing" >&2; exit 2; }
export STAGING_DIR

echo "== 4. module build (0 warnings/errors required)"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order ) >/dev/null 2>&1 || true
( cd "$WORK" && make ARCH=arm CROSS_COMPILE=$CROSS -C "$KDIR" M="$WORK" modules ) \
	> "$ART/build.log" 2>&1
if grep -nE 'warning:|error:|Error [0-9]|conflict marker' "$ART/build.log" >/dev/null; then
	echo "build-rc2: compiler/modpost diagnostics (must be zero):" >&2
	grep -nE 'warning:|error:|Error [0-9]|conflict marker' "$ART/build.log" | head -30 >&2
	cp "$WORK/hi5622_wifi.ko" "$ART/" 2>/dev/null || true
	exit 1
fi
[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-rc2: no module built" >&2; exit 1; }
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"

echo "== 5. artifact identity"
OBJCOPY=${CROSS}objcopy
$OBJCOPY -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$ART/.text.bin"
$OBJCOPY -O binary --only-section=.modinfo "$ART/hi5622_wifi.ko" "$ART/.modinfo.bin"
TEXT_SHA=$(sha256sum "$ART/.text.bin" | cut -d' ' -f1)
KO_SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
KO_SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
${CROSS}strip --strip-debug -o "$ART/hi5622_wifi.activity.ko" "$ART/hi5622_wifi.ko"
ACT_SHA=$(sha256sum "$ART/hi5622_wifi.activity.ko" | cut -d' ' -f1)
ACT_SIZE=$(stat -c %s "$ART/hi5622_wifi.activity.ko")
# NOTE: the image-bake installed form is produced by the image lane's
# scripts/strip-kmod.sh (NO_RENAME=1) - this lane deploys the ACTIVITY form
# (strip --strip-debug), which is what every previous bench module used.
PARAMS=$(strings "$ART/.modinfo.bin" | grep -c '^parmtype=')
VERMAGIC=$(strings "$ART/.modinfo.bin" | grep '^vermagic=')
DEPENDS=$(strings "$ART/.modinfo.bin" | grep '^depends=')
rm -f "$ART/.text.bin" "$ART/.modinfo.bin"

cat > "$EV/build-module.txt" <<EOF
# wifi-rc-impl-2 module build record (host-only; .ko is local evidence only)
# base: the IMAGE #3 merged driver source tree (evidence/base-pin.txt)
#       + patches/0001-rate-ctl-2.patch (evidence/patch-apply.txt)
# build: make ARCH=arm CROSS_COMPILE=arm-openwrt-linux-muslgnueabi- -C linux-6.18.60 M=<tree> modules
# toolchain: OpenWrt GCC 14.4.0 musl-eabi (arm_cortex-a9)
# result: 0 warnings, 0 errors, 0 conflict markers
module_sha256 $KO_SHA
module_size $KO_SIZE
module_text_sha256 $TEXT_SHA
module_params $PARAMS
module_activity_sha256 $ACT_SHA
module_activity_size $ACT_SIZE

$VERMAGIC
$DEPENDS
# note: the sha256 is build-path dependent via debug info; the .text sha is the
# stable identity (other lanes compare .text).  The module is UNSTRIPPED here.
EOF
cat "$EV/build-module.txt"
echo "== build PASS: $ART/hi5622_wifi.ko"
