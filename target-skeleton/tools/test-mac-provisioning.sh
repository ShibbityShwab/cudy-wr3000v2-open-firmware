#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# test-mac-provisioning.sh - host-side mocked tests for the luofu factory-MAC
# provisioning path.
#
# STATUS: host-only. No router, UART, TFTP, flash or bench tool is touched;
# nothing is booted and no device is contacted.
#
# WHAT THIS EXERCISES
#   The real target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh is
#   sourced in a fresh shell per scenario with LUOFU_MAC_TEST_ROOT pointing at a
#   fixture that fakes everything the helper reads:
#
#     <fixture>/proc/mtd          fake MTD table (stock 17-slot layout, or
#                                 variants: no bdinfo label, small partition)
#     <fixture>/dev/mtdN          fake mtd character device (regular file; the
#                                 6-byte MAC copy is placed at 0xde00)
#     mtd_get_mac_binary          shell-function mocks define which read path is
#                                 taken (standard helper, empty, or garbage)
#     _luofu_mac_urandom_hex      override for deterministic random-fallback
#                                 assertions, or to force a failure
#     ucidef_set_interface_macaddr mock that logs the applied lan/wan values,
#                                 proving the addresses go through the normal
#                                 board.d/network entry point
#
#   The real base-files/etc/board.d/02_network is checked statically (it sources
#   the helper and calls luofu_mac_setup/luofu_mac_apply, it does not source the
#   non-existent /lib/functions/board.sh, and it is executable).
#
# SCENARIOS
#   static   : sh -n, file modes, source wiring, no factory-partition writes
#   canon    : MAC normalization (colon/dash/dot/12-hex/upper case/invalid)
#   valid    : unicast+universal accepted; multicast, LAA, zero, erased, OUI
#              00:00:00 and the WAN-wrap case rejected with a reason
#   add      : LAN+1 derivation matches the vendor/OpenWrt macaddr_add rule in
#              both implementations (system.sh mock and POSIX fallback), incl.
#              the ...:ff:ff:ff -> ...:00:00:00 wrap and OUI preservation
#   read     : mtd_get_mac_binary path; direct /dev/mtdN fallback; label-based
#              (not index-based) partition lookup; exact 0xde00 offset;
#              missing table/label/device/short read; garbage from the helper
#   corrupt  : each invalid 6-byte factory value falls back to random + a log
#   offset   : decoy MAC bytes outside 0xde00 are never picked up
#   random   : locally-administered/unicast bit handling and the WAN relation;
#              a failed random source leaves the MACs unset
#   apply    : ucidef_set_interface_macaddr gets lan then wan, exact values;
#              nothing applied when setup failed; no write to the fixture
#
# EXIT: 0 every scenario passed, 1 at least one failed.
#
# USAGE:
#   tools/test-mac-provisioning.sh [--helper PATH] [--board PATH] [--keep]
#
#   --helper  alternate luofu-mac.sh (default: the repository one)
#   --board   alternate board.d/02_network (default: the repository one)
#   --keep    keep the fixture directory for inspection

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
HELPER="$SCRIPT_DIR/../target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh"
BOARD="$SCRIPT_DIR/../target/linux/hisilicon/base-files/etc/board.d/02_network"
keep=0

while [ $# -gt 0 ]; do
	case "$1" in
	--helper) HELPER="$2"; shift 2 ;;
	--board) BOARD="$2"; shift 2 ;;
	--keep) keep=1; shift ;;
	-h|--help) sed -n '2,64p' "$0"; exit 0 ;;
	*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
done

[ -f "$HELPER" ] || { echo "helper not found: $HELPER" >&2; exit 2; }
[ -f "$BOARD" ] || { echo "board.d/02_network not found: $BOARD" >&2; exit 2; }
command -v hexdump >/dev/null 2>&1 || {
	echo "hexdump is required for the mocked direct-read path" >&2
	exit 2
}

SH=${SH:-sh}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/luofu-mac-test.XXXXXX") || exit 1
cleanup() {
	if [ "$keep" = 1 ]; then
		echo "fixtures kept in $TMP" >&2
		return
	fi
	rm -rf "$TMP"
}
trap cleanup EXIT

PASS=0
FAIL=0
CASE=""

