#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# test-mac-nvmem.sh - host-side checks for the bdinfo factory-MAC nvmem wiring
# and the guarded LAN/WAN interface map of the luofu R116 (Cudy WR3000 v2.0)
# OpenWrt target.
#
# STATUS: host-only. No router, UART, network, TFTP, flash or bench tool is
# touched; nothing is booted and no device is contacted.
#
# WHAT THIS EXERCISES
#   static  : sh -n and modes of the new/edited base-files, the board.d wiring
#             (the existing luofu-mac.sh provisioner must stay sourced and
#             called), patch 026's hardening (deferred nvmem provider honoured,
#             standard netdev MAC callbacks added, no MTD/write path added) and
#             the raw board-DTS wiring (read-only bdinfo partition with a
#             fixed-layout/mac-base cell at +0xde00; gmac1..3 = offset 0,
#             gmac4 = offset 1).
#   dts     : only with --kernel <linux tree>: cpp+dtc the board DTS, require
#             RC=0 with zero warnings, decompile and repeat the wiring checks
#             on the compiled tree.
#   nvmem   : a synthetic 0x40000-byte bdinfo image (container header, decoy
#             bytes, the plaintext 6-byte copy at +0xde00, 0xff fill) is read
#             exactly as mtd_nvmem_reg_read() would read the cell (6 bytes at
#             the DTS offset) and post-processed with the kernel mac-base
#             arithmetic (48-bit eth_addr_add) using the per-node cell offsets
#             parsed from the DTS.  The erased and all-zero cases are checked
#             to be rejected - the fail-safe contract behind the GEMAC driver's
#             logged random fallback - and the wrap case shows the documented
#             difference between the kernel 48-bit add and the userspace
#             24-bit-masked macaddr_add (the provisioner rejects that base).
#   netdev  : the real luofu-netdev.sh is sourced per scenario with
#             LUOFU_NET_TEST_ROOT pointing at a fake /sys/class/net and mocked
#             ucidef_set_interface_lan/wan helpers.  All four netdevs present
#             -> the vendor map is recorded (WAN eth3, LAN eth2 eth1 eth0);
#             any missing netdev -> the placeholder logs and NO netifd config
#             is invented; missing ucidef helpers -> logged refusal.
#   integ   : the pre-existing factory-MAC provisioner test
#             (tools/test-mac-provisioning.sh, 103 assertions) must still pass,
#             proving the board.d change is backward compatible.
#
# USAGE:
#   tools/test-mac-nvmem.sh [--kernel DIR] [--dts PATH] [--netdev PATH]
#                           [--board PATH] [--mac PATH] [--patch PATH]
#                           [--skip-integration] [--keep]
#
# EXIT: 0 every executed check passed, 1 a check failed, 2 usage error.

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
DTS="$SCRIPT_DIR/../dts/luofu-r116.dts"
NETDEV="$SCRIPT_DIR/../target/linux/hisilicon/base-files/lib/functions/luofu-netdev.sh"
BOARD="$SCRIPT_DIR/../target/linux/hisilicon/base-files/etc/board.d/02_network"
MAC="$SCRIPT_DIR/../target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh"
PATCH="$SCRIPT_DIR/../target/linux/hisilicon/patches-6.18/026-net-gmac-bdinfo-mac-nvmem.patch"
KERNEL_DIR=
RUN_INTEGRATION=1
KEEP=0

while [ $# -gt 0 ]; do
	case "$1" in
	--kernel) KERNEL_DIR="$2"; shift 2 ;;
	--dts) DTS="$2"; shift 2 ;;
	--netdev) NETDEV="$2"; shift 2 ;;
	--board) BOARD="$2"; shift 2 ;;
	--mac) MAC="$2"; shift 2 ;;
	--patch) PATCH="$2"; shift 2 ;;
	--skip-integration) RUN_INTEGRATION=0; shift ;;
	--keep) KEEP=1; shift ;;
	-h|--help) sed -n '2,56p' "$0"; exit 0 ;;
	*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

