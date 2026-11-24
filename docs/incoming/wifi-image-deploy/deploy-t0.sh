#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# deploy-t0.sh - host-side orchestrator for the T0 zero-flash bench-image
# deploy (task-119). Installs the seven built bench .apk packages into the
# RUNNING pair-B rootfs over SSH (persistent overlay; no flash write, no
# selector change, no reboot) and verifies the result on the device.
#
# Modes (default: --check, which never contacts the router):
#   --check      host-only verification of the artifacts + tree + plan
#   --apply      attended install: preflight -> stage -> apk add -> read-back
#   --load-check --apply: additionally load cfg80211+hi5622_wifi from the
#                rootfs (write-free probe; reboot before a normal session)
#   --rollback   attended: apk del + verify the pre-install state is restored
#   --verify     attended read-only post-boot verification of an installed set
#
# Hard rules honoured: writes only the rootfs overlay, never flash, never the
# boot selector, never /dev/mem, never UART, never rmmod, never a reboot.
# All device steps are staged in /tmp and fail closed.
set -u

getenv() { eval "printf '%s' \"\$$1\"" 2>/dev/null; }
have_env() { [ -n "$(getenv "$1")" ]; }

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$HERE
OPENSRC=$(cd "$HERE/../../.." && pwd)
REPO_ROOT=$(cd "$OPENSRC/.." && pwd)
DEFAULT_ART=/home/shibbityshwab/wifi-image-integration-artifacts
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_HOST=root@192.168.1.1

# expected identities (authoritative; tools/expected-*.txt are cross-checked)
EXPECTED_PKGS_SHA=674f209f01e8ef5f75d19bcc85ec2cb1ea722602ca53533022afde5cd9c7563e
EXPECTED_FILES_SHA=9cdfb9072c28546fbb8f0abf0d0626ad095e8057e8c5b94c8dc6a680d6763332
KERNEL_DEP=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1
VERMAGIC=6f995278d21563ed4dd8f2609f3659c8
CONTAINER_SHA=df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310
KERNEL_SLICE_SHA=13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f

APK_TABLE=$(cat <<'TABLE'
iw 6.17-r1 52997 9b46713fb5a0e866e7b1fb919f2a30eca4bc01d9aeabf49fa50b20faedd69f08 116868
iwinfo 2026.05.26~66bdd1a0-r1 8322 5badc21509df6ba6df109697879cf28960315c8ceb19aabb258f79564ae5de23 16922
libiwinfo20230701 2026.05.26~66bdd1a0-r1 25768 d1178900f817c08c232bf4a704e5b883f02c1f67dbe93e41af9f4b8132ce3f55 57408
libiwinfo-data 2026.05.26~66bdd1a0-r1 3925 3bb216a4f2ae69a27cfe86daf97c597b92daa4f0fca68b00c0d842076d933320 18264
wireless-regdb 2026.09.03-r1 3440 afd292e0c0947aaa5497e52b2a9e9484e690a9abfef1153c36d9a06143a758a1 6360
kmod-hi5622-cfg80211 6.18.60-r1 183520 716fd384cc7ad58b7c3e4000046addce7c76b3223fe3b8d5b05600fb46ed624c 406061
kmod-hi5622-wifi 6.18.60-r1 59302 aac8f3fa0f230ac19f94cb010c6f15e443a52e35b095a7e54908b2185eaf9aef 133920
TABLE
)

MODE=check
N_MODES=0
DO_LOAD=0
YES_REALLY=0
ATTENDED=0
ART=$DEFAULT_ART
OWRT=$DEFAULT_OWRT
RHOST=$DEFAULT_HOST
RSH_OVERRIDE=$(getenv T0_RSH)
RSCP_OVERRIDE=$(getenv T0_RSCP)
STATE_OVERRIDE=$(getenv T0_STATE_DIR)
KEEP_STAGING=0
MODULE_PARAMS=

usage() {
	sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'
	printf '\noptions:\n'
	printf '  --artifacts DIR   apk/container directory (default %s)\n' "$DEFAULT_ART"
	printf '  --owrt DIR        OpenWrt tree for the host apk tool (default %s)\n' "$DEFAULT_OWRT"
	printf '  --host USER@IP    router target (default %s)\n' "$DEFAULT_HOST"
	printf '  --state DIR       evidence directory override\n'
	printf '  --param KEY=VALUE module parameter for --load-check (repeatable)\n'
	printf '  --keep-staging    leave the /tmp staging dir on the device\n'
	printf '  --yes-really      operator confirms the action\n'
	printf '  --attended        operator confirms a human is at the bench\n'
	exit 2
}

