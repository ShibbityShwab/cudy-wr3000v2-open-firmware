#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# perf_probe.sh - read-only Wi-Fi stack metrics probe for the bench box.
#
# Lane: wifi-perf-baseline (shared task task-132).  HOST-ONLY by construction:
# this script only READS /proc, /sys and dmesg.  It performs no device register
# write, no module load/unload, no reboot, no flash and no firmware operation.
# The host harness pipes it to the box through tools/rsh.sh on stdin, so no
# file is written on the device either:
#
#     tools/rsh.sh 'sh -s -- --snapshot' < tools/perf_probe.sh
#
# Output format (stable, parsed by perf_session.py; one item per line):
#   PERF-PROBE v1
#   SNAP-START <n> <uptime_s> <epoch_s>
#   CPU-AGG <user> <nice> <system> <idle> <iowait> <irq> <softirq> <steal>
#   CPU-CORE <cpu> <user> ... <steal>
#   STAT <key> <value>              (/proc/stat aggregate lines, ctxt/intr/...)
#   MEM <key>=<value> ...           (/proc/meminfo selection)
#   LOAD <raw line>
#   MOD <raw /proc/modules line>    (or MOD-ABSENT)
#   PARAM <name>=<value>            (/sys/module/hi5622_wifi/parameters/*)
#   THREAD <pid> <comm> <utime_jiffies> <stime_jiffies>
#   DEV <pci-bdf> <vendor> <device>
#   ATTR-BEGIN <bdf> <name>
#   ... raw attr body ...
#   ATTR-END
#   DMESG-BEGIN
#   ... dmesg tail ...
#   DMESG-END
#   SNAP-END
#   PERF-PROBE-END
#
# Usage:
#   perf_probe.sh [--snapshot] [--watch SECONDS] [--interval SEC]
#                 [--dmesg-lines N] [--help]
# Default: one snapshot (--snapshot).  --watch is a bounded read-only sampler:
# snapshots every --interval seconds for --watch seconds (min 1 s / 1 s).
#
# Busybox/POSIX sh only.  No eval.  Never touches a device CA.
set -u

MODE=snapshot
WATCH_S=0
INTERVAL_S=1
DMESG_LINES=200

usage() {
	cat <<'EOF'
perf_probe.sh - read-only Wi-Fi stack metrics probe (no device writes)

  --snapshot          one snapshot (default)
  --watch SECONDS     sample for SECONDS (bounded; read-only)
  --interval SECONDS  sampling interval for --watch (default 1)
  --dmesg-lines N     dmesg tail length (default 200)
  --help              this text
EOF
}

while [ $# -gt 0 ]; do
	case "$1" in
	--snapshot) MODE=snapshot ;;
	--watch) shift; [ $# -gt 0 ] || { usage >&2; exit 2; }; WATCH_S=$1; MODE=watch ;;
	--interval) shift; [ $# -gt 0 ] || { usage >&2; exit 2; }; INTERVAL_S=$1 ;;
	--dmesg-lines) shift; [ $# -gt 0 ] || { usage >&2; exit 2; }; DMESG_LINES=$1 ;;
	--help|-h) usage; exit 0 ;;
	*) usage >&2; exit 2 ;;
	esac
	shift
done

case "$WATCH_S" in ''|*[!0-9]*) echo "bad --watch" >&2; exit 2 ;; esac
case "$INTERVAL_S" in ''|*[!0-9]*) echo "bad --interval" >&2; exit 2 ;; esac
[ "$INTERVAL_S" -ge 1 ] || INTERVAL_S=1
[ "$DMESG_LINES" -ge 1 ] || DMESG_LINES=1

MODULE=hi5622_wifi
SYS=/sys/module/$MODULE

uptime_s() {
	awk '{print $1}' /proc/uptime 2>/dev/null || echo 0
}

