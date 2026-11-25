#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-dfs-mode-send (task-190) host tests.
#
# HOST-ONLY: no device, no kernel, no firmware, no vendor blob.  Three parts:
#
#   1. the portable model in src/ - the fail-closed dfs_mode x
#      beacon_static x band matrix, the byte-exact 29-byte 0x01F7 frame
#      (checked against the lane spec array and re-derived field by field by
#      tools/dfs_mode_send_test.py), the worker precheck, the builder
#      refusals and the sysfs status line;
#   2. the ordering contract against the PATCHED driver tree
#      (/tmp/dfsmode-build by default): the real hi5622_beacon_plan() must
#      put the dfs_mode step AFTER desc_table and BEFORE ap_chan, and the
#      source order in hi5622_scan.c must post 'dfs_mode' after 'desc_table'
#      and before 'ap_chan' (tools/order_test.c + tools/dfs_mode_send_test.py,
#      including the src/ drift guard);
#   3. the built artifact contract when tools/build-dfs-mode-send.sh has run:
#      61 params incl dfs_mode, the feature strings, 12 symvers.
#
# Tree selection: $DFSMODE_TREE, else /tmp/dfsmode-build.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-gcc}
PYTHON=${PYTHON:-python3}
WORK=${TMPDIR:-/tmp}/wifi-dfs-mode-send-host-tests
rm -rf "$WORK"
mkdir -p "$WORK" "$LANE/evidence"
FAIL=0

echo '== 1. portable 0x01F7 DFS-mode model (src/)'
$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$LANE/src" \
	"$LANE/src/dfs_mode_selftest.c" "$LANE/src/hi5622_dfs_mode.c" \
	-o "$WORK/dfs_mode_selftest"
"$WORK/dfs_mode_selftest" | tee "$WORK/part1.txt"
FRAME_HEX=$("$WORK/dfs_mode_selftest" --dump | sed -n 's/^frame://p')
[ -n "$FRAME_HEX" ] || { echo 'FAIL: no frame emitted' >&2; FAIL=1; }

TREE=${DFSMODE_TREE:-}
if [ -z "$TREE" ]; then
	for cand in /tmp/dfsmode-build; do
		if [ -d "$cand" ] && [ -f "$cand/hi5622_scan.c" ]; then TREE=$cand; break; fi
	done
fi
if [ -n "$TREE" ] && [ -f "$TREE/hi5622_scan.c" ]; then
	echo "== 2. ordering contract on the patched driver tree ($TREE)"
	for f in hi5622_dfs_mode.c hi5622_dfs_mode.h; do
		if ! cmp -s "$LANE/src/$f" "$TREE/$f"; then
			echo "FAIL: src/$f drifted from the patched tree" >&2
			FAIL=1
		fi
	done
	echo '-- 2a. the frame passes the REAL frame_post validator (h2d_frames.c)'
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/validate_frame_test.c" "$TREE/h2d_frames.c" \
		-o "$WORK/validate_frame_test"
	"$WORK/validate_frame_test" | tee "$WORK/part2-validate.txt"
	echo '-- 2b. the ordering contract (hi5622_scan_logic.c)'
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/order_test.c" "$TREE/hi5622_scan_logic.c" \
		-o "$WORK/order_test"
	"$WORK/order_test" | tee "$WORK/part2-order.txt"
	$PYTHON "$LANE/tools/dfs_mode_send_test.py" --frame-hex "$FRAME_HEX" \
		--tree "$TREE" | tee "$WORK/part2-frame.txt"
else
	echo '== 2. SKIP: no patched driver tree (run tools/build-dfs-mode-send.sh first)'
fi

ART=/tmp/dfsmode-artifact/hi5622_wifi.ko
if [ -f "$ART" ]; then
	echo "== 3. built artifact contract ($ART)"
	MI=$(modinfo -p "$ART")
	NP=$(printf '%s\n' "$MI" | wc -l)
	[ "$NP" = "61" ] || { echo "FAIL: param count $NP != 61" >&2; FAIL=1; }
	for p in dfs_mode desc_table ap_chan_wait_ms beacon_static vap5g; do
		printf '%s\n' "$MI" | grep -q "^${p}:" || {
			echo "FAIL: missing module param $p" >&2; FAIL=1; }
	done
	for s in "cfg80211: start_ap beacon_static frame %u/%u 'dfs_mode' posted (%zu B" \
		 "cfg80211: dfs_mode post: bandobject=0x%08x published=0x%08x attach=%u (%s)" \
		 "dfs_mode=%u is not a valid DFS-mode parameter" \
		 "dfs_mode: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u"; do
		strings -a "$ART" | grep -qF "$s" || {
			echo "FAIL: feature string missing: $s" >&2; FAIL=1; }
	done
	SIZE=$(stat -c %s "$ART")
	SHA=$(sha256sum "$ART" | cut -d' ' -f1)
	echo "artifact $SIZE B sha256 $SHA params $NP"
else
	echo '== 3. SKIP: no built artifact (run tools/build-dfs-mode-send.sh first)'
fi

if [ "$FAIL" != 0 ]; then
	echo 'host tests FAILED' >&2
	exit 1
fi
echo 'host tests PASS (no device contacted)'