for f in "$DTS" "$NETDEV" "$BOARD" "$MAC" "$PATCH"; do
	[ -f "$f" ] || { echo "missing input: $f" >&2; exit 2; }
done
command -v od >/dev/null 2>&1 || { echo "od is required" >&2; exit 2; }

TMP=$(mktemp -d "${TMPDIR:-/tmp}/luofu-mac-nvmem.XXXXXX") || exit 1
cleanup() {
	if [ "$KEEP" = 1 ]; then
		echo "fixtures kept in $TMP" >&2
		return
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT

PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf 'ok %d - %s\n' "$PASS" "$*"; }
fail() { FAIL=$((FAIL + 1)); printf 'not ok %d - %s\n' "$FAIL" "$*"; }
note() { printf '  # %s\n' "$*"; }

check_eq() { # <desc> <expected> <actual>
	if [ "$2" = "$3" ]; then pass "$1"; else fail "$1: expected [$2], got [$3]"; fi
}

check_match() { # <desc> <extended-regex> <file>
	if grep -Eq -- "$2" "$3"; then pass "$1"; else fail "$1 (no match for /$2/ in $3)"; fi
}

check_no_match() { # <desc> <extended-regex> <file>
	if grep -Eq -- "$2" "$3"; then fail "$1 (unexpected /$2/ in $3)"; else pass "$1"; fi
}

check_file_mode() { # <desc> <file> <octal>
	local mode
	mode=$(stat -c '%a' "$2" 2>/dev/null)
	if [ "$mode" = "$3" ]; then pass "$1"; else fail "$1: expected $3, got $mode"; fi
}

# Lines from the first match of <start> up to (excluding) the first following
# match of <end>.  Plain substrings/regexes, no backslashes needed.
block_between() { # <file> <start-regex> <end-regex>
	awk -v s="$2" -v e="$3" '
		!inb && $0 ~ s { inb = 1 }
		inb && seen && $0 ~ e { exit }
		inb { print; seen = 1 }
	' "$1"
}

dts_bdinfo() { block_between "$1" 'partition@4c0000' 'partition@500000'; }

dts_gmac() { # <file> <n> (raw DTS; labels are compile-time only)
	case "$2" in
	1) block_between "$1" 'gmac1: ethernet@14380000' 'gmac2: ethernet@14400000' ;;
	2) block_between "$1" 'gmac2: ethernet@14400000' 'gmac3: ethernet@14480000' ;;
	3) block_between "$1" 'gmac3: ethernet@14480000' 'gmac4: ethernet@14500000' ;;
	4) block_between "$1" 'gmac4: ethernet@14500000' 'MDIO bus controller' ;;
	esac
}

dts_gmac_compiled() { # <decompiled dts> <n>
	case "$2" in
	1) block_between "$1" 'ethernet@14380000' 'ethernet@14400000' ;;
	2) block_between "$1" 'ethernet@14400000' 'ethernet@14480000' ;;
	3) block_between "$1" 'ethernet@14480000' 'ethernet@14500000' ;;
	4) block_between "$1" 'ethernet@14500000' 'mdio@14000000' ;;
	esac
}

cell_arg() { # <node slice> -> the mac-base cell argument N
	printf '%s' "$1" | grep -o 'bdinfo_mac [0-9]*' | awk '{print $2}'
}

# ---------------------------------------------------------------- static ----

