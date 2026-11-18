# SPDX-License-Identifier: GPL-2.0-only
#
# luofu-mac.sh - factory MAC provisioning for the HiSilicon luofu R116
# (Cudy WR3000 v2.0) OpenWrt target.
#
# STATUS: HOST-VALIDATED ONLY. This file is exercised by
# tools/test-mac-provisioning.sh against a mocked /proc/mtd, mocked mtd device
# files and a mocked ucidef helper; it has NEVER run on a router. No device,
# UART, TFTP or flash tool is involved, and nothing is booted.
#
# WHAT IT DOES
#   Reads the one plaintext 6-byte factory MAC copy at offset 0xde00 of the
#   encrypted bdinfo MTD partition (mtd6, 0x40000) and derives the two netdev
#   addresses the vendor userspace uses:
#
#     LAN = factory base MAC                 (vendor 2.4.15 board.d/02_network:25)
#     WAN = LAN + 1 in the low 24 bits       (vendor 2.4.15 board.d/02_network:27)
#
#   Evidence (docs/MISC-SOC-BLOCKS.md section 1; per-unit values are withheld
#   there):
#     [V] the plaintext 0xde00 copy and the decrypted "mac" line of the same
#         bdinfo partition are the same six bytes on the measured unit;
#     [V] vendor 2.5.24 /etc/uci-defaults/99_fixwan:41 reads the LAN address
#         with `mtd_get_mac_binary bdinfo 0xde00`;
#     [V] vendor 2.4.15 /etc/board.d/02_network:25-29 canonicalizes
#         `bdinfo mac` for LAN and adds 1 for WAN, then applies both with
#         ucidef_set_interface_macaddr;
#     [V] vendor 2.4.15 /etc/uci-defaults/99_fixwan:17-52 applies the LAN base
#         MAC to every LAN device and base+1 to WAN (`ori_macaddr` records it).
#   The increment is the vendor/OpenWrt `macaddr_add` form: the first three
#   octets (the OUI) are preserved and the last three are incremented modulo
#   2^24, i.e. ...:ff:ff:ff + 1 wraps to ...:00:00:00 instead of carrying into
#   the OUI. This file mirrors that rule exactly and uses system.sh's
#   macaddr_add when it is available.
#
# SAFETY RULES KEPT
#   - READ ONLY: the bdinfo partition is never written and no partition content
#     is copied or fabricated. A MAC that fails validation is NOT repaired; the
#     target falls back to a generated locally-administered random MAC and logs
#     why. Factory data is never invented.
#   - The U-Boot env `ethaddr` is NOT used: the loader's copy is a
#     locally-administered random value, not the factory MAC
#     (MISC-SOC-BLOCKS.md section 1.2), and mixing it with bdinfo would give one
#     unit two identities.
#   - The locally-administered bit in the bdinfo MAC is treated as a corruption
#     signal, not as a value to keep: the factory MAC should be a universally
#     administered unicast address. See luofu_mac_validate().
#
# WHEN IT RUNS
#   base-files/etc/board.d/02_network calls luofu_mac_setup + luofu_mac_apply
#   while board.d is processed (preinit). /proc/mtd and /dev/mtdN exist then:
#   the target kernel has CONFIG_DEVTMPFS_MOUNT=y and the SPI-NAND MTD
#   partitions are registered before init. The addresses reach /etc/board.json
#   through ucidef_set_interface_macaddr; /bin/config_generate turns them into
#   netifd device macaddr options as soon as the board's interface map exists
#   (TODO(switch): the internal LSW switch driver, TARGET-DESIGN.md section 8.1;
#   until then the board.json entries are recorded but no netdev is configured).
#
# TEST HOOK
#   LUOFU_MAC_TEST_ROOT (host tests only; unset on the device) prefixes the
#   /proc/mtd and /dev paths, exactly like platform.sh's LUOFU_TEST_ROOT.

LUOFU_MAC_TEST_ROOT="${LUOFU_MAC_TEST_ROOT:-}"
LUOFU_MAC_MTD_TABLE="$LUOFU_MAC_TEST_ROOT/proc/mtd"
LUOFU_MAC_DEV_DIR="$LUOFU_MAC_TEST_ROOT/dev"

# The measured factory sources (MISC-SOC-BLOCKS.md section 1.1). These are
# constants, not tunables: the partition label and the plaintext offset are
# what the vendor userspace and the local flash dump establish.
LUOFU_MAC_BDINFO_PART="bdinfo"
LUOFU_MAC_BDINFO_OFFSET="0xde00"
LUOFU_MAC_BDINFO_LEN=6

