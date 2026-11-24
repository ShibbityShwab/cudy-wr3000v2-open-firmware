#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# preflight.sh - L0 host readiness for the wifi-onair-observer harness.
#
# HOST-ONLY and read-only by default.  Checks the host Wi-Fi stack, that the
# host's own uplink interface is present, and that the observer interface
# name is safe; it never runs a command against the bench router and never
# drops the base uplink.  --check-only is safe to run from anywhere (no root,
# no mutation).  The live phases need root (nl80211/netdev operations).
#
# Usage:
#   tools/preflight.sh --check-only     # read-only host report + verdict
#   sudo tools/preflight.sh             # same checks, live-mode gate
#   ONAIR_EVIDENCE_DIR=... tools/preflight.sh --check-only
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=preflight
LOG="$EVID/preflight.log"

CHECK_ONLY=0
for arg in "$@"; do
	case "$arg" in
	--check-only) CHECK_ONLY=1 ;;
	--help | -h)
		sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "unknown argument: $arg (see --help)"
		exit 2
		;;
	esac
done

FAILED=0
bad() { err "$*"; FAILED=$((FAILED + 1)); }
ok() { log "ok   $*"; }

section "host environment"
log "lane: $LANE_DIR"
log "evidence: $EVID"
log "mode: dry-run=$DRY_RUN check-only=$CHECK_ONLY base-if=$BASE_IF observer-if=$OBS_IF monitor-if=$MON_IF"
log "ap: ssid='$AP_SSID' freq=${AP_FREQ}MHz channel=$AP_CHANNEL"
have python3 && ok "python3: $(python3 -V 2>&1)" || bad "python3 missing"
have iw && ok "iw: $(iw --version 2>&1 | head -n 1)" || bad "iw missing"
have wpa_supplicant && ok "wpa_supplicant: $(wpa_supplicant -v 2>&1 | head -n 1)" || bad "wpa_supplicant missing"
have wpa_cli && ok "wpa_cli: present" || bad "wpa_cli missing"
have ip && ok "ip: present" || bad "ip missing"
have nmcli && ok "nmcli: $(nmcli --version 2>&1 | head -n 1)" || bad "nmcli missing (needed to keep NetworkManager off the observer iface)"
have ping && ok "ping: $(ping -V 2>&1 | head -n 1)" || warn "ping missing: L4b ping rung will be SKIPPED"
have arping && ok "arping: present" || warn "arping missing: optional L2 reachability probe skipped"
have git && ok "git: present (evidence-root discovery)" || warn "git missing: evidence defaults to lane/local-evidence"

section "base uplink guard (must never be touched)"
PHY=
if iw dev "$BASE_IF" info >/dev/null 2>&1; then
	ok "base iface $BASE_IF present"
	PHY=$(phy_of "$BASE_IF")
	if [ -n "$PHY" ]; then
		ok "base iface phy: $PHY"
	else
		bad "cannot determine phy of $BASE_IF"
	fi
else
	bad "base iface $BASE_IF not found (host Wi-Fi down?); the harness refuses to run without its guard target"
fi
guard_snapshot || true
if iw dev "$BASE_IF" link 2>/dev/null | grep -q '^Connected to'; then
	ok "base uplink $BASE_IF connected (SSID: $(iw dev "$BASE_IF" link 2>/dev/null | sed -n 's/^[[:space:]]*SSID: //p' | head -n 1)) -> the harness must not drop it"
else
	warn "base uplink $BASE_IF is not connected; continuing is safe but there is no uplink to protect"
fi

section "wiphy capability for a second managed interface"
if [ -n "$PHY" ]; then
	capture_file "$EVID/iw-phy-info.txt" iw phy "$PHY" info || bad "iw phy $PHY info failed"
	if grep -q '^[[:space:]]*\* managed' "$EVID/iw-phy-info.txt" 2>/dev/null; then
		ok "managed mode supported"
	else
		bad "wiphy does not advertise managed mode"
	fi
	if grep -q '^[[:space:]]*\* monitor' "$EVID/iw-phy-info.txt" 2>/dev/null; then
		ok "monitor mode advertised (optional beacon capture rung)"
	else
		warn "monitor mode not advertised: L2b beacon capture will be SKIPPED"
	fi
	if grep -q 'valid interface combinations' "$EVID/iw-phy-info.txt" 2>/dev/null; then
		COMBOS=$(sed -n '/valid interface combinations/,/^[[:space:]]*$/p' "$EVID/iw-phy-info.txt")
		if printf '%s\n' "$COMBOS" | grep -qE 'managed.*<=\s*[2-9]'; then
			ok "wiphy allows 2 managed interfaces in one combination (second iface can coexist with $BASE_IF)"
		else
			warn "no explicit 2-managed-interface combination found; creating $OBS_IF may fail with -EBUSY (recorded as L1 FAIL, not a router defect)"
		fi
		if printf '%s\n' "$COMBOS" | grep -q '#channels <= 2'; then
			ok "wiphy supports 2 channels concurrently (base uplink can stay on its channel during the ch6 scan)"
		else
			warn "wiphy does not advertise a 2-channel combination; the ch6 scan may briefly off-channel the base uplink"
		fi
	fi
fi

section "observer interface name and state"
if validate_observer_iface "$OBS_IF"; then ok "observer iface name '$OBS_IF' is safe"; else bad "unsafe observer iface"; fi
if iface_exists "$OBS_IF"; then
	warn "interface $OBS_IF already exists (stale run?) - run tools/cleanup.sh before a live run"
else
	ok "observer iface $OBS_IF does not exist (L1 will create it)"
fi
if [ -f "$EVID/supplicant.pid" ]; then
	warn "stale wpa_supplicant pid file found in evidence dir - run tools/cleanup.sh"
else
	ok "no stale wpa_supplicant pid file"
fi
if nmcli -t -f DEVICE device 2>/dev/null | grep -qx "$OBS_IF"; then
	warn "NetworkManager already sees $OBS_IF; L1 will set it unmanaged"
fi

section "bench-link interface (informational only; harness never touches it)"
if iface_exists enp195s0f3u1; then
	ok "bench USB-Ethernet enp195s0f3u1 present; the harness never addresses it"
else
	warn "enp195s0f3u1 not present (bench link down); irrelevant to the Wi-Fi observer but noted"
fi

section "root / mode"
if [ "$DRY_RUN" = 1 ]; then
	ok "dry-run: external commands are mocked; no host mutation"
elif [ "$CHECK_ONLY" = 1 ]; then
	ok "check-only: read-only; live phases must be re-run under sudo"
else
	if [ "$(id -u)" -eq 0 ]; then
		ok "root: live phases may run"
	else
		bad "not root: live phases need sudo"
	fi
fi

if [ "$DRY_RUN" != 1 ] && [ "$CHECK_ONLY" != 1 ]; then
	section "NetworkManager unmanaged guard"
	NM_CONF=/etc/NetworkManager/conf.d/99-wifi-onair-observer.conf
	if [ -f "$NM_CONF" ] && grep -q 'wifi-onair-observer' "$NM_CONF" 2>/dev/null; then
		ok "NM unmanaged conf present: $NM_CONF"
	else
		log "NM unmanaged conf not yet installed (iface-up.sh installs it before creating $OBS_IF)"
	fi
fi

section "verdict"
if [ "$FAILED" -ne 0 ]; then
	verdict preflight FAIL "$FAILED check(s) failed; see $LOG"
	exit 1
fi
verdict preflight PASS "host stack, base-uplink guard target and observer name checks passed (check-only=$CHECK_ONLY)"
exit 0