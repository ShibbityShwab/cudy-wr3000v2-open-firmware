#!/bin/bash
set -uo pipefail
: "${ROUTER_PASSWORD:?set ROUTER_PASSWORD to the router root password}"
HOST="${ROUTER_HOST:-root@192.168.10.1}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="${1:-$SCRIPT_DIR/../cal-snapshots/$(date -u +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT"
SSH_OPTS=(-o PubkeyAuthentication=no -o PreferredAuthentications=password -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10)
export SSH_ASKPASS=/tmp/askpass-wr3000.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0
printf '#!/bin/sh\nprintf %%s "$ROUTER_PASSWORD"\n' > "$SSH_ASKPASS"
chmod +x "$SSH_ASKPASS"
run() { ssh "${SSH_OPTS[@]}" "$HOST" "$1" 2>/dev/null; }

{
	echo "== captured $(date -u +%Y-%m-%dT%H:%M:%SZ) =="
	run "uname -a; cat /etc/custom-firmware-version 2>/dev/null; cat /sys/class/ubi/ubi0/mtd_num"
	echo "-- chip identity --"
	run "iwpriv Hisilicon0 get_chipid; iwpriv Hisilicon0 get_dieid; iwpriv Hisilicon0 get_mode; iwpriv Hisilicon0 get_channel"
} > "$OUT/identity.txt"

ALG_CMDS="get_2g_power_param get_5g_power_param get_2g_low_power_param get_5g_low_power_param \
get_2g_all_curve_param get_5g_all_curve_param get_xo_ppm_cali_param get_xo_ducy_cali_param \
get_rssi_param get_polynomial_params get_cfg_param get_edca_param get_wme_params get_wmm_params \
get_scan_param get_tx_param get_aie_param get_ar_dev_param get_stru_param get_sub_param \
get_whitelist_param get_ns_aggr_param get_ns_rate_param get_wps_param"
: > "$OUT/alg-values.txt"
for c in $ALG_CMDS; do
	printf '=== %s ===\n' "$c" >> "$OUT/alg-values.txt"
	run "timeout 5 iwpriv Hisilicon0 alg $c" >> "$OUT/alg-values.txt"
done

run "for f in /usr/local/factory/wifi.cal /usr/local/factory/hi5622v100.cal /usr/local/factory/wifi_cali_data.kv /usr/local/factory/wifi_cali_data_2g.kv; do echo \"== \$f\"; cat \$f 2>/dev/null; echo; done" > "$OUT/factory-cal.txt"
run "for p in /sys/module/hi5622v100_wifi/parameters/*; do echo \"== \$p\"; cat \$p; done 2>/dev/null" > "$OUT/module-params.txt"
run "uci show wireless 2>/dev/null | head -60" > "$OUT/wireless-config.txt"

for f in /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini /lib/firmware/hi_wifi/cfg_device_hisi.ini; do
	scp -O "${SSH_OPTS[@]}" "$HOST:$f" "$OUT/$(basename "$f")" >/dev/null 2>&1 && echo "pulled $(basename "$f")"
done

( cd "$OUT" && sha256sum * > MANIFEST.sha256 )
echo "snapshot dir: $OUT"
cat "$OUT/MANIFEST.sha256"
rm -f "$SSH_ASKPASS"
