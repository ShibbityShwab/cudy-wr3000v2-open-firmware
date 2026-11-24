#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - device-free contract checks for the wifi-abi-image lane
# (task-128: kmod-ABI image rebuild + flash runbook).
#
# Checks the lane's static contracts, not the world: scripts are syntactically
# valid, contain no device/flash command, the runbook names every gate and the
# exact artifacts, the pinned hashes agree between scripts/docs, and (when the
# host-only build has run) the recorded evidence shows PASS and the extracted
# flashable members carry the pinned hashes.
#
# usage: ./tools/selftest.sh [-h|--help]
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
REPO=$(cd "$LANE/../../../.." && pwd)
EVID=$LANE/build-evidence
ART=$REPO/build/abi-image-artifacts
PASSES=0
FAILS=0
ok()  { PASSES=$((PASSES + 1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
chk() { desc=$1; shift; if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi; }
chkeq() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1 (got '$2' want '$3')"; fi; }

for a in "$@"; do
	case "$a" in -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;; *) bad "unknown argument: $a";; esac
done

printf '== wifi-abi-image lane selftest (device-free) ==\n'
printf 'lane: %s\n\n' "$LANE"

# ---- 1. files exist ------------------------------------------------------
for f in README.md REPORT.md FLASH-RUNBOOK.md tools/build-abi-image.sh tools/verify-abi-image.sh tools/selftest.sh; do
	chk "$f exists" test -s "$LANE/$f"
done
chk "build script executable" test -x "$LANE/tools/build-abi-image.sh"
chk "verify script executable" test -x "$LANE/tools/verify-abi-image.sh"

# ---- 2. syntax -----------------------------------------------------------
for s in build-abi-image verify-abi-image selftest; do
	chk "$s.sh bash -n" bash -n "$LANE/tools/$s.sh"
done

# ---- 3. host-only: no device/flash command in the scripts ---------------
# The two scripts must never be able to touch the router or flash a slot.
for s in build-abi-image verify-abi-image; do
	f=$LANE/tools/$s.sh
	for token in 'ssh ' 'rsh.sh' 'rscp.sh' 'mtd ' 'ubiformat' 'devmem' 'insmod ' 'rmmod ' '/dev/mtd' 'flash_erase' 'nandwrite' '/sbin/sysupgrade' 'reboot '; do
		if grep -vE '^\s*#' "$f" | grep -qF -- "$token"; then
			bad "$s.sh must not contain the device/flash token '$token' outside comments"
		else
			ok "$s.sh has no '$token' command"
		fi
	done
done

# ---- 4. build script safety markers ------------------------------------
f=$LANE/tools/build-abi-image.sh
chk "build serializes with flock" grep -q 'flock -n 9' "$f"
chk "build refuses to run while a make works in the tree" grep -q 'pgrep -x make' "$f"
chk "build restores .config byte-identically" grep -q 'byte-identical to pre-build' "$f"
chk "build restores the rootfs staging tree" grep -q 'rootfs staging tree (no bench packages left installed)' "$f"
chk "build verifies the kernel package version" grep -q 'verify-abi-image.sh' "$f"

# ---- 5. verify script pins ---------------------------------------------
f=$LANE/tools/verify-abi-image.sh
chk "verify pins the validated kernel slice" grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' "$f"
chk "verify pins the kernel slot hash" grep -q '1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e' "$f"
chk "verify pins the kernel vermagic" grep -q 'KERNEL_VERMAGIC=6f995278d21563ed4dd8f2609f3659c8' "$f"
chk "verify pins the kernel package dep" grep -q 'KERNEL_DEP=6.18.60~' "$f"
chk "verify checks the image apk-db kernel version" grep -q 'image apk-db kernel version' "$f"
chk "verify checks kmod .apk kernel= deps" grep -q 'artefact .* depends on kernel=' "$f"
chk "verify checks module vermagic" grep -q 'modinfo -F vermagic' "$f"

# ---- 6. shared inputs exist (read-only deps of the build) --------------
INTEG=$LANE/../wifi-image-integration
chk "integration config delta present" test -s "$INTEG/config-delta.config"
chk "kernel symbols carry-over present" test -s "$INTEG/kernel-symbols.config"
chk "integration driver-install tool present" test -x "$INTEG/tools/install-into-tree.sh"
chk "integration image verifier present" test -x "$INTEG/tools/verify-image.sh"
chk "UBI extractor present" test -s "$INTEG/tools/extract-ubi-rootfs.py"
chk "driver source present" test -s "$LANE/../wifi-cfg80211/src/Kbuild"

# ---- 7. runbook names the gates and exact artifacts --------------------
R=$LANE/FLASH-RUNBOOK.md
for needle in \
	'tools/bench-link.sh' \
	'tools/recovery-card.sh' \
	'tools/uart-capture.sh' \
	'tools/stock-telnet-install.sh' \
	'--inspect' \
	'--apply --attended --recovery-armed' \
	'ubiformat' \
	'1794a531' \
	'6f995278' \
	'query --fields version kernel' \
	'rollback' \
	'pair A' \
	'RESET' \
	'0x400392F0' \
	'never write the active pair'; do
	chk "runbook names '$needle'" grep -qF -- "$needle" "$R"
done
chk "runbook warns about the active pair/ubiformat hazard" grep -qE 'active pair|running pair' "$R"
chk "runbook references the saved stock rollback" grep -qE 'stock pair A' "$R"

# ---- 8. docs agree with the pinned values ------------------------------
chk "report names the kernel slot hash" grep -q '1794a531ed19' "$LANE/REPORT.md"
chk "report names the kernel package pin" grep -q '6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1' "$LANE/REPORT.md"
chk "readme names the flash runbook" grep -q 'FLASH-RUNBOOK.md' "$LANE/README.md"

# ---- 9. recorded evidence (only after a build) -------------------------
if [ -s "$EVID/build-summary.txt" ]; then
	chk "build evidence records result PASS" grep -q 'result *: PASS' "$EVID/build-summary.txt"
	chk "build evidence records the kernel package dep" grep -q '6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1' "$EVID/abi-verify.txt"
	chk "abi verification PASS" grep -q 'RESULT: PASS' "$EVID/abi-verify.txt"
	chk "abi verification has zero failed checks" grep -q ' 0 failed' "$EVID/abi-verify.txt"
	chk "integration verifier PASS" grep -q 'RESULT: PASS' "$EVID/integration-verifier.txt"
	chk "container recorded in the build summary" grep -q 'container size / sha256' "$EVID/build-summary.txt"
	chk "kernel slice recorded in the build summary" grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' "$EVID/build-summary.txt"
	chk "rootfs UBI recorded in the build summary" grep -q 'rootfs ubi size / sha256' "$EVID/build-summary.txt"
else
	bad "build-evidence/build-summary.txt missing (run tools/build-abi-image.sh first)"
fi

# ---- 10. flashable artifacts (only after a build; gitignored) ----------
if [ -s "$ART/kernel-slice.bin" ]; then
	chkeq "artifacts kernel-slice.bin sha256" "$(sha256sum "$ART/kernel-slice.bin" | cut -d' ' -f1)" "13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f"
	chk "artifacts rootfs.ubi present" test -s "$ART/rootfs.ubi"
	chk "artifacts SHA256SUMS present" test -s "$ART/SHA256SUMS"
	chkeq "artifacts SHA256SUMS has 14 entries" "$(wc -l < "$ART/SHA256SUMS")" "14"
	chk "artifacts SHA256SUMS verifies (sha256sum -c)" sh -c "cd '$ART' && sha256sum -c SHA256SUMS"
	chk "artifact kernel full-slot hash verified in evidence" grep -q 'full kernel slot sha256 (kernelb flash contract)' "$EVID/abi-verify.txt"
else
	bad "flashable artifacts missing in $ART (run tools/build-abi-image.sh first)"
fi

printf '\n== wifi-abi-image selftest: %d passed, %d failed ==\n' "$PASSES" "$FAILS"
if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS\n'
