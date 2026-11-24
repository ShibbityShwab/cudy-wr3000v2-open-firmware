#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# test-platform-abinstall.sh - host-side mocked tests for the luofu A/B
# sysupgrade install path and the boot-time pair recorder.
#
# STATUS: host-only. No router, UART, TFTP, flash or bench tool is touched.
#
# WHAT THIS EXERCISES
#   The real target/linux/hisilicon/base-files/lib/upgrade/platform.sh is
#   sourced in a subshell per scenario and platform_do_upgrade is called
#   against a fixture that fakes everything the function touches:
#
#     /proc/mtd                                  -> fixture file (stock 17 slots)
#     /sys/class/ubi/ubi0/mtd_num                -> fixture file (booted rootfs)
#     /sys/devices/platform/boot-selector/*     -> fixture files (the driver ABI)
#     /proc/cmdline                              -> fixture file (optional slot token)
#     /etc/init.d/luofu_record_pair              -> fixture copy of the real recorder
#     mtd, ubiformat, sync, reboot               -> shims on PATH that log each
#                                                   call and write image bytes
#                                                   into fake /dev/mtdN files
#
#   The second half runs the real boot-time recorder
#   target/linux/hisilicon/base-files/etc/init.d/luofu_record_pair in a POSIX
#   shell against the same fixture root (mocked ubi0/mtd_num, /proc/mtd,
#   /proc/cmdline and selector sysfs): the core-utils `sh` on the host and the
#   busybox ash on the device share the syntax the script uses.
#
#   platform.sh exposes the LUOFU_TEST_ROOT hook for exactly this purpose; on
#   the device it is unset and every path is absolute. The image is a real tar
#   container with the expected sysupgrade-<board>/ directory and the member
#   magics, or a real built sysupgrade.bin supplied with --container (then the
#   real platform_check_image runs too, not the stub).
#
# INSTALL-PATH SCENARIOS
#   1. A boot  -> writes kernelb+rootfsb, selector b, kernel write first
#   2. B boot  -> writes kernela+rootfsa, selector a
#   3. selector 0x21 manual-b        -> refuse, no write
#   4. selector/rootfs pair mismatch -> refuse, no write
#   5. unknown selector value        -> refuse, no write
#   6. no selector interface         -> refuse, no write
#   7. kernel member over the slot   -> refuse, no write
#   8. rootfs member over the slot   -> refuse, no write
#   9. foreign image (real check)    -> refuse, no write
#  10. kernel slot write fails       -> refuse, selector untouched, no ubiformat
#  11. rootfs slot write fails       -> refuse, selector untouched
#  12. selector store fails          -> refuse, selector stays on the active pair
#  13. platform_check_image fails    -> refuse, no write
#  14. no boot pair recorder         -> install proceeds, warns (refusals unchanged)
#  15. real built container          -> only with --container; full path, rc 0
#
# RECORDER SCENARIOS (the boot-time record-running-pair service)
#  16. A boot, cell records b        -> writes a, read-back verified; a second
#                                       run is a no-op (idempotent assertion)
#  17. B boot, cell records a        -> writes b
#  18. running pair already recorded -> no write (read-only next_pair proves it)
#  19. cell manual-b                 -> no write, fails safe
#  20. cell unknown                  -> no write, fails safe
#  21. no selector interface         -> no write, bounded wait, fails safe
#  22. no ubi0/mtd_num               -> no write, fails safe
#  23. mtd label is not rootfsa/b    -> no write, fails safe
#  24. next_pair store fails         -> reports failure, cell unchanged
#  25. read-back does not match      -> reports failure, no false success
#  26. cmdline disagrees with ubi0   -> no write, fails safe
#  27. cmdline agrees (ubi.mtd=N)    -> writes the running pair
#
# EXIT: 0 every scenario passed, 1 at least one failed.
#
# USAGE:
#   tools/test-platform-abinstall.sh [--container PATH] [--platform PATH] [--keep]
#
#   --container  a built ...sysupgrade.bin; adds scenario 14 and runs the real
#                platform_check_image (host-built artifact; optional)
#   --platform   alternate platform.sh path (default: the repository one)
#   --keep       keep the fixture directory for inspection