die()  { printf 'FAIL: %s\n' "$*" >&2; exit 1; }
say()  { printf '%s\n' "$*"; }
note() { printf 'note: %s\n' "$*" >&2; }

while [ $# -gt 0 ]; do
	case "$1" in
		--check) MODE=check ;;
		--apply) MODE=apply; N_MODES=$((N_MODES + 1)) ;;
		--rollback) MODE=rollback; N_MODES=$((N_MODES + 1)) ;;
		--verify) MODE=verify; N_MODES=$((N_MODES + 1)) ;;
		--load-check) DO_LOAD=1 ;;
		--artifacts) shift; ART=$1 ;;
		--artifacts=*) ART=$(printf '%s' "$1" | cut -c13-) ;;
		--owrt) shift; OWRT=$1 ;;
		--owrt=*) OWRT=$(printf '%s' "$1" | cut -c8-) ;;
		--host) shift; RHOST=$1 ;;
		--host=*) RHOST=$(printf '%s' "$1" | cut -c8-) ;;
		--state) shift; STATE_OVERRIDE=$1 ;;
		--state=*) STATE_OVERRIDE=$(printf '%s' "$1" | cut -c9-) ;;
		--param) shift; MODULE_PARAMS="$MODULE_PARAMS --param $1" ;;
		--param=*) MODULE_PARAMS="$MODULE_PARAMS --param $(printf '%s' "$1" | cut -c9-)" ;;
		--keep-staging) KEEP_STAGING=1 ;;
		--yes-really) YES_REALLY=1 ;;
		--attended) ATTENDED=1 ;;
		-h|--help) usage ;;
		*) echo "unknown argument: $1" >&2; usage ;;
	esac
	shift
done
if [ "$N_MODES" -gt 1 ]; then die "choose one action mode (--apply | --rollback | --verify)"; fi

# ------------------------------------------------------------------ state ---
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
if [ -n "$STATE_OVERRIDE" ]; then STATE=$STATE_OVERRIDE
else STATE=$REPO_ROOT/build/evidence-t0-deploy/$STAMP; fi
mkdir -p "$STATE" || die "cannot create evidence dir $STATE"
LOG=$STATE/deploy.log
: >"$LOG"

run_capture() { # outfile, command...
	outf=$1; shift
	"$@" >"$outf" 2>&1
	rc=$?
	cat "$outf" | tee -a "$LOG"
	return "$rc"
}

PASSES=0
FAILS=0
gate_ok()   { PASSES=$((PASSES + 1)); say "ok   $*"; }
gate_bad()  { FAILS=$((FAILS + 1)); say "FAIL $*"; }
gate_file() { # description actual expected
	if [ "$2" = "$3" ]; then gate_ok "$1"; else gate_bad "$1 (got $2 want $3)"; fi
}

# ------------------------------------------------------- host verification ---
check_manifests() {
	f=$LANE/tools/expected-packages.txt
	if [ ! -f "$f" ]; then gate_bad "expected-packages.txt missing"; return; fi
	h=$(sha256sum "$f" | awk '{print $1}')
	gate_file "expected-packages.txt sha256" "$h" "$EXPECTED_PKGS_SHA"
	f=$LANE/tools/expected-files.txt
	if [ ! -f "$f" ]; then gate_bad "expected-files.txt missing"; return; fi
	h=$(sha256sum "$f" | awk '{print $1}')
	gate_file "expected-files.txt sha256" "$h" "$EXPECTED_FILES_SHA"
}

