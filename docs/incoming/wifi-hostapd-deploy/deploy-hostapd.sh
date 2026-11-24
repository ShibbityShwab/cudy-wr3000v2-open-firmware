#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# deploy-hostapd.sh - host-side orchestrator for the wifi-hostapd-deploy T0
# zero-flash deploy (shared task task-125).
#
# Installs the four AP-start hostapd .apk packages plus the two bench config
# fixtures into the RUNNING pair-B rootfs over SSH (persistent overlay; no
# flash write, no boot-selector change, no reboot, no module load/unload) and
# verifies the result on the device.  Rollback is apk del + remove the config
# dir - no reboot.
#
# Modes (default: --check, never contacts the router):
#   --check      host-only: pins, apk metadata/depends closure, payload
#                extraction, ELF/ABI checks, config validity (tree-source
#                hostapd), ladder-recipe consistency, script safety audit
#   --apply      attended install: preflight -> stage -> apk add -> read-back
#   --verify     attended read-only post-install verification
#   --rollback   attended: apk del + remove /root/hostapd-deploy
#
# Hard rules honoured: writes only the rootfs overlay (/usr, /etc,
# /lib/netifd, /lib/apk db+world, /root/hostapd-deploy); staging only in
# /tmp; no flash, no boot selector, no /dev/mem, no UART, no rmmod, no
# reboot.  Every device action requires --attended AND --yes-really plus a
# PASSING bench-link check and a READY recovery door.
#
# The device-side half is device/hostapd-device.sh; the host-side checks are
# the same ones tools/selftest.sh runs (plus mutation tests).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$HERE
OPENSRC=$(cd "$HERE/../../.." && pwd)
REPO_ROOT=$(cd "$OPENSRC/.." && pwd)
DEFAULT_ART=/home/shibbityshwab/hostapd-lane-artifacts
DEFAULT_OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
DEFAULT_HOST=root@192.168.1.1

# Pinned identities of the host check inputs (authoritative; a local edit of
# these tables fails the check closed).
EXPECTED_PKGS_SHA=b8189e9eef8a0c8e255a6cdc75fb45ab0fa63207f0eed86145769d8bf04aa416
EXPECTED_FILES_SHA=052ee2b83839836aebebb08e93358fa6912df000fbe187b7f4bf62fd2f0b7ecd
EXPECTED_BASE_SHA=813d677fae3f24a53784d67e5f05b0592b3b960ad08c200d4a7754ccf9d87f6b
REMOTE_STAGE=/tmp/hostapd-deploy-stage
REMOTE_DIR=/root/hostapd-deploy

MODE=check
ATTENDED=0
YES_REALLY=0
KEEP_ON_FAILURE=0
ART=$DEFAULT_ART
OWRT=$DEFAULT_OWRT
RHOST=$DEFAULT_HOST
EVIDENCE=
WORK=

PASS=0
FAIL=0
ok()   { PASS=$((PASS+1)); printf 'ok   %s\n' "$*"; }
bad()  { FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$*"; }
eq()   { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2' want '$3')"; fi; }
say()  { printf '== %s\n' "$*"; }
kv()   { printf 'KV %s=%s\n' "$1" "$2"; }
die()  { bad "$*"; finish; }
finish() {
	printf '\n== deploy-hostapd: %d passed, %d failed (mode=%s) ==\n' "$PASS" "$FAIL" "$MODE"
	if [ -n "$WORK" ] && [ -d "$WORK" ]; then rm -rf "$WORK"; fi
	if [ "$FAIL" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
	printf 'RESULT: PASS\n'
	exit 0
}
usage() { sed -n '3,30p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
	case "$1" in
		--check) MODE=check ;;
		--apply) MODE=apply ;;
		--verify) MODE=verify ;;
		--rollback) MODE=rollback ;;
		--attended) ATTENDED=1 ;;
		--yes-really) YES_REALLY=1 ;;
		--keep-on-failure) KEEP_ON_FAILURE=1 ;;
		--artifacts) shift; ART=$1 ;;
		--artifacts=*) ART=${1#*=} ;;
		--owrt) shift; OWRT=$1 ;;
		--owrt=*) OWRT=${1#*=} ;;
		--host) shift; RHOST=$1 ;;
		--host=*) RHOST=${1#*=} ;;
		--evidence) shift; EVIDENCE=$1 ;;
		--evidence=*) EVIDENCE=${1#*=} ;;
		-h|--help) usage; exit 0 ;;
		*) echo "unknown argument: $1" >&2; usage; exit 2 ;;
	esac
	shift
