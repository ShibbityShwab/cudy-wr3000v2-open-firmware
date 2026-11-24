#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# selftest.sh - host-side build+run of the wifi-5ghz band5g logic test.
# HOST-ONLY: pure userspace, no kernel tree, no device access.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
CC=${CC:-gcc}
BIN=${TMPDIR:-/tmp}/band5g_selftest.$$
trap 'rm -f "$BIN"' EXIT

echo "== build"
"$CC" -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
	-I "$LANE/src" \
	"$LANE/src/hi5622_band5g.c" "$LANE/src/band5g_selftest.c" \
	-o "$BIN"
echo "build: OK ($CC, -std=c99 -Wall -Wextra -Werror -pedantic)"

echo "== run"
"$BIN"
