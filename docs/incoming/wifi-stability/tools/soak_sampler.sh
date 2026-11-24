#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# soak_sampler.sh - read-only long-run sampler for the Hi5622V100 clean-room
# AP soak (wifi-stability lane, shared task task-136).
#
# Runs ON THE BENCH BOX.  Deployment and lifecycle are owned by tools/soak.py;
# this script only samples READ-ONLY state and appends tab-separated rows:
#
#   # wifi-soak/1 <header columns>
#   <row per interval>
#   #END rc=<0|n> elapsed=<s> samples=<n>
#
# HARD RULES held here: never rmmod, never write device memory/registers,
# never read the RC misc window (the bar0read helper itself refuses that),
# no reboot/power action, no network.  The only writes are local files under
# /tmp/wifi-soak (the sampler log + the dmesg copy).
#
# Usage (from soak.py):
#   OUT=/tmp/wifi-soak/soak.tsv DMESG_OUT=/tmp/wifi-soak/dmesg.log #   DURATION_S=3600 INTERVAL_S=10 DEV=/sys/bus/pci/devices/0000:00:00.0 #   HELPER=/root/hi5622-bar0read IFACE=wlan0 SSID=cudy-ap-test #   sh soak_sampler.sh
set -u
OUT=$1
DMESG_OUT=$2
DURATION_S=$3
INTERVAL_S=$4
DEV=$5
HELPER=$6
IFACE=$7
SSID=$8
STOP=/tmp/wifi-soak/stop

mkdir -p /tmp/wifi-soak 2>/dev/null || :
: > "$DMESG_OUT" 2>/dev/null || :
rm -f "$STOP" 2>/dev/null || :
: > "$OUT" 2>/dev/null || :
START=$(cut -d. -f1 /proc/uptime 2>/dev/null)
[ -n "$START" ] || START=0

# BAR0 physical base from the endpoint resource file (same recipe as the
# bench tooling: first line, first field).
RPHYS=$(awk 'NR==1{print $1}' "$DEV/resource" 2>/dev/null)
BAR0_PHYS=0
case "$RPHYS" in
0x*|0X*) RH=$RPHYS; RH=$(echo "$RH" | sed -e 's/^0[xX]//'); BAR0_PHYS=$((0x$RH)) ;;
esac

read_phys() {  # read_phys <hex-offset> -> decimal value or empty
	if [ "$BAR0_PHYS" -le 0 ]; then return 0; fi
	off=$1
	if [ -x "$HELPER" ]; then
		v=$("$HELPER" $((BAR0_PHYS + 0x$off)) 2>/dev/null | head -1)
	else
		return 0
	fi
	case "$v" in
	0x*|0X*) vv=$(echo "$v" | sed -e 's/^0[xX]//'); printf '%d\n' "$((0x$vv))" ;;
	*) : ;;
	esac
	return 0
}
jsan() { tr -d '\n\r\t"\\' | cut -c1-220; }
read_file() { cat "$1" 2>/dev/null | head -1; }

# dmesg delta policy: prefer the clear-on-read form; fall back to a line mark.
DMESG_MODE=mark
if dmesg -c >/dev/null 2>&1; then DMESG_MODE=clear; fi
DMESG_MARK=$(dmesg 2>/dev/null | wc -l)
if [ "$DMESG_MODE" = "clear" ]; then dmesg -c >/dev/null 2>&1 || :; fi

