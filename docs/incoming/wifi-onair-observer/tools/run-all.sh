#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-all.sh - one-command external-observer run (L0-L4, optional L2b/L5).
#
# HOST-ONLY: the observer is a second managed Wi-Fi interface on the host
# (default wlan1) plus an optional monitor interface.  There is NO bench-router
# contact anywhere in this lane: no SSH, no UART, no TFTP, no flash.  The Lead
# starts the AP with the final module in the separate console session; this
# harness observes it over the air.
#
# Expected bench outcome (documented in RUNBOOK.md):
#   L0 preflight            PASS
#   L1 second interface     PASS
#   L2 passive scan ch6     PASS  <- decisive on-air beacon proof
#   L2b monitor capture     PASS or SKIP (best effort)
#   L3 WPA2 association     EXPECTED-FAIL at AUTH (AP mgmt-TX/RX fail-closed)
#   L4 DHCP                 BLOCKED while L3 is not complete (expected class)
#   L5 WPA3-SAE             SKIP unless the AP was restarted with the transition
#                           fixture; then EXPECTED-FAIL at SAE/AUTH
#
# Usage:
#   sudo tools/run-all.sh --label ap-final
#   sudo tools/run-all.sh --with-monitor --full
#   sudo tools/run-all.sh --skip-assoc            # scan + capture only
#   sudo tools/run-all.sh --wpa3                  # AP already in WPA2+WPA3 mode
#   sudo tools/run-all.sh --dry-run               # mock host, no mutation (tests)
#   tools/run-all.sh --help
#
# Exit codes: 0 all phases matched the expected bench outcome; 1 run completed
# with unexpected findings; 2 harness/safety error (base-uplink guard, cleanup).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)

DRY=0
WITH_MONITOR=0
SKIP_ASSOC=0
WPA3=0
FULL=0
EXPECT_OPEN=0
NO_CLEANUP=0
EVID_DIR=
LABEL=onair
while [ $# -gt 0 ]; do
	arg=$1
	shift
	case "$arg" in
	--dry-run) DRY=1 ;;
	--with-monitor) WITH_MONITOR=1 ;;
	--skip-assoc) SKIP_ASSOC=1 ;;
	--wpa3) WPA3=1 ;;
	--full) FULL=1 ;;
	--expect-open) EXPECT_OPEN=1 ;;
	--no-cleanup) NO_CLEANUP=1 ;;
	--evidence-dir)
		[ $# -gt 0 ] || { printf 'run-all: --evidence-dir needs a path\n' >&2; exit 2; }
		EVID_DIR=$1
		shift
		;;
	--label)
		[ $# -gt 0 ] || { printf 'run-all: --label needs a value\n' >&2; exit 2; }
		LABEL=$1
		shift
		;;
	--help | -h)
		sed -n '2,36p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		printf 'run-all: unknown argument: %s (see --help)\n' "$arg" >&2
		exit 2
		;;
	esac
done

if [ "$DRY" = 1 ]; then
	export ONAIR_DRY_RUN=1
	if [ -z "${ONAIR_MOCK_DIR:-}" ]; then
		printf 'run-all: --dry-run needs ONAIR_MOCK_DIR (the mock command directory)\n' >&2
		exit 2
	fi
fi
if [ -n "$EVID_DIR" ]; then
	export ONAIR_EVIDENCE_DIR="$EVID_DIR"
fi
export ONAIR_EVIDENCE_LABEL="$LABEL"
. "$HERE/lib.sh"

PHASE=run-all
LOG="$EVID/run-all.log"
CLEANUP_DONE=0

onabort() {
	warn "aborting: running cleanup before exit"
	if [ "$CLEANUP_DONE" = 0 ] && [ "$NO_CLEANUP" = 0 ]; then
		CLEANUP_DONE=1
		ONAIR_EVIDENCE_DIR="$EVID" ONAIR_PHASE=cleanup "$TOOLS_DIR/cleanup.sh" || true
	fi
	exit 2
}
trap onabort INT TERM HUP

section "wifi-onair-observer run: $LABEL"
log "evidence dir: $EVID"
log "mode: dry-run=$DRY monitor=$WITH_MONITOR skip-assoc=$SKIP_ASSOC wpa3=$WPA3 full=$FULL expect-open=$EXPECT_OPEN"
log "observer iface: $OBS_IF   base uplink: $BASE_IF   monitor: $MON_IF"
log "target AP: ssid='$AP_SSID' freq=${AP_FREQ}MHz channel=$AP_CHANNEL"
log "NOTE: this harness never contacts the router; the Lead owns the AP-side console."