# Results / diagnostics (public to the caller and to tests).
LUOFU_LAN_MAC=""
LUOFU_WAN_MAC=""
LUOFU_MAC_SOURCE=""
LUOFU_MAC_RAW=""
LUOFU_MAC_READ_PATH=""
LUOFU_MAC_READ_ERROR=""
LUOFU_MAC_ERROR=""
LUOFU_MAC_CANONICAL=""
LUOFU_MAC_MTD_INDEX=""
LUOFU_MAC_MTD_SIZE=""

luofu_mac_log() {
	echo "luofu-mac: $*" >&2
}

luofu_mac_canonicalize() { # <mac-in-any-common-form> -> lower-case colon form
	local mac="$1" octet rest canon=""

	if command -v macaddr_canonicalize >/dev/null 2>&1; then
		macaddr_canonicalize "$mac"
		return
	fi

	# POSIX transcription of system.sh's macaddr_canonicalize() for contexts
	# without /lib/functions/system.sh. Accepts the same forms that helper
	# accepts: colon/dash/dot/space separated octets, 4-hex groups, a bare
	# 12-hex string and 1-digit octets.
	mac=$(printf '%s' "$mac" | tr -d '"')
	[ ${#mac} -le 17 ] || return 1
	mac=$(printf '%s' "$mac" | tr 'A-F' 'a-f')
	case "$mac" in
	*[!0-9a-f.:\ -]*) return 1 ;;
	esac

	set -- $(printf '%s' "$mac" | tr '.:-' '   ')
	for octet in "$@"; do
		case ${#octet} in
		1) canon="$canon 0$octet" ;;
		2) canon="$canon $octet" ;;
		4) canon="$canon ${octet%??} ${octet#??}" ;;
		12)
			rest=$octet
			while [ -n "$rest" ]; do
				canon="$canon $(printf '%s' "$rest" | cut -c1-2)"
				rest=$(printf '%s' "$rest" | cut -c3-)
			done
			;;
		*) return 1 ;;
		esac
	done
	set -- $canon
	[ $# -eq 6 ] || return 1
	printf '%s:%s:%s:%s:%s:%s\n' "$1" "$2" "$3" "$4" "$5" "$6"
}

luofu_mac_validate() { # <mac>; 0 = valid factory-style base MAC
	local mac first

	LUOFU_MAC_ERROR=""
	LUOFU_MAC_CANONICAL=""

	mac=$(luofu_mac_canonicalize "$1")
	[ -n "$mac" ] || {
		LUOFU_MAC_ERROR="not a 48-bit MAC address ($1)"
		return 1
	}

	# Sanity: erased flash and empty/zero reads are the two realistic corrupt
	# readings of the plaintext copy; report them before the bit checks.
	case "$mac" in
	00:00:00:00:00:00)
		LUOFU_MAC_ERROR="all-zero address"
		return 1
		;;
	ff:ff:ff:ff:ff:ff)
		LUOFU_MAC_ERROR="all-0xff address (erased or absent bdinfo data)"
		return 1
		;;
	esac

	first=${mac%%:*}
	if [ $((0x$first & 0x01)) -ne 0 ]; then
		LUOFU_MAC_ERROR="multicast address (group bit set)"
		return 1
	fi
	if [ $((0x$first & 0x02)) -ne 0 ]; then
		LUOFU_MAC_ERROR="locally-administered address (random/loader MAC, not a factory OUI)"
		return 1
	fi

	case "$mac" in
	00:00:00:*)
		LUOFU_MAC_ERROR="OUI 00:00:00 is not an assigned vendor prefix"
		return 1
		;;
	*:ff:ff:ff)
		# Conservative project check [I], not a vendor rule: WAN = LAN+1 is the
		# vendor rule, and a base ending in ff:ff:ff would make the WAN address
		# wrap its NIC field to 00:00:00. That reading of the factory copy is
		# suspicious enough to prefer the logged random fallback.
		LUOFU_MAC_ERROR="last three octets are ff:ff:ff; WAN=LAN+1 would wrap the NIC field to 00:00:00"
		return 1
		;;
	esac

	LUOFU_MAC_CANONICAL="$mac"
	return 0
}

