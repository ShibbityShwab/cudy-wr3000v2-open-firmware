#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Host-only build + selftest for the wifi-vap-scan frame builders (task-110).
# No kernel, no device, no vendor artifact needed: pure C99.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-/tmp/vapscan_selftest}"
echo "== compile (C99 -Wall -Wextra -Werror -pedantic) =="
$CC -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o "$OUT" \
	src/vapscan_frames.c src/vapscan_selftest.c
echo "== selftest =="
"$OUT"
echo "== emit bench frames =="
"$OUT" --emit