done

if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")" || exit 1
	exec > >(tee "$EVIDENCE") 2>&1
fi
WORK=$(mktemp -d /tmp/hostapd-deploy-check.XXXXXX) || { echo "cannot create work dir"; exit 1; }

# ------------------------------------------------------------- transports ---
rsh_cmd() { ROUTER_HOST="$RHOST" "$REPO_ROOT/tools/rsh.sh" "$@"; }

need_attended() {
	if [ "$ATTENDED" -ne 1 ]; then bad "device action needs --attended (human at the bench)"; finish; fi
	if [ "$YES_REALLY" -ne 1 ]; then bad "device action needs --yes-really"; finish; fi
	if [ ! -x "$REPO_ROOT/tools/rsh.sh" ]; then bad "missing $REPO_ROOT/tools/rsh.sh"; finish; fi
}

preflight_host() {
	say "host preflight"
	local bl=$REPO_ROOT/tools/bench-link.sh rc=$REPO_ROOT/tools/recovery-card.sh
	if [ ! -x "$bl" ]; then
		bad "bench-link check not executable: $bl"
	elif "$bl" >>"$WORK/preflight.log" 2>&1; then ok "bench link PASS ($bl)"
	else bad "bench link FAILED - fix with tools/bench-link.sh --apply"; fi
	if [ ! -x "$rc" ]; then
		bad "recovery-card check not executable: $rc"
	elif "$rc" >>"$WORK/preflight.log" 2>&1; then ok "recovery door READY ($rc)"
	else bad "recovery door NOT READY - do not fire"; fi
	if [ "$FAIL" -ne 0 ]; then die "host preflight failed; no device action taken"; fi
}

# ----------------------------------------------------------- host checks ---
hash_file() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }

table_field() { awk -v n="$1" -v f="$2" '$1 == n { print $(f + 1); exit }' "$LANE/tools/expected-packages.txt"; }
table_names() { awk '$1 !~ /^#/ && NF >= 5 { print $1 }' "$LANE/tools/expected-packages.txt"; }

locate_apk() { # name version -> path or empty
	local name=$1 version=$2 c
	for c in "$ART/$name-$version.apk" "$OWRT/bin/packages/arm_cortex-a9/base/$name-$version.apk"; do
		[ -f "$c" ] && { printf '%s' "$c"; return 0; }
	done
	f=$(find "$OWRT/bin/packages" -name "$name-$version.apk" 2>/dev/null | head -n1)
	[ -n "$f" ] && printf '%s' "$f"
}

