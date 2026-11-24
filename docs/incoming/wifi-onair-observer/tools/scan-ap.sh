#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# scan-ap.sh - L2 on-air beacon proof: passive scan for the bench AP on ch6.
#
# HOST-ONLY: the observer interface scans; the bench router is never
# contacted.  A PASSIVE scan is deliberate: the clean-room AP's management-TX
# path is fail-closed, so it may not answer probe requests at all, while its
# beacons (if any) are firmware-generated.  Active probing is available for
# diagnosis only.
#
# Usage:
#   sudo tools/scan-ap.sh                  # passive ch6 scan (decisive on-air gate)
#   sudo tools/scan-ap.sh --full           # additional full-band passive scan
#   sudo tools/scan-ap.sh --active         # active scan (diagnosis only)
#   sudo tools/scan-ap.sh --expect-open    # AP fixture is an open network
#   tools/scan-ap.sh --help
#
# Env: ONAIR_SCAN_TIMEOUT (default 25), ONAIR_SCAN_DWELL_TU (default 1500)
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=scan
LOG="$EVID/scan.log"

MODE=passive
FULL=0
EXPECT_OPEN=0
for arg in "$@"; do
	case "$arg" in
	--passive) MODE=passive ;;
	--active) MODE=active ;;
	--full) FULL=1 ;;
	--expect-open) EXPECT_OPEN=1 ;;
	--help | -h)
		sed -n '2,22p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "unknown argument: $arg (see --help)"
		exit 2
		;;
	esac
done

iface_exists "$OBS_IF" || { err "observer iface $OBS_IF missing (run tools/iface-up.sh)"; exit 2; }
guard_check "scan" || exit 2
require_root || exit 2

SCAN_OUT="$EVID/scan-ch6.txt"
DWELL=${ONAIR_SCAN_DWELL_TU:-1500}

PASSIVE_ARG=""
section "L2 scan for '$AP_SSID' on $AP_FREQ MHz (ch$AP_CHANNEL)"
if [ "$MODE" = passive ]; then
	log "mode: passive on one frequency (no probe requests; only beacons can answer)"
	PASSIVE_ARG="passive"
else
	warn "mode: ACTIVE - the AP's mgmt-TX path is fail-closed, so probe responses may be missing; diagnostic only"
	PASSIVE_ARG=""
fi

capture_file "$SCAN_OUT" iw dev "$OBS_IF" scan -u freq "$AP_FREQ" duration "$DWELL" $PASSIVE_ARG
SCAN_RC=$?
if [ "$SCAN_RC" -ne 0 ]; then
	warn "iw scan with 'duration' failed rc=$SCAN_RC; retrying without duration"
	capture_file "$SCAN_OUT" iw dev "$OBS_IF" scan -u freq "$AP_FREQ" $PASSIVE_ARG
	SCAN_RC=$?
fi
if [ "$SCAN_RC" -ne 0 ]; then
	err "iw scan failed rc=$SCAN_RC (interface busy? another wpa_supplicant attached? see $SCAN_OUT)"
	verdict scan HARNESS-ERROR "iw scan failed rc=$SCAN_RC"
	exit 2
fi

if [ "$EXPECT_OPEN" = 1 ]; then
	python3 "$TOOLS_DIR/onair_scan.py" --iw-scan "$SCAN_OUT" \
		--expect-ssid "$AP_SSID" --expect-freq "$AP_FREQ" --expect-channel "$AP_CHANNEL" \
		--allow-probe-response --json-out "$EVID/scan-ch6.json"
else
	python3 "$TOOLS_DIR/onair_scan.py" --iw-scan "$SCAN_OUT" \
		--expect-ssid "$AP_SSID" --expect-freq "$AP_FREQ" --expect-channel "$AP_CHANNEL" \
		--expect-wpa2 --allow-probe-response --json-out "$EVID/scan-ch6.json"
fi
PARSE_RC=$?

if [ "$PARSE_RC" -ne 0 ]; then
	section "L2 diagnosis: full-band passive scan"
	warn "target not seen as expected on ch$AP_CHANNEL; running a full-band passive scan to locate the SSID (diagnosis only)"
	FALLBACK="$EVID/scan-fallback-all.txt"
	capture_file "$FALLBACK" iw dev "$OBS_IF" scan -u passive
	FALLBACK_RC=$?
	if [ "$FALLBACK_RC" -eq 0 ]; then
		python3 "$TOOLS_DIR/onair_scan.py" --iw-scan "$FALLBACK" \
			--expect-ssid "$AP_SSID" --any-freq --allow-probe-response \
			--json-out "$EVID/scan-fallback-all.json" || true
	else
		warn "full-band fallback scan failed rc=$FALLBACK_RC"
	fi
	verdict scan FAIL "beacon-on-air gate not met on ch$AP_CHANNEL: see $EVID/scan-ch6.json and fallback diagnosis"
	exit 1
fi

if [ "$FULL" = 1 ]; then
	section "L2 context: full-band passive scan (informational)"
	capture_file "$EVID/scan-full.txt" iw dev "$OBS_IF" scan -u passive || warn "full-band scan failed (informational only)"
fi

section "L2 post-check"
capture iw dev "$OBS_IF" link >"$EVID/scan-link-after.txt" 2>/dev/null || true
guard_check "scan" || exit 2
section "L2 verdict"
verdict scan PASS "on-air beacon proof on ch$AP_CHANNEL: ssid=$AP_SSID (see $EVID/scan-ch6.json)"
exit 0