check_artifacts() {
	if [ ! -d "$ART" ]; then gate_bad "artifacts dir missing: $ART"; return; fi
	while read -r name version asize asha isize; do
		[ -n "$name" ] || continue
		f=$ART/$name-$version.apk
		if [ ! -f "$f" ]; then gate_bad "missing $f"; continue; fi
		sz=$(wc -c <"$f" | tr -d ' ')
		gate_file "$name size" "$sz" "$asize"
		h=$(sha256sum "$f" | awk '{print $1}')
		gate_file "$name sha256" "$h" "$asha"
	done <<TABLE
$APK_TABLE
TABLE
	if [ -f "$ART/bench-image-sysupgrade.bin" ]; then
		h=$(sha256sum "$ART/bench-image-sysupgrade.bin" | awk '{print $1}')
		gate_file "bench image container sha256" "$h" "$CONTAINER_SHA"
	fi
	if [ -f "$ART/kernel-slice.bin" ]; then
		h=$(sha256sum "$ART/kernel-slice.bin" | awk '{print $1}')
		gate_file "kernel slice sha256 (must be the validated one)" "$h" "$KERNEL_SLICE_SHA"
	fi
	if [ -f "$ART/SHA256SUMS" ]; then
		bad=0
		while read -r want file; do
			case "$file" in ""|SHA256SUMS) continue ;; esac
			case "$file" in *.apk) ;; *) continue ;; esac
			if [ -f "$ART/$file" ]; then
				got=$(sha256sum "$ART/$file" | awk '{print $1}')
				if [ "$want" != "$got" ]; then gate_bad "SHA256SUMS mismatch for $file"; bad=1; fi
			fi
		done <"$ART/SHA256SUMS"
		if [ "$bad" -eq 0 ]; then gate_ok "artifacts SHA256SUMS apk lines match"; fi
	fi
}

apk_tool() {
	if [ -x "$OWRT/staging_dir/host/bin/apk" ]; then APK=$OWRT/staging_dir/host/bin/apk
	else APK=; fi
}

check_apk_metadata() {
	apk_tool
	if [ -z "$APK" ]; then
		gate_bad "host apk tool not found under $OWRT (cannot verify package metadata)"
		return
	fi
	while read -r name version asize asha isize; do
		[ -n "$name" ] || continue
		f=$ART/$name-$version.apk
		[ -f "$f" ] || continue
		dump=$STATE/apk-$name.adbdump.txt
		"$APK" adbdump "$f" >"$dump" 2>/dev/null
		if ! grep -q "^  name: $name$" "$dump"; then gate_bad "$name adbdump name"; continue; fi
		got=$(awk '/^  version:/ { print $2; exit }' "$dump")
		gate_file "$name adbdump version" "$got" "$version"
		got=$(awk '/^  installed-size:/ { print $2; exit }' "$dump")
		gate_file "$name installed-size" "$got" "$isize"
		case "$name" in
			kmod-*)
				if grep -q "kernel=$KERNEL_DEP" "$dump"; then
					gate_ok "$name depends kernel=$KERNEL_DEP"
				else
					gate_bad "$name does not depend on kernel=$KERNEL_DEP"
				fi ;;
			iw)
				if grep -q 'libnl-tiny1' "$dump"; then gate_ok "iw depends libnl-tiny1"
				else gate_bad "iw does not depend on libnl-tiny1"; fi ;;
			iwinfo)
				if grep -q 'libiwinfo20230701' "$dump"; then gate_ok "iwinfo depends libiwinfo20230701"
				else gate_bad "iwinfo does not depend on libiwinfo20230701"; fi ;;
		esac
		# the embedded hooks must be stock OpenWrt boilerplate: the known
		# helpers present, and no device/flash/module operation inside them
		hooks=$(awk '/^scripts:/ { ins = 1; next } ins && /^[a-z]/ { ins = 0 } ins' "$dump")
		if [ -z "$hooks" ]; then
			gate_bad "$name has no install hooks"
		elif printf '%s\n' "$hooks" | grep -qE 'devmem|/dev/mem|mtd|ubiformat|nvram|reboot|rmmod|insmod|modprobe|5A5A|CECE|hcc_|/tmp'; then
			gate_bad "$name install hook contains a device/flash/module operation"
			printf '%s\n' "$hooks" >>"$LOG"
		elif printf '%s\n' "$hooks" | grep -q 'default_postinst\|default_prerm\|add_group_and_user'; then
			gate_ok "$name hooks are stock OpenWrt boilerplate (no device ops)"
		else
			gate_bad "$name hooks are not the expected OpenWrt boilerplate"
		fi
	done <<TABLE
$APK_TABLE
TABLE
}

check_tree() {
	KD=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -f "$KD/.config.set" ]; then
		got=$(grep '=[ym]' "$KD/.config.set" | LC_ALL=C sort | md5sum | awk '{print $1}')
		gate_file "tree kernel vermagic (recomputed from .config.set)" "$got" "$VERMAGIC"
		if grep -q '^CONFIG_CFG80211=m' "$KD/.config"; then
			gate_ok "tree kernel config has CONFIG_CFG80211=m"
		else
			gate_bad "tree kernel config lacks CONFIG_CFG80211=m"
		fi
		if [ -f "$KD/.vermagic" ]; then
			got=$(tr -d '[:space:]' <"$KD/.vermagic")
			gate_file "tree .vermagic file" "$got" "$VERMAGIC"
		fi
	else
		note "tree kernel build dir not present; skipping the kernel-config recompute"
	fi
	if [ -f "$KD/net/wireless/cfg80211.ko" ]; then
		gate_ok "tree cfg80211.ko build product present"
	fi
}

