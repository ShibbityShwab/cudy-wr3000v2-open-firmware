#!/bin/sh
# run-host-tests.sh - the no-device test entry point of lane
# wifi-ax-advanced (task-195).  Builds the portable AX config-event model on
# the host and runs both the C selftest and the artifact re-derivation tool.
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
mkdir -p /tmp/wifi-ax-advanced-tests
$CC -std=c99 -Wall -Wextra -Werror -O2 \
	-o /tmp/wifi-ax-advanced-tests/ax_events_selftest \
	src/hi5622_ax_events.c src/ax_events_selftest.c
/tmp/wifi-ax-advanced-tests/ax_events_selftest
python3 tools/fw_ax_advanced_re.py --selftest
echo "host tests: PASS"
