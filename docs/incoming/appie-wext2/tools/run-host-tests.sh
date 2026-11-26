#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane appie-wext2 (task-239) host KAT.
# Compiles the portable vendor-WEXT model with the host C compiler and runs the
# self-test.  No kernel, no device, no vendor artifact.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/appie-wext2-selftest
$CC -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
    "$LANE/src/hi5622_app_ie_wext.c" "$LANE/src/app_ie_wext_selftest.c" \
    -o "$OUT"
"$OUT"
