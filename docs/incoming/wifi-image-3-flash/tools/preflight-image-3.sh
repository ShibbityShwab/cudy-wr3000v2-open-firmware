#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# preflight-image-3.sh - HOST-ONLY preflight for the IMAGE #3 flash (Lead runbook gate).
#
# Lane: wifi-image-3-flash (task-201).  This script never contacts the router,
# the bench link, the UART, TFTP, the network or any device interface: it hashes
# local artifacts, parses captured text witnesses, and prints a verdict.  The
# fire itself is the Lead's attended decision (see ../RUNBOOK.md).
#
# It verifies, from the shipped bytes: the container size/sha256 and tar members,
# CONTROL + fip-manifest, the UNCHANGED kernel slice and the derived kernelb slot
# identity, the rootfs UBI, the flash geometry (kernelb/mtd12 exact fit,
# rootfsb/mtd14 free space), the optional sibling artifacts/module identities,
# the optional stock pair-A witness and the optional /proc/mtd capture.
#
# usage:
#   preflight-image-3.sh                    host artifacts only (auto paths under $HOME)
#   preflight-image-3.sh --container FILE --artifacts DIR --module-artifacts DIR
#   preflight-image-3.sh --strict --stock-witness FILE --proc-mtd FILE [--path rootfs-only|container]
#   preflight-image-3.sh --unit-test        internal parser/geometry tests (no artifacts)
#   preflight-image-3.sh --print-pins       print the pinned identities and exit
#
# exit codes: 0 = PASS, 1 = FAIL (do not fire), 2 = usage / missing input.
#
# Text-only: this file and its output carry sizes, sha256 digests and partition
# numbers; no vendor binary or firmware byte is embedded.
set -eu
LC_ALL=C
export LC_ALL

C_SIZE=7843840
C_SHA=4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53
K_SIZE=3372638
K_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
KSLOT_SIZE=8650752
KSLOT_SHA=1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
R_SIZE=4456448
R_SHA=90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3
SQUASH_SIZE=4009984
SQUASH_SHA=ecbba4f97300607cd41614ab09c9aa6378bcea215e8de37f4c3097b860a0bfaf
MOD_I_SIZE=358544
MOD_I_SHA=a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed
MOD_A_SIZE=379092
MOD_A_SHA=545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
CFG_SIZE=2317952
CFG_SHA=bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91
HOSTAPD_SIZE=1618388
HOSTAPD_SHA=fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6
MTD_ERASE=131072
MTD11_SIZE=8650752
MTD12_SIZE=8650752
MTD13_SIZE=24379392
MTD14_SIZE=24379392
FREE_BYTES=$((MTD14_SIZE - R_SIZE))
MEMDIR=sysupgrade-cudy_wr3000-v2

CONTAINER=""
ARTIFACTS=""
MODART=""
WITNESS=""
PROCMTD=""
STRICT=0
UNIT=0
PRINT_PINS=0
PATH_MODE=rootfs-only
PASS=0
FAIL=0
WARN=0

say()    { printf '%s\n' "$*"; }
pass()   { PASS=$((PASS + 1)); printf 'PASS  %s\n' "$*"; }
fail()   { FAIL=$((FAIL + 1)); printf 'FAIL  %s\n' "$*"; }
warn()   { WARN=$((WARN + 1)); printf 'WARN  %s\n' "$*"; }
record() { printf 'RECORD %s\n' "$*"; }
note()   { printf 'NOTE  %s\n' "$*"; }
die()    { printf 'ERROR: %s\n' "$*" >&2; exit 2; }
need_tool() { command -v "$1" >/dev/null 2>&1 || die "required tool missing: $1"; }
file_size() { wc -c < "$1" | tr -d ' '; }
file_sha()  { sha256sum "$1" | cut -d' ' -f1; }

pad_sha() {
	cat "$1"
	_pad=$(($2 - $(file_size "$1")))
	if [ "$_pad" -gt 0 ]; then
		head -c "$_pad" /dev/zero | tr '\000' '\377'
	fi
}

