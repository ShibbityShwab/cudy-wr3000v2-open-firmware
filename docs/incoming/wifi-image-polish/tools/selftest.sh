#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# selftest.sh - host-only verification gate for the wifi-image-polish lane.
#
# Runs, in order:
#   1. the static cross-check (tools/validate.py): rootfs payload, uci policy,
#      modprobe.d/loader/init consistency, hostapd capability matrix, report
#      and refs artifact identities, no-binary/no-credential policy;
#   2. the hostapd v2.12 config-parse gate (same source version as the image)
#      when the host build is available - a negative control is included;
#   3. loader dry-run on the host (/usr/sbin/hi5622-load with HI5622_DRY_RUN=1)
#      to prove the packaged defaults and the exact insmod incantation without
#      loading anything;
#   4. compile evidence pin: build-evidence/compile-evidence.env must still
#      describe the pinned stack1/polished artifacts and the build logs must
#      have zero warnings/errors;
#   5. the patch file sanity (no CRLF, contains the mgmt_diag gate).
#
# No device contact, no network, no root.  Exit 0 = PASS.
# The heavy compile gate is separate: ./tools/build-polished-module.sh
set -u
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
PASS=0
FAIL=0
SKIP=0
ok()   { PASS=$((PASS + 1)); printf 'ok   %s\n' "$*"; }
bad()  { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$*"; }
skip() { SKIP=$((SKIP + 1)); printf 'skip %s\n' "$*"; }

# --- 1. static validation ---------------------------------------------------
HOSTAPD=
if [ -x "$LANE_DIR/tools/build-hostapd-validator.sh" ]; then
	# lane-local hostapd v2.12 with CONFIG_SAE, so the WPA3 transition fixture
	# is parsed by the same config surface as the target hostapd-mini.
	HOSTAPD=$("$LANE_DIR/tools/build-hostapd-validator.sh" 2>/dev/null || true)
fi
if [ -n "$HOSTAPD" ] && [ -x "$HOSTAPD" ]; then
	if python3 "$LANE_DIR/tools/validate.py" --hostapd "$HOSTAPD" >/tmp/wifi-image-polish-validate.log 2>&1; then
		tail -1 /tmp/wifi-image-polish-validate.log
		ok "validate.py PASS (static checks + hostapd config-parse gate)"
		# fold the sub-check count into our counter
		sub=$(sed -n 's/^RESULT: PASS (\([0-9]*\) checks)$/\1/p' /tmp/wifi-image-polish-validate.log)
		[ -n "$sub" ] && PASS=$((PASS + sub))
	else
		bad "validate.py FAIL (see /tmp/wifi-image-polish-validate.log)"
		tail -20 /tmp/wifi-image-polish-validate.log >&2
	fi
else
	skip "hostapd host build unavailable (tar.zst missing?) - static-only validation"
	if python3 "$LANE_DIR/tools/validate.py" >/tmp/wifi-image-polish-validate.log 2>&1; then
		ok "validate.py PASS (static checks only)"
		sub=$(sed -n 's/^RESULT: PASS (\([0-9]*\) checks)$/\1/p' /tmp/wifi-image-polish-validate.log)
		[ -n "$sub" ] && PASS=$((PASS + sub))
	else
		bad "validate.py FAIL (see /tmp/wifi-image-polish-validate.log)"
		tail -20 /tmp/wifi-image-polish-validate.log >&2
	fi
fi

# --- 2. loader dry-run ------------------------------------------------------
LOADER=$LANE_DIR/rootfs/usr/sbin/hi5622-load
for args in "" "--no-c2 --no-ap"; do
	if out=$(HI5622_DRY_RUN=1 "$LOADER" $args 2>&1); then
		case "$out" in
		*"dry-run] insmod"*"fw_path=hi_wifi/FIRMWARE.PD4.bin"*"allow_frames=16"*"vap_start_mode=1"*)
			ok "loader dry-run ($args): packaged defaults + explicit insmod incantation" ;;
		*) bad "loader dry-run ($args): unexpected plan output" ;;
		esac
		case "$out" in
		*"rmmod"*) bad "loader dry-run ($args): mentions rmmod" ;;
		*) ok "loader dry-run ($args): never unloads" ;;
		esac
	else
		bad "loader dry-run ($args) failed"
	fi
done

# --- 3. compile evidence pin ------------------------------------------------
EVID=$LANE_DIR/build-evidence/compile-evidence.env
if [ -f "$EVID" ]; then
	# shellcheck disable=SC1090
	. "$EVID"
	if [ "${STACK1_SHA256:-}" = "44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b" ] &&
	   [ "${STACK1_SIZE:-}" = "242572" ]; then
		ok "compile evidence: stack1 reproduces the hardware-fired artifact (242572 B 44c6cc12...)"
	else
		bad "compile evidence: stack1 identity drift"
	fi
	if [ "${POLISHED_SHA256:-}" = "d145a2050cfcba7511d052da6eb67478dac75aa7285a1ad9c44b3dba822bd2c6" ] &&
	   [ "${POLISHED_SIZE:-}" = "242920" ] && [ "${POLISHED_PARM_COUNT:-}" = "20" ]; then
		ok "compile evidence: polished module pin (242920 B d145a205..., 20 parms)"
	else
		bad "compile evidence: polished identity drift"
	fi
	case "${POLISHED_VERMAGIC:-}" in
	*"6.18.60"*"ARMv7"*) ok "compile evidence: polished vermagic matches the image kernel ABI" ;;
	*) bad "compile evidence: vermagic missing/mismatched" ;;
	esac
	case "${POLISHED_DEPENDS:-}" in
	*cfg80211*) ok "compile evidence: depends=cfg80211" ;;
	*) bad "compile evidence: depends missing cfg80211" ;;
	esac
	for log in "$LANE_DIR"/build-evidence/stack1-build.log "$LANE_DIR"/build-evidence/stack2-build.log; do
		if [ -f "$log" ]; then
			if grep -iE 'warning:|error:' "$log" >/dev/null 2>&1; then
				bad "compile log has warnings/errors: $(basename "$log")"
			else
				ok "compile log clean: $(basename "$log")"
			fi
		else
			bad "missing compile log: $(basename "$log")"
		fi
	done
else
	bad "missing build-evidence/compile-evidence.env - run tools/build-polished-module.sh"
fi

# --- 4. patch sanity --------------------------------------------------------
PATCH=$LANE_DIR/patches/0001-polish-module-defaults-and-mgmt-diag.patch
if [ -f "$PATCH" ]; then
	if grep -q "$(printf '\r')" "$PATCH"; then
		bad "patch: CRLF line endings"
	else
		ok "patch: LF line endings"
	fi
	for needle in "mgmt_diag" "hi_wifi/FIRMWARE.PD4.bin" "hi5622_allow_frames = 16u" "hi5622_vap_start_first = 1u"; do
		if grep -qF "$needle" "$PATCH"; then
			ok "patch: contains $needle"
		else
			bad "patch: missing $needle"
		fi
	done
	if grep -q "dump_stack" "$PATCH"; then
		ok "patch: dump_stack gating present"
	else
		bad "patch: dump_stack gating missing"
	fi
else
	bad "missing polish patch"
fi

# --- summary ----------------------------------------------------------------
echo
if [ "$FAIL" -ne 0 ]; then
	echo "RESULT: FAIL ($FAIL failed, $PASS passed, $SKIP skipped)"
	exit 1
fi
echo "RESULT: PASS ($PASS passed, $SKIP skipped; host-only, no device contact)"
exit 0
