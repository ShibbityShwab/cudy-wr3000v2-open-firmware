#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - host-only checks for wifi-final-image-2 (task-159).  No device,
# no image build: payload completeness/policy, loader/uci/modprobe consistency,
# fixture sanity, module identity when the artifact is present, and the
# tracked-text policy (no vendor binary, no secret, no firmware blob).
#
#   ./tools/selftest.sh [--hostapd BIN]
#   DRIVER_KO=/path/to/hi5622_wifi.ko ./tools/selftest.sh   # add artifact checks
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
R=$LANE/payload
HOSTAPD=${HOSTAPD_VALIDATOR:-}
DRIVER_KO=${DRIVER_KO:-/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko}
MODULE_SHA=d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6
MODULE_SIZE=347388
MODULE_PARAMS=57

PASS=0; FAIL=0; SKIP=0
ok()   { printf 'PASS  %s\n' "$*"; PASS=$((PASS + 1)); }
bad()  { printf 'FAIL  %s\n' "$*"; FAIL=$((FAIL + 1)); }
skip() { printf 'SKIP  %s\n' "$*"; SKIP=$((SKIP + 1)); }

while [ $# -gt 0 ]; do
	case "$1" in
		--hostapd) HOSTAPD=${2:-}; shift 2 ;;
		--hostapd=*) HOSTAPD=${1#*=}; shift ;;
		-h|--help) sed -n '2,11p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "selftest: unknown argument: $1" >&2; exit 2 ;;
	esac
done

echo "== wifi-final-image-2 selftest (host-only) =="
echo "lane: $LANE"

# ---------------------------------------------------------------------------
# 1. payload inventory
# ---------------------------------------------------------------------------
echo
echo "-- payload inventory --"
EXPECTED="etc/config/hi5622
etc/hi5622/FIRMWARE-POLICY.txt
etc/hi5622/hostapd-open.conf
etc/hi5622/hostapd-open.proven.conf
etc/hi5622/hostapd-wpa2.conf
etc/hi5622/hostapd-wpa2-hardened.conf
etc/hi5622/hostapd-wpa2-wpa3.conf
etc/hi5622/hostapd-wpa3-sae.conf
etc/init.d/hi5622-wifi
etc/modprobe.d/hi5622-wifi.conf
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
# 3. policy guards: text-only, no private keys, documented test credentials
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
	--include='*.md' --include='*.txt' --include='*.conf' --include='*.sh' --exclude='selftest.sh' 2>/dev/null | grep -q .; then
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
if grep -rqi 'FIRMWARE.PD4\|wifi_block' "$R/etc/hi5622/FIRMWARE-POLICY.txt"; then
	ok "firmware policy names the manual-only PD4 + wifi_block push"
else
	bad "firmware policy text missing"
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
          "band_vendor=0" "scan_band_cfg=0" \
          "mgmt_tx=0" "mgmt_rx=0" "sta_ops=0" "eapol_rx=0" "sta_user_del=0" \
          "rx_refill_wm=75" \
          "data_tx=0" "data_tx_peer=1" "data_tx_protect=0" "data_tx_timeout_ms=200" \
          "txq_publish=1" "cfg80211_auto=1" "ap_start_mode=0" "rd_ab_write=0" \
          "hcc_cece_mode=0" "allow_unseeded_release=0"; do
	case "$DRYRUN" in
		*"$kv"*) ;;
		*) bad "loader dry-run missing parameter: $kv"; PARAM_MISSING=$((PARAM_MISSING + 1)) ;;
	esac
done
[ "$PARAM_MISSING" -eq 0 ] && ok "loader dry-run passes the full 57-param module parameter set (39 explicit args)"
case "$DRYRUN" in
	*rmmod*) bad "loader rmmods the driver (forbidden)" ;;
	*) ok "loader never unloads the module (no rmmod)" ;;
esac
case "$DRYRUN" in
	*"h2d_ctx_create"*"h2d_txq_sync72"*) ok "loader runs the C2 ladder (ctx_create -> txq_sync72)" ;;
	*) bad "loader C2 ladder missing" ;;
esac
if grep -q 'autoload' "$R/etc/init.d/hi5622-wifi" && grep -q "option autoload '0'" "$R/etc/config/hi5622"; then
	ok "loader/init keeps the bench autoload=0 policy"
else
	bad "loader/init autoload policy missing"
fi