ubi_magic_ok() {
	[ "$(head -c 4 "$1" | od -An -tx1 | tr -d ' \n')" = "55424923" ]
}

hex_to_dec() {
	case "$1" in
	''|*[!0-9a-fA-F]*) return 1 ;;
	esac
	printf '%s' "$((0x$1))"
}

witness_get() {
	awk -v k="$2" '$1 == k { $1 = ""; sub(/^[ \t]+/, ""); print; found = 1; exit } END { exit !found }' "$1" 2>/dev/null || true
}

proc_mtd_field() {
	awk -v n="mtd$2:" '$1 == n { print $'"$3"'; exit }' "$1"
}

proc_mtd_name() {
	proc_mtd_field "$1" "$2" 4 | sed -n 's/^"\(.*\)"$/\1/p'
}

check_pin() {
	if [ ! -f "$2" ]; then
		fail "$1 not found: $2"
		return 0
	fi
	_sz=$(file_size "$2")
	_sh=$(file_sha "$2")
	if [ "$_sz" = "$3" ] && [ "$_sh" = "$4" ]; then
		pass "$1 is the pinned artifact ($4, $3 B)"
	else
		fail "$1 mismatch: size $_sz B (want $3 B), sha256 $_sh (want $4)"
	fi
	return 0
}

usage() {
	cat <<'USAGE'
preflight-image-3.sh - host-only preflight for the IMAGE #3 flash (Lead runbook gate)

usage:
  preflight-image-3.sh                 host artifacts only (auto paths under $HOME)
  preflight-image-3.sh --container FILE [--artifacts DIR] [--module-artifacts DIR]
  preflight-image-3.sh --strict --stock-witness FILE --proc-mtd FILE [--path rootfs-only|container]
                                       the fire gate (host artifacts + on-box witness)
  preflight-image-3.sh --unit-test     internal parser/geometry tests (no artifacts)
  preflight-image-3.sh --print-pins    print the pinned identities and exit
  preflight-image-3.sh --help

exit: 0 = PASS, 1 = FAIL (do not fire), 2 = usage / missing input.

--stock-witness is a text file produced on the stock pair-A shell, one "key value"
line each (values may contain spaces only for cmdline):
  cmdline   cat /proc/cmdline
  ubimtd    cat /sys/class/ubi/ubi0/mtd_num
  cell      cat /sys/devices/platform/sysenv/boot_reg
  bootflag  fw_printenv bootflag | sed 's/^bootflag=//'
  kernelb   dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum
  kernela   dd if=/dev/mtd11 bs=131072 count=66 2>/dev/null | sha256sum
--proc-mtd is "cat /proc/mtd" (the stock partition table).  The witness hash is the
authoritative kernelb identity; no multi-MB slot dump has to leave the router.
USAGE
}

while [ $# -gt 0 ]; do
	case "$1" in
	--container)        [ $# -ge 2 ] || die "--container needs a value";        CONTAINER=$2; shift ;;
	--artifacts)        [ $# -ge 2 ] || die "--artifacts needs a value";        ARTIFACTS=$2; shift ;;
	--module-artifacts) [ $# -ge 2 ] || die "--module-artifacts needs a value"; MODART=$2; shift ;;
	--stock-witness)    [ $# -ge 2 ] || die "--stock-witness needs a value";    WITNESS=$2; shift ;;
	--proc-mtd)         [ $# -ge 2 ] || die "--proc-mtd needs a value";         PROCMTD=$2; shift ;;
	--path)             [ $# -ge 2 ] || die "--path needs a value";             PATH_MODE=$2; shift ;;
	--strict)           STRICT=1 ;;
	--unit-test)        UNIT=1 ;;
	--print-pins)       PRINT_PINS=1 ;;
	-h|--help)          usage; exit 0 ;;
	*)                  die "unknown option: $1 (try --help)" ;;
	esac
	shift
done

case "$PATH_MODE" in
rootfs-only|container) ;;
*) die "--path must be rootfs-only or container (got: $PATH_MODE)" ;;
esac

