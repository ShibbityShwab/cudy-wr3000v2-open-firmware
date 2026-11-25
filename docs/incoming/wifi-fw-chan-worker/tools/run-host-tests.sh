#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-host-tests.sh - the lane's host verification (lane wifi-fw-chan-worker,
# shared task task-180).  HOST-ONLY, read-only: runs the decoder/selftest against
# the pinned local vendor artifacts in place, writes nothing but stdout.
#
# Usage: ./tools/run-host-tests.sh
# Exit 0 = PASS.  .provision-venv/bin/python is the project interpreter.
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
PY=${PY:-$LANE/../../../../.provision-venv/bin/python}
[ -x "$PY" ] || PY=$(command -v python3)
FW=${FW:-/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin}
KO=${KO:-/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko}
PD4=${PD4:-/home/shibbityshwab/pd4-rebuilt.bin}
echo "== fw_chan_worker_re.py --selftest (fw=$FW ko=$KO pd4=$PD4)"
exec "$PY" "$LANE/tools/fw_chan_worker_re.py" --selftest --fw "$FW" --ko "$KO" --pd4 "$PD4"
