#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# wpa3-sae.sh - L5 WPA3-SAE variant: expected-negative association attempt.
#
# HOST-ONLY.  This is a variant of tools/wpa2-assoc.sh with a fail-closed
# precondition: the AP must first be restarted with the WPA2+WPA3 transition
# fixture (hostapd-wpa2-wpa3.conf on the router, done by the Lead; this
# harness never touches the router).  If the on-air beacon does not advertise
# SAE, the run is SKIPPED instead of producing a misleading "WPA3 failed"
# result.
#
# EXPECTED TODAY: FAIL at SAE/AUTH.  SAE needs the AUTH management frame
# TX/RX path; the clean-room AP driver .mgmt_tx is fail-closed and there is no
# management RX.  The harness records the exact failure and never claims WPA3.
#
# Usage:
#   sudo tools/wpa3-sae.sh                 # after restarting the AP with SAE
#   sudo tools/wpa3-sae.sh --timeout 60
#   tools/wpa3-sae.sh --help
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=wpa3-sae
LOG="$EVID/wpa3-sae.log"

for arg in "$@"; do
	case "$arg" in
	--help | -h)
		sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	esac
done

if [ ! -f "$EVID/scan-ch6.txt" ]; then
	warn "no L2 scan capture in this evidence dir: cannot verify the AP is in SAE mode"
	verdict wpa3-sae SKIP "run tools/scan-ap.sh first; WPA3 needs the AP restarted with the WPA2+WPA3 transition fixture"
	exit 0
fi
if [ ! -f "$EVID/scan-ch6.json" ]; then
	warn "no parsed L2 scan JSON; re-run tools/scan-ap.sh"
	verdict wpa3-sae SKIP "L2 scan result missing"
	exit 0
fi

section "L5 precondition: the beacon must advertise SAE (AP restarted with the transition fixture)"
python3 "$TOOLS_DIR/onair_scan.py" --iw-scan "$EVID/scan-ch6.txt" \
	--expect-ssid "$AP_SSID" --expect-freq "$AP_FREQ" --expect-channel "$AP_CHANNEL" \
	--expect-wpa2 --require-akm SAE --allow-probe-response \
	--json-out "$EVID/scan-sae-check.json"
SAE_CHECK_RC=$?
if [ "$SAE_CHECK_RC" -ne 0 ]; then
	verdict wpa3-sae SKIP "AP beacon has no SAE AKM (still the WPA2 fixture); restart the AP with hostapd-wpa2-wpa3.conf and re-run tools/scan-ap.sh - do NOT report WPA3 from this run"
	exit 0
fi
log "beacon advertises SAE (and PMF-capable); proceeding with the expected-negative SAE attempt"

# Non-interactive: hand off to the shared association engine with the SAE
# fixture.  Keep this script the documented entry point.
exec "$TOOLS_DIR/wpa2-assoc.sh" --wpa3 "$@"
