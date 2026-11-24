#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/selftest.sh - run every host-side check of the scan D2H lane.
# No device, no network, no writes outside /tmp and the lane's testvectors/.
set -eu
LANE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="$LANE_DIR/../../../../.provision-venv/bin/python"
if [ ! -x "$PYTHON" ]; then
	PYTHON="python3"
fi

echo "== host decoder unit tests"
cc -std=c99 -Wall -Wextra -Werror -pedantic -I"$LANE_DIR/src" \
   "$LANE_DIR/tools/scan_d2h_selftest.c" \
   "$LANE_DIR/src/hi5622_scan_d2h.c" -o /tmp/scan_d2h_selftest
/tmp/scan_d2h_selftest

echo
echo "== RE evidence self-check (pinned local vendor artifacts, read-only)"
"$PYTHON" "$LANE_DIR/tools/fw_scan_d2h_re.py" --selftest

echo
echo "== test vectors"
"$PYTHON" "$LANE_DIR/tools/gen_vectors.py" --write
"$PYTHON" "$LANE_DIR/tools/gen_vectors.py" --check

echo
echo "selftest.sh: ALL HOST CHECKS PASS"
