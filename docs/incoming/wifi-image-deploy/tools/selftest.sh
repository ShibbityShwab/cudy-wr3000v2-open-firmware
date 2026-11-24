#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - device-free checks for the wifi-image-deploy lane (task-119).
# Covers script syntax/lint, manifest identity, tree<->apk module linkage,
# safety policy (no binaries, no flash/UART/devmem/rmmod/reboot commands),
# host-only deploy behaviour (default dry-run, attended gates, tamper
# detection), a fully mocked apply/rollback device flow and - when the local
# artifacts + OpenWrt tree are present - the real host apk-transaction
# simulation.
#
# usage: tools/selftest.sh [--quick]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ART=$(printenv T0_ARTIFACTS 2>/dev/null || true)
[ -n "$ART" ] || ART=/home/shibbityshwab/wifi-image-integration-artifacts
OWRT=$(printenv T0_OWRT 2>/dev/null || true)
[ -n "$OWRT" ] || OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
QUICK=0
if [ "$#" -gt 0 ] && [ "$1" = "--quick" ]; then QUICK=1; fi

PASS=0
FAIL=0
SKIP=0
ok()   { PASS=$((PASS + 1)); printf 'ok   %s\n' "$*"; }
bad()  { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$*"; }
skip() { SKIP=$((SKIP + 1)); printf 'skip %s\n' "$*"; }
chk()  { if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }
chkeq(){ if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2' want '$3')"; fi; }

TMP=$(mktemp -d /tmp/t0-selftest.XXXXXX)
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

printf '== wifi-image-deploy selftest ==\n'
printf 'lane: %s\n' "$LANE"

# ------------------------------------------------------- files and syntax ---
chk "deploy-t0.sh present"        "[ -f '$LANE/deploy-t0.sh' ]"
chk "t0-device.sh present"        "[ -f '$LANE/device/t0-device.sh' ]"
chk "host simulator present"      "[ -f '$LANE/tools/verify-apks-host.sh' ]"
chk "README.md present"           "[ -f '$LANE/README.md' ]"
chk "REPORT.md present"           "[ -f '$LANE/REPORT.md' ]"
chk "BENCH-VALIDATION.md present" "[ -f '$LANE/BENCH-VALIDATION.md' ]"
chk "deploy-t0.sh executable"     "[ -x '$LANE/deploy-t0.sh' ]"
chk "verify-apks-host.sh executable" "[ -x '$LANE/tools/verify-apks-host.sh' ]"
chk "deploy-t0.sh bash -n"        "bash -n '$LANE/deploy-t0.sh'"
chk "host simulator bash -n"      "bash -n '$LANE/tools/verify-apks-host.sh'"
chk "selftest.sh bash -n"         "bash -n '$LANE/tools/selftest.sh'"
chk "device script sh -n"         "sh -n '$LANE/device/t0-device.sh'"
if command -v shellcheck >/dev/null 2>&1; then
	for f in "$LANE/deploy-t0.sh" "$LANE/tools/verify-apks-host.sh" "$LANE/tools/selftest.sh"; do
		out=$(shellcheck -s bash "$f" 2>&1)
		if [ -z "$out" ]; then ok "shellcheck $(basename "$f")"; else bad "shellcheck $(basename "$f")"; printf '%s\n' "$out" | head -5; fi
	done
	out=$(shellcheck -s sh "$LANE/device/t0-device.sh" 2>&1)
	if [ -z "$out" ]; then ok "shellcheck t0-device.sh"; else bad "shellcheck t0-device.sh"; printf '%s\n' "$out" | head -5; fi
else
	skip "shellcheck not installed"
fi

# -------------------------------------------------------------- manifests ---
chk "expected-packages has 7 rows" "[ \$(grep -cvE '^#|^\\s*\$' '$LANE/tools/expected-packages.txt') -eq 7 ]"
chk "expected-files has 7 rows"    "[ \$(grep -cvE '^#|^\\s*\$' '$LANE/tools/expected-files.txt') -eq 7 ]"
chk "every apk row has 5 fields"   "awk '!/^#/ && NF && NF != 5 { exit 1 }' '$LANE/tools/expected-packages.txt'"
chk "every file row has 3 fields"  "awk '!/^#/ && NF && NF != 3 { exit 1 }' '$LANE/tools/expected-files.txt'"
chk "device script embeds the kernel dep" "grep -q 'KERNEL_DEP_EXPECTED=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1' '$LANE/device/t0-device.sh'"
chk "deploy script embeds the kernel dep" "grep -q 'KERNEL_DEP=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1' '$LANE/deploy-t0.sh'"
chk "deploy does not default to a write mode" "grep -q '^MODE=check' '$LANE/deploy-t0.sh'"
chk "deploy requires --attended"    "grep -q -- '--attended is required' '$LANE/deploy-t0.sh'"
chk "deploy requires --yes-really"  "grep -q -- '--yes-really is required' '$LANE/deploy-t0.sh'"
chk "device script verifies the no-autoload policy" "grep -q 'check_no_autoload' '$LANE/device/t0-device.sh'"
chk "no script writes an autoload entry" "! grep -qE '(>|>>|tee)[[:space:]]*[^|]*modules[.]d' '$LANE/device/t0-device.sh' '$LANE/deploy-t0.sh'"
chk "deploy script has a userspace-only set" "grep -q 'USERS_TABLE=' '$LANE/deploy-t0.sh' && grep -q 'USERS_ONLY' '$LANE/deploy-t0.sh'"
chk "userspace set is exactly five packages" "[ \$(sed -n '/^USERS_TABLE=/,/^UTABLE\$/p' '$LANE/deploy-t0.sh' | grep -c '^[a-z]') -eq 5 ]"
chk "userspace set contains no kmod rows" "! sed -n '/^USERS_TABLE=/,/^UTABLE\$/p' '$LANE/deploy-t0.sh' | grep -q '^kmod-'"
chk "device script accepts --set" "grep -q -- '--set) SET=' '$LANE/device/t0-device.sh'"
chk "device script filters kmod rows for userspace" "grep -q 'kmod-' '$LANE/device/t0-device.sh'"
chk "device script refuses load for userspace set" "grep -q 'load-requires-full' '$LANE/device/t0-device.sh'"

# --------------------------------------------------------- policy / safety ---
chk "no binaries in the lane" "! find '$LANE' -type f \( -name '*.ko' -o -name '*.o' -o -name '*.apk' -o -name '*.bin' -o -name '*.ubi' -o -name '*.squashfs' -o -name '*.img' \) | grep -q ."
codefiles="$LANE/deploy-t0.sh $LANE/device/t0-device.sh $LANE/tools/verify-apks-host.sh"
# dangerous *invocations* (command position) must not appear in any script;
# the words legitimately appear in safety comments and in the hook-check pattern,
# so only a token at the start of a simple command is treated as a failure.
# strip comment lines first: safety comments legitimately name the operations
: >"$TMP/code_run.txt"
for cf in $codefiles; do grep -vE '^[[:space:]]*#' "$cf" >>"$TMP/code_run.txt"; done
# shellcheck disable=SC2086 # codefiles is a deliberate list
for pat in mtd ubiformat nvram reboot sysupgrade rmmod flash_erase; do
	if grep -qE "(^|[;&])[[:space:]]*$pat([[:space:]]|\$)" "$TMP/code_run.txt"; then
		bad "forbidden command invocation: $pat"
	else
		ok "no '$pat' command invocation in the scripts"
	fi
done
# the device-side script must not open raw device nodes or the UART
device_run=$(grep -vE '^[[:space:]]*#' "$LANE/device/t0-device.sh")
for pat in '/dev/mtd' '/dev/mem' '/dev/tty' 'ttyUSB' 'devmem'; do
	if printf '%s\n' "$device_run" | grep -qE "$pat"; then
		bad "device script references a raw device node: $pat"
	else
		ok "device script does not touch $pat"
	fi
done
chk "device script never writes to device nodes" "! grep -qE '>[[:space:]]*/dev/(mtd|mem|tty)' '$LANE/device/t0-device.sh'"

# --------------------------------------------- real artifacts + tree linkage ---
ART_OK=0
if [ -d "$ART" ] && [ -f "$ART/SHA256SUMS" ]; then
	ART_OK=1
	chk "container present" "[ -f '$ART/bench-image-sysupgrade.bin' ]"
	chk "kernel slice present" "[ -f '$ART/kernel-slice.bin' ]"
	chkeq "kernel slice is the validated one" "$(sha256sum "$ART/kernel-slice.bin" 2>/dev/null | awk '{print $1}')" "13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f"
	chkeq "container is the bench image" "$(sha256sum "$ART/bench-image-sysupgrade.bin" 2>/dev/null | awk '{print $1}')" "df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310"
	while read -r name version _ asha _; do
		[ -n "$name" ] || continue
		f=$ART/$name-$version.apk
		if [ -f "$f" ]; then
			chkeq "$name sha256" "$(sha256sum "$f" | awk '{print $1}')" "$asha"
		else
			bad "$name apk missing"
		fi
	done <<TABLE
$(sed -n '/^[^#]/p' "$LANE/tools/expected-packages.txt")
TABLE
else
	skip "local artifacts dir not present ($ART) - artifact identity checks skipped"
fi
TREE_OK=0
if [ -d "$OWRT" ]; then
	TREE_OK=1
	KD=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
	if [ -f "$KD/.config.set" ]; then
		chkeq "tree vermagic recomputed from .config.set" "$(grep '=[ym]' "$KD/.config.set" | LC_ALL=C sort | md5sum | awk '{print $1}')" "6f995278d21563ed4dd8f2609f3659c8"
		chk "tree kernel config has CONFIG_CFG80211=m" "grep -q '^CONFIG_CFG80211=m' '$KD/.config'"
		if [ "$ART_OK" -eq 1 ] && command -v modinfo >/dev/null 2>&1 && [ -x "$OWRT/staging_dir/host/bin/apk" ]; then
			TM=$OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/hi5622-wifi/hi5622_wifi.ko
			if [ -f "$TM" ]; then
				a=$(modinfo "$TM" 2>/dev/null | grep -E '^(name|vermagic|depends|license):' | sort)
				mkdir -p "$TMP/apk"
				( cd "$TMP/apk" && "$OWRT/staging_dir/host/bin/apk" extract --allow-untrusted -q \
					"$ART/kmod-hi5622-wifi-6.18.60-r1.apk" >/dev/null 2>&1 )
				b=$(modinfo "$TMP/apk/lib/modules/6.18.60/hi5622_wifi.ko" 2>/dev/null | grep -E '^(name|vermagic|depends|license):' | sort)
				if [ -n "$a" ] && [ "$a" = "$b" ]; then ok "tree driver .ko .modinfo == packaged .ko .modinfo"
				else bad "tree driver .ko .modinfo differs from packaged .ko"; fi
			fi
		fi
	else
		skip "tree kernel build dir absent - tree linkage checks skipped"
	fi
else
	skip "OpenWrt tree absent ($OWRT) - tree linkage checks skipped"
fi

# ----------------------------------------------- host-only deploy behaviour ---
# the default mode never contacts a device helper
T0_RSH=$TMP/nm-rsh T0_RSCP=$TMP/nm-rscp "$LANE/deploy-t0.sh" --state "$TMP/state-default" >"$TMP/check-default.log" 2>&1
chk "default mode never invoked a device helper" "[ ! -e '$TMP/nm-rsh' ] && [ ! -e '$TMP/nm-rscp' ]"
chk "default mode reports a result" "grep -q 'RESULT: PASS\\|RESULT: FAIL' '$TMP/check-default.log'"
if [ "$ART_OK" -eq 1 ]; then
	if "$LANE/deploy-t0.sh" --check --state "$TMP/state-check" >"$TMP/check-real.log" 2>&1; then
		ok "host check PASS on the real artifacts"
	else
		bad "host check FAIL on the real artifacts"
		tail -10 "$TMP/check-real.log"
	fi
	mkdir -p "$TMP/tampered"
	cp "$ART"/*.apk "$TMP/tampered/" 2>/dev/null
	f=$(find "$TMP/tampered" -maxdepth 1 -name '*.apk' 2>/dev/null | head -1)
	if [ -n "$f" ]; then
		printf 'X' >>"$f"
		"$LANE/deploy-t0.sh" --check --artifacts "$TMP/tampered" --state "$TMP/state-tamper" >"$TMP/check-tamper.log" 2>&1
		chk "tampered artifact fails the host check" "[ $? -ne 0 ]"
		chk "tampered artifact reported" "grep -q 'FAIL' '$TMP/check-tamper.log'"
	else
		skip "could not create tampered apk copy"
	fi
	if "$LANE/deploy-t0.sh" --check --userspace-only --state "$TMP/state-check-users" >"$TMP/check-users.log" 2>&1; then
		ok "host check PASS for the userspace-only set"
	else
		bad "host check FAIL for the userspace-only set"
		tail -10 "$TMP/check-users.log"
	fi
	chk "userspace host check used the five-package set" "grep -q 'userspace-only set: container/kernel-slice identity not required' '$TMP/check-users.log'"
	chk "userspace host check verified no kernel= dependency" "grep -q 'has no kernel= dependency' '$TMP/check-users.log' && [ \$(grep -c 'has no kernel= dependency' '$TMP/check-users.log') -eq 5 ]"
else
	skip "host-check/tamper behavior skipped (no local artifacts)"
fi
"$LANE/deploy-t0.sh" --apply --state "$TMP/state-gate1" >"$TMP/gate1.log" 2>&1
chk "apply without --attended/--yes-really is refused" "[ $? -ne 0 ]"
chk "refusal names --attended" "grep -q -- '--attended is required' '$TMP/gate1.log'"
"$LANE/deploy-t0.sh" --apply --attended --state "$TMP/state-gate2" >"$TMP/gate2.log" 2>&1
chk "apply with --attended but no --yes-really is refused" "[ $? -ne 0 ]"
chk "refusal names --yes-really" "grep -q -- '--yes-really is required' '$TMP/gate2.log'"

# --------------------------------------------------------- mocked device flow ---
mkdir -p "$TMP/mock"
cat >"$TMP/mock/rsh" <<'RSH'
#!/usr/bin/env bash
printf 'RSH %s\n' "$*" >>"$MOCK_LOG"
cmd=$*
devcmd=$(printf '%s' "$cmd" | sed "s#/tmp/t0-deploy-#$MOCK_DEV/t0-deploy-#g")
case "$cmd" in
	*T0-REACHABLE*) printf 'T0-REACHABLE\nLinux bench 6.18.60 #1 SMP\napk-tools 3.0.5, compiled for armel.\nbench\n' ;;
	*"mkdir -p"*) eval "$devcmd"; exit $? ;;
	*"sha256sum -c apk.sha256"*) eval "$devcmd"; exit $? ;;
	*"rm -rf"*) eval "$devcmd"; exit 0 ;;
	*"t0-device.sh install "*) if printf '%s' "$cmd" | grep -q -- '--set userspace'; then printf 'T0GATE install_set PASS userspace\nT0-RESULT: PASS (1 gates, 0 failed)\n'; else printf 'T0GATE install_set PASS full\nT0-RESULT: PASS (1 gates, 0 failed)\n'; fi ;;
	*"t0-device.sh preflight "*) printf 'T0GATE stage-count PASS 7 packages\nT0-RESULT: PASS (1 gates, 0 failed)\n' ;;
	*"t0-device.sh rollback "*) printf 'T0GATE apk-del PASS rc=0\nT0-RESULT: PASS (1 gates, 0 failed)\n' ;;
	*"t0-device.sh load "*) printf 'T0GATE load-wifi PASS rc=0\nT0-RESULT: PASS (1 gates, 0 failed)\n' ;;
	*"t0-device.sh verify "*) printf 'T0GATE receipt PASS present\nT0-RESULT: PASS (1 gates, 0 failed)\n' ;;
	*) printf 'UNEXPECTED\n'; exit 1 ;;
esac
RSH
cat >"$TMP/mock/rscp" <<'RSCP'
#!/usr/bin/env bash
printf 'RSCP %s\n' "$*" >>"$MOCK_LOG"
src=$1
last=""
for a in "$@"; do last=$a; done
dest=$(printf '%s' "$last" | cut -d: -f2-)
rel=$(printf '%s' "$dest" | sed 's:^/tmp/::')
mkdir -p "$MOCK_DEV/$(dirname "$rel")" || exit 1
cp "$src" "$MOCK_DEV/$rel" || exit 1
exit 0
RSCP
cat >"$TMP/mock/bench-link" <<'BL'
#!/bin/sh
echo "PASS: bench link ok (mock)"
exit 0
BL
cat >"$TMP/mock/recovery" <<'RC'
#!/bin/sh
echo "STATUS: recovery door is READY (mock)"
exit 0
RC
chmod +x "$TMP/mock/rsh" "$TMP/mock/rscp" "$TMP/mock/bench-link" "$TMP/mock/recovery"
mock_env() {
	export T0_RSH=$TMP/mock/rsh T0_RSCP=$TMP/mock/rscp
	export T0_PREFLIGHT_BENCH_LINK=$TMP/mock/bench-link T0_PREFLIGHT_RECOVERY=$TMP/mock/recovery
	export MOCK_LOG=$TMP/mock/calls.log
	export MOCK_DEV=$TMP/mock/dev
	rm -rf "$MOCK_DEV"
	mkdir -p "$MOCK_DEV"
	: >"$MOCK_LOG"
}
mock_env
"$LANE/deploy-t0.sh" --apply --attended --yes-really --state "$TMP/state-mock" >"$TMP/mock-apply.log" 2>&1
chkeq "mocked apply exits 0" "$?" "0"
chk "mocked apply reported PASS" "grep -q 'RESULT PASS' '$TMP/mock-apply.log'"
rscp_n=$(grep -c '^RSCP' "$TMP/mock/calls.log")
chk "mocked apply staged the 7 apks + script + manifests" "[ $rscp_n -ge 10 ]"
chk "mocked apply ran the device install" "grep -q 't0-device.sh install ' '$TMP/mock/calls.log'"
chk "mocked apply staged under the device /tmp" "grep -q 'mkdir -p .*/tmp/t0-deploy-' '$TMP/mock/calls.log'"
mock_env
"$LANE/deploy-t0.sh" --rollback --attended --yes-really --state "$TMP/state-mock-rb" >"$TMP/mock-rollback.log" 2>&1
chkeq "mocked rollback exits 0" "$?" "0"
chk "mocked rollback ran the device rollback" "grep -q 't0-device.sh rollback ' '$TMP/mock/calls.log'"
mock_env
"$LANE/deploy-t0.sh" --apply --userspace-only --attended --yes-really --state "$TMP/state-mock-users" >"$TMP/mock-users.log" 2>&1
chkeq "mocked userspace apply exits 0" "$?" "0"
chk "mocked userspace apply passed the userspace set to the device" "grep -q 't0-device.sh install .*--set userspace' '$TMP/mock/calls.log'"
rscp_u=$(grep -c '^RSCP' "$TMP/mock/calls.log")
chk "mocked userspace apply staged only the five apks" "[ $rscp_u -eq 9 ]"
chk "mocked userspace apply staged no kmod package" "! grep -q 'kmod-hi5622' '$TMP/mock/calls.log'"
mock_env
"$LANE/deploy-t0.sh" --apply --userspace-only --load-check --attended --yes-really --state "$TMP/state-mock-users-load" >"$TMP/mock-users-load.log" 2>&1
chk "userspace-only --load-check is refused" "[ $? -ne 0 ]"
chk "userspace-only load refusal names the full set" "grep -q -- '--load-check needs the full set' '$TMP/mock-users-load.log'"
chk "userspace-only load refusal did not contact the device" "[ ! -s '$TMP/mock/calls.log' ]"
cat >"$TMP/mock/rsh-fail" <<'RSH'
#!/usr/bin/env bash
printf 'RSH %s\n' "$*" >>"$MOCK_LOG"
cmd=$*
case "$cmd" in
	*T0-REACHABLE*) printf 'T0-REACHABLE\n' ;;
	*"mkdir -p"*) exit 0 ;;
	*"sha256sum -c apk.sha256"*) for i in 1 2 3 4 5 6 7; do printf 'ok OK\n'; done ;;
	*"t0-device.sh preflight "*) printf 'T0GATE kernel-dep FAIL kernel=mismatch\nT0-RESULT: FAIL (0 passed, 1 failed)\n'; exit 1 ;;
	*"t0-device.sh install "*) printf 'SHOULD-NOT-RUN\n' ;;
	*) exit 0 ;;