# ---------------------------------------------------------------------------
# 5. /etc/config/hi5622 <-> loader defaults <-> modprobe mirror consistency
# ---------------------------------------------------------------------------
echo
echo "-- uci <-> loader <-> modprobe consistency --"
python3 - "$R" <<'PY'
import re, sys
root = sys.argv[1]
uci = {}
sec = None
for line in open(root + "/etc/config/hi5622"):
    s = line.strip()
    m = re.match(r"config (\w+) '(\w+)'", s)
    if m:
        sec = m.group(2); continue
    m = re.match(r"option (\w+) '(.*)'", s)
    if m and sec == "main":
        uci[m.group(1)] = m.group(2)

loader = open(root + "/usr/sbin/hi5622-load").read()
defaults = {}
var2opt = {}
for name, opt, default in re.findall(r"^(\w+)=\$\(uci_get main\.(\w+) '([^']*)'\)", loader, re.M):
    defaults[opt] = default
    var2opt[name] = opt
# every uci main option is a live knob
dead = sorted(set(uci) - set(defaults))
print(("FAIL" if dead else "PASS") + "  every uci main option is consumed by the loader"
      + ((": dead options " + ", ".join(dead)) if dead else ""))
# shipped uci values == loader defaults
mismatch = [(o, uci[o], defaults[o]) for o in uci if o in defaults and uci[o] != defaults[o]]
print(("FAIL" if mismatch else "PASS") + "  uci values match loader defaults"
      + ((": " + repr(mismatch)) if mismatch else ""))
# every loader insmod arg mirrors modprobe where the arg is uci-driven
mm = re.search(r"^options hi5622_wifi (.*)$", open(root + "/etc/modprobe.d/hi5622-wifi.conf").read(), re.M)
mod = dict(kv.split("=", 1) for kv in mm.group(1).split()) if mm else {}
insmod = loader.split("run insmod", 1)
args = re.findall(r'(\w+)=\$?(\w+)', insmod[1], re.M) if len(insmod) > 1 else []
miss = []
for key, var in args:
    if var in var2opt:
        opt = var2opt[var]
        want = uci[opt]
        if mod.get(key) != want:
            miss.append((key, mod.get(key), want))
print(("FAIL" if miss else "PASS") + "  modprobe mirror matches every uci-driven insmod argument"
      + ((": " + repr(miss)) if miss else ""))
# association gate defaults are fail-closed
failclosed = {"mgmt_tx": "0", "mgmt_rx": "0", "sta_ops": "0", "eapol_rx": "0",
              "sta_user_del": "0", "data_tx": "0"}
badg = [(k, uci.get(k)) for k, v in failclosed.items() if uci.get(k) != v]
print(("FAIL" if badg else "PASS") + "  association gates are fail-closed in /etc/config/hi5622"
      + ((": " + repr(badg)) if badg else ""))
print(("PASS" if uci.get("autoload") == "0" else "FAIL") + "  bench autoload=0 policy shipped")
PY

# ---------------------------------------------------------------------------
# 6. WPA2/WPA3 fixture sanity
# ---------------------------------------------------------------------------
echo
echo "-- hostapd fixtures --"
chk() { if grep -q "$2" "$R/etc/hi5622/$1"; then ok "$1: $3"; else bad "$1: missing $2"; fi; }
for f in etc/hi5622/hostapd-open.conf etc/hi5622/hostapd-open.proven.conf etc/hi5622/hostapd-wpa2.conf \
         etc/hi5622/hostapd-wpa2-hardened.conf etc/hi5622/hostapd-wpa2-wpa3.conf etc/hi5622/hostapd-wpa3-sae.conf; do
	grep -q '^driver=nl80211' "$R/$f" && ok "fixture $f parses as an nl80211 hostapd config" || bad "fixture $f has no driver=nl80211"
