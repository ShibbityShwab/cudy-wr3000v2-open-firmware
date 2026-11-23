#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# census.sh - regenerate the REPORT.md sec 2 sender census + init-path
# reachability tables from the LOCAL vendor modules.  Host-only; the modules are
# read at runtime and nothing is copied into the repository.
#
# Environment overrides: PY (python), KO2415, KO2524 (module directories).
set -eu

here=$(CDPATH= cd -- $(dirname -- $0)/.. && pwd)
py=$(printenv PY || true)
[ -n "$py" ] || py=$here/../../../../.provision-venv/bin/python
ko2415=$(printenv KO2415 || true)
[ -n "$ko2415" ] || ko2415=/home/shibbityshwab/recovery-extract/lib/hisilicon/ko
ko2524=$(printenv KO2524 || true)
[ -n "$ko2524" ] || ko2524=/tmp/rootfs2524b/lib/modules/5.10.201
t=$here/tools/ko_xref.py

for spec in \
	"2.4.15-plat:$ko2415/hi5622v100_plat.ko" \
	"2.4.15-wifi:$ko2415/hi5622v100_wifi.ko" \
	"2.5.24-plat:$ko2524/hi5622v100_plat.ko" \
	"2.5.24-wifi:$ko2524/hi5622v100_wifi.ko"
do
	name=${spec%%:*}
	ko=${spec#*:}
	[ -f "$ko" ] || { echo "SKIP $name: not found ($ko)" >&2; continue; }
	echo "== $name =="
	"$py" "$t" "$ko" stats
	echo "-- H2D senders --"
	"$py" "$t" "$ko" senders
	echo
done

echo "== init-path reachability (2.4.15 wifi: only the post-ready config sync) =="
"$py" "$t" "$ko2415/hi5622v100_wifi.ko" reach \
	'init_module,host_module_init,hal_main_init,hmac_main_init,wal_main_init,hdpp_main_init,hmac_board_init' \
	'hdpp_config_send_event,hmac_config_send_event,hmac_config_alg_send_event,hmac_send_msg'
echo "== init-path reachability (2.4.15 plat: NO PATH) =="
"$py" "$t" "$ko2415/hi5622v100_plat.ko" reach \
	'init_module,hcc_init,hcc_start,bal_init,pcie_main_init,pcie_msg_init,pcie_comm_init,multi_chip_loading,wlan_power_on' \
	'hcc_timer_timeout_proc,hcc_xfer_done,pcie_ete_rcv_buff_check,shuangta_ete_sr_dscr_fill,exception_handler'

