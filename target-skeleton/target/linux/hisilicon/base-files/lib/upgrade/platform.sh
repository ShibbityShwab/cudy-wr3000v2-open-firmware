# SPDX-License-Identifier: GPL-2.0-only
#
# platform.sh - OpenWrt sysupgrade platform hooks for the HiSilicon luofu R116
# (Cudy WR3000 v2.0) target.
#
# STATUS: HOST-ONLY / NEVER RUN ON HARDWARE / NO DEVICE CLAIM.
#
# platform_check_image() re-derives, from the received bytes only, every
# structural field the image recipe claimed (tar layout, CONTROL, uImage
# header, FIP trailer and gate magic, descriptor constants, rootfs UBI volume
# shape, fip-manifest.txt cross-check, slot budgets). It refuses anything
# else, including a vendor recovery.bin. The container still says
# NOT FLASHABLE-UNTIL-INSTALLED: that is a provenance statement (the install/
# upgrade path has never run end-to-end on a device), not a write gate -
# platform_do_upgrade is the A/B install path.
#
# platform_do_upgrade() implements that path host-side, fail-closed:
#   1. re-runs platform_check_image (defence in depth: a direct do_stage2
#      invocation must not write an unvalidated container);
#   2. resolves the ACTIVE pair from two independent in-boot witnesses that
#      must agree: the boot-selector sysfs current_pair (the SYSCTRL cell's
#      record of the pair) and the rootfs slot this system booted from
#      (/sys/class/ubi/ubi0/mtd_num cross-checked with that mtd's /proc/mtd
#      label). /proc/cmdline is not used: the measured boot carries no slot
#      token. This check is only valid because the target image records the
#      running pair on every boot (base-files/etc/init.d/luofu_record_pair),
#      so the cell and the booted rootfs agree again after init;
#   3. selects the INACTIVE pair by /proc/mtd label (kernelb+rootfsb after an
#      A boot, kernela+rootfsa after a B boot) and validates the measured
#      partition sizes and the container members against them;
#   4. writes the kernel slice (mtd write; erase+write) and the rootfs UBI
#      (ubiformat) to the inactive pair only, then reads both regions back and
#      verifies them;
#   5. syncs, and only then writes the inactive pair's value into the selector
#      record, reading it back. The bootloader selects the pair from its env
#      bootflag, so writing the value that does not match the env is what
#      makes the next boot take the bootloader's failover path: it re-stamps
#      the env to the written pair, saves it and boots it; its bootfip then
#      stores the opposite value, and the new image's boot pair recorder
#      re-records the running pair once that boot succeeds. On any failure the
#      selector is left - or restored - on the active pair, so the next boot
#      still comes up on the untouched pair. The selector write is last
#      because there is no way back from a half-written pair except the other
#      pair;
#   6. returns and lets /lib/upgrade/do_stage2 run its existing reboot path.
#      This function never reboots, never erases rootfs_data (mtd15 is a
#      separate shared partition) and never writes the env copies: the env
#      belongs to the bootloader, and its mismatch/failover path is the
#      supported re-stamp.
#
# Selector evidence (corrected 2026-11-18 from docs/SELECTOR-0x21.md; measured,
# nothing invented):
#   - The bootloader selects the pair from the env "bootflag" (bootcmd reads
#     kernel${bootflag}); the SYSCTRL cell is the loader's *record* of the
#     pair, not a second selector. 0x10 = A pair recorded, 0x11 = B pair
#     recorded: the loader's own "bootreg init, set 10 / set 11" prints, its
#     bootfip opposite-pair store and the controlled 0x10/0x11 tests (parent
#     BENCH-FACTS.md section 5; ledger phase 67). The cell is sticky across
#     warm resets and is cleared by a true power-on.
#   - On every boot bootfip stores the OPPOSITE pair's low byte (low-byte
#     read-modify-write, upper bits preserved); a recorded value that
#     contradicts the env on the next boot makes the loader re-stamp the env
#     and boot the other pair. A successful boot is only stable if userspace
#     re-records the running pair, which the target image does in
#     base-files/etc/init.d/luofu_record_pair (START=96, after
#     /etc/init.d/done). Without it the reboot after a normal boot silently
#     follows the failover path back to the old pair.
#   - 0x21 is the project's measured *bench* handover value (FLASH-PLAN.md /
#     tools/fire-device.sh), not a vendor value: vendor userspace writes only
#     0x10/0x11, the loader discards the 0x20 marker bit on the same boot and
#     0x21 never persists. An in-boot 0x21 (or 0x00, or any other unmeasured
#     value) still refuses the automated handover, as before.
#   - Env blocks enva (mtd3) / envb (mtd4): the bootloader owns them and
#     re-stamps them only on its mismatch/failover path. This path never
#     writes them. One attended A/B validation has to confirm the re-stamp.
#
# Rules kept from the earlier refusal design: labels, never bare MTD numbers;
# never U-Boot's TFTP fipupdate_eth/rootfsupdate_eth (they overwrite both
# slots); never a "slot failed" status word (the captures do not support it) -
# rollback is switching back to the other pair, which stays bootable.
#
# The mocked host-side harness for this file is tools/test-platform-abinstall.sh;
# it runs the real functions against a fake /proc/mtd, selector sysfs and
# mtd/ubiformat/sync/reboot commands (and the real container check when a
# built sysupgrade.bin is supplied).

