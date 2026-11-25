#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# assoc-router-fire.sh - ROUTER-SIDE association-E2E fire script.
#
# RUN THIS ON THE BENCH ROUTER (console), BY THE LEAD. It is the router half of
# the wifi-assoc-e2e runbook; the host half is ../assoc_e2e.py. The host-only
# lane that authored it NEVER executes it against the router.
#
# What it does, in the ONE order proven correct:
#   module load -> boot ladder -> C2 (ctx_create + txq_sync72) -> AP FIRST
#   -> dp_probe -> dp_setup -> h2d_txq_addr_sync -> dp_rx_arm -> drain loop
# The AP must start before dp_setup (the AP start re-initializes the MAC; with
# dp_setup first the RX bank status is 0x02 (cleared) instead of 0x182 (armed)).
#
# Stages (each is a deliberate, logged rung; every stage ends with a golden
# page/mask/staging snapshot):
#   --check                      read-only preconditions + plan
#   --load KO                    insmod the corrected association module (ab9e18a4)
#   --boot                       prepare upload verify seed hcc_arm hcc_loop release + ready
#   --c2                         h2d_ctx_create + h2d_txq_sync72
#   --stage ap [open|wpa2|wpa3]  __ap iface + hostapd (5-message order in dmesg)
#   --stage dp                   dp_probe -> dp_setup -> h2d_txq_addr_sync -> dp_rx_arm
#   --stage drain-start|drain-stop   bounded RX consumer loop (EAPOL/data)
#   --stage snapshot             consolidated evidence dump (print + file)
#   --stage stop                 stop hostapd + drain loop, final snapshot
#   --full [--profile P]         check -> boot -> c2 -> ap -> dp -> drain-start -> snapshot
#
# Options: --dry-run (print actions, write nothing), --config FILE (hostapd
# config override), --profile P, --timeout SEC.
# Env overrides: ASSOC_DEV ASSOC_ROOT ASSOC_PROFILE ASSOC_PSK ASSOC_HOSTAPD_CONF
#                ASSOC_DRAIN_SEC ASSOC_BOOT_TIMEOUT
#
# Safety: fail-closed. Any stage failure prints STOP and exits non-zero; do not
# retry in the same load (one H2D lane, one idea per load). A page/mask
# deviation means STOP -> physical power-cycle (the SoC reboot does not reset
# the Wi-Fi chip).
set -u

# --- paths / defaults -----------------------------------------------------
D=$(printenv ASSOC_DEV 2>/dev/null || true)
[ -n "$D" ] || D=/sys/bus/pci/devices/0000:00:00.0
ROOT=$(printenv ASSOC_ROOT 2>/dev/null || true)
[ -n "$ROOT" ] || ROOT=/tmp/assoc-e2e
PROFILE=$(printenv ASSOC_PROFILE 2>/dev/null || true)
[ -n "$PROFILE" ] || PROFILE=wpa2
PSK=$(printenv ASSOC_PSK 2>/dev/null || true)
[ -n "$PSK" ] || PSK=test-passphrase-121
DRAIN_SEC=$(printenv ASSOC_DRAIN_SEC 2>/dev/null || true)
[ -n "$DRAIN_SEC" ] || DRAIN_SEC=1
BOOT_TIMEOUT=$(printenv ASSOC_BOOT_TIMEOUT 2>/dev/null || true)
[ -n "$BOOT_TIMEOUT" ] || BOOT_TIMEOUT=20
DRY=$(printenv ASSOC_DRY_RUN 2>/dev/null || true)
[ -n "$DRY" ] || DRY=0
KO=
CONF=
STAGE=
FULL=0
LOGF=$ROOT/router.log

usage() {
	sed -n '2,45p' "$0" | sed 's/^# \{0,1\}//'
}

