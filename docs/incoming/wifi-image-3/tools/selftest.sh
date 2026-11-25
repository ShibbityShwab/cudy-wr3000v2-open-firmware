#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - host-only, artifact-free checks of the wifi-image-3 lane
# (lane wifi-image-3, task-196).  No device, no network, no container needed.
#
# Checks the text payload (loader/uci/modprobe/init/hostapd fixtures/notes),
# the lane scripts' syntax, the payload-vs-loader-vs-module parameter
# consistency, and (when the local module artifacts exist) the pinned IMAGE #3
# module identity.  It does NOT need sudo and never writes outside a temp dir.
#
# usage: ./tools/selftest.sh
set -uo pipefail
export LC_ALL=C

HERE=$(cd "$(dirname "$0")" && pwd)
LANE=$(cd "$HERE/.." && pwd)
P=$LANE/payload
MOD_INST=/home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.installed.ko
MOD_ACT=/home/shibbityshwab/wifi-image-3-module-artifacts/hi5622_wifi.activity.ko

PASS=0; FAIL=0; SKIP=0
pass() { PASS=$((PASS+1)); echo "PASS  $*"; }
fail() { FAIL=$((FAIL+1)); echo "FAIL  $*"; }
skip() { SKIP=$((SKIP+1)); echo "SKIP  $*"; }
ok() { if "$@" >/dev/null 2>&1; then pass "$DESC"; else fail "$DESC"; fi; }

echo "== wifi-image-3 selftest (host-only, task-196) =="
echo

# ---- 1. payload presence + shell syntax ----------------------------------
for f in etc/config/hi5622 usr/sbin/hi5622-load etc/modprobe.d/hi5622-wifi.conf etc/init.d/hi5622-wifi etc/hi5622/hostapd-open-ax.conf etc/hi5622/hostapd-open-5g40.conf etc/hi5622/IMAGE-3-NOTES.txt; do
	[ -f "$P/$f" ] && pass "payload file present: $f" || fail "payload file MISSING: $f"
done
sh -n "$P/usr/sbin/hi5622-load" && pass "hi5622-load passes sh -n" || fail "hi5622-load shell syntax"
sh -n "$P/etc/init.d/hi5622-wifi" && pass "hi5622-wifi init passes sh -n" || fail "init shell syntax"
bash -n "$LANE/tools/bake-image-3.sh" && pass "bake-image-3.sh passes bash -n" || fail "bake script syntax"
bash -n "$LANE/tools/verify-image-3.sh" && pass "verify-image-3.sh passes bash -n" || fail "verify script syntax"
[ -x "$P/usr/sbin/hi5622-load" ] && pass "loader is executable in payload" || fail "loader not executable"
[ -x "$P/etc/init.d/hi5622-wifi" ] && pass "init script is executable in payload" || fail "init not executable"

# ---- 2. uci policy -------------------------------------------------------
U=$P/etc/config/hi5622
grep -q "^config driver 'main'" "$U" && pass "uci driver main section present" || fail "uci driver main section missing"
grep -q "^config ap 'ap'" "$U" && pass "uci ap section present" || fail "uci ap section missing"
grep -q "option autoload '0'" "$U" && pass "bench default autoload=0 (nothing loads at boot)" || fail "autoload not fail-closed"
grep -q 'IMAGE #3' "$U" && pass "uci labelled IMAGE #3" || fail "uci not labelled IMAGE #3"
for o in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q "option $o '" "$U" && pass "uci image #3 gate present: $o" || fail "uci gate missing: $o"
done
grep -q "option band5g '0'" "$U" && pass "band5g ships 0 (fail-closed)" || fail "band5g not fail-closed"
grep -q "option vap5g '0'" "$U" && pass "vap5g ships 0 (fail-closed)" || fail "vap5g not fail-closed"
grep -q "option desc_table '0'" "$U" && pass "desc_table ships 0 (fail-closed)" || fail "desc_table not fail-closed"
grep -q "option dfs_mode '0'" "$U" && pass "dfs_mode ships 0 (fail-closed)" || fail "dfs_mode not fail-closed"
grep -q "option he_caps '0'" "$U" && pass "he_caps ships 0 (fail-closed)" || fail "he_caps not fail-closed"
grep -q "option ba_enable '0'" "$U" && pass "ba_enable ships 0 (fail-closed)" || fail "ba_enable not fail-closed"
grep -q "option ap_chan_wait_ms '300'" "$U" && pass "ap_chan_wait_ms ships the proven 300 ms" || fail "ap_chan_wait_ms default wrong"
grep -q "option rx_refill_wm '75'" "$U" && pass "rx_refill_wm kept from image #2 (75)" || fail "rx_refill_wm missing"
grep -q "option country ''" "$U" && pass "AP country remains operator-set (regdomain)" || fail "AP country default changed"