check_tools() {
	say "host check inputs"
	[ -d "$LANE/tools" ] || die "lane tools dir missing"
	local a=("$LANE/tools/expected-packages.txt" "$LANE/tools/expected-files.txt" "$LANE/tools/image-baseline-packages.txt")
	for f in "${a[@]}"; do [ -f "$f" ] && ok "table present: $(basename "$f")" || bad "missing table $f"; done
	eq "expected-packages.txt sha256" "$(hash_file "$LANE/tools/expected-packages.txt")" "$EXPECTED_PKGS_SHA"
	eq "expected-files.txt sha256" "$(hash_file "$LANE/tools/expected-files.txt")" "$EXPECTED_FILES_SHA"
	eq "image-baseline-packages.txt sha256" "$(hash_file "$LANE/tools/image-baseline-packages.txt")" "$EXPECTED_BASE_SHA"
	HOSTAPK=$OWRT/staging_dir/host/bin/apk
	if [ -x "$HOSTAPK" ]; then ok "OpenWrt host apk-tools: $HOSTAPK"
	else bad "host apk-tools missing: $HOSTAPK"; fi
	READELF=$(command -v readelf 2>/dev/null || true)
	if [ -z "$READELF" ]; then
		READELF=$(ls "$OWRT"/staging_dir/toolchain-*/bin/*-readelf 2>/dev/null | head -n1)
	fi
	if [ -n "$READELF" ] && [ -x "$READELF" ]; then ok "readelf: $READELF"
	else bad "no readelf available"; fi
	for t in python3 tar; do
		command -v "$t" >/dev/null 2>&1 && ok "tool: $t" || bad "missing tool: $t"
	done
	if [ -x "$LANE/tools/build-host-hostapd.sh" ]; then ok "host-config validator present"
	else bad "missing tools/build-host-hostapd.sh"; fi
}

check_apks() {
	say "apk identity"
	local n=0
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		n=$((n+1))
		f=$(locate_apk "$name" "$version")
		if [ -z "$f" ]; then bad "artifact missing: $name-$version.apk (checked $ART and $OWRT/bin/packages)"; continue; fi
		sz=$(wc -c <"$f" | tr -d ' ')
		eq "$name size" "$sz" "$asize"
		eq "$name sha256" "$(hash_file "$f")" "$asha"
		printf '%s\n' "$f" >"$WORK/$name.path"
	done <"$LANE/tools/expected-packages.txt"
	kv packages "$n"
	[ "$n" -eq 4 ] && ok "4-package deploy set" || bad "$n package rows (want 4)"
}

apk_info() { # adbdump file, extract "  field: value" of the info block
	awk -v f="$2" '
		/^info:/ { info = 1; next }
		info && /^paths:/ { info = 0 }
		info && $1 == f":" { $1 = ""; sub(/^ +/, ""); print; exit }
	' "$1"
}

check_metadata() {
	say "apk metadata + dependency closure"
	: >"$WORK/set-names.txt"
	: >"$WORK/depends.txt"
	while read -r name version asize asha isize; do
		case "$name" in ''|\#*) continue ;; esac
		f=$(cat "$WORK/$name.path")
		"$HOSTAPK" adbdump "$f" >"$WORK/$name.adbdump" 2>"$WORK/$name.adbdump.err" || { bad "$name adbdump rc"; continue; }
		eq "$name metadata name" "$(apk_info "$WORK/$name.adbdump" name)" "$name"
		eq "$name metadata version" "$(apk_info "$WORK/$name.adbdump" version)" "$version"
		eq "$name metadata arch" "$(apk_info "$WORK/$name.adbdump" arch)" "arm_cortex-a9"
		eq "$name installed-size" "$(apk_info "$WORK/$name.adbdump" installed-size)" "$isize"
		printf '%s\n' "$name" >>"$WORK/set-names.txt"
		awk '
			/^info:/ { info = 1; next }
			info && /^paths:/ { info = 0 }
			info && /^  depends:/ { d = 1; next }
			info && d && /^    - / { split($0, a, "- "); print a[2]; next }
			info && d && !/^    - / { d = 0 }
		' "$WORK/$name.adbdump" >>"$WORK/depends.txt"
	done <"$LANE/tools/expected-packages.txt"
	sort -u "$WORK/depends.txt" -o "$WORK/depends.txt"
	awk '$1 !~ /^#/ && NF >= 2 { print $1 }' "$LANE/tools/image-baseline-packages.txt" | sort -u >"$WORK/baseline-names.txt"
	local unknown=0 d base
	while read -r d; do
		[ -n "$d" ] || continue
		base=${d%%[<>=]*}
		if grep -qx "$base" "$WORK/set-names.txt"; then continue; fi
		if grep -qx "$base" "$WORK/baseline-names.txt"; then continue; fi
		unknown=$((unknown+1))
		bad "dependency '$base' is neither in the deploy set nor in the bench-image baseline"
	done <"$WORK/depends.txt"
	kv dependencies "$(wc -l <"$WORK/depends.txt" | tr -d ' ')"
	kv unresolved_dependencies "$unknown"
	[ "$unknown" -eq 0 ] && ok "every dependency resolves in the set or the baseline image"
}

check_payload() {
	say "payload extraction + installed-file manifest"
	local payload=$WORK/payload
	mkdir -p "$payload"
	local apks=()
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		apks+=("$(cat "$WORK/$name.path")")
	done <"$LANE/tools/expected-packages.txt"
	if ( cd "$payload" && "$HOSTAPK" extract --allow-untrusted --no-network "${apks[@]}" ) >"$WORK/extract.log" 2>&1; then
		ok "apk extract rc=0 (4 packages extracted)"
	else
		bad "apk extract failed (see work/extract.log)"; return
	fi
	local n=0
	while read -r path sha size; do
		case "$path" in ''|\#*) continue ;; esac
		n=$((n+1))
		if [ ! -f "$payload/$path" ]; then bad "manifest file missing from payload: $path"; continue; fi
		eq "file $path sha256" "$(hash_file "$payload/$path")" "$sha"
		eq "file $path size" "$(wc -c <"$payload/$path" | tr -d ' ')" "$size"
	done <"$LANE/tools/expected-files.txt"
	kv manifest_files "$n"
	[ "$n" -eq 8 ] && ok "8 installed payload files" || bad "$n payload files (want 8)"
	# no unexpected payload files (apk db metadata under lib/apk/packages/ excluded)
	( cd "$payload" && find . -type f ! -path './lib/apk/packages/*' -printf '%P\n' | sort ) >"$WORK/payload-files.txt"
	awk '$1 !~ /^#/ && NF >= 3 { print $1 }' "$LANE/tools/expected-files.txt" | sort >"$WORK/manifest-files.txt"
	if diff -u "$WORK/manifest-files.txt" "$WORK/payload-files.txt" >"$WORK/payload-diff.txt" 2>&1; then
		ok "payload contains exactly the manifest files (no extras, no omissions)"
	else
		bad "payload/manifest mismatch (see work/payload-diff.txt)"; head -20 "$WORK/payload-diff.txt"
	fi
}

check_elf() {
	say "ELF / ABI"
	local hp=$WORK/payload/usr/sbin/hostapd
	if [ ! -f "$hp" ]; then bad "hostapd binary missing from payload"; return; fi
	"$READELF" -h "$hp" >"$WORK/hostapd.elfhdr" 2>&1 || { bad "readelf -h failed"; return; }
	eq "hostapd ELF class" "$(awk '/Class:/ {print $2}' "$WORK/hostapd.elfhdr")" "ELF32"
	eq "hostapd ELF machine" "$(awk '/Machine:/ {print $2}' "$WORK/hostapd.elfhdr")" "ARM"
	if grep -q 'Version5 EABI' "$WORK/hostapd.elfhdr"; then ok "hostapd EABI5"; else bad "hostapd not EABI5"; fi
	if grep -q 'DYN' "$WORK/hostapd.elfhdr"; then ok "hostapd is a PIE (OpenWrt PKG_ASLR_PIE)"; else bad "hostapd is not PIE (DYN)"; fi
	"$READELF" -l "$hp" 2>/dev/null | grep -q '/lib/ld-musl-arm\.so\.1' && ok "hostapd interpreter /lib/ld-musl-arm.so.1" || bad "hostapd musl interpreter missing"
	"$READELF" -d "$hp" 2>/dev/null | awk '/NEEDED/ { gsub(/\[/, "", $NF); gsub(/\]/, "", $NF); print $NF }' | sort -u >"$WORK/needed.txt"
	kv needed_libs "$(tr '\n' ' ' <"$WORK/needed.txt")"
	declare -A so_pkg=(
		[libubox.so.20261103]=libubox20261103
		[libubus.so.20261028]=libubus20261028
		[libblobmsg_json.so.20261103]=libblobmsg-json20261103
		[libucode.so.20261009]=libucode20261009
		[libnl-tiny.so.1]=libnl-tiny1
		[libudebug.so]=libudebug
		[libgcc_s.so.1]=libgcc1
		[libc.so]=libc
	)
	local n=0 unknown=0 lib pkg
	while read -r lib; do
		[ -n "$lib" ] || continue
		n=$((n+1))
		pkg=${so_pkg[$lib]:-}
		if [ -z "$pkg" ]; then bad "NEEDED $lib has no baseline package mapping"; unknown=$((unknown+1)); continue; fi
		if grep -qx "$pkg" "$WORK/baseline-names.txt"; then
			ok "NEEDED $lib <- baseline package $pkg"
		elif grep -qx "$pkg" "$WORK/set-names.txt"; then
			ok "NEEDED $lib <- deploy-set package $pkg"
		else
			bad "NEEDED $lib needs package $pkg which is not in the image baseline or set"; unknown=$((unknown+1))
		fi
	done <"$WORK/needed.txt"
	kv needed_unresolved "$unknown"
	[ "$n" -ge 6 ] && ok "$n DT_NEEDED libraries checked" || bad "only $n DT_NEEDED libraries (expected >=6)"
}

check_configs() {
	say "config fixture validity (tree-source hostapd v2.12)"
	if "$LANE/tools/validate-configs.sh" --evidence "$WORK/config-validation.txt" >"$WORK/config-validate.out" 2>&1; then
		ok "config fixtures parse with hostapd v2.12 (negative control caught)"
	else
		bad "config validation failed (see work/config-validate.out)"
	fi
	# semantic gates the parser cannot express
	awk 'BEGIN{FS="="} /^[a-z_]+=/{gsub(/ /,"",$1); v[$1]=$2} END{}' /dev/null
	local f
	for f in "$LANE/configs/hostapd-open.conf" "$LANE/configs/hostapd-wpa2.conf"; do
		grep -q '^interface=wlan0$' "$f" && ok "$(basename "$f") interface=wlan0" || bad "$(basename "$f") interface line"
		grep -q '^beacon_int=100$' "$f" && ok "$(basename "$f") beacon_int=100" || bad "$(basename "$f") beacon_int line"
		grep -q '^dtim_period=2$' "$f" && ok "$(basename "$f") dtim_period=2" || bad "$(basename "$f") dtim_period line"
	done
	if grep -qE '^wpa=' "$LANE/configs/hostapd-open.conf"; then bad "open fixture contains a wpa= line"; else ok "open fixture is an open network"; fi
	if grep -q '^wpa_passphrase=CHANGEME' "$LANE/configs/hostapd-wpa2.conf"; then ok "wpa2 fixture uses a placeholder passphrase only"; else bad "wpa2 fixture passphrase is not the placeholder"; fi
}

check_ladder() {
	say "AP-start ladder recipe"
	if ( cd "$LANE" && python3 tools/check-ladder.py ) >"$WORK/ladder-check.out" 2>&1; then
		ok "ladder recipe consistent ($(grep -o '[0-9]* passed' "$WORK/ladder-check.out" | head -n1))"
	else
		bad "ladder recipe check failed (see work/ladder-check.out)"; tail -10 "$WORK/ladder-check.out"
	fi
}

check_scripts() {
	say "script syntax + static safety audit"
	local f
	for f in "$LANE/deploy-hostapd.sh" "$LANE/device/hostapd-device.sh" "$LANE"/tools/*.sh; do
		[ -f "$f" ] || continue
		if bash -n "$f" 2>"$WORK/syntax.err"; then ok "bash -n $(basename "$f")"
		else bad "bash -n $(basename "$f")"; cat "$WORK/syntax.err"; fi
	done
	if python3 -m py_compile "$LANE/tools/check-ladder.py" 2>"$WORK/pycompile.err"; then ok "python compile check-ladder.py"
	else bad "python compile check-ladder.py"; cat "$WORK/pycompile.err"; fi
	# static audit: forbidden operations may appear only in comments.
	local hits=0 hit f2
	for f2 in "$LANE/deploy-hostapd.sh" "$LANE/device/hostapd-device.sh" "$LANE"/tools/*.sh; do
		[ -f "$f2" ] || continue
		hit=$(awk '{ sub(/#.*/, ""); print }' "$f2" | grep -nE '(^|[^a-z_])mtd([^a-z_]|$)|ubiformat|sysupgrade|nvram|rmmod|/dev/mem|boot_reg|bootflag|0x400392f0|reboot|uart|minicom|picocom' || true)
		if [ -n "$hit" ]; then bad "forbidden operation in $(basename "$f2"): $hit"; hits=$((hits+1)); fi
	done
	[ "$hits" -eq 0 ] && ok "no flash/selector/devmem/module/power-cycle/UART operation in any lane script"
}

