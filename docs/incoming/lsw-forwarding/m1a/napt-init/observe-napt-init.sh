#!/bin/sh
# observe-napt-init.sh - READ-ONLY snapshot for the NAPT init prototype.
# Prints key=value lines; performs only devmem read forms.
set -u
DEV=""
for c in /usr/bin/devmem /bin/devmem; do [ -x "$c" ] && DEV="$c" && break; done
[ -n "$DEV" ] || DEV=$(command -v devmem 2>/dev/null || true)
[ -n "$DEV" ] || { echo "no devmem tool" >&2; exit 3; }
rd() { "$DEV" "$1" 32 2>/dev/null | tr -d ' \t\r\n'; }
row() { echo "$1=$2"; }
row uptime_s "$(cut -d. -f1 /proc/uptime 2>/dev/null)"
row cmdline_mirror "$(grep -o 'luofu_switch.cpu_mirror=[01]' /proc/cmdline 2>/dev/null)"
row cmdline_accept "$(grep -o 'luofu_pie.accepterr=[01]' /proc/cmdline 2>/dev/null)"
for f in rx_packets rx_bytes rx_dropped rx_errors tx_packets; do
  [ -r "/sys/class/net/eth2/statistics/$f" ] && row "eth2_$f" "$(cat /sys/class/net/eth2/statistics/$f)"
done
# NAPT control/config words
for off in 0x50 0x54 0x60 0x64 0x68 0x70 0x130 0x134 0x138 0x13c; do
  row "napt_$off" "$(rd $((0x12280000 + off)))"
done
row pfe_glb_2c "$(rd 0x1200002c)"
# NAPT counters (decoded names)
for pair in "1000 tx_total" "1004 dn_drop" "1008 up_drop" "100c dn_tocpu" "1010 up_tocpu" \
            "1060 rx_total" "1068 rx_unip" "106c rx_tocpu" "1070 rx_napt" "1074 rx_mc" \
            "1088 mc_fail_tocpu"; do
  set -- $pair
  row "naptcnt_$2" "$(rd $((0x12280000 + 0x$1)))"
done
# IFC counter block (names on stock: rx_total/ifc_hit/ifc_tocpu/ifc_napt among others)
for off in 1000 1004 1008 100c 1010 1014 1018 101c 1020 1024 1028 102c 1030 1034 1038 103c; do
  row "ifccnt_$off" "$(rd $((0x12100000 + 0x$off)))"
done
# L2 counters (first group)
for pair in "0 rx_total" "4 tx_total" "8 rx_drop" "c port_move" "10 age_drop" "18 search_ok_mc" "1c search_fail_mc"; do
  set -- $pair
  row "l2cnt_$2" "$(rd $((0x121c0000 + 0x$1)))"
done
# mirror state
row mirror_d8 "$(rd 0x124000d8)"
row mirror_dc "$(rd 0x124000dc)"
row mirror_c0 "$(rd 0x124000c0)"
row dmesg_oops "$(dmesg 2>/dev/null | grep -c -E 'Oops|BUG:|panic' 2>/dev/null || true)"
row dmesg_tail "$(dmesg 2>/dev/null | tail -3 | tr '\n' '|' 2>/dev/null || true)"
