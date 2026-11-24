#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - host-only checks for the wifi-final-image lane (no device, no
# image build): payload completeness/policy, loader/uci consistency, no
# binary/secret payload, hostapd v2.12 config-parse gate (when a validator
# binary is available), module identity (when the artifact is present) and
# docs/evidence consistency.
#
#   ./tools/selftest.sh [--hostapd BIN]
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
R=$LANE/payload
HOSTAPD=${HOSTAPD_VALIDATOR:-/tmp/wifi-image-polish-hostapd/hostapd/hostapd}
DRIVER_KO=${DRIVER_KO:-/tmp/wifi-allpatch-artifact/hi5622_wifi.ko}

PASS=0; FAIL=0; SKIP=0
ok()   { printf 'PASS  %s\n' "$*"; PASS=$((PASS + 1)); }
bad()  { printf 'FAIL  %s\n' "$*"; FAIL=$((FAIL + 1)); }
skip() { printf 'SKIP  %s\n' "$*"; SKIP=$((SKIP + 1)); }

while [ $# -gt 0 ]; do
	case "$1" in
		--hostapd) HOSTAPD=${2:-}; shift 2 ;;
		--hostapd=*) HOSTAPD=${1#*=}; shift ;;
		-h|--help) sed -n '3,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "selftest: unknown argument: $1" >&2; exit 2 ;;
	esac
done

echo "== wifi-final-image selftest (host-only) =="
echo "lane: $LANE"

# ---------------------------------------------------------------------------
# 1. payload inventory
# ---------------------------------------------------------------------------
echo
echo "-- payload inventory --"
EXPECTED="etc/config/hi5622
etc/init.d/hi5622-wifi
etc/modprobe.d/hi5622-wifi.conf
etc/hi5622/FIRMWARE-POLICY.txt
etc/hi5622/hostapd-open.conf
etc/hi5622/hostapd-open.proven.conf
etc/hi5622/hostapd-wpa2.conf
etc/hi5622/hostapd-wpa2-wpa3.conf
usr/sbin/hi5622-load"
while IFS= read -r rel; do
	[ -f "$R/$rel" ] && ok "payload present: $rel" || bad "payload missing: $rel"
done <<EOF
$EXPECTED
EOF
GOT=$(find "$R" -type f | sed "s|^$R/||" | sort)
EXPECTED_SORTED=$(printf '%s\n' "$EXPECTED" | sort)
if [ "$GOT" = "$EXPECTED_SORTED" ]; then
	ok "payload inventory exactly $(printf '%s\n' "$EXPECTED" | wc -l) files (no strays)"
else
	bad "payload inventory differs from the expected file set"
fi

# ---------------------------------------------------------------------------
# 2. executable bits + shell syntax
# ---------------------------------------------------------------------------
echo
echo "-- executable scripts --"
for rel in usr/sbin/hi5622-load etc/init.d/hi5622-wifi; do
	if [ -x "$R/$rel" ]; then ok "exec bit: $rel"; else bad "exec bit missing: $rel"; fi
	if sh -n "$R/$rel" 2>/dev/null; then ok "sh -n clean: $rel"; else bad "sh -n failed: $rel"; fi
done
if [ "$(stat -c %a "$R/usr/sbin/hi5622-load")" = "755" ] && [ "$(stat -c %a "$R/etc/init.d/hi5622-wifi")" = "755" ]; then
	ok "payload script modes are 755"
else
	bad "payload script modes are not 755"
fi

# ---------------------------------------------------------------------------
# 3. policy guards: text-only payload, no private keys, no real credentials
# ---------------------------------------------------------------------------
echo
echo "-- policy guards --"
BIN_FOUND=0
while IFS= read -r f; do
	if head -c 4 "$f" 2>/dev/null | grep -q $'\x7fELF'; then bad "ELF binary inside the lane: $f"; BIN_FOUND=1; fi
	case "$f" in *.ko|*.o|*.bin|*.fw) bad "binary-looking payload file: $f"; BIN_FOUND=1 ;; esac
done < <(find "$LANE" -type f ! -path "$LANE/build-evidence/work/*")
[ "$BIN_FOUND" -eq 0 ] && ok "no ELF/.ko/.bin/.fw file in the lane (build artefacts stay local)"
if grep -rn -e 'BEGIN RSA PRIVATE KEY' -e 'BEGIN OPENSSH PRIVATE KEY' -e 'BEGIN EC PRIVATE KEY' -e 'BEGIN PRIVATE KEY' "$LANE" \
	--include='*.md' --include='*.txt' --include='*.conf' --include='*.sh' \
	--exclude='selftest.sh' 2>/dev/null | grep -q .; then
	bad "private key material in the lane"
else
	ok "no private key material in the lane"
fi
BADCRED=$(grep -rhn 'wpa_passphrase=' "$R" | grep -v 'CHANGEME-lab-only' | grep -v 'test-passphrase-121' || true)
if [ -z "$BADCRED" ]; then ok "only documented throwaway/test passphrases in fixtures"; else bad "unexpected wpa_passphrase line: $BADCRED"; fi
if grep -q 'wpa_passphrase=test-passphrase-121' "$R/etc/hi5622/hostapd-wpa2.conf"; then
	ok "WPA2 fixture uses the project test credential (cudy-ap-test / test-passphrase-121)"