pass() { PASS=$((PASS + 1)); printf 'ok %d - %s\n' "$PASS" "$*"; }
fail() { FAIL=$((FAIL + 1)); printf 'not ok %d - %s\n' "$FAIL" "$*"; }
note() { printf '  # %s\n' "$*"; }

check_eq() { # <description> <expected> <actual>
	if [ "$2" = "$3" ]; then
		pass "$1"
	else
		fail "$1: expected [$2], got [$3]"
	fi
}

check_ne() { # <description> <unexpected> <actual>
	if [ "$2" != "$3" ]; then
		pass "$1"
	else
		fail "$1: unexpected [$2]"
	fi
}

check_line() { # <description> <exact line> <file>
	if grep -Fxq -- "$2" "$3"; then
		pass "$1"
	else
		fail "$1: line [$2] not found in $3"
		note "file content:"
		sed -n '1,20p' "$3" | sed 's/^/    /'
	fi
}

check_match() { # <description> <ERE> <file>
	if grep -Eq -- "$2" "$3"; then
		pass "$1"
	else
		fail "$1: pattern [$2] not found in $3"
		sed -n '1,20p' "$3" | sed 's/^/    /'
	fi
}

check_no_match() { # <description> <ERE> <file>
	if grep -Eq -- "$2" "$3"; then
		fail "$1: pattern [$2] unexpectedly found in $3"
	else
		pass "$1"
	fi
}

check_file_mode() { # <description> <path> <octal mode>
	local mode
	mode=$(stat -c '%a' "$2" 2>/dev/null)
	check_eq "$1" "$3" "$mode"
}

new_case() { # <name>
	CASE="$TMP/$1"
	rm -rf "$CASE"
	mkdir -p "$CASE/proc" "$CASE/dev"
	: > "$CASE/out.log"
	: > "$CASE/err.log"
}

write_stock_mtd() { # stock 17-slot partition table (bdinfo = mtd6)
	cat > "$CASE/proc/mtd" <<'MTD'
dev:    size   erasesize  name
mtd0: 00040000 00020000 "esbc"
mtd1: 00100000 00020000 "uboota"
mtd2: 00100000 00020000 "ubootb"
mtd3: 00040000 00020000 "enva"
mtd4: 00040000 00020000 "envb"
mtd5: 00200000 00020000 "fac"
mtd6: 00040000 00020000 "bdinfo"
mtd7: 00200000 00020000 "cfga"
mtd8: 00200000 00020000 "cfgb"
mtd9: 00440000 00020000 "log"
mtd10: 00040000 00020000 "pstore"
mtd11: 00840000 00020000 "kernela"
mtd12: 00840000 00020000 "kernelb"
mtd13: 01740000 00020000 "rootfsa"
mtd14: 01740000 00020000 "rootfsb"
mtd15: 01600000 00020000 "rootfs_data"
mtd16: 01d80000 00020000 "upgrade"
MTD
}

write_mtd_single() { # <index> <label> <size-hex>
	{
		printf 'dev:    size   erasesize  name\n'
		printf 'mtd%s: %s 00020000 "%s"\n' "$1" "$3" "$2"
	} > "$CASE/proc/mtd"
}

write_mac_at() { # <mtd-index> <12-hex> [offset-hex, default de00]
	local idx=$1
	local hex=$2
	local off=${3:-de00}
	local dev="$CASE/dev/mtd$idx"
	local i octet

	[ -e "$dev" ] || truncate -s $((0x10000)) "$dev"
	i=0
	while [ $i -lt 6 ]; do
		octet=${hex:$((i * 2)):2}
		printf "\\x${octet}" | dd of="$dev" bs=1 seek=$((0x$off + i)) conv=notrunc 2>/dev/null
		i=$((i + 1))
	done
}

# Run a shell body in a fresh process with the fixture exported and the real
# helper sourced. stdout -> out.log, stderr -> err.log, exit status -> rc file.
run_body() { # <body-text>
	local body="$1"

	(
		export LUOFU_MAC_TEST_ROOT="$CASE"
		export CASE_UI_LOG="$CASE/ui.log"
		: > "$CASE/ui.log"
		ucidef_set_interface_macaddr() {
			printf '%s %s\n' "$1" "$2" >> "$CASE_UI_LOG"
		}
		[ -f "$CASE/pre.sh" ] && . "$CASE/pre.sh"
		. "$HELPER"
		[ -f "$CASE/post.sh" ] && . "$CASE/post.sh"
		eval "$body"
	) > "$CASE/out.log" 2> "$CASE/err.log"
	echo "$?" > "$CASE/rc"
}

