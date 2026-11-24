#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# run-session.sh - host-side entry point for the attended wifi-bench-session.
#
#   ./host/run-session.sh                       # DRY-RUN (default): artifact
#                                               # pins + plans + rendered device
#                                               # scripts; never contacts the box
#   ./host/run-session.sh --phases s,a          # plan/run a subset
#   ./host/run-session.sh --yes-really --attended
#                                               # REAL attended run (Lead only)
#   ./host/run-session.sh --yes-really --attended --allow-partial
#                                               # run ready phases, report the
#                                               # still-pending ones
#
# Exit codes: 0 all planned/run phases PASS; 12 PARTIAL (a phase pending or
# PASS-PARTIAL); 10 STOP/FAIL; 2 usage/refusal.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
PY="$ROOT/.provision-venv/bin/python"
[ -x "$PY" ] || PY=python3
echo "run-session: lane=$LANE root=$ROOT python=$PY"
exec "$PY" "$HERE/session_driver.py" "$@"
