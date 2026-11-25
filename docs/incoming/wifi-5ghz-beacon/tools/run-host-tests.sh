#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - host-only tests for lane wifi-5ghz-beacon
# (shared task task-175).  No device, no kernel, no firmware, no vendor blob.
#
# Part 1 (always): the portable channel-activation planner in src/ - the
# fail-closed ap_chan_wait_ms decision matrix, the corrected module-table
# address arithmetic and the firmware channel-ctx "programmed" predicate.
# Part 2 (when a patched driver tree is present): the patch contract on the
# driver sources - corrected readback constants, the new parameter, the
# fail-closed abort strings and the band-1 gate pack - plus the src/ drift
# guard (the committed portable code must be byte-identical to the patched
# tree).
#
# Tree selection for part 2: $BEACON5G_TREE, else /tmp/beacon5g-build
# (tools/build-beacon5g-ready.sh), else /tmp/beacon5g-edit (scratch).
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
CC=${CC:-gcc}
WORK=${TMPDIR:-/tmp}/wifi-5ghz-beacon-host-tests
rm -rf "$WORK"
mkdir -p "$WORK"
FAIL=0

echo '== 1. portable channel-activation planner (src/)'
$CC -std=c99 -Wall -Wextra -pedantic -Werror -O2 -I"$LANE/src" \
	"$LANE/src/beacon5g_selftest.c" "$LANE/src/hi5622_beacon5g.c" \
	-o "$WORK/beacon5g_selftest"
"$WORK/beacon5g_selftest" | tee "$WORK/part1.txt"

TREE=${BEACON5G_TREE:-}
if [ -z "$TREE" ]; then
	for c in /tmp/beacon5g-build /tmp/beacon5g-edit; do
		if [ -d "$c" ] && [ -f "$c/hi5622_scan.c" ]; then TREE=$c; break; fi
	done
fi
if [ -n "$TREE" ] && [ -f "$TREE/hi5622_scan.c" ]; then
	echo "== 2. patch contract on the driver tree ($TREE)"
	for f in hi5622_beacon5g.c hi5622_beacon5g.h; do
		if ! cmp -s "$LANE/src/$f" "$TREE/$f"; then
			echo "FAIL: src/$f drifted from the patched tree" >&2
			FAIL=1
		fi
	done
	check() { # file, grep-pattern, description
		if grep -qF "$2" "$TREE/$1"; then
			echo "PASS $3"
		else
			echo "FAIL $3 (missing in $1)" >&2
			FAIL=1
		fi
	}
	check hi5622_scan.c 'HI5622_STAB_MODULE_TABLE_SLOT0_CPU	0x171458u /* BAR0 0x829458 */' \
		'corrected module-table slot0 constant (BAR0 0x829458)'
	check hi5622_scan.c 'HI5622_STAB_MODULE_TABLE_BASE_CPU	0x172504u /* BAR0 0x82a504 */' \
		'corrected module-table base constant (BAR0 0x82a504)'
	check hi5622_scan.c 'module_param_named(ap_chan_wait_ms' \
		'ap_chan_wait_ms parameter is declared'
	check hi5622_scan.c '5GHz channel activation: waiting up to' \
		'band-1 bounded wait log'
	check hi5622_scan.c 'the firmware module channel context did not report channel' \
		'fail-closed wait timeout abort'
	check hi5622_scan.c 'band publish words: band0=' \
		'read-only activation gate pack'
	check hi5622_scan.c 'hi5622_beacon5g_plan(' \
		'planner is used by start_ap'
	check hi5622_scan.c 'if (prc) {' \
		'fail-closed parameter validation in start_ap'
	if grep -qF '0x175458u' "$TREE/hi5622_scan.c" || \
	   grep -qF '0x17a504u' "$TREE/hi5622_scan.c"; then
		echo "FAIL: the old (mis-addressed) readback constants are still present" >&2
		FAIL=1
	else
		echo 'PASS old mis-addressed readback constants are gone'
	fi
	if grep -qF 'hi5622_beacon5g.o' "$TREE/Kbuild"; then
		echo 'PASS Kbuild lists hi5622_beacon5g.o'
	else
		echo 'FAIL: Kbuild does not list hi5622_beacon5g.o' >&2
		FAIL=1
	fi
else
	echo '== 2. SKIP: no patched driver tree (run tools/build-beacon5g-ready.sh first)'
fi

if [ "$FAIL" != 0 ]; then
	echo 'host tests FAILED' >&2
	exit 1
fi
echo 'host tests PASS (no device contacted)'