host_check() {
	say "== T0 deploy: host-only artifact/tree verification (no device contact) =="
	say "artifacts: $ART"
	say "tree     : $OWRT"
	check_manifests
	check_artifacts
	check_apk_metadata
	check_tree
	{
		echo "mode=check"
		echo "artifacts=$ART"
		echo "owrt=$OWRT"
		echo "gates_passed=$PASSES"
		echo "gates_failed=$FAILS"
		if [ "$FAILS" -eq 0 ]; then echo "result=PASS"; else echo "result=FAIL"; fi
	} >"$STATE/check.txt"
	printf '\n== host check: %d passed, %d failed ==\n' "$PASSES" "$FAILS"
	if [ "$FAILS" -ne 0 ]; then
		printf 'RESULT: FAIL - do not deploy\n'
		exit 1
	fi
	printf 'RESULT: PASS - artifacts/tree identity verified; a device action needs --apply --attended --yes-really\n'
}

# --------------------------------------------------------------- preflight ---
preflight_host() {
	if [ "$ATTENDED" -ne 1 ]; then die "--attended is required for a device action (human at the bench)"; fi
	if [ "$YES_REALLY" -ne 1 ]; then die "--yes-really is required for a device action"; fi
	say "== host preflight =="
	BL=$(getenv T0_PREFLIGHT_BENCH_LINK)
	if [ -z "$BL" ]; then BL=$REPO_ROOT/tools/bench-link.sh; fi
	RC=$(getenv T0_PREFLIGHT_RECOVERY)
	if [ -z "$RC" ]; then RC=$REPO_ROOT/tools/recovery-card.sh; fi
	if [ ! -x "$BL" ]; then gate_bad "bench-link check not executable: $BL"
	elif "$BL" >>"$LOG" 2>&1; then gate_ok "bench link PASS ($BL)"
	else gate_bad "bench link FAILED - fix with tools/bench-link.sh --apply"; fi
	if [ ! -x "$RC" ]; then gate_bad "recovery-card check not executable: $RC"
	elif "$RC" >>"$LOG" 2>&1; then gate_ok "recovery door READY ($RC)"
	else gate_bad "recovery door NOT READY - arm TFTP/recovery image before any fire"; fi
	if [ "$FAILS" -ne 0 ]; then
		printf 'RESULT: FAIL - preflight gates failed; no device action taken\n'
		exit 1
	fi
}

# ------------------------------------------------------------ device calls ---
run_rsh() {
	if [ -n "$RSH_OVERRIDE" ]; then "$RSH_OVERRIDE" "$1"
	else ROUTER_HOST=$RHOST "$REPO_ROOT/tools/rsh.sh" "$1"; fi
}
put_file() { # local, remote path
	if [ -n "$RSCP_OVERRIDE" ]; then "$RSCP_OVERRIDE" "$1" "$RHOST:$2"
	else "$REPO_ROOT/tools/rscp.sh" -q "$1" "$RHOST:$2"; fi
}
remote_mode() { # stage, mode, extra params
	st=$1; md=$2; shift 2
	args=$*
	run_rsh "sh $st/t0-device.sh $md $st $args"
}

stage_files() {
	STAMP2=$(date -u +%Y%m%dT%H%M%SZ)
	STAGE=/tmp/t0-deploy-$STAMP2
	say "staging dir: $STAGE"
	if ! run_rsh "mkdir -p '$STAGE' && chmod 700 '$STAGE'" >>"$LOG" 2>&1; then
		die "cannot create staging dir on $RHOST (SSH/bench link?)"
	fi
	put_file "$LANE/device/t0-device.sh" "$STAGE/t0-device.sh" || die "stage t0-device.sh failed"
	put_file "$LANE/tools/expected-packages.txt" "$STAGE/expected-packages.txt" || die "stage manifests failed"
	put_file "$LANE/tools/expected-files.txt" "$STAGE/expected-files.txt" || die "stage manifests failed"
	while read -r name version asize asha isize; do
		[ -n "$name" ] || continue
		put_file "$ART/$name-$version.apk" "$STAGE/$name-$version.apk" \
			|| die "stage $name-$version.apk failed"
	done <<TABLE
$APK_TABLE
TABLE
	: >"$STATE/apk.sha256"
	while read -r name version asize asha isize; do
		[ -n "$name" ] || continue
		printf '%s  %s-%s.apk\n' "$asha" "$name" "$version" >>"$STATE/apk.sha256"
	done <<TABLE
$APK_TABLE
TABLE
	put_file "$STATE/apk.sha256" "$STAGE/apk.sha256" || die "stage checksum list failed"
	remote_rc=$(run_rsh "cd '$STAGE' && sha256sum -c apk.sha256" 2>&1 | tee -a "$LOG" | grep -c ': OK$')
	if [ "$remote_rc" -ne 7 ]; then
		gate_bad "remote sha256 re-check verified $remote_rc/7 staged artifacts"
		return 1
	fi
	gate_ok "remote sha256 re-check verified 7/7 staged artifacts"
}