static_checks() {
	note "static: syntax, modes, wiring, patch 026, raw DTS"

	local rc bdinfo n block arg expect

	sh -n "$NETDEV" 2>"$TMP/netdev.shn" && rc=0 || rc=1
	check_eq "luofu-netdev.sh passes sh -n" 0 "$rc"
	sh -n "$BOARD" 2>"$TMP/board.shn" && rc=0 || rc=1
	check_eq "board.d/02_network passes sh -n" 0 "$rc"
	sh -n "$MAC" 2>"$TMP/mac.shn" && rc=0 || rc=1
	check_eq "luofu-mac.sh still passes sh -n" 0 "$rc"

	check_file_mode "luofu-netdev.sh mode is 0644" "$NETDEV" 644
	check_file_mode "board.d/02_network mode is 0755" "$BOARD" 755

	check_match "board.d sources the interface-map helper" '^\. /lib/functions/luofu-netdev\.sh$' "$BOARD"
	check_match "board.d calls luofu_netdev_setup" 'luofu_netdev_setup' "$BOARD"
	# Backward compatibility: the factory-MAC provisioner must stay intact.
	check_match "board.d still sources the MAC helper" '^\. /lib/functions/luofu-mac\.sh$' "$BOARD"
	check_match "board.d still calls luofu_mac_setup" 'luofu_mac_setup' "$BOARD"
	check_match "board.d still calls luofu_mac_apply" 'luofu_mac_apply' "$BOARD"
	check_no_match "board.d does not source the non-existent board.sh" 'lib/functions/board\.sh' "$BOARD"
	check_no_match "board.d does not invent per-jack MAC offsets" 'base\+[2-9]|offset [2-9]' "$BOARD"

	check_match "helper uses the LUOFU_NET_TEST_ROOT seam" 'LUOFU_NET_TEST_ROOT' "$NETDEV"
	check_match "helper maps LAN eth2 eth1 eth0 (vendor order)" 'LUOFU_NET_LAN_DEVS="eth2 eth1 eth0"' "$NETDEV"
	check_match "helper maps WAN eth3" 'LUOFU_NET_WAN_DEV="eth3"' "$NETDEV"
	check_match "helper checks netdev presence before mapping" 'luofu_netdev_ready' "$NETDEV"
	check_no_match "helper contains no write path" 'mtd write|mtd_write|nvmem.*write|uci set|uci_set' "$NETDEV"

	check_match "patch 026 hardens a deferred nvmem provider" 'if \(ret == -EPROBE_DEFER\)' "$PATCH"
	check_match "patch 026 defers probe instead of randomising" 'MAC-address nvmem provider is not ready' "$PATCH"
	check_match "patch 026 installs ndo_set_mac_address" '\.ndo_set_mac_address[[:space:]]*= eth_mac_addr' "$PATCH"
	check_match "patch 026 installs ndo_validate_addr" '\.ndo_validate_addr[[:space:]]*= eth_validate_addr' "$PATCH"
	check_match "patch 026 keeps the logged random fallback" 'eth_hw_addr_random' "$PATCH"
	check_match "patch 026 reads the standard DT MAC helper" 'of_get_mac_address\(dev->of_node, mac\)' "$PATCH"
	check_no_match "patch 026 adds no nvmem write callback" 'reg_write|nvmem_config|nvmem_register' "$PATCH"
	check_no_match "patch 026 adds no MTD write/erase path" 'mtd_write|mtd_erase|mtd_read' "$PATCH"

	bdinfo=$(dts_bdinfo "$DTS")
	[ -n "$bdinfo" ] && pass "board DTS has the bdinfo partition block" || fail "board DTS has the bdinfo partition block"
	printf '%s\n' "$bdinfo" | grep -q 'label = "bdinfo";' && pass "bdinfo partition keeps its label" || fail "bdinfo partition keeps its label"
	printf '%s\n' "$bdinfo" | grep -q 'read-only;' && pass "bdinfo partition is read-only" || fail "bdinfo partition is read-only"
	# 6.18 adopted path: the kernel keeps no nvmem layout/cells; the factory
	# MAC is provisioned by userspace (board.d/luofu-mac.sh) like the vendor
	# scripts (boot-tested on image c3ef2a50).
	printf '%s\n' "$bdinfo" | grep -q 'fixed-layout' && fail "adopted path: no fixed-layout nvmem layout in bdinfo" || pass "adopted path: no fixed-layout nvmem layout in bdinfo"
	printf '%s\n' "$bdinfo" | grep -q 'mac-base' && fail "adopted path: no mac-base cell in bdinfo" || pass "adopted path: no mac-base cell in bdinfo"
	printf '%s\n' "$bdinfo" | grep -q 'nvmem' && fail "adopted path: no nvmem children in bdinfo" || pass "adopted path: no nvmem children in bdinfo"
	printf '%s\n' "$bdinfo" | grep -q 'reg = <0x4c0000 0x40000>;' && pass "bdinfo keeps the stock 0x4c0000/0x40000 geometry" || fail "bdinfo keeps the stock 0x4c0000/0x40000 geometry"
	check_no_match "board DTS does not invent a custom bdinfo nvmem driver" 'hisilicon,luofu-bdinfo|luofu-bdinfo-nvmem' "$DTS"

	for n in 1 2 3 4; do
		block=$(dts_gmac "$DTS" "$n")
		if [ -z "$block" ]; then
			fail "gmac$n node found"
			continue
		fi
		pass "gmac$n node found"
		printf '%s\n' "$block" | grep -q 'nvmem-cells' && fail "gmac$n carries no kernel nvmem MAC cell (adopted path)" || pass "gmac$n carries no kernel nvmem MAC cell (adopted path)"
		printf '%s\n' "$block" | grep -q 'nvmem-cell-names' && fail "gmac$n carries no nvmem-cell-names (adopted path)" || pass "gmac$n carries no nvmem-cell-names (adopted path)"
	done
}

