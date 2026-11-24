#!/bin/sh
# build-module.sh - compile-verify the wifi-beacon-static module on the
# project 6.18.60 tree (host-only; hostapd/device untouched).
set -eu
SRC=/tmp/beaconstatic/b
WORK=/tmp/beaconstatic/build
STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
export STAGING_DIR
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
CC=$CROSS"gcc"
STRINGS=$CROSS"strings"
STRIP=$CROSS"strip"
NM=$CROSS"nm"
KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
[ -x "$CC" ] || { echo "missing cross gcc"; exit 2; }
[ -d "$KDIR" ] || { echo "missing KDIR"; exit 2; }
rm -rf "$WORK"
mkdir -p "$WORK"
cp -a /tmp/wifi-stability-build/combined/. "$WORK"/
cd "$WORK"
rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers build.log 2>/dev/null || true
cp -a "$SRC"/. .
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules >build.log 2>&1 || { tail -60 build.log; exit 1; }
if grep -iE 'warning:|error:' build.log; then echo 'FAIL: warnings/errors present'; exit 1; fi
echo 'BUILD OK (zero warnings/errors)'
ls -l hi5622_wifi.ko
$STRINGS hi5622_wifi.ko | grep -m1 '^vermagic='
sha256sum hi5622_wifi.ko
$STRIP --strip-debug -o hi5622_wifi.stripped.ko hi5622_wifi.ko
echo 'stripped:'; ls -l hi5622_wifi.stripped.ko; sha256sum hi5622_wifi.stripped.ko
$CROSS"objcopy" -O binary --only-section=.text hi5622_wifi.ko .t1
$CROSS"objcopy" -O binary --only-section=.text hi5622_wifi.stripped.ko .t2
cmp -s .t1 .t2 && echo "strip .text identity: OK" || { echo "FAIL: strip changed .text"; exit 1; }
echo "base chain pin: wifi-stability final combined stripped 269688 B sha256 b5dfb883a0352d24d3279c73c6410d60c90318b2f17c84b9d3fa006ebb1a50b2 (this patch applies on top of it)"
$NM -u hi5622_wifi.stripped.ko | grep -c 'cfg80211_' || true
echo 'beacon_static symbols:'; $NM hi5622_wifi.ko | grep -iE 'beacon_plan|beacon_step|chan_cfg|build_chan' | head -20 || true
echo 'beacon_static strings:'; $STRINGS hi5622_wifi.stripped.ko | grep -E 'beacon_static|ap_chan' | head -20 || true