need_tool sha256sum
need_tool tar
need_tool awk
need_tool sed
need_tool od
need_tool head

TMPDIR_PF=$(mktemp -d) || die "cannot create a temporary directory"
trap 'rm -rf "$TMPDIR_PF"' EXIT HUP INT TERM

print_pins() {
	cat <<EOF2
IMAGE #3 pinned identities (docs/incoming/wifi-image-3/HASHES.txt)
container        $C_SIZE B  sha256 $C_SHA
kernel slice     $K_SIZE B  sha256 $K_SHA  (UNCHANGED container member)
kernelb slot     $KSLOT_SIZE B  sha256 $KSLOT_SHA  (0xFF-padded slice; mtd12)
rootfs UBI       $R_SIZE B  sha256 $R_SHA  (container member "root")
root squashfs    $SQUASH_SIZE B  sha256 $SQUASH_SHA  (UBI vol0)
module installed $MOD_I_SIZE B  sha256 $MOD_I_SHA  (64 params)
module activity  $MOD_A_SIZE B  sha256 $MOD_A_SHA
cfg80211.ko      $CFG_SIZE B  sha256 $CFG_SHA
hostapd          $HOSTAPD_SIZE B  sha256 $HOSTAPD_SHA  (v2.12, CONFIG_DRIVER_11AX_SUPPORT)
mtd11 kernela    $MTD11_SIZE B (0x840000)
mtd12 kernelb    $MTD12_SIZE B (0x840000)  <- full slot fits exactly
mtd13 rootfsa    $MTD13_SIZE B (0x1740000)
mtd14 rootfsb    $MTD14_SIZE B (0x1740000)  <- $FREE_BYTES B free after IMAGE #3
eraseblock       $MTD_ERASE B (128 KiB); rootfs UBI = $((R_SIZE / MTD_ERASE)) PEBs
EOF2
}

unit_test() {
	if [ "$((0x840000))" -eq "$MTD12_SIZE" ] && [ "$((0x1740000))" -eq "$MTD14_SIZE" ] \
	   && [ "$FREE_BYTES" -eq 19922944 ] && [ "$((R_SIZE / MTD_ERASE))" -eq 34 ]; then
		pass "unit: mtd geometry (mtd12 0x840000, mtd14 0x1740000, free 19922944 B, 34 PEBs)"
	else
		fail "unit: mtd geometry"
	fi

	printf 'A' > "$TMPDIR_PF/pad-in"
	_pad_sha=$(pad_sha "$TMPDIR_PF/pad-in" 8 | sha256sum | cut -d' ' -f1)
	_exp_sha=$( { printf 'A'; head -c 7 /dev/zero | tr '\000' '\377'; } | sha256sum | cut -d' ' -f1)
	if [ "$_pad_sha" = "$_exp_sha" ]; then
		pass "unit: 0xFF slot padding (8-byte fixture)"
	else
		fail "unit: 0xFF slot padding"
	fi

	cat > "$TMPDIR_PF/mtd.txt" <<'EOF3'
dev:    size   erasesize  name
mtd0: 00080000 00020000 "uboot"
mtd11: 00840000 00020000 "kernela"
mtd12: 00840000 00020000 "kernelb"
mtd13: 01740000 00020000 "rootfsa"
mtd14: 01740000 00020000 "rootfsb"
EOF3
	if [ "$(proc_mtd_name "$TMPDIR_PF/mtd.txt" 12)" = "kernelb" ] \
	   && [ "$(proc_mtd_field "$TMPDIR_PF/mtd.txt" 12 2)" = "00840000" ] \
	   && [ "$(proc_mtd_field "$TMPDIR_PF/mtd.txt" 14 2)" = "01740000" ] \
	   && [ "$(proc_mtd_field "$TMPDIR_PF/mtd.txt" 14 3)" = "00020000" ] \
	   && [ "$(hex_to_dec "$(proc_mtd_field "$TMPDIR_PF/mtd.txt" 14 2)")" = "24379392" ]; then
		pass "unit: /proc/mtd parser (name/size/erasesize/hex)"
	else
		fail "unit: /proc/mtd parser"
	fi

	cat > "$TMPDIR_PF/witness.txt" <<'EOF4'
cmdline ubi.mtd=rootfsa ubi.block=0,0 root=/dev/ubiblock0_0 console=ttyS0
ubimtd 13
cell 0x10
bootflag a
kernelb 1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e
kernela aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
EOF4
	if [ "$(witness_get "$TMPDIR_PF/witness.txt" ubimtd)" = "13" ] \
	   && [ "$(witness_get "$TMPDIR_PF/witness.txt" cell)" = "0x10" ] \
	   && [ "$(witness_get "$TMPDIR_PF/witness.txt" bootflag)" = "a" ] \
	   && [ "$(witness_get "$TMPDIR_PF/witness.txt" kernelb)" = "$KSLOT_SHA" ] \
	   && [ "$(witness_get "$TMPDIR_PF/witness.txt" cmdline)" = "ubi.mtd=rootfsa ubi.block=0,0 root=/dev/ubiblock0_0 console=ttyS0" ]; then
		pass "unit: stock-witness parser (multi-word cmdline preserved)"
	else
		fail "unit: stock-witness parser"
	fi

	printf 'UBI#' > "$TMPDIR_PF/ubi.bin"
	printf 'hsqs' > "$TMPDIR_PF/sq.bin"
	if ubi_magic_ok "$TMPDIR_PF/ubi.bin" && ! ubi_magic_ok "$TMPDIR_PF/sq.bin"; then
		pass "unit: UBI magic detector"
	else
		fail "unit: UBI magic detector"
	fi

	printf 'preflight-image-3 unit-test: %s passed, %s failed\n' "$PASS" "$FAIL"
	[ "$FAIL" -eq 0 ]
}