REQUIRE_IMAGE_METADATA=1

LUOFU_KERNEL_SLOT_SIZE=8650752
LUOFU_ROOTFS_SLOT_SIZE=24313856
LUOFU_BOARD_NAME=cudy_wr3000-v2
LUOFU_LOADADDR=0x80608000
LUOFU_UBI_PEB_SIZE=131072
LUOFU_UBI_VID_HDR_OFFSET=2048
LUOFU_UBI_LAYOUT_VOL_ID=0x7fffefff
LUOFU_GATE_MAGIC=0xd7b7ab1e

_luofu_hex32() { # <file> <offset> -> 8 hex chars (big-endian u32)
	hexdump -v -n 4 -s "$2" -e '1/1 "%02x"' "$1" 2>/dev/null
}

_luofu_hex8() { # <file> <offset> -> 2 hex chars
	hexdump -v -n 1 -s "$2" -e '1/1 "%02x"' "$1" 2>/dev/null
}

_luofu_hex16() { # <file> <offset> -> 4 hex chars (big-endian u16)
	hexdump -v -n 2 -s "$2" -e '1/1 "%02x"' "$1" 2>/dev/null
}

_luofu_be32() { # <file> <offset> -> decimal (failure -> empty return 1)
	local h
	h="$(_luofu_hex32 "$1" "$2")"
	[ "${#h}" -eq 8 ] || return 1
	echo "$((0x$h))"
}

_luofu_hexval() { # "0x310ba8" -> decimal
	echo "$((0x$(echo "$1" | sed -e 's/^0[xX]//')))"
}

_luofu_manifest_get() { # <manifest-file> <key>
	sed -n "s/^$2:[[:space:]]*//p" "$1" 2>/dev/null | head -n 1
}

_luofu_tar_member() { # <image> <member-path> <output-file>
	get_image "$1" cat | tar xOf - "$2" > "$3" 2>/dev/null
}

