#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# lib.sh - shared helpers for the wifi-onair-observer host harness.
#
# HOST-ONLY: nothing in this lane talks to the bench router (no SSH, no UART,
# no TFTP, no flash, no devmem). The observer is a second managed Wi-Fi
# interface on the host (default wlan1) plus an optional monitor interface.
# The BASE interface (default wlan0, the host's own uplink) must never be
# touched or dropped.
#
# This file is POSIX sh and is sourced, never executed.
set -u

LANE_DIR=$(cd "$(dirname "$0")/.." 2>/dev/null && pwd)
CONFIG_DIR="$LANE_DIR/configs"
TOOLS_DIR="$LANE_DIR/tools"

BASE_IF=${ONAIR_BASE_IF:-wlan0}
OBS_IF=${ONAIR_IFACE:-wlan1}
MON_IF=${ONAIR_MON_IF:-mon-onair}
AP_SSID=${ONAIR_SSID:-cudy-ap-test}
AP_FREQ=${ONAIR_FREQ:-2437}          # MHz; 2437 = 2.4 GHz channel 6
AP_CHANNEL=${ONAIR_CHANNEL:-6}
SCAN_TIMEOUT=${ONAIR_SCAN_TIMEOUT:-25}
ASSOC_TIMEOUT=${ONAIR_ASSOC_TIMEOUT:-45}
DRY_RUN=${ONAIR_DRY_RUN:-0}
MOCK_DIR=${ONAIR_MOCK_DIR:-}
MOCK_STATE=${ONAIR_MOCK_STATE:-}

# --- evidence directory ----------------------------------------------------
# Never write live runs into the tracked lane; default to the parent repo's
# gitignored build/ tree (local evidence convention), falling back to
# lane/local-evidence (gitignored by the lane .gitignore).
onair_toplevel=$(git -C "$LANE_DIR" rev-parse --show-toplevel 2>/dev/null || true)
if [ -n "$onair_toplevel" ] && [ -d "$(dirname "$onair_toplevel")/build" ]; then
	ONAIR_EVIDENCE_BASE="$(dirname "$onair_toplevel")/build/wifi-onair-observer"
else
	ONAIR_EVIDENCE_BASE="$LANE_DIR/local-evidence"
fi
EVIDENCE_BASE=${ONAIR_EVIDENCE_BASE:-$ONAIR_EVIDENCE_BASE}
EVID=${ONAIR_EVIDENCE_DIR:-$EVIDENCE_BASE/$(date -u +%Y%m%dT%H%M%SZ)${ONAIR_EVIDENCE_LABEL:+-$ONAIR_EVIDENCE_LABEL}}
mkdir -p "$EVID" 2>/dev/null || true
PHASE=${ONAIR_PHASE:-main}
LOG="$EVID/$PHASE.log"
VERDICTS="$EVID/verdicts.txt"
GUARD_STATE="$EVID/guard-wlan0.txt"

# --- logging ---------------------------------------------------------------
log() { printf '%s\n' "[$PHASE] $*"; printf '%s\n' "[$PHASE] $*" >>"$LOG"; }
warn() { printf '%s\n' "[$PHASE] WARN: $*" >&2; printf '%s\n' "[$PHASE] WARN: $*" >>"$LOG"; }
err() { printf '%s\n' "[$PHASE] ERROR: $*" >&2; printf '%s\n' "[$PHASE] ERROR: $*" >>"$LOG"; }
section() { printf '\n=== %s ===\n' "$*"; printf '\n=== %s ===\n' "$*" >>"$LOG"; }

have() { command -v "$1" >/dev/null 2>&1; }