if [ "$UNIT" -eq 1 ]; then
	unit_test
	exit $?
fi

if [ "$PRINT_PINS" -eq 1 ]; then
	print_pins
	exit 0
fi


if [ -z "$CONTAINER" ]; then
	_d=$HOME/wifi-image-3-artifacts
	if [ -f "$_d/image-3-sysupgrade.bin" ]; then
		CONTAINER=$_d/image-3-sysupgrade.bin
	else
		die "no container: pass --container FILE (default looked for $_d/image-3-sysupgrade.bin)"
	fi
fi
[ -f "$CONTAINER" ] || die "container not found: $CONTAINER"
if [ -z "$ARTIFACTS" ]; then
	_ad=$(dirname "$CONTAINER")
	if [ -f "$_ad/SHA256SUMS" ] && [ -f "$_ad/kernel-slice.bin" ] && [ -f "$_ad/rootfs.ubi" ]; then
		ARTIFACTS=$_ad
	fi
fi
if [ -z "$MODART" ] && [ -d "$HOME/wifi-image-3-module-artifacts" ]; then
	MODART=$HOME/wifi-image-3-module-artifacts
fi

if [ "$STRICT" -eq 1 ]; then
	_mode=fire-gate
else
	_mode=host-artifacts
fi
say "== IMAGE #3 flash preflight (host-only; task-201) =="
say "mode      : $_mode"
say "path      : $PATH_MODE"
say "container : $CONTAINER"
if [ -n "$ARTIFACTS" ]; then say "artifacts : $ARTIFACTS"; else say "artifacts : (not supplied)"; fi
if [ -n "$MODART" ]; then say "module art: $MODART"; else say "module art: (not supplied)"; fi
if [ -n "$WITNESS" ]; then say "witness   : $WITNESS"; else say "witness   : (not supplied)"; fi
if [ -n "$PROCMTD" ]; then say "proc-mtd  : $PROCMTD"; else say "proc-mtd  : (not supplied)"; fi
say ""

if [ "$STRICT" -eq 1 ]; then
	[ -n "$WITNESS" ] || fail "strict mode requires --stock-witness FILE (the stock pair-A read-only witness)"
	[ -n "$PROCMTD" ] || fail "strict mode requires --proc-mtd FILE (the stock /proc/mtd capture)"
	[ -n "$ARTIFACTS" ] || warn "strict mode: no --artifacts dir; the sibling artifact set is not cross-checked"
	[ -n "$MODART" ] || warn "strict mode: no --module-artifacts dir; the merged module identities are not cross-checked locally"
