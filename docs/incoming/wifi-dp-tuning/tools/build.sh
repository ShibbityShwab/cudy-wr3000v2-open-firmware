#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host+kernel verification for the wifi-dp-tuning
# lane (shared task task-134).  Host-only: no device contact, no image build.
#
#   1. stage the combined base tree (wifi-dp-air/src + ap-start v7 patch +
#      hi5622_ap_frames.{c,h}), apply patches/0001-txq-consumer-tuning.patch;
#   2. prove the patch reproduces this lane's src/ files byte-for-byte;
#   3. host selftests: this lane's TXQ layout test (37 checks) + the inherited
#      wifi-dp-txrx portable TX/RX regression (254 checks);
#   4. kernel module build against the project 6.18.60 tree (0 warnings,
#      vermagic, attribute/symbol contract), debug-strip for a path-independent
#      artifact and report size/sha256;
#   5. device-free publication checks (no binaries/objects in the lane, SPDX).
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
INC=$(cd "$LANE/.." && pwd)
DP_AIR=$INC/wifi-dp-air/src
AP_START=$INC/wifi-ap-start
DP_TXRX=$INC/wifi-dp-txrx
PATCH=$LANE/patches/0001-txq-consumer-tuning.patch
WORK=/tmp/wifi-dp-tuning-build
ART=/tmp/wifi-dp-tuning-artifact
LOG=/tmp/wifi-dp-tuning-build.log
CC=cc
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo "PASS: $*"; }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
die()  { echo "FAIL: $*" >&2; exit 1; }

[ -d "$DP_AIR" ] || die "wifi-dp-air/src missing"
[ -f "$AP_START/provider/apstart-provider.patch" ] || die "ap-start v7 patch missing"
[ -f "$AP_START/src/hi5622_ap_frames.c" ] || die "ap-start frames missing"
[ -f "$PATCH" ] || die "own patch missing"

# ---------------------------------------------------------------- kernel tree
set +u
KDIR=$KDIR_OVERRIDE
STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
    for root in /home/shibbityshwab/openwrt-lab-upstream-target \
                /home/shibbityshwab/openwrt-lab-selectorfix-img; do
        c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
        if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
    done
fi
[ -n "$KDIR" ] && [ -d "$KDIR" ] || die "6.18.60 kernel tree not found"
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
export STAGING_DIR

echo "== 1. stage combined base + apply the tuning patch =="
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$DP_AIR/." "$WORK/"
find "$WORK" -maxdepth 1 -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
    -o -name '*.mod.c' -o -name '*.cmd' -o -name 'Module.symvers' \
    -o -name 'modules.order' \) -delete
cp "$AP_START/src/hi5622_ap_frames.h" "$AP_START/src/hi5622_ap_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$AP_START/provider/apstart-provider.patch" ) \
    >/dev/null || die "ap-start patch failed"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) >/dev/null \
    || die "tuning patch failed"
ok "combined base staged and tuning patch applied"

echo "== 2. patch reproduces the lane src/ files =="
for f in dp_txrx.c dp_txrx.h hi5622_dp.c hi5622_dp_txrx.c hi5622_dp_txrx.h; do
    cmp -s "$LANE/src/$f" "$WORK/$f" || die "src/$f != patched tree"
done
ok "5 changed files reproduce byte-for-byte from patches/0001"
n=$(grep -c '^+++ ' "$PATCH")
[ "$n" = "5" ] && ok "patch scope is exactly 5 files" || bad "patch scope $n files"

echo "== 3. host selftests (device-free) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$WORK" \
      -o "$WORK/txq_selftest" "$LANE/tools/txq_selftest.c" "$WORK/dp_txrx.c" \
      || die "TXQ selftest compile failed"
TXQ=$("$WORK/txq_selftest" | tail -1)
case "$TXQ" in
    "SUMMARY: 37 passed, 0 failed") ok "TXQ-table layout selftest: $TXQ" ;;
    *) die "TXQ selftest: $TXQ" ;;
esac
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
      -Wstrict-prototypes -Wmissing-prototypes -I"$WORK" \
      -o "$WORK/dp_txrx_selftest" "$DP_TXRX/tools/dp_txrx_selftest.c" \
      "$WORK/dp_txrx.c" || die "inherited dp_txrx selftest compile failed"
