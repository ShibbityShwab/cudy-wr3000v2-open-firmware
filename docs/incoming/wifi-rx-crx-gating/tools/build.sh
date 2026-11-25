#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host verification for the wifi-rx-crx-gating lane
# (shared task task-164). HOST-ONLY: no bench/device contact; never loads a module.
#
#   1. stage the base driver tree (BASE; default the image-2 chain tree)
#   2. apply patches/0001-crx-delivery-gate.patch and prove the patched tree
#      matches src/ byte-for-byte
#   3. build+run the host selftest (must be 0 failures)
#   4. optional kernel module build against the project 6.18.60 tree
#   5. publication checks: no binaries/objects in the lane, SPDX headers
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
PATCH=$LANE/patches/0001-crx-delivery-gate.patch
SRC=$LANE/src
WORK=/tmp/wifi-rx-crx-gating-build
ART=/tmp/wifi-rx-crx-gating-artifact
LOG=/tmp/wifi-rx-crx-gating-build.log
CC=${CC:-cc}
PASS=0; FAIL=0
ok()  { PASS=$((PASS+1)); echo "PASS: $*"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
die() { echo "FAIL: $*" >&2; exit 1; }

[ -f "$PATCH" ] || die "patch missing"
[ -f "$LANE/tools/crx_gate_selftest.c" ] || die "selftest missing"

BASE=${BASE:-/tmp/wifi-rx-watermark-build/rebuild}
[ -d "$BASE" ] || die "base tree '$BASE' missing (set BASE=<image-2 chain tree>)"
echo "== 1. stage base tree: $BASE =="
rm -rf "$WORK" "$ART"; mkdir -p "$WORK" "$ART"
cp -a "$BASE/." "$WORK/"
find "$WORK" -maxdepth 1 -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
    -o -name '*.mod.c' -o -name '*.cmd' -o -name 'Module.symvers' \
    -o -name 'modules.order' \) -delete
ok "base staged ($(ls "$WORK" | wc -l) files)"

echo "== 2. apply the CRX-gate patch and compare with src/ =="
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) >"$ART/patch-apply.log" 2>&1 \
    || die "patch failed to apply (see $ART/patch-apply.log)"
for f in Kbuild hi5622_dp.c hi5622_crx_gate.c hi5622_crx_gate.h; do
    cmp -s "$SRC/$f" "$WORK/$f" || die "src/$f != patched tree"
done
ok "patch scope is exactly 4 files and src/ reproduces byte-for-byte"

echo "== 3. host selftest (device-free) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I"$SRC" \
    "$LANE/tools/crx_gate_selftest.c" "$SRC/hi5622_crx_gate.c" \
    -o "$ART/crx_gate_selftest" 2>"$ART/selftest-build.log" \
    || die "selftest build failed (see $ART/selftest-build.log)"
"$ART/crx_gate_selftest" | tee "$ART/selftest.log"
grep -q '0 failures' "$ART/selftest.log" \
    && ok "host selftest: 0 failures" || bad "host selftest failures"

echo "== 4. kernel module build (optional: SKIP_KERNEL=1 to skip) =="
if [ "${SKIP_KERNEL:-0}" = "1" ]; then
    ok "kernel build skipped by SKIP_KERNEL=1 (see BUILD-NOTES.md section 2)"
else
    KDIR=${KDIR_OVERRIDE:-}; STAGING=${STAGING_OVERRIDE:-}
    if [ -z "$KDIR" ]; then
        for root in /home/shibbityshwab/openwrt-lab-selectorfix-img \
                    /home/shibbityshwab/openwrt-lab-upstream-target; do
            c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
            if [ -d "$c" ]; then KDIR=$c; STAGING=$root/staging_dir; break; fi
        done
    fi
    if [ -z "$KDIR" ] || [ ! -d "$KDIR" ]; then
        bad "6.18.60 kernel tree not found (set KDIR_OVERRIDE/STAGING_OVERRIDE)"
    else
        export STAGING_DIR=$STAGING
        CROSS=$STAGING/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
        make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules \
            > "$LOG" 2>&1
        RC=$?
        WARN=$(grep -c 'warning:' "$LOG" || true)
        [ "$RC" = "0" ] && ok "kernel build RC=0" || bad "kernel build RC=$RC (see $LOG)"
        [ "$WARN" = "0" ] && ok "kernel build: 0 warnings" || bad "kernel build: $WARN warnings"
        if [ -f "$WORK/hi5622_wifi.ko" ]; then
            cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.unstripped.ko"
            ok "artifact: $(stat -c %s "$ART/hi5622_wifi.unstripped.ko") B sha256 $(sha256sum "$ART/hi5622_wifi.unstripped.ko" | cut -d' ' -f1)"
        fi
    fi
fi

echo "== 5. publication checks =="
if find "$LANE" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
        -o -name '*.bin' -o -name '*.dis' \) | grep -q .; then
    bad "binary/object files inside the lane"
else
    ok "lane contains no binaries/objects"
fi
for f in "$LANE"/src/hi5622_crx_gate.c "$LANE"/src/hi5622_crx_gate.h \
         "$LANE"/tools/crx_gate_selftest.c; do
    head -n 1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' \
        && ok "SPDX header: $(basename "$f")" || bad "SPDX header missing: $f"
done

echo
echo "RESULT: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ] || exit 1
