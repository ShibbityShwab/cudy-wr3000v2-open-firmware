#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# wpa2-assoc.sh - L3 WPA2-PSK association attempt + 4-way handshake result.
#
# HOST-ONLY: starts a dedicated wpa_supplicant instance on the observer
# interface and records the exact outcome.  The bench AP is never contacted
# out-of-band; the interaction is ordinary 802.11 on the air.
#
# The expected outcome today is AUTH-TIMEOUT: the clean-room AP driver's
# management-TX path is fail-closed and there is no management RX, so the
# client's AUTH frame gets no AP answer.  The harness classifies that as
# EXPECTED-FAIL (it reproduces the documented blocker); anything further is
# recorded honestly (a completed association is an UNEXPECTED-PASS and its
# 4-way result is the decisive key-path datum).
#
# Usage:
#   sudo tools/wpa2-assoc.sh                     # default config + timeout
#   sudo tools/wpa2-assoc.sh --config FILE --timeout 45
#   tools/wpa2-assoc.sh --help
#
# Env: ONAIR_PSK (lab passphrase override, never logged), ONAIR_ASSOC_TIMEOUT,
#      ONAIR_AP_BSSID (expected BSSID from the L2 scan), ONAIR_CTRL_DIR
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=wpa2-assoc
LOG="$EVID/wpa2-assoc.log"

VARIANT=wpa2-psk
CONF="$CONFIG_DIR/wpa_supplicant-wpa2.conf"
TIMEOUT=$ASSOC_TIMEOUT
while [ $# -gt 0 ]; do
	arg=$1
	shift
	case "$arg" in
	--wpa3) VARIANT=wpa3-sae; PHASE=wpa3-sae; LOG="$EVID/wpa3-sae.log"; CONF="$CONFIG_DIR/wpa_supplicant-wpa3-sae.conf"; TIMEOUT=${ONAIR_WPA3_TIMEOUT:-60} ;;
	--config)
		[ $# -gt 0 ] || { err "--config needs a path"; exit 2; }
		CONF=$1
		shift
		;;
	--timeout)
		[ $# -gt 0 ] || { err "--timeout needs seconds"; exit 2; }
		TIMEOUT=$1
		shift
		;;
	--help | -h)
		sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "unknown argument: $arg (see --help)"
		exit 2
		;;
	esac
done

[ -f "$CONF" ] || { err "supplicant config not found: $CONF"; exit 2; }
iface_exists "$OBS_IF" || { err "observer iface $OBS_IF missing (run tools/iface-up.sh)"; exit 2; }
guard_check "$PHASE" || exit 2
require_root || exit 2

if [ -e "$EVID/scan-ch6.json" ]; then
	log "L2 scan evidence present: $(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d.get("verdict","?"), "-", d.get("detail","")[:120])' "$EVID/scan-ch6.json" 2>/dev/null || echo 'unreadable')"
else
	warn "no L2 scan evidence in this run: the association attempt will start with the client's own scan; run tools/scan-ap.sh first for the on-air gate"
fi

EXPECT_BSSID=${ONAIR_AP_BSSID:-}
if [ -z "$EXPECT_BSSID" ] && [ -f "$EVID/scan-ch6.json" ]; then
	EXPECT_BSSID=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); t=d.get("target") or {}; print(t.get("bssid",""))' "$EVID/scan-ch6.json" 2>/dev/null || true)
fi
if [ -n "$EXPECT_BSSID" ]; then
	log "expected AP BSSID (from the on-air beacon): $EXPECT_BSSID"
else
	warn "no expected AP BSSID available; bssid cross-check will be skipped"
fi

section "L3 stage the supplicant fixture"
STAGED=$(stage_supplicant_conf "$CONF") || exit 2
psk_redact <"$STAGED" >"$EVID/supplicant-config.redacted.txt"
log "staged config: $(basename "$STAGED") (passphrase redacted in the evidence copy)"
log "variant: $VARIANT   timeout: ${TIMEOUT}s"

section "L3 start wpa_supplicant and wait for the outcome"
start_observer_supplicant "$STAGED" || { verdict "$PHASE" HARNESS-ERROR "wpa_supplicant did not start"; exit 2; }

STATUS_POLLS="$EVID/supplicant-status-polls.txt"
: >"$STATUS_POLLS"
ELAPSED=0
DONE=0
while [ "$ELAPSED" -lt "$TIMEOUT" ]; do
	STATUS=$(supplicant_status)
	printf -- '--- poll %ss ---\n%s\n' "$ELAPSED" "$STATUS" >>"$STATUS_POLLS"
	if printf '%s' "$STATUS" | grep -q 'wpa_state=COMPLETED'; then
		DONE=1
		break
	fi
	if supplicant_log_has_terminal "$EVID/supplicant.log"; then
		DONE=1
		break
	fi
	sleep 1
	ELAPSED=$((ELAPSED + 1))
done
log "polling finished after ${ELAPSED}s (done=$DONE)"

# Always stop the observer instance before judging; leave the log for the assessor.
supplicant_status >"$EVID/status-final.txt" 2>&1 || true
capture iw dev "$OBS_IF" link >"$EVID/link-final.txt" 2>&1 || true
stop_observer_supplicant
rm -f "$STAGED"

section "L3 assess the handshake outcome"
python3 "$TOOLS_DIR/wpa_assess.py" --supplicant-log "$EVID/supplicant.log" \
	--status "$EVID/status-final.txt" --variant "$VARIANT" \
	--expect-bssid "$EXPECT_BSSID" --json-out "$EVID/assoc-result.json"
ASSESS_RC=$?
if [ -f "$EVID/assoc-result.json" ]; then
	python3 - "$EVID/assoc-result.json" >"$EVID/handshake-evidence.txt" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("assoc stage: %s" % d.get("stage"))
print("assoc complete: %s   four-way: %s" % (d.get("assoc"), d.get("fourway")))
for line in d.get("evidence_lines", []):
    print(line)
PY
fi

guard_check "$PHASE" || exit 2

section "L3 verdict"
case "$ASSESS_RC" in
0)
	if [ -f "$EVID/assoc-result.json" ] && grep -q '"verdict": "PASS"' "$EVID/assoc-result.json" 2>/dev/null; then
		verdict "$PHASE" PASS "association COMPLETED on air; four-way handshake PASS (see $EVID/assoc-result.json) - UNEXPECTED vs the known mgmt blocker, record it"
	elif [ -f "$EVID/assoc-result.json" ] && grep -q '"verdict": "EXPECTED-FAIL"' "$EVID/assoc-result.json" 2>/dev/null; then
		verdict "$PHASE" EXPECTED-FAIL "attempt stopped at the documented AP mgmt-TX/RX blocker; 4-way not reached (see $EVID/assoc-result.json)"
	else
		verdict "$PHASE" PASS "outcome classified (see $EVID/assoc-result.json)"
	fi
	exit 0
	;;
1)
	verdict "$PHASE" FAIL "unexpected association/handshake result - inspect $EVID/assoc-result.json and the AP-side logs"
	exit 1
	;;
*)
	verdict "$PHASE" HARNESS-ERROR "wpa_assess could not classify the capture (rc=$ASSESS_RC)"
	exit 2
	;;
esac