result_get() { # <key> -> last key=value line from out.log
	sed -n "s/^$1=//p" "$CASE/out.log" | tail -n 1
}

SETUP_BODY='
luofu_mac_setup
echo "setup_rc=$?"
echo "source=$LUOFU_MAC_SOURCE"
echo "lan=$LUOFU_LAN_MAC"
echo "wan=$LUOFU_WAN_MAC"
echo "path=$LUOFU_MAC_READ_PATH"
echo "read_error=$LUOFU_MAC_READ_ERROR"
echo "mac_error=$LUOFU_MAC_ERROR"
luofu_mac_apply
echo "apply_rc=$?"
'

run_setup() { run_body "$SETUP_BODY"; }

# ---------------------------------------------------------------------------
# static checks
# ---------------------------------------------------------------------------

static_checks() {
	note "static: syntax, modes, wiring, no factory-partition writes"

	SH_N=0
	$SH -n "$HELPER" 2>"$TMP/helper.shn" || SH_N=1
	check_eq "helper passes sh -n" 0 "$SH_N"
	SH_N=0
	$SH -n "$BOARD" 2>"$TMP/board.shn" || SH_N=1
	check_eq "board.d/02_network passes sh -n" 0 "$SH_N"
	BASH_N=0
	bash -n "$0" || BASH_N=1
	check_eq "test itself passes bash -n" 0 "$BASH_N"

	check_file_mode "helper mode is 0644" "$HELPER" 644
	check_file_mode "board.d/02_network mode is 0755" "$BOARD" 755

	check_match "board.d sources the MAC helper" '^\. /lib/functions/luofu-mac\.sh$' "$BOARD"
	check_match "board.d calls luofu_mac_setup" 'luofu_mac_setup' "$BOARD"
	check_match "board.d calls luofu_mac_apply" 'luofu_mac_apply' "$BOARD"
	check_no_match "board.d no longer sources the non-existent board.sh" 'lib/functions/board\.sh' "$BOARD"

	check_no_match "helper never writes the bdinfo partition" 'mtd[[:space:]]+write|dd[^|]*of=/dev/mtd' "$HELPER"
	check_no_match "helper never reads the U-Boot env" 'mtd_get_mac_ascii|fw_printenv|uboot-envtools' "$HELPER"
	check_match "helper reads offset 0xde00" '0xde00' "$HELPER"
	check_match "helper reads the bdinfo partition" 'bdinfo' "$HELPER"
}

# ---------------------------------------------------------------------------
# pure functions: canonicalize / validate / add
# ---------------------------------------------------------------------------

canonicalize_checks() {
	note "canonicalize: colon/dash/dot/12-hex/upper case"

	new_case canon
	run_body '
echo "c1=$(luofu_mac_canonicalize "AA:BB:CC:DD:EE:FF")"
echo "c2=$(luofu_mac_canonicalize "aabbccddeeff")"
echo "c3=$(luofu_mac_canonicalize "aa-bb-cc-dd-ee-ff")"
echo "c4=$(luofu_mac_canonicalize "aabb.ccdd.eeff")"
echo "c5=$(luofu_mac_canonicalize "not-a-mac")"
echo "c6=$(luofu_mac_canonicalize "00:11:22:33:44:55:66")"
echo "c7=$(luofu_mac_canonicalize "0:1:2:3:4:5")"
'
	check_eq "canon: upper-case colon form" "aa:bb:cc:dd:ee:ff" "$(result_get c1)"
	check_eq "canon: bare 12-hex" "aa:bb:cc:dd:ee:ff" "$(result_get c2)"
	check_eq "canon: dash form" "aa:bb:cc:dd:ee:ff" "$(result_get c3)"
	check_eq "canon: dot form" "aa:bb:cc:dd:ee:ff" "$(result_get c4)"
	check_eq "canon: garbage rejected" "" "$(result_get c5)"
	check_eq "canon: too long rejected" "" "$(result_get c6)"
	check_eq "canon: short octets padded" "00:01:02:03:04:05" "$(result_get c7)"
}