fi

_csz=$(file_size "$CONTAINER")
_csh=$(file_sha "$CONTAINER")
if [ "$_csz" = "$C_SIZE" ] && [ "$_csh" = "$C_SHA" ]; then
	pass "container is the pinned IMAGE #3 build ($C_SHA, $C_SIZE B)"
else
	fail "container mismatch: size $_csz B (want $C_SIZE B), sha256 $_csh (want $C_SHA) - this is NOT the baked IMAGE #3 artifact"
fi
record "container $C_SIZE B; kernel member $K_SIZE B; root member $R_SIZE B; tar metadata $((C_SIZE - K_SIZE - R_SIZE)) B"

EXTRACT=0
if tar -tf "$CONTAINER" > "$TMPDIR_PF/members.txt" 2>"$TMPDIR_PF/tar.err"; then
	EXTRACT=1
else
	fail "container is not a readable tar archive ($(tr '\n' ' ' < "$TMPDIR_PF/tar.err"))"
fi

if [ "$EXTRACT" -eq 1 ]; then
	_n=$(wc -l < "$TMPDIR_PF/members.txt" | tr -d ' ')
	if [ "$_n" = "6" ]; then
		pass "container has exactly 6 tar members (dir + CONTROL + INSTALL-STATUS + fip-manifest + kernel + root)"
	else
		fail "container has $_n tar members (want 6)"
	fi
	for _m in CONTROL INSTALL-STATUS.txt fip-manifest.txt kernel root; do
		if grep -Fxq "$MEMDIR/$_m" "$TMPDIR_PF/members.txt"; then
			:
		else
			fail "container is missing member: $MEMDIR/$_m"
		fi
	done
	if grep -Fxq "$MEMDIR" "$TMPDIR_PF/members.txt" || grep -Fxq "$MEMDIR/" "$TMPDIR_PF/members.txt"; then
		pass "container carries the $MEMDIR directory member"
	else
		fail "container is missing the $MEMDIR directory member"
	fi
	for _m in CONTROL INSTALL-STATUS.txt fip-manifest.txt kernel root; do
		if ! tar -xOf "$CONTAINER" "$MEMDIR/$_m" > "$TMPDIR_PF/$_m.bin" 2>/dev/null; then
			fail "cannot extract container member: $_m"
		fi
	done
	if [ -f "$TMPDIR_PF/kernel.bin" ]; then
		check_pin "kernel slice (member)" "$TMPDIR_PF/kernel.bin" "$K_SIZE" "$K_SHA"
		_slot=$(pad_sha "$TMPDIR_PF/kernel.bin" "$KSLOT_SIZE" | sha256sum | cut -d' ' -f1)
		if [ "$_slot" = "$KSLOT_SHA" ]; then
			pass "kernelb slot identity derived from the slice: $KSLOT_SHA ($KSLOT_SIZE B, 0xFF-padded)"
		else
			fail "kernelb slot identity mismatch: derived $_slot (want $KSLOT_SHA)"
		fi
	fi
	if [ -f "$TMPDIR_PF/CONTROL.bin" ]; then
		if grep -qx 'BOARD=cudy_wr3000-v2' "$TMPDIR_PF/CONTROL.bin" \
		   && grep -qx 'TARGET=hisilicon/luofu' "$TMPDIR_PF/CONTROL.bin"; then
			pass "CONTROL names BOARD=cudy_wr3000-v2 TARGET=hisilicon/luofu"
		else
			fail "CONTROL does not name the WR3000 v2 / hisilicon luofu target"
		fi
	fi
	if [ -f "$TMPDIR_PF/fip-manifest.txt.bin" ]; then
		if grep -qx 'slice-size: 3372638' "$TMPDIR_PF/fip-manifest.txt.bin" \
		   && grep -qx "slice-sha256: $K_SHA" "$TMPDIR_PF/fip-manifest.txt.bin" \
		   && grep -qx 'slot-limit: 8650752' "$TMPDIR_PF/fip-manifest.txt.bin"; then
			pass "fip-manifest slice-size/slice-sha256/slot-limit match the unchanged kernel"
		else
			fail "fip-manifest does not match the unchanged kernel slice"
		fi
	fi
	if [ -f "$TMPDIR_PF/INSTALL-STATUS.txt.bin" ]; then
		if grep -q 'NOT FLASHABLE-UNTIL-INSTALLED' "$TMPDIR_PF/INSTALL-STATUS.txt.bin"; then
			pass "INSTALL-STATUS.txt present (carries the do-not-stock-sysupgrade warning)"
		else
			fail "INSTALL-STATUS.txt missing its do-not-stock-sysupgrade warning"
		fi
	fi
	if [ -f "$TMPDIR_PF/root.bin" ]; then
		check_pin "rootfs UBI (member root)" "$TMPDIR_PF/root.bin" "$R_SIZE" "$R_SHA"
		if ubi_magic_ok "$TMPDIR_PF/root.bin"; then
			pass "rootfs member starts with the UBI# EC magic"
		else
			fail "rootfs member does not start with the UBI# EC magic"
		fi
		if [ "$((R_SIZE % MTD_ERASE))" -eq 0 ]; then
			pass "rootfs UBI is a whole number of 128 KiB PEBs ($((R_SIZE / MTD_ERASE)))"
		else
			fail "rootfs UBI is not PEB-aligned"
		fi
	fi
	if [ "$KSLOT_SIZE" -eq "$MTD12_SIZE" ]; then
		pass "kernelb/mtd12 is $MTD12_SIZE B and the padded kernel slot fills it exactly"
	else
		fail "kernel slot size $KSLOT_SIZE B does not match kernelb/mtd12 size $MTD12_SIZE B"
	fi
	if [ "$MTD14_SIZE" -ge "$R_SIZE" ]; then
		pass "rootfsb/mtd14 $MTD14_SIZE B holds the $R_SIZE B rootfs UBI; $FREE_BYTES B free ($((R_SIZE * 100 / MTD14_SIZE))% used)"
	else
		fail "rootfsb/mtd14 ($MTD14_SIZE B) is too small for the rootfs UBI ($R_SIZE B)"
	fi
	record "after the rootfs write $FREE_BYTES B remain on mtd14 for the rootfs_data overlay"
