#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# ladders/fire-5ghz-ap.sh - ON-ROUTER hand-holding for the 5 GHz AP ladder.
#
# HOST-ONLY DELIVERABLE: this lane never runs it.  The LEAD runs it, attended,
# after reading FIRE-RECIPE.md and arming the recovery door.  It is a
# hand-holding script, not an autonomous runner: every device write is one
# explicit command, echo'd before it runs, and it refuses to continue on a
# missing precondition.
#
# Usage (on the router, as root, attended):
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh preflight
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh load
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh boot          # ladder on 0000 ONLY
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh c2
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh ap-open
#   CONFIRM=yes sh /tmp/fire-5ghz-ap.sh ap-wpa2      # fresh load, not a restart
#
# NEVER use endpoint 0001 for any boot-stage action (REPORT sec. 3).
set -eu
STEP=${1:-help}
[ "${CONFIRM:-}" = "yes" ] || { echo "refusing: CONFIRM=yes required (attended fire)" >&2; exit 2; }

D=/sys/bus/pci/devices/0000:00:00.0
S=/sys/bus/pci/devices/0001:00:00.0
REL=$(uname -r)
KO=/lib/modules/$REL/hi5622_wifi.ko
FW=hi_wifi/FIRMWARE.PD4.bin
BLOCK=${WIFI_BLOCK:-/lib/firmware/hi_wifi/wifi_block.bin}
want=1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53

say() { echo "+ $*"; }
run() { say "$*"; "$*"; }
wr() { say "echo $2 > $1/$2"; echo "$2" > "$1/$2"; }

golden() {
	cat "$D/boot" "$D/status"
	echo "ASSERT: page 0x86807FEF / state 1 / mask 0 / staging 0"
}

case "$STEP" in
preflight)
	run uname -r
	[ -f "$KO" ] || { echo "missing $KO" >&2; exit 1; }
	run sha256sum "$KO"
	echo "ASSERT: $want"
	[ -f "/lib/firmware/$FW" ] || { echo "missing /lib/firmware/$FW (push by hand)" >&2; exit 1; }
	[ -f "/lib/firmware/regulatory.db" ] || echo "WARN: no regulatory.db - 5 GHz AP will be NO_IR"
	run iw reg get
	run iw reg set US
	run iw reg get
	run ls -d /sys/class/ieee80211/* 2>/dev/null || true
	golden
	echo "PREFLIGHT: also verify bench link + recovery door + UART capture before load"
	;;
load)
	[ -f "$KO" ] || { echo "missing $KO" >&2; exit 1; }
	run insmod /lib/modules/$REL/cfg80211.ko || true
	say "insmod $KO ... band5g=1"
	insmod "$KO" \
		fw_path=$FW wifi_block_path=$BLOCK \
		allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
		vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
		hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
		rx_enable=1 beacon_static=1 beacon_vap_start=1 \
		wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
		band_vendor=0 scan_band_cfg=0 band5g=1
	echo "ASSERT dmesg: SECONDARY host view ... no wiphy (0001) + ONE-RADIO/TWO-BAND profile (0000)"
	cat /sys/module/hi5622_wifi/parameters/band5g
	;;
boot)
	# EXPERIMENTAL-HYBRID note: the proven image script runs the boot ladder at
	# load; with the manually loaded module use the explicit sequence below.
	for a in prepare upload verify seed hcc_arm hcc_loop release; do
		wr "$D" "$a"
	done
	golden
	echo "ASSERT dmesg: ready handshake decoded + wiphy registered (2 band(s): 2.4GHz + 5GHz)"
	;;
c2)
	wr "$D" h2d_ctx_create
	# the write VALUE selects the grammar (3 = t1); use 3 per the proven recipe
	say "echo 3 > $D/h2d_ctx_create"; echo 3 > "$D/h2d_ctx_create"
	say "echo 1 > $D/h2d_txq_sync72"; echo 1 > "$D/h2d_txq_sync72"
	cat "$D/h2d_status"
	;;
ap-open|ap-wpa2)
	conf=/tmp/hostapd-5ghz-open.conf
	[ "$STEP" = "ap-wpa2" ] && conf=/tmp/hostapd-5ghz-wpa2.conf
	[ -f "$conf" ] || { echo "push $conf first" >&2; exit 1; }
	run ls /sys/class/ieee80211/
	run iw phy phy0 info
	run iw phy phy0 interface add wlan5 type __ap
	run ip link set wlan5 up
	run /usr/sbin/hostapd -dd -t "$conf"
	golden
	echo "ASSERT: ap_chan posted band=1 channel=36; wlan5: AP-ENABLED; page golden/mask 0"
	;;
*)
	echo "steps: preflight load boot c2 ap-open ap-wpa2" >&2
	exit 2
	;;
esac
echo "STEP $STEP done - inspect dmesg + the golden page before the next step"