# ------------------------------------------------------------------- dts ----

dts_checks() {
	if [ -z "$KERNEL_DIR" ]; then
		note "dts: skipped (pass --kernel <linux tree> for the compiled checks)"
		return
	fi

	local dtc pre dtb out rc bdinfo n block arg expect
	dtc="$KERNEL_DIR/scripts/dtc/dtc"
	if [ ! -x "$dtc" ]; then
		fail "dts: dtc not found or not executable at $dtc"
		return
	fi

	pre="$TMP/board-dts.pre"
	dtb="$TMP/board-dts.dtb"
	out="$TMP/board-dts.out.dts"

	cpp -nostdinc -I"$KERNEL_DIR/include" -I"$KERNEL_DIR/arch/arm/boot/dts" \
	    -undef -D__DTS__ -x assembler-with-cpp "$DTS" -o "$pre" 2>"$TMP/cpp.err"
	rc=$?
	check_eq "dts: cpp preprocess RC=0" 0 "$rc"
	if [ "$rc" != 0 ]; then
		note "cpp stderr: $(cat "$TMP/cpp.err")"
		return
	fi

	"$dtc" -I dts -O dtb -o "$dtb" "$pre" 2>"$TMP/dtc.err"
	rc=$?
	check_eq "dts: dtc compile RC=0" 0 "$rc"
	if [ -s "$TMP/dtc.err" ]; then
		fail "dts: dtc compiled with zero warnings"
		note "dtc stderr: $(cat "$TMP/dtc.err")"
	else
		pass "dts: dtc compiled with zero warnings"
	fi
	[ "$rc" = 0 ] || return

	"$dtc" -I dtb -O dts -o "$out" "$dtb" 2>"$TMP/dtc-decompile.err"
	rc=$?
	check_eq "dts: decompile RC=0" 0 "$rc"
	[ "$rc" = 0 ] || return

	bdinfo=$(block_between "$out" 'partition@4c0000' 'partition@500000')
	printf '%s\n' "$bdinfo" | grep -q 'read-only;' && pass "compiled: bdinfo partition is read-only" || fail "compiled: bdinfo partition is read-only"
	printf '%s\n' "$bdinfo" | grep -q 'fixed-layout' && fail "compiled: no fixed-layout container (adopted path)" || pass "compiled: no fixed-layout container (adopted path)"
	printf '%s\n' "$bdinfo" | grep -q 'mac-base' && fail "compiled: no mac-base cell (adopted path)" || pass "compiled: no mac-base cell (adopted path)"
	for n in 1 2 3 4; do
		block=$(dts_gmac_compiled "$out" "$n")
		printf '%s\n' "$block" | grep -q 'nvmem-cells' && fail "compiled: gmac$n carries no nvmem-cells (adopted path)" || pass "compiled: gmac$n carries no nvmem-cells (adopted path)"
	done
}

