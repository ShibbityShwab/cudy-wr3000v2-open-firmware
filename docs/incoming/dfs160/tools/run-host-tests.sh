#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane dfs160 (task-240) host tests.
# Compiles the portable model + selftest with the host C compiler and runs it.
# No kernel, no device, no vendor artifact.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/dfs160-selftest
$CC -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
    "$LANE/src/hi5622_dfs160.c" "$LANE/src/dfs160_selftest.c" -o "$OUT"
"$OUT"