_luofu_check_uimage_slice() { # <slice> <manifest>
	local slice="$1" manifest="$2"
	local size ih_size payload_end load ep loadaddr base_magic base_size
	local gate_off gate container total count i entry_off ov_off ov_size
	local ov_magic prev_end

	loadaddr="$(_luofu_hexval "$LUOFU_LOADADDR")"
	size="$(wc -c < "$slice")"
	[ "$size" -le "$LUOFU_KERNEL_SLOT_SIZE" ] || {
		echo "kernel slice is $size B, over the $LUOFU_KERNEL_SLOT_SIZE B slot"; return 1; }

	[ "$(_luofu_hex32 "$slice" 0)" = 27051956 ] || {
		echo "kernel slice does not start with a legacy uImage magic"; return 1; }
	ih_size="$(_luofu_be32 "$slice" 12)" || { echo "cannot read uImage ih_size"; return 1; }
	load="$(_luofu_be32 "$slice" 16)" || { echo "cannot read uImage ih_load"; return 1; }
	ep="$(_luofu_be32 "$slice" 20)" || { echo "cannot read uImage ih_ep"; return 1; }
	[ "$load" -eq "$loadaddr" ] && [ "$ep" -eq "$loadaddr" ] || {
		echo "uImage load/entry $(printf '0x%08x' "$load")/$(printf '0x%08x' "$ep") != derived $LUOFU_LOADADDR"
		return 1; }
	[ "$((0x40 + ih_size))" -lt "$size" ] || { echo "uImage payload runs past the slice"; return 1; }

	payload_end=$((0x40 + ih_size))
	base_magic="$(_luofu_hex32 "$slice" "$payload_end")"
	[ "$base_magic" = d00dfeed ] || {
		echo "no base DTB (FDT magic d00dfeed) after the uImage payload"; return 1; }
	base_size="$(_luofu_be32 "$slice" "$((payload_end + 4))")" || {
		echo "cannot read the base DTB totalsize"; return 1; }
	[ "$base_size" -gt 0 ] && [ "$((payload_end + base_size))" -lt "$size" ] || {
		echo "base DTB totalsize $base_size does not fit the slice"; return 1; }

	# The pre-entry gate: 4 BE bytes at 0x40 + ih_size + base totalsize.
	gate_off=$((payload_end + base_size))
	gate="$(_luofu_hex32 "$slice" "$gate_off")"
	[ "$gate" = d7b7ab1e ] || {
		echo "FIP gate magic at slice offset $gate_off is 0x$gate, want $LUOFU_GATE_MAGIC"; return 1; }

	container="$(_luofu_be32 "$slice" "$((gate_off + 4))")" || { echo "cannot read descriptor container total"; return 1; }
	[ "$container" -gt 0 ] && [ "$container" -le $((8 * 1024 * 1024)) ] || {
		echo "descriptor container total $container is outside (0, 8 MiB]"; return 1; }
	[ "$(_luofu_be32 "$slice" "$((gate_off + 8))")" -eq 32 ] || { echo "descriptor +8 is not 32"; return 1; }
	[ "$(_luofu_be32 "$slice" "$((gate_off + 12))")" -eq 32 ] || { echo "descriptor +12 is not 32"; return 1; }
	[ "$(_luofu_be32 "$slice" "$((gate_off + 20))")" -eq 32 ] || { echo "descriptor +20 is not 32"; return 1; }
	count="$(_luofu_be32 "$slice" "$((gate_off + 16))")" || { echo "cannot read descriptor entry count"; return 1; }
	[ "$count" -ge 1 ] && [ "$count" -le 64 ] || { echo "descriptor entry count $count is outside [1, 64]"; return 1; }
	total=$((32 + 32 * count))
	prev_end="$total"
	i=0
	while [ "$i" -lt "$count" ]; do
		entry_off=$((gate_off + 32 + 32 * i))
		ov_size="$(_luofu_be32 "$slice" "$entry_off")" || { echo "cannot read overlay $i size"; return 1; }
		ov_off="$(_luofu_be32 "$slice" "$((entry_off + 4))")" || { echo "cannot read overlay $i offset"; return 1; }
		[ "$ov_size" -gt 0 ] && [ "$ov_size" -le 131072 ] || {
			echo "overlay $i FDT size $ov_size is outside (0, 128 KiB]"; return 1; }
		[ "$ov_off" -eq "$prev_end" ] || {
			echo "overlay $i offset $ov_off is not the canonical contiguous layout ($prev_end)"; return 1; }
		[ "$((ov_off + ov_size))" -le "$container" ] || {
			echo "overlay $i runs past the container total"; return 1; }
		ov_magic="$(_luofu_hex32 "$slice" "$((gate_off + ov_off))")"
		[ "$ov_magic" = d00dfeed ] || { echo "overlay $i is not an FDT"; return 1; }
		prev_end=$((ov_off + ov_size))
		i=$((i + 1))
	done
	[ "$container" -eq "$prev_end" ] || {
		echo "descriptor container total $container != 32 + 32*$count + overlay sizes $prev_end"; return 1; }
	[ "$((gate_off + container))" -le "$size" ] || { echo "FIP trailer runs past the slice"; return 1; }

	# fip-manifest.txt must agree with the bytes, not the other way round.
	[ -s "$manifest" ] || { echo "container has no fip-manifest.txt"; return 1; }
	[ "$(_luofu_manifest_get "$manifest" manifest-version)" = 2 ] || {
		echo "fip-manifest.txt manifest-version is not 2 (schema not frozen)"; return 1; }
	[ "$(_luofu_manifest_get "$manifest" slice-size)" = "$size" ] || {
		echo "fip-manifest.txt slice-size does not match the kernel member"; return 1; }
	command -v sha256sum >/dev/null 2>&1 || {
		echo "sha256sum is missing; cannot verify the kernel slice hash"; return 1; }
	[ "$(_luofu_manifest_get "$manifest" slice-sha256)" = "$(sha256sum "$slice" | cut -d' ' -f1)" ] || {
		echo "fip-manifest.txt slice-sha256 does not match the kernel member"; return 1; }
	[ "$(_luofu_manifest_get "$manifest" gate-magic)" = "$LUOFU_GATE_MAGIC" ] || {
		echo "fip-manifest.txt gate-magic does not match 0x$LUOFU_GATE_MAGIC"; return 1; }
	[ "$(_luofu_hexval "$(_luofu_manifest_get "$manifest" gate-offset)")" -eq "$gate_off" ] || {
		echo "fip-manifest.txt gate-offset does not match the recomputed $gate_off"; return 1; }
	return 0
}

