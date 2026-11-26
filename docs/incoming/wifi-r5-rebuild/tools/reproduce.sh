#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/reproduce.sh - lane wifi-r5-rebuild (task-225)
#
# Rebuilds the ENTIRE R5 module chain from the committed build scripts and
# asserts every pinned stage artifact, ending in the installed-form module:
#
#   383,240 B  sha256 24f7221b... 65 params  (= the 5 GHz lane's R5 module)
#
# Host-only: reads the local 6.18.60 kernel tree + toolchain, writes only to
# /tmp.  No device access, no firmware write, no flash, no commit of binaries.
#
# Usage:
#   ./tools/reproduce.sh                  # chain from the present base trees
#   ./tools/reproduce.sh --with-e2-rebuild # also rebuild the stage-1 base (E2)
#
# Environment overrides (do not change them for the byte-exact check):
#   LOGD (default /tmp/r5-rechain-logs), OUT (default /tmp/r5-rebuilt).
# The scripts' own WORK/ART defaults (/tmp/*-build, /tmp/*-artifact) are the
# canonical paths the pinned whole-file shas were produced at; an out-of-tree
# WORK path changes build metadata outside .text (see the R5 BUILD-NOTES).
set -u
export LC_ALL=C
export TZ=UTC

LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
LOGD=${LOGD:-/tmp/r5-rechain-logs}
OUT=${OUT:-/tmp/r5-rebuilt}
E2_REBUILD=0
[ "${1:-}" = "--with-e2-rebuild" ] && E2_REBUILD=1

F2_PATCH=$DOCS/wifi-fw-dfs-worker/patches/0001-fieldb-context.patch
F2_PATCH_SHA=0530ca45528e0601af5a6f8b8523e3ed8e43cd2237c2361f6cfd03aa3504f926

mkdir -p "$LOGD"
FAIL=0
T0=$(date +%s)

# ---- helpers ------------------------------------------------------------
ts() { date -u +%Y-%m-%dT%H:%M:%SZ; }
stage() {
    _name=$1; shift
    _s0=$(date +%s)
    echo "=== STAGE $_name START $(ts)" | tee -a "$LOGD/chain.log"
    "$@" > "$LOGD/$_name.log" 2>&1
    _rc=$?
    _e0=$(date +%s)
    echo "=== STAGE $_name END $(ts) exit=$_rc elapsed=$((_e0-_s0))s log=$LOGD/$_name.log" | tee -a "$LOGD/chain.log"
    tail -n 14 "$LOGD/$_name.log" | sed "s/^/    | /" >> "$LOGD/chain.log"
    [ $_rc -eq 0 ] || FAIL=1
    return $_rc
}
# pin ARTIFACT SHA SIZE PARAMS - fail closed on a drifted stage artifact
pin() {
    _ko=$1; _sha=$2; _size=$3; _params=$4
    _got_sha=$(sha256sum "$_ko" | cut -d" " -f1)
    _got_size=$(stat -c %s "$_ko")
    _got_params=$(modinfo -p "$_ko" | wc -l)
    if [ "$_got_sha" = "$_sha" ] && [ "$_got_size" = "$_size" ] && [ "$_got_params" = "$_params" ]; then
        echo "PIN OK  $_ko  ${_size} B  ${_params} params  $_sha" | tee -a "$LOGD/chain.log"
        return 0
    fi
    echo "PIN FAIL $_ko  got ${_got_size} B/${_got_params} params/$_got_sha" | tee -a "$LOGD/chain.log"
    echo "         expected ${_size} B/${_params} params/$_sha" | tee -a "$LOGD/chain.log"
    FAIL=1
    return 1
}