fi


if [ -n "$ARTIFACTS" ]; then
	if [ -d "$ARTIFACTS" ]; then
		check_pin "artifact image-3-sysupgrade.bin" "$ARTIFACTS/image-3-sysupgrade.bin" "$C_SIZE" "$C_SHA"
		check_pin "artifact kernel-slice.bin" "$ARTIFACTS/kernel-slice.bin" "$K_SIZE" "$K_SHA"
		check_pin "artifact rootfs.ubi" "$ARTIFACTS/rootfs.ubi" "$R_SIZE" "$R_SHA"
		check_pin "artifact root.squashfs" "$ARTIFACTS/root.squashfs" "$SQUASH_SIZE" "$SQUASH_SHA"
		check_pin "artifact cfg80211.ko" "$ARTIFACTS/cfg80211.ko" "$CFG_SIZE" "$CFG_SHA"
		check_pin "artifact hostapd" "$ARTIFACTS/hostapd" "$HOSTAPD_SIZE" "$HOSTAPD_SHA"
		check_pin "artifact hi5622_wifi.installed.ko" "$ARTIFACTS/hi5622_wifi.installed.ko" "$MOD_I_SIZE" "$MOD_I_SHA"
		if [ -f "$ARTIFACTS/SHA256SUMS" ]; then
			if ( cd "$ARTIFACTS" && sha256sum -c SHA256SUMS >/dev/null 2>&1 ); then
				pass "artifact directory SHA256SUMS verifies"
			else
				fail "artifact directory SHA256SUMS does NOT verify"
			fi
		else
			warn "artifact directory has no SHA256SUMS"
		fi
	else
		fail "artifacts directory not found: $ARTIFACTS"
	fi
