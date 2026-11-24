#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel verify for the wifi-dp-txrx lane
# (shared task task-122).  No device contact, no image build.
#
#   1. host compile + run the portable TX/RX selftest (strict -Werror);
#   2. static-decode assertions against the LOCAL vendor .ko/FIRMWARE.bin
#      (read in place; only booleans/counts printed, nothing copied);
#   3. protocol-sequence schema validation through the bench tooling
#      (lane attrs injected in memory only);
#   4. stage a scratch copy of wifi-datapath-bringup/src, copy this lane's
#      kernel files, apply the reproducible patch edits, verify the committed
#      patch matches, and compile the module against the project 6.18.60 tree
#      (warning count + vermagic + symbols + size/sha256 evidence);
#   5. device-free source/publication checks.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
SRC=$LANE/src
TOOLS=$LANE/tools
BRINGUP=$(cd "$LANE/../wifi-datapath-bringup" && pwd)
WORK=/tmp/wifi-dp-txrx-build
CC=cc
KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
LOG=/tmp/hi5622-wifi-dp-txrx-build.log

die() { echo "FAIL: $*" >&2; exit 1; }

[ -d "$SRC" ] || die "lane src/ missing"
[ -d "$BRINGUP/src" ] || die "wifi-datapath-bringup src/ missing (E1 base lane)"

if [ ! -f "$KDIR/include/generated/autoconf.h" ]; then
    for cand in /home/shibbityshwab/openwrt-lab-verify \
                /home/shibbityshwab/openwrt-lab-selectorfix-k \
                /home/shibbityshwab/openwrt-lab; do
        k=$cand/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
        if [ -f "$k/include/generated/autoconf.h" ] &&
           [ -x "$cand/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-gcc" ]; then
            KDIR=$k
            STAGING_DIR=$cand/staging_dir
            CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
            break
        fi
    done
fi
export KDIR STAGING_DIR CROSS_COMPILE

echo "== 1. host portable TX/RX selftest (device-free) =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/dp_txrx_selftest" \
      "$TOOLS/dp_txrx_selftest.c" "$SRC/dp_txrx.c" ||
    die "host selftest compile failed"
"$WORK/dp_txrx_selftest" || die "host selftest failed"
echo

echo "== 2. static-decode assertions (local vendor artifacts, host-only) =="
if [ -x "$PY" ]; then
    "$PY" "$TOOLS/dp_txrx_static_decode.py" || die "static decode failed"
else
    echo "SKIP: project python not found ($PY)"
fi
echo

echo "== 3. protocol-sequence validation (bench tooling, no device) =="
if [ -x "$PY" ]; then
    "$PY" "$TOOLS/validate_sequence.py" || die "sequence validation failed"
else
    echo "SKIP: project python not found ($PY)"
fi
echo

echo "== 4. kernel integration: scratch copy + patch + module build =="
cp -r "$BRINGUP/src" "$WORK/src" || die "scratch copy failed"
cp "$SRC/dp_txrx.h" "$SRC/dp_txrx.c" \
   "$SRC/hi5622_dp_txrx.h" "$SRC/hi5622_dp_txrx.c" "$WORK/src/" ||
    die "kernel file copy failed"
if [ -x "$PY" ]; then
    "$PY" "$TOOLS/patch_builder.py" --work "$WORK" \
        --emit-patch "$WORK/0001-hi5622-dp-txrx.patch" ||
        die "patch_builder failed"
    if ! diff -u "$LANE/patches/0001-hi5622-dp-txrx.patch" \
         "$WORK/0001-hi5622-dp-txrx.patch" >/dev/null; then
        echo "FAIL: committed patch is stale; diff against the builder:"
        diff -u "$LANE/patches/0001-hi5622-dp-txrx.patch" \
             "$WORK/0001-hi5622-dp-txrx.patch" | head -20
        die "regenerate patches/0001-hi5622-dp-txrx.patch with patch_builder.py"
    fi
else
    die "project python missing: cannot reproduce the patch"
fi
[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "${CROSS_COMPILE}gcc" ] || die "cross compiler not found: ${CROSS_COMPILE}gcc"
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK/src" \
     clean >/dev/null 2>&1 || true
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK/src" \
     modules >"$LOG" 2>&1; then
    tail -30 "$LOG"
    die "kernel module build failed (full log: $LOG)"
fi
KO=$WORK/src/hi5622_wifi.ko
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG" || true)
[ "$WARN" -eq 0 ] || { grep -E 'warning:|error:' "$LOG" | head -20; \
                       die "build is not clean: $WARN warning/error line(s)"; }
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
"${CROSS_COMPILE}nm" "$KO" | grep -q ' T hi5622_dp_txrx_bind' ||
    die "hi5622_dp_txrx_bind missing from the module"
"${CROSS_COMPILE}nm" "$KO" | grep -q ' T dp_txrx_rx_desc_parse' ||
    die "portable RX logic missing from the module"
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo

echo "== 5. device-free source/publication checks =="
grep -q 'dp_tx_frame' "$LANE/patches/0001-hi5622-dp-txrx.patch" ||
    die "patch does not register dp_tx_frame"
[ "$(grep -c '^--- a/src/' "$LANE/patches/0001-hi5622-dp-txrx.patch")" -eq 2 ] ||
    die "patch touches more than hi5622_dp.c + Kbuild"
for f in "$SRC/dp_txrx.c" "$SRC/hi5622_dp_txrx.c" "$SRC/hi5622_dp_txrx.h"; do
    head -1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' ||
        die "missing GPL-2.0 SPDX header: $f"
done
if git -C "$LANE/../.." ls-files --error-unmatch \
        "$LANE/src/hi5622_wifi.ko" >/dev/null 2>&1; then
    die "built .ko must never be committed"
fi
if find "$LANE" -type f \( -name '*.ko' -o -name '*.o' -o -name 'FIRMWARE*' \
     -o -name '*.bin' -o -name 'wifi_block*' \) | grep -q .; then
    die "vendor/built binary artifact present in the lane"
fi
echo "OK: host selftest + static decode + sequence + clean kernel build + publication checks"
echo "artifact: $SIZE B sha256 $SHA"
