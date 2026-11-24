#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-check.sh - fast host gate for the wifi-data-tx lane
# (shared task task-150).  HOST-ONLY: compiles/runs the portable logic
# selftest and cross-checks the committed patch/docs/ladder.  No kernel build,
# no device, no vendor artifact needed.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
BIN=/tmp/wifi-data-tx-hostcheck/data_tx_selftest
mkdir -p /tmp/wifi-data-tx-hostcheck

fail=0
say() { printf '%s\n' "$*"; }
must_grep() { # $1 = file, $2 = pattern, $3 = what
	if grep -q -- "$2" "$1"; then
		say "  ok   $3"
	else
		say "  FAIL $3 ($1 missing: $2)"
		fail=1
	fi
}
must_not_grep() {
	if grep -q -- "$2" "$1" 2>/dev/null; then
		say "  FAIL $3 ($1 contains: $2)"
		fail=1
	else
		say "  ok   $3"
	fi
}

say "== 1. portable logic host selftest (C99 -Wall -Wextra -Werror)"
cc -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
	"$LANE/tools/data_tx_selftest.c" \
	"$LANE/src/hi5622_data_tx_logic.c" -o "$BIN"
"$BIN"

say "== 2. patch content (every claimed file + every claimed hook)"
P="$LANE/patches/0001-data-tx.patch"
for f in Kbuild hi5622_driver.h hi5622_dp_txrx.h hi5622_dp_txrx.c \
	 hi5622_cfg80211.h hi5622_cfg80211_ops.c hi5622_cfg80211.c \
	 hi5622_data_tx.c hi5622_data_tx.h hi5622_data_tx_logic.c \
	 hi5622_data_tx_logic.h; do
	must_grep "$P" "b/$f" "patch adds/edits $f"
done
for pat in hi5622_data_tx_init hi5622_data_tx_xmit hi5622_data_tx_remove \
	   hi5622_data_tx_status hi5622_data_tx_probe_store \
	   hi5622_dp_tx_data_post hi5622_dp_tx_data_poll \
	   data_tx_probe tx_post_lock hdtx_build_data_mpdu \
	   hdtx_classify_eapol hdtx_decide_tx hdtx_ring_advance \
	   hdtx_frame_write hdtx_buffer_devva; do
	must_grep "$P" "$pat" "patch carries $pat"
done
must_grep "$P" "if (hi5622_data_tx_enable)" "ndo_start_xmit routes to the data path when enabled"
must_grep "$P" "hi5622_sta_ops_xmit_drop" "fail-closed default keeps the accounted drop"
must_grep "$P" "hi5622_ca_to_bar0\|hi5622_dp_tx_data" "data path reuses the proven dp helpers"

say "== 3. wire facts (literal constants in the portable header)"
H="$LANE/src/hi5622_data_tx_logic.h"
for pat in "HDTX_FRAME_HOST_HDR	4u" \
	   "HDTX_FC_DATA_FROMDS	(HDTX_FC_DATA | HDTX_FC_FROMDS)" \
	   "HDTX_FC_QOS_DATA_FROMDS	(HDTX_FC_QOS_DATA | HDTX_FC_FROMDS)" \
	   "HDTX_FC_PROTECTED	0x4000u" \
	   "HDTX_EAPOL_KEY_INFO_OFF	5u" "HDTX_EAPOL_KEYDLEN_OFF	97u" \
	   "HDTX_EAPOL_KEY_MINLEN	95u" "HDTX_ETH_P_PAE		0x888eu" \
	   "HDTX_KEY_INFO_PAIRWISE	0x0008u" "HDTX_LLC_HDR_LEN	6u"; do
	must_grep "$H" "$pat" "header constant"
done
for pat in "hmac_tx_msdu_dscr_add" "hmac_tx_frame_control_dscr_set" \
	   "hmac_is_eapol_key_ptk_4_4" "8x64x8" \
	   "0x0078" "0x0008" "vap type 2" "FromDS=1" ; do
	must_grep "$LANE/TABLES.md" "$pat" "TABLES records $pat"
done

say "== 4. fail-closed policy + no secrets / no vendor binary"
must_grep "$LANE/src/hi5622_data_tx.c" "module_param_named(data_tx," "data_tx module param"
must_grep "$LANE/src/hi5622_data_tx.c" "module_param_named(data_tx_peer," "data_tx_peer module param"
must_grep "$LANE/src/hi5622_data_tx.c" "module_param_named(data_tx_protect," "data_tx_protect module param"
must_grep "$LANE/src/hi5622_data_tx.c" "module_param_named(data_tx_timeout_ms," "data_tx_timeout_ms module param"
must_grep "$LANE/src/hi5622_data_tx.c" "fail-closed default" "fail-closed default documented in code"
must_not_grep "$LANE/src/hi5622_data_tx.c" "hi5622_data_tx_enable = 1" "data_tx default is not 1"
must_not_grep "$P" "BEGIN RSA PRIVATE KEY" "patch has no private key"
must_not_grep "$P" "wpa_hexdump\|print_hex_dump\|%*phN" "patch never hexdumps a frame/key"
if find "$LANE" -type f \( -name '*.ko' -o -name '*.bin' -o -name '*.o' -o -name '*.fw' \) | grep -q .; then
	say "  FAIL lane contains a binary artifact"
	fail=1
else
	say "  ok   no .ko/.bin/.o/.fw under the lane directory"
fi
if grep -RIlq 'v2.5.24/lib/modules\|recovery-extract.*\.ko' "$LANE/patches" 2>/dev/null; then
	say "  FAIL patch references a local vendor path"
	fail=1
else
	say "  ok   patch carries no local vendor path"
fi

say "== 5. ladder"
python3 -m json.tool "$LANE/ladders/data-tx.json" >/dev/null
say "  ok   ladders/data-tx.json parses"
must_grep "$LANE/ladders/data-tx.json" '"not_fired_by_this_lane": true' "ladder marked not fired"
must_grep "$LANE/ladders/data-tx.json" '"bench_contact": false' "ladder states no bench contact"
must_grep "$LANE/LADDER.md" "Abort policy" "abort policy present"
must_grep "$LANE/LADDER.md" "No bench contact" "host-only rule stated"
must_grep "$LANE/LADDER.md" "recovery door" "recovery door required"

say "== 6. evidence <-> refs consistency"
REF="$LANE/refs/artifact-identity.txt"
BV="$LANE/evidence/build-data-tx.txt"
[ -f "$REF" ] || { say "  FAIL missing $REF"; fail=1; }
[ -f "$BV" ] || { say "  FAIL missing $BV"; fail=1; }
if [ -f "$REF" ] && [ -f "$BV" ]; then
	C_SHA=$(sed -n 's/^chain-id  : all-patch base artifact asserted \([0-9a-f]*\).*/\1/p' "$BV")
	P_SHA=$(sed -n 's/^stripped  : [0-9]* B sha256 //p' "$BV")
	P_SIZE=$(sed -n 's/^stripped  : \([0-9]*\) B .*/\1/p' "$BV")
	U_SHA=$(sed -n 's/^unstripped: [0-9]* B sha256 //p' "$BV")
	must_grep "$REF" "$C_SHA" "refs carry the asserted all-patch chain sha"
	must_grep "$REF" "$P_SHA" "refs carry the patched stripped sha"
	must_grep "$REF" "$P_SIZE" "refs carry the patched stripped size"
	must_grep "$REF" "$U_SHA" "refs carry the patched unstripped sha"
fi

say ""
if [ "$fail" -ne 0 ]; then
	say "wifi-data-tx host check: FAILURES"
	exit 1
fi
say "wifi-data-tx host check: PASS"