# In dry-run the whole PATH is mocked; refuse to run a non-mock binary so a
# scripting mistake can never touch the real host network.
_mock_guard() {
	[ "$DRY_RUN" = 1 ] || return 0
	if [ -z "$MOCK_DIR" ]; then
		err "dry-run requested without ONAIR_MOCK_DIR"
		exit 2
	fi
	onair_resolved=$(command -v "$1" 2>/dev/null || true)
	case "$onair_resolved" in
	"$MOCK_DIR"/*) return 0 ;;
	esac
	err "dry-run refused non-mock command: $1 (resolved: ${onair_resolved:-none})"
	exit 2
}

# run <cmd...> : execute, log, fail the phase on non-zero unless ONAIR_ALLOW_FAIL=1
run() {
	_mock_guard "$1"
	log "+ $*"
	if "$@" >>"$LOG" 2>&1; then
		onair_rc=0
	else
		onair_rc=$?
	fi
	if [ "$onair_rc" -ne 0 ] && [ "${ONAIR_ALLOW_FAIL:-0}" != 1 ]; then
		err "command failed rc=$onair_rc: $*"
		return "$onair_rc"
	fi
	return 0
}

# capture <cmd...> : stdout+stderr to caller AND the phase log
capture() {
	_mock_guard "$1"
	onair_out=$("$@" 2>&1)
	onair_rc=$?
	printf '%s\n' "$onair_out" >>"$LOG"
	printf '%s\n' "$onair_out"
	return "$onair_rc"
}

# capture_file <file> <cmd...> : store output in <file>, log it, return rc
capture_file() {
	onair_f=$1
	shift
	_mock_guard "$1"
	if "$@" >"$onair_f" 2>&1; then
		onair_rc=0
	else
		onair_rc=$?
	fi
	{ printf -- '--- %s ---\n' "$*"; cat "$onair_f"; printf '\n'; } >>"$LOG"
	return "$onair_rc"
}

# verdict <phase> <status> <detail>   status: PASS FAIL EXPECTED-FAIL BLOCKED SKIP HARNESS-ERROR GUARD-FAIL
verdict() {
	onair_line=$(printf 'ONAIR-VERDICT|%s|%s|%s' "$1" "$2" "$3")
	printf '%s\n' "$onair_line" >>"$VERDICTS"
	printf '%s\n' "$onair_line"
}

require_root() {
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] root requirement skipped (mock environment)"
		return 0
	fi
	if [ "$(id -u)" -ne 0 ]; then
		err "this phase needs root (Wi-Fi netdev / nl80211 operations): re-run with sudo"
		return 1
	fi
	return 0
}

# --- interface safety ------------------------------------------------------
# The observer interface may only be a secondary wlanX; the monitor interface
# may only be a mon* name. The host uplink, the bench USB-Ethernet link and
# anything else are refused.
validate_observer_iface() {
	onair_name=$1
	onair_why=
	case "$onair_name" in
	wlan[0-9] | wlan[0-9][0-9]) ;;
	*) onair_why="observer iface must look like wlanN" ;;
	esac
	if [ -z "$onair_why" ] && [ "$onair_name" = "$BASE_IF" ]; then
		onair_why="observer iface must differ from the base uplink iface ($BASE_IF)"
	fi
	if [ -z "$onair_why" ] && [ "$onair_name" = "enp195s0f3u1" ]; then
		onair_why="refusing to touch the bench USB-Ethernet link"
	fi
	if [ -n "$onair_why" ]; then
		err "refusing interface '$onair_name': $onair_why"
		return 1
	fi
	return 0
}

validate_monitor_iface() {
	onair_name=$1
	case "$onair_name" in
	mon*) ;;
	*)
		err "refusing monitor iface '$onair_name': must start with 'mon'"
		return 1
		;;
	esac
	case "$onair_name" in
	"$BASE_IF" | enp* | eth*)
		err "refusing monitor iface '$onair_name' (host/bench interface)"
		return 1
		;;
	esac
	return 0
}

# wlan0 guard: record the base uplink state before RF work and re-check after
# every phase. The harness never runs a command against $BASE_IF.
guard_snapshot() {
	{
		iw dev "$BASE_IF" info 2>&1
		printf -- '--- link ---\n'
		iw dev "$BASE_IF" link 2>&1
	} >"$GUARD_STATE" 2>&1 || true
	if iw dev "$BASE_IF" link 2>/dev/null | grep -q '^Connected to'; then
		printf 'connected\n' >>"$GUARD_STATE"
		log "guard: base uplink $BASE_IF is connected before RF work (will be re-checked)"
		GUARD_WAS_CONNECTED=1
	else
		printf 'disconnected\n' >>"$GUARD_STATE"
		log "guard: base uplink $BASE_IF is not connected (nothing to protect; still untouched)"
		GUARD_WAS_CONNECTED=0
	fi
}

guard_check() {
	onair_phase=${1:-phase}
	if iw dev "$BASE_IF" link 2>/dev/null | grep -q '^Connected to'; then
		onair_now=1
	else
		onair_now=0
	fi
	onair_was=0
	if [ -f "$GUARD_STATE" ] && tail -n 1 "$GUARD_STATE" | grep -qx connected; then
		onair_was=1
	fi
	if [ "$onair_was" = 1 ] && [ "$onair_now" = 0 ]; then
		err "guard: base uplink $BASE_IF LOST its connection during '$onair_phase'"
		verdict guard GUARD-FAIL "$BASE_IF disconnected during $onair_phase"
		return 1
	fi
	log "guard: base uplink $BASE_IF still connected during '$onair_phase'"
	return 0
}

# --- small helpers ---------------------------------------------------------
phy_of() { # phy_of <iface> -> phyN (empty if unknown)
	iw dev "$1" info 2>/dev/null | sed -n 's/.*wiphy \([0-9][0-9]*\).*/phy\1/p' | head -n 1
}