set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
default_platform="$SCRIPT_DIR/../target/linux/hisilicon/base-files/lib/upgrade/platform.sh"
RECORDER="$SCRIPT_DIR/../target/linux/hisilicon/base-files/etc/init.d/luofu_record_pair"
container=""
platform_path="$default_platform"
keep=0

while [ $# -gt 0 ]; do
	case "$1" in
	--container) container="$2"; shift 2 ;;
	--platform) platform_path="$2"; shift 2 ;;
	--keep) keep=1; shift ;;
	-h|--help)
		sed -n '2,60p' "$0"
		exit 0
		;;
	*)
		echo "unknown argument: $1" >&2
		exit 2
		;;
	esac
done

[ -f "$platform_path" ] || {
	echo "platform.sh not found: $platform_path" >&2
	exit 2
}
[ -f "$RECORDER" ] || {
	echo "recorder not found: $RECORDER" >&2
	exit 2
}

ORIG_PATH="$PATH"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/luofu-abinstall-test.XXXXXX") || exit 1
cleanup() {
	[ "$keep" = 1 ] && echo "fixtures kept in $TMP" >&2 && return
	rm -rf "$TMP"
}
trap cleanup EXIT

PASS=0
FAIL=0
CASE=""
CMDLOG=""
CASE_RC=""

note() { printf '  # %s\n' "$*"; }
pass() { PASS=$((PASS + 1)); printf 'ok %d - %s\n' "$PASS" "$*"; }
fail() { FAIL=$((FAIL + 1)); printf 'not ok %d - %s\n' "$FAIL" "$*"; }

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

check_match() { # <description> <pattern> <text-file>
	if grep -Eq -- "$2" "$3"; then
		pass "$1"
	else
		fail "$1: pattern [$2] not in $3"
		note "output was:"
		sed -n '1,12p' "$3" | sed 's/^/    /'
	fi
}

check_no_match() { # <description> <pattern> <text-file>
	if grep -Eq -- "$2" "$3"; then
		fail "$1: pattern [$2] unexpectedly found in $3"
	else
		pass "$1"
	fi
}

log_line() { # <fixed string> -> first command-log line number or empty
	grep -n -F -- "$1" "$CMDLOG" 2>/dev/null | head -n 1 | cut -d: -f1
}

count_log() { # <basic regex>
	grep -Ec -- "$1" "$CMDLOG" 2>/dev/null || true
}

selector_value() {
	cat "$CASE/root/sys/devices/platform/boot-selector/current_pair" 2>/dev/null
}

# Write the PATH shims for mtd/ubiformat/sync/reboot.
write_shims() {
	cat > "$CASE/bin/mtd" <<'SHIM'
#!/bin/sh
echo "mtd $*" >> "$LUOFU_CMDLOG"
cmd="$1"
shift
case "$cmd" in
write)
	src="$1"
	dev="$2"
	if [ -n "${LUOFU_MOCK_ROMTD:-}" ] && [ "$dev" = "$LUOFU_MOCK_ROMTD" ]; then
		echo "mtd: $dev: write refused (read-only partition fixture)" >&2
		exit 1
	fi
	cp "$src" "$dev" || exit 1
	;;
verify)
	src="$1"
	dev="$2"
	cmp "$src" "$dev" || exit 1
	;;
*)
	echo "mtd: unsupported fixture command: $cmd" >&2
	exit 2
	;;
esac
SHIM
	chmod +x "$CASE/bin/mtd"

	cat > "$CASE/bin/ubiformat" <<'SHIM'
#!/bin/sh
echo "ubiformat $*" >> "$LUOFU_CMDLOG"
dev="$1"
shift
file=""
while [ $# -gt 0 ]; do
	case "$1" in
	-f) file="$2"; shift 2 ;;
	*) shift ;;
	esac
