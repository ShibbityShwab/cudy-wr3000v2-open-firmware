#!/bin/sh
# collect-stock-dump.sh - READ-ONLY CFE/switch dump collector for the stock vendor boot.
#
# Run ON THE STOCK BOX (telnet root shell). It only READS: /dev/mem via dd, and
# /proc/hsan text files via cat. It writes only into /tmp/cfe/<phase>/ and the
# final tarball; it never writes a register, never loads a module and never
# touches /etc or the flash.
#
# Usage:
#   sh collect-stock-dump.sh <phase> [full|key]
#     phase: p0, p1, p2, p2b, p3, p4, p5, ...
#     full : full MMIO window set + carveout + text state
#     key  : carveout + text state + six key MMIO windows (default)
#
# The traffic phases themselves are driven from the bench host; this script only
# captures state. The companion host analysis script is analyze-cfe-dump.py.

set -u
PHASE=$1
[ -n "$PHASE" ] || { echo "usage: collect-stock-dump.sh <phase> [full|key]" >&2; exit 1; }
MODE=$2
[ -n "$MODE" ] || MODE=key
OUT=/tmp/cfe/$PHASE
mkdir -p "$OUT" || exit 1

[ -r /dev/mem ] || { echo "FATAL: /dev/mem not readable" >&2; exit 2; }
if ! dd if=/dev/mem of=/dev/null bs=4096 skip=524288 count=1 status=none 2>/dev/null; then
	echo "FATAL: carveout (0x80000000) not readable via /dev/mem; report this to the lead" >&2
	exit 3
fi

ddr() { # ddr <bytes> <phys> <out>
	dd if=/dev/mem of="$OUT/$3" bs=4096 skip=$(( $2 / 4096 )) \
	   count=$(( $1 / 4096 )) status=none 2>/dev/null || echo "WARN: dump $3 failed" >&2
}

echo "== collect-stock-dump phase=$PHASE mode=$MODE =="
date -u +%Y-%m-%dT%H:%M:%SZ > "$OUT/time.txt"
uname -a >> "$OUT/time.txt"
cat /proc/uptime >> "$OUT/time.txt"
cat /proc/cmdline > "$OUT/cmdline.txt" 2>/dev/null

# 1. carveout: queue 4 MiB + flow 1 MiB (0x80000000..0x80500000)
ddr $((0x500000)) $((0x80000000)) carveout.bin
# control page outside the carveout (not switch state, useful as a pattern check)
ddr 4096 $((0x80600000)) flashinfo-page.bin

# 2. MMIO windows
if [ "$MODE" = full ]; then
	WINDOWS="0x11000000:dp 0x11080000:ips 0x11100000:fam 0x11180000:pe 0x11200000:eqm 0x11280000:eps 0x11380000:prbs 0x12000000:pfe_glb 0x12080000:pa 0x12100000:ifc 0x12180000:l2 0x12280000:napt 0x12380000:mcc 0x12400000:opp 0x12480000:pub 0x10a70000:pie"
	for w in $WINDOWS; do
		base=${w%%:*}; name=${w##*:}
		ddr $((0x80000)) $((base)) "mmio-$name.bin"
	done
else
	KEY="0x12480000:pub 0x12380000:mcc 0x12100000:ifc 0x12280000:napt 0x12180000:l2 0x11280000:eps"
	for w in $KEY; do
		base=${w%%:*}; name=${w##*:}
		ddr $((0x80000)) $((base)) "mmio-$name.bin"
	done
fi

# 3. text state (procfs + kernel/network views)
for f in /proc/hsan/lsw/pfe/cnt /proc/hsan/lsw/pfe/tocpu_cnt /proc/hsan/lsw/pfe/drop_cnt \
         /proc/hsan/lsw/dp/cnt /proc/hsan/lsw/dp/vport /proc/hsan/lsw/memory/config \
         /proc/hsan/lsw/table/cnt /proc/hsan/lsw/table/glb_attr \
         /proc/hsan/lsw/table/ifc /proc/hsan/lsw/table/ifc_cnt \
         /proc/hsan/lsw/table/l3_cnt /proc/hsan/lsw/table/l3intf \
         /proc/hsan/lsw/table/nhp /proc/hsan/lsw/table/nhp_cnt \
         /proc/hsan/lsw/table/flow_cnt /proc/hsan/lsw/table/ipv4_flow /proc/hsan/lsw/table/ipv6_flow \
         /proc/hsan/lsw/table/tagact /proc/hsan/lsw/table/userll /proc/hsan/lsw/table/userll_ext \
         /proc/hsan/lsw/table/car /proc/hsan/lsw/table/car_profile \
         /proc/hsan/lsw/queue/cnt /proc/hsan/lsw/queue/dev_port \
         /proc/hsan/cfe/table/flow /proc/hsan/cfe/table/mac_stats \
         /proc/hsan/cfe/intf/dump_intf /proc/hsan/cfe/intf/br_dump \
         /proc/hsan/cfe/lrn/dump_lib /proc/hsan/cfe/lrn/lrn_num /proc/hsan/cfe/hook/cnt \
         /proc/hsan/net/pie_cnt /proc/hsan/net/pie_chl_cnt /proc/hsan/net/pie_config \
         /proc/hsan/net/eth0/cnt /proc/hsan/net/eth1/cnt /proc/hsan/net/eth2/cnt /proc/hsan/net/eth3/cnt \
         /proc/hsan/net/swa/rootif_dump /proc/hsan/net/swa/vlan_filter; do
	[ -r "$f" ] || continue
	n=$(echo "$f" | tr '/.' '__')
	cat "$f" > "$OUT/proc$n.txt" 2>/dev/null
done
ip -s link > "$OUT/ip-s-link.txt" 2>&1
brctl showmacs br-lan > "$OUT/brctl-macs.txt" 2>&1 || true
cat /proc/interrupts > "$OUT/interrupts.txt" 2>/dev/null

# 4. manifest
( cd /tmp/cfe && find "$PHASE" -type f | sort | xargs -r sha256sum > "$PHASE.sha256" )
echo "dump complete: /tmp/cfe/$PHASE ($(du -sk /tmp/cfe/$PHASE 2>/dev/null | cut -f1) KiB)"
echo "next: cd /tmp && tar czf /tmp/cfe-$PHASE.tar.gz cfe/$PHASE && sha256sum /tmp/cfe-$PHASE.tar.gz"
echo "transfer to the bench host (nc example): host: nc -l -p 9000 > cfe-$PHASE.tar.gz ; stock: nc 192.168.10.44 9000 < /tmp/cfe-$PHASE.tar.gz"