else
	bad "WPA2 fixture test credential missing"
fi

# ---------------------------------------------------------------------------
# 4. loader dry-run: full explicit insmod parameter set
# ---------------------------------------------------------------------------
echo
echo "-- loader dry-run --"
DRYRUN=$(HI5622_DRY_RUN=1 sh "$R/usr/sbin/hi5622-load" 2>&1 || true)
PARAM_MISSING=0
for kv in "fw_path=hi_wifi/FIRMWARE.PD4.bin" "wifi_block_path=hi_wifi/wifi_block.bin" \
          "allow_frames=32" "frame_gap_ms=100" "h2d_send_budget=10" \
          "vap_start_first=1" "vap_start_mode=1" "vap_start_module=0" \
          "scan_dwell_ms=60" "hcc_poll_ms=5" "hcc_trace=0" "stop_ap_mode=0" \
          "mgmt_diag=0" "rx_enable=1" "beacon_static=1" "beacon_vap_start=1" \
          "wpa_keys=1" "wpa_key_module=0" "sta_join=0" "ht_mode=0" "vht_cap=0" \
          "band_vendor=0" "scan_band_cfg=0" "txq_publish=1" "cfg80211_auto=1" \
          "ap_start_mode=0" "rd_ab_write=0" "hcc_cece_mode=0" "allow_unseeded_release=0"; do
	case "$DRYRUN" in
		*"$kv"*) ;;
		*) bad "loader dry-run missing parameter: $kv"; PARAM_MISSING=$((PARAM_MISSING + 1)) ;;
	esac
done
[ "$PARAM_MISSING" -eq 0 ] && ok "loader dry-run passes the full final-module parameter set (29 explicit args)"
case "$DRYRUN" in
	*rmmod*) bad "loader rmmods the driver (forbidden)" ;;
	*) ok "loader never unloads the module (no rmmod)" ;;
esac
case "$DRYRUN" in
	*"h2d_ctx_create"*"h2d_txq_sync72"*) ok "loader runs the C2 ladder (ctx_create -> txq_sync72)" ;;
	*) bad "loader C2 ladder missing" ;;
esac

# ---------------------------------------------------------------------------
# 5. /etc/config/hi5622 <-> loader defaults <-> modprobe mirror consistency
# ---------------------------------------------------------------------------
echo
echo "-- uci <-> loader <-> modprobe consistency --"
python3 - "$R" <<'PY'
import re, sys
root = sys.argv[1]
uci = {"main": {}, "ap": {}}
sec = None
for line in open(root + "/etc/config/hi5622"):
    s = line.strip()
    m = re.match(r"config (\w+) '(\w+)'", s)
    if m:
        sec = m.group(2); continue
    m = re.match(r"option (\w+) '(.*)'", s)
    if m and sec:
        uci[sec][m.group(1)] = m.group(2)
loader = open(root + "/usr/sbin/hi5622-load").read()
ld = {}
for m in re.finditer(r"uci_get (main|ap)\.([a-z0-9_]+) '([^']*)'", loader):
    ld[(m.group(1), m.group(2))] = m.group(3)
fails = 0
def check(cond, msg):
    global fails
    print(("PASS  " if cond else "FAIL  ") + msg)
    if not cond: fails += 1
for secname in ("main", "ap"):
    for k, v in uci[secname].items():
        if (secname, k) in ld:
            check(ld[(secname, k)] == v,
                  "uci %s.%s=%s matches loader default '%s'" % (secname, k, v, ld[(secname, k)]))
check(all(uci["main"].get(k) == v for k, v in {
    "autoload": "0", "allow_frames": "32", "h2d_send_budget": "10",
    "rx_enable": "1", "beacon_static": "1", "beacon_vap_start": "1",
    "wpa_keys": "1", "wpa_key_module": "0", "sta_join": "0",
    "ht_mode": "0", "vht_cap": "0", "band_vendor": "0",
    "scan_band_cfg": "0", "mgmt_diag": "0", "txq_publish": "1",
}.items()), "uci: final-module feature policy = validated single-boot recipe")
modprobe = ""
for line in open(root + "/etc/modprobe.d/hi5622-wifi.conf"):
    if line.startswith("options hi5622_wifi "):
        modprobe = line.strip()
opts = dict(p.split("=", 1) for p in modprobe.split()[2:] if "=" in p)
missing = [k for k in ("allow_frames", "h2d_send_budget", "rx_enable",
                       "beacon_static", "wpa_keys", "sta_join") if k not in opts]
check(not missing, "modprobe mirror carries the feature-gate parameters")
check(opts.get("allow_frames") == uci["main"].get("allow_frames") == "32",
      "modprobe mirror allow_frames mirrors uci loader policy")
sys.exit(1 if fails else 0)
PY
if [ "$?" -eq 0 ]; then ok "uci/loader/modprobe consistency checks passed"; else bad "uci/loader/modprobe consistency failed"; fi