_luofu_check_rootfs_ubi() { # <ubi-image>
	local ubi="$1"
	local size pebs vid_off data_off name_len name vol_type used_ebs i
	size="$(wc -c < "$ubi")"
	[ "$size" -le "$LUOFU_ROOTFS_SLOT_SIZE" ] || {
		echo "rootfs UBI is $size B, over the $LUOFU_ROOTFS_SLOT_SIZE B slot"; return 1; }
	[ "$((size % LUOFU_UBI_PEB_SIZE))" -eq 0 ] || {
		echo "rootfs UBI size $size is not a whole number of $LUOFU_UBI_PEB_SIZE B PEBs"; return 1; }
	pebs=$((size / LUOFU_UBI_PEB_SIZE))
	[ "$pebs" -ge 3 ] || { echo "rootfs UBI has only $pebs PEBs"; return 1; }
	[ "$(_luofu_hex32 "$ubi" 0)" = 55424923 ] || {
		echo "rootfs UBI does not start with the UBI# EC magic"; return 1; }

	vid_off="$(_luofu_be32 "$ubi" 16)" || { echo "cannot read UBI vid_hdr_offset"; return 1; }
	[ "$vid_off" -eq "$LUOFU_UBI_VID_HDR_OFFSET" ] || {
		echo "rootfs UBI vid_hdr_offset $vid_off != $LUOFU_UBI_VID_HDR_OFFSET (measured geometry)"; return 1; }
	data_off="$(_luofu_be32 "$ubi" 20)" || { echo "cannot read UBI data_offset"; return 1; }
	[ "$data_off" -ge $((LUOFU_UBI_VID_HDR_OFFSET + 64)) ] || {
		echo "rootfs UBI data_offset $data_off overlaps the VID header"; return 1; }

	# layout volume copy at PEB 0 must be the internal layout volume (LNUM 0)
	[ "$(_luofu_hex32 "$ubi" "$vid_off")" = 55424921 ] || {
		echo "rootfs UBI PEB 0 has no UBI! VID header"; return 1; }
	[ "$(_luofu_be32 "$ubi" "$((vid_off + 8))")" -eq "$(_luofu_hexval "$LUOFU_UBI_LAYOUT_VOL_ID")" ] || {
		echo "rootfs UBI PEB 0 is not the layout volume"; return 1; }
	[ "$(_luofu_be32 "$ubi" "$((vid_off + 12))")" -eq 0 ] || {
		echo "rootfs UBI PEB 0 is not layout-volume LNUM 0"; return 1; }

	# volume table record 0: static volume named "squashfs"
	name_len="$(_luofu_hex16 "$ubi" "$((data_off + 14))")"
	[ "$name_len" = 0008 ] || { echo "rootfs UBI volume table record 0 name_len is not 8"; return 1; }
	name="$(dd if="$ubi" bs=1 skip=$((data_off + 16)) count=8 2>/dev/null)"
	[ "$name" = squashfs ] || { echo "rootfs UBI volume 0 is not named squashfs"; return 1; }
	vol_type="$(_luofu_hex8 "$ubi" "$((data_off + 12))")"
	[ "$vol_type" = 02 ] || { echo "rootfs UBI volume 0 is not a static volume"; return 1; }

	# PEB 2 is the first volume LEB; its used_ebs fixes the expected PEB count.
	[ "$(_luofu_hex32 "$ubi" $((2 * LUOFU_UBI_PEB_SIZE + vid_off)))" = 55424921 ] || {
		echo "rootfs UBI PEB 2 has no UBI! VID header"; return 1; }
	[ "$(_luofu_be32 "$ubi" "$((2 * LUOFU_UBI_PEB_SIZE + vid_off + 8))")" -eq 0 ] || {
		echo "rootfs UBI PEB 2 is not volume 0"; return 1; }
	used_ebs="$(_luofu_be32 "$ubi" "$((2 * LUOFU_UBI_PEB_SIZE + vid_off + 24))")" || {
		echo "cannot read rootfs UBI used_ebs"; return 1; }
	[ "$used_ebs" -ge 1 ] || { echo "rootfs UBI volume 0 has used_ebs 0"; return 1; }
	[ "$pebs" -eq $((2 + used_ebs)) ] || {
		echo "rootfs UBI has $pebs PEBs, expected 2 layout + $used_ebs volume"; return 1; }

	# every PEB must carry both UBI headers (header CRCs and data CRCs are
	# verified by the host build tool; TODO: add the busybox crc32 path here
	# once the on-device applet set is confirmed)
	i=0
	while [ "$i" -lt "$pebs" ]; do
		[ "$(_luofu_hex32 "$ubi" "$((i * LUOFU_UBI_PEB_SIZE))")" = 55424923 ] || {
			echo "rootfs UBI PEB $i has no UBI# EC magic"; return 1; }
		[ "$(_luofu_hex32 "$ubi" "$((i * LUOFU_UBI_PEB_SIZE + vid_off))")" = 55424921 ] || {
			echo "rootfs UBI PEB $i has no UBI! VID header"; return 1; }
		i=$((i + 1))
	done
	return 0
}