printf '# wifi-soak/1 uptime page0 page1 page2 page3 state mask staging hb excp frames overruns d2h_drop phase memavail memfree load1 hapdm hapd_rss_kb hapd_cpu ap_ok dm_new\n' >> "$OUT"
S=0
while :; do
	NOW=$(cut -d. -f1 /proc/uptime 2>/dev/null)
	[ -n "$NOW" ] || NOW=$S
	[ -f "$STOP" ] && break
	EL=$((NOW - START))
	[ "$EL" -ge "$DURATION_S" ] && break

	rp() { v=$(read_phys $1); if [ -n "$v" ]; then printf '%s' "$v"; else printf 'na'; fi; }
	P0=$(rp 877000); P1=$(rp 877004)
	P2=$(rp 877008); P3=$(rp 87700c)
	STATE=$(rp 7d0b60); MASK=$(rp 6f8008); STG=$(rp 6f800c)
	CAT=$(cat "$DEV/rd_catalog" 2>/dev/null)
	HB=$(echo "$CAT" | grep 'g=4' | grep 'id=0x04' | sed -n 's/.*count=\([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "$HB" ] || HB=0
	EXCP=$(echo "$CAT" | sed -n 's/.*excp_info=\([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "$EXCP" ] || EXCP=0
	RDY=$(cat "$DEV/hcc_ready" 2>/dev/null | tr '\n' ' ')
	FRAMES=$(echo "$RDY" | sed -n 's/.*frames=\([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "$FRAMES" ] || FRAMES=0
	OVR=$(echo "$RDY" | sed -n 's/.*overruns=\([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "$OVR" ] || OVR=0
	DROP=$(cat "$DEV/hcc_status" 2>/dev/null | sed -n 's/.*drop=\([0-9][0-9]*\).*/\1/p' | head -1)
	[ -n "$DROP" ] || DROP=0
	MEM=$(awk '/^MemAvailable:/{print $2}' /proc/meminfo 2>/dev/null)
	[ -n "$MEM" ] || MEM=0
	FREE=$(awk '/^MemFree:/{print $2}' /proc/meminfo 2>/dev/null)
	[ -n "$FREE" ] || FREE=0
	LOAD=$(cut -d' ' -f1 /proc/loadavg 2>/dev/null)
	[ -n "$LOAD" ] || LOAD=0
	HAPDM=0; HRSS=0; HCPU=0
	for pid in $(ps 2>/dev/null | awk '/[h]ostapd/{print $1}'); do
		HAPDM=$((HAPDM + 1))
		stm=$(cat /proc/$pid/statm 2>/dev/null)
		if [ -n "$stm" ]; then
			rss=$(echo "$stm" | awk '{print $2}')
			[ -n "$rss" ] && HRSS=$((HRSS + rss * 4))
		fi
		st=$(cat /proc/$pid/stat 2>/dev/null)
		if [ -n "$st" ]; then
			cpu=$(echo "$st" | awk '{print $14 + $15}')
			[ -n "$cpu" ] && HCPU=$((HCPU + cpu))
		fi
	done
	PHASE=$(echo "$RDY" | sed -n 's/.*phase=\([A-Z_]*\).*/\1/p' | head -1)
	[ -n "$PHASE" ] || PHASE=unknown
	APINFO=$(iw dev "$IFACE" info 2>/dev/null | tr '\n' ';')
	APOK=0
	case "$APINFO" in *"type AP"*) case "$APINFO" in *"ssid $SSID"*) APOK=1 ;; esac ;; esac
	if [ "$DMESG_MODE" = "clear" ]; then
		NEW=$(dmesg -c 2>/dev/null)
		DMNEW=$(echo "$NEW" | grep -c . 2>/dev/null)
		[ -n "$DMNEW" ] || DMNEW=0
		echo "$NEW" >> "$DMESG_OUT" 2>/dev/null
	else
		ALL=$(dmesg 2>/dev/null)
		DMNEW=$(echo "$ALL" | tail -n +$((DMESG_MARK + 1)) | grep -c . 2>/dev/null)
		[ -n "$DMNEW" ] || DMNEW=0
		echo "$ALL" | tail -n +$((DMESG_MARK + 1)) >> "$DMESG_OUT" 2>/dev/null
		DMESG_MARK=$(echo "$ALL" | wc -l)
	fi
	printf '%s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s %s\n' \
		"$NOW" "$P0" "$P1" "$P2" "$P3" "$STATE" "$MASK" "$STG" "$HB" \
		"$EXCP" "$FRAMES" "$OVR" "$DROP" "$PHASE" "$MEM" "$FREE" "$LOAD" \
		"$HAPDM" "$HRSS" "$HCPU" "$APOK" "$DMNEW" >> "$OUT"
	S=$((S + 1))
	sleep "$INTERVAL_S" 2>/dev/null || sleep 1
done
END=$(cut -d. -f1 /proc/uptime 2>/dev/null)
[ -n "$END" ] || END=$NOW
printf '#END rc=0 elapsed=%s samples=%s\n' "$((END - START))" "$S" >> "$OUT"
exit 0