# ---------------------------------------------------------------------------
# 6. hostapd v2.12 config-parse gate (optional host build; negative control)
# ---------------------------------------------------------------------------
echo
echo "-- hostapd config-parse gate --"
if [ -x "$HOSTAPD" ]; then
	for c in hostapd-open.proven.conf hostapd-open.conf hostapd-wpa2.conf hostapd-wpa2-wpa3.conf; do
		CONF=$R/etc/hi5622/$c
		BLOB=$(timeout 20 "$HOSTAPD" -dd -i wlan0 "$CONF" 2>&1 || true)
		REACHED=0; PARSEERR=0
		case "$BLOB" in
			*"Configuration file: $CONF"*) REACHED=1 ;;
		esac
		case "$BLOB" in
			*"unknown configuration item"*|*"errors found in configuration file"*|*"invalid line"*) PARSEERR=1 ;;
		esac
		if [ "$REACHED" -eq 1 ] && [ "$PARSEERR" -eq 0 ]; then
			ok "hostapd parsed $c cleanly before driver init"
		else
			bad "hostapd rejected $c (reached=$REACHED parse_error=$PARSEERR)"
		fi
	done
	BADCONF=$(mktemp /tmp/hp-bad.XXXXXX.conf)
	printf 'driver=nl80211\ninterface=wlan0\nbogus_option_xyz=1\n' >"$BADCONF"
	BBLOB=$(timeout 20 "$HOSTAPD" -dd -i wlan0 "$BADCONF" 2>&1 || true)
	rm -f "$BADCONF"
	case "$BBLOB" in
		*"unknown configuration item"*|*"errors found in configuration file"*) ok "hostapd negative control refused (bogus option)" ;;
		*) bad "hostapd negative control was NOT refused" ;;
	esac
else
	skip "hostapd validator binary not found ($HOSTAPD); parse gate not run"
fi

# ---------------------------------------------------------------------------
# 7. driver artifact identity (present on the build host)
# ---------------------------------------------------------------------------
echo
echo "-- driver artifact identity --"
if [ -f "$DRIVER_KO" ]; then
	SIZE=$(stat -c %s "$DRIVER_KO"); SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
	[ "$SIZE" = "308080" ] && ok "artifact size 308,080 B" || bad "artifact size $SIZE != 308080"
	[ "$SHA" = "5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41" ] \
		&& ok "artifact sha256 5b587949... (pinned all-patch build)" || bad "artifact sha256 mismatch: $SHA"
	[ "$(modinfo -F parm "$DRIVER_KO" 2>/dev/null | wc -l)" = "47" ] && ok "artifact has 47 parameters" || bad "artifact parameter count != 47"
	case "$(modinfo -F vermagic "$DRIVER_KO" 2>/dev/null || true)" in
		*6.18.60*) ok "artifact vermagic is 6.18.60" ;;
		*) bad "artifact vermagic is not 6.18.60" ;;
	esac
	case "$(modinfo -F depends "$DRIVER_KO" 2>/dev/null || true)" in
		*cfg80211*) ok "artifact depends=cfg80211" ;;
		*) bad "artifact does not depend on cfg80211" ;;
	esac
else
	skip "driver artifact not present at $DRIVER_KO (not a build host for this lane)"
fi

# ---------------------------------------------------------------------------
# 8. committed evidence + hashes consistency
# ---------------------------------------------------------------------------
echo
echo "-- docs / evidence --"
for f in README.md REPORT.md BUILD-NOTES.md HASHES.txt VERIFY.txt FLASH-FEASIBILITY.md; do
	[ -f "$LANE/$f" ] && ok "doc present: $f" || bad "doc missing: $f"
done
if [ -f "$LANE/HASHES.txt" ] && [ -f "$LANE/build-evidence/hashes.txt" ]; then
	for kv in container_sha256 rootfs_ubi_sha256 kernel_sha256 module_installed_sha256; do
		V=$(grep -m1 -E "^$kv +=" "$LANE/build-evidence/hashes.txt" | sed 's/^[^=]*= *//')
		case "$V" in
			"") skip "build-evidence has no $kv" ;;
			*) if grep -q "$V" "$LANE/HASHES.txt"; then ok "HASHES.txt records $kv=$V"; else bad "HASHES.txt does not record $kv=$V"; fi ;;
		esac
	done
else
	skip "HASHES.txt / build-evidence/hashes.txt not both present yet"
fi
if grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' "$LANE/REPORT.md" 2>/dev/null; then
	ok "REPORT records the unchanged kernel slice hash"
else
	bad "REPORT does not record the unchanged kernel slice hash"
fi
if grep -q '5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41' "$LANE/REPORT.md" 2>/dev/null; then
	ok "REPORT records the final all-patch module hash"
else
	bad "REPORT does not record the final all-patch module hash"
fi

echo
if [ "$FAIL" -eq 0 ]; then
	echo "RESULT: PASS ($PASS checks, $SKIP skipped)"
	exit 0
fi
echo "RESULT: FAIL ($FAIL failed / $PASS passed / $SKIP skipped)"
exit 1