# In dry-run the netdev world is simulated under ONAIR_MOCK_STATE/net/<iface>/.
iface_exists() {
	if [ "$DRY_RUN" = 1 ]; then
		[ -n "$MOCK_STATE" ] && [ -e "$MOCK_STATE/net/$1" ]
		return $?
	fi
	[ -e "/sys/class/net/$1" ]
}
iface_is_up() {
	if [ "$DRY_RUN" = 1 ]; then
		iface_exists "$1"
		return $?
	fi
	[ -f "/sys/class/net/$1/operstate" ] && [ "$(cat "/sys/class/net/$1/operstate" 2>/dev/null)" = up ]
}
iface_mac() {
	if [ "$DRY_RUN" = 1 ]; then
		cat "$MOCK_STATE/net/$1/mac" 2>/dev/null
		return 0
	fi
	cat "/sys/class/net/$1/address" 2>/dev/null
}

psk_redact() { # stdin -> stdout with psk values masked (for evidence copies)
	sed -e 's/^\([[:space:]]*psk=\).*/\1"***REDACTED***"/' -e 's/^\([[:space:]]*wpa_passphrase=\).*/\1***REDACTED***/'
}

# Temporary root-only copy of a fixture; respects ONAIR_PSK override WITHOUT
# ever logging the value.  Prints the temp path.
stage_supplicant_conf() { # stage_supplicant_conf <source> -> path
	onair_src=$1
	onair_dst="$EVID/supplicant-active.conf"
	if [ -n "${ONAIR_PSK:-}" ]; then
		case "$ONAIR_PSK" in
		*[![:print:]]* | *'"'* | *\\*)
			err "ONAIR_PSK contains characters that cannot go into the fixture"
			return 1
			;;
		esac
		if [ "${#ONAIR_PSK}" -lt 8 ]; then
			err "ONAIR_PSK is shorter than the 8-character WPA passphrase minimum"
			return 1
		fi
		ONAIR_PSK_ENV=$ONAIR_PSK awk '
			BEGIN { psk = ENVIRON["ONAIR_PSK_ENV"] }
			/^[[:space:]]*psk=/ { printf "\tpsk=\"%s\"\n", psk; next }
			{ print }
		' "$onair_src" >"$onair_dst"
		chmod 600 "$onair_dst"
		# diagnostics go to the phase log only: stdout is the return channel
		printf '%s\n' "[$PHASE] staged supplicant config with an ONAIR_PSK override (value not logged)" >>"$LOG"
	else
		cp "$onair_src" "$onair_dst"
		chmod 600 "$onair_dst"
		printf '%s\n' "[$PHASE] staged supplicant config from $(basename "$onair_src")" >>"$LOG"
	fi
	printf '%s\n' "$onair_dst"
}