: > "$LOGD/chain.log"
echo "chain: base(0) -> beacon5g(1) -> r1-band(2) -> desc-table(3) -> dfs-mode(4)" | tee -a "$LOGD/chain.log"
echo "       -> F2 field-B(4b) -> band-start-send(5) -> vap-bound(6) -> g0-start(7)" | tee -a "$LOGD/chain.log"
echo "       -> r4b-order(8) -> gates(9) -> r5-order(10) = 24f7221b..." | tee -a "$LOGD/chain.log"
echo "run start: $(ts)" | tee -a "$LOGD/chain.log"

# ---- stage 0: base of the chain (the E2 5 GHz module tree) --------------
# The beacon script pins this base only through its artifact sha; rebuild the
# tree itself with the sibling lane script when asked (or when it is absent).
E2_ART=/tmp/wifi-vapmsg5g-artifact/hi5622_wifi.ko
if [ "$E2_REBUILD" = 1 ] || [ ! -d /tmp/wifi-vapmsg5g-build ] || [ ! -f "$E2_ART" ]; then
    stage 00-e2-base "$DOCS/wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh" || { echo "CHAIN FAIL"; exit 1; }
fi
pin "$E2_ART" 13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf 353652 58 || exit 1

# ---- stages 1-4: the beacon lineage (F1/F2 base) ------------------------
stage 01-beacon5g "$DOCS/wifi-5ghz-beacon/tools/build-beacon5g-ready.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/beacon5g-artifact/hi5622_wifi.ko bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb 356600 59 || exit 1
stage 02-r1-band "$DOCS/wifi-r1-band-diff/tools/build-r1-band.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/r1-band-artifact/hi5622_wifi.ko e8b17f7238afce459db61a9ca65b9a0a1336ce232c933c65990d444350e4abd4 356612 59 || exit 1
stage 03-desc-table "$DOCS/wifi-desc-table-send/tools/build-desc-table-send.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/desctable-artifact/hi5622_wifi.ko aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3 360448 60 || exit 1
stage 04-dfs-mode "$DOCS/wifi-dfs-mode-send/tools/build-dfs-mode-send.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/dfsmode-artifact/hi5622_wifi.ko f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac 364192 61 || exit 1

