#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# dfs160b host selftest runner (task-245): userspace only, no kernel tree,
# no device, no vendor artifact.  Builds the portable model + selftest with
# the host compiler and runs every check.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
OUT=${OUT:-/tmp/dfs160b-selftest}
CC=${CC:-cc}
CFLAGS="${CFLAGS:--std=c99 -O2 -Wall -Wextra -Werror -Wshadow -Wconversion}"
rm -rf "$OUT"
mkdir -p "$OUT"
"$CC" $CFLAGS -I"$LANE/src" -o "$OUT/dfs160b_selftest" \
	"$LANE/src/hi5622_dfsb.c" "$LANE/src/dfs160b_selftest.c"
"$OUT/dfs160b_selftest" | tee "$OUT/HOST-TESTS.txt"
echo "dfs160b: host selftest PASS (log: $OUT/HOST-TESTS.txt)"
