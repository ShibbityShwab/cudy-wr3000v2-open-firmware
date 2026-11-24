#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# selftest.sh - host-only checks for the wifi-sta-join lane (no device contact)
#   H0  portable C unit tests (bounds, gates, body/frame map, D2H decode)
#   H1  static re-derivation from the pinned LOCAL vendor module (skipped if
#       the module is not present; it is never committed)
#   H2  no-key-log / fail-closed static audit
# Usage: ./tools/selftest.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
CC=${CC:-gcc}
OUT=${TMPDIR:-/tmp}/sta-join-selftest

echo "== H0 host unit tests"
"$CC" -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
	-I "$LANE/src" -o "$OUT" \
	"$LANE/tools/sta_join_selftest.c" \
	"$LANE/src/hi5622_sta_logic.c" \
	"$LANE/src/hi5622_sta_frames.c"
"$OUT"

echo "== H1 static re-derivation (local vendor module; optional)"
VKO=${HI5622_WIFI_KO:-}
if [ -z "$VKO" ]; then
	for c in /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko \
		 /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko; do
		[ -f "$c" ] && VKO=$c && break
	done
fi
PY=""
for c in "$ROOT/.provision-venv/bin/python" python3; do
	if command -v "$c" >/dev/null 2>&1 || [ -x "$c" ]; then
		if "$c" -c 'import elftools, capstone' >/dev/null 2>&1; then
			PY=$c
			break
		fi
	fi
done
if [ -n "$VKO" ] && [ -n "$PY" ]; then
	"$PY" "$LANE/tools/fw_sta_join_re.py" "$VKO"
elif [ -z "$VKO" ]; then
	echo "SKIP: pinned local vendor module not found (never committed)"
else
	echo "SKIP: python with pyelftools+capstone not found (use .provision-venv)"
fi

echo "== H2 safety / fail-closed audit"
python3 "$LANE/tools/audit_no_key_log.py"

echo "ALL-SELFTESTS-PASS"