validate_checks() {
	note "validate: factory-style acceptance and every rejection class"

	new_case valid
	run_body '
for m in \
	"00:11:22:33:44:55" \
	"A4:BB:CC:DD:EE:FF" \
	"01:11:22:33:44:55" \
	"02:11:22:33:44:55" \
	"00:00:00:00:00:00" \
	"ff:ff:ff:ff:ff:ff" \
	"00:00:00:12:34:56" \
	"00:11:22:ff:ff:ff" \
	"nonsense"
do
	if luofu_mac_validate "$m"; then
		echo "V [$m] ok canon=$LUOFU_MAC_CANONICAL"
	else
		echo "V [$m] reject reason=$LUOFU_MAC_ERROR"
	fi
done
'
	check_line "validate: accepts a universal unicast MAC" "V [00:11:22:33:44:55] ok canon=00:11:22:33:44:55" "$CASE/out.log"
	check_line "validate: canonicalizes upper case" "V [A4:BB:CC:DD:EE:FF] ok canon=a4:bb:cc:dd:ee:ff" "$CASE/out.log"
	check_line "validate: rejects multicast" "V [01:11:22:33:44:55] reject reason=multicast address (group bit set)" "$CASE/out.log"
	check_line "validate: rejects locally-administered" "V [02:11:22:33:44:55] reject reason=locally-administered address (random/loader MAC, not a factory OUI)" "$CASE/out.log"
	check_line "validate: rejects all-zero" "V [00:00:00:00:00:00] reject reason=all-zero address" "$CASE/out.log"
	check_line "validate: rejects erased flash" "V [ff:ff:ff:ff:ff:ff] reject reason=all-0xff address (erased or absent bdinfo data)" "$CASE/out.log"
	check_line "validate: rejects unassigned OUI 00:00:00" "V [00:00:00:12:34:56] reject reason=OUI 00:00:00 is not an assigned vendor prefix" "$CASE/out.log"
	check_line "validate: rejects the WAN-wrap base" "V [00:11:22:ff:ff:ff] reject reason=last three octets are ff:ff:ff; WAN=LAN+1 would wrap the NIC field to 00:00:00" "$CASE/out.log"
	check_line "validate: rejects non-MAC input" "V [nonsense] reject reason=not a 48-bit MAC address (nonsense)" "$CASE/out.log"
}