done
[ -n "$file" ] || { echo "ubiformat: fixture saw no -f image" >&2; exit 2; }
if [ -n "${LUOFU_MOCK_ROROOT:-}" ] && [ "$dev" = "$LUOFU_MOCK_ROROOT" ]; then
	echo "ubiformat: $dev: format refused (read-only partition fixture)" >&2
	exit 1
fi
cp "$file" "$dev" || exit 1
SHIM
	chmod +x "$CASE/bin/ubiformat"

	cat > "$CASE/bin/sync" <<'SHIM'
#!/bin/sh
echo "sync" >> "$LUOFU_CMDLOG"
SHIM
	chmod +x "$CASE/bin/sync"

	cat > "$CASE/bin/reboot" <<'SHIM'
#!/bin/sh
echo "reboot" >> "$LUOFU_CMDLOG"
echo "reboot: platform_do_upgrade must not reboot (the framework does)" >&2
exit 1
SHIM
	chmod +x "$CASE/bin/reboot"

	# The recorder logs through logger(1); the shim appends to a known file so
	# the tests can assert on the service's decisions without relying on the
	# host's syslog.
	cat > "$CASE/bin/logger" <<'SHIM'
#!/bin/sh
echo "logger: $*" >> "$LUOFU_TEST_LOG"
SHIM
	chmod +x "$CASE/bin/logger"
}

# Fixture: stock 17-slot partition map, booted rootfs slot, selector state.
new_case() { # <name> [<selector>] [<ubi-mtd-num>]
	local name="$1"
	local selector="${2:-a}"
	local ubi_mtd="${3:-13}"

	CASE="$TMP/$name"
	CMDLOG="$CASE/log/commands"
	mkdir -p "$CASE/root/proc" \
		"$CASE/root/sys/class/ubi/ubi0" \
		"$CASE/root/sys/devices/platform/boot-selector" \
		"$CASE/root/etc/init.d" \
		"$CASE/root/dev" "$CASE/root/tmp" \
		"$CASE/bin" "$CASE/log" "$CASE/img"
	: > "$CMDLOG"
	# The boot pair recorder is part of the image; platform.sh only checks for
	# its presence (warning only), and the recorder scenarios run it below.
	cp "$RECORDER" "$CASE/root/etc/init.d/luofu_record_pair"
	: > "$CASE/root/proc/cmdline"

	cat > "$CASE/root/proc/mtd" <<'MTD'
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

	echo "$ubi_mtd" > "$CASE/root/sys/class/ubi/ubi0/mtd_num"
	echo "$selector" > "$CASE/root/sys/devices/platform/boot-selector/current_pair"
	ln -s current_pair "$CASE/root/sys/devices/platform/boot-selector/next_pair"

	local n
	for n in 11 12 13 14 15 16; do
		: > "$CASE/root/dev/mtd$n"
	done

	write_shims
}

# Container fixture: a tar with the expected board dir and member magics.
# <kernel-size> <root-size> are the member lengths; the first four bytes are
# the uImage magic and the UBI# magic, plus UBI! at the VID header offset.
make_container() { # <case> <kernel-size> <root-size> -> path
	local case_dir="$1"
	local ksize="$2"
	local rsize="$3"
	local dir="$case_dir/img/sysupgrade-cudy_wr3000-v2"
	local out="$case_dir/img/sysupgrade.bin"

	mkdir -p "$dir"
	dd if=/dev/zero of="$dir/kernel" bs=1024 count=$(( (ksize + 1023) / 1024 )) 2>/dev/null
	truncate -s "$ksize" "$dir/kernel"
	printf '\047\005\031\126' | dd of="$dir/kernel" bs=4 count=1 conv=notrunc 2>/dev/null

	dd if=/dev/zero of="$dir/root" bs=1024 count=$(( (rsize + 1023) / 1024 )) 2>/dev/null
	truncate -s "$rsize" "$dir/root"
	printf '\125\102\111\043' | dd of="$dir/root" bs=4 count=1 conv=notrunc 2>/dev/null
	if [ "$rsize" -gt 2048 ]; then
		printf '\125\102\111\041' | dd of="$dir/root" bs=1 count=4 seek=2048 conv=notrunc 2>/dev/null
	fi

	tar -C "$case_dir/img" -cf "$out" "sysupgrade-cudy_wr3000-v2"
	echo "$out"
}

