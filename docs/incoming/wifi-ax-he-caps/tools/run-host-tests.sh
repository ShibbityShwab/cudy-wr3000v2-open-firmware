#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - no device, no network: compile and run the HE
# capability contract selftest (lane wifi-ax-he-caps, task-177).
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
BIN=${BIN:-${TMPDIR:-/tmp}/he_caps_selftest}
CFLAGS="-std=c99 -Wall -Wextra -Werror -O2"

echo "== build selftest ($CC)"
$CC $CFLAGS -I"$LANE/src" \
	"$LANE/src/he_caps_selftest.c" "$LANE/src/hi5622_he_caps.c" \
	-o "$BIN"
echo "== run selftest"
"$BIN"