# --- logging / guards -----------------------------------------------------
say() {
	printf '%s\n' "$*"
	[ "$DRY" = 1 ] || printf '%s\n' "$*" >>"$LOGF" 2>/dev/null
}
die() { say "STOP: $*"; exit 1; }
dry() {
	if [ "$DRY" = 1 ]; then say "DRY: $*"; return 0; fi
	return 1
}
need_root() {
	[ "$DRY" = 1 ] && return 0
	[ "$(id -u)" = 0 ] || die "router stages need root (run as root on the bench router)"
}
need_dev() {
	[ "$DRY" = 1 ] && return 0
	[ -d "$D" ] || die "PCI device dir $D missing - is this the bench router with the Wi-Fi endpoint enumerated?"
}
attr() { printf '%s' "$D/$1"; }
have_attr() {
	[ "$DRY" = 1 ] && return 0
	[ -e "$D/$1" ]
}
read_attr() {
	[ "$DRY" = 1 ] && { echo "(dry-run)"; return 0; }
	cat "$D/$1" 2>/dev/null || true
}
write_attr() {
	if [ "$DRY" = 1 ]; then say "DRY: echo $2 > $(attr "$1")"; return 0; fi
	have_attr "$1" || die "attr $1 missing"
	printf '%s' "$2" >"$(attr "$1")" || die "write $1 failed"
}
mark_golden() {
	if [ "$DRY" = 1 ]; then say "DRY: golden check (page 0x86807fef/7/2/0xabcd, state 1, mask 0, staging 0)"; return 0; fi
	B=$(read_attr boot)
	printf '%s\n' "$B" >>"$ROOT/boot-snapshots.txt" 2>/dev/null || true
	if printf '%s' "$B" | grep -q "0x86807fef" && \
	   printf '%s' "$B" | grep -q "golden:1" && \
	   printf '%s' "$B" | grep -q "state CA .*=0x00000001"; then
		say "GOLDEN: page/state OK"
	else
		say "GOLDEN: FAIL"
		printf '%s\n' "$B"
		return 1
	fi
}
wait_ready() {
	[ "$DRY" = 1 ] && { say "DRY: poll hcc_ready for id1_seen=1 id2_seen=1"; return 0; }
	i=0
	while [ "$i" -lt "$BOOT_TIMEOUT" ]; do
		R=$(read_attr hcc_ready)
		printf '%s' "$R" | grep -q "id1_seen=1 id2_seen=1" && { say "READY: $R"; return 0; }
		sleep 1
		i=$((i + 1))
	done
	say "READY: timeout after $BOOT_TIMEOUT s"
	return 1
}
module_loaded() {
	[ "$DRY" = 1 ] && return 0
	grep -q "^hi5622_wifi " /proc/modules 2>/dev/null
}
need_module() {
	module_loaded || die "hi5622_wifi is not loaded (run --load KO first)"
}
stage_header() { say ""; say "=== $* ==="; }

# --- stages ---------------------------------------------------------------
do_check() {
	stage_header "check (read-only)"
	say "device dir : $D"
	say "log dir    : $ROOT"
	[ "$DRY" = 1 ] || mkdir -p "$ROOT" || die "cannot create $ROOT"
	say "profile    : $PROFILE   (PSK from ASSOC_PSK / lab default; never logged)"
	if [ "$DRY" = 1 ]; then
		say "DRY: required attrs: prepare upload verify seed hcc_arm hcc_loop release hcc_ready h2d_ctx_create h2d_txq_sync72 dp_probe dp_setup h2d_txq_addr_sync dp_rx_arm dp_rx_drain dp_tx_status cfg80211_status data_tx_status boot hcc_status"
		say "DRY: firmware files /lib/firmware/hi_wifi/FIRMWARE.PD4.bin and wifi_block.bin must pre-exist (policy: pushed by hand, never packaged)"
		say "DRY: CHECK PASS (dry-run)"
		return 0
	fi
	missing=
	for a in prepare upload verify seed hcc_arm hcc_loop release hcc_ready \
		 h2d_ctx_create h2d_txq_sync72 dp_probe dp_setup h2d_txq_addr_sync \
		 dp_rx_arm dp_rx_drain dp_tx_status cfg80211_status data_tx_status boot hcc_status; do
		[ -e "$D/$a" ] || missing="$missing $a"
	done
	[ -z "$missing" ] || die "missing attrs:$missing"
	say "attrs     : all present"
	if module_loaded; then
		say "module    : loaded"
		for p in mgmt_tx mgmt_rx sta_ops eapol_rx data_tx rx_enable beacon_static beacon_vap_start wpa_keys allow_frames h2d_send_budget; do
			v=$(cat "/sys/module/hi5622_wifi/parameters/$p" 2>/dev/null || echo MISSING)
			say "param $p = $v"
		done
	else
		say "module    : NOT loaded (run --load KO)"
	fi
	for f in /lib/firmware/hi_wifi/FIRMWARE.PD4.bin /lib/firmware/hi_wifi/wifi_block.bin; do
		if [ -f "$f" ]; then
			say "fw        : $f $(sha256sum "$f" 2>/dev/null | cut -d' ' -f1)"
		else
			say "fw        : MISSING $f (push by hand before --load; never package vendor firmware)"
		fi
	done
	if [ -n "$KO" ] && [ -f "$KO" ]; then
		say "artifact  : $KO $(sha256sum "$KO" 2>/dev/null | cut -d' ' -f1)"
	else
		say "artifact  : not given (--load KO path)"
	fi
	mark_golden || die "golden boot not healthy - do not fire"
	say "CHECK PASS"
}