write_summary() { # result
	{
		echo "mode=$MODE"
		echo "stamp=$STAMP"
		echo "host=$RHOST"
		echo "artifacts=$ART"
		echo "container_sha256=$CONTAINER_SHA"
		echo "kernel_slice_sha256=$KERNEL_SLICE_SHA"
		echo "kernel_package=$KERNEL_DEP"
		echo "gates_passed=$PASSES"
		echo "gates_failed=$FAILS"
		echo "result=$1"
	} >"$STATE/summary.txt"
	printf '== T0 %s: %d gates passed, %d failed - RESULT %s ==\n' "$MODE" "$PASSES" "$FAILS" "$1"
	printf 'evidence: %s\n' "$STATE"
}

device_flow() { # action: install|rollback|verify
	action=$1
	host_check
	if [ "$FAILS" -ne 0 ]; then die "host verification failed; no device action"; fi
	PASSES=0; FAILS=0
	preflight_host
	say "== device reachability ($RHOST) =="
	res=$(run_rsh "echo T0-REACHABLE; uname -a; apk --version 2>/dev/null | head -n1; cat /proc/sys/kernel/hostname 2>/dev/null" 2>&1 | tee -a "$LOG")
	case "$res" in
		*T0-REACHABLE*) gate_ok "device reachable over SSH" ;;
		*) die "device not reachable at $RHOST (bench link/key?)" ;;
	esac
	say "== staging =="
	stage_files || die "staging failed"
	OUT=$STATE/device-$action.txt
	say "== device $action =="
	if run_capture "$OUT" remote_mode "$STAGE" "$action"; then
		gate_ok "device $action reported PASS"
	else
		gate_bad "device $action reported FAIL"
	fi
	if [ "$action" = install ] && [ "$DO_LOAD" -eq 1 ]; then
		say "== device load check (rootfs modules; write-free probe) =="
		# shellcheck disable=SC2086 # MODULE_PARAMS is a deliberate list
		if run_capture "$STATE/device-load.txt" remote_mode "$STAGE" load $MODULE_PARAMS; then
			gate_ok "rootfs module load + probe PASS"
		else
			gate_bad "rootfs module load FAILED"
		fi
	fi
	if [ "$KEEP_STAGING" -eq 0 ] && [ "$FAILS" -eq 0 ]; then
		run_rsh "rm -rf '$STAGE'" >>"$LOG" 2>&1 || note "could not remove $STAGE"
	else
		say "staging kept on device: $STAGE"
	fi
	if [ "$FAILS" -ne 0 ]; then
		if [ "$action" = install ]; then
			say "== automatic rollback after a failed install =="
			if run_capture "$STATE/device-rollback-auto.txt" remote_mode "$STAGE" rollback; then
				gate_ok "automatic rollback PASS"
			else
				gate_bad "automatic rollback FAILED - run: deploy-t0.sh --rollback --attended --yes-really"
			fi
		fi
		write_summary FAIL
		exit 1
	fi
	write_summary PASS
	say ""
	say "next steps:"
	if [ "$action" = install ]; then
		say "  1. reboot when convenient (the next bench session needs a fresh boot)"
		say "  2. after boot: deploy-t0.sh --verify --attended --yes-really   (read-only)"
		say "  3. bench validation: see BENCH-VALIDATION.md (native rootfs iw/iwinfo/driver)"
	else
		say "  package set rolled back; loaded modules stay until the next reboot"
	fi
}

case "$MODE" in
	check) host_check ;;
	apply) device_flow install ;;
	rollback) device_flow rollback ;;
	verify) device_flow verify ;;
	*) die "unknown mode" ;;
esac