# Run platform_do_upgrade in a clean subshell: fresh PATH, the fixture root and
# a fresh source of platform.sh. <image> <check-mode>
#   check-mode: real | stub-ok | stub-refuse
run_do_upgrade() {
	local image="$1"
	local mode="$2"
	local out="$CASE/log/out.log"

	(
		export VERBOSE=1
		export LUOFU_TEST_ROOT="$CASE/root"
		export LUOFU_CMDLOG="$CMDLOG"
		export PATH="$CASE/bin:$ORIG_PATH"
		export LUOFU_MOCK_ROMTD="${LUOFU_MOCK_ROMTD:-}"
		export LUOFU_MOCK_ROROOT="${LUOFU_MOCK_ROROOT:-}"
		v() { echo "upgrade: $*" >&2; }
		get_image() {
			local from="$1"
			local cmd="$2"
			[ -n "$cmd" ] || cmd="cat"
			$cmd < "$from"
		}
		. "$platform_path"
		case "$mode" in
		real) : ;;
		stub-ok)
			platform_check_image() { echo "stub-check-ok" >> "$CMDLOG"; return 0; }
			;;
		stub-refuse)
			platform_check_image() { echo "stub-check-refuse" >> "$CMDLOG"; return 74; }
			;;
		*)
			echo "bad check mode: $mode" >&2
			exit 2
			;;
		esac
		platform_do_upgrade "$image"
	) > "$out" 2>&1
	CASE_RC=$?
	echo "$CASE_RC" > "$CASE/log/rc"
}


# Run the real boot-time recorder in a POSIX shell against the fixture root.
# <wait-seconds>: bounded selector wait, 0 keeps the tests instant.
run_recorder() {
	local wait="${1:-0}"
	local out="$CASE/log/recorder.log"

	(
		export LUOFU_TEST_ROOT="$CASE/root"
		export LUOFU_TEST_LOG="$out"
		export LUOFU_RECORD_WAIT="$wait"
		export PATH="$CASE/bin:$ORIG_PATH"
		env sh -c '. "$1"; start' sh "$RECORDER"
	) > "$out" 2>&1
	CASE_RC=$?
	echo "$CASE_RC" > "$CASE/log/recorder.rc"
}

# Set the fixture kernel command line (empty file = no slot token, the
# measured stock case).
cmdline_set() {
	printf '%s\n' "$1" > "$CASE/root/proc/cmdline"
}

# Make the selector next_pair unwritable for the fixture user: a recorder
# that tried to store would fail, so this proves a "no write" decision.
selector_freeze() {
	rm -f "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	printf '%s\n' "$1" > "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	chmod 444 "$CASE/root/sys/devices/platform/boot-selector/next_pair"
}

# ---------------------------------------------------------------------------
# Scenarios
# ---------------------------------------------------------------------------