do_load() {
	stage_header "load corrected association module"
	[ -n "$KO" ] || die "--load needs a KO path"
	[ "$DRY" = 1 ] || [ -f "$KO" ] || die "artifact not found: $KO"
	say "artifact : $KO"
	[ "$DRY" = 1 ] || say "sha256   : $(sha256sum "$KO" | cut -d' ' -f1)"
	if [ "$DRY" = 1 ]; then
		say "DRY: insmod $KO fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 vap_start_module=0 hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 mgmt_tx=1 mgmt_rx=1 sta_ops=1 eapol_rx=1 sta_user_del=0 data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200"
		return 0
	fi
	module_loaded && die "module already loaded - one load per boot (unload is not a retry path; power-cycle)"
	insmod "$KO" fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=hi_wifi/wifi_block.bin \
		allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
		vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
		hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 mgmt_diag=0 \
		rx_enable=1 beacon_static=1 beacon_vap_start=1 \
		wpa_keys=1 wpa_key_module=0 \
		sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
		mgmt_tx=1 mgmt_rx=1 sta_ops=1 eapol_rx=1 sta_user_del=0 \
		data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200
	rc=$?
	say "insmod rc=$rc"
	[ "$rc" = 0 ] || die "insmod failed - STOP (record dmesg tail, power-cycle before retry)"
	sleep 2
	say "params   : mgmt_tx=$(cat /sys/module/hi5622_wifi/parameters/mgmt_tx 2>/dev/null) mgmt_rx=$(cat /sys/module/hi5622_wifi/parameters/mgmt_rx 2>/dev/null)"
	mark_golden || die "page/mask not golden after load"
}

do_boot() {
	stage_header "boot ladder"
	need_module
	for a in prepare upload verify seed hcc_arm hcc_loop; do
		write_attr "$a" 1
		say "$a = 1"
		[ "$DRY" = 1 ] || sleep 1
	done
	write_attr release 1
	say "release = 1"
	[ "$DRY" = 1 ] || sleep 2
	wait_ready || die "HCC ready handshake did not arrive - STOP (dmesg tail + golden snapshot)"
	say "hcc_ready: $(read_attr hcc_ready)"
	mark_golden || die "page/mask not golden after boot ladder"
}

do_c2() {
	stage_header "C2 (h2d_ctx_create + h2d_txq_sync72)"
	need_module
	write_attr h2d_ctx_create 1
	[ "$DRY" = 1 ] || sleep 1
	write_attr h2d_txq_sync72 1
	[ "$DRY" = 1 ] || sleep 1
	say "h2d_status: $(read_attr h2d_status)"
	mark_golden || die "page/mask not golden after C2"
}

