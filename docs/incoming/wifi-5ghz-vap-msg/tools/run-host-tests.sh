#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - host-only tests for lane wifi-5ghz-vap-msg
# (shared task task-174).  No device, no kernel, no firmware.
#
# Part 1 (always): the portable vendor-order planner in src/ - the
# fail-closed decision matrix of the vap5g parameter.
# Part 2 (when a patched driver tree is present): the two band-sensitive
# messages built by the driver's own portable builders (vapscan_frames.c,
# vapstart_frames.c): g0 id7/id8 keep the mac chip id 0, the VAP add carries
# band 1 in body+0x12 and chip 0 in body+0x13.
#
# Tree selection for part 2: $VAPMSG5G_TREE, else /tmp/wifi-vapmsg5g-build
# (tools/build-vapmsg5g-ready.sh), else /tmp/vapmsg5g-diff/b (scratch).
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
CC=${CC:-gcc}
WORK=${TMPDIR:-/tmp}/wifi-5ghz-vap-msg-host-tests
rm -rf "$WORK"
mkdir -p "$WORK"
TOTAL=0
FAIL=0

echo '== 1. portable vendor-order planner (src/)'
$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$LANE/src" \
	"$LANE/src/vapmsg5g_selftest.c" "$LANE/src/hi5622_vapmsg5g.c" \
	-o "$WORK/vapmsg5g_selftest"
"$WORK/vapmsg5g_selftest" | tee "$WORK/part1.txt"

TREE=${VAPMSG5G_TREE:-}
if [ -z "$TREE" ]; then
	for c in /tmp/wifi-vapmsg5g-build /tmp/vapmsg5g-diff/b; do
		if [ -d "$c" ] && [ -f "$c/vapscan_frames.c" ]; then TREE=$c; break; fi
	done
fi
if [ -n "$TREE" ] && [ -f "$TREE/vapscan_frames.c" ] && \
   [ -f "$TREE/vapstart_frames.c" ]; then
	echo "== 2. band-sensitive wire bytes from the patched driver tree ($TREE)"
	# The lane's portable copy must be byte-identical to the patched tree
	# (source-drift guard, same rule as the band5g lane).
	for f in hi5622_vapmsg5g.c hi5622_vapmsg5g.h; do
		cmp -s "$LANE/src/$f" "$TREE/$f" || {
			echo "FAIL: src/$f drifted from the patched tree" >&2
			FAIL=1
		}
	done
	$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$TREE" \
		"$LANE/tools/frame_bytes_selftest.c" \
		"$TREE/vapscan_frames.c" "$TREE/vapstart_frames.c" \
		-o "$WORK/frame_bytes_selftest"
	"$WORK/frame_bytes_selftest" | tee "$WORK/part2.txt"
else
	echo '== 2. SKIP: no patched driver tree (run tools/build-vapmsg5g-ready.sh first)'
fi

if [ "$FAIL" != 0 ]; then
	echo 'host tests FAILED' >&2
	exit 1
fi
echo 'host tests PASS (no device contacted)'
