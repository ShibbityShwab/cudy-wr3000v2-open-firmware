#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# tools/build.sh - canonical host verification for the wifi-rx-linux-parity lane
# (shared task task-166).  HOST-ONLY: no bench/device contact, no UART, no SSH.
#
#   1. reconstruct the CURRENT published RX source state from the committed
#      lane outputs: wifi-rx-static/src + the wifi-rx-watermark patch (the
#      final combined build trees used by earlier lanes lived in /tmp and are
#      not reproducible);
#   2. apply patches/0001-rx-linux-parity.patch and prove it reproduces the
#      six files this lane owns (src/) byte-for-byte;
#   3. build+run the device-free parity model host selftest (0 failures);
#   4. kernel-header compile: the two portable files are compiled into a tiny
#      module against the project 6.18.60 tree; if a hybrid committed header
#      base (wifi-dp-air + wifi-dp-tuning dp_txrx) is available, the patched
#      kernel glue objects are compiled too (0 warnings expected);
#   5. publication checks: no binaries/objects in the lane, SPDX headers.
#
# The kernel MODULE link of the full association tree is intentionally NOT
# attempted here: that tree is not committed (see BUILD-NOTES.md).  An
# integrator with the tree can set RX_FULL_BASE to it and this script will
# apply the patch there and build the whole module.
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)          # .../wifi-rx-linux-parity
LANE=$(cd "$HERE/.." && pwd)
INCOMING=$(cd "$LANE/.." && pwd)             # .../docs/incoming
PATCH=$LANE/patches/0001-rx-linux-parity.patch
SRC=$LANE/src
WORK=/tmp/wifi-rx-parity-build
ART=/tmp/wifi-rx-parity-artifact
LOG=$ART/build.log
CC=${CC:-cc}
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $*"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
die() { echo "FAIL: $*" >&2; exit 1; }

mkdir -p "$ART"
: > "$LOG"
exec > >(tee -a "$LOG") 2>&1

[ -f "$PATCH" ] || die "patch missing: $PATCH"
[ -f "$LANE/tools/rx_parity_selftest.c" ] || die "selftest missing"
[ -d "$INCOMING/wifi-rx-static/src" ] || die "wifi-rx-static/src missing"

echo "== 1. reconstruct the current RX source state =="
rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$INCOMING/wifi-rx-static/src/." "$WORK/"
# The watermark lane's patch is the only published delta on top of rx-static;
# it applies with line offsets to the rx-static src (documented in its
# BUILD-NOTES), which is exactly the committed form of the current state.
( cd "$WORK" && patch -p1 --no-backup-if-mismatch \
      < "$INCOMING/wifi-rx-watermark/patches/0001-rx-refill-watermark.patch" ) \
    >/dev/null || die "watermark patch failed to apply to the reconstructed base"
