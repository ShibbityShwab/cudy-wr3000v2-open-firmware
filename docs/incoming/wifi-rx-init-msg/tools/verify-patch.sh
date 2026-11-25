#!/usr/bin/env bash
# tools/verify-patch.sh - device-free verification of the lane patch (task-170):
# applies patches/0001-rx-init-msg.patch to a copy of the committed
# wifi-h2d-send/src tree, checks git apply --check / apply, then syntax-checks
# the patched portable logic in host mode.  No kernel-module build.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
SRC=$ROOT/opensource/docs/incoming/wifi-h2d-send/src
PATCH=$LANE/patches/0001-rx-init-msg.patch
WORK=${WORK:-/tmp/wifi-rx-init-msg-patch-check}
rm -rf "$WORK" && mkdir -p "$WORK/m"
cp -a "$SRC" "$WORK/m/src"
( cd "$WORK/m" && git apply -p1 --check "$PATCH" )
echo "PASS: patch applies cleanly (-p1) to the committed wifi-h2d-send/src tree"
( cd "$WORK/m" && git apply -p1 "$PATCH" )
CC=${CC:-cc}
$CC -std=c99 -fsyntax-only -I "$WORK/m/src" "$WORK/m/src/h2d_send_logic.c"
echo "PASS: patched h2d_send_logic.c compiles in host mode (syntax only)"
"$LANE/tools/run-host-tests.sh"
