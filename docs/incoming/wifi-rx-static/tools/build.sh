#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host + kernel verification for the wifi-rx-static
# lane (shared task task-137).  HOST-ONLY: no bench/device contact.
#
#   1. stage the final combined clean-room driver base tree (RX_BASE; default
#      /tmp/wifi-stability-build/combined) into /tmp/wifi-rx-lane-build;
#   2. apply patches/0001-rx-live-enable.patch and prove it reproduces src/
#      byte-for-byte;
#   3. build+run the RX model host selftest (must be 0 failures);
#   4. build the kernel module against the project 6.18.60 tree (0 warnings,
#      report unstripped/stripped size + sha256);
#   5. publication checks: no binaries/objects in the lane, SPDX headers.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
PATCH=$LANE/patches/0001-rx-live-enable.patch
SRC=$LANE/src
WORK=/tmp/wifi-rx-lane-build
ART=/tmp/wifi-rx-lane-artifact
LOG=/tmp/wifi-rx-lane-build.log
CC=cc
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $*"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
die() { echo "FAIL: $*" >&2; exit 1; }

[ -f "$PATCH" ] || die "patch missing"
[ -f "$LANE/tools/rx_selftest.c" ] || die "selftest missing"

# ---------------------------------------------------------------- base tree
BASE=${RX_BASE:-/tmp/wifi-stability-build/combined}
[ -d "$BASE" ] || die "base tree '$BASE' missing (set RX_BASE to the final combined clean-room driver source tree; see BUILD-NOTES.md)"
echo "== 1. stage base tree: $BASE =="
rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$BASE/." "$WORK/"
find "$WORK" -maxdepth 1 -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
    -o -name '*.mod.c' -o -name '*.cmd' -o -name 'Module.symvers' \
    -o -name 'modules.order' \) -delete
ok "base staged ($(ls "$WORK" | wc -l) files)"

echo "== 2. apply the RX patch and compare with src/ =="
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) >/dev/null \
    || die "patch failed to apply"
for f in Kbuild hi5622_dp.c hi5622_dp_txrx.c hi5622_dp_txrx.h \
         hi5622_rx_live.c hi5622_rx_live.h; do
    cmp -s "$SRC/$f" "$WORK/$f" || die "src/$f != patched tree"
done
ok "patch scope is exactly 6 files and src/ reproduces byte-for-byte"

echo "== 3. host selftest (device-free) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I"$WORK" \
    "$LANE/tools/rx_selftest.c" "$WORK/hi5622_rx_live.c" \
    -o "$ART/rx_selftest" 2>"$ART/selftest-build.log" \
    || die "selftest build failed (see $ART/selftest-build.log)"
NFAIL=$("$ART/rx_selftest" | tee "$ART/selftest.log" | sed -n 's/^\([0-9]*\) checks, \([0-9]*\) failures$/\2/p')
[ -n "$NFAIL" ] || die "selftest summary missing"
[ "$NFAIL" = "0" ] && ok "host selftest: 0 failures ($(grep -c '^PASS: ' "$ART/selftest.log") checks)" \
    || bad "host selftest: $NFAIL failures (see $ART/selftest.log)"

echo "== 4. kernel module build against the project 6.18.60 tree =="
set +u
KDIR=${KDIR_OVERRIDE:-}
STAGING_DIR=${STAGING_OVERRIDE:-}
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
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
    > "$LOG" 2>&1
RC=$?
WARN=$(grep -c 'warning:' "$LOG" || true)
[ "$RC" = "0" ] && ok "kernel build RC=0" || bad "kernel build RC=$RC (see $LOG)"
[ "$WARN" = "0" ] && ok "kernel build: 0 warnings" || bad "kernel build: $WARN warnings"
[ -f "$WORK/hi5622_wifi.ko" ] || die "module artifact missing"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.unstripped.ko"
UNSHA=$(sha256sum "$ART/hi5622_wifi.unstripped.ko" | cut -d' ' -f1)
UNSZ=$(stat -c %s "$ART/hi5622_wifi.unstripped.ko")
\
"$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-strip" \
    --strip-debug -o "$ART/hi5622_wifi.ko" "$WORK/hi5622_wifi.ko" 2>/dev/null \
    || cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"
SSHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
SSZ=$(stat -c %s "$ART/hi5622_wifi.ko")
ok "artifact: unstripped $UNSZ B sha256 $UNSHA / stripped $SSZ B sha256 $SSHA"
if command -v modinfo >/dev/null 2>&1; then
    modinfo "$ART/hi5622_wifi.ko" 2>/dev/null | grep -q '^parm:.*rx_enable:uint' \
        && ok "modinfo carries rx_enable" || bad "modinfo rx_enable missing"
fi

echo "== 5. publication checks =="
if find "$LANE" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
        -o -name '*.bin' -o -name '*.dis' \) | grep -q .; then
    bad "binary/object files inside the lane"
else
    ok "lane contains no binaries/objects"
fi
for f in "$LANE"/src/hi5622_rx_live.c "$LANE"/src/hi5622_rx_live.h \
         "$LANE"/tools/rx_selftest.c; do
    head -n 1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' \
        && ok "SPDX header: $(basename "$f")" || bad "SPDX header missing: $f"
done

echo
echo "RESULT: $PASS passed, $FAIL failed"
echo "artifact dir: $ART (insmod candidate: hi5622_wifi.ko)"
[ "$FAIL" = "0" ] || exit 1