run_phase() { # run_phase <name> <script> [args...]
	onair_name=$1
	onair_script=$2
	shift 2
	section "phase: $onair_name"
	ONAIR_EVIDENCE_DIR="$EVID" ONAIR_PHASE="$onair_name" "$onair_script" "$@"
	onair_rc=$?
	log "phase $onair_name: rc=$onair_rc"
	return "$onair_rc"
}

PHASE_FAILED=0
HARNESS_ERROR=0
HOST_READY=1
SCAN_OK=1

record_skip() { # record_skip <phase> <detail>
	onair_skip=$(printf 'ONAIR-VERDICT|%s|SKIP|%s' "$1" "$2")
	printf '%s\n' "$onair_skip" >>"$EVID/verdicts.txt"
	printf '%s\n' "$onair_skip"
	log "recording $1 as SKIP: $2"
}

# L0 -----------------------------------------------------------------------
run_phase preflight "$TOOLS_DIR/preflight.sh"
case $? in
0) ;;
2) HARNESS_ERROR=1; HOST_READY=0 ;;
*) PHASE_FAILED=1; HOST_READY=0 ;;
esac

if [ "$HOST_READY" = 1 ] && [ "$HARNESS_ERROR" = 0 ]; then
	# L1 -------------------------------------------------------------------
	run_phase iface-up "$TOOLS_DIR/iface-up.sh"
	case $? in
	0) ;;
	2) HARNESS_ERROR=1; HOST_READY=0 ;;
	*) PHASE_FAILED=1; HOST_READY=0 ;;
	esac
fi

if [ "$HOST_READY" = 1 ] && [ "$HARNESS_ERROR" = 0 ]; then
	# L2 -------------------------------------------------------------------
	SCAN_EXTRA=
	[ "$FULL" = 1 ] && SCAN_EXTRA=--full
	[ "$EXPECT_OPEN" = 1 ] && SCAN_EXTRA="$SCAN_EXTRA --expect-open"
	run_phase scan "$TOOLS_DIR/scan-ap.sh" $SCAN_EXTRA
	case $? in
	0) ;;
	2) HARNESS_ERROR=1; SCAN_OK=0 ;;
	*) PHASE_FAILED=1; SCAN_OK=0 ;;
	esac
fi

if [ "$HOST_READY" = 1 ] && [ "$HARNESS_ERROR" = 0 ] && [ "$WITH_MONITOR" = 1 ]; then
	# L2b ------------------------------------------------------------------
	run_phase beacon-capture "$TOOLS_DIR/capture-beacon.sh"
	case $? in
	0) ;;
	2) HARNESS_ERROR=1 ;;
	1) warn "monitor capture rung did not produce beacons (best effort; scan rung is decisive)" ;;
	esac
fi

if [ "$HARNESS_ERROR" = 0 ]; then
	if [ "$SKIP_ASSOC" = 1 ]; then
		record_skip wpa2-assoc "association rung skipped by --skip-assoc"
		record_skip dhcp "association rung skipped"
	elif [ "$SCAN_OK" = 0 ]; then
		record_skip wpa2-assoc "no on-air beacon (L2 FAIL); the association attempt is not meaningful"
		record_skip dhcp "no association"
	else
		# L3 ---------------------------------------------------------------
		run_phase wpa2-assoc "$TOOLS_DIR/wpa2-assoc.sh"
		ASSOC_RC=$?
		case "$ASSOC_RC" in
		2) HARNESS_ERROR=1 ;;
		1) PHASE_FAILED=1 ;;
		esac
		# L4 only when the association actually completed (UNEXPECTED-PASS)
		ASSOC_VERDICT=
		if [ -f "$EVID/assoc-result.json" ]; then
			ASSOC_VERDICT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("verdict",""))' "$EVID/assoc-result.json" 2>/dev/null || true)
		fi
		if [ "$ASSOC_VERDICT" = "PASS" ]; then
			run_phase dhcp "$TOOLS_DIR/dhcp-test.sh"
			case $? in
			2) HARNESS_ERROR=1 ;;
			1) PHASE_FAILED=1 ;;
			esac
		else
			record_skip dhcp "association not complete (L3 ${ASSOC_VERDICT:-unknown}); DHCP/ping not reached"
		fi
	fi
fi