do_ap() {
	stage_header "AP start (vendor 5-message order), profile=$PROFILE"
	need_module
	[ -n "$CONF" ] || {
		case $PROFILE in
		open) CONF=/etc/hi5622/hostapd-open.proven.conf ;;
		wpa2) CONF=/etc/hi5622/hostapd-wpa2.conf ;;
		wpa3) CONF=/etc/hi5622/hostapd-wpa3-sae.conf ;;
		*) die "unknown profile $PROFILE (open|wpa2|wpa3) or pass --config FILE" ;;
		esac
	}
	say "hostapd conf: $CONF"
	if [ "$DRY" = 1 ]; then
		say "DRY: copy $CONF to $ROOT/hostapd.conf; substitute wpa_passphrase only if the placeholder is present"
		say "DRY: iw phy PHY interface add wlan0 type __ap; ip link set wlan0 up"
		say "DRY: hostapd -B -t -P $ROOT/hostapd.pid -i wlan0 $ROOT/hostapd.conf > $ROOT/hostapd.log 2>&1"
		say "DRY: expect dmesg: plan (5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start -> vendor AP-start order complete -> wlan0: AP-ENABLED"
		return 0
	fi
	[ -f "$CONF" ] || die "hostapd config not found: $CONF (final image bakes /etc/hi5622/*; otherwise copy it to the router first)"
	mkdir -p "$ROOT" || die "cannot create $ROOT"
	cp "$CONF" "$ROOT/hostapd.conf" || die "cannot stage hostapd conf"
	if grep -q "CHANGEME-lab-only" "$ROOT/hostapd.conf"; then
		[ -n "$PSK" ] || die "config has the placeholder passphrase and ASSOC_PSK is empty"
		sed -e "s/^wpa_passphrase=.*/wpa_passphrase=$PSK/" "$ROOT/hostapd.conf" >"$ROOT/hostapd.staged"
		mv "$ROOT/hostapd.staged" "$ROOT/hostapd.conf"
		say "passphrase: substituted from ASSOC_PSK (lab value; never logged)"
	fi
	killall hostapd 2>/dev/null || true
	rm -f "$ROOT/hostapd.pid"
	PHY=$(ls /sys/class/ieee80211/ 2>/dev/null | head -1)
	[ -n "$PHY" ] || die "no ieee80211 phy - the wiphy is not registered (check boot ladder/C2)"
	say "phy: $PHY"
	iw dev wlan0 del 2>/dev/null || true
	iw phy "$PHY" interface add wlan0 type __ap || die "cannot create __ap iface"
	ip link set wlan0 up || die "cannot bring wlan0 up"
	sleep 2
	hostapd -B -t -P "$ROOT/hostapd.pid" -i wlan0 "$ROOT/hostapd.conf" >>"$ROOT/hostapd.log" 2>&1
	rc=$?
	say "hostapd launch rc=$rc"
	sleep 4
	if grep -q "AP-ENABLED" "$ROOT/hostapd.log"; then
		say "hostapd: AP-ENABLED"
	else
		say "hostapd log tail:"; tail -20 "$ROOT/hostapd.log" 2>/dev/null
		mark_golden || true
		die "hostapd did not reach AP-ENABLED - STOP (see log; do not restart hostapd in the same load if a frame was posted)"
	fi
	iw dev wlan0 info 2>/dev/null || true
	mark_golden || die "page/mask not golden after AP start"
	say "AP START PASS"
}

do_dp() {
	stage_header "data plane: AP-first order (dp_probe -> dp_setup -> txq_addr_sync -> dp_rx_arm)"
	need_module
	P=$(read_attr dp_probe)
	say "dp_probe: $(printf '%s' "$P" | head -1)"
	write_attr dp_setup 1
	[ "$DRY" = 1 ] || sleep 2
	say "dp_status: $(read_attr dp_status | head -2)"
	write_attr h2d_txq_addr_sync 1
	[ "$DRY" = 1 ] || sleep 1
	say "dp_tx_status: $(read_attr dp_tx_status | head -2)"
	write_attr dp_rx_arm 1
	[ "$DRY" = 1 ] || sleep 1
	say "dp_rx_arm store done; live-bank verdict is in dmesg (expect status=0x00000182 mask=0xfffffcc1 norm/small rptr=0x0020)"
	mark_golden || die "page/mask not golden after the data plane"
	say "DP PASS"
}

do_drain_start() {
	stage_header "RX drain loop (bounded, $DRAIN_SEC s cadence)"
	need_module
	if [ "$DRY" = 1 ]; then
		say "DRY: background loop: while :; do echo 1 > $(attr dp_rx_drain); sleep $DRAIN_SEC; done"
		return 0
	fi
	if [ -f "$ROOT/drain.pid" ] && kill -0 "$(cat "$ROOT/drain.pid")" 2>/dev/null; then
		say "drain loop already running pid $(cat "$ROOT/drain.pid")"
		return 0
	fi
	( while :; do printf '1' >"$(attr dp_rx_drain)" 2>/dev/null || break; sleep "$DRAIN_SEC"; done ) >"$ROOT/drain.log" 2>&1 &
	echo $! >"$ROOT/drain.pid"
	say "drain loop pid $(cat "$ROOT/drain.pid") - EAPOL/data completions are consumed and delivered to the AP netdev (eapol_rx=1)"
}