# Wait up to <timeout_s> for the wpa_supplicant ctrl socket directory.
wait_ctrl_ready() { # wait_ctrl_ready <dir> <timeout_s>
	onair_waited=0
	while [ "$onair_waited" -lt "$2" ]; do
		[ -S "$1/$OBS_IF" ] && return 0
		sleep 1
		onair_waited=$((onair_waited + 1))
	done
	return 1
}

# --- wpa_supplicant observer instance --------------------------------------
# A dedicated instance/ctrl-dir for the observer iface; NetworkManager's own
# wpa_supplicant on the base iface is never touched.
ONAIR_CTRL_DIR=${ONAIR_CTRL_DIR:-/run/wpa_supplicant-onair}
ONAIR_TERMINAL_FAIL_RE='Authentication with .* timed out|CTRL-EVENT-AUTH-REJECT|Association with .* timed out|CTRL-EVENT-ASSOC-REJECT|4-Way Handshake failed|CTRL-EVENT-CONNECTED|WPA: Key negotiation completed'

supplicant_log_has_terminal() { # supplicant_log_has_terminal <log>
	[ -f "$1" ] || return 1
	grep -qE "$ONAIR_TERMINAL_FAIL_RE" "$1"
}

start_observer_supplicant() { # start_observer_supplicant <config-path>
	onair_conf=$1
	onair_log="$EVID/supplicant.log"
	rm -f "$EVID/supplicant.pid" "$onair_log"
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] would start wpa_supplicant on $OBS_IF with the staged config"
	else
		mkdir -p "$ONAIR_CTRL_DIR"
	fi
	run wpa_supplicant -B -D nl80211 -i "$OBS_IF" -c "$onair_conf" \
		-P "$EVID/supplicant.pid" -f "$onair_log" -d || return 1
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] assuming the mock ctrl socket is ready"
		return 0
	fi
	wait_ctrl_ready "$ONAIR_CTRL_DIR" 10 || {
		err "wpa_supplicant ctrl socket did not appear in $ONAIR_CTRL_DIR"
		return 1
	}
	return 0
}

supplicant_status() { # supplicant_status -> status text on stdout
	capture wpa_cli -p "$ONAIR_CTRL_DIR" -i "$OBS_IF" status
}

stop_observer_supplicant() {
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] would terminate the observer wpa_supplicant (pid file only, no real kill)"
		rm -f "$EVID/supplicant.pid"
		return 0
	fi
	if [ -f "$EVID/supplicant.pid" ]; then
		onair_pid=$(cat "$EVID/supplicant.pid" 2>/dev/null || true)
		if [ -n "$onair_pid" ] && kill -0 "$onair_pid" 2>/dev/null; then
			capture wpa_cli -p "$ONAIR_CTRL_DIR" -i "$OBS_IF" terminate >/dev/null 2>&1 || true
			onair_i=0
			while [ "$onair_i" -lt 10 ] && kill -0 "$onair_pid" 2>/dev/null; do
				sleep 1
				onair_i=$((onair_i + 1))
			done
			if kill -0 "$onair_pid" 2>/dev/null; then
				warn "wpa_supplicant pid $onair_pid did not exit; sending TERM"
				kill "$onair_pid" 2>/dev/null || true
			fi
		fi
		rm -f "$EVID/supplicant.pid"
	fi
	log "observer wpa_supplicant stopped (base iface and NM are untouched)"
}