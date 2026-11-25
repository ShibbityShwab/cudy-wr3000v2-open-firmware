#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# selftest.sh - artifact-free tests for the wifi-image-3-flash tooling.
#
# Lane: wifi-image-3-flash (task-201).  HOST-ONLY: no artifact, device, network or
# UART is needed.  It syntax-checks the preflight, exercises its usage/error paths,
# runs its internal parser/geometry unit tests, builds a synthetic 6-member
# container to prove the member checks work, verifies the docs carry the pinned
# identities, and scans the preflight for device-contact commands.
#
# usage: ./tools/selftest.sh        (exit 0 = PASS, 1 = FAIL)
set -u
LC_ALL=C
export LC_ALL

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
PRE=$HERE/preflight-image-3.sh
RUNBOOK=$LANE/RUNBOOK.md
README=$LANE/README.md
PF=$LANE/PREFLIGHT.md
EVID=$LANE/build-evidence

PASS=0
FAIL=0
pass() { PASS=$((PASS + 1)); printf 'ok   %s\n' "$*"; }
fail() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$*"; }

TMP=$(mktemp -d) || { printf 'FAIL cannot create tmpdir\n'; exit 1; }
trap 'rm -rf "$TMP"' EXIT HUP INT TERM

# --- 1. syntax -------------------------------------------------------------
if sh -n "$PRE" 2>/dev/null; then
	pass "preflight-image-3.sh passes sh -n"
else
	fail "preflight-image-3.sh has a shell syntax error"
fi
if [ -x "$PRE" ]; then
	pass "preflight-image-3.sh is executable"
else
	fail "preflight-image-3.sh is not executable"
fi

# --- 2. help ---------------------------------------------------------------
_out=$("$PRE" --help 2>&1); _st=$?
if [ "$_st" -eq 0 ] && printf '%s' "$_out" | grep -q 'host-only preflight'; then
	pass "--help exits 0 and names the host-only preflight"
else
	fail "--help failed (exit $_st)"
fi

# --- 3. unknown option -----------------------------------------------------
"$PRE" --bogus >/dev/null 2>&1; _st=$?
if [ "$_st" -eq 2 ]; then
	pass "unknown option exits 2 (usage)"
else
	fail "unknown option exit was $_st (want 2)"
fi
"$PRE" --path nope >/dev/null 2>&1; _st=$?
if [ "$_st" -eq 2 ]; then
	pass "invalid --path exits 2 (usage)"
else
	fail "invalid --path exit was $_st (want 2)"
fi

# --- 4. print-pins ---------------------------------------------------------
_out=$("$PRE" --print-pins 2>&1); _st=$?
if [ "$_st" -eq 0 ] \
   && printf '%s' "$_out" | grep -q '4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53' \
   && printf '%s' "$_out" | grep -q '1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e' \
   && printf '%s' "$_out" | grep -q '19922944' \
   && printf '%s' "$_out" | grep -q 'hostapd'; then
	pass "--print-pins prints the container/slot/free-space/hostapd pins"
else
	fail "--print-pins output incomplete (exit $_st)"
fi

# --- 5. unit tests ---------------------------------------------------------
_out=$("$PRE" --unit-test 2>&1); _st=$?
if [ "$_st" -eq 0 ] && printf '%s' "$_out" | grep -q 'unit-test: 5 passed, 0 failed'; then
	pass "--unit-test passes (mtd geometry, 0xFF padding, /proc/mtd parser, witness parser, UBI magic)"
else
	fail "--unit-test failed (exit $_st)"
fi

# --- 6. missing container (default path) -> exit 2 ------------------------
HOME="$TMP/no-home" "$PRE" >/dev/null 2>&1; _st=$?
if [ "$_st" -eq 2 ]; then
	pass "missing default container exits 2 with a usage error"
else
	fail "missing default container exit was $_st (want 2)"
fi

# --- 7. explicit nonexistent container -> exit 2 --------------------------
"$PRE" --container "$TMP/does-not-exist.bin" >/dev/null 2>&1; _st=$?
if [ "$_st" -eq 2 ]; then
	pass "explicit nonexistent container exits 2"
else
	fail "explicit nonexistent container exit was $_st (want 2)"
fi

# --- 8. non-tar container -> exit 1 with both FAIL lines ------------------
printf 'this is not a tar archive\n' > "$TMP/not-a-container.bin"
_out=$(HOME="$TMP/no-home" "$PRE" --container "$TMP/not-a-container.bin" 2>&1); _st=$?
if [ "$_st" -eq 1 ] \
   && printf '%s' "$_out" | grep -q 'container mismatch' \
   && printf '%s' "$_out" | grep -q 'not a readable tar archive'; then
	pass "a non-tar container fails closed (exit 1, size/sha + tar FAIL)"
else
	fail "non-tar container handling wrong (exit $_st)"
fi

# --- 9. synthetic 6-member container: members parse, pins fail closed -----
mkdir -p "$TMP/ctr/sysupgrade-cudy_wr3000-v2"
printf 'BOARD=cudy_wr3000-v2\nTARGET=hisilicon/luofu\nVERSION=test\n' \
	> "$TMP/ctr/sysupgrade-cudy_wr3000-v2/CONTROL"