luofu_mac_add() { # <mac> <delta>: OUI preserved, low 24 bits + delta (mod 2^24)
	local mac="$1"
	local val="$2"
	local oui nic o4 o5 o6 n

	if command -v macaddr_add >/dev/null 2>&1; then
		macaddr_add "$mac" "$val"
		return
	fi

	# POSIX fallback; same algorithm as OpenWrt/vendor macaddr_add().
	oui=${mac%:*:*:*}
	nic=${mac#*:*:*:}
	o4=${nic%%:*}
	nic=${nic#*:}
	o5=${nic%%:*}
	o6=${nic#*:}
	n=$(( ((0x$o4 << 16) | (0x$o5 << 8) | 0x$o6) + val ))
	n=$(( n & 0xffffff ))
	printf '%s:%02x:%02x:%02x\n' "$oui" \
		$(( (n >> 16) & 0xff )) $(( (n >> 8) & 0xff )) $(( n & 0xff ))
}

_luofu_mac_urandom_hex() { # -> 12 lower-case hex chars
	local hex

	hex=$(hexdump -v -n 6 -e '6/1 "%02x"' /dev/urandom 2>/dev/null)
	if [ ${#hex} -ne 12 ]; then
		hex=$(od -An -tx1 -N 6 /dev/urandom 2>/dev/null | tr -d ' \n')
	fi
	[ ${#hex} -eq 12 ] || return 1
	case "$hex" in
	*[!0-9a-f]*) return 1 ;;
	esac
	printf '%s\n' "$hex"
}

luofu_mac_random() { # -> locally-administered unicast random MAC
	local hex first tail

	hex=$(_luofu_mac_urandom_hex) || return 1
	first=$((0x${hex%??????????}))
	tail=${hex#??}
	# Locally-administered bit set, multicast/group bit clear (never factory data).
	first=$(( (first | 0x02) & 0xfe ))
	hex=$(printf '%02x%s' "$first" "$tail")
	luofu_mac_canonicalize "$hex"
}

_luofu_mac_mtd_lookup() { # <table> <label>; sets LUOFU_MAC_MTD_INDEX / _SIZE
	local dev size erasesize name

	LUOFU_MAC_MTD_INDEX=""
	LUOFU_MAC_MTD_SIZE=""

	[ -r "$1" ] || {
		LUOFU_MAC_READ_ERROR="MTD table $1 is not readable"
		return 1
	}

	while read -r dev size _erasesize name; do
		case "$dev" in
		mtd[0-9]*) ;;
		*) continue ;;
		esac
		name=${name%\"}
		name=${name#\"}
		[ "$name" = "$2" ] || continue
		LUOFU_MAC_MTD_INDEX=${dev#mtd}
		LUOFU_MAC_MTD_INDEX=${LUOFU_MAC_MTD_INDEX%:}
		LUOFU_MAC_MTD_SIZE=$((0x$size))
		return 0
	done < "$1"

	LUOFU_MAC_READ_ERROR="partition '$2' not found in $1"
	return 1
}

luofu_mac_read_bdinfo_direct() { # sets LUOFU_MAC_RAW from /dev/mtdN
	local dev off size hex mac

	LUOFU_MAC_RAW=""
	LUOFU_MAC_READ_ERROR=""

	_luofu_mac_mtd_lookup "$LUOFU_MAC_MTD_TABLE" "$LUOFU_MAC_BDINFO_PART" || return 1

	off=$((LUOFU_MAC_BDINFO_OFFSET))
	size=$((LUOFU_MAC_MTD_SIZE))
	[ "$size" -ge $((off + LUOFU_MAC_BDINFO_LEN)) ] || {
		LUOFU_MAC_READ_ERROR="bdinfo partition (mtd$LUOFU_MAC_MTD_INDEX, $size B) is too small for offset $LUOFU_MAC_BDINFO_OFFSET"
		return 1
	}

	dev="$LUOFU_MAC_DEV_DIR/mtd$LUOFU_MAC_MTD_INDEX"
	[ -e "$dev" ] || {
		LUOFU_MAC_READ_ERROR="mtd character device $dev is missing"
		return 1
	}

	hex=$(hexdump -v -n "$LUOFU_MAC_BDINFO_LEN" -s "$LUOFU_MAC_BDINFO_OFFSET" \
		-e '6/1 "%02x"' "$dev" 2>/dev/null | tr -d ' \t\n')
	[ ${#hex} -eq 12 ] || {
		LUOFU_MAC_READ_ERROR="short/failed read from $dev at offset $LUOFU_MAC_BDINFO_OFFSET"
		return 1
	}
	mac=$(luofu_mac_canonicalize "$hex")
	[ -n "$mac" ] || {
		LUOFU_MAC_READ_ERROR="unparsable read from $dev at offset $LUOFU_MAC_BDINFO_OFFSET"
		return 1
	}
	LUOFU_MAC_RAW=$mac
	return 0
}

luofu_mac_read_bdinfo() { # sets LUOFU_MAC_RAW and LUOFU_MAC_READ_PATH
	local mac

	LUOFU_MAC_RAW=""
	LUOFU_MAC_READ_ERROR=""
	LUOFU_MAC_READ_PATH=""

	# 1. The standard OpenWrt helper, i.e. the vendor 2.5.24 read path
	#    (mtdblock device, MAC formatted by get_mac_binary()).
	if command -v mtd_get_mac_binary >/dev/null 2>&1; then
		mac=$(mtd_get_mac_binary "$LUOFU_MAC_BDINFO_PART" "$LUOFU_MAC_BDINFO_OFFSET" 2>/dev/null)
		mac=$(luofu_mac_canonicalize "$mac" 2>/dev/null)
		if [ -n "$mac" ]; then
			LUOFU_MAC_RAW=$mac
			LUOFU_MAC_READ_PATH="mtd_get_mac_binary $LUOFU_MAC_BDINFO_PART $LUOFU_MAC_BDINFO_OFFSET"
			return 0
		fi
	fi

	# 2. Direct /dev/mtdN character-device read. Also covers a boot stage where
	#    the mtdblock devices do not exist yet (mtdchar is part of MTD core).
	if luofu_mac_read_bdinfo_direct; then
		LUOFU_MAC_READ_PATH="direct read $LUOFU_MAC_DEV_DIR/mtd$LUOFU_MAC_MTD_INDEX @ $LUOFU_MAC_BDINFO_OFFSET"
		return 0
	fi

	return 1
}

luofu_mac_setup() { # sets LUOFU_LAN_MAC / LUOFU_WAN_MAC / LUOFU_MAC_SOURCE
	local base wan

	LUOFU_LAN_MAC=""
	LUOFU_WAN_MAC=""
	LUOFU_MAC_SOURCE=""
	LUOFU_MAC_ERROR=""

	if luofu_mac_read_bdinfo; then
		base=$LUOFU_MAC_RAW
		if luofu_mac_validate "$base"; then
			base=$LUOFU_MAC_CANONICAL
			wan=$(luofu_mac_canonicalize "$(luofu_mac_add "$base" 1)" 2>/dev/null)
			if [ -n "$wan" ]; then
				LUOFU_LAN_MAC=$base
				LUOFU_WAN_MAC=$wan
				LUOFU_MAC_SOURCE="factory"
				luofu_mac_log "factory MAC from $LUOFU_MAC_READ_PATH: LAN $base WAN $wan (WAN=LAN+1)"
				return 0
			fi
			luofu_mac_log "factory MAC $base read but the WAN derivation failed; falling back to random"
		else
			luofu_mac_log "bdinfo+$LUOFU_MAC_BDINFO_OFFSET rejected: $LUOFU_MAC_ERROR; falling back to random"
		fi
	else
		luofu_mac_log "cannot read a factory MAC from bdinfo+$LUOFU_MAC_BDINFO_OFFSET (${LUOFU_MAC_READ_ERROR:-unknown reason}); falling back to random"
	fi

	base=$(luofu_mac_random) || {
		luofu_mac_log "ERROR: cannot generate a random fallback MAC (no usable /dev/urandom); leaving the MACs unset"
		return 1
	}
	wan=$(luofu_mac_canonicalize "$(luofu_mac_add "$base" 1)" 2>/dev/null)
	[ -n "$wan" ] || {
		luofu_mac_log "ERROR: random MAC derivation failed; leaving the MACs unset"
		return 1
	}
	LUOFU_LAN_MAC=$base
	LUOFU_WAN_MAC=$wan
	LUOFU_MAC_SOURCE="random"
	luofu_mac_log "WARNING: using random locally-administered MACs (LAN $base WAN $wan); the unit has no usable factory MAC - identity changes on a factory reset"
	return 0
}

luofu_mac_apply() { # write both addresses into the board.json interface entries
	if [ -z "$LUOFU_LAN_MAC" ] || [ -z "$LUOFU_WAN_MAC" ]; then
		luofu_mac_log "no MAC addresses to apply"
		return 1
	fi
	if ! command -v ucidef_set_interface_macaddr >/dev/null 2>&1; then
		luofu_mac_log "ucidef_set_interface_macaddr is unavailable; not applying MACs"
		return 1
	fi
	ucidef_set_interface_macaddr "lan" "$LUOFU_LAN_MAC"
	ucidef_set_interface_macaddr "wan" "$LUOFU_WAN_MAC"
	luofu_mac_log "applied LAN $LUOFU_LAN_MAC / WAN $LUOFU_WAN_MAC to the lan/wan board.json entries (source: $LUOFU_MAC_SOURCE)"
	return 0
}