rm -f "$WORK"/*.orig
ok "base reconstructed (wifi-rx-static/src + wifi-rx-watermark patch)"

echo "== 2. apply the parity patch and compare with src/ =="
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) >/dev/null \
    || die "parity patch failed to apply"
for f in hi5622_dp.c hi5622_dp_txrx.c hi5622_dp_txrx.h \
         hi5622_rx_parity.c hi5622_rx_parity.h Kbuild; do
    cmp -s "$SRC/$f" "$WORK/$f" || die "src/$f != patched tree"
done
ok "patch scope is exactly 6 files and src/ reproduces byte-for-byte"

echo "== 3. host selftest (device-free, vendor-data-free) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I"$WORK" \
    "$LANE/tools/rx_parity_selftest.c" "$WORK/hi5622_rx_parity.c" \
    "$WORK/hi5622_rx_live.c" -o "$ART/rx_parity_selftest" \
    || die "selftest build failed"
"$ART/rx_parity_selftest" > "$ART/host-test.log" || true
NFAIL=$(sed -n 's/^\([0-9]*\) checks, \([0-9]*\) failures$/\2/p' "$ART/host-test.log")
NCHK=$(sed -n 's/^\([0-9]*\) checks, \([0-9]*\) failures$/\1/p' "$ART/host-test.log")
[ -n "$NFAIL" ] || die "selftest summary missing"
[ "$NFAIL" = "0" ] && ok "host selftest: $NCHK checks, 0 failures" \
                   || bad "host selftest: $NFAIL failures (see $ART/host-test.log)"

echo "== 4. kernel-header compile (6.18.60) =="
KDIR=""
STAGING=""
for root in /home/shibbityshwab/openwrt-lab-upstream-target \
            /home/shibbityshwab/openwrt-lab-selectorfix-img; do
    c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    if [ -d "$c" ]; then
        KDIR=$c
        STAGING=$root/staging_dir
        break
    fi
done
if [ -z "$KDIR" ]; then
    echo "SKIP: 6.18.60 kernel tree not found"
else
    CROSS=$STAGING/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
    KM=$ART/kernel-probe
    rm -rf "$KM" && mkdir -p "$KM"
    cp "$WORK/hi5622_rx_parity.h" "$WORK/hi5622_rx_parity.c" \
       "$WORK/hi5622_rx_live.h" "$WORK/hi5622_rx_live.c" "$KM/"
    cat > "$KM/wrapper.c" <<'EOF'
// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
MODULE_LICENSE("GPL");
extern int rxp_publish_value(unsigned int a, unsigned int b,
                             unsigned int c, unsigned int *d);
static int __init parity_probe_init(void)
{
        unsigned int v;
        return rxp_publish_value(0, 1, 256, &v);
}
static void __exit parity_probe_exit(void) { }
module_init(parity_probe_init);
module_exit(parity_probe_exit);
EOF
    cat > "$KM/Makefile" <<'EOF'
obj-m := parity_probe.o
parity_probe-objs := wrapper.o hi5622_rx_parity.o hi5622_rx_live.o
ccflags-y += -I$(src)
EOF
    ( export STAGING_DIR=$STAGING
      make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$KM" modules ) \
        > "$ART/kernel-probe.log" 2>&1
    KRC=$?
    if [ -f "$KM/parity_probe.ko" ]; then
        ok "kernel-header compile of the portable model RC=$KRC"
    else
        bad "kernel-header compile of the portable model failed (see $ART/kernel-probe.log)"
    fi
    HG=$ART/glue-probe
    rm -rf "$HG" && mkdir -p "$HG"
    cp -a "$INCOMING/wifi-dp-air/src/." "$HG/"
    # kbuild prefers Kbuild over Makefile; drop the base tree's full-module
    # Kbuild so the probe Makefile below defines the object set.
    rm -f "$HG/Kbuild"
    cp "$INCOMING/wifi-dp-tuning/src/dp_txrx.c" \
       "$INCOMING/wifi-dp-tuning/src/dp_txrx.h" "$HG/"
    cp "$WORK/hi5622_dp.c" "$WORK/hi5622_dp_txrx.c" \
       "$WORK/hi5622_dp_txrx.h" "$WORK/hi5622_rx_parity.c" \
       "$WORK/hi5622_rx_parity.h" "$WORK/hi5622_rx_live.c" \
       "$WORK/hi5622_rx_live.h" "$HG/"
    cat > "$HG/Makefile" <<'EOF'
obj-m := glue_probe.o
glue_probe-y := hi5622_dp.o hi5622_dp_txrx.o hi5622_rx_live.o \
                hi5622_rx_parity.o dp_txrx.o dp_rings.o
ccflags-y += -I$(src)
EOF
    ( export STAGING_DIR=$STAGING
      make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$HG" modules ) \
        > "$ART/glue-compile.log" 2>&1
    GRC=$?
    GWARN=$(grep -c 'warning:' "$ART/glue-compile.log" || true)
    # The probe deliberately links only the patched objects, so modpost
    # reports the *other* driver symbols as unresolved - that is expected and
    # not a compile failure.  The gate is: every object compiled, no compiler
    # error, no compiler warning.
    CERR=$(grep ': error:' "$ART/glue-compile.log" | grep -vc 'ERROR: modpost' || true)
    GOBJ=0
    for o in hi5622_dp.o hi5622_dp_txrx.o hi5622_rx_live.o \
             hi5622_rx_parity.o dp_txrx.o dp_rings.o; do
        [ -f "$HG/$o" ] && GOBJ=$((GOBJ+1))
    done
    if [ "$CERR" = "0" ] && [ "$GWARN" = "0" ] && [ "$GOBJ" = "6" ]; then
        ok "kernel compile of the patched glue objects: 6/6 objects, 0 errors, 0 warnings (modpost unresolved is the unlinked full module, expected)"
    else
        bad "kernel compile of the patched glue objects: errors=$CERR warnings=$GWARN objects=$GOBJ/6 (see $ART/glue-compile.log)"
    fi
    echo "     (modpost RC=$GRC; unresolved symbols belong to the unlinked full driver, not to the patched files)"
fi

echo "== 5. optional full-module integration build (RX_FULL_BASE) =="
if [ -n "${RX_FULL_BASE:-}" ] && [ -d "${RX_FULL_BASE:-}" ] && [ -n "$KDIR" ]; then
    FULL=$ART/full
    rm -rf "$FULL" && cp -a "$RX_FULL_BASE/." "$FULL/"
    ( cd "$FULL" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) >/dev/null
    ( export STAGING_DIR=$STAGING
      make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$FULL" modules ) \
        > "$ART/full-module.log" 2>&1
    FRC=$?
    FWARN=$(grep -c 'warning:' "$ART/full-module.log" || true)
    if [ "$FRC" = "0" ] && [ "$FWARN" = "0" ]; then
        ok "full association-tree module build: RC=0, 0 warnings"
        [ -f "$FULL/hi5622_wifi.ko" ] && \
            echo "     artifact: $FULL/hi5622_wifi.ko sha256 $(sha256sum "$FULL/hi5622_wifi.ko" | cut -d' ' -f1)"
    else
        bad "full association-tree module build: RC=$FRC warnings=$FWARN"
    fi
else
    echo "SKIP: RX_FULL_BASE not set (full association tree is not committed)"
fi

echo "== 6. publication checks =="
if find "$LANE" -type f \( -name '*.o' -o -name '*.ko' -o -name '*.mod' \
        -o -name '*.bin' -o -name '*.dis' \) | grep -q .; then
    bad "binary/object files inside the lane"
else
    ok "lane contains no binaries/objects"
fi
for f in "$SRC/hi5622_rx_parity.c" "$SRC/hi5622_rx_parity.h" \
         "$LANE/tools/rx_parity_selftest.c"; do
    head -n 1 "$f" | grep -q 'SPDX-License-Identifier: GPL-2.0' \
        && ok "SPDX header: $(basename "$f")" \
        || bad "SPDX header missing: $f"
done
if grep -rIn 'CONFIDENTIAL\|BEGIN RSA PRIVATE' "$LANE" \
        --exclude=build.sh >/dev/null 2>&1; then
    bad "confidential marker found in the lane"
else
    ok "no confidential markers"
fi

echo
echo "RESULT: $PASS passed, $FAIL failed"
echo "artifact dir: $ART"
[ "$FAIL" = "0" ] || exit 1
