#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# capture-beacon.sh - L2b optional monitor-mode beacon capture.
#
# HOST-ONLY, best effort.  The decisive on-air rung is the scan
# (tools/scan-ap.sh); this rung additionally captures raw beacons (count,
# interval, IEs) with tools/sniff.py because the host has no tcpdump/tshark.
#
# The monitor interface (default mon-onair) is created by
# tools/iface-up.sh --monitor and MUST NOT be allowed to disturb the base
# uplink.  The capture runs while a passive scan on the observer interface
# parks the phy on the AP channel; if the host wiphy does not allow the
# monitor interface alongside the managed ones, this rung is SKIPPED, never
# faked.
#
# Usage:
#   sudo tools/capture-beacon.sh                 # 20 s capture during a ch6 scan
#   sudo tools/capture-beacon.sh --duration 15
#   tools/capture-beacon.sh --help
#
# Env: ONAIR_CAPTURE_SECONDS (default 20), ONAIR_MON_IF
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=beacon-capture
LOG="$EVID/beacon-capture.log"

CAPTURE_SECONDS=${ONAIR_CAPTURE_SECONDS:-20}
while [ $# -gt 0 ]; do
	arg=$1
	shift
	case "$arg" in
	--duration)
		[ $# -gt 0 ] || { err "--duration needs seconds"; exit 2; }
		CAPTURE_SECONDS=$1
		shift
		;;
	--help | -h)
		sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "unknown argument: $arg (see --help)"
		exit 2
		;;
	esac
done

validate_monitor_iface "$MON_IF" || exit 2
require_root || exit 2
if [ "$DRY_RUN" = 1 ]; then
	log "[dry-run] monitor capture needs a live raw socket; recording SKIP (the live rung is best effort anyway)"
	verdict beacon-capture SKIP "dry-run: monitor capture not executed"
	exit 0
fi
iface_exists "$OBS_IF" || { err "observer iface $OBS_IF missing (run tools/iface-up.sh)"; exit 2; }
if ! iface_exists "$MON_IF"; then
	log "creating monitor interface $MON_IF"
	"$TOOLS_DIR/iface-up.sh" --monitor || true
fi
if ! iface_exists "$MON_IF"; then
	warn "monitor interface $MON_IF unavailable on this wiphy combination"
	verdict beacon-capture SKIP "monitor interface could not be created on the host wiphy; scan rung remains the on-air proof"
	exit 0
fi

guard_check "beacon-capture" || exit 2
PCAP="$EVID/beacons.pcap"
SUMMARY="$EVID/beacon-summary.json"
SNIFF_LOG="$EVID/sniff.log"
log "starting monitor capture on $MON_IF for ${CAPTURE_SECONDS}s (pcap: $PCAP)"
"$TOOLS_DIR/sniff.py" --iface "$MON_IF" --ssid "$AP_SSID" --duration "$CAPTURE_SECONDS" \
	--pcap-out "$PCAP" --json-out "$SUMMARY" >"$SNIFF_LOG" 2>&1 &
SNIFF_PID=$!
printf '%s\n' "$SNIFF_PID" >"$EVID/sniff.pid"

section "L2b drive the phy onto the AP channel with a passive scan"
sleep 1
DWELL=${ONAIR_SCAN_DWELL_TU:-1500}
capture_file "$EVID/beacon-scan.txt" iw dev "$OBS_IF" scan freq "$AP_FREQ" passive duration "$DWELL" -u || warn "dwell scan failed; the monitor may have missed the channel"

section "L2b wait for the capture"
wait "$SNIFF_PID"
SNIFF_RC=$?
rm -f "$EVID/sniff.pid"
cat "$SNIFF_LOG" >>"$LOG"
if [ "$SNIFF_RC" -eq 2 ]; then
	guard_check "beacon-capture" || exit 2
	verdict beacon-capture HARNESS-ERROR "sniff.py could not run (raw socket) - see $SNIFF_LOG"
	exit 2
fi

ASSESS_RC=1
if [ -f "$SUMMARY" ]; then
	python3 - "$SUMMARY" <<'PY' || ASSESS_RC=1
import json, sys
d = json.load(open(sys.argv[1]))
print("frames=%d beacons=%d expected-beacons=%d auth=%d (to-ap=%d from-ap=%d) assoc-req=%d" % (
    d.get("frames_total", 0), d.get("beacons_total", 0), d.get("beacons_expected", 0),
    d.get("auth_frames", 0), d.get("auth_to_ap", 0), d.get("auth_from_ap", 0),
    d.get("assoc_requests", 0)))
print("verdict: %s: %s" % (d.get("verdict"), d.get("detail")))
sys.exit(0 if d.get("verdict") == "PASS" else 1)
PY
	ASSESS_RC=$?
fi
guard_check "beacon-capture" || exit 2

section "L2b verdict"
if [ "$ASSESS_RC" -eq 0 ]; then
	verdict beacon-capture PASS "monitor captured expected beacons (count/interval/IEs in $SUMMARY)"
	exit 0
fi
verdict beacon-capture FAIL "monitor capture ran but did not observe the expected beacons - see $SUMMARY; keep the scan rung as the primary evidence"
exit 1