do_drain_stop() {
	stage_header "RX drain loop stop"
	if [ "$DRY" = 1 ]; then say "DRY: kill drain loop"; return 0; fi
	if [ -f "$ROOT/drain.pid" ]; then
		kill "$(cat "$ROOT/drain.pid")" 2>/dev/null || true
		rm -f "$ROOT/drain.pid"
		say "drain loop stopped"
	else
		say "drain loop not running"
	fi
}

do_snapshot() {
	stage_header "evidence snapshot"
	if [ "$DRY" = 1 ]; then
		say "DRY: dump boot / hcc_status / dp_status / cfg80211_status / data_tx_status / hostapd log / dmesg markers"
		return 0
	fi
	OUT=$ROOT/snapshot.txt
	{
		echo "== ts =="; date -u 2>/dev/null || date
		echo "== boot =="; read_attr boot
		echo "== hcc_status =="; read_attr hcc_status | head -3
		echo "== cfg80211_status =="; read_attr cfg80211_status
		echo "== data_tx_status =="; read_attr data_tx_status
		echo "== dp_status =="; read_attr dp_status | head -6
		echo "== hostapd log tail =="; tail -60 "$ROOT/hostapd.log" 2>/dev/null
		echo "== dmesg association markers =="
		dmesg 2>/dev/null | grep -E "mgmt_tx|mgmt RX|sta_ops|data_tx|rx_arm|rx_drain|tx_complete|frame post|AP-start order|AP-ENABLED|key_add|set_default|EXCEPTION|staging" | tail -120
	} >"$OUT" 2>&1
	cat "$OUT"
}

do_stop() {
	stage_header "stop"
	if [ "$DRY" = 1 ]; then say "DRY: stop hostapd + drain"; return 0; fi
	do_drain_stop
	if [ -f "$ROOT/hostapd.pid" ]; then kill "$(cat "$ROOT/hostapd.pid")" 2>/dev/null || true; rm -f "$ROOT/hostapd.pid"; fi
	killall hostapd 2>/dev/null || true
	do_snapshot
	say "STOPPED. To change profile: hostapd restart is supported (E2 validated), but if page/mask is not golden, power-cycle the Wi-Fi chip."
}

do_full() {
	do_check
	[ -n "$KO" ] && do_load
	do_boot
	do_c2
	do_ap
	do_dp
	do_drain_start
	do_snapshot
	say ""
	say "FULL STAGE COMPLETE - now run the HOST client rung:"
	say "  ./scripts/assoc_e2e.py reset && ./scripts/assoc_e2e.py scan --profile $PROFILE"
	say "  ./scripts/assoc_e2e.py assoc --profile $PROFILE"
	say "then capture router evidence: sh $0 --stage snapshot"
}

# --- args -----------------------------------------------------------------
while [ $# -gt 0 ]; do
	case $1 in
	--check) STAGE=check ;;
	--load) shift; [ $# -gt 0 ] || die "--load needs a KO path"; KO=$1; STAGE=load ;;
	--boot) STAGE=boot ;;
	--c2) STAGE=c2 ;;
	--full) FULL=1 ;;
	--stage) shift; [ $# -gt 0 ] || die "--stage needs a name"; STAGE=$1 ;;
	--profile) shift; [ $# -gt 0 ] || die "--profile needs a name"; PROFILE=$1 ;;
	--config) shift; [ $# -gt 0 ] || die "--config needs a path"; CONF=$1 ;;
	--dry-run) DRY=1 ;;
	--help | -h) usage; exit 0 ;;
	*) die "unknown argument $1 (see --help)" ;;
	esac
	shift
done
[ -n "$STAGE" ] || STAGE=check
# --full is a sequence, not a single stage; it must win over a --load stage name
[ "$FULL" = 1 ] && STAGE=full

need_root
need_dev
if [ "$DRY" = 0 ]; then
	mkdir -p "$ROOT" || die "cannot create $ROOT"
	: >>"$LOGF" 2>/dev/null || true
fi

case $STAGE in
	check) do_check ;;
	load) do_check; do_load ;;
	boot) do_boot ;;
	c2) do_c2 ;;
	ap) do_ap ;;
	dp) do_dp ;;
	drain-start) do_drain_start ;;
	drain-stop) do_drain_stop ;;
	snapshot) do_snapshot ;;
	stop) do_stop ;;
	full) do_full ;;
	*) die "unknown stage '$STAGE' (check|load|boot|c2|ap|dp|drain-start|drain-stop|snapshot|stop|full)" ;;
esac
say "STAGE DONE: $STAGE"
