#!/usr/bin/env bash
# tools/run-host-tests.sh - device-free host test for the candidate RX-init
# config message model (lane rx-init-decode/task-170).  Compiles the portable
# logic strictly (-Werror -pedantic) and runs the selftest.  No device, no IO.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
CC=${CC:-cc}
WORK=${WORK:-/tmp/hi5622-rx-init-msg-hosttest}
rm -rf "$WORK" && mkdir -p "$WORK"
$CC -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wconversion \
	-I"$LANE/src" -o "$WORK/rx_init_selftest" \
	"$LANE/src/hi5622_rx_init_logic.c" "$LANE/tools/rx_init_selftest.c"
"$WORK/rx_init_selftest"
