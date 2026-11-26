#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Host-only test runner for the monitor2 lane (task-246).
# Compiles the portable monitor logic with the host C compiler and runs the
# pinned vector/behaviour checks.  No kernel tree, no device, no bench.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$HERE/../src
OUT=${OUT:-/tmp/monitor2-host-tests}
CC=${CC:-gcc}
mkdir -p "$OUT"
echo "== host compile =="
"$CC" -std=c99 -Wall -Wextra -Werror -I "$SRC" \
	"$HERE/monitor_selftest.c" "$SRC/hi5622_monitor_logic.c" \
	-o "$OUT/monitor_selftest" || exit 1
echo "== run =="
"$OUT/monitor_selftest" | tee "$OUT/HOST-TESTS.txt"
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] && echo "PASS: monitor host selftest" || echo "FAIL: monitor host selftest"
exit "$rc"