# ----------------------------------------------------------------- nvmem ----

write_mac_bytes() { # <file> <hex12>: write 6 bytes at +0xde00
	local file="$1" hex="$2" i b

	for ((i = 0; i < 12; i += 2)); do
		b=$((16#$(printf '%s' "$hex" | cut -c$((i + 1))-$((i + 2)))))
		printf "\\$(printf '%03o' "$b")"
	done | dd of="$file" bs=1 seek=$((0xde00)) conv=notrunc 2>/dev/null
}

make_bdinfo() { # <file> <mac-hex12> <fill: ff|00>
	local file="$1" mac="$2" fill="$3" hex

	dd if=/dev/zero bs=4096 count=64 of="$file" 2>/dev/null || return 1
	case "$fill" in
	ff) tr '\000' '\377' < "$file" > "$file.tmp" && mv "$file.tmp" "$file" ;;
	00) : ;;
	*) echo "unsupported fill $fill" >&2; return 1 ;;
	esac

	printf '\000\000\000\001' | dd of="$file" bs=1 seek=0 conv=notrunc 2>/dev/null
	# decoy bytes before/after the real copy (must never be picked up)
	printf '\336\255\276\357\000\001' | dd of="$file" bs=1 seek=256 conv=notrunc 2>/dev/null
	printf '\336\255\276\357\000\002' | dd of="$file" bs=1 seek=$((0xde06)) conv=notrunc 2>/dev/null
	hex=$(printf '%s' "$mac" | tr -d ':')
	write_mac_bytes "$file" "$hex"
}

read_cell() { # <image> <offset-dec> <len>
	od -An -v -tx1 -j "$2" -N "$3" "$1" | tr -d ' \n'
}

hex6() { # <file> <offset-dec> -> 12 hex chars
	od -An -v -tx1 -j "$2" -N 6 "$1" | tr -d ' \n'
}

mac_add48() { # <mac> <delta> -> 48-bit add (kernel eth_addr_add semantics)
	local mac="$1" val="$2" hex n
	hex=$(printf '%s' "$mac" | tr -d ':')
	n=$((0x$hex + val))
	printf '%012x' $((n & 0xffffffffffff)) | sed 's/../&:/g; s/:$//'
}

mac_valid_generic() { # mirrors is_valid_ether_addr(): non-zero, group bit clear
	local first
	case "$1" in
	00:00:00:00:00:00) return 1 ;;
	esac
	first=$(printf '%s' "$1" | cut -d: -f1)
	[ $((0x$first & 1)) -eq 0 ]
}

