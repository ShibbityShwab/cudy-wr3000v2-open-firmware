#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-check.sh - fast host gate for the wifi-sta-ops lane
# (shared task task-149).  HOST-ONLY: compiles/runs the portable builder +
# parser selftest and cross-checks the committed patch/docs/ladder.  No
# kernel build, no device, no vendor artifact needed (the RE tool runs only
# when the local, never-committed artifacts are passed in the environment).
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
BIN=/tmp/wifi-sta-ops-hostcheck/sta_ops_selftest
mkdir -p /tmp/wifi-sta-ops-hostcheck

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

say "== 1. portable builder + RX-parser host selftest"
cc -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE_DIR/src" \
	"$LANE_DIR/tools/sta_ops_selftest.c" \
	"$LANE_DIR/src/hi5622_sta_ops_logic.c" -o "$BIN"
"$BIN"

say "== 2. patch content (module patch must touch every claimed file)"
P="$LANE_DIR/patches/0001-sta-ops.patch"
for f in hi5622_sta_ops.c hi5622_sta_ops.h hi5622_sta_ops_logic.c \
	 hi5622_sta_ops_logic.h hi5622_cfg80211_ops.c hi5622_cfg80211.c \
	 hi5622_cfg80211.h hi5622_dp_txrx.c Kbuild; do
	must_grep "$P" "b/$f" "patch adds/edits $f"
done
for pat in hi5622_sta_ops_add hi5622_sta_ops_change hi5622_sta_ops_del \
	   hi5622_sta_ops_rx_deliver hi5622_sta_ops_xmit_drop \
	   "sta_user_del" "eapol_rx" "netif_rx" "hsop_build_user_add" \
	   "hsop_build_asoc_syn" "hsop_rx_to_eth"; do
	must_grep "$P" "$pat" "patch carries $pat"
done

say "== 3. decoded wire facts (header + tables)"
H="$LANE_DIR/src/hi5622_sta_ops_logic.h"
for pat in "HSOP_USER_ADD_BODY_LEN	168u" \
	   "HSOP_USER_DEL_BODY_LEN	12u" \
	   "HSOP_CFG_CMD_ASOC_SYN	0x07d2u" \
	   "HSOP_ASOC_SYN_BODY_LEN	4u" \
	   "HSOP_WAL_KICK_CMD	0x00d0u" \
	   "HSOP_ETH_P_PAE		0x888eu" \
	   "HSOP_EAPOL_KEY_MIN	99u" \
	   "HSOP_EAPOL_KEY_PAIRWISE	0x0008u" \
	   "HSOP_ASOC_STATE_ASSOC	1u"; do
	must_grep "$H" "$pat" "header constant"
done
for pat in "0x83e25" "0x10c23c" "0x07d2" "0x00d0" "0x102470" \
	   "0x1048f0" "0x103ac8" "180" "24 B" "32 B" "0xc0" "0x888e" "0x5e"; do
	must_grep "$LANE_DIR/TABLES.md" "$pat" "TABLES records $pat"
done

say "== 4. fail-closed policy + no secret/no vendor binary"
must_grep "$LANE_DIR/src/hi5622_sta_ops.c" "module_param_named(sta_ops" "sta_ops module param"
must_grep "$LANE_DIR/src/hi5622_sta_ops.c" "module_param_named(eapol_rx" "eapol_rx module param"
must_grep "$LANE_DIR/src/hi5622_sta_ops.c" "fails closed" "fail-closed default documented"
must_not_grep "$LANE_DIR/patches/0001-sta-ops.patch" "BEGIN RSA PRIVATE[ ]KEY" "patch has no private key"
if find "$LANE_DIR" -type f \( -name '*.ko' -o -name '*.bin' -o -name '*.o' \) | grep -q .; then
	say "  FAIL lane contains a binary artifact"
	fail=1
else
	say "  ok   no .ko/.bin/.o under the lane directory"
fi
if grep -RIlq 'v2.5.24/lib/modules' "$LANE_DIR/patches" 2>/dev/null; then
	say "  FAIL patch references a local vendor path"
	fail=1
else
	say "  ok   patch carries no local vendor path"
fi

say "== 5. ladder"
python3 -m json.tool "$LANE_DIR/ladders/sta-assoc-eapol.json" >/dev/null
say "  ok   ladders/sta-assoc-eapol.json parses"
must_grep "$LANE_DIR/ladders/sta-assoc-eapol.json" '"not_fired_by_this_lane": true' "ladder marked not fired"
must_grep "$LANE_DIR/LADDER.md" "Abort policy" "abort policy present"
must_grep "$LANE_DIR/LADDER.md" "no bench contact" "host-only rule stated"

say "== 6. evidence <-> refs consistency"
REF="$LANE_DIR/refs/artifact-identity.txt"
BV="$LANE_DIR/evidence/build-sta-ops.txt"
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

say "== 7. optional: re-derive the vendor/firmware facts"
set +u
KO_ARG=$STA_OPS_KO
PD4_ARG=$STA_OPS_PD4
PY=$STA_OPS_PY
set -u
if [ -z "$PY" ]; then PY=python3; fi
if [ -n "$KO_ARG" ] && [ -n "$PD4_ARG" ]; then
	if [ -x "$LANE_DIR/tools/sta_ops_re.py" ]; then
		if "$PY" "$LANE_DIR/tools/sta_ops_re.py" "$KO_ARG" "$PD4_ARG" \
			> /tmp/wifi-sta-ops-hostcheck/re.txt; then
			say "  ok   sta_ops_re.py: all derived facts match"
		else
			say "  FAIL sta_ops_re.py reported a mismatch"
			tail -5 /tmp/wifi-sta-ops-hostcheck/re.txt
			fail=1
		fi
	else
		say "  skip sta_ops_re.py not executable"
	fi
else
	say "  skip STA_OPS_KO/STA_OPS_PD4 not set (local artifacts are never committed)"
fi

say ""
if [ "$fail" -ne 0 ]; then
	say "wifi-sta-ops host check: FAILURES"
	exit 1
fi
say "wifi-sta-ops host check: PASS"
