#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/selftest.sh - host-only selftests for the wifi-sta-static lane.
# No device, no vendor binary, no firmware: pure-C logic/builders only.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${OUT:-/tmp/wifi-sta-static-selftest}
CFLAGS="-std=gnu99 -O1 -Wall -Wextra -Werror -I$LANE/src"

mkdir -p "$OUT"
echo "== host compiler: $($CC --version | head -1)"

$CC $CFLAGS -o "$OUT/sta_logic_selftest" \
	"$LANE/tools/sta_logic_selftest.c" "$LANE/src/hi5622_sta_logic.c"
$CC $CFLAGS -o "$OUT/sta_frames_selftest" \
	"$LANE/tools/sta_frames_selftest.c" "$LANE/src/hi5622_sta_frames.c" \
	"$LANE/src/hi5622_sta_logic.c"

echo "== sta_logic_selftest"
"$OUT/sta_logic_selftest"
echo "== sta_frames_selftest"
"$OUT/sta_frames_selftest"

# The WAL opcodes/body sizes must stay in sync with the inherited h2d allowlist
# that hi5622_frame_post() enforces: opcode 0x123/108 B is added by this lane
# and 0x00d0/16 B already exists (named KICK_STA there).  Check the staged
# driver tree when one is given (STAGED=...), otherwise skip loudly.
if [ -n "${STAGED:-}" ]; then
	if grep -q "H2D_WAL_CMD_STA_CONNECT" "$STAGED/h2d_frames.h" &&
	   grep -q "H2D_WAL_CMD_STA_CONNECT, 108u" "$STAGED/h2d_frames.c"; then
		echo "== h2d allowlist: STA connect opcode present (0x123/108)"
	else
		echo "FAIL: h2d_frames allowlist is missing the STA connect opcode" >&2
		exit 1
	fi
else
	echo "== h2d allowlist check skipped (set STAGED=<driver tree>)"
fi
echo "ALL-SELFTESTS-PASS"
