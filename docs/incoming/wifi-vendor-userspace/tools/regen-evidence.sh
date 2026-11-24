#!/usr/bin/env bash
# regen-evidence.sh — regenerate the evidence lists behind
# opensource/docs/incoming/wifi-vendor-userspace/TABLES.md
#
# Usage:  tools/regen-evidence.sh [/path/to/extracted/rootfs] [second-rootfs-for-delta]
# Default rootfs: /tmp/rootfs2524b (Cudy WR3000 v2.0 stock 2.5.24 extraction).
#
# This script prints ONLY file names, sizes, hashes, symbol names and grep hits from the
# LOCAL extracted rootfs. It never copies binary content and must not be used to publish
# vendor bytes. Local evidence only; host-only (no device access).

set -uo pipefail

ROOTFS=/tmp/rootfs2524b
ROOTFS2=
if [ $# -ge 1 ]; then ROOTFS=$1; fi
if [ $# -ge 2 ]; then ROOTFS2=$2; fi

if [ ! -d "$ROOTFS" ]; then
	echo "usage: $0 <extracted-rootfs-dir> [second-rootfs-dir]" >&2
	exit 2
fi

say() { printf '\n== %s ==\n' "$1"; }
have() { command -v "$1" >/dev/null 2>&1; }

say "T1: version pins"
[ -f "$ROOTFS/etc/rom_version" ] && cat "$ROOTFS/etc/rom_version"
[ -f "$ROOTFS/etc/hi_version" ] && sed -n '1,12p' "$ROOTFS/etc/hi_version"

say "T1: firmware artifacts (sizes + hashes only)"
FW=$ROOTFS/lib/firmware/hi_wifi
for f in FIRMWARE.bin cfg_device_hisi.ini cfg_hi5622v100_hisi.ini; do
	[ -f "$FW/$f" ] && { stat -c '%n %s B' "$FW/$f"; md5sum "$FW/$f"; }
done
[ -f "$FW/cfg_device_hisi.ini" ] && grep -E 'firmware_(itcm|dtcm|plat_custom|wifi_custom)' "$FW/cfg_device_hisi.ini"

say "T1: Wi-Fi module pins (2.5.24 extraction)"
for f in "$ROOTFS"/lib/modules/*/hi5622v100_*.ko; do
	[ -f "$f" ] && { stat -c '%n %s B' "$f"; md5sum "$f"; }
done
if have readelf; then
	for f in "$ROOTFS"/lib/modules/*/hi5622v100_*.ko; do
		[ -f "$f" ] || continue
		printf -- '-- %s modinfo tail --\n' "$f"
		readelf -p .modinfo "$f" 2>/dev/null | grep -E 'vermagic|license|parmtype' | head -8
	done
fi

say "T1: module packaging delta"
printf 'lib/modules/*.ko count: '; ls "$ROOTFS"/lib/modules/*/*.ko 2>/dev/null | wc -l
printf 'lib/hisilicon/ko count: '; ls "$ROOTFS"/lib/hisilicon/ko 2>/dev/null | wc -l
ls "$ROOTFS"/lib/hisilicon/ko 2>/dev/null | grep -E 'hi5622v100' || echo '(no hi5622v100_* in lib/hisilicon/ko)'

say "T1: script hashes and 2.4.15 delta (if second rootfs given)"
for f in usr/bin/wifi_init.sh usr/bin/hi5622v100_cal_init.sh etc/init.d/wpad etc/init.d/softapd \
         lib/netifd/wireless/mac80211.sh lib/netifd/hostapd.sh etc/config/wireless sbin/wifi; do
	[ -f "$ROOTFS/$f" ] || continue
	if [ -n "$ROOTFS2" ] && [ -f "$ROOTFS2/$f" ]; then
		a=$(sha256sum "$ROOTFS/$f" | cut -c1-16)
		b=$(sha256sum "$ROOTFS2/$f" | cut -c1-16)
		if [ "$a" = "$b" ]; then v=SAME; else v=DIFF; fi
		printf '%-42s %s %s %s\n' "$f" "$a" "$b" "$v"
	else
		printf '%-42s %s\n' "$f" "$(sha256sum "$ROOTFS/$f" | cut -c1-16)"
	fi
done

say "T2: init chain / Wi-Fi loaders"
for f in etc/init.d/hsan_start etc/init.d/hsan_prepare etc/init.d/hsan_appm \
         etc/init.d/wpad etc/init.d/softapd etc/init.d/hyfibrd etc/init.d/hcsh; do
	[ -f "$ROOTFS/$f" ] || continue
	printf -- '-- %s --\n' "$f"
	grep -nE 'START=|STOP=|wifi_init|cal_init|hi_app|hostapd|wpa_supplicant|softapd|hyfibrd|PROG=' "$ROOTFS/$f" | head -12
done
printf -- '-- rc.d Wi-Fi/related entries --\n'
ls "$ROOTFS"/etc/rc.d 2>/dev/null | grep -E 'hsan|wpad|softapd|network|hyfi|hcsh|cmagent|cmsd'

say "T3/T4: /sys/hisys/hipriv call sites (text files only)"
grep -rn 'hisys/hipriv' "$ROOTFS/etc" "$ROOTFS/lib" "$ROOTFS/sbin" "$ROOTFS/usr/bin" "$ROOTFS/usr/sbin" 2>/dev/null | head -80

say "T5: iwpriv call sites (text files only)"
grep -rn 'iwpriv' "$ROOTFS/etc" "$ROOTFS/lib" "$ROOTFS/sbin" "$ROOTFS/usr/bin" 2>/dev/null | head -60

say "T3: interface inventory (netifd / privileged helpers / libs)"
for f in lib/netifd/wireless/mac80211.sh lib/netifd/hostapd.sh sbin/wifi; do
	[ -f "$ROOTFS/$f" ] || continue
	printf -- '-- %s (vendor markers) --\n' "$f"
	grep -nE 'hipriv|ubus call hostapd|ubus call wpa_supplicant|iwpriv|hi_cfm|MAC_OFFSET|VAP_2G_NUM|create_mld|drv_setup_vif' "$ROOTFS/$f" | head -40
done
printf -- '-- binaries: dynamic deps (names only) --\n'
for f in usr/sbin/wpad usr/sbin/softapd usr/sbin/hcshd usr/bin/iwinfo usr/bin/hi_appm usr/lib/libhi_owal_ssf.so; do
	[ -f "$ROOTFS/$f" ] || continue
	printf '%s: ' "$f"
	if have readelf; then readelf -d "$ROOTFS/$f" 2>/dev/null | grep NEEDED | sed 's/.*\[//;s/\].*//' | tr '\n' ' '; fi
	echo
done

say "T3/T5: driver symbol surfaces (names/counts only)"
KO=
for f in "$ROOTFS"/lib/modules/*/hi5622v100_wifi.ko; do [ -f "$f" ] && KO=$f; done
PLAT=
for f in "$ROOTFS"/lib/modules/*/hi5622v100_plat.ko; do [ -f "$f" ] && PLAT=$f; done
if [ -n "$KO" ] && have readelf; then
	printf 'wal_hipriv_* unique symbols: '; readelf -sW "$KO" 2>/dev/null | grep -oE 'wal_hipriv_[A-Za-z0-9_]+' | sort -u | wc -l
	printf 'wal_cfg80211_* unique symbols: '; readelf -sW "$KO" 2>/dev/null | grep -oE 'wal_cfg80211_[A-Za-z0-9_]+' | sort -u | wc -l
	printf 'dev_netlink_* symbols: '; readelf -sW "$KO" 2>/dev/null | grep -oE 'dev_netlink_[A-Za-z0-9_]+' | sort -u | tr '\n' ' '; echo
	printf 'g_st_nl_hiwifi* symbols: '; readelf -sW "$KO" 2>/dev/null | grep -oE 'g_st_nl_hiwifi[A-Za-z0-9_]*' | sort -u | tr '\n' ' '; echo
	printf 'vendor netlink (dev_netlink_*/oam_netlink_*) symbols: '; readelf -sW "$KO" "$PLAT" 2>/dev/null | grep -oE '(dev_netlink_[A-Za-z0-9_]+|oam_netlink_[a-z_]+)' | sort -u | tr '\n' ' '; echo
	printf 'HCC builder symbols present: '
	readelf -sW "$KO" 2>/dev/null | grep -oE '(hmac_config_send_event|hmac_config_alg_send_event|hdpp_config_send_event|hmac_user_add|hmac_del_user_notify_dmac|hmac_enc_scan_send_req|hmac_sta_wait_join|hmac_sta_sync_join_req_params|wal_netdev_set_mac_addr)' | sort -u | tr '\n' ' '; echo
	printf 'imported cfg80211/wiphy/ieee80211 symbols: '; readelf -sW "$KO" 2>/dev/null | grep -E 'UND' | grep -oE '(cfg80211_[a-z_]+|wiphy_[a-z_]+|ieee80211_[a-z_]+)' | sort -u | wc -l
fi
if [ -n "$PLAT" ] && have readelf; then
	printf 'plat netlink/sysfs imports: '
	readelf -sW "$PLAT" 2>/dev/null | grep -E 'UND' | grep -oE '(netlink_[a-z_]+|kobject_[a-z_]+|sysfs_[a-z_]+)' | sort -u | tr '\n' ' '; echo
fi

say "T3: hipriv command vocabulary seen in driver strings (flags + names only)"
if [ -n "$KO" ] && have strings; then
	strings -n 3 "$KO" | grep -xE '(create|destroy|wds_vap_mode|sae_pwe|isolation_mode|isolation_fwd|isolation_type|11k_switch|11v_cfg_bsst|m2u_snoop_on|add_country_ie_switch|set_regdomain_pwr|radartool|kick_user|usernum|bintval|dtim_period|resp_hide|set_ie|stat|dump_hdpp|tx_drop_detail|hipriv|hisys|create_mld|delete_mld)' | sort -u | tr '\n' ' '; echo
fi

say "T4/T6: calibration chain"
for f in usr/bin/hi5622v100_cal_init.sh usr/bin/hi5622v100_cal_get.sh; do
	[ -f "$ROOTFS/$f" ] || continue
	printf -- '-- %s alg commands --\n' "$f"
	grep -nE 'iwpriv|alg ' "$ROOTFS/$f" | head -20
done

say "T6/T7: prior-lane evidence pointers (committed docs; no local data)"
echo 'wifi-hcc-re / wifi-d2h-direct / wifi-h2d-catalog / wifi-driver-re / wifi-hcc-service'
echo 'phase6/phase7 (hipriv on-device), phase8 (cali container), phase16 (module load), phase19/phase49 (runtime health)'

say "done"
echo "NOTE: local evidence only. Print only names/sizes/hashes/symbol names;"
echo "do not paste vendor file content into tracked text."
