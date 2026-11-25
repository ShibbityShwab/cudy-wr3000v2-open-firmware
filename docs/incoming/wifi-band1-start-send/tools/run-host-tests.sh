#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-band1-start-send (task-206) host tests.
#
# HOST-ONLY: no device, no kernel, no firmware, no vendor blob.  Four parts:
#
#   1. the portable model in src/ - the fail-closed band_start /
#      band_start_fallback x beacon_static x band x module matrix, the
#      byte-exact 33-byte 0x0242 sync-start frame (and its forced-start and
#      0x0243 nudge siblings), the firmware guard precheck, the builder
#      refusals and the sysfs status line;
#   2. the ordering contract against the PATCHED driver tree
#      (/tmp/bandstart-build by default): the real hi5622_beacon_plan() must
#      put the band_start step AFTER dfs_mode and BEFORE ap_chan, and the
#      source order in hi5622_scan.c must post 'band_start' after 'dfs_mode'
#      and before 'ap_chan' (tools/order_test.c + tools/band_start_send_test.py,
#      including the src/ drift guard);
#   3. the real frame_post validator (h2d_frames.c) must accept the frames;
#   4. the built artifact contract when tools/build-band-start-send.sh has run:
#      63 params incl band_start + band_start_fallback, the feature strings,
#      12 symvers.
#
# Tree selection: $BANDSTART_TREE, else /tmp/bandstart-build.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-gcc}
PYTHON=${PYTHON:-python3}
WORK=${TMPDIR:-/tmp}/wifi-band1-start-send-host-tests
rm -rf "$WORK"
mkdir -p "$WORK" "$LANE/evidence"
FAIL=0

echo '== 1. portable 0x0242 band-start model (src/)'
$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$LANE/src" \
	"$LANE/src/band_start_selftest.c" "$LANE/src/hi5622_band_start.c" \
	-o "$WORK/band_start_selftest"
"$WORK/band_start_selftest" | tee "$WORK/part1.txt"
DUMP=$("$WORK/band_start_selftest" --dump)
FRAME_HEX=$(printf '%s\n' "$DUMP" | sed -n 's/^frame://p')
FORCED_HEX=$(printf '%s\n' "$DUMP" | sed -n 's/^frame_forced://p')
NUDGE_HEX=$(printf '%s\n' "$DUMP" | sed -n 's/^frame_nudge://p')
[ -n "$FRAME_HEX" ] && [ -n "$FORCED_HEX" ] && [ -n "$NUDGE_HEX" ] || {
	echo 'FAIL: no frames emitted' >&2; FAIL=1; }

TREE=${BANDSTART_TREE:-}
if [ -z "$TREE" ]; then
	for cand in /tmp/bandstart-build; do
		if [ -d "$cand" ] && [ -f "$cand/hi5622_scan.c" ]; then TREE=$cand; break; fi
	done
fi
if [ -n "$TREE" ] && [ -f "$TREE/hi5622_scan.c" ]; then
	echo "== 2. ordering contract on the patched driver tree ($TREE)"
	for f in hi5622_band_start.c hi5622_band_start.h; do
		if ! cmp -s "$LANE/src/$f" "$TREE/$f"; then
			echo "FAIL: src/$f drifted from the patched tree" >&2
			FAIL=1
		fi
	done
	echo '-- 2a. the frames pass the REAL frame_post validator (h2d_frames.c)'
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/validate_frame_test.c" "$TREE/h2d_frames.c" \
		-o "$WORK/validate_frame_test"
	"$WORK/validate_frame_test" | tee "$WORK/part2-validate.txt"
	echo '-- 2b. the ordering contract (hi5622_scan_logic.c)'
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/order_test.c" "$TREE/hi5622_scan_logic.c" \
		-o "$WORK/order_test"
	"$WORK/order_test" | tee "$WORK/part2-order.txt"
	$PYTHON "$LANE/tools/band_start_send_test.py" \
		--frame-hex "$FRAME_HEX" --forced-hex "$FORCED_HEX" \
		--nudge-hex "$NUDGE_HEX" --tree "$TREE" \
		| tee "$WORK/part2-frame.txt"
else
	echo '== 2. SKIP: no patched driver tree (run tools/build-band-start-send.sh first)'
fi

ART=/tmp/bandstart-artifact/hi5622_wifi.ko
if [ -f "$ART" ]; then
	echo "== 3. built artifact contract ($ART)"
	MI=$(modinfo -p "$ART")
	NP=$(printf '%s\n' "$MI" | wc -l)
	[ "$NP" = "63" ] || { echo "FAIL: param count $NP != 63" >&2; FAIL=1; }
	for p in band_start band_start_fallback dfs_mode desc_table ap_chan_wait_ms beacon_static vap5g; do
		printf '%s\n' "$MI" | grep -q "^${p}:" || {
			echo "FAIL: missing module param $p" >&2; FAIL=1; }
	done
	for s in "cfg80211: start_ap beacon_static frame %u/%u 'band_start' posted (%zu B, g0 id1 key 0x0242" \
		 "cfg80211: band_start post: module=%u bandobject=%s state[1]=%u state[4]=0x%02x counter=%u (%s)" \
		 "cfg80211: band_start fallback: start marker state[1]=%u for module=%u (%s)" \
		 "band_start=%u is not a valid band-start parameter" \
		 "band_start=1 needs the beacon_static=1 vendor order" \
		 "band_start_fallback=1 needs band_start=1" \
		 "band-start builder refused (%d)" \
		 "band-start frame (g0 id1 key 0x0242) not posted (%d)" \
		 "band_start: param=%u (%s) fallback=%u beacon_static=%u last_rc=%d posted=%u forced=%u nudge=%u refused=%u"; do
		strings -a "$ART" | grep -qF "$s" || {
			echo "FAIL: feature string missing: $s" >&2; FAIL=1; }
	done
	SIZE=$(stat -c %s "$ART")
	SHA=$(sha256sum "$ART" | cut -d' ' -f1)
	echo "artifact $SIZE B sha256 $SHA params $NP"
else
	echo '== 3. SKIP: no built artifact (run tools/build-band-start-send.sh first)'
fi

if [ "$FAIL" != 0 ]; then
	echo 'host tests FAILED' >&2
	exit 1
fi
echo 'host tests PASS (no device contacted)'
