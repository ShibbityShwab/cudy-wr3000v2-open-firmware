#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-host.sh - host-only verification for the wifi-ap-mgmt lane
# (shared task task-143).  Device-free: compiles the portable wire logic with
# a C99 compiler, runs the selftest, and applies static fail-closed gates to
# the lane sources.  No vendor binary is needed.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/wifi-ap-mgmt-hostcheck}
CC=${CC:-cc}
rm -rf "$OUT"
mkdir -p "$OUT"

echo "== 1. strict C99 compile + host selftest"
$CC -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
	-I "$LANE/src" -o "$OUT/mgmt_selftest" \
	"$LANE/src/hi5622_mgmt_logic.c" "$LANE/tools/mgmt_selftest.c"
"$OUT/mgmt_selftest" | tee "$OUT/selftest.txt"
grep -q "0 failures" "$OUT/selftest.txt"

echo "== 2. static fail-closed gates"
fail=0
gate() {
	desc=$1; shift
	if "$@" >/dev/null 2>&1; then echo "PASS $desc"; else echo "FAIL $desc"; fail=1; fi
}
gate "mgmt_tx module param compiled default is 0 (no initializer)" \
	grep -q '^static unsigned int hi5622_mgmt_tx;$' "$LANE/src/hi5622_mgmt.c"
gate "mgmt_rx module param compiled default is 0 (no initializer)" \
	grep -q '^static unsigned int hi5622_mgmt_rx;$' "$LANE/src/hi5622_mgmt.c"
gate "mgmt_tx=0 refuses before any build/post" \
	grep -q 'if (!hi5622_mgmt_tx) {' "$LANE/src/hi5622_mgmt.c"
gate "carrier gate: values >1 refused" \
	grep -q 'hi5622_mgmt_tx > 1u' "$LANE/src/hi5622_mgmt.c"
gate "AP-only policy (non-AP mgmt_tx refused)" \
	grep -q 'wdev->iftype != NL80211_IFTYPE_AP' "$LANE/src/hi5622_mgmt.c"
gate "frame pointer is the coherent DMA address (devva checked)" \
	grep -q 'p.frame_devaddr = (u32)mgmt_state.tx_dma' "$LANE/src/hi5622_mgmt.c"
gate "vendor WAL cmd 0x01F2 body 12 in the portable logic" \
	grep -q 'HIMG_WAL_CMD_MGMT_TX	0x01F2u' "$LANE/src/hi5622_mgmt_logic.h"
gate "vendor envelope overhead 32 bytes" \
	grep -q 'HIMG_WAL_OVERHEAD	32u' "$LANE/src/hi5622_mgmt_logic.h"
gate "D2H RX group1 id13 pinned" \
	grep -q 'HIMG_RX_ID		13u' "$LANE/src/hi5622_mgmt_logic.h"
gate "D2H rx info 48 B / channel+7 / len+17 / rssi+39 pinned" \
	sh -c 'grep -q "HIMG_RX_INFO_LEN	48u" "$1" && grep -q "HIMG_RX_INFO_CHANNEL	7u" "$1" && grep -q "HIMG_RX_INFO_FRAME_LEN	17u" "$1" && grep -q "HIMG_RX_INFO_RSSI	39u" "$1"' sh "$LANE/src/hi5622_mgmt_logic.h"
gate "RX delivery goes through cfg80211_rx_mgmt, never a device write" \
	grep -q 'cfg80211_rx_mgmt(wdev, rx.freq' "$LANE/src/hi5622_mgmt.c"
gate "TX post goes through the fail-closed frame gate" \
	grep -q 'hi5622_frame_post(c->ctx, wire, wire_len, "mgmt_tx", NULL)' "$LANE/src/hi5622_mgmt.c"

if [ -f "$LANE/patches/0001-mgmt-tx-rx.patch" ]; then
	P="$LANE/patches/0001-mgmt-tx-rx.patch"
	gate "patch adds cap bit 15 exactly once" \
		sh -c '[ "$(grep -c "HI5622_FRAME_CAP_MGMT_TX	(1u << 15)" "$1")" = 1 ]' sh "$P"
	gate "patch wires the provider builder" \
		grep -q 'build_mgmt_tx = vapscan_mgmt_tx' "$P"
	gate "patch replaces the fail-closed stub with the gated op" \
		grep -q 'return hi5622_mgmt_op_tx(wiphy, wdev, params, cookie);' "$P"
	gate "patch hooks the D2H delivery" \
		grep -q 'hi5622_mgmt_deliver(ctx, hdr, payload, payload_avail, wire_len);' "$P"
	gate "patch adds the two cfg80211 imports (12-line symvers contract)" \
		sh -c '[ "$(grep -c "cfg80211_rx_mgmt_ext" "$1")" -ge 1 ] && [ "$(grep -c "cfg80211_mgmt_tx_status_ext" "$1")" -ge 1 ]' sh "$P"
fi

echo
if [ "$fail" -eq 0 ]; then
	echo "build-host: PASS (selftest + static fail-closed gates)"
else
	echo "build-host: FAIL"; exit 1
fi
