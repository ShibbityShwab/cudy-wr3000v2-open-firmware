#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# validate-configs.sh - host-side validity gate for the bench hostapd config
# fixtures.  It runs an x86_64 hostapd built from the SAME source version the
# OpenWrt bench package uses (see build-host-hostapd.sh) over each fixture:
# a config-file parse error ("unknown configuration item" / "errors found in
# configuration file") is a hard FAIL; a clean parse followed by the expected
# host-driver initialization failure is the PASS signature (the host is not
# root and has no matching wlan0, so the driver cannot come up - that is after
# the config was fully parsed and checked).
#
# It also runs a NEGATIVE control (a deliberately broken config must be
# caught) so the gate itself cannot be vacuous.
#
# usage: ./tools/validate-configs.sh [--host-hostapd PATH] [--configs DIR] [--evidence FILE]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
CONFIGS="$LANE/configs"
HOSTAPD=
EVIDENCE=
VERIFIES=0
FAILS=0
ok()  { VERIFIES=$((VERIFIES+1)); printf 'ok   %s\n' "$*"; }
bad() { FAILS=$((FAILS+1)); printf 'FAIL %s\n' "$*"; }
while [ $# -gt 0 ]; do
	case "$1" in
		--host-hostapd) shift; HOSTAPD=$1 ;;
		--host-hostapd=*) HOSTAPD=${1#*=} ;;
		--configs) shift; CONFIGS=$1 ;;
		--evidence) shift; EVIDENCE=$1 ;;
		-h|--help) sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")"
	exec > >(tee "$EVIDENCE") 2>&1
fi
if [ -z "$HOSTAPD" ]; then
	HOSTAPD=$("$HERE/build-host-hostapd.sh") || { echo "RESULT: FAIL (no host hostapd)" ; exit 1; }
fi
[ -x "$HOSTAPD" ] || { echo "no host hostapd binary at $HOSTAPD" >&2; exit 2; }
ver=$("$HOSTAPD" -v 2>&1 | head -n1)
printf 'host hostapd: %s (%s)\n' "$HOSTAPD" "$ver"
case "$ver" in
	*"hostapd v2.12"*) ok "hostapd version matches the tree source (v2.12)" ;;
	*) bad "unexpected host hostapd version: $ver" ;;
esac
# The fixtures are run under a dummy ifname: config parsing happens before the
# driver touches the interface, so the host name does not matter.
run_one() { # file expect_good label
	f=$1; want=$2; label=$3
	out=$("$HOSTAPD" -dd -i wlan0 "$f" 2>&1 || true)
	if printf '%s' "$out" | grep -q "unknown configuration item\|errors found in configuration file\|invalid line"; then
		if [ "$want" = good ]; then
			bad "$label: config parse error"
			printf '%s\n' "$out" | grep -i "unknown configuration item\|errors found" | head -3
		else
			ok "$label: broken config refused by the parser (negative control)"
		fi
		return
	fi
	if printf '%s' "$out" | grep -q "Configuration file: $f"; then
		if [ "$want" = good ]; then ok "$label: config parsed cleanly (pre-driver)"; 
		else bad "$label: broken config unexpectedly parsed cleanly"; fi
	else
		bad "$label: hostapd did not reach the config-file load stage"
	fi
}
for f in "$CONFIGS/hostapd-open.conf" "$CONFIGS/hostapd-wpa2.conf"; do
	[ -f "$f" ] || { bad "missing fixture $f"; continue; }
	run_one "$f" good "$(basename "$f")"
done
BAD=$(mktemp /tmp/hostapd-badconf.XXXXXX)
printf 'bogus_option_xyz=1\n' >"$BAD"
run_one "$BAD" bad "negative-control"
rm -f "$BAD"
printf '\n== validate-configs: %d passed, %d failed ==\n' "$VERIFIES" "$FAILS"
if [ "$FAILS" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS - both fixtures parse with the tree-source hostapd v2.12; a broken config is refused\n'
