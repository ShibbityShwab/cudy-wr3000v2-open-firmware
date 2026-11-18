# SPDX-License-Identifier: GPL-2.0-only
#
# platform.sh - OpenWrt sysupgrade platform hooks for the HiSilicon luofu R116
# (Cudy WR3000 v2.0) target.
#
# STATUS: SKELETON / FAIL-CLOSED / NEVER RUN ON HARDWARE.
#
# platform_check_image() is real: it reads the container built by
# image/luofu.mk and re-derives, from the received bytes only, every structural
# field the recipe claimed (tar layout, CONTROL, uImage header, FIP trailer and
# gate magic, descriptor constants, rootfs UBI volume shape, fip-manifest.txt
# cross-check, slot budgets). It refuses anything else, including a vendor
# recovery.bin.
#
# platform_do_upgrade() REFUSES to write. The A/B boot-selector protocol
# (sysenv cell 0x10100C08) and the install path are [U] (TARGET-DESIGN.md 3.4,
# 5.4, 5.5):
#   - the exact A-side handover value is not established and the older
#     "plain fw_setenv works" assumption is wrong;
#   - the environment-block writer (crc32-LE | flags | data, both enva/envb)
#     has never been validated as a Linux-side write;
#   - no path has been executed end to end on a recovered unit.
# Writing the inactive kernel/rootfs pair without the selector handover would
# at best install an unbootable slot and at worst overwrite the bootable pair
# after a later handover. So this function does nothing but refuse.
#
# WHAT platform_do_upgrade MUST DO BEFORE IT IS ENABLED (TARGET-DESIGN.md 5.4):
#   1. read the active pair from the loader's bootreg/sysenv state (exact Linux
#      read path is [U]) - never assume, and never use a bare MTD number;
#   2. write ONLY the inactive pair, located by /proc/mtd label
#      (kernela+kernelb / rootfsa+rootfsb), kernel slot via mtd write, rootfs
#      via ubiformat of the UBI image;
#   3. read back a hash of both written regions and compare with the container;
#   4. update BOTH environment copies and the boot selector with the MEASURED
#      recipe, then reboot;
#   5. keep rootfs_data (JFFS2 overlay) unless -n; the two slots share it, so an
#      upgrade is not atomic across the overlay;
#   6. never use U-Boot's TFTP fipupdate_eth/rootfsupdate_eth commands (they
#      overwrite both slots);
#   7. rollback is by switching back to the other pair, NOT by relying on a
#      "slot failed" status word (that older claim is not supported by the
#      2026-11-16 captures).
# Only after one attended A/B validation may platform_do_upgrade be implemented.

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

	echo "hisilicon/luofu: image check passed (structural only; A/B install path [U], do not flash)"
	return 0
	)
}

platform_do_upgrade() {
	# FAIL-CLOSED BY DESIGN - see the header for the full TODO list.
	echo "hisilicon/luofu: REFUSING to upgrade: the A/B boot-selector protocol" >&2
	echo "  (sysenv 0x10100C08) and the install path are NOT validated [U]" >&2
	echo "  (TARGET-DESIGN.md 3.4, 5.4, 5.5). Nothing has been written." >&2
	echo "  Do not enable this path until one attended A/B validation has run." >&2
	return 1
}
