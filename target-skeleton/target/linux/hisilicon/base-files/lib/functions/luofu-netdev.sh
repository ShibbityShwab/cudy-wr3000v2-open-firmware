# SPDX-License-Identifier: GPL-2.0-only
#
# luofu-netdev.sh - LAN/WAN interface map for the HiSilicon luofu R116
# (Cudy WR3000 v2.0) OpenWrt target.
#
# STATUS: PLACEHOLDER ACTIVATION, HOST-VALIDATED ONLY. The map is applied only
# when all four expected GEMAC netdevs exist; on a normal boot they do not (the
# wired path is attended-gated, patches 020/022/023), so this helper logs the
# intended map and configures nothing. It has NEVER run on a router. No device,
# UART, TFTP or flash tool is involved, and nothing is booted.
#
# WHAT IT MAPS, AND WHY THESE NAMES
#   The vendor userspace mapping for R116 is WAN = eth3, LAN = eth2 eth1 eth0
#   (parent:rootfs-2.4.15/etc/board.d/02_network:25-29) [V]. The netdevs are
#   the four live GEMAC netdevs registered by patch 023: gmac1..gmac4 in
#   devicetree order allocate eth0..eth3 [I]. Their LSW MAC-port numbers are
#   the DT "hisilicon,port" cells 9, 10, 11, 12 (LSW-SPEC.md section 6.1, from
#   the vendor devicetree; the live set is ports 9-12). So:
#
#     eth0 = gmac1 = LSW MAC port 9    LAN
#     eth1 = gmac2 = LSW MAC port 10   LAN
#     eth2 = gmac3 = LSW MAC port 11   LAN  (the bench cable port)
#     eth3 = gmac4 = LSW MAC port 12   WAN
#
#   LAN ports 4-12 semantics (LSW-SPEC.md sections 6.2 and 7.7): the internal
#   switch's populated qid-table rows cover ports 4-12 - ports 4-7 are the
#   internal PIE host/queue ports (not user jacks) and ports 8-12 are the MAC
#   ports, of which the board wires 9-12. The user-visible LAN/WAN split is
#   thus the MAC-port grouping above, and the vendor userspace (not hardware)
#   decides which MAC is LAN and which is WAN (LSW-SPEC.md 6.6: "the WAN/LAN
#   distinction is a userspace policy, not hardware").
#
#   MAC addresses are NOT this file's job: luofu-mac.sh records LAN/WAN
#   addresses from the factory bdinfo copy, and the kernel nvmem wiring
#   (patch 026) gives the same addresses to the netdevs. This map only names
#   the devices/ports those addresses belong to.
#
# WHEN IT RUNS
#   base-files/etc/board.d/02_network calls luofu_netdev_setup() while board.d
#   is processed (preinit / first boot). board.d runs once per image: if the
#   first boot has no netdevs, the map is absent until /etc/board.json is
#   regenerated. That is why the readiness check exists, and why an attended
#   boot that needs the map must regenerate board.json after the gated
#   netdevs appear (documented in the port-notes/mac-nvmem SUMMARY.md).
#
# TODO(netdevs): once the GEMAC netdevs register unconditionally (the PIE/LSW
#   data path no longer attended-gated), delete the readiness guard, make the
#   two ucidef_set_interface_* calls unconditional and drop this TODO. Until
#   then this file must never invent a netifd config for a device that does
#   not exist. Do not add per-jack / per-port offsets here: the vendor rule is
#   LAN = base MAC, WAN = base+1 (MISC-SOC-BLOCKS.md section 1.4), and there is
#   no per-port address rule in any vendor script.
#
# TEST HOOK
#   LUOFU_NET_TEST_ROOT (host tests only; unset on the device) prefixes the
#   /sys/class/net path, exactly like LUOFU_MAC_TEST_ROOT in luofu-mac.sh.

LUOFU_NET_TEST_ROOT="${LUOFU_NET_TEST_ROOT:-}"
LUOFU_NET_SYS_CLASS_NET="$LUOFU_NET_TEST_ROOT/sys/class/net"

# Vendor userspace mapping (parent:rootfs-2.4.15/etc/board.d/02_network:25-29).
# LAN is kept in the vendor's order (eth2 eth1 eth0) so board.json matches it.
LUOFU_NET_LAN_DEVS="eth2 eth1 eth0"
LUOFU_NET_WAN_DEV="eth3"

luofu_netdev_log() {
	echo "luofu-netdev: $*" >&2
}

luofu_netdev_present() { # <netdev name>
	[ -e "$LUOFU_NET_SYS_CLASS_NET/$1" ]
}

luofu_netdev_ready() { # 0 when every expected netdev exists
	local dev missing=""

	for dev in $LUOFU_NET_LAN_DEVS $LUOFU_NET_WAN_DEV; do
		if ! luofu_netdev_present "$dev"; then
			missing="$missing $dev"
		fi
	done
	if [ -n "$missing" ]; then
		LUOFU_NET_MISSING="${missing# }"
		return 1
	fi
	LUOFU_NET_MISSING=""
	return 0
}

luofu_netdev_apply() { # record the vendor LAN/WAN map in board.json
	if ! command -v ucidef_set_interface_wan >/dev/null 2>&1 ||
	   ! command -v ucidef_set_interface_lan >/dev/null 2>&1; then
		luofu_netdev_log "ucidef_set_interface_lan/wan is unavailable; not applying the LAN/WAN map"
		return 1
	fi
	ucidef_set_interface_wan "$LUOFU_NET_WAN_DEV"
	ucidef_set_interface_lan "$LUOFU_NET_LAN_DEVS"
	luofu_netdev_log "applied LAN ($LUOFU_NET_LAN_DEVS) / WAN ($LUOFU_NET_WAN_DEV) to board.json (LSW MAC ports 9-11 / 12)"
	return 0
}

luofu_netdev_setup() { # 0 = map applied, 1 = placeholder (netdevs absent)
	LUOFU_NET_MISSING=""

	if ! luofu_netdev_ready; then
		luofu_netdev_log "placeholder LAN/WAN map (TODO(netdevs)): expected netdevs eth0..eth3 are absent (missing: $LUOFU_NET_MISSING); the GEMAC netdevs only register on an attended boot with luofu_pie.arm=1 and both luofu_lsw gates (patch 023), so no netifd interface is configured"
		return 1
	fi

	luofu_netdev_apply || return 1
	return 0
}