platform_check_image() {
	# The whole body runs in a subshell so its temporary directory and trap
	# cannot leak into sysupgrade; the subshell's exit status is the return
	# value (74 is the image-validation "broken" code the framework reads).
	(
	local image="$1"
	local tmpdir members board_dir base status
	[ -f "$image" ] || { echo "image not found: $image"; return 1; }
	tmpdir="$(mktemp -d /tmp/luofu-check.XXXXXX)" || {
		echo "cannot create a temporary directory"; return 1; }
	trap 'rm -rf "$tmpdir"' EXIT

	# 1. the container must be a sysupgrade tar with exactly one board dir
	if ! get_image "$image" cat | tar tf - > "$tmpdir/members" 2>"$tmpdir/tar.err"; then
		echo "image is not a tar container (foreign or vendor image; refused)"
		return 74
	fi
	board_dir="$(grep -m 1 '^sysupgrade-[A-Za-z0-9._-]*/$' "$tmpdir/members")"
	[ -n "$board_dir" ] || {
		echo "tar container has no sysupgrade-*/ directory (foreign image; refused)"
		return 74
	}
	board_dir="${board_dir%/}"
	[ "$(grep -c '^sysupgrade-[A-Za-z0-9._-]*/$' "$tmpdir/members")" -eq 1 ] || {
		echo "tar container has more than one sysupgrade-*/ directory"; return 1; }
	[ "$board_dir" = "sysupgrade-$LUOFU_BOARD_NAME" ] || {
		echo "container board directory is $board_dir, not sysupgrade-$LUOFU_BOARD_NAME (foreign image; refused)"
		return 74
	}
	for base in CONTROL fip-manifest.txt INSTALL-STATUS.txt kernel root; do
		grep -qx "$board_dir/$base" "$tmpdir/members" || {
			echo "container is missing $board_dir/$base"; return 1; }
	done
	_luofu_tar_member "$image" "$board_dir/CONTROL" "$tmpdir/CONTROL" || return 1
	_luofu_tar_member "$image" "$board_dir/fip-manifest.txt" "$tmpdir/manifest" || return 1
	_luofu_tar_member "$image" "$board_dir/INSTALL-STATUS.txt" "$tmpdir/status" || return 1
	_luofu_tar_member "$image" "$board_dir/kernel" "$tmpdir/kernel" || return 1
	_luofu_tar_member "$image" "$board_dir/root" "$tmpdir/root" || return 1

	# 2. CONTROL identity
	grep -qx "BOARD=$LUOFU_BOARD_NAME" "$tmpdir/CONTROL" || {
		echo "CONTROL BOARD does not match $LUOFU_BOARD_NAME"; return 1; }
	grep -qx "TARGET=hisilicon/luofu" "$tmpdir/CONTROL" || {
		echo "CONTROL TARGET does not match hisilicon/luofu"; return 1; }
	grep -q 'NOT FLASHABLE' "$tmpdir/status" || {
		echo "container does not carry the NOT FLASHABLE install status"; return 1; }

	# 3. kernel slice structure + manifest against the received bytes
	_luofu_check_uimage_slice "$tmpdir/kernel" "$tmpdir/manifest" || return 1

	# 4. rootfs UBI shape
	_luofu_check_rootfs_ubi "$tmpdir/root" || return 1

	echo "hisilicon/luofu: image check passed (structural only; the A/B install path writes the inactive pair, never the active one)"
	return 0
	)
}

# ===========================================================================
# A/B install path (platform_do_upgrade)
# ===========================================================================
#
# Everything below runs from /lib/upgrade/do_stage2 inside the sysupgrade
# ramfs, so only binaries that switch_to_ramfs() copies are available. The
# extra RAMFS_COPY_BIN entries are needed by the re-run of platform_check_image
# (mktemp, sha256sum) and by the kernel read-back hash (head); they are busybox
# applet symlinks, not second copies. Tool availability is still checked
# explicitly so a missing tool fails closed instead of writing half a pair.
RAMFS_COPY_BIN="${RAMFS_COPY_BIN:-} mktemp sha256sum head"

# /proc/mtd on the R116 reports rootfsa/rootfsb as 0x1740000 = 24,379,392 B
# (parent dumps/router-20261108/full/proc_mtd.txt). The image recipe's
# LUOFU_ROOTFS_SLOT_SIZE (24,313,856 B) is the container/UBI budget INSIDE that
# partition, not the partition size; the write path checks the partition
# against the measured size and each container member against the budget.
LUOFU_ROOTFS_PART_SIZE=24379392

# Host-side test hook: unset (empty prefix) on the device. The mocked harness
# (tools/test-platform-abinstall.sh) exports it as a directory tree so the
# /proc, /sys and /dev lookups resolve inside a fixture; the commands it runs
# are shimmed on PATH. The image path is never relocated by this.
LUOFU_TEST_ROOT="${LUOFU_TEST_ROOT:-}"
LUOFU_MTD_TABLE="$LUOFU_TEST_ROOT/proc/mtd"
LUOFU_UBI_SYS="$LUOFU_TEST_ROOT/sys/class/ubi"
# Selector platform device name: boot-selector (the devicetree node name and
# the path observed on the unit; see the console record in the parent ledger
# 2026-11-23).  luofu-selector is the earlier draft/fixture name and is only
# accepted as a fallback by _luofu_selector_dir().
LUOFU_SELECTOR_BASE="$LUOFU_TEST_ROOT/sys/devices/platform"
LUOFU_DEV_DIR="$LUOFU_TEST_ROOT/dev"
LUOFU_WORK_DIR="$LUOFU_TEST_ROOT/tmp/luofu-upgrade"

