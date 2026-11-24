#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# run-host-check.sh - host-only verification for the wifi-wpa3-sae lane.
#
#   1. compiles and runs tools/wpa3_selftest.c against the exact portable
#      hi5622_key_frames.c the kernel module compiles (44 checks);
#   2. runs static assertions on the patched kernel sources in src/ (AKM
#      advertisement, PMF default-mgmt key path, fail-closed policy kept);
#   3. runs the hostapd config gate (tools/validate-configs.sh, builds the
#      tree-source hostapd v2.12 with CONFIG_SAE if needed).
#
# No device, no network (the hostapd source tarball is the OpenWrt tree's
# prefetched dl/ download).  usage: ./tools/run-host-check.sh [--evidence FILE]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
SRC=$LANE/src
EVIDENCE=
while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) shift; EVIDENCE=$1 ;;
		--evidence=*) EVIDENCE=${1#*=} ;;
		-h|--help) sed -n '3,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")"
	exec > >(tee "$EVIDENCE") 2>&1
fi
VERIFIES=0
FAILS=0
ok()  { VERIFIES=$((VERIFIES+1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS+1)); printf 'FAIL %s\n' "$*"; }

TMP=$(mktemp -d /tmp/wpa3-hostcheck.XXXXXX)
trap 'rm -rf "$TMP"' EXIT

echo "=== 1. portable key-frame selftest (44 checks) ==="
if cc -O2 -std=c99 -Wall -Wextra -Werror -I "$SRC" \
	"$LANE/tools/wpa3_selftest.c" "$SRC/hi5622_key_frames.c" \
	-o "$TMP/wpa3_selftest" 2>"$TMP/cc.log"; then
	ok "selftest compiles warning-free (-Wall -Wextra -Werror)"
	if "$TMP/wpa3_selftest" >"$TMP/selftest.log" 2>&1; then
		ok "selftest PASS ($(grep -o '== wpa3_selftest: [0-9]* checks, [0-9]* failed ==' "$TMP/selftest.log"))"
	else
		bad "selftest FAILED"
	fi
	cat "$TMP/selftest.log"
else
	bad "selftest does not compile"
	cat "$TMP/cc.log"
fi

echo
echo "=== 2. static assertions on the patched kernel sources ==="
CHK=$SRC/hi5622_cfg80211.c
grep -q 'WLAN_AKM_SUITE_SAE' "$CHK" && ok "wiphy AKM table advertises SAE" || bad "no SAE AKM"
grep -q 'WLAN_AKM_SUITE_PSK' "$CHK" && ok "wiphy AKM table advertises PSK" || bad "no PSK AKM"
grep -q 'wiphy->akm_suites = hi5622_akm_suites;' "$CHK" && ok "wiphy->akm_suites registered" || bad "akm_suites not registered"
grep -q 'wiphy->n_akm_suites = ARRAY_SIZE(hi5622_akm_suites);' "$CHK" && ok "wiphy->n_akm_suites registered" || bad "n_akm_suites missing"
grep -q 'wiphy->iftype_akm_suites = hi5622_iftype_akm_suites;' "$CHK" && ok "AP iftype AKM override registered" || bad "iftype AKM override missing"
grep -q 'WLAN_CIPHER_SUITE_BIP_GMAC_256' "$CHK" && ok "BIP-GMAC-256 cipher advertised (PMF)" || bad "BIP-GMAC missing"
grep -q 'WLAN_CIPHER_SUITE_BIP_CMAC_256' "$CHK" && ok "BIP-CMAC-256 cipher advertised (WPA3 default)" || bad "BIP-CMAC missing"
grep -q '\.tx = BIT(0x01) | BIT(0x03) | BIT(0x05)' "$CHK" && ok "AP mgmt TX mask covers (re)assoc/probe responses" || bad "AP mgmt TX mask wrong"
OPS=$SRC/hi5622_cfg80211_ops.c
grep -qE '^[[:space:]]*\.set_default_mgmt_key[[:space:]]*= hi5622_keys_op_set_default_mgmt' "$OPS" && ok "cfg80211 .set_default_mgmt_key registered" || bad ".set_default_mgmt_key not registered"
grep -qE '^[[:space:]]*\.add_key[[:space:]]*= hi5622_keys_op_add' "$OPS" && ok "cfg80211 .add_key still registered" || bad "add_key missing"
KEYS=$SRC/hi5622_keys.c
grep -q 'hi5622_mgmt_key_cipher' "$KEYS" && ok "keys layer latches the management cipher" || bad "no mgmt cipher latch"
grep -q '"key_default_mgmt"' "$KEYS" && ok "PMF default-mgmt frame post label present" || bad "no default-mgmt post label"
grep -q 'firmware worker rejects cipher byte 0' "$KEYS" && ok "unknown cipher fails closed (never posts a guessed byte)" || bad "cipher-0 fail-closed missing"
grep -q 'hi5622_mgmt_key_cipher\[key_index - HKF_MIN_MGMT_KEY_INDEX\]' "$KEYS" \
	&& ok "default-mgmt op reads the latch by key index" || bad "latch read missing"
KF=$SRC/hi5622_key_frames.c
grep -q 'body\[0x04\] = (u8)(p->cipher & 0xffu);' "$KF" && ok "mgmt body carries the cipher at +0x04" || bad "mgmt cipher byte missing"
grep -q 'HKF_CIPHER_MGMT' "$KF" && ok "mgmt cipher class enforced in the portable validator" || bad "mgmt class check missing"
LOGIC=$SRC/hi5622_cfg80211_logic.c
grep -q '"set_default_mgmt_key", HI5622_M3, HI5622_OP_IMPL' "$LOGIC" && ok "op map marks set_default_mgmt_key implemented" || bad "op map not updated"
API=$SRC/hi5622_frames_api.h
grep -qE 'u8[[:space:]]+mgmt;' "$API" && ok "neutral default-key params carry the mgmt variant" || bad "frames_api mgmt field missing"
PROV=$SRC/hi5622_vapscan_provider.c
grep -q 'dp.mgmt = p->mgmt' "$PROV" && ok "provider forwards the mgmt variant" || bad "provider forward missing"
# fail-closed policy must be preserved
if grep -qE 'hi5622_wpa_keys[[:space:]]*=[[:space:]]*1' "$SRC" 2>/dev/null; then
	bad "wpa_keys default was changed away from fail-closed"
else
	ok "wpa_keys still has no non-zero initializer (fail-closed default 0)"
fi

echo
echo "=== 3. hostapd config gate ==="
if "$HERE/validate-configs.sh" >"$TMP/validate.log" 2>&1; then
	ok "hostapd config gate PASS"
	tail -3 "$TMP/validate.log" | head -2
else
	bad "hostapd config gate FAILED"
	tail -40 "$TMP/validate.log"
fi

printf '\n== run-host-check: %d passed, %d failed ==\n' "$VERIFIES" "$FAILS"
if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS - portable selftest, patched-source static assertions, hostapd v2.12 config gate\n'
