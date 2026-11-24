#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# iface-up.sh - L1 create the second managed observer interface (default wlan1)
#              without touching the host uplink (default wlan0).
#
# HOST-ONLY.  nl80211/netdev operations on the host's own Wi-Fi phy; the bench
# router is not contacted.  The base uplink is snapshotted before the change
# and re-checked afterwards; an unexpected base-uplink drop is a hard failure
# that aborts the run.
#
# Usage:
#   sudo tools/iface-up.sh                 # create OBS_IF (managed)
#   sudo tools/iface-up.sh --teardown      # delete OBS_IF if it exists
#   sudo tools/iface-up.sh --monitor       # additionally create MON_IF (monitor)
#   sudo tools/iface-up.sh --monitor-teardown
#   tools/iface-up.sh --help
#
# Env: ONAIR_IFACE (default wlan1), ONAIR_BASE_IF (default wlan0),
#      ONAIR_MON_IF (default mon-onair), ONAIR_PHY (default: derived from base)
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"

MODE=up
for arg in "$@"; do
	case "$arg" in
	--teardown) MODE=teardown ;;
	--monitor) MODE=monitor ;;
	--monitor-teardown) MODE=monitor-teardown ;;
	--help | -h)
		sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'
		exit 0
		;;
	*)
		err "unknown argument: $arg (see --help)"
		exit 2
		;;
	esac
done

PHASE=iface-up
LOG="$EVID/iface-up.log"
NM_CONF=/etc/NetworkManager/conf.d/99-wifi-onair-observer.conf

validate_observer_iface "$OBS_IF" || exit 2
validate_monitor_iface "$MON_IF" || exit 2
require_root || exit 2

PHY=${ONAIR_PHY:-}
if [ -z "$PHY" ]; then
	PHY=$(phy_of "$BASE_IF")
	[ -z "$PHY" ] && PHY=$(phy_of "$OBS_IF")
fi
if [ -z "$PHY" ] && [ "$MODE" = up ]; then
	err "cannot determine the Wi-Fi phy (base iface '$BASE_IF' missing?)"
	exit 1
fi

# --- NetworkManager: keep it away from OUR interfaces only -----------------
install_nm_unmanaged() {
	if [ "$DRY_RUN" = 1 ]; then
		log "[dry-run] would write $NM_CONF (unmanaged-devices=$OBS_IF;$MON_IF) and reload NetworkManager"
		return 0
	fi
	if [ -f "$NM_CONF" ] && grep -q "interface-name:$OBS_IF" "$NM_CONF" 2>/dev/null; then
		log "NM unmanaged conf already installed: $NM_CONF"
		return 0
	fi
	if ! mkdir -p "$(dirname "$NM_CONF")" 2>/dev/null; then
		err "cannot create $(dirname "$NM_CONF")"
		return 1
	fi
	{
		printf '# wifi-onair-observer lane: never manage the observer interfaces\n'
		printf '[keyfile]\n'
		printf 'unmanaged-devices=interface-name:%s;interface-name:%s\n' "$OBS_IF" "$MON_IF"
	} >"$NM_CONF"
	log "installed NM unmanaged conf: $NM_CONF"
	if have nmcli; then
		run nmcli general reload || warn "nmcli general reload failed; runtime 'device set managed no' will still be attempted"
	fi
	return 0
}

teardown_iface() { # teardown_iface <name>
	onair_name=$1
	if ! iface_exists "$onair_name"; then
		log "interface $onair_name already absent"
		return 0
	fi
	log "deleting interface $onair_name"
	run iw dev "$onair_name" del 2>/dev/null || run ip link del "$onair_name" || {
		err "failed to delete interface $onair_name"
		return 1
	}
	onair_i=0
	while [ "$onair_i" -lt 10 ] && iface_exists "$onair_name"; do
		sleep 1
		onair_i=$((onair_i + 1))
	done
	if iface_exists "$onair_name"; then
		err "interface $onair_name still exists after delete"
		return 1
	fi
	return 0
}

case "$MODE" in
teardown)
	section "L1 teardown observer interface"
	guard_snapshot
	teardown_iface "$OBS_IF" || { verdict iface-up HARNESS-ERROR "teardown of $OBS_IF failed"; exit 2; }
	guard_check "iface-up --teardown" || exit 2
	verdict iface-up SKIP "teardown only; $OBS_IF removed"
	exit 0
	;;
monitor-teardown)
	section "L1 teardown monitor interface"
	teardown_iface "$MON_IF" || { verdict iface-up HARNESS-ERROR "teardown of $MON_IF failed"; exit 2; }
	verdict iface-up SKIP "teardown only; $MON_IF removed"
	exit 0
	;;
monitor)
	section "L1b create monitor interface for optional beacon capture"
	[ -n "$PHY" ] || { err "phy unknown"; exit 1; }
	if iface_exists "$MON_IF"; then
		log "monitor iface $MON_IF already exists"
	else
		run iw phy "$PHY" interface add "$MON_IF" type monitor || {
			verdict iface-up SKIP "monitor iface $MON_IF could not be created (phy combination); beacon capture rung will be SKIPPED"
			exit 0
		}
		run ip link set "$MON_IF" up || true
	fi
	capture iw dev "$MON_IF" info || true
	verdict iface-up PASS "monitor iface $MON_IF ready (best effort; see RUNBOOK L2b)"
	exit 0
	;;
up)
	section "L1 create second managed observer interface"
	guard_snapshot
	install_nm_unmanaged || exit 2
	if iface_exists "$OBS_IF"; then
		warn "$OBS_IF already exists; reusing it (run tools/cleanup.sh for a clean slate)"
	else
		log "creating $OBS_IF on $PHY (base iface $BASE_IF stays untouched)"
		run iw phy "$PHY" interface add "$OBS_IF" type managed || {
			verdict iface-up FAIL "iw phy $PHY interface add $OBS_IF failed (wiphy combination/EBUSY?)"
			exit 1
		}
	fi
	if have nmcli; then
		run nmcli device set "$OBS_IF" managed no || warn "nmcli device set $OBS_IF managed no failed (NM may still manage it)"
	fi
	run ip link set "$OBS_IF" up || { verdict iface-up FAIL "could not bring $OBS_IF up"; exit 1; }
	onair_i=0
	while [ "$onair_i" -lt 10 ] && ! iface_exists "$OBS_IF"; do
		sleep 1
		onair_i=$((onair_i + 1))
	done
	capture iw dev "$OBS_IF" info >"$EVID/observer-iface-info.txt" || true
	if ! iface_exists "$OBS_IF"; then
		verdict iface-up FAIL "$OBS_IF did not appear as a netdev"
		exit 1
	fi
	log "observer iface MAC: $(iface_mac "$OBS_IF") (distinct from the base iface)"
	guard_check "iface-up" || exit 2
	verdict iface-up PASS "$OBS_IF created on $PHY, managed by wpa_supplicant only; base $BASE_IF untouched"
	exit 0
	;;
esac