# ---- 3. loader policy ----------------------------------------------------
L=$P/usr/sbin/hi5622-load
grep -q 'IMAGE #3' "$L" && pass "loader labelled IMAGE #3" || fail "loader not labelled"
grep -q 'a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed' "$L" && pass "loader records the installed module sha (a717c86d...)" || fail "loader module sha missing"
grep -q '545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99' "$L" && pass "loader records the activity module sha (545192df...)" || fail "loader activity sha missing"
grep -q '64 module parameters' "$L" && pass "loader records the 64-parameter surface" || fail "loader param count text missing"
for p in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q -- "$p=" "$L" && pass "loader passes $p" || fail "loader does not pass $p"
done
for p in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q "verify_param $p " "$L" && pass "loader verifies $p readback" || fail "loader does not verify $p"
done
grep -q 'require band5g=1' "$L" && pass "loader refuses the 5 GHz gates without band5g=1" || fail "loader band5g consistency guard missing"
grep -q 'he_caps=$HE_CAPS (0/1 only' "$L" && pass "loader refuses he_caps>1 before load" || fail "loader he_caps range guard missing"
grep -qE '^[[:space:]]*(sudo[[:space:]]+)?rmmod' "$L" && fail "loader invokes rmmod (forbidden after a CPU release)" || pass "loader never invokes rmmod (bench rule; the string appears only in the policy message)"
grep -q 'rmmod is forbidden' "$L" && pass "loader explains why it refuses to reload after a load-time mismatch" || fail "loader lost the load-time-parameter policy message"
grep -q 'insmod "$DRV_KO"' "$L" && pass "loader uses explicit insmod (the proven path)" || fail "loader lost the insmod path"

# ---- 4. modprobe mirror --------------------------------------------------
M=$P/etc/modprobe.d/hi5622-wifi.conf
grep -q 'IMAGE #3' "$M" && pass "modprobe conf labelled IMAGE #3" || fail "modprobe conf not labelled"
grep -q 'a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed' "$M" && pass "modprobe conf records the installed module sha" || fail "modprobe conf module sha missing"
grep -q '^options hi5622_wifi ' "$M" && pass "modprobe options line present" || fail "modprobe options line missing"
for p in band5g vap5g ap_chan_wait_ms desc_table dfs_mode he_caps ba_enable; do
	grep -q "$p=" "$M" && pass "modprobe defaults carry $p" || fail "modprobe defaults missing $p"
done
grep -q 'band5g=0' "$M" && grep -q 'he_caps=0' "$M" && grep -q 'ba_enable=0' "$M" && pass "modprobe defaults are fail-closed" || fail "modprobe defaults not fail-closed"
grep -q 'rx_refill_wm=75' "$M" && pass "modprobe defaults keep rx_refill_wm=75" || fail "modprobe rx_refill_wm missing"

# uci-vs-modprobe value sync for the gates that must match the loader policy
SYNC_OK=1
for p in band5g vap5g desc_table dfs_mode he_caps ba_enable; do
	UV=$(grep "option $p '" "$U" | sed "s/.*option $p '//" | sed "s/'//")
	MV=$(sed -n "s/^options hi5622_wifi //p" "$M" | tr ' ' '\n' | grep "^$p=" | head -1 | cut -d= -f2)
	[ "$UV" = "$MV" ] || { SYNC_OK=0; echo "      sync: uci $p=$UV modprobe $p=$MV"; }
done
[ "$SYNC_OK" = "1" ] && pass "uci and modprobe agree on every image #3 gate value" || fail "uci and modprobe gate values drift"
# every uci key the loader reads must exist in the shipped uci
grep -o 'uci_get main.[a-z_0-9]*' "$L" | sed 's/uci_get main.//' | sort -u > "$LANE/build-evidence/.selftest-loader-keys"
grep -o "option [a-z_0-9]* " "$U" | sed 's/option //' | sed 's/ //' | sort -u > "$LANE/build-evidence/.selftest-uci-keys"
BAD=$(comm -23 "$LANE/build-evidence/.selftest-loader-keys" "$LANE/build-evidence/.selftest-uci-keys" | tr -d ' ')
rm -f "$LANE/build-evidence/.selftest-loader-keys" "$LANE/build-evidence/.selftest-uci-keys"
[ -z "$BAD" ] && pass "every loader uci key exists in the shipped uci" || fail "loader reads uci keys not shipped: $BAD"

# ---- 5. hostapd fixtures -------------------------------------------------
for f in hostapd-open-ax.conf hostapd-open-5g40.conf; do
	F=$P/etc/hi5622/$f
	[ -f "$F" ] && pass "fixture present: $f" || fail "fixture MISSING: $f"
	for k in driver interface ssid hw_mode channel auth_algs; do
		grep -q "^$k=" "$F" && pass "$f has $k" || fail "$f missing $k"
	done