add_checks() {
	note "add: vendor/OpenWrt macaddr_add rule (both implementations)"

	ADD_BODY='
for spec in \
	"00:11:22:33:44:55|1" \
	"aa:bb:cc:dd:ee:fe|1" \
	"aa:bb:cc:dd:ee:ff|1" \
	"aa:bb:cc:ff:ff:ff|1" \
	"00:11:22:00:00:00|1" \
	"de:ad:be:ef:ff:fe|1"
do
	set -- $(printf "%s" "$spec" | tr "|" " ")
	echo "A [$1]+$2=$(luofu_mac_add "$1" "$2")"
done
'
	new_case add-fallback
	run_body "$ADD_BODY"

	check_line "add: simple +1" "A [00:11:22:33:44:55]+1=00:11:22:33:44:56" "$CASE/out.log"
	check_line "add: last byte ff keeps the OUI" "A [aa:bb:cc:dd:ee:fe]+1=aa:bb:cc:dd:ee:ff" "$CASE/out.log"
	check_line "add: NIC-field wrap, no OUI carry" "A [aa:bb:cc:dd:ee:ff]+1=aa:bb:cc:dd:ef:00" "$CASE/out.log"
	check_line "add: full NIC wrap to 00:00:00" "A [aa:bb:cc:ff:ff:ff]+1=aa:bb:cc:00:00:00" "$CASE/out.log"
	check_line "add: zero NIC increments low byte" "A [00:11:22:00:00:00]+1=00:11:22:00:00:01" "$CASE/out.log"
	check_line "add: last NIC address wraps" "A [de:ad:be:ef:ff:fe]+1=de:ad:be:ef:ff:ff" "$CASE/out.log"
	cp "$CASE/out.log" "$TMP/add-fallback.out"

	# Same vectors with a mock macaddr_add that mirrors /lib/functions/system.sh
	# (the production path). The outputs must be byte-identical.
	new_case add-helper
	cat > "$CASE/pre.sh" <<'SH'
macaddr_add() {
	local mac=$1
	local val=$2
	local oui=${mac%:*:*:*}
	local nic=${mac#*:*:*:}

	nic=$(printf "%06x" $((0x${nic//:/} + val & 0xffffff)) | sed 's/^\(.\{2\}\)\(.\{2\}\)\(.\{2\}\)/\1:\2:\3/')
	echo $oui:$nic
}
SH
	run_body "$ADD_BODY"
	if cmp -s "$TMP/add-fallback.out" "$CASE/out.log"; then
		pass "add: POSIX fallback and system.sh implementation agree on all vectors"
	else
		fail "add: POSIX fallback and system.sh implementation disagree"
		diff -u "$TMP/add-fallback.out" "$CASE/out.log" | sed -n '1,10p' | sed 's/^/    /'
	fi
}

# ---------------------------------------------------------------------------
# factory read paths
# ---------------------------------------------------------------------------

read_helper_path() {
	note "read: mtd_get_mac_binary (vendor 2.5.24 path)"

	new_case read-helper
	cat > "$CASE/pre.sh" <<'SH'
mtd_get_mac_binary() {
	[ "$1" = "bdinfo" ] && [ "$2" = "0xde00" ] || return 1
	echo "00:11:22:33:44:55"
}
SH
	run_setup
	check_eq "read-helper: setup rc" 0 "$(result_get setup_rc)"
	check_eq "read-helper: source is factory" factory "$(result_get source)"
	check_eq "read-helper: LAN = base MAC" "00:11:22:33:44:55" "$(result_get lan)"
	check_eq "read-helper: WAN = base+1" "00:11:22:33:44:56" "$(result_get wan)"
	check_eq "read-helper: read path recorded" "mtd_get_mac_binary bdinfo 0xde00" "$(result_get path)"
	check_match "read-helper: applied through ucidef" '^lan 00:11:22:33:44:55$' "$CASE/ui.log"
	check_match "read-helper: WAN entry recorded" '^wan 00:11:22:33:44:56$' "$CASE/ui.log"
}

read_direct_path() {
	note "read: direct /dev/mtdN fallback when the standard helper is absent"

	new_case read-direct
	write_stock_mtd
	write_mac_at 6 001122334455
	run_setup
	check_eq "read-direct: setup rc" 0 "$(result_get setup_rc)"
	check_eq "read-direct: source is factory" factory "$(result_get source)"
	check_eq "read-direct: LAN" "00:11:22:33:44:55" "$(result_get lan)"
	check_eq "read-direct: WAN" "00:11:22:33:44:56" "$(result_get wan)"
	check_match "read-direct: path names the char device and offset" '^path=direct read .*/dev/mtd6 @ 0xde00$' "$CASE/out.log"
	check_match "read-direct: factory read is logged" 'factory MAC from direct read .*/dev/mtd6 @ 0xde00: LAN 00:11:22:33:44:55 WAN 00:11:22:33:44:56' "$CASE/err.log"
}

read_direct_when_helper_fails() {
	note "read: direct fallback when mtd_get_mac_binary returns empty or garbage"

	new_case read-direct-empty
	write_stock_mtd
	write_mac_at 6 a0b2c3d4e5f6
	cat > "$CASE/pre.sh" <<'SH'
mtd_get_mac_binary() { return 1; }
SH
	run_setup
	check_eq "read-empty-helper: LAN from direct read" "a0:b2:c3:d4:e5:f6" "$(result_get lan)"
	check_eq "read-empty-helper: source is factory" factory "$(result_get source)"

	new_case read-direct-garbage
	write_stock_mtd
	write_mac_at 6 102030405060
	cat > "$CASE/pre.sh" <<'SH'
mtd_get_mac_binary() { echo "this is not a mac"; }
SH
	run_setup
	check_eq "read-garbage-helper: LAN from direct read" "10:20:30:40:50:60" "$(result_get lan)"
	check_eq "read-garbage-helper: source is factory" factory "$(result_get source)"
}

read_label_not_index() {
	note "read: bdinfo is found by label, never by a hardcoded mtd index"

	new_case read-label
	write_mtd_single 5 bdinfo 00040000
	write_mac_at 5 0011223344aa
	run_setup
	check_eq "read-label: LAN read from mtd5 by label" "00:11:22:33:44:aa" "$(result_get lan)"
	check_match "read-label: path names mtd5" '^path=direct read .*/dev/mtd5 @ 0xde00$' "$CASE/out.log"
}

read_exact_offset() {
	note "read: only the 6 bytes at 0xde00 are used"

	new_case read-offset
	write_stock_mtd
	# Fill the fixture with a decoy pattern, then place the real copy at 0xde00
	# and different decoys at 0x0000, 0xdd00 and 0xde06.
	truncate -s $((0x10000)) "$CASE/dev/mtd6"
	perl -e 'print "\xaa" x 65536' > "$CASE/dev/mtd6" 2>/dev/null || 		dd if=/dev/zero bs=1024 count=64 2>/dev/null | tr '\000' '\252' > "$CASE/dev/mtd6"
	write_mac_at 6 001122334455
	write_mac_at 6 ffffffffffff dd00
	write_mac_at 6 deadbeef0001 de06
	write_mac_at 6 0badc0ffee00 0000
	run_setup
	check_eq "read-offset: exact 0xde00 bytes used" "00:11:22:33:44:55" "$(result_get lan)"
	check_eq "read-offset: WAN derived from them" "00:11:22:33:44:56" "$(result_get wan)"
}

read_missing_cases() {
	note "read: missing table / label / device / short partition all fall back"

	new_case read-no-table
	: > "$CASE/proc/mtd"
	rm -f "$CASE/proc/mtd"
	run_setup
	check_eq "read-no-table: source is random" random "$(result_get source)"
	check_match "read-no-table: log names the unreadable table" 'MTD table .* is not readable' "$CASE/err.log"

	new_case read-no-label
	write_mtd_single 0 esbc 00040000
	write_mac_at 6 001122334455
	run_setup
	check_eq "read-no-label: source is random" random "$(result_get source)"
	check_match "read-no-label: log names the missing label" "partition 'bdinfo' not found" "$CASE/err.log"

	new_case read-small-partition
	write_mtd_single 6 bdinfo 00000100
	write_mac_at 6 001122334455
	run_setup
	check_eq "read-small-partition: source is random" random "$(result_get source)"
	check_match "read-small-partition: log names the too-small partition" 'too small for offset 0xde00' "$CASE/err.log"

	new_case read-missing-device
	write_stock_mtd
	run_setup
	check_eq "read-missing-device: source is random" random "$(result_get source)"
	check_match "read-missing-device: log names the missing device" 'character device .*/dev/mtd6 is missing' "$CASE/err.log"

	new_case read-short-device
	write_stock_mtd
	truncate -s $((0xde00 + 2)) "$CASE/dev/mtd6"
	run_setup
	check_eq "read-short-device: source is random" random "$(result_get source)"
	check_match "read-short-device: log names the short read" 'short/failed read' "$CASE/err.log"
}

corrupt_factory_values() {
	note "corrupt: every invalid factory value -> logged random fallback"

	local mac reason
	while IFS='|' read -r mac reason; do
		[ -n "$mac" ] || continue
		new_case "corrupt-$(printf '%s' "$mac" | tr -d ':')"
		write_stock_mtd
		write_mac_at 6 "$(printf '%s' "$mac" | tr -d ':')"
		run_setup
		check_eq "corrupt [$mac]: source is random" random "$(result_get source)"
		check_match "corrupt [$mac]: log names the rejection" "$reason" "$CASE/err.log"
	done <<'CORRUPT'
00:00:00:00:00:00|all-zero address
ff:ff:ff:ff:ff:ff|all-0xff address
01:22:33:44:55:66|multicast address
02:22:33:44:55:66|locally-administered address
00:00:00:12:34:56|OUI 00:00:00 is not an assigned vendor prefix
00:11:22:ff:ff:ff|last three octets are ff:ff:ff
CORRUPT
}

# ---------------------------------------------------------------------------
# random fallback
# ---------------------------------------------------------------------------

random_checks() {
	note "random: locally-administered unicast MACs + WAN relation"

	new_case random-bits
	cat > "$CASE/post.sh" <<'SH'
_luofu_mac_urandom_hex() { echo "011122334455"; }
SH
	run_setup
	check_eq "random-bits: source is random" random "$(result_get source)"
	check_eq "random-bits: multicast cleared, LA bit set" "02:11:22:33:44:55" "$(result_get lan)"
	check_eq "random-bits: WAN = LAN+1" "02:11:22:33:44:56" "$(result_get wan)"
	check_match "random-bits: warning logged" 'WARNING: using random locally-administered MACs' "$CASE/err.log"

	new_case random-wrap
	cat > "$CASE/post.sh" <<'SH'
_luofu_mac_urandom_hex() { echo "feffffffffff"; }
SH
	run_setup
	check_eq "random-wrap: existing LA bit kept, unchanged" "fe:ff:ff:ff:ff:ff" "$(result_get lan)"
	check_eq "random-wrap: WAN wraps the NIC field" "fe:ff:ff:00:00:00" "$(result_get wan)"

	new_case random-source
	run_body '
h=$(luofu_mac_random)
echo "r=$h"
echo "valid=$(luofu_mac_validate "$h" && printf yes || printf no)"
'
	check_match "random-source: real /dev/urandom gives a 12-hex MAC" '^r=[0-9a-f]{2}(:[0-9a-f]{2}){5}$' "$CASE/out.log"
	check_line "random-source: random MAC is a unicast UNIVERSAL address before fallback marking" "valid=no" "$CASE/out.log"

	new_case random-failure
	cat > "$CASE/post.sh" <<'SH'
_luofu_mac_urandom_hex() { return 1; }
SH
	run_setup
	check_eq "random-failure: setup rc is a failure" 1 "$(result_get setup_rc)"
	check_eq "random-failure: LAN left empty" "" "$(result_get lan)"
	check_eq "random-failure: WAN left empty" "" "$(result_get wan)"
	check_eq "random-failure: apply refused" 1 "$(result_get apply_rc)"
	check_match "random-failure: clear error logged" 'ERROR: cannot generate a random fallback MAC' "$CASE/err.log"
	check_eq "random-failure: nothing applied" "" "$(cat "$CASE/ui.log")"
}

# ---------------------------------------------------------------------------
# apply + read-only guarantees
# ---------------------------------------------------------------------------

apply_checks() {
	note "apply: exact board.json entries, nothing when setup failed"

	new_case apply-refuse
	run_body '
luofu_mac_apply
echo "apply_rc=$?"
'
	check_eq "apply: refuses without a setup result" 1 "$(result_get apply_rc)"
	check_eq "apply: wrote nothing" "" "$(cat "$CASE/ui.log")"

	new_case apply-order
	write_stock_mtd
	write_mac_at 6 001122334455
	run_setup
	check_eq "apply: rc" 0 "$(result_get apply_rc)"
	check_line "apply: lan entry first" "lan 00:11:22:33:44:55" "$CASE/ui.log"
	check_line "apply: wan entry second" "wan 00:11:22:33:44:56" "$CASE/ui.log"
	check_eq "apply: exactly two entries" 2 "$(wc -l < "$CASE/ui.log")"
}

readonly_checks() {
	note "read-only: the fixture bdinfo bytes are never modified"

	new_case readonly
	write_stock_mtd
	write_mac_at 6 001122334455
	before=$(sha256sum "$CASE/dev/mtd6" | cut -d' ' -f1)
	run_setup
	after=$(sha256sum "$CASE/dev/mtd6" | cut -d' ' -f1)
	check_eq "read-only: fake bdinfo device unchanged" "$before" "$after"
	check_eq "read-only: no write command in the log" "" "$(grep -E 'mtd write|dd .*of=' "$CASE/err.log" || true)"

	new_case source-silent
	run_body 'true'
	check_eq "load: sourcing the helper prints nothing on stdout" "" "$(cat "$CASE/out.log")"
	check_eq "load: sourcing the helper prints nothing on stderr" "" "$(cat "$CASE/err.log")"
}

# ---------------------------------------------------------------------------

echo "# luofu factory-MAC provisioning - host-side mocked tests"
echo "# helper: $HELPER"
echo "# board.d: $BOARD"
echo

static_checks
canonicalize_checks
validate_checks
add_checks
read_helper_path
read_direct_path
read_direct_when_helper_fails
read_label_not_index
read_exact_offset
read_missing_cases
corrupt_factory_values
random_checks
apply_checks
readonly_checks

echo
echo "# passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
