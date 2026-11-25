#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-desc-table-send (task-188) host tests.
#
# HOST-ONLY: no device, no kernel, no firmware, no vendor blob.  Three parts:
#
#   1. the portable model in src/ - the fail-closed desc_table x
#      beacon_static x band matrix, the byte-exact 68-byte 0x00F7 frame
#      (checked against the lane spec array and re-derived field by field by
#      tools/desc_table_send_test.py), the builder refusals and the sysfs
#      status line;
#   2. the ordering contract against the PATCHED driver tree
#      (/tmp/desctable-build by default): the real hi5622_beacon_plan() must
#      put the desc_table step after vap_add and before ap_chan, and the
#      source order in hi5622_scan.c must post 'desc_table' before 'ap_chan'
#      (tools/order_test.c + tools/desc_table_send_test.py, including the
#      src/ drift guard);
#   3. the built artifact contract when tools/build-desc-table-send.sh has
#      run: 60 params incl desc_table, the feature strings, 12 symvers.
#
# Tree selection: $DESCTABLE_TREE, else /tmp/desctable-build.
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
CC=${CC:-gcc}
PYTHON=${PYTHON:-python3}
WORK=${TMPDIR:-/tmp}/wifi-desc-table-send-host-tests
rm -rf "$WORK"
mkdir -p "$WORK" "$LANE/evidence"
FAIL=0

echo '== 1. portable 0xF7 descriptor-table model (src/)'
$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$LANE/src" \
	"$LANE/src/desc_table_selftest.c" "$LANE/src/hi5622_desc_table.c" \
	-o "$WORK/desc_table_selftest"
"$WORK/desc_table_selftest" | tee "$WORK/part1.txt"
FRAME_HEX=$("$WORK/desc_table_selftest" --dump | sed -n 's/^frame://p')
[ -n "$FRAME_HEX" ] || { echo 'FAIL: no frame emitted' >&2; FAIL=1; }

TREE=${DESCTABLE_TREE:-}
if [ -z "$TREE" ]; then
	for cand in /tmp/desctable-build; do
		if [ -d "$cand" ] && [ -f "$cand/hi5622_scan.c" ]; then TREE=$cand; break; fi
	done
fi
if [ -n "$TREE" ] && [ -f "$TREE/hi5622_scan.c" ]; then
	echo "== 2. ordering contract on the patched driver tree ($TREE)"
	for f in hi5622_desc_table.c hi5622_desc_table.h; do
		if ! cmp -s "$LANE/src/$f" "$TREE/$f"; then
			echo "FAIL: src/$f drifted from the patched tree" >&2
			FAIL=1
		fi
	done
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/order_test.c" "$TREE/hi5622_scan_logic.c" \
		-o "$WORK/order_test"
	"$WORK/order_test" | tee "$WORK/part2-order.txt"
	$PYTHON "$LANE/tools/desc_table_send_test.py" --frame-hex "$FRAME_HEX" \
		--tree "$TREE" | tee "$WORK/part2-frame.txt"
else
	echo '== 2. SKIP: no patched driver tree (run tools/build-desc-table-send.sh first)'
fi

ART=/tmp/desctable-artifact/hi5622_wifi.ko
if [ -f "$ART" ]; then
	echo "== 3. built artifact contract ($ART)"
	MI=$(modinfo -p "$ART")
	NP=$(printf '%s\n' "$MI" | wc -l)
	[ "$NP" = "60" ] || { echo "FAIL: param count $NP != 60" >&2; FAIL=1; }
	for p in desc_table ap_chan_wait_ms beacon_static vap5g; do
		printf '%s\n' "$MI" | grep -q "^$p:" || {
			echo "FAIL: missing module param $p" >&2; FAIL=1; }
	done
	for s in "cfg80211: start_ap beacon_static frame %u/%u 'desc_table' posted (%zu B" \
		 "cfg80211: desc_table post: module ctx=0x%08x bandobject=0x%08x (%s)" \
		 "desc_table=%u is not a valid descriptor-table parameter" \
		 "desc_table: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u"; do
		strings -a "$ART" | grep -qF "$s" || {
			echo "FAIL: feature string missing: $s" >&2; FAIL=1; }
	done
	SIZE=$(stat -c %s "$ART")
	SHA=$(sha256sum "$ART" | cut -d' ' -f1)
	echo "artifact $SIZE B sha256 $SHA params $NP"
else
	echo '== 3. SKIP: no built artifact (run tools/build-desc-table-send.sh first)'
fi

if [ "$FAIL" != 0 ]; then
	echo 'host tests FAILED' >&2
	exit 1
fi
echo 'host tests PASS (no device contacted)'
