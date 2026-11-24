#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# selftest.sh - host-only selftest of the wifi-bench-session package.
#
# Never contacts the router.  Checks: shell syntax, python compile, sequence
# schema validation through the committed runner, hard-rule token audit,
# hostapd config sanity, artifact pins, generated device-script syntax, and a
# full dry-run plan of phases S/A (bench_session --dry-run only).
#
# usage: tools/selftest.sh [--quick]     (--quick skips the dry-run plan)
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
ROOT=$(cd "$LANE/../../../.." && pwd)
PY="$ROOT/.provision-venv/bin/python"
[ -x "$PY" ] || PY=python3
BENCH_TOOLS="$ROOT/opensource/docs/incoming/wifi-bench-tooling/tools"
QUICK=0
[ "${1:-}" = "--quick" ] && QUICK=1
PASS=0 FAIL=0
ok()  { PASS=$((PASS + 1)); printf 'PASS %s\n' "$*"; }
bad() { FAIL=$((FAIL + 1)); printf 'FAIL %s\n' "$*"; }
cd "$LANE" || exit 1
echo "== shell syntax =="
for f in device/ap-fire.sh device/ap-readback.sh host/run-session.sh \
         tools/check-artifacts.sh tools/selftest.sh generated/*.sh; do
  if [ -f "$f" ] && sh -n "$f" 2>/dev/null; then ok "sh -n $f"; else bad "sh -n $f"; fi
done
echo "== python compile =="
if "$PY" -m py_compile host/session_driver.py 2>/dev/null; then ok "py_compile session_driver.py"; else bad "py_compile session_driver.py"; fi
rm -rf host/__pycache__ 2>/dev/null || true
echo "== sequence schema (committed runner) =="
for seq in sequences/session-scan13.json sequences/session-ap-base.json; do
  if "$PY" - "$BENCH_TOOLS" "$seq" <<'PYEOF'
import sys
sys.path.insert(0, sys.argv[1])
import protocol_sequence as PS
seq = PS.load(sys.argv[2])
doc = PS.plan_doc(seq)
print("  %s: sha256=%s steps=%d checks=%d writes=%d"
      % (seq["name"], seq["sha256"][:16], doc["totals"]["steps"],
         doc["totals"]["checks"], doc["totals"]["attr_writes"]))
assert seq["profile"] == "hi5622-wifi-cfg80211", seq["profile"]
assert doc["totals"]["steps"] > 0
PYEOF
  then ok "sequence $seq"; else bad "sequence $seq"; fi
done
echo "== hard-rule token audit =="
audit_fail=0
if grep -rnE '^[^#]*\brmmod\b' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rnE 'ttyUSB|ttyAMA|/dev/tty' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rniE 'mtd (erase|write)|nandwrite|flash_erase' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rn '10161000' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rnE '^[^#]*(0x400392f0|0x3f12f0)' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rnE 'devmem +[0-9a-fA-Fx]+ +(8|16|32|64) ' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
if grep -rnE 'poweroff|halt -f|echo b > /proc/sysrq' device/ host/ sequences/ generated/ 2>/dev/null; then audit_fail=1; fi
[ "$audit_fail" -eq 0 ] && ok "no rmmod/UART/flash/CA-write/devmem-write tokens" || bad "hard-rule token audit"
echo "== hostapd config =="
CONF=device/hostapd-open.conf
for want in 'interface=wlan0' 'driver=nl80211' 'beacon_int=100' 'dtim_period=2' 'auth_algs=1'; do
  grep -qx "$want" "$CONF" && ok "hostapd.conf $want" || bad "hostapd.conf missing $want"
done
if grep -qE '^wpa=' "$CONF"; then bad "hostapd.conf must stay OPEN (no wpa=)"; else ok "hostapd.conf OPEN network"; fi
echo "== artifact pins =="
if sh tools/check-artifacts.sh >/tmp/wifi-bench-session-pins.$$ 2>&1; then
  ok "artifact pins (pending phases noted)"; grep -E 'DEPENDENCY-PENDING' /tmp/wifi-bench-session-pins.$$ | sed 's/^/  /' || true
else
  bad "artifact pins"; tail -5 /tmp/wifi-bench-session-pins.$$
fi
rm -f /tmp/wifi-bench-session-pins.$$
echo "== no vendor binaries in the lane =="
if find . -type f \( -name '*.ko' -o -name '*.bin' -o -name '*.o' -o -name '*.fw' -o -name '*.img' -o -name '*.apk' -o -name '*.ipk' \) | grep -q .; then
  bad "binary artifact found in the tracked lane (must stay local evidence)"; find . -type f \( -name '*.ko' -o -name '*.bin' -o -name '*.o' \) | sed 's/^/  /'
else
  ok "no vendor/binary artifacts"
fi
if [ "$QUICK" -eq 0 ]; then
  echo "== dry-run plan (bench_session --dry-run; host only) =="
  OUT=/tmp/wifi-bench-session-selftest.$$
  if "$PY" host/session_driver.py --phases s,a,x --dry-run \
        --evidence-dir "$OUT" >"$OUT.log" 2>&1; then
    ok "dry-run plan S+A+X"
    grep -E 'phase [SAX] ->|DEPENDENCY-PENDING' "$OUT.log" | sed 's/^/  /' || true
  else
    bad "dry-run plan S+A+X"; tail -20 "$OUT.log"
  fi
  rm -rf "$OUT" "$OUT.log"
else
  echo "== dry-run plan skipped (--quick) =="
fi
echo ""
echo "selftest: $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
exit 0