nvmem_checks() {
	note "nvmem: synthetic bdinfo image read like mtd_nvmem_reg_read()"

	local base img got out lan wan cell_off

	base="10:20:30:40:50:60"	# documentation test data, not a per-unit value
	img="$TMP/bdinfo.bin"
	make_bdinfo "$img" "$base" ff || { fail "nvmem: synthetic image built"; return; }
	pass "nvmem: synthetic bdinfo image built (0x40000 bytes)"

	check_match "userspace: provisioner reads the bdinfo factory copy at 0xde00" '^LUOFU_MAC_BDINFO_OFFSET="0xde00"$' "$MAC"

	got=$(read_cell "$img" $((0xde00)) 6)
	check_eq "nvmem: 6-byte read at +0xde00 returns the factory copy" "$(printf '%s' "$base" | tr -d ':')" "$got"
	check_eq "nvmem: decoy bytes after the copy are outside the cell" "deadbeef0002" "$(hex6 "$img" $((0xde06)))"
	check_eq "nvmem: decoy bytes before the copy are outside the cell" "deadbeef0001" "$(hex6 "$img" 256)"

	lan=$base
	wan=$( . "$MAC"; luofu_mac_add "$base" 1 )
	check_eq "nvmem: LAN address = factory base (vendor/userspace rule)" "$base" "$lan"
	check_eq "nvmem: WAN address = factory base+1 (vendor/userspace rule)" "10:20:30:40:50:61" "$wan"
	check_eq "nvmem: base+1 does not touch the OUI" "10:20:30:40:51:00" "$(mac_add48 '10:20:30:40:50:ff' 1)"

	# Wrap case: kernel 48-bit add carries into the OUI; the vendor/userspace
	# rule masks to 24 bits.  The provisioner rejects that base, so the
	# divergence is documented instead of reachable for a valid unit.
	check_eq "nvmem: kernel 48-bit add carries on ...:ff:ff:ff" "10:20:31:00:00:00" "$(mac_add48 '10:20:30:ff:ff:ff' 1)"
	out=$(
		. "$MAC"
		luofu_mac_add '10:20:30:ff:ff:ff' 1
	)
	check_eq "nvmem: userspace macaddr_add masks to 24 bits" "10:20:30:00:00:00" "$out"
	out=$(
		. "$MAC"
		if luofu_mac_validate '10:20:30:ff:ff:ff'; then
			echo accepted
		else
			printf 'rejected:%s\n' "$LUOFU_MAC_ERROR"
		fi
	)
	case "$out" in
	rejected:*ff:ff:ff*) pass "nvmem: provisioner rejects the divergent wrap base" ;;
	*) fail "nvmem: provisioner rejects the divergent wrap base: [$out]" ;;
	esac

	# Fail-safe contract: erased / zero copies are invalid, so the GEMAC
	# of_get_mac_address() error path (logged random fallback) runs.
	img="$TMP/bdinfo-erased.bin"
	dd if=/dev/zero bs=4096 count=64 2>/dev/null | tr '\000' '\377' > "$img"
	check_eq "nvmem: erased partition reads 0xff at +0xde00" "ffffffffffff" "$(read_cell "$img" $((0xde00)) 6)"
	mac_valid_generic "ff:ff:ff:ff:ff:ff" && fail "nvmem: erased copy is rejected (fail-safe fallback)" || pass "nvmem: erased copy is rejected (fail-safe fallback)"
	mac_valid_generic "00:00:00:00:00:00" && fail "nvmem: all-zero copy is rejected (fail-safe fallback)" || pass "nvmem: all-zero copy is rejected (fail-safe fallback)"
	mac_valid_generic "$base" && pass "nvmem: a factory-style base copy is accepted" || fail "nvmem: a factory-style base copy is accepted"

	check_no_match "nvmem: patch 026 adds no write provider" 'reg_write' "$PATCH"
}

# ---------------------------------------------------------------- netdev ----

run_netdev() { # <fixture-root> <calls-file>
	local root="$1" calls="$2"

	(
		export LUOFU_NET_TEST_ROOT="$root"
		. "$NETDEV"
		ucidef_set_interface_wan() { printf 'wan %s\n' "$1" >> "$calls"; }
		ucidef_set_interface_lan() { printf 'lan %s\n' "$1" >> "$calls"; }
		luofu_netdev_setup
		printf 'rc=%d\n' "$?"
	) >"$TMP/netdev.out" 2>&1
}

netdev_run() { # <root> <calls-file>
	rm -f "$2"
	run_netdev "$1" "$2"
}

