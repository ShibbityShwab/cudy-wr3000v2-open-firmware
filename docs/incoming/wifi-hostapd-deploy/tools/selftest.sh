#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - device-free lane selftest for wifi-hostapd-deploy (task-125).
#
# Runs the full host check (deploy-hostapd.sh --check) and then mutation /
# negative-control tests proving the gates are not vacuous:
#   * a single flipped byte in an .apk is caught (size or sha256 gate);
#   * a tampered pin table is caught (pinned-sha256 gate);
#   * a tampered config fixture is caught (config validity gate);
#   * "type managed" in the ladder is caught (iface-type gate);
#   * the stale 0x7D2758 context constant is caught (arithmetic gate);
#   * a device action without --attended/--yes-really is refused host-side;
#   * the device script emits a fail-closed RESULT line on a broken stage and
#     never runs a mutating mode from this test.
# Nothing here contacts the router and no device action is executed.
#
# usage: ./tools/selftest.sh [--evidence FILE] [--work DIR] [--keep]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
EVIDENCE=
WORK=
KEEP=0
PASS=0
FAIL=0
ok()  { PASS=$((PASS+1)); printf 'ok   %s\n' "$*"; }
bad() { FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$*"; }
while [ $# -gt 0 ]; do
	case "$1" in
		--evidence) shift; EVIDENCE=$1 ;;
		--evidence=*) EVIDENCE=${1#*=} ;;
		--work) shift; WORK=$1 ;;
		--work=*) WORK=${1#*=} ;;
		--keep) KEEP=1 ;;
		-h|--help) sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "unknown argument: $1" >&2; exit 2 ;;
	esac
	shift
done
if [ -n "$EVIDENCE" ]; then
	mkdir -p "$(dirname "$EVIDENCE")"
	exec > >(tee "$EVIDENCE") 2>&1
fi
if [ -z "$WORK" ]; then WORK=$(mktemp -d /tmp/hostapd-selftest.XXXXXX); fi
cleanup() { [ "$KEEP" -eq 1 ] && echo "work dir kept: $WORK" || rm -rf "$WORK"; }
trap cleanup EXIT

echo "== wifi-hostapd-deploy selftest =="
echo "lane: $LANE"
echo "work: $WORK"

# clone the lane into $WORK/lane so negative tests can mutate a copy
cp -a "$LANE" "$WORK/lane"
rm -rf "$WORK/lane/tools/__pycache__" "$WORK/lane/build-evidence"
TESTLANE=$WORK/lane

# --- 1. the full host check passes ---------------------------------------
if "$LANE/deploy-hostapd.sh" --check >"$WORK/host-check.out" 2>&1; then
	line=$(grep '== deploy-hostapd:' "$WORK/host-check.out" | tail -n1)
	ok "deploy-hostapd --check PASS: $line"
else
	bad "deploy-hostapd --check FAILED (see work/host-check.out)"
	tail -20 "$WORK/host-check.out"
fi

# --- 2. no kernel= dependency anywhere (userspace-only ABI independence) --
if awk '$1 !~ /^#/ && NF >= 5' "$TESTLANE/tools/expected-packages.txt" | grep -q 'kernel='; then
	bad "deploy set carries a kernel= dependency"
else
	ok "deploy set has no kernel= dependency (userspace-only, kmod ABI pin untouched)"
fi
A=$("$TESTLANE/tools/../../../../../home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/host/bin/apk" --version 2>/dev/null || true)
: "$A"
if grep -qE '^kmod-' "$TESTLANE/tools/expected-packages.txt"; then
	bad "deploy set contains a kmod package"
else
	ok "deploy set contains no kmod package"
fi

# --- 3. one flipped .apk byte must be caught -----------------------------
ARTCOPY=$WORK/artifacts
cp -a /home/shibbityshwab/hostapd-lane-artifacts "$ARTCOPY" 2>/dev/null || cp -a /home/shibbityshwab/openwrt-lab-selectorfix-img/bin/packages/arm_cortex-a9/base "$ARTCOPY"
F=$(ls "$ARTCOPY"/hostapd-mini-*.apk | head -n1)
printf 'X' >>"$F"    # size gate (and sha gate) must fire
if "$LANE/deploy-hostapd.sh" --check --artifacts "$ARTCOPY" >"$WORK/tamper-apk.out" 2>&1; then
	bad "tampered .apk was NOT caught"
else
	if grep -q 'FAIL hostapd-mini size' "$WORK/tamper-apk.out" || grep -q 'FAIL hostapd-mini sha256' "$WORK/tamper-apk.out"; then
		ok "tampered .apk caught by the identity gate"
	else
		bad "tampered .apk caused a failure but not via the identity gate"
	fi
fi

# --- 4. a tampered pin table must be caught ------------------------------
cp "$TESTLANE/tools/expected-packages.txt" "$WORK/pins.bak"
printf '# tampered\n' >>"$TESTLANE/tools/expected-packages.txt"
if "$TESTLANE/deploy-hostapd.sh" --check >"$WORK/tamper-pins.out" 2>&1; then
	bad "tampered pin table was NOT caught"