done
grep -q '^ieee80211ax=1' "$P/etc/hi5622/hostapd-open-ax.conf" && pass "2.4 GHz ax fixture enables ieee80211ax" || fail "ax fixture lacks ieee80211ax=1"
grep -q '^channel=6' "$P/etc/hi5622/hostapd-open-ax.conf" && pass "2.4 GHz ax fixture on channel 6" || fail "ax fixture channel wrong"
grep -q '^channel=40' "$P/etc/hi5622/hostapd-open-5g40.conf" && pass "5 GHz fixture on the F2-proven channel 40" || fail "5G fixture channel wrong"
grep -q '^hw_mode=a' "$P/etc/hi5622/hostapd-open-5g40.conf" && pass "5 GHz fixture uses hw_mode=a" || fail "5G fixture hw_mode wrong"
grep -q 'cudy-ap-test-5g40' "$P/etc/hi5622/hostapd-open-5g40.conf" && pass "5 GHz fixture keeps the F2 fire SSID" || fail "5G fixture SSID changed"

# ---- 6. notes + publication policy -------------------------------------
N=$P/etc/hi5622/IMAGE-3-NOTES.txt
[ -f "$N" ] && pass "operator notes present" || fail "operator notes missing"
grep -q 'FIRMWARE.PD4.bin' "$N" && pass "notes state the firmware push policy" || fail "notes missing firmware policy"
grep -q 'band5g=1' "$N" && pass "notes carry the F2 5 GHz recipe" || fail "notes missing the 5 GHz recipe"
grep -q 'he_caps=1' "$N" && pass "notes carry the 802.11ax recipe" || fail "notes missing the AX recipe"
TEXT_BAD=$(file "$P"/etc/config/hi5622 "$P"/usr/sbin/hi5622-load "$P"/etc/modprobe.d/hi5622-wifi.conf "$P"/etc/init.d/hi5622-wifi "$P"/etc/hi5622/*.conf "$P"/etc/hi5622/*.txt | grep -v -E 'ASCII text|Unicode text|POSIX shell script|shell script' | wc -l)
[ "$TEXT_BAD" = "0" ] && pass "every payload file is plain text (no binary in the tracked payload)" || fail "payload contains non-text files"
VEND=$(file "$P"/* "$P"/*/* 2>/dev/null | grep -c -E 'firmware|ELF|data$' || true)
[ "$VEND" = "0" ] && pass "no vendor binary/firmware is staged in the payload" || fail "payload contains binary/firmware-like files"

# ---- 7. local module artifacts (optional, local evidence only) ----------
if [ -f "$MOD_INST" ] && [ -f "$MOD_ACT" ]; then
	S=$(sha256sum "$MOD_INST" | cut -d' ' -f1)
	[ "$S" = "a717c86db2b974cfa4c5560648508cbb589f67b47393a48993ee8eb939321fed" ] && pass "local installed module sha matches the pin" || fail "local installed module sha drift"
	[ "$(stat -c %s "$MOD_INST")" = "358544" ] && pass "local installed module size 358,544 B" || fail "local installed module size drift"
	S2=$(sha256sum "$MOD_ACT" | cut -d' ' -f1)
	[ "$S2" = "545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99" ] && pass "local activity module sha matches the pin" || fail "local activity module sha drift"
	[ "$(modinfo -p "$MOD_INST" | wc -l)" = "64" ] && pass "local module exposes 64 parameters" || fail "local module parameter count drift"
	case "$(modinfo -F vermagic "$MOD_INST")" in *6.18.60*) pass "local module vermagic 6.18.60" ;; *) fail "local module vermagic drift" ;; esac
	case "$(modinfo -F depends "$MOD_INST")" in *cfg80211*) pass "local module depends on cfg80211" ;; *) fail "local module depends drift" ;; esac
	modinfo -p "$MOD_INST" | sed 's/:.*//' | sort -u > /tmp/wifi-image-3-selftest-mod-params.txt
	awk '/run insmod .*DRV_KO/{f=1;next} f&&/DRY_RUN/{exit} f' "$L" | grep -o '"[a-z_0-9]*=' | sed 's/^"//' | sed 's/=$//' | sort -u > /tmp/wifi-image-3-selftest-loader-params.txt
	BADP=$(comm -23 /tmp/wifi-image-3-selftest-loader-params.txt /tmp/wifi-image-3-selftest-mod-params.txt | tr -d ' ')
	rm -f /tmp/wifi-image-3-selftest-mod-params.txt /tmp/wifi-image-3-selftest-loader-params.txt
	[ -z "$BADP" ] && pass "loader parameters are a subset of the module's 64 parameters" || fail "loader passes unknown module params: $BADP"
else
	skip "local module artifacts absent at $MOD_INST (identity checks need the local .ko; never committed)"
fi

echo
echo "selftest: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ] || exit 1
exit 0
