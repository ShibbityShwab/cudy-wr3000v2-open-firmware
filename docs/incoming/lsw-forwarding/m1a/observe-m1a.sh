#!/bin/sh
# observe-m1a.sh - READ-ONLY snapshot for the M1a probe session
# (opensource/docs/incoming/lsw-forwarding/m1a).
#
# Prints key=value lines: counters, decoded L2 DFX counter words, the tag action
# entry, pfe_glb attribute, mirror state. It performs only devmem READ forms.
# Usage: sh observe-m1a.sh > /tmp/m1a-snapshot.txt
set -u
DEV=""
for c in /usr/bin/devmem /bin/devmem; do [ -x "$c" ] && DEV="$c" && break; done
[ -n "$DEV" ] || DEV=$(command -v devmem 2>/dev/null || true)
[ -n "$DEV" ] || { echo "no devmem tool" >&2; exit 3; }
rd() { "$DEV" "$1" 32 2>/dev/null | tr -d ' \t\r\n'; }
row() { echo "$1=$2"; }
row uptime_s "$(cut -d. -f1 /proc/uptime 2>/dev/null)"
row cmdline_mirror "$(grep -o 'luofu_switch.cpu_mirror=[01]' /proc/cmdline 2>/dev/null)"
row cmdline_accepterr "$(grep -o 'luofu_pie.accepterr=[01]' /proc/cmdline 2>/dev/null)"
for f in rx_packets rx_bytes rx_dropped rx_errors tx_packets tx_bytes; do
  [ -r "/sys/class/net/eth2/statistics/$f" ] && row "eth2_$f" "$(cat /sys/class/net/eth2/statistics/$f)"
done
row br_lan_state "$(cat /sys/class/net/br-lan/operstate 2>/dev/null)"
# tag action table entries 0..2 (readable 16-byte entries, physical 0x124d4000+)
idx=0; while [ "$idx" -le 2 ]; do w=0; while [ "$w" -le 3 ]; do
  row "tagact_${idx}_w${w}" "$(rd $((0x124d4000 + idx*16 + w*4)))"; w=$((w+1)); done
  idx=$((idx+1)); done
row pfe_glb_2c "$(rd 0x1200002c)"
# L2 DFX counters, first named group (vendor hi_pfe_dfx_dump_cnt decode)
row l2_rx_total "$(rd 0x121c0000)"
row l2_tx_total "$(rd 0x121c0004)"
row l2_rx_drop "$(rd 0x121c0008)"
row l2_port_move "$(rd 0x121c000c)"
row l2_age_drop "$(rd 0x121c0010)"
row l2_sport_clash "$(rd 0x121c0014)"
row l2_search_ok_mc "$(rd 0x121c0018)"
row l2_search_fail_mc "$(rd 0x121c001c)"
row l2_tx_drop "$(rd 0x121c003c)"
# mirror fallback state (W19/W20 registers)
row mirror_d8 "$(rd 0x124000d8)"
row mirror_dc "$(rd 0x124000dc)"
row mirror_c0 "$(rd 0x124000c0)"
row dmesg_oops "$(dmesg 2>/dev/null | grep -c -E 'Oops|BUG:|panic' 2>/dev/null || true)"
row dmesg_tail "$(dmesg 2>/dev/null | tail -3 | tr '\n' '|' 2>/dev/null || true)"