if [ "$HARNESS_ERROR" = 0 ] && [ "$WPA3" = 1 ]; then
	# L5 -------------------------------------------------------------------
	if [ "$HOST_READY" = 0 ] || [ "$SCAN_OK" = 0 ]; then
		record_skip wpa3-sae "no on-air beacon scan to verify SAE mode"
	else
		run_phase wpa3-sae "$TOOLS_DIR/wpa3-sae.sh"
		case $? in
		2) HARNESS_ERROR=1 ;;
		1) PHASE_FAILED=1 ;;
		esac
	fi
fi

# cleanup always ------------------------------------------------------------
if [ "$NO_CLEANUP" = 0 ] && [ "$CLEANUP_DONE" = 0 ]; then
	CLEANUP_DONE=1
	run_phase cleanup "$TOOLS_DIR/cleanup.sh"
	case $? in
	2) HARNESS_ERROR=1 ;;
	esac
else
	warn "--no-cleanup: host artefacts left in place; run tools/cleanup.sh before leaving"
fi

# result matrix -------------------------------------------------------------
section "result matrix"
python3 - "$EVID" <<'PY'
import os, sys
evid = sys.argv[1]
verdicts = []
path = os.path.join(evid, "verdicts.txt")
if os.path.exists(path):
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.strip().split("|")
        if len(parts) == 4 and parts[0] == "ONAIR-VERDICT":
            verdicts.append(parts[1:])
expected = {
    "preflight": {"PASS": "expected"},
    "iface-up": {"PASS": "expected"},
    "scan": {"PASS": "expected - decisive on-air beacon proof",
             "FAIL": "UNEXPECTED: beacon not on air / wrong channel / host wiphy"},
    "beacon-capture": {"PASS": "expected (best effort)", "SKIP": "allowed (wiphy combination)",
                       "FAIL": "optional rung failed; scan rung remains decisive"},
    "wpa2-assoc": {"EXPECTED-FAIL": "expected - AP mgmt-TX/RX fail-closed blocker",
                   "PASS": "UNEXPECTED-PASS: association completed; four-way result recorded",
                   "FAIL": "UNEXPECTED: association advanced past the known blocker - new evidence"},
    "dhcp": {"PASS": "unexpected good: DHCP works", "BLOCKED": "expected while association is blocked",
             "SKIP": "expected (no association / static mode)", "FAIL": "UNEXPECTED"},
    "ping": {"PASS": "unexpected good: data path works", "SKIP": "expected", "FAIL": "data-path finding"},
    "wpa3-sae": {"SKIP": "expected unless the AP was restarted with SAE",
                 "EXPECTED-FAIL": "expected - SAE needs the mgmt TX/RX path",
                 "PASS": "UNEXPECTED-PASS: WPA3 worked - re-verify and update the RE report"},
    "cleanup": {"PASS": "expected"},
}
rows, findings = [], []
for phase, status, detail in verdicts:
    exp = expected.get(phase, {}).get(status, "record")
    rows.append((phase, status, exp, detail))
    if exp.startswith("UNEXPECTED") or status in ("HARNESS-ERROR", "GUARD-FAIL"):
        findings.append("%s %s: %s" % (phase, status, detail))
lines = ["# External-observer PASS/FAIL matrix", "",
         "Run: %s" % os.path.basename(evid), "",
         "| phase | verdict | expectation | detail |", "| --- | --- | --- | --- |"]
for phase, status, exp, detail in rows:
    lines.append("| %s | **%s** | %s | %s |" % (phase, status, exp, detail.replace("|", "/")))
if not rows:
    lines.append("| (none) | | | no verdicts recorded |")
lines += ["", "## Findings", ""]
if findings:
    lines += ["- " + f for f in findings]
else:
    lines.append("- none: every phase matched its expected class")
open(os.path.join(evid, "matrix.md"), "w", encoding="utf-8").write("\n".join(lines) + "\n")
print("\n".join(lines))
PY

section "run summary"
if [ -f "$EVID/matrix.md" ]; then
	log "matrix written: $EVID/matrix.md"
fi
log "verdicts: $(grep -c '^ONAIR-VERDICT' "$EVID/verdicts.txt" 2>/dev/null || echo 0) phase result(s)"
if [ "$HARNESS_ERROR" != 0 ]; then
	log "RESULT: HARNESS/SAFETY ERROR - inspect the logs above; host cleanup was attempted"
	exit 2
fi
if [ "$PHASE_FAILED" != 0 ]; then
	log "RESULT: run completed with UNEXPECTED findings (see matrix.md)"
	exit 1
fi
log "RESULT: run completed - every phase matched its expected class (scan PASS is the decisive on-air beacon proof)"
exit 0