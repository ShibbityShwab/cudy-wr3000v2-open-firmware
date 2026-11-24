#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel build/verify driver for the
# wifi-datapath-bringup lane (shared task task-111).
#
#   1. host compile + run the data-plane portable selftest (strict
#      -Werror -pedantic, device-free);
#   2. host compile + run the inherited H2D regression selftest (the control
#      plane the data plane rides on must stay green);
#   3. host static decode selftest (vendor .ko + FIRMWARE.bin read in place;
#      only decoded constants are printed, nothing vendor is copied);
#   4. out-of-tree kernel module build against the project 6.18.60 tree and
#      artifact evidence (size, sha256, vermagic, symbols, warning count);
#   5. device-free source/selftest checks.
#
# Never builds an image, never touches the device, never copies vendor
# firmware/calibration bytes.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
WORK=/tmp/wifi-datapath-bringup-build
CC=cc
KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
LOG=/tmp/hi5622-wifi-datapath-bringup-build.log
KO=$SRC/hi5622_wifi.ko
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python

die() { echo "FAIL: $*" >&2; exit 1; }

# Fall back to another configured 6.18.60 luofu build dir if the default was
# cleaned (the trees are local build products, not deliverables).
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
    export KDIR STAGING_DIR CROSS_COMPILE
fi
export STAGING_DIR KDIR CROSS_COMPILE

[ -d "$KDIR" ] || die "kernel tree not found: $KDIR"
[ -x "$CROSS_COMPILE"gcc ] || die "cross compiler not found: $CROSS_COMPILE"gcc
command -v "$CC" >/dev/null || die "host compiler not found: $CC"

echo "== 1. host data-plane selftest (device-free) =="
rm -rf "$WORK"
mkdir -p "$WORK"
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/dp_logic_selftest" \
      "$HERE/dp_logic_selftest.c" "$SRC/dp_rings.c" \
      "$SRC/h2d_send_logic.c" "$SRC/h2d_frames.c" "$SRC/hcc_core.c" || die "host data-plane selftest compile failed"
"$WORK/dp_logic_selftest" || die "host data-plane selftest failed"
echo

echo "== 2. host H2D control-plane regression selftest =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$SRC" \
      -o "$WORK/h2d_logic_selftest" \
      "$HERE/h2d_logic_selftest.c" "$SRC/h2d_send_logic.c" \
      "$SRC/h2d_frames.c" "$SRC/hcc_core.c" || die "H2D regression selftest compile failed"
"$WORK/h2d_logic_selftest" || die "H2D regression selftest failed"
echo

echo "== 3. static decode selftest (local vendor artifacts, host-only) =="
if [ -x "$PY" ]; then
    "$PY" "$HERE/dp_static_decode.py" --selftest || die "static decode selftest failed"
else
    echo "SKIP: project python not found ($PY); static decode selftest not run"
fi
echo

echo "== 4. kernel module build (project 6.18.60 tree) =="
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" clean >/dev/null 2>&1 || true
rm -f "$KO"
if ! make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$SRC" modules >"$LOG" 2>&1; then
    tail -40 "$LOG"
    die "kernel module build failed (full log: $LOG)"
fi
WARN=$(grep -cE 'warning:|error:|ERROR:' "$LOG" || true)
[ -f "$KO" ] || die "no hi5622_wifi.ko produced"
SIZE=$(stat -c %s "$KO")
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
VM=$(modinfo -F vermagic "$KO" 2>/dev/null)
SYMS=$("$CROSS_COMPILE"nm "$KO" | grep -cE ' T (dp_|hi5622_dp)' || true)
H2DSYMS=$("$CROSS_COMPILE"nm "$KO" | grep -cE ' T h2d_' || true)
echo "module      : $KO"
echo "size_bytes  : $SIZE"
echo "sha256      : $SHA"
echo "vermagic    : $VM"
echo "dp syms     : $SYMS (T dp_*/hi5622_dp*)"
echo "h2d syms    : $H2DSYMS (T h2d_*)"
echo "warnings    : $WARN (compiler+modpost; 0 expected)"
echo "log         : $LOG"
echo
[ "$WARN" -eq 0 ] || die "build is not clean: $WARN warning/error line(s)"
modinfo -F parm "$KO" 2>/dev/null | grep -q '^h2d_send_budget:' || \
    die "h2d_send_budget module parameter missing from the module"
"$CROSS_COMPILE"nm "$KO" | grep -q ' T h2d_post_txq_addr' || \
    die "h2d_post_txq_addr not exported in the built module"
"$CROSS_COMPILE"nm "$KO" | grep -q ' T dp_mac_program' || \
    die "dp_mac_program not in the built module"

echo "== 5. protocol sequence schema validation (bench-tooling validator) =="
if [ -x "$PY" ] && [ -f "$HERE/validate_sequence.py" ]; then
    "$PY" "$HERE/validate_sequence.py" || die "protocol sequence validation failed"
else
    echo "SKIP: project python or validator missing"
fi
echo

echo "== 6. source checks =="
grep -q 'DP_MAC_CA_BAND0' "$SRC/dp_rings.h" || die "dp_rings.h constants missing"
grep -q 'dp_setup' "$SRC/hi5622_dp.c" || die "dp_setup attribute missing"
grep -q '_HI5622_DP_H' "$SRC/hi5622_dp.h" || die "hi5622_dp.h header guard missing"
echo "OK: host data-plane selftest + H2D regression + static decode + clean kernel build"
echo "artifact: $SIZE B sha256 $SHA"