emit_cpu() {
	awk '/^cpu /{printf "CPU-AGG"; for (i=2;i<=9;i++) printf " %s", $i; printf "\n"}' /proc/stat 2>/dev/null
	awk '/^cpu[0-9]+ /{printf "CPU-CORE %s", $1; for (i=2;i<=9;i++) printf " %s", $i; printf "\n"}' /proc/stat 2>/dev/null
	awk '$1=="ctxt"||$1=="intr"||$1=="processes"||$1=="procs_running"||$1=="procs_blocked"{print "STAT", $1, $2}' /proc/stat 2>/dev/null
}

emit_mem() {
	awk '/^(MemTotal|MemFree|MemAvailable|Slab|SReclaimable|SUnreclaim|Buffers|Cached):/{printf "MEM %s=%s\n", $1, $2}' /proc/meminfo 2>/dev/null
	awk '{print "LOAD", $0}' /proc/loadavg 2>/dev/null
}

emit_module() {
	if grep -q "^$MODULE " /proc/modules 2>/dev/null; then
		grep "^$MODULE " /proc/modules
		echo "MOD $MODULE"
	else
		echo "MOD-ABSENT"
	fi
	if [ -d "$SYS/parameters" ]; then
		for p in "$SYS"/parameters/*; do
			[ -e "$p" ] || continue
			printf 'PARAM %s=%s\n' "$(basename "$p")" "$(cat "$p" 2>/dev/null)"
		done
	fi
}

emit_threads() {
	for c in /proc/[0-9]*/comm; do
		[ -e "$c" ] || continue
		read -r nm < "$c" 2>/dev/null || continue
		case "$nm" in
		*hi5622*)
			pid=$(basename "$(dirname "$c")")
			set -- $(cat "/proc/$pid/stat" 2>/dev/null)
			[ $# -ge 15 ] || continue
			echo "THREAD $pid $nm $14 $15"
			;;
		esac
	done
}

emit_devs() {
	for d in /sys/bus/pci/devices/*; do
		[ -e "$d/vendor" ] || continue
		v=$(cat "$d/vendor" 2>/dev/null)
		p=$(cat "$d/device" 2>/dev/null)
		[ "$v" = "0x59e7" ] && [ "$p" = "0x0005" ] || continue
		bdf=$(basename "$d")
		echo "DEV $bdf $v $p"
		for a in status boot dev_status prepare upload verify seed release \
			 hcc_status hcc_ready hcc_events hcc_rings hcc_plan \
			 h2d_status h2d_plan \
			 rd_status rd_catalog rd_decode \
			 cfg80211_status dp_status dp_rings dp_tx_status dp_rx_status; do
			[ -e "$d/$a" ] || continue
			echo "ATTR-BEGIN $bdf $a"
			cat "$d/$a" 2>/dev/null
			echo "ATTR-END"
		done
	done
}

emit_dmesg() {
	echo "DMESG-BEGIN"
	dmesg 2>/dev/null | tail -n "$DMESG_LINES"
	echo "DMESG-END"
}

snapshot() {
	echo "SNAP-START $1 $(uptime_s) $(date +%s 2>/dev/null || echo 0)"
	emit_cpu
	emit_mem
	emit_module
	emit_threads
	emit_devs
	emit_dmesg
	echo "SNAP-END"
}

echo "PERF-PROBE v1"
echo "HOSTNAME $(uname -n 2>/dev/null)"
echo "KERNEL $(uname -r 2>/dev/null)"
echo "MODE $MODE WATCH_S=$WATCH_S INTERVAL_S=$INTERVAL_S DMESG_LINES=$DMESG_LINES"
N=0
if [ "$MODE" = watch ]; then
	DEADLINE=$(( $(date +%s 2>/dev/null || echo 0) + WATCH_S ))
	while :; do
		snapshot "$N"
		N=$((N + 1))
		NOW=$(date +%s 2>/dev/null || echo 0)
		[ "$NOW" -ge "$DEADLINE" ] && break
		sleep "$INTERVAL_S"
	done
else
	snapshot 0
fi
echo "SNAPSHOTS $N"
echo "PERF-PROBE-END"
