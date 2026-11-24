#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-check.sh - fast host gate for the wifi-wpa-static lane
# (shared task task-139).  HOST-ONLY: compiles/runs the portable builder
# selftest and cross-checks the committed patch/docs/evidence; no kernel
# build, no device.  The heavy compile gate is build-wpa-static-module.sh.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
BIN=/tmp/wifi-wpa-static-build/key_selftest
mkdir -p /tmp/wifi-wpa-static-build

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

say "== 1. portable builder host selftest"
cc -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE_DIR/src" \
	"$LANE_DIR/tools/key_selftest.c" "$LANE_DIR/src/hi5622_key_frames.c" \
	-o "$BIN"
"$BIN"

say "== 2. patch content"
P="$LANE_DIR/patches/0001-wpa-static.patch"
for f in hi5622_key_frames.c hi5622_key_frames.h hi5622_keys.c hi5622_keys.h \
	 hi5622_frames_api.h hi5622_cfg80211_ops.c hi5622_vapscan_provider.c \
	 hi5622_scan_logic.c hi5622_cfg80211_logic.c hi5622_cfg80211.c Kbuild; do
	must_grep "$P" "b/$f" "patch adds/edits $f"
done
must_grep "$P" "HI5622_FRAME_CAP_KEY_ADD" "patch adds KEY_ADD cap"
must_grep "$P" "hi5622_keys_op_add" "patch wires .add_key"
must_grep "$P" "hkf_cipher_classify" "patch carries cipher classifier"

say "== 3. decoded constants (src + tables)"
for pat in "HKF_CMD_ADD_KEY" "0x0078" "HKF_CMD_SET_DEFAULT_KEY" "0x0079" \
	   "HKF_CMD_DEL_KEY" "0x007a" "HKF_KEY_MAX" "HKF_SEQ_MAX" \
	   "HKF_CIPHER_CLASS_DATA" "HKF_CIPHER_CLASS_MGMT" "HKF_CIPHER_CLASS_WEP"; do
	must_grep "$LANE_DIR/src/hi5622_key_frames.h" "$pat" "header constant $pat"
done
for pat in "0x0A5825" "0x0A5C9D" "0x0A4271" "0x10C23C" "0x0078" "0x0079" \
	   "0x007A" "0x44" "0x28"; do
	must_grep "$LANE_DIR/TABLES.md" "$pat" "TABLES has $pat"
done
must_grep "$LANE_DIR/TABLES.md" "WARN_ON(ops->add_key" "TABLES records cfg80211 pairing rule"
must_grep "$LANE_DIR/REPORT.md" "269,688" "REPORT records chain identity"
must_grep "$LANE_DIR/REPORT.md" "282,048" "REPORT records patched artifact size"

say "== 4. fail-closed policy + no key material logging"
must_grep "$LANE_DIR/src/hi5622_keys.c" "wpa_keys" "wpa_keys module param"
must_grep "$LANE_DIR/src/hi5622_keys.c" "fail-closed default" "fail-closed default documented"
if grep -n "key\[" "$LANE_DIR/src/hi5622_keys.c" | grep -qi "dev_.*(.*key"; then
	say "  FAIL key bytes may be logged"; fail=1
else
	say "  ok   no key-material format argument in the kernel logging"
fi

say "== 5. ladder"
python3 -m json.tool "$LANE_DIR/ladders/wpa2-ap.json" >/dev/null
say "  ok   ladders/wpa2-ap.json parses"
must_grep "$LANE_DIR/ladders/wpa2-ap.json" '"not_fired_by_this_lane": true' "ladder marked not fired"
must_grep "$LANE_DIR/LADDER.md" "Abort policy" "abort policy present"

say "== 6. evidence <-> refs consistency"
REF="$LANE_DIR/refs/artifact-identity.txt"
BV="$LANE_DIR/evidence/build-wpa-static.txt"
[ -f "$REF" ] || { say "  FAIL missing $REF"; fail=1; }
[ -f "$BV" ] || { say "  FAIL missing $BV"; fail=1; }
if [ -f "$REF" ] && [ -f "$BV" ]; then
	C_SHA=$(sed -n 's/^chain-id  : [0-9]* \(.*\) (asserted).*/\1/p' "$BV")
	P_SHA=$(sed -n 's/^sha256    : //p' "$BV")
	P_SIZE=$(sed -n 's/^size      : //p' "$BV")
	must_grep "$REF" "$C_SHA" "refs carry the asserted chain sha"
	must_grep "$REF" "$P_SHA" "refs carry the patched artifact sha"
	must_grep "$REF" "$P_SIZE" "refs carry the patched artifact size"
fi

say "== 7. host-only / no vendor-binary policy"
if find "$LANE_DIR" -type f \( -name '*.ko' -o -name '*.bin' -o -name '*.o' \
	-o -name '*.fw' -o -name '*.img' \) | grep -q .; then
	say "  FAIL binary/vendor artifact found under the lane dir"; fail=1
else
	say "  ok   no binary/object/firmware files under the lane dir"
fi

say ""
if [ "$fail" = "0" ]; then
	say "run-host-check: PASS"
else
	say "run-host-check: FAILED"
	exit 1
fi