_luofu_err() {
	echo "hisilicon/luofu: $*" >&2
}

_luofu_warn() {
	echo "hisilicon/luofu: WARNING: $*" >&2
}

# The image's boot pair recorder re-records the running pair after every
# successful boot; it is what keeps the selector record and the booted pair
# in agreement (and what stops a normal reboot from taking the loader's
# failover path). Checked as a warning only: refusals are unchanged.
_luofu_recorder_present() {
	[ -e "$LUOFU_TEST_ROOT/etc/init.d/luofu_record_pair" ] ||
		[ -e /etc/init.d/luofu_record_pair ]
}

# Exactly one /proc/mtd entry for a label -> "<mtdN> <size-hex>". More than
# one match is treated as no match: the label must be unambiguous.
_luofu_mtd_lookup() { # <label>
	awk -v want="\"$1\"" '
		$1 ~ /^mtd[0-9]+:$/ {
			name = $4
			for (i = 5; i <= NF; i++)
				name = name " " $i
			if (name == want) {
				print substr($1, 1, length($1) - 1), $2
				found++
			}
		}
		END { exit(found == 1 ? 0 : 1) }
	' "$LUOFU_MTD_TABLE"
}

# Exactly one /proc/mtd entry for "<mtdN>" -> unquoted partition label.
_luofu_mtd_label() { # <mtdN>
	local label

	label="$(awk -v want="$1:" '
		$1 == want {
			name = $4
			for (i = 5; i <= NF; i++)
				name = name " " $i
			print name
			found++
		}
		END { exit(found == 1 ? 0 : 1) }
	' "$LUOFU_MTD_TABLE")" || return 1
	label="${label#\"}"
	label="${label%\"}"
	[ -n "$label" ] || return 1
	echo "$label"
}

_luofu_mtd_size() { # <8-hex-digit size> -> decimal
	case "$1" in
	''|*[!0-9a-fA-F]*) return 1 ;;
	esac
	echo "$((0x$1))"
}

# Resolve the selector sysfs directory (patches-6.18/017 is the only accepted
# read/write path for the SYSCTRL boot-selector cell).  boot-selector is the
# devicetree node name and the hardware-observed path; luofu-selector is
# accepted only for the mocked harness / earlier drafts.
_luofu_selector_dir() {
	local name

	for name in boot-selector luofu-selector; do
		if [ -r "$LUOFU_SELECTOR_BASE/$name/current_pair" ]; then
			echo "$LUOFU_SELECTOR_BASE/$name"
			return 0
		fi
	done
	return 1
}

_luofu_selector_read() {
	local pair dir

	dir="$(_luofu_selector_dir)" || return 1
	read -r pair _ < "$dir/current_pair" 2>/dev/null || return 1
	[ -n "$pair" ] || return 1
	echo "$pair"
}

_luofu_selector_write() { # <a|b>
	local want="$1" got dir

	dir="$(_luofu_selector_dir)" || return 1
	printf '%s\n' "$want" > "$dir/next_pair" 2>/dev/null || return 1
	got="$(_luofu_selector_read)" || return 1
	[ "$got" = "$want" ] || return 1
	return 0
}

# The pair this system really booted from, via the UBI rootfs device: the same
# witness the bench slot gate used (ubi0's mtd_num cross-checked against that
# mtd's /proc/mtd label). /proc/cmdline is deliberately not used - the measured
# boot carries no slot token.
_luofu_booted_rootfs_pair() {
	local mtdnum label pair

	[ -r "$LUOFU_UBI_SYS/ubi0/mtd_num" ] || return 1
	read -r mtdnum _ < "$LUOFU_UBI_SYS/ubi0/mtd_num" 2>/dev/null || return 1
	case "$mtdnum" in
	''|*[!0-9]*) return 1 ;;
	esac
	label="$(_luofu_mtd_label "mtd$mtdnum")" || return 1
	case "$label" in
	rootfsa) pair=a ;;
	rootfsb) pair=b ;;
	*) return 1 ;;
	esac
	echo "$pair"
}

_luofu_active_pair() {
	local selector_pair rootfs_pair

	selector_pair="$(_luofu_selector_read)" || {
		_luofu_err "no boot-selector interface at $LUOFU_SELECTOR_BASE/boot-selector/current_pair (CONFIG_LUOFU_SELECTOR and the devicetree node are required); refusing"
		return 1
	}
	case "$selector_pair" in
	a|b) ;;
	*)
		_luofu_err "boot selector state '$selector_pair' is not one of the two established recorded pairs (0x10=a, 0x11=b); 0x21 manual/recovery and 0x00 un-initialised are not accepted for an automated handover; refusing"
		return 1
		;;
	esac

	rootfs_pair="$(_luofu_booted_rootfs_pair)" || {
		_luofu_err "cannot identify the booted rootfs slot (ubi0/mtd_num missing or its /proc/mtd label is not rootfsa/rootfsb); refusing"
		return 1
	}
	[ "$selector_pair" = "$rootfs_pair" ] || {
		_luofu_err "boot selector says pair $selector_pair but this system booted rootfs$rootfs_pair; refusing (stale or partial handover)"
		return 1
	}
	echo "$rootfs_pair"
}

