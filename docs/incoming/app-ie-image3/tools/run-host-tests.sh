#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - lane app-ie-rebase (task-235) host KAT.
# Re-runs the task-234 KAT (the sibling lane's self-test, unchanged) against
# this lane's byte-identical portable module.  Host C compiler only: no
# kernel, no device, no vendor artifact.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
SELFTEST=${SELFTEST:-$LANE/../wifi-app-ie/src/app_ie_selftest.c}
CC=${CC:-cc}
OUT=${TMPDIR:-/tmp}/app-ie-image3-selftest
[ -f "$SELFTEST" ] || { echo "run-host-tests: KAT source $SELFTEST absent" >&2; exit 2; }
$CC -std=c99 -Wall -Wextra -Werror -O2 -I"$LANE/src" \
    "$LANE/src/hi5622_app_ie.c" "$SELFTEST" -o "$OUT"
"$OUT"
