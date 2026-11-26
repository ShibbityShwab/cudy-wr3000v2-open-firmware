#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane wifi-app-ie (task-234) host tests.
# Compiles the portable model + selftest with the host C compiler and runs it.
# No kernel, no device, no vendor artifact.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/app-ie-selftest
$CC -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
    "$LANE/src/hi5622_app_ie.c" "$LANE/src/app_ie_selftest.c" -o "$OUT"
"$OUT"
