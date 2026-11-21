#!/bin/sh
# check-napt-path.sh - READ-ONLY diagnosis on the current baseline image.
# Prints the eth2, IFC, NAPT and L2 counters plus the NAPT config words.
# Run once, then ping the box from the host, then run again and diff:
#   a rising ifc_napt-class counter with up_tocpu staying 0 confirms that unicast
#   frames reach the (uninitialized) NAPT engine and are dropped there.
set -u
DEV=""
for c in /usr/bin/devmem /bin/devmem; do [ -x "$c" ] && DEV="$c" && break; done
[ -n "$DEV" ] || DEV=$(command -v devmem 2>/dev/null || true)
[ -n "$DEV" ] || { echo "no devmem tool" >&2; exit 3; }
rd() { "$DEV" "$1" 32 2>/dev/null | tr -d ' \t\r\n'; }
row() { echo "$1=$2"; }
row uptime_s "$(cut -d. -f1 /proc/uptime 2>/dev/null)"
for f in rx_packets rx_bytes rx_dropped rx_errors; do
  [ -r "/sys/class/net/eth2/statistics/$f" ] && row "eth2_$f" "$(cat /sys/class/net/eth2/statistics/$f)"
done
row pfe_glb_2c "$(rd 0x1200002c)"
row napt_130 "$(rd 0x12280130)"
row napt_64 "$(rd 0x12280064)"
row naptcnt_up_tocpu "$(rd 0x12281010)"
row naptcnt_rx_napt "$(rd 0x12281070)"
row naptcnt_rx_unip "$(rd 0x12281068)"
i=0
while [ "$i" -lt 16 ]; do
  row "ifccnt_$((0x1000 + i*4))" "$(rd $((0x12100000 + 0x1000 + i*4)))"
  i=$((i + 1))
done
row l2cnt_rx_total "$(rd 0x121c0000)"
row l2cnt_tx_total "$(rd 0x121c0004)"
