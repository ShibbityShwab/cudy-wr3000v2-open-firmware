#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - the wifi-rc-impl-2 host suites (HOST-ONLY).
#
# Compiles and runs the three device-free suites against the PATCHED current
# base tree (the IMAGE #3 merged driver sources + patches/0001-rate-ctl-2.patch):
#
#   1. rc2_frame_selftest  - the BYTE-EXACT 39-B SET rate_mode auto frame
#                            (the decoded g0 id2 / key 0x0101 carrier), the
#                            carrier fields, the current base validator, the
#                            builder refusals;
#   2. rc2_c2_order_selftest - the CORRECTED C2 ORDER on the real patched gate
#                            (h2d_gate_check) + the rate_ctl precondition:
#                            type-1 create -> ctx4+0x190 attestation -> ordered
#                            TXQ -> rate command, and every refusal branch;
#   3. rc2_matrix_selftest - the FAIL-CLOSED MATRIX (blob KATs, builder
#                            refusals, g1-id3 parser, decision matrix, names).
#
# No device, no kernel tree, no vendor artifact.  TREE defaults to
# /tmp/rc2-build (produced by tools/make-patch.py or tools/build-rc2.sh); set
# TREE=<dir> to test an already-patched tree.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
TREE=${TREE:-/tmp/rc2-build}
OUT=/tmp/rc2-host
CC=${CC:-cc}
EV=$LANE/evidence

if [ ! -f "$TREE/hi5622_rate_ctl_wire.c" ]; then
	echo "== staging + patching the base tree into $TREE"
	python3 "$LANE/tools/make-patch.py" --apply --dest "$TREE"
fi

rm -rf "$OUT"
mkdir -p "$OUT"
CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -pedantic -I$TREE"

run() {
	name=$1; shift
	echo "== $name"
	"$CC" $CFLAGS "$@" -o "$OUT/$name"
	"$OUT/$name" | tee "$EV/$name.txt"
	grep -q ', 0 failed,' "$EV/$name.txt" || {
		echo "$name: FAILED"; exit 1; }
}

# 1. byte-exact frame
run rc2_frame_selftest "$LANE/tools/rc2_frame_selftest.c" \
	"$TREE/hi5622_rate_ctl_logic.c" "$TREE/hi5622_rate_ctl_wire.c" \
	"$TREE/h2d_frames.c"

# 2. corrected C2 order (the real patched h2d gate + the rc precondition)
run rc2_c2_order_selftest "$LANE/tools/rc2_c2_order_selftest.c" \
	"$TREE/h2d_send_logic.c" "$TREE/h2d_frames.c" \
	"$TREE/hi5622_rate_ctl_logic.c"

# 3. the fail-closed matrix
run rc2_matrix_selftest "$LANE/tools/rc2_matrix_selftest.c" \
	"$TREE/hi5622_rate_ctl_logic.c"

echo "== all host suites PASS (evidence: evidence/rc2_*.txt)"