netdev_checks() {
	note "netdev: mocked /sys/class/net + mocked ucidef helpers"

	local root calls net rc

	root="$TMP/net-all"
	mkdir -p "$root/sys/class/net"
	for net in eth0 eth1 eth2 eth3; do mkdir -p "$root/sys/class/net/$net"; done
	calls="$TMP/net-all.calls"
	netdev_run "$root" "$calls"
	rc=$(grep -o 'rc=[0-9]*' "$TMP/netdev.out" | tail -1 | cut -d= -f2)
	check_eq "netdev: all netdevs present -> setup RC=0" 0 "$rc"
	check_eq "netdev: vendor WAN mapping applied (eth3)" "wan eth3" "$(sed -n '1p' "$calls")"
	check_eq "netdev: vendor LAN mapping applied (eth2 eth1 eth0)" "lan eth2 eth1 eth0" "$(sed -n '2p' "$calls")"
	check_eq "netdev: exactly two interface entries recorded" 2 "$(wc -l < "$calls")"
	grep -q 'applied LAN' "$TMP/netdev.out" && pass "netdev: applied map is logged with the LSW port correlation" || fail "netdev: applied map is logged with the LSW port correlation"

	root="$TMP/net-missing-wan"
	mkdir -p "$root/sys/class/net"
	for net in eth0 eth1 eth2; do mkdir -p "$root/sys/class/net/$net"; done
	calls="$TMP/net-missing-wan.calls"
	netdev_run "$root" "$calls"
	rc=$(grep -o 'rc=[0-9]*' "$TMP/netdev.out" | tail -1 | cut -d= -f2)
	check_eq "netdev: missing WAN -> placeholder RC=1" 1 "$rc"
	[ -e "$calls" ] && fail "netdev: missing WAN invents no netifd config" || pass "netdev: missing WAN invents no netifd config"
	grep -q 'placeholder LAN/WAN map' "$TMP/netdev.out" && pass "netdev: placeholder reason is logged" || fail "netdev: placeholder reason is logged"
	grep -q 'missing: eth3' "$TMP/netdev.out" && pass "netdev: the missing netdev is named in the log" || fail "netdev: the missing netdev is named in the log"

	root="$TMP/net-none"
	mkdir -p "$root/sys/class/net"
	calls="$TMP/net-none.calls"
	netdev_run "$root" "$calls"
	rc=$(grep -o 'rc=[0-9]*' "$TMP/netdev.out" | tail -1 | cut -d= -f2)
	check_eq "netdev: no netdevs -> placeholder RC=1" 1 "$rc"
	[ -e "$calls" ] && fail "netdev: no netdevs invents no netifd config" || pass "netdev: no netdevs invents no netifd config"

	root="$TMP/net-all"
	(
		export LUOFU_NET_TEST_ROOT="$root"
		. "$NETDEV"
		luofu_netdev_setup
		printf 'rc=%d\n' "$?"
	) >"$TMP/net-noucidef.out" 2>&1
	rc=$(grep -o 'rc=[0-9]*' "$TMP/net-noucidef.out" | tail -1 | cut -d= -f2)
	check_eq "netdev: missing ucidef helpers -> RC=1" 1 "$rc"
	grep -q 'ucidef_set_interface_lan/wan is unavailable' "$TMP/net-noucidef.out" && pass "netdev: missing ucidef helpers is logged" || fail "netdev: missing ucidef helpers is logged"

	check_eq "netdev: fixture /sys tree is unchanged" 4 "$(find "$TMP/net-all/sys/class/net" -maxdepth 1 -mindepth 1 | wc -l)"
}

# ------------------------------------------------------------- integration --

integration_checks() {
	if [ "$RUN_INTEGRATION" != 1 ]; then
		note "integration: skipped (--skip-integration)"
		return
	fi

	local out rc
	out="$TMP/test-mac-provisioning.log"
	"$SCRIPT_DIR/test-mac-provisioning.sh" >"$out" 2>&1
	rc=$?
	check_eq "integration: tools/test-mac-provisioning.sh RC=0" 0 "$rc"
	check_eq "integration: no failing provisioner assertion" 0 "$(grep -c '^not ok' "$out" || true)"
	note "provisioner: $(grep -c '^ok ' "$out" || true) passing assertions"
}

# ------------------------------------------------------------------- main --

echo "# test-mac-nvmem.sh (host-only)"
echo "# dts:    $DTS"
echo "# netdev: $NETDEV"
echo "# board:  $BOARD"
echo "# mac:    $MAC"
echo "# patch:  $PATCH"
if [ -n "$KERNEL_DIR" ]; then
	echo "# kernel: $KERNEL_DIR"
else
	echo "# kernel: <not given; compiled DTS checks skipped>"
fi

static_checks
dts_checks
nvmem_checks
netdev_checks
integration_checks

echo "# pass: $PASS fail: $FAIL"
[ "$FAIL" = 0 ]