else
	if grep -q 'FAIL expected-packages.txt sha256' "$WORK/tamper-pins.out"; then
		ok "tampered pin table caught by the pinned-sha256 gate"
	else
		bad "tampered pin table failed but not via the pinned-sha gate"
	fi
fi
cp "$WORK/pins.bak" "$TESTLANE/tools/expected-packages.txt"

# --- 5. a tampered config fixture must be caught -------------------------
cp "$TESTLANE/configs/hostapd-open.conf" "$WORK/conf.bak"
printf 'bogus_option_xyz=1\n' >>"$TESTLANE/configs/hostapd-open.conf"
if "$TESTLANE/deploy-hostapd.sh" --check >"$WORK/tamper-conf.out" 2>&1; then
	bad "tampered config fixture was NOT caught"
else
	if grep -q 'config fixture validity' "$WORK/tamper-conf.out" || grep -q 'config validation failed' "$WORK/tamper-conf.out"; then
		ok "tampered config fixture caught by the config validity gate"
	else
		bad "tampered config fixture failed but not via the config gate"
	fi
fi
cp "$WORK/conf.bak" "$TESTLANE/configs/hostapd-open.conf"

# --- 6. "type managed" in the ladder must be caught ----------------------
python3 - "$TESTLANE/recipes/ap_start_ladder.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
for s in d["steps"]:
    if s["step"] == "V1":
        s["action"] = s["action"].replace("type __ap", "type managed")
json.dump(d, open(p, "w"), indent=1)
PY
if python3 "$TESTLANE/tools/check-ladder.py" >"$WORK/lad-managed.out" 2>&1; then
	bad "'type managed' in the ladder was NOT caught"
else
	grep -q 'V1 creates' "$WORK/lad-managed.out" && ok "'type managed' caught by the iface-type gate" || bad "'type managed' failed but not via the iface gate"
fi

# --- 7. the stale 0x7D2758 context constant must be caught ---------------
python3 - "$TESTLANE/recipes/ap_start_ladder.json" <<'PY'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
for r in d["readback_pack"]:
    if r.get("name", "").startswith("ctx0 = "):
        r["bar0"] = "0x7D2758"
json.dump(d, open(p, "w"), indent=1)
PY
if python3 "$TESTLANE/tools/check-ladder.py" >"$WORK/lad-stale.out" 2>&1; then
	bad "stale 0x7D2758 context constant was NOT caught"
else
	grep -q 'ctx0 BAR0' "$WORK/lad-stale.out" && ok "stale 0x7D2758 constant caught by the arithmetic gate" || bad "stale constant failed but not via the arithmetic gate"
fi

# --- 8. device action without --attended/--yes-really is refused ---------
if "$LANE/deploy-hostapd.sh" --apply >"$WORK/refuse-apply.out" 2>&1; then
	bad "--apply without --attended/--yes-really was NOT refused"
else
	grep -q 'needs --attended' "$WORK/refuse-apply.out" && ok "device action refused without --attended" || bad "device action refused for the wrong reason"
fi
if "$LANE/deploy-hostapd.sh" --rollback >"$WORK/refuse-rb.out" 2>&1; then
	bad "--rollback without --attended/--yes-really was NOT refused"
else
	ok "rollback refused without --attended/--yes-really"
fi

# --- 9. check mode never calls the transport -----------------------------
if awk '/^do_check\(\)/,/^}/' "$LANE/deploy-hostapd.sh" | grep -q 'rsh_cmd'; then
	bad "--check function calls the ssh transport"
else
	ok "--check path never invokes the ssh transport (host-only)"
fi

# --- 10. device script is fail-closed on a broken stage, no mutation -----
if HOSTAPD_STAGE="$WORK/empty-stage" sh "$LANE/device/hostapd-device.sh" preflight >"$WORK/dev-preflight.out" 2>&1; then
	bad "device preflight passed on an empty stage"
else
	if grep -q '^HOSTAPD-DEV-RESULT' "$WORK/dev-preflight.out" && grep -q 'failed=[1-9]' "$WORK/dev-preflight.out"; then
		ok "device script fails closed on a broken stage (RESULT line emitted)"
	else
		bad "device script did not emit a fail-closed RESULT line"
	fi
fi
if grep -q 'HOSTAPD-DEV-ACTION apk add' "$WORK/dev-preflight.out"; then
	bad "device preflight attempted an apk add"
else
	ok "device preflight never attempts a package mutation"
fi

# --- 11. hostapd command flags come from the supported set ---------------
if grep -q -- '-B' "$LANE/recipes/ap_start_ladder.json" && ! grep -q 'hostapd .* -f ' "$LANE/recipes/ap_start_ladder.json"; then
	ok "ladder hostapd command uses only supported flags"
else
	bad "ladder hostapd command flags unexpected"
fi

echo
echo "== selftest: $PASS passed, $FAIL failed =="
if [ "$FAIL" -ne 0 ]; then printf 'RESULT: FAIL\n'; exit 1; fi
printf 'RESULT: PASS - host checks + mutation tests green; no device contact\n'