INH=$("$WORK/dp_txrx_selftest" | tail -1)
case "$INH" in
    "SUMMARY: 254 passed, 0 failed") ok "inherited portable TX/RX regression: $INH" ;;
    *) die "inherited selftest: $INH" ;;
esac

echo "== 4. kernel module build (project 6.18.60 tree) =="
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean \
    >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
    >"$LOG" 2>&1 || { tail -30 "$LOG" >&2; die "module build failed"; }
if grep -iE 'warning:|error:' "$LOG"; then
    die "compiler/modpost warnings - refusing artifact"
fi
ok "module built with 0 compiler/modpost warnings"
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || die "no .ko produced"
VERMAGIC=$(strings -a "$KO" | grep -m1 '^vermagic=')
case "$VERMAGIC" in
    vermagic=6.18.60*) ok "$VERMAGIC" ;;
    *) die "unexpected $VERMAGIC" ;;
esac
DEPENDS=$(strings -a "$KO" | grep -m1 '^depends=')
[ "$DEPENDS" = "depends=cfg80211" ] && ok "$DEPENDS" || bad "$DEPENDS"

NM="$CROSS_COMPILE"nm
for sym in hi5622_dp_txq_show hi5622_dp_tx_complete_poll_store \
           hi5622_dp_mac_ext_show hi5622_dp_mac_tail_init_store \
           dp_txrx_txq_publish dp_txrx_txq_user_block dp_txrx_txq_flags_set; do
    "$NM" "$KO" | grep -q " $sym\$" || die "missing symbol $sym"
done
ok "TXQ-publish + completion-poll symbols present"
for attr in dp_txq_table dp_tx_complete_poll dp_mac_ext dp_mac_tail_init \
            dp_tx_frame dp_tx_status dp_rx_poll dp_rx_capture; do
    strings -a "$KO" | grep -qx "$attr" || die "missing attribute $attr"
done
ok "new attribute surface present"
PARM_OK=1
for par in txq_publish tx_complete_ms mac_tail_writes; do
    "$NM" "$KO" | grep -q "__param_str_$par\$" || PARM_OK=0
    strings -a "$KO" | grep -q "^parmtype=$par:uint" || PARM_OK=0
done
[ "$PARM_OK" = "1" ] \
    && ok "module parameters recorded (txq_publish/tx_complete_ms/mac_tail_writes)" \
    || bad "module parameters not recorded"

# path-independent artifact: strip debug, prove .text unchanged
"$CROSS_COMPILE"strip --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$CROSS_COMPILE"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS_COMPILE"objcopy -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" && ok "debug strip leaves .text byte-identical" \
    || die "strip changed .text"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
USHA=$(sha256sum "$KO" | cut -d' ' -f1)

echo "== 5. protocol-sequence validation (no device) =="
if [ -x "$PY" ]; then
    "$PY" "$LANE/tools/validate_sequence.py" | tail -2 || die "sequence validation failed"
    ok "G-TXQ ladder sequence validated"
else
    echo "SKIP: project python not found ($PY)"
fi

echo "== 6. device-free publication checks =="
[ -z "$(find "$LANE" -name '*.o' -o -name '*.ko' -o -name '*.mod' | head -1)" ] \
    && ok "no object/binary artifacts in the lane" || bad "lane carries binaries"
for f in "$LANE"/src/*.c "$LANE"/src/*.h "$LANE"/tools/*.c; do
    head -1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' || bad "missing SPDX: $f"
done
ok "SPDX headers present in all C/H sources"
[ -z "$(find "$LANE" -name '*.bin' -o -name '*.fw' -o -name '*.rom' | head -1)" ] \
    && ok "no firmware/vendor blob in the lane" || bad "lane carries a blob"

echo
echo "== artifact =="
echo "module (debug-stripped, path-independent): $ART/hi5622_wifi.ko"
echo "size   : $SIZE"
echo "sha256 : $SHA"
echo "unstripped: $USIZE B sha256 $USHA"
echo "base     : wifi-dp-air/src + ap-start v7 + patches/0001 (5 files)"
echo
echo "SUMMARY: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ] || exit 1