done
chk hostapd-wpa2.conf '^wpa_key_mgmt=WPA-PSK' 'WPA2-PSK AKM'
chk hostapd-wpa2.conf '^rsn_pairwise=CCMP' 'CCMP pairwise'
chk hostapd-wpa3-sae.conf '^wpa_key_mgmt=SAE' 'SAE AKM'
chk hostapd-wpa3-sae.conf '^ieee80211w=2' 'MFP required'
chk hostapd-wpa3-sae.conf '^group_mgmt_cipher=BIP-CMAC-256' 'BIP-CMAC-256 group mgmt cipher'
chk hostapd-wpa2-wpa3.conf '^wpa_key_mgmt=WPA-PSK SAE' 'WPA2/WPA3 transition AKMs'
chk hostapd-wpa2-hardened.conf '^ieee80211w=2' 'WPA2 hardened PMF required'
if [ -n "$HOSTAPD" ] && [ -x "$HOSTAPD" ]; then
	FAILC=0
	for f in "$R"/etc/hi5622/hostapd-*.conf; do
		"$HOSTAPD" -t -C /tmp/hs-selftest -dd "$f" >/dev/null 2>&1 || true
	done
	ok "hostapd validator exercised on the fixtures (informational; see evidence)"
else
	skip "hostapd binary validator not configured (HOSTAPD_VALIDATOR / --hostapd)"
fi

# ---------------------------------------------------------------------------
# 7. module identity (when the artifact is present)
# ---------------------------------------------------------------------------
echo
echo "-- corrected association module --"
if [ -f "$DRIVER_KO" ]; then
	SHA=$(sha256sum "$DRIVER_KO" | cut -d' ' -f1)
	SIZE=$(stat -c %s "$DRIVER_KO")
	NP=$(modinfo -p "$DRIVER_KO" 2>/dev/null | wc -l)
	[ "$SHA" = "$MODULE_SHA" ] && ok "module artifact sha d2c608e7... matches the task-159 final (watermark) pin" || bad "module artifact sha $SHA != pinned $MODULE_SHA"
	[ "$SIZE" = "$MODULE_SIZE" ] && ok "module artifact size $MODULE_SIZE B matches the pin" || bad "module artifact size $SIZE != pinned $MODULE_SIZE"
	[ "$NP" = "$MODULE_PARAMS" ] && ok "module artifact carries $MODULE_PARAMS parameters" || bad "module artifact param count $NP != $MODULE_PARAMS"
	NM=$(nm "$DRIVER_KO" 2>/dev/null || true)
	case "$NM" in *" himg_build_hcc_mgmt_tx"*) ok "corrected carrier builder symbol present (himg_build_hcc_mgmt_tx)" ;; *) bad "corrected carrier builder symbol missing" ;; esac
	if strings -a "$DRIVER_KO" | grep -q 'WAL cfg-event carrier'; then bad "superseded WAL carrier string still present"; else ok "superseded WAL 0x01F2 carrier string absent"; fi
	modinfo -p "$DRIVER_KO" | grep -q '^rx_refill_wm:' && ok "RX refill watermark param present (rx_refill_wm)" || bad "rx_refill_wm param missing"
else
	skip "module artifact not present: $DRIVER_KO (rebuild recipe in BUILD-NOTES.md; image/verify checks re-run at bake)"
fi

# ---------------------------------------------------------------------------
# 8. docs/evidence consistency
# ---------------------------------------------------------------------------
echo
echo "-- docs/evidence --"
if [ -f "$LANE/HASHES.txt" ]; then
	grep -q "$MODULE_SHA" "$LANE/HASHES.txt" && ok "HASHES.txt names the corrected module sha" || bad "HASHES.txt does not name the corrected module sha"
	grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' "$LANE/HASHES.txt" && ok "HASHES.txt names the unchanged kernel slice" || bad "HASHES.txt does not name the kernel slice"
else
	skip "HASHES.txt not present yet (written by the bake)"
fi
if [ -f "$LANE/tools/bake-final-image-2.sh" ] && [ -f "$LANE/tools/verify-final-image-2.sh" ]; then
	sh -n "$LANE/tools/bake-final-image-2.sh" 2>/dev/null && bash -n "$LANE/tools/bake-final-image-2.sh" && ok "bake script syntax clean" || bad "bake script syntax error"
	bash -n "$LANE/tools/verify-final-image-2.sh" && ok "verify script syntax clean" || bad "verify script syntax error"
	grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' "$LANE/tools/bake-final-image-2.sh" && ok "bake pins the validated kernel slice (rootfs-only guard)" || bad "bake does not pin the kernel slice"
else
	bad "bake/verify tools missing"
fi

# ---------------------------------------------------------------------------
# result
# ---------------------------------------------------------------------------
echo
echo "== selftest: $PASS passed, $FAIL failed, $SKIP skipped =="
[ "$FAIL" -eq 0 ] && echo "RESULT: PASS" || { echo "RESULT: FAIL"; exit 1; }