do_check() {
	say "wifi-hostapd-deploy host check (never contacts the router)"
	check_tools
	check_apks
	check_metadata
	check_payload
	check_elf
	check_configs
	check_ladder
	check_scripts
	finish
}

# ------------------------------------------------------- device sessions ---
stage_device() {
	say "stage the deploy set on $RHOST:$REMOTE_STAGE"
	if ! rsh_cmd "mkdir -p $REMOTE_STAGE && rm -f $REMOTE_STAGE/*.apk"; then
		bad "cannot create remote stage $REMOTE_STAGE"; finish
	fi
	local files=("$LANE/tools/expected-packages.txt" "$LANE/tools/expected-files.txt" "$LANE/device/hostapd-device.sh" "$LANE/configs/hostapd-open.conf" "$LANE/configs/hostapd-wpa2.conf")
	local f name version
	while read -r name version asize asha _; do
		case "$name" in ''|\#*) continue ;; esac
		files+=("$(cat "$WORK/$name.path")")
	done <"$LANE/tools/expected-packages.txt"
	for f in "${files[@]}"; do
		local base
		base=$(basename "$f")
		if cat "$f" | rsh_cmd "cat > $REMOTE_STAGE/$base"; then :; else
			bad "push failed: $base"; finish
		fi
	done
	# verify the staging identities on the box with the device script itself
	rsh_cmd "sh $REMOTE_STAGE/hostapd-device.sh preflight" >"$WORK/device-preflight.out" 2>&1
	cat "$WORK/device-preflight.out"
	if grep -q '^HOSTAPD-DEV-RESULT .*failed=0 stopped=0' "$WORK/device-preflight.out"; then
		ok "device preflight PASS (staging + image + overlay + apk v3)"
	else
		bad "device preflight FAILED - no install performed"
		finish
	fi
}