printf 'NOT FLASHABLE-UNTIL-INSTALLED\n' \
	> "$TMP/ctr/sysupgrade-cudy_wr3000-v2/INSTALL-STATUS.txt"
printf 'slice-size: 3372638\nslice-sha256: 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f\nslot-limit: 8650752\n' \
	> "$TMP/ctr/sysupgrade-cudy_wr3000-v2/fip-manifest.txt"
printf 'K' > "$TMP/ctr/sysupgrade-cudy_wr3000-v2/kernel"
printf 'UBI#' > "$TMP/ctr/sysupgrade-cudy_wr3000-v2/root"
( cd "$TMP/ctr" && tar -cf "$TMP/synthetic.bin" sysupgrade-cudy_wr3000-v2 ) || fail "cannot create the synthetic container"
_out=$(HOME="$TMP/no-home" "$PRE" --container "$TMP/synthetic.bin" 2>&1); _st=$?
if [ "$_st" -eq 1 ] \
   && printf '%s' "$_out" | grep -q 'PASS  container has exactly 6 tar members' \
   && printf '%s' "$_out" | grep -q 'PASS  CONTROL names BOARD=cudy_wr3000-v2 TARGET=hisilicon/luofu' \
   && printf '%s' "$_out" | grep -q 'PASS  fip-manifest slice-size/slice-sha256/slot-limit match' \
   && printf '%s' "$_out" | grep -q 'FAIL  kernel slice (member) mismatch' \
   && printf '%s' "$_out" | grep -q 'FAIL  rootfs UBI (member root) mismatch'; then
	pass "a synthetic 6-member container parses its members and fails closed on the wrong kernel/root"
else
	fail "synthetic container check failed (exit $_st)"
fi

# --- 10. docs carry the pinned identities ---------------------------------
check_doc_pin() { # FILE LABEL REGEX
	if grep -Eq "$3" "$1" 2>/dev/null; then
		pass "$2"
	else
		fail "$2"
	fi
}
check_doc_pin "$RUNBOOK" "RUNBOOK carries the container pin" '4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the kernel-slice pin" '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the kernelb slot pin" '1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the rootfs UBI pin" '90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the installed-module pin" 'a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the cfg80211 pin" 'bc4d9c73f5dae02b7e31dd378891b86a29e38eb783fa690e045a0cd6b9861b91'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the hostapd pin" 'fef44f3bdfc87c583f8d694338f9eb715c756d85422ef7c81b51e19040a838e6'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the 19,922,944 B free-space number" '19,922,944|19922944'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the 7,843,840 B container size" '7,843,840|7843840'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the kernel apk pin" '6f995278d21563ed4dd8f2609f3659c8'
check_doc_pin "$RUNBOOK" "RUNBOOK carries the image-2 rollback rootfs pin" '31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the two install paths" 'rootfs-only.*full|Path A.*Path B'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the recovery door" 'recovery-card.sh --arm'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the recovery image identity" '22,024,351|22024351'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the read-only UART capture" 'uart-capture.sh start'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the page-golden receipt" 'page CA 0x.*86807fef|0x86807fef'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the HE AP receipt" 'HE Iftypes: AP'
check_doc_pin "$RUNBOOK" "RUNBOOK documents the 5 GHz R8 gates" 'band5g=1'
check_doc_pin "$RUNBOOK" "RUNBOOK has the STOP-conditions section" '## 10. STOP conditions'
check_doc_pin "$README" "README carries the container pin" '4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53'
check_doc_pin "$README" "README carries the 19,922,944 B free-space number" '19,922,944|19922944'
check_doc_pin "$README" "README carries the 7,843,840 B container size" '7,843,840|7843840'
check_doc_pin "$PF" "PREFLIGHT documents the --strict fire gate" '\-\-strict'
check_doc_pin "$PF" "PREFLIGHT documents the witness file format" 'kernelb'

# --- 11. committed evidence ----------------------------------------------
if [ -f "$EVID/preflight-run.txt" ] \
   && grep -q 'preflight-image-3: 23 passed, 0 failed, 0 warnings' "$EVID/preflight-run.txt" \
   && grep -q 'RESULT: PASS' "$EVID/preflight-run.txt"; then
	pass "build-evidence/preflight-run.txt records the 23/0 host-artifact run"
else
	fail "build-evidence/preflight-run.txt missing or not the 23/0 run"
fi

# --- 12. the preflight contains no device-contact command ----------------
if grep -nE 'ssh |telnet|scp |curl|wget|insmod|rmmod|reboot|devmem|/dev/mem|mtd write|ubiformat /dev' "$PRE" >/dev/null 2>&1; then
	fail "preflight script contains a device-contact command"
else
	pass "preflight script contains no ssh/telnet/curl/wget/mtd-write/ubiformat/insmod/rmmod command"
fi

# --- summary --------------------------------------------------------------
printf 'selftest: %s passed, %s failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
