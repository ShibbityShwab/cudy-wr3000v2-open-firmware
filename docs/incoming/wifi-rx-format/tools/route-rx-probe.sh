#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# route-rx-probe.sh - ROUTER-SIDE, READ-ONLY RX-format witness script (lane
# wifi-rx-format, task-163).
#
# RUN THIS ON THE BENCH ROUTER, BY THE LEAD. The host-only lane that authored
# it NEVER executes it against the router. It writes nothing: every stage is
# a read (sysfs file reads, dmesg tail, and the golden-page snapshot only).
#
# The point: `cat dp_rx_probe` gives a decisive one-line verdict for the open
# "MAC RX captures nothing" bug *after the association ladder has done
# AP -> dp_setup -> dp_txq_sync -> dp_rx_arm*:
#   FORMAT-SETUP-MISMATCH ......... config/ring words do not mirror the vendor
#                                   stock dump -> fix setup before capture work
#   DEVICE-WROTE-FRAMES ........... the MAC DID DMA a received frame into host
#                                   memory: the gap is the completion/report
#                                   pointer path, not the entry/header format
#   COMPLETION-ENTRIES-NONZERO ... completion entries exist but no frame header
#   MAC-WROTE-NOTHING ............. rings armed/configured, no received frame
#                                   reached host memory in this load
#
# Stages:
#   --check        read-only preconditions (device dir, attrs, gating params)
#   --probe        dp_rx_probe + dp_status/dp_rx_capture head
#   --snapshot     read-only evidence dump (golden boot page/mask, live bank,
#                  dmesg markers, final probe verdict)
#   --all          check -> probe -> snapshot
#
# Env overrides: RXF_DEV (default /sys/bus/pci/devices/0000:00:00.0)
#                RXF_LOG (default /tmp/rxfmt-router.log)  RXF_DRY_RUN=1
# Safety: fail-closed; no stage writes. If the golden line deviates, STOP and
# power-cycle physically (the SoC reboot does not reset the Wi-Fi chip).
set -u
D=${RXF_DEV:-/sys/bus/pci/devices/0000:00:00.0}
LOG=${RXF_LOG:-/tmp/rxfmt-router.log}
DRY=${RXF_DRY_RUN:-0}
STAGE=
for a in "$@"; do
	case "$a" in
	--check|--probe|--snapshot) STAGE=$a ;;
	--all) STAGE=--all ;;
	-h|--help) sed -n '2,38p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	*) echo "route-rx-probe: unknown argument $a" >&2; exit 2 ;;
	esac
done
[ -n "$STAGE" ] || STAGE=--all
say() { printf '%s\n' "$*"; [ "$DRY" = 1 ] || printf '%s\n' "$*" >>"$LOG" 2>/dev/null; }
die() { say "STOP: $*"; exit 1; }
r() { # r <attr> : read one device attribute (read-only)
	if [ "$DRY" = 1 ]; then say "DRY: cat $D/$1"; return 0; fi
	[ -e "$D/$1" ] || { say "attr $1: (absent)"; return 0; }
	cat "$D/$1" 2>/dev/null || say "attr $1: (read failed)"
}
param() { # param <name> : one module parameter value (read-only)
	if [ "$DRY" = 1 ]; then say "DRY: cat /sys/module/hi5622_wifi/parameters/$1"; return 0; fi
	cat "/sys/module/hi5622_wifi/parameters/$1" 2>/dev/null || say "param $1: (absent)"
}
stage_check() {
	say "== check: $D (read-only)"
	if [ "$DRY" = 0 ] && [ ! -d "$D" ]; then die "PCI device dir missing"; fi
	say "driver  : $(readlink "$D/driver" 2>/dev/null || echo none)"
	for f in boot hcc_status dp_status dp_rings dp_rx_arm dp_rx_drain dp_rx_probe dp_rx_capture; do
		if [ "$DRY" = 1 ]; then say "has $f: (dry-run)"; continue; fi
		[ -e "$D/$f" ] && say "has $f: yes" || say "has $f: NO"
	done
	for p in rx_enable rx_clear_status rx_drain_max rx_refill_wm mgmt_rx mgmt_tx; do
		printf '%s=%s ' "$p" "$(param "$p")"
	done
	say ""
	say "note: run this only after the association ladder did"
	say "      AP -> dp_setup -> dp_txq_sync -> dp_rx_arm in THIS load."
}
stage_probe() {
	say "== probe: dp_rx_probe (read-only RX format / device-write witness)"
	r dp_rx_probe
	say "== probe: dp_status head (read-only)"
	r dp_status
}
stage_snapshot() {
	say "== snapshot: $(date -u '+%Y-%m-%dT%H:%M:%SZ') (read-only)"
	say "-- golden page/mask/staging (expect page 0x86807fef, state CA 0x1, golden:1, staging 0)"
	r boot
	say "-- hcc_status head"
	r hcc_status
	say "-- dmesg markers"
	if [ "$DRY" = 1 ]; then say "DRY: dmesg | grep markers"; else
		dmesg 2>/dev/null | grep -E "hi5622|DP rx|DP setup|DP mac|excp|EXCEPTION|cfg80211: mgmt|sta_ops|data_tx|rx_arm|rx_drain|staging" | tail -120
	fi
	say "-- verdict for the record"
	r dp_rx_probe
}
case "$STAGE" in
--check) stage_check ;;
--probe) stage_probe ;;
--snapshot) stage_snapshot ;;
--all) stage_check; stage_probe; stage_snapshot ;;
esac
say "done: read-only, nothing written. If the golden page/state line deviates:"
say "STOP, do not retry in this load, power-cycle the Wi-Fi chip physically."