device_mode() { # mode
	local mode=$1
	say "device action: $mode"
	rsh_cmd "sh $REMOTE_STAGE/hostapd-device.sh $mode" >"$WORK/device-$mode.out" 2>&1
	cat "$WORK/device-$mode.out"
	local res
	res=$(grep '^HOSTAPD-DEV-RESULT' "$WORK/device-$mode.out" | tail -n1)
	kv "device_result_$mode" "${res:-none}"
	if printf '%s' "$res" | grep -q 'failed=0 stopped=0'; then
		ok "device $mode PASS"
		return 0
	fi
	bad "device $mode FAILED"
	return 1
}

device_var() { # KV name from the last device output
	grep "^KV $1=" "$WORK/device-install.out" 2>/dev/null | tail -n1 | cut -d= -f2-
}

do_apply() {
	need_attended
	preflight_host
	do_check
	stage_device
	device_mode install || {
		local mutated
		mutated=$(device_var install_mutated)
		kv install_mutated "${mutated:-unknown}"
		if [ "${mutated:-0}" = 1 ] && [ "$KEEP_ON_FAILURE" -ne 1 ]; then
			say "automatic rollback of this run's install (install_mutated=1)"
			device_mode rollback || bad "automatic rollback FAILED - inspect before any retry"
		else
			say "rollback not automatic (mutated=${mutated:-unknown}, keep-on-failure=$KEEP_ON_FAILURE)"
		fi
		finish
	}
	device_mode verify || finish
	say "installed: hostapd v2.12 + fixtures; next step = AP-START-LADDER.md (attended ladder)"
	finish
}

do_verify() {
	need_attended
	preflight_host
	do_check
	stage_device
	device_mode verify || finish
	finish
}

do_rollback() {
	need_attended
	preflight_host
	do_check
	stage_device
	device_mode rollback || finish
	finish
}

say "wifi-hostapd-deploy / task-125 / mode=$MODE / host=$RHOST"
case "$MODE" in
	check) do_check ;;
	apply) do_apply ;;
	verify) do_verify ;;
	rollback) do_rollback ;;
esac