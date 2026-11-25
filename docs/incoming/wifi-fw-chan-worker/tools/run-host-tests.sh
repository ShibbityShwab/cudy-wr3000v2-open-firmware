#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - the lane's host verification (lane wifi-fw-chan-worker,
# shared task task-180).  HOST-ONLY, read-only: runs the decoder/selftest against
# the pinned local vendor artifacts in place and the portable C model selftest.
# Nothing is written but the compiled selftest under /tmp.
#
# Usage: ./tools/run-host-tests.sh
# Exit 0 = PASS.  .provision-venv/bin/python is the project interpreter; the C
# selftest uses the first working host compiler (cc/clang/gcc).
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
PY=$LANE/../../../../.provision-venv/bin/python
[ -x "$PY" ] || PY=$(command -v python3)
FW=${FW:-}
[ -n "$FW" ] || FW=/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
KO=${KO:-}
[ -n "$KO" ] || KO=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
PD4=${PD4:-}
[ -n "$PD4" ] || PD4=/home/shibbityshwab/pd4-rebuilt.bin

rc=0
echo "== 1. fw_chan_worker_re.py --selftest (firmware decode, 89 checks)"
"$PY" "$LANE/tools/fw_chan_worker_re.py" --selftest --fw "$FW" --ko "$KO" \
	--pd4 "$PD4" | tail -3 || rc=1

echo
echo "== 2. chan_gate_selftest (portable C model of the decoded predicate)"
CC=
for c in cc clang gcc; do
	if command -v "$c" >/dev/null 2>&1 && "$c" --version >/dev/null 2>&1; then CC=$c; break; fi
done
[ -n "$CC" ] || { echo "no host C compiler found" >&2; exit 2; }
"$CC" -std=c99 -Wall -Wextra -Werror -O2 \
	-o /tmp/chan_gate_selftest \
	"$LANE/src/chan_gate_selftest.c" "$LANE/src/hi5622_chan_gate.c" || rc=1
/tmp/chan_gate_selftest | tail -3 || rc=1

echo
if [ "$rc" = 0 ]; then echo "run-host-tests: PASS"; else echo "run-host-tests: FAIL"; fi
exit "$rc"
