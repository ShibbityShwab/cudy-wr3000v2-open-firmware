#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/validator-check.sh - host-only regression check for the h2d_frames
# validator extension carried by provider/vapstart-provider.patch.
#
# Why: the 2026-11-26 bench fire rejected the 56-byte WAL group-2 VAP-add frame
# in hi5622_frame_post() ("firmware receive slot has no handler") because the
# firmware's static group-2 receive table has no id-0 slot.  The patch teaches
# the validator this one decoded family and must not weaken any other rule.
#
# This check copies the committed wifi-scan-ap src tree to a scratch dir (never
# modifies another lane), applies the provider patch, then:
#   1. compiles + runs src/wal_validator_selftest.c against the PATCHED
#      h2d_frames (accepted WAL family + still-refused mutations + inherited
#      frame regression);
#   2. compiles + runs the inherited wifi-h2d-frames lane selftest against the
#      PATCHED h2d_frames (proves the extension does not change any existing
#      verdict).
set -eu
LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DOCS="$(cd "$LANE_DIR/.." && pwd)"
KO_SRC="${KO_SRC:-$DOCS/wifi-scan-ap/src}"
PATCH="$LANE_DIR/provider/vapstart-provider.patch"
WORK="${WORK:-/tmp/wifi-vap-start-validator}"
CC="${CC:-cc}"

[ -d "$KO_SRC" ] || { echo "validator-check: missing reference module src: $KO_SRC" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "validator-check: missing provider patch: $PATCH" >&2; exit 2; }

rm -rf "$WORK"
mkdir -p "$WORK"
cp -a "$KO_SRC/." "$WORK/"
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/.*.cmd
cp "$LANE_DIR"/src/vapstart_frames.h "$LANE_DIR"/src/vapstart_frames.c \
   "$LANE_DIR"/src/wal_validator_selftest.c "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -I$WORK"

echo "== 1. WAL cfg-event family vs the patched h2d_frames validator =="
"$CC" $CFLAGS -o "$WORK/wal_validator_selftest" \
	"$LANE_DIR/src/wal_validator_selftest.c" \
	"$LANE_DIR/src/vapstart_frames.c" "$WORK/h2d_frames.c"
"$WORK/wal_validator_selftest"

echo
echo "== 2. inherited h2d_frames selftest (wifi-h2d-frames lane) =="
H2D_SELFTEST="$DOCS/wifi-h2d-frames/src/h2d_selftest.c"
[ -f "$H2D_SELFTEST" ] || { echo "validator-check: missing $H2D_SELFTEST" >&2; exit 2; }
# Copy the selftest next to the PATCHED h2d_frames.h: a quoted include would
# otherwise resolve to the test's own (old) tree and the enum/error numbering
# would disagree with the patched library.  The expected result is the same
# 233/0 summary the unpatched scan-ap copy produces (checked in REPORT.md).
cp "$H2D_SELFTEST" "$WORK/h2d_selftest.c"
"$CC" $CFLAGS -o "$WORK/h2d_selftest" "$WORK/h2d_selftest.c" "$WORK/h2d_frames.c"
"$WORK/h2d_selftest"

echo
echo "validator-check: OK (WAL family accepted, mutations refused, inherited verdicts unchanged)"