_luofu_other_pair() {
	[ "$1" = a ] && echo b || echo a
}

# Resolve one pair's kernel and rootfs partitions and check the measured
# sizes. Prints "<kernel-dev> <kernel-size> <rootfs-dev> <rootfs-size>".
_luofu_pair_slots() { # <pair>
	local pair="$1" kpart rpart kdev ksize rdev rsize

	kpart="$(_luofu_mtd_lookup "kernel$pair")" || {
		_luofu_err "no unique /proc/mtd partition named kernel$pair; refusing"
		return 1
	}
	kdev="$LUOFU_DEV_DIR/${kpart%% *}"
	ksize="$(_luofu_mtd_size "${kpart#* }")" || {
		_luofu_err "cannot read the size of kernel$pair from /proc/mtd; refusing"
		return 1
	}
	rpart="$(_luofu_mtd_lookup "rootfs$pair")" || {
		_luofu_err "no unique /proc/mtd partition named rootfs$pair; refusing"
		return 1
	}
	rdev="$LUOFU_DEV_DIR/${rpart%% *}"
	rsize="$(_luofu_mtd_size "${rpart#* }")" || {
		_luofu_err "cannot read the size of rootfs$pair from /proc/mtd; refusing"
		return 1
	}

	[ "$ksize" -eq "$LUOFU_KERNEL_SLOT_SIZE" ] || {
		_luofu_err "kernel$pair is $ksize B, not the measured $LUOFU_KERNEL_SLOT_SIZE B kernel slot; refusing"
		return 1
	}
	[ "$rsize" -eq "$LUOFU_ROOTFS_PART_SIZE" ] || {
		_luofu_err "rootfs$pair is $rsize B, not the measured $LUOFU_ROOTFS_PART_SIZE B rootfs partition; refusing"
		return 1
	}

	echo "$kdev $ksize $rdev $rsize"
}

# md5 of the first <size> bytes of a device. The read is block-aligned and
# truncated with head, so it works against a /dev/mtdN char device and against
# the mocked regular-file devices.
_luofu_region_md5() { # <device> <size>
	local dev="$1" size="$2" blocks

	blocks=$(( (size + LUOFU_UBI_PEB_SIZE - 1) / LUOFU_UBI_PEB_SIZE ))
	dd if="$dev" bs="$LUOFU_UBI_PEB_SIZE" count="$blocks" 2>/dev/null |
		head -c "$size" | md5sum | cut -d' ' -f1
}

