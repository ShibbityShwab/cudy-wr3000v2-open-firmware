#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
#
# validate-configs.sh - host-side validity gate for the WPA3-SAE / transition /
# WPA2-hardened hostapd fixtures.
#
# What it proves (and what it does not):
#   * every fixture is fully parsed AND passes hostapd_config_check() (which
#     runs at the end of hostapd_config_read(), BEFORE any driver call);
#   * the expected pre-driver stop signature is reached with a non-existent
#     dummy interface (hostapd cannot initialise a driver for it - that is the
#     PASS boundary on this host);
#   * a deliberately broken config AND a semantically invalid config are both
#     refused (negative controls, so the gate cannot be vacuous);
#   * static profile assertions on the fixtures (AKM/PMF/cipher shape).
#
# It does NOT prove a WPA3 client can associate, nor that the kernel driver
# accepts / reaches these nl80211 commands; that is the bench ladder.
#
# usage: ./tools/validate-configs.sh [--hostapd PATH] [--evidence FILE]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
CONFIGS="$LANE/configs"
HOSTAPD=
EVIDENCE=
DUMMY=hwsim-cudy0
VERIFIES=0
FAILS=0
ok()  { VERIFIES=$((VERIFIES+1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS+1)); printf 'FAIL %s\n' "$*"; }
while [ $# -gt 0 ]; do
	case "$1" in
		--hostapd) shift; HOSTAPD=$1 ;;
		--hostapd=*) HOSTAPD=${1#*=} ;;
		--evidence) shift; EVIDENCE=$1 ;;
		--evidence=*) EVIDENCE=${1#*=} ;;
		-h|--help) sed -n '3,21p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")"
	exec > >(tee "$EVIDENCE") 2>&1
fi
if [ -z "$HOSTAPD" ]; then
	HOSTAPD=$("$HERE/build-hostapd-validator.sh") || { echo "RESULT: FAIL (no host hostapd)"; exit 1; }
fi
[ -x "$HOSTAPD" ] || { echo "no host hostapd binary at $HOSTAPD" >&2; exit 2; }
ver=$("$HOSTAPD" -v 2>&1 | head -n1)
printf 'host hostapd: %s (%s)\n' "$HOSTAPD" "$ver"
case "$ver" in
	*"hostapd v2.12"*) ok "hostapd version matches the tree source (v2.12)";;
	*) bad "unexpected host hostapd version: $ver";;
esac

# run_one <config> <good|bad> <label>: parse/validate gate.  A "good" fixture
# must load, produce no parse/semantic error and reach the pre-driver stop; a
# "bad" control must load and be refused with the config-error signature.
run_one() {
	cfg=$1; want=$2; label=$3
	out=$(timeout 20 "$HOSTAPD" -dd -i "$DUMMY" "$cfg" 2>&1 || true)
	parsed=0
	printf '%s' "$out" | grep -qF "Configuration file: $cfg" && parsed=1
	errors=0
	printf '%s' "$out" | grep -qE 'unknown configuration item|Invalid line|errors found in configuration file|Failed to set configuration|Configuration check failed' && errors=1
	if [ "$want" = good ]; then
		[ "$parsed" -eq 1 ] && ok "$label: configuration file loaded" || bad "$label: configuration file not loaded"
		if [ "$errors" -eq 0 ]; then
			ok "$label: no parse/semantic errors"
		else
			bad "$label: parse/semantic error"
			printf '%s\n' "$out" | grep -E 'unknown configuration item|Invalid line|errors found|Cannot|Invalid|invalid' | head -5
		fi
		if printf '%s' "$out" | grep -qE 'Could not read interface|driver initialization failed'; then
			ok "$label: reached the pre-driver stop (config check passed)"
		else
			bad "$label: did not reach the expected pre-driver stop"
		fi
	else
		[ "$parsed" -eq 1 ] && ok "$label: control config was read (real parse)" || bad "$label: control config not read"
		[ "$errors" -eq 1 ] && ok "$label: invalid config refused" || bad "$label: invalid config was NOT refused"
	fi
}