case_a_to_b() {
	note "A boot: selector a, booted rootfsa (mtd13); target must be B"
	new_case a-to-b a 13
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_eq "A->B exit status" 0 "$CASE_RC"
	if [ "$CASE_RC" -eq 0 ]; then
		check_eq "A->B selector moved to b" b "$(selector_value)"
	fi
	check_match "A->B wrote the kernel slice to kernelb" "mtd write .*/kernel .*/dev/mtd12" "$CMDLOG"
	check_match "A->B formatted rootfsb" "ubiformat .*dev/mtd14 " "$CMDLOG"
	check_no_match "A->B never touched the active pair (mtd11/mtd13)" "/dev/mtd(11|13)" "$CMDLOG"
	check_no_match "A->B never touched rootfs_data or the env blocks" "/dev/mtd(3|4|15)" "$CMDLOG"
	check_eq "A->B never rebooted" "" "$(grep -n '^reboot' "$CMDLOG" | head -n 1 | cut -d: -f2-)"
	local kline rline sline
	kline=$(log_line "mtd write")
	rline=$(log_line "ubiformat ")
	sline=$(log_line "sync")
	note "command lines: kernel-write=$kline ubiformat=$rline first-sync=$sline"
	if [ -n "$kline" ] && [ -n "$rline" ] && [ -n "$sline" ] &&
		[ "$kline" -lt "$rline" ] && [ "$rline" -lt "$sline" ]; then
		pass "A->B write order: kernel, rootfs, sync"
	else
		fail "A->B write order: kernel, rootfs, sync (saw $kline/$rline/$sline)"
	fi
	check_match "A->B re-ran the container check" "stub-check-ok" "$CMDLOG"
	check_match "A->B verified the kernel read-back" "read back byte-identical" "$CASE/log/out.log"
	check_match "A->B passed the UBI header read-back" "UBI#/UBI! headers" "$CASE/log/out.log"
	check_no_match "A->B (recorder present) does not warn about the recorder" "no boot pair recorder" "$CASE/log/out.log"
}

case_b_to_a() {
	note "B boot: selector b, booted rootfsb (mtd14); target must be A"
	new_case b-to-a b 14
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_eq "B->A exit status" 0 "$CASE_RC"
	if [ "$CASE_RC" -eq 0 ]; then
		check_eq "B->A selector moved to a" a "$(selector_value)"
	fi
	check_match "B->A wrote the kernel slice to kernela" "mtd write .*/kernel .*/dev/mtd11" "$CMDLOG"
	check_match "B->A formatted rootfsa" "ubiformat .*dev/mtd13 " "$CMDLOG"
	check_no_match "B->A never touched the active pair (mtd12/mtd14)" "/dev/mtd(12|14)" "$CMDLOG"
}

case_manual_b() {
	note "selector 0x21 (manual/recovery handover) must be refused"
	new_case manual-b manual-b 14
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "manual-b exit status is a refusal" 0 "$CASE_RC"
	check_eq "manual-b made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_eq "manual-b left the selector alone" manual-b "$(selector_value)"
	check_match "manual-b explains the refusal" "not one of the two established recorded pairs" "$CASE/log/out.log"
}

case_pair_mismatch() {
	note "selector says a but the booted rootfs is rootfsb: refuse"
	new_case mismatch a 14
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "mismatch exit status is a refusal" 0 "$CASE_RC"
	check_eq "mismatch made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_match "mismatch explains the refusal" "booted rootfs" "$CASE/log/out.log"
}

case_unknown_selector() {
	note "an unmeasured selector value must be refused"
	new_case unknown unknown 13
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "unknown selector exit status is a refusal" 0 "$CASE_RC"
	check_eq "unknown selector made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_match "unknown selector explains the refusal" "not one of the two established recorded pairs" "$CASE/log/out.log"
}

case_no_selector() {
	note "a missing selector interface must be refused"
	new_case no-selector a 13
	rm -f "$CASE/root/sys/devices/platform/boot-selector/current_pair" \
		"$CASE/root/sys/devices/platform/boot-selector/next_pair"
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "no-selector exit status is a refusal" 0 "$CASE_RC"
	check_eq "no-selector made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_match "no-selector explains the refusal" "no boot-selector interface" "$CASE/log/out.log"
}