esac
RSH
chmod +x "$TMP/mock/rsh-fail"
export T0_RSH=$TMP/mock/rsh-fail MOCK_LOG=$TMP/mock/calls-fail.log
: >"$TMP/mock/calls-fail.log"
"$LANE/deploy-t0.sh" --apply --attended --yes-really --state "$TMP/state-mock-fail" >"$TMP/mock-fail.log" 2>&1
chk "device preflight FAIL aborts the deploy" "[ $? -ne 0 ]"
chk "no install step ran after the failed preflight" "! grep -q 't0-device.sh install ' '$TMP/mock/calls-fail.log'"
unset T0_RSH T0_RSCP T0_PREFLIGHT_BENCH_LINK T0_PREFLIGHT_RECOVERY MOCK_LOG

# --------------------------------------------- device script fail-closed mode ---
mkdir -p "$TMP/devstage"
cp "$LANE/tools/expected-packages.txt" "$LANE/tools/expected-files.txt" "$LANE/device/t0-device.sh" "$TMP/devstage/"
sh "$LANE/device/t0-device.sh" >/dev/null 2>&1
chkeq "device script without args prints usage and exits 2" "$?" "2"
sh "$LANE/device/t0-device.sh" install "$TMP/devstage" >"$TMP/dev-fail.log" 2>&1
chkeq "device script fails closed off the bench image" "$?" "1"
chk "device script emitted gate lines" "grep -q '^T0GATE image FAIL' '$TMP/dev-fail.log'"
chk "device script emitted a FAIL result" "grep -q '^T0-RESULT: FAIL' '$TMP/dev-fail.log'"

# ------------------------------------------------------- real simulations ---
if [ "$QUICK" -eq 0 ] && [ "$ART_OK" -eq 1 ] && [ "$TREE_OK" -eq 1 ]; then
	if "$LANE/tools/verify-apks-host.sh" >"$TMP/host-sim.log" 2>&1; then
		ok "host apk-transaction simulation PASS"
	else
		bad "host apk-transaction simulation FAIL"
		tail -20 "$TMP/host-sim.log"
	fi
	if "$LANE/tools/verify-apks-host.sh" --userspace-only >"$TMP/host-sim-users.log" 2>&1; then
		ok "host userspace-only transaction simulation PASS"
	else
		bad "host userspace-only transaction simulation FAIL"
		tail -20 "$TMP/host-sim-users.log"
	fi
else
	skip "host apk-transaction simulation skipped (quick mode or missing artifacts/tree)"
fi

# ----------------------------------------------------------------- result ---
printf '\n== wifi-image-deploy selftest: %d passed, %d failed, %d skipped ==\n' "$PASS" "$FAIL" "$SKIP"
if [ "$FAIL" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS\n'
