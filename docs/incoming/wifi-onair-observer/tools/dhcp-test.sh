#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# dhcp-test.sh - L4 address acquisition and reachability over the observer
#               interface after a completed association.
#
# HOST-ONLY.  The host has no dhclient/dhcpcd/udhcpc/busybox, so the probe uses
# the bundled tools/dhcp_probe.py (raw AF_PACKET, root).  DHCP is meaningless
# until L3 association completes: without it this phase records BLOCKED.
#
# On this bench "no DHCP reply" is an expected class: the AP is hostapd started
# by hand and wlan0 may not be bridged into br-lan / dnsmasq may not serve the
# VAP.  No reply = BLOCKED (harness OK), with router-side hints printed for the
# Lead.  A lease that then fails ping is a data-path finding (FAIL).
#
# Usage:
#   sudo tools/dhcp-test.sh                       # DHCP + ping (router as target)
#   sudo tools/dhcp-test.sh --static 192.168.1.50/24 --ping-target 192.168.1.1
#   sudo tools/dhcp-test.sh --no-ping
#   tools/dhcp-test.sh --help
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"
PHASE=dhcp
LOG="$EVID/dhcp.log"

DHCP_TIMEOUT=${ONAIR_DHCP_TIMEOUT:-20}
STATIC_CIDR=
PING_TARGET=${ONAIR_PING_TARGET:-}
NO_PING=0
while [ $# -gt 0 ]; do
	arg=$1
	shift
	case "$arg" in
	--static)
		[ $# -gt 0 ] || { err "--static needs an addr/prefix"; exit 2; }
		STATIC_CIDR=$1
		shift
		;;
	--ping-target)
		[ $# -gt 0 ] || { err "--ping-target needs an address"; exit 2; }
		PING_TARGET=$1
		shift
		;;
	--no-ping) NO_PING=1 ;;
	--timeout)
		[ $# -gt 0 ] || { err "--timeout needs seconds"; exit 2; }
		DHCP_TIMEOUT=$1
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

iface_exists "$OBS_IF" || { err "observer iface $OBS_IF missing (run tools/iface-up.sh)"; exit 2; }
guard_check "dhcp" || exit 2
require_root || exit 2

section "L4 preconditions"
LINK=$(iw dev "$OBS_IF" link 2>&1 || true)
printf '%s\n' "$LINK" >"$EVID/dhcp-link.txt"
if ! printf '%s' "$LINK" | grep -q '^Connected to'; then
	warn "observer interface is not associated (L3 not completed)"
	verdict dhcp BLOCKED "no association on $OBS_IF - DHCP/ping require L3; expected while the AP mgmt path is fail-closed"
	exit 0
fi
log "observer associated: $(printf '%s' "$LINK" | head -n 1)"
capture ip -4 addr show dev "$OBS_IF" >"$EVID/dhcp-addr-before.txt" 2>&1 || true

DHCP_JSON="$EVID/dhcp-result.json"
LEASE_IP=
LEASE_PREFIX=
LEASE_ROUTER=

if [ -n "$STATIC_CIDR" ]; then
	section "L4 static address (DHCP explicitly not tested)"
	run ip addr add "$STATIC_CIDR" dev "$OBS_IF" || { verdict dhcp HARNESS-ERROR "ip addr add $STATIC_CIDR failed"; exit 2; }
	LEASE_IP=${STATIC_CIDR%%/*}
	LEASE_PREFIX=${STATIC_CIDR##*/}
	log "configured static $LEASE_IP/$LEASE_PREFIX on $OBS_IF (L4 DHCP rung SKIPPED)"
	verdict dhcp SKIP "static $LEASE_IP/$LEASE_PREFIX configured; DHCP not tested by request"
else
	section "L4 DHCP probe (bundled minimal client)"
	python3 "$TOOLS_DIR/dhcp_probe.py" --iface "$OBS_IF" --timeout "$DHCP_TIMEOUT" \
		--json-out "$DHCP_JSON"
	DHCP_RC=$?
	if [ "$DHCP_RC" -eq 2 ]; then
		verdict dhcp HARNESS-ERROR "dhcp_probe could not run (root/socket) - see $EVID/dhcp.log"
		exit 2
	fi
	if [ "$DHCP_RC" -ne 0 ]; then
		section "L4 DHCP diagnosis (no reply = BLOCKED, not a harness defect)"
		cat <<'EOF'
  No DHCP reply on the AP VAP. Likely causes, in order:
    1. wlan0 is not bridged into the router's LAN bridge (br-lan) - the AP is
       hostapd started by hand on top of the clean-room driver; check on the
       router console:  bridge link show | grep wlan0 ; ip -4 addr show br-lan
    2. no DHCP server is running for the VAP:  pgrep -a dnsmasq  on the router
    3. the VAP has no IP path at all (no br-lan attachment, no dnsmasq config):
       then DHCP cannot work regardless of the client.
  Everything above is router-side observation done by the Lead in the existing
  console session; this harness never contacts the router.
  Follow-up rungs: fix the router-side bridge/dnsmasq setup and re-run
  tools/dhcp-test.sh, or use --static <CIDR> --ping-target <AP LAN IP> to test
  the data path without DHCP.
EOF
		verdict dhcp BLOCKED "no DHCP reply (AP-side bridge/dnsmasq prerequisite missing); harness OK, see diagnosis in $LOG"
		exit 0
	fi
	LEASE_IP=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print((d.get("ack") or d.get("offer") or {}).get("yiaddr",""))' "$DHCP_JSON")
	LEASE_MASK=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print((d.get("ack") or {}).get("subnet_mask",""))' "$DHCP_JSON")
	LEASE_ROUTER=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print((d.get("ack") or d.get("offer") or {}).get("router",""))' "$DHCP_JSON")
	if [ -z "$LEASE_IP" ]; then
		verdict dhcp HARNESS-ERROR "DHCP probe reported a lease but no yiaddr was parsed"
		exit 2
	fi
	if [ -n "$LEASE_MASK" ]; then
		LEASE_PREFIX=$(python3 -c 'import ipaddress,sys; print(ipaddress.IPv4Network("0.0.0.0/"+sys.argv[1]).prefixlen)' "$LEASE_MASK")
	else
		LEASE_PREFIX=24
		warn "no subnet mask in the lease; assuming /$LEASE_PREFIX"
	fi
	run ip addr add "$LEASE_IP/$LEASE_PREFIX" dev "$OBS_IF" || { verdict dhcp HARNESS-ERROR "could not apply the lease to $OBS_IF"; exit 2; }
	log "DHCP lease applied: $LEASE_IP/$LEASE_PREFIX (router=${LEASE_ROUTER:-unknown})"
	verdict dhcp PASS "DHCP OFFER+ACK from the AP; $LEASE_IP/$LEASE_PREFIX configured on $OBS_IF"
fi

section "L4b reachability"
if [ "$NO_PING" = 1 ]; then
	verdict ping SKIP "ping explicitly disabled"
	exit 0
fi
if [ -z "$PING_TARGET" ] && [ -n "$LEASE_ROUTER" ]; then
	PING_TARGET=$LEASE_ROUTER
fi
if [ -z "$PING_TARGET" ]; then
	warn "no ping target: DHCP did not supply a router and ONAIR_PING_TARGET is unset"
	verdict ping SKIP "no target address (set ONAIR_PING_TARGET or --ping-target to the AP LAN IP the Lead reads on the router console)"
	exit 0
fi
log "ping target: $PING_TARGET (via $OBS_IF)"
if ! have ping; then
	verdict ping SKIP "ping binary missing on the host"
	exit 0
fi
capture_file "$EVID/ping.txt" ping -I "$OBS_IF" -c 3 -W 2 "$PING_TARGET"
PING_RC=$?
if [ "$PING_RC" -eq 0 ]; then
	verdict ping PASS "ICMP echo reply from $PING_TARGET over $OBS_IF (data path both ways)"
	exit 0
fi
verdict ping FAIL "no ICMP echo reply from $PING_TARGET over $OBS_IF - data-path finding (see $EVID/ping.txt)"
exit 1