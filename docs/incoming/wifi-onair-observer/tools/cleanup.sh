#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# cleanup.sh - restore the host to its pre-run state.  Idempotent; the last
# line of defence for the base uplink guard.
#
# HOST-ONLY.  Removes ONLY artefacts this harness created:
#   - the observer wpa_supplicant instance (its pid file/ctrl socket), never
#     NetworkManager's own supplicant;
#   - the observer interface (default wlan1) and the monitor interface
#     (default mon-onair);
#   - the addresses the harness configured on the observer interface (the
#     interface is deleted, so this is implicit);
#   - the NetworkManager unmanaged drop-in the harness installed;
#   - a leftover sniff.py process from this evidence dir.
# It never touches the base uplink (wlan0), the bench USB-Ethernet link
# (enp195s0f3u1) or any other interface.
#
# Usage:
#   sudo tools/cleanup.sh
#   sudo tools/cleanup.sh --iface-only     # skip the NM drop-in removal
#   tools/cleanup.sh --help
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=cleanup
LOG="$EVID/cleanup.log"

IFACE_ONLY=0
for arg in "$@"; do
	case "$arg" in
	--iface-only) IFACE_ONLY=1 ;;
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

validate_observer_iface "$OBS_IF" || exit 2
validate_monitor_iface "$MON_IF" || exit 2
# Root is only required when there is something to remove (the script is
# idempotent; a non-root run on a clean host is allowed and reports PASS).
if [ "$DRY_RUN" = 1 ] || iface_exists "$OBS_IF" || iface_exists "$MON_IF"; then
	require_root || exit 2
fi
NM_CONF=/etc/NetworkManager/conf.d/99-wifi-onair-observer.conf
FAILED=0

section "cleanup: observer wpa_supplicant"
stop_observer_supplicant || FAILED=$((FAILED + 1))

section "cleanup: sniff.py"
if [ -f "$EVID/sniff.pid" ]; then
	SNIFF_PID=$(cat "$EVID/sniff.pid" 2>/dev/null || true)
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] would stop sniff.py pid ${SNIFF_PID:-?}"
	else
		if [ -n "$SNIFF_PID" ] && kill -0 "$SNIFF_PID" 2>/dev/null; then
			log "stopping leftover sniff.py pid $SNIFF_PID"
			kill "$SNIFF_PID" 2>/dev/null || true
		fi
	fi
	rm -f "$EVID/sniff.pid"
fi

section "cleanup: interfaces"
if iface_exists "$MON_IF"; then
	log "removing monitor interface $MON_IF"
	run iw dev "$MON_IF" del 2>/dev/null || run ip link del "$MON_IF" || FAILED=$((FAILED + 1))
fi
if iface_exists "$OBS_IF"; then
	log "removing observer interface $OBS_IF (removes any lease/static address with it)"
	run iw dev "$OBS_IF" del 2>/dev/null || run ip link del "$OBS_IF" || FAILED=$((FAILED + 1))
fi
if [ "$DRY_RUN" != 1 ]; then
	onair_i=0
	while [ "$onair_i" -lt 10 ] && { iface_exists "$MON_IF" || iface_exists "$OBS_IF"; }; do
		sleep 1
		onair_i=$((onair_i + 1))
	done
fi
if iface_exists "$MON_IF" || iface_exists "$OBS_IF"; then
	err "observer/monitor interface still present after delete"
	FAILED=$((FAILED + 1))
else
	log "interfaces clean"
fi

section "cleanup: NetworkManager drop-in"
if [ "$IFACE_ONLY" = 1 ]; then
	log "skipping NM drop-in removal (--iface-only)"
elif [ -f "$NM_CONF" ] && grep -q 'wifi-onair-observer' "$NM_CONF" 2>/dev/null; then
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] would remove $NM_CONF and reload NetworkManager"
	else
		rm -f "$NM_CONF" && log "removed $NM_CONF"
		have nmcli && run nmcli general reload || warn "nmcli general reload failed"
	fi
else
	log "no harness NM drop-in present"
fi

section "cleanup: temporary supplicant state"
if [ "$DRY_RUN" != 1 ]; then
	case "$ONAIR_CTRL_DIR" in
	/run/wpa_supplicant-onair) rm -rf "$ONAIR_CTRL_DIR" ;;
	*) warn "not removing unexpected ctrl dir: $ONAIR_CTRL_DIR" ;;
	esac
	rm -f "$EVID/supplicant-active.conf" "$EVID/supplicant.pid"
fi

section "cleanup: base uplink guard (final)"
guard_check "cleanup" || FAILED=$((FAILED + 1))
if iw dev "$BASE_IF" link 2>/dev/null | grep -q '^Connected to'; then
	log "base uplink $BASE_IF is connected (unchanged by the harness)"
elif iface_exists "$BASE_IF"; then
	log "base uplink $BASE_IF is present but not connected (unchanged by the harness)"
else
	log "base uplink $BASE_IF is not present (unchanged by the harness)"
fi

section "cleanup verdict"
if [ "$FAILED" -ne 0 ]; then
	verdict cleanup FAIL "$FAILED cleanup step(s) failed - inspect the host manually"
	exit 2
fi
verdict cleanup PASS "observer artefacts removed; base uplink $BASE_IF untouched"
exit 0