else
	warn "no artifact directory supplied/detected: the sibling artifact set is not cross-checked (the container pins above remain authoritative)"
fi

if [ -n "$MODART" ]; then
	if [ -d "$MODART" ]; then
		check_pin "module activity artifact" "$MODART/hi5622_wifi.activity.ko" "$MOD_A_SIZE" "$MOD_A_SHA"
		check_pin "module installed artifact" "$MODART/hi5622_wifi.installed.ko" "$MOD_I_SIZE" "$MOD_I_SHA"
		if [ -f "$MODART/hi5622_wifi.unstripped.ko" ]; then
			record "module unstripped artifact present: $(file_size "$MODART/hi5622_wifi.unstripped.ko") B (not a pinned input)"
		fi
	else
		fail "module artifacts directory not found: $MODART"
	fi
else
	warn "no module artifacts supplied/detected: the merged module identities are not cross-checked locally"
fi

if [ -n "$WITNESS" ]; then
	if [ ! -f "$WITNESS" ]; then
		fail "stock witness not found: $WITNESS"
	else
		_w_cmd=$(witness_get "$WITNESS" cmdline)
		_w_ubi=$(witness_get "$WITNESS" ubimtd)
		_w_cell=$(witness_get "$WITNESS" cell)
		if [ -z "$_w_cell" ]; then _w_cell=$(witness_get "$WITNESS" boot_reg); fi
		_w_flag=$(witness_get "$WITNESS" bootflag)
		_w_kb=$(witness_get "$WITNESS" kernelb)
		if [ -z "$_w_kb" ]; then _w_kb=$(witness_get "$WITNESS" mtd12); fi
		_w_ka=$(witness_get "$WITNESS" kernela)
		if [ -z "$_w_ka" ]; then _w_ka=$(witness_get "$WITNESS" mtd11); fi
		_w_ubi=$(printf '%s' "$_w_ubi" | tr -d ' \r\n')
		_w_cell=$(printf '%s' "$_w_cell" | tr -d ' \r\n')
		_w_flag=$(printf '%s' "$_w_flag" | tr -d ' \r\n')
		_w_kb=$(printf '%s' "$_w_kb" | tr -d ' \r\n')
		case "$_w_cmd" in
		*rootfsb*)
			fail "witness: the stock cmdline uses ubi.mtd=rootfsb - stock is not on pair A; fix the pair first" ;;
		*ubi.mtd=rootfsa*)
			pass "witness: stock cmdline uses ubi.mtd=rootfsa" ;;
		*)
			fail "witness: the stock cmdline does not use ubi.mtd=rootfsa (got: $_w_cmd)" ;;
		esac
		case "$_w_ubi" in
		13) pass "witness: the attached UBI device is mtd13 (rootfsa)" ;;
		'') fail "witness: ubimtd is empty (no UBI attached / not readable)" ;;
		*) fail "witness: the attached UBI device is mtd$_w_ubi (want mtd13/rootfsa)" ;;
		esac
		case "$_w_cell" in
		10|0x10|0x00000010) pass "witness: selector cell is 0x10 (pair A)" ;;
		*) fail "witness: selector cell is '$_w_cell' (want 0x10/pair A)" ;;
		esac
		case "$_w_flag" in
		a) pass "witness: U-Boot env bootflag is a" ;;
		*) fail "witness: U-Boot env bootflag is '$_w_flag' (want a)" ;;
		esac
		case "$_w_kb" in
		"$KSLOT_SHA")
			pass "witness: kernelb/mtd12 is already the validated slot $KSLOT_SHA" ;;
		'')
			fail "witness: kernelb hash missing" ;;
		*)
			if [ "$PATH_MODE" = "rootfs-only" ]; then
				fail "witness: kernelb/mtd12 is not the validated slot - the rootfs-only path is NOT allowed (use --path container to write+verify kernelb, or recover the slot first)"
			else
				warn "witness: kernelb is not the validated slot; the container path writes and verifies it"
			fi ;;
		esac
		case "$_w_ka" in
		'') warn "witness: kernela hash missing (record it as the rollback witness)" ;;
		*) record "witness: kernela/mtd11 rollback witness $_w_ka" ;;
		esac
		case "$_w_kb" in
		"$KSLOT_SHA") WITNESS_OK=1 ;;
		esac
	fi
fi

if [ -n "$PROCMTD" ]; then
	if [ ! -f "$PROCMTD" ]; then
		fail "/proc/mtd capture not found: $PROCMTD"
	else
		_okm=1
		for _id in 11 12 13 14; do
			_pname=$(proc_mtd_name "$PROCMTD" "$_id")
			_phex=$(proc_mtd_field "$PROCMTD" "$_id" 2)
			_per=$(proc_mtd_field "$PROCMTD" "$_id" 3)
			if [ -z "$_pname" ] || [ -z "$_phex" ] || [ -z "$_per" ]; then
				fail "mtd$_id is missing from the /proc/mtd capture"
				_okm=0
				continue
			fi
			if ! _pdec=$(hex_to_dec "$_phex" 2>/dev/null); then
				fail "mtd$_id size is not hex: $_phex"
				_okm=0
				continue
			fi
			case "$_id" in
			11) _want_name=kernela; _want=$MTD11_SIZE ;;
			12) _want_name=kernelb; _want=$MTD12_SIZE ;;
			13) _want_name=rootfsa; _want=$MTD13_SIZE ;;
			14) _want_name=rootfsb; _want=$MTD14_SIZE ;;
			esac
			if [ "$_pname" != "$_want_name" ] || [ "$_pdec" != "$_want" ] || [ "$_per" != "00020000" ]; then
				fail "mtd$_id is $_pname $_phex $_per (want $_want_name $_want B erase 0x20000)"
				_okm=0
			fi
		done
		if [ "$_okm" -eq 1 ]; then
			pass "the /proc/mtd capture matches the geometry (kernela/kernelb 8,650,752 B; rootfsa/rootfsb 24,379,392 B; erase 128 KiB)"
		fi
		_m14=$(proc_mtd_field "$PROCMTD" 14 2)
		if _m14dec=$(hex_to_dec "$_m14" 2>/dev/null); then
			if [ "$_m14dec" -ge "$R_SIZE" ]; then
				pass "mtd14 free space after the IMAGE #3 UBI: $((_m14dec - R_SIZE)) B (>= $FREE_BYTES expected)"
			else
				fail "mtd14 ($_m14dec B) is too small for the rootfs UBI ($R_SIZE B)"
			fi
		else
			fail "cannot read the mtd14 size from the /proc/mtd capture"
		fi
	fi
fi

say ""
say "-------------------------------------------------------------------------------"
say "preflight-image-3: $PASS passed, $FAIL failed, $WARN warnings"
if [ "$FAIL" -gt 0 ]; then
	say "RESULT: FAIL - do not fire. Fix the FAIL lines above; nothing was written."
	exit 1
fi
if [ "$STRICT" -eq 1 ]; then
	if [ "$PATH_MODE" = "rootfs-only" ]; then
		say "RESULT: PASS - fire-ready for the ROOTFS-ONLY path: write mtd14 only (ubiformat), never kernelb, then the pair-b handover (RUNBOOK.md section 7)."
	else
		say "RESULT: PASS - fire-ready for the FULL CONTAINER path: write kernelb (validated slice) + rootfsb from the container, then the pair-b handover (RUNBOOK.md section 8)."
	fi
else
	say "RESULT: PASS - host artifacts are the pinned IMAGE #3 build. NOT fire-ready yet: rerun with --strict --stock-witness FILE --proc-mtd FILE before any write (RUNBOOK.md section 6)."
fi
say "REMINDER: the fire still needs a human at the bench, tools/recovery-card.sh READY, tools/bench-link.sh PASS and a running read-only UART capture (RUNBOOK.md section 2)."
exit 0