case_kernel_oversize() {
	note "a kernel member larger than the kernel slot must be refused"
	new_case kernel-oversize a 13
	local image
	image=$(make_container "$CASE" 8650753 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "oversize kernel exit status is a refusal" 0 "$CASE_RC"
	check_eq "oversize kernel made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_match "oversize kernel explains the refusal" "does not fit" "$CASE/log/out.log"
}

case_root_oversize() {
	note "a rootfs member larger than the rootfs slot must be refused"
	new_case root-oversize a 13
	local image
	image=$(make_container "$CASE" 4096 24313857)
	run_do_upgrade "$image" stub-ok
	check_ne "oversize rootfs exit status is a refusal" 0 "$CASE_RC"
	check_eq "oversize rootfs made no write" "" "$(grep -v '^stub-check-ok' "$CMDLOG")"
	check_match "oversize rootfs explains the refusal" "rootfs image budget" "$CASE/log/out.log"
}

case_foreign_image() {
	note "a foreign image must be refused by the real container check"
	new_case foreign a 13
	local image="$CASE/img/foreign.bin"
	printf 'this is not a luofu sysupgrade container\n' > "$image"
	run_do_upgrade "$image" real
	check_ne "foreign image exit status is a refusal" 0 "$CASE_RC"
	check_eq "foreign image made no write" "" "$(grep -v '^stub-check' "$CMDLOG")"
	check_match "foreign image explains the refusal" "not a tar container" "$CASE/log/out.log"
}

case_kernel_write_fails() {
	note "a failing kernel-slot write must refuse before touching the rootfs"
	new_case kernel-write-fails a 13
	export LUOFU_MOCK_ROMTD="$CASE/root/dev/mtd12"
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	unset LUOFU_MOCK_ROMTD
	check_ne "kernel write failure exit status is a refusal" 0 "$CASE_RC"
	check_eq "kernel write failure left the selector on a" a "$(selector_value)"
	check_match "kernel write failure is reported" "mtd write to kernelb failed" "$CASE/log/out.log"
	check_eq "kernel write failure never formatted rootfsb" "" "$(log_line 'ubiformat ')"
}

case_root_write_fails() {
	note "a failing rootfs format must refuse and keep the old selector"
	new_case rootfs-write-fails a 13
	export LUOFU_MOCK_ROROOT="$CASE/root/dev/mtd14"
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	unset LUOFU_MOCK_ROROOT
	check_ne "rootfs write failure exit status is a refusal" 0 "$CASE_RC"
	check_eq "rootfs write failure left the selector on a" a "$(selector_value)"
	check_match "rootfs write failure is reported" "ubiformat of rootfsb failed" "$CASE/log/out.log"
}

case_selector_write_fails() {
	note "a failing selector store must refuse and leave the active pair selected"
	new_case selector-write-fails a 13
	rm -f "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	mkdir "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_ne "selector write failure exit status is a refusal" 0 "$CASE_RC"
	check_eq "selector write failure left the selector on a" a "$(selector_value)"
	check_match "selector write failure is reported" "could not move the boot selector" "$CASE/log/out.log"
	check_match "selector write failure still checked the kernel" "read back byte-identical" "$CASE/log/out.log"
}

case_check_refuse() {
	note "a failed platform_check_image must stop everything"
	new_case check-refuse a 13
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-refuse
	check_ne "check refusal exit status is a refusal" 0 "$CASE_RC"
	check_eq "check refusal made no write" "" "$(grep -v '^stub-check-refuse' "$CMDLOG")"
	check_match "check refusal is reported" "validation failed again" "$CASE/log/out.log"
}


case_no_recorder() {
	note "a running system without the boot pair recorder: install still proceeds, warning only"
	new_case no-recorder a 13
	rm -f "$CASE/root/etc/init.d/luofu_record_pair"
	local image
	image=$(make_container "$CASE" 4096 8192)
	run_do_upgrade "$image" stub-ok
	check_eq "no-recorder install exit status (refusals unchanged)" 0 "$CASE_RC"
	check_match "no-recorder install warns that the recorder is missing" "no boot pair recorder" "$CASE/log/out.log"
	if [ "$CASE_RC" -eq 0 ]; then
		check_eq "no-recorder install still moved the selector" b "$(selector_value)"
	fi
}

# --- boot-time pair recorder ------------------------------------------------

case_rec_a_boot() {
	note "recorder: A boot, bootfip left the B marker (0x11) in the cell; must record a"
	new_case rec-a-boot b 13
	run_recorder 0
	check_eq "recorder A exit status" 0 "$CASE_RC"
	check_eq "recorder A wrote the running pair a" a "$(selector_value)"
	check_match "recorder A logged the record" "recording the running pair a" "$CASE/log/recorder.log"
	run_recorder 0
	check_eq "recorder A second run exit status" 0 "$CASE_RC"
	check_eq "recorder A second run kept a" a "$(selector_value)"
	check_match "recorder A second run is a no-op (idempotent)" "already recorded" "$CASE/log/recorder.log"
}

case_rec_b_boot() {
	note "recorder: B boot, bootfip left the A marker (0x10) in the cell; must record b"
	new_case rec-b-boot a 14
	run_recorder 0
	check_eq "recorder B exit status" 0 "$CASE_RC"
	check_eq "recorder B wrote the running pair b" b "$(selector_value)"
	check_match "recorder B logged the record" "recording the running pair b" "$CASE/log/recorder.log"
}

case_rec_already_recorded() {
	note "recorder: the running pair is already recorded; the read-only next_pair proves no write"
	new_case rec-already a 13
	selector_freeze a
	run_recorder 0
	check_eq "recorder already-recorded exit status" 0 "$CASE_RC"
	check_eq "recorder already-recorded left the cell alone" a "$(selector_value)"
	check_match "recorder already-recorded says so" "already recorded" "$CASE/log/recorder.log"
}

case_rec_manual_b() {
	note "recorder: cell manual-b must never be overwritten"
	new_case rec-manual-b manual-b 14
	run_recorder 0
	check_eq "recorder manual-b exit status" 0 "$CASE_RC"
	check_eq "recorder manual-b left the cell untouched" manual-b "$(selector_value)"
	check_match "recorder manual-b fails safe" "not one of the two established recorded pairs" "$CASE/log/recorder.log"
}

case_rec_unknown() {
	note "recorder: an unknown cell value must never be overwritten"
	new_case rec-unknown unknown 13
	run_recorder 0
	check_eq "recorder unknown exit status" 0 "$CASE_RC"
	check_eq "recorder unknown left the cell untouched" unknown "$(selector_value)"
	check_match "recorder unknown fails safe" "not one of the two established recorded pairs" "$CASE/log/recorder.log"
}

case_rec_no_selector() {
	note "recorder: a missing selector interface after the bounded wait must not block the boot"
	new_case rec-no-selector b 13
	rm -f "$CASE/root/sys/devices/platform/boot-selector/current_pair" \
		"$CASE/root/sys/devices/platform/boot-selector/next_pair"
	run_recorder 1
	check_eq "recorder no-selector exit status" 0 "$CASE_RC"
	check_match "recorder no-selector fails safe after the bounded wait" "no boot-selector interface" "$CASE/log/recorder.log"
}

case_rec_no_ubi() {
	note "recorder: without ubi0/mtd_num the booted pair is unknown and nothing is written"
	new_case rec-no-ubi b 13
	rm -f "$CASE/root/sys/class/ubi/ubi0/mtd_num"
	run_recorder 0
	check_eq "recorder no-ubi exit status" 0 "$CASE_RC"
	check_eq "recorder no-ubi left the cell untouched" b "$(selector_value)"
	check_match "recorder no-ubi fails safe" "cannot identify the booted rootfs pair" "$CASE/log/recorder.log"
}

case_rec_bad_label() {
	note "recorder: an mtd label that is not rootfsa/rootfsb is not a pair witness"
	new_case rec-bad-label b 15
	run_recorder 0
	check_eq "recorder bad-label exit status" 0 "$CASE_RC"
	check_eq "recorder bad-label left the cell untouched" b "$(selector_value)"
	check_match "recorder bad-label fails safe" "cannot identify the booted rootfs pair" "$CASE/log/recorder.log"
}

case_rec_store_fails() {
	note "recorder: a failing next_pair store must be reported and the cell left alone"
	new_case rec-store-fails b 13
	rm -f "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	mkdir "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	run_recorder 0
	check_ne "recorder store-failure exit status is a failure" 0 "$CASE_RC"
	check_eq "recorder store-failure left the cell untouched" b "$(selector_value)"
	check_match "recorder store-failure is reported" "FAILED to store pair a" "$CASE/log/recorder.log"
}

case_rec_readback_mismatch() {
	note "recorder: a store whose read-back does not match must not report success"
	new_case rec-readback b 13
	rm -f "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	printf 'a\n' > "$CASE/root/sys/devices/platform/boot-selector/next_pair"
	run_recorder 0
	check_ne "recorder read-back mismatch exit status is a failure" 0 "$CASE_RC"
	check_eq "recorder read-back mismatch left current_pair on the stale value" b "$(selector_value)"
	check_match "recorder read-back mismatch is reported" "FAILED: the selector reads 'b' after writing pair a" "$CASE/log/recorder.log"
}

case_rec_cmdline_disagree() {
	note "recorder: a cmdline slot token that contradicts ubi0 must not write"
	new_case rec-cmdline-disagree b 13
	cmdline_set "console=ttyS0 ubi.mtd=rootfsb"
	run_recorder 0
	check_eq "recorder cmdline-disagreement exit status" 0 "$CASE_RC"
	check_eq "recorder cmdline-disagreement left the cell untouched" b "$(selector_value)"
	check_match "recorder cmdline-disagreement fails safe" "kernel command line names pair b but ubi0 is rootfsa" "$CASE/log/recorder.log"
}

case_rec_cmdline_agree() {
	note "recorder: a matching numeric cmdline ubi.mtd token is an extra witness, not a contradiction"
	new_case rec-cmdline-agree b 13
	cmdline_set "console=ttyS0 ubi.mtd=13"
	run_recorder 0
	check_eq "recorder cmdline-agreement exit status" 0 "$CASE_RC"
	check_eq "recorder cmdline-agreement recorded the running pair a" a "$(selector_value)"
	check_match "recorder cmdline-agreement logged the record" "recording the running pair a" "$CASE/log/recorder.log"
}

case_real_container() {
	note "real built container: the real platform_check_image plus the write path"
	[ -n "$container" ] || return 0
	[ -f "$container" ] || { fail "real container exists: $container"; return 0; }
	new_case real-container a 13
	run_do_upgrade "$container" real
	check_eq "real container exit status" 0 "$CASE_RC"
	if [ "$CASE_RC" -eq 0 ]; then
		check_eq "real container selector moved to b" b "$(selector_value)"
	fi
	check_match "real container passed the structural check" "image check passed" "$CASE/log/out.log"
	check_match "real container wrote kernelb" "mtd write .*/dev/mtd12" "$CMDLOG"
	check_match "real container formatted rootfsb" "ubiformat .*dev/mtd14 " "$CMDLOG"
	check_no_match "real container never touched the active pair" "/dev/mtd(11|13)" "$CMDLOG"
}

echo "# luofu A/B install path - host-side mocked tests"
echo "# platform.sh: $platform_path"
[ -n "$container" ] && echo "# real container: $container"
echo

case_a_to_b
case_b_to_a
case_manual_b
case_pair_mismatch
case_unknown_selector
case_no_selector
case_kernel_oversize
case_root_oversize
case_foreign_image
case_kernel_write_fails
case_root_write_fails
case_selector_write_fails
case_check_refuse
case_no_recorder
case_rec_a_boot
case_rec_b_boot
case_rec_already_recorded
case_rec_manual_b
case_rec_unknown
case_rec_no_selector
case_rec_no_ubi
case_rec_bad_label
case_rec_store_fails
case_rec_readback_mismatch
case_rec_cmdline_disagree
case_rec_cmdline_agree
case_real_container

echo
echo "# passed: $PASS  failed: $FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
