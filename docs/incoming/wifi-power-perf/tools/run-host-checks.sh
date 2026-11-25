#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-host-checks.sh - device-free gate for the wifi-power-perf lane.
#   ./tools/run-host-checks.sh              selftest + model + patch dry-run
#   ./tools/run-host-checks.sh --with-build also cross-build the patched module
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$LANE_DIR"

echo "== 1. portable logic selftest (73 checks)"
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -I src tools/pm_perf_selftest.c -o /tmp/pm_perf_selftest
/tmp/pm_perf_selftest

echo "== 2. independent performance model"
python3 tools/perf_model.py --check >/dev/null
echo "perf_model: PASS"

echo "== 3. patch applies to the pinned base"
BASE=${BASE_TREE:-/tmp/sibling-mgmt-fix}
if [ ! -d "$BASE" ]; then
	echo "run-host-checks: base tree $BASE missing; skipping patch-apply check (run build-patched-module.sh to rebuild it)" >&2
else
	rm -rf /tmp/wpp-apply && cp -a "$BASE" /tmp/wpp-apply 2>/dev/null || true
	( cd /tmp/wpp-apply && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order 2>/dev/null ) || true
	for p in "$LANE_DIR"/patches/*.patch; do
		( cd /tmp/wpp-apply && patch -p1 --dry-run < "$p" >/dev/null )
	done
	echo "patch-apply: PASS"
fi

if [ "${1:-}" = "--with-build" ]; then
	echo "== 4. cross-build the patched module (host-only)"
	"$LANE_DIR/tools/build-patched-module.sh"
fi
echo "HOST CHECKS PASS"