platform_do_upgrade() {
	local image="$1"
	local workdir="$LUOFU_WORK_DIR"
	local active target klabel rlabel slots
	local kdev ksize rdev rsize
	local kfile rfile kbytes rbytes kmd5 kmd5_read
	local magic vid

	# The framework calls platform_check_image before stage 2, but this
	# function re-runs the full byte-level check: a direct do_stage2
	# invocation must not write an unvalidated container.
	platform_check_image "$image" || {
		_luofu_err "container validation failed again inside platform_do_upgrade; nothing was written"
		return 1
	}

	command -v mtd >/dev/null 2>&1 &&
		command -v ubiformat >/dev/null 2>&1 &&
		command -v md5sum >/dev/null 2>&1 &&
		command -v head >/dev/null 2>&1 || {
		_luofu_err "the upgrade tools (mtd, ubiformat, md5sum, head) are missing from the upgrade environment; refusing (the image needs the ubi-utils package)"
		return 1
	}

	active="$(_luofu_active_pair)" || return 1
	target="$(_luofu_other_pair "$active")"

	_luofu_recorder_present || _luofu_warn "no boot pair recorder at /etc/init.d/luofu_record_pair on this system; the post-boot pair recording is what keeps the selector stable - do not hand over to a build without it"
	klabel="kernel$target"
	rlabel="rootfs$target"

	slots="$(_luofu_pair_slots "$target")" || return 1
	set -- $slots
	kdev="$1"
	ksize="$2"
	rdev="$3"
	rsize="$4"

	[ -e "$kdev" ] || { _luofu_err "$kdev (partition $klabel) does not exist; refusing"; return 1; }
	[ -e "$rdev" ] || { _luofu_err "$rdev (partition $rlabel) does not exist; refusing"; return 1; }
	[ -r "$kdev" ] && [ -w "$kdev" ] || {
		_luofu_err "$kdev (partition $klabel) is not readable and writable; refusing (a read-only slot cannot take the kernel)"; return 1; }
	[ -r "$rdev" ] && [ -w "$rdev" ] || {
		_luofu_err "$rdev (partition $rlabel) is not readable and writable; refusing (a read-only slot cannot take the rootfs)"; return 1; }

	v "hisilicon/luofu: A/B install: booted pair $active (selector and booted rootfs agree), writing the inactive pair $target"
	v "hisilicon/luofu: kernel $klabel = $kdev ($ksize B), rootfs $rlabel = $rdev ($rsize B)"

	# The work directory lives in the upgrade ramfs; it is removed at the end
	# of a successful write and thrown away with the ramfs on a failed one.
	rm -rf "$workdir"
	mkdir -p "$workdir" || { _luofu_err "cannot create $workdir; refusing"; return 1; }

	_luofu_tar_member "$image" "sysupgrade-$LUOFU_BOARD_NAME/kernel" "$workdir/kernel" || {
		_luofu_err "cannot extract the kernel member from the container; refusing"; return 1; }
	_luofu_tar_member "$image" "sysupgrade-$LUOFU_BOARD_NAME/root" "$workdir/root" || {
		_luofu_err "cannot extract the root member from the container; refusing"; return 1; }
	kfile="$workdir/kernel"
	rfile="$workdir/root"

	kbytes="$(wc -c < "$kfile")"
	rbytes="$(wc -c < "$rfile")"
	[ "$kbytes" -gt 0 ] && [ "$kbytes" -le "$ksize" ] || {
		_luofu_err "kernel member is $kbytes B, does not fit the $ksize B $klabel slot; refusing"; return 1; }
	# The rootfs member is bounded by the image budget (24,313,856 B, the
	# same value platform_check_image enforces), not by the whole 24,379,392 B
	# partition: the recipe's UBI layout only guarantees the former.
	[ "$rbytes" -gt 0 ] && [ "$rbytes" -le "$LUOFU_ROOTFS_SLOT_SIZE" ] || {
		_luofu_err "rootfs member is $rbytes B, over the $LUOFU_ROOTFS_SLOT_SIZE B rootfs image budget; refusing"; return 1; }
	[ "$(_luofu_hex32 "$kfile" 0)" = 27051956 ] || {
		_luofu_err "kernel member does not start with the legacy uImage magic; refusing"; return 1; }
	[ "$(_luofu_hex32 "$rfile" 0)" = 55424923 ] || {
		_luofu_err "rootfs member does not start with the UBI# magic; refusing"; return 1; }

	# --- kernel slot: erase+write, then verify the read-back md5 -----------
	v "hisilicon/luofu: writing the $kbytes B kernel slice to $klabel"
	mtd write "$kfile" "$kdev" || {
		_luofu_err "mtd write to $klabel failed; the selector still names pair $active, which remains intact - reboot and retry"
		return 1
	}
	kmd5="$(md5sum "$kfile" | cut -d' ' -f1)"
	kmd5_read="$(_luofu_region_md5 "$kdev" "$kbytes")"
	[ -n "$kmd5" ] && [ "$kmd5" = "$kmd5_read" ] || {
		_luofu_err "kernel read-back md5 mismatch on $klabel ($kmd5_read != $kmd5); the selector still names pair $active - refusing the handover"
		return 1
	}
	v "hisilicon/luofu: kernel $klabel read back byte-identical (md5 $kmd5)"

	# --- rootfs slot: ubiformat the UBI image, then re-read PEB 0 ----------
	v "hisilicon/luofu: ubiformat the $rbytes B rootfs UBI image to $rlabel"
	ubiformat "$rdev" -y -f "$rfile" || {
		_luofu_err "ubiformat of $rlabel failed; the selector still names pair $active, which remains intact - reboot and retry"
		return 1
	}
	magic="$(_luofu_hex32 "$rdev" 0)"
	vid="$(_luofu_hex32 "$rdev" "$LUOFU_UBI_VID_HDR_OFFSET")"
	[ "$magic" = 55424923 ] && [ "$vid" = 55424921 ] || {
		_luofu_err "rootfs read-back on $rlabel lost the UBI headers (EC 0x$magic, VID 0x$vid); the selector still names pair $active - refusing the handover"
		return 1
	}
	v "hisilicon/luofu: rootfs $rlabel read back with UBI#/UBI! headers at PEB 0"

	# --- last step: durable writes, then move the selector -----------------
	sync
	_luofu_selector_write "$target" || {
		_luofu_err "could not move the boot selector to pair $target (the next_pair store or its read-back failed); restoring pair $active"
		_luofu_selector_write "$active" ||
			_luofu_err "could not restore the selector either - check $LUOFU_SELECTOR_BASE/boot-selector/current_pair before rebooting"
		return 1
	}
	sync
	rm -rf "$workdir"

	v "hisilicon/luofu: A/B install complete: pair $target written and selected (selector reads $target); the framework reboots into it"
	return 0
}