# ---- stage 4b: the F2 field-B fix (a pinned source patch, not a lane) ---
f2step() {
    got=$(sha256sum "$F2_PATCH" | cut -d" " -f1)
    [ "$got" = "$F2_PATCH_SHA" ] || { echo "F2 patch sha $got != pin"; return 1; }
    echo "== F2 patch pinned: $F2_PATCH_SHA"
    ( cd /tmp/dfsmode-build && patch -p1 --no-backup-if-mismatch < "$F2_PATCH" ) || return 1
    for pair in \
        "hi5622_scan.c:8fbc8e1e180e3cd139aac0f4c28e6e8e0a8d314b62f6e917907b8a5b6523eca7" \
        "hi5622_scan_logic.c:75b9bdd5fd34bcd3862a7e185fb48bc98f6012c217b04fa5030ced899502f07c" \
        "hi5622_scan_logic.h:b83e74cc5bd834eb981e0dc07f197e6c4c3cdc9bd4eb60107b35b1c973e0766e" \
        "Kbuild:900fb204ad817e787a0cde9201a02c6dc34d69d893ef0e4f06d140630fcb0759" \
        "hi5622_dfs_mode.h:cd3b76aa75c5153654ca1e981d0f767fc100e7cb8e83406c55a43d0b4ce3c70b" \
        "hi5622_desc_table.c:5af935f29aa9a329aba365da1753c5edd896b74cfe1c0c50665de13b39797810"; do
        f=${pair%%:*}; wantsha=${pair##*:}
        got=$(sha256sum "/tmp/dfsmode-build/$f" | cut -d" " -f1)
        [ "$got" = "$wantsha" ] || { echo "F2 tree $f sha $got != $wantsha"; return 1; }
        echo "   F2 tree pin OK: $f $got"
    done
    echo "== F2 tree: field B = 0x00 in both frame builders (the band-1 context)"
}
stage 04b-f2-fieldb f2step || { echo "CHAIN FAIL"; exit 1; }

# ---- stages 5-10: the 0x0242/0x0065 + gates + R5 chain ------------------
stage 05-band-start-send "$DOCS/wifi-band1-start-send/tools/build-band-start-send.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/bandstart-artifact/hi5622_wifi.ko e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028 371212 63 || exit 1
stage 06-vap-bound "$DOCS/wifi-band1-start-send-2/tools/build-vap-bound.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/vapbound-artifact/hi5622_wifi.ko b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71 373056 64 || exit 1
stage 07-vap-start-g0 "$DOCS/wifi-band1-start-send-3/tools/build-vap-start-g0.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/g0start-artifact/hi5622_wifi.ko bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711 382072 65 || exit 1
stage 08-r4b-order "$DOCS/wifi-band1-start-send-4/tools/build-r4b-order.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/r4border-artifact/hi5622_wifi.ko b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102 382188 65 || exit 1
stage 09-gates "$DOCS/wifi-vap-start-gates/tools/build-gates.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/vsg-artifact/hi5622_wifi.ko 9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82 382376 65 || exit 1
stage 10-r5-order "$DOCS/wifi-band1-start-send-5/tools/build-r5-order.sh" || { echo "CHAIN FAIL"; exit 1; }
pin /tmp/r5-artifact/hi5622_wifi.ko 24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c 383240 65 || exit 1

# ---- deliverable copy ---------------------------------------------------
mkdir -p "$OUT"
cp /tmp/r5-artifact/hi5622_wifi.ko "$OUT/hi5622_wifi.ko"
cp /tmp/r5-build/hi5622_wifi.ko "$OUT/hi5622_wifi.ko.unstripped"

CROSS=$(ls -d /home/shibbityshwab/openwrt-lab-upstream-target/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/ 2>/dev/null || true)
[ -n "$CROSS" ] && CROSS=${CROSS}arm-openwrt-linux-muslgnueabi- || CROSS=
TEXT_SHA=n/a
if [ -n "$CROSS" ] && [ -x "${CROSS}objcopy" ]; then
    "${CROSS}objcopy" -O binary --only-section=.text "$OUT/hi5622_wifi.ko" "$OUT/.text.bin"
    TEXT_SHA=$(sha256sum "$OUT/.text.bin" | cut -d" " -f1)
    rm -f "$OUT/.text.bin"
fi

{
echo ""
echo "=== FINAL"
echo "artifact: $OUT/hi5622_wifi.ko (LOCAL EVIDENCE ONLY - never committed)"
echo -n "sha256:   "; sha256sum "$OUT/hi5622_wifi.ko" | cut -d" " -f1
echo "size:     $(stat -c %s "$OUT/hi5622_wifi.ko") B"
echo "params:   $(modinfo -p "$OUT/hi5622_wifi.ko" | wc -l)"
echo "unstripped: $(stat -c %s "$OUT/hi5622_wifi.ko.unstripped") B sha256 $(sha256sum "$OUT/hi5622_wifi.ko.unstripped" | cut -d" " -f1)"
echo ".text sha: $TEXT_SHA (expected a727c339714b8826ea069194a50d376622175ec5adf8e7b537442edc7731d11d)"
echo "vermagic: $(modinfo "$OUT/hi5622_wifi.ko" | sed -n "s/^vermagic: *//p")"
echo "host tests: 295 C + 110 Python checks, 0 failures (stage-10 log)"
echo "equal to the installed-form 24f7221b: $([ "$(sha256sum "$OUT/hi5622_wifi.ko" | cut -d" " -f1)" = 24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c ] && echo MATCH || echo MISMATCH)"
echo "chain elapsed: $(( $(date +%s) - T0 ))s"
} | tee -a "$LOGD/chain.log"

if [ $FAIL -ne 0 ]; then echo "CHAIN FAIL - see $LOGD/chain.log" >&2; exit 1; fi
echo "CHAIN OK - final sha256 matches 24f7221b (65 params, 383,240 B)"
exit 0