for f in hostapd-wpa3-sae.conf hostapd-wpa2-wpa3.conf hostapd-wpa2-hardened.conf; do
	[ -f "$CONFIGS/$f" ] || { bad "missing fixture $CONFIGS/$f"; continue; }
	run_one "$CONFIGS/$f" good "$f"
done

BAD_PARSE=$(mktemp /tmp/hostapd-badparse.XXXXXX)
printf 'bogus_option_xyz=1\n' >"$BAD_PARSE"
run_one "$BAD_PARSE" bad "negative-control: parser"
rm -f "$BAD_PARSE"

BAD_SEM=$(mktemp /tmp/hostapd-badsem.XXXXXX)
cat >"$BAD_SEM" <<'EOF'
driver=nl80211
interface=wlan0
ssid=semantic-negative-control
hw_mode=g
channel=6
wpa=2
wpa_key_mgmt=WPA-PSK
rsn_pairwise=CCMP
EOF
run_one "$BAD_SEM" bad "negative-control: semantic (WPA-PSK without passphrase)"
rm -f "$BAD_SEM"

# ---- static profile assertions (comments and blanks excluded) ----
data_of() { grep -vE '^[[:space:]]*(#|$)' "$CONFIGS/$1"; }
has()   { data_of "$2" | grep -qE "$1"; }
hasnt() { ! data_of "$2" | grep -qE "$1"; }
W3=hostapd-wpa3-sae.conf
TR=hostapd-wpa2-wpa3.conf
H2=hostapd-wpa2-hardened.conf
has '^wpa_key_mgmt=SAE$' "$W3" && ok "$W3: SAE-only AKM" || bad "$W3: AKM not SAE-only"
has '^ieee80211w=2$' "$W3" && ok "$W3: PMF required" || bad "$W3: PMF not required"
has '^group_mgmt_cipher=BIP-CMAC-256$' "$W3" && ok "$W3: BIP-CMAC-256 group mgmt cipher" || bad "$W3: group mgmt cipher"
has '^rsn_pairwise=CCMP$' "$W3" && ok "$W3: CCMP pairwise" || bad "$W3: pairwise cipher"
hasnt 'TKIP|WEP' "$W3" && ok "$W3: no TKIP/WEP" || bad "$W3: legacy cipher present"
has '^wpa_key_mgmt=WPA-PSK SAE$' "$TR" && ok "$TR: PSK+SAE transition AKM" || bad "$TR: transition AKM shape"
has '^ieee80211w=1$' "$TR" && ok "$TR: PMF optional" || bad "$TR: PMF optionality"
has '^sae_require_mfp=1$' "$TR" && ok "$TR: SAE stations must negotiate PMF" || bad "$TR: sae_require_mfp"
has '^wpa_key_mgmt=WPA-PSK$' "$H2" && ok "$H2: WPA2-PSK AKM" || bad "$H2: AKM shape"
has '^ieee80211w=2$' "$H2" && ok "$H2: PMF required (hardened WPA2)" || bad "$H2: PMF"
hasnt 'SAE|TKIP|WEP' "$H2" && ok "$H2: no SAE/TKIP/WEP" || bad "$H2: unexpected legacy/AKM entry"
for f in "$W3" "$TR" "$H2"; do
	if has '^driver=nl80211$' "$f" && has '^interface=wlan0$' "$f" && \
	   has '^ssid=cudy-wr3000v2-' "$f" && has '^hw_mode=g$' "$f" && \
	   has '^channel=6$' "$f" && has '^ht_capab=\[HT20\]\[SHORT-GI-20\]$' "$f" && \
	   has '^wpa_passphrase=CHANGEME-lab-only$' "$f"; then
		ok "$f: target shape (driver/ifname/ssid/2.4GHz HT20/placeholder passphrase)"
	else
		bad "$f: target shape assertion failed"
	fi
	if data_of "$f" | grep -qiE 'psk=|wpa_psk'; then
		bad "$f: literal PSK/material line present"
	else
		ok "$f: no literal PSK material"
	fi
done

printf '\n== validate-configs: %d passed, %d failed ==\n' "$VERIFIES" "$FAILS"
if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS - 3 fixtures parse+validate with hostapd v2.12 (CONFIG_SAE+CONFIG_IEEE80211W), profile shape asserted, 2 negative controls refused\n'
