# M3b/M3d: member-access census - verified summary

Machine census: tools/m3-member-scan.py (hint-gated, upper bound). Manual
verification: tools/m3-verify.py windows + hand reading (this file).

## Census totals (distinct struct.member hits)

| module | KERNEL-owned hits | of which drifted | VENDOR-owned hits |
| --- | --- | --- | --- |
| hi5622v100_wifi.ko | 28 | 21 | 37 |
| hi5622v100_plat.ko | 15 | 4 | 0 |
| cfg80211.ko | 30 | 20 | 64 |

VENDOR-owned = wiphy / wireless_dev / ieee80211_channel / cfg80211_chan_def /
supported_band / scan_request / connect_params / cfg80211_bss. These are
CONSISTENT between driver and vendor cfg80211.ko (both built against the vendor
5.10 tree) -> safe on the vendor-stack path, and they are exactly what would
break on a 6.18-native cfg80211 path (wiphy.priv -64, wiphy.registered -136,
wireless_dev.conn -56 ...).

## VERIFIED-REAL drifted KERNEL-owned accesses (disassembly-confirmed)

Pointer identity confirmed by hand in tools/m3-verify.py windows.

hi5622v100_wifi.ko (driver):
- net_device.state   0x28 -> 0x88   wal_netdev_set_mac_addr  ldr r2,[r0,#40]
                                     (ndo_set_mac_address path, live on ifconfig/mac change)
- net_device.mtu     0x148 -> 0x30   oal_net_device_change_mtu str r1,[r3,#328]
- sk_buff.tail       0x94 -> 0x9c    hmac_update_fbt_scan_result (rx path, skb walk)
- sk_buff.end        0x98 -> 0xa0    hmac_update_fbt_scan_result
- sk_buff.head       0x9c -> 0xa4    hmac_update_fbt_scan_result
- sk_buff.mac_header 0x92 -> 0x9a    hmac_update_fbt_scan_result
- sk_buff.users      0xa8 -> 0xb0    oal_cfg80211_external_auth_request ldr r0,[r0,#168]
- sk_buff.mark       0x80 -> 0x88    hmac_config_send_meas_req (tx build, 0x5c-0x80 field block)
- sk_buff.vlan_proto 0x78 -> 0x80    hmac_config_send_meas_req
- sk_buff.skb_iif    0x70 -> 0x78    hmac_config_send_meas_req
- unprobed accesses on ndev: +0x328 (wal_netdev_set_mac_addr) - additional drift surface

hi5622v100_plat.ko:
- sk_buff.head/data/truesize/vlan_tci (oal_mem_sdt_netbuf_alloc, hwifi_config_init)
  netbuf alloc/free path reads 5.10 sk_buff fields.

cfg80211.ko (vendor stack - the killer):
- genl_info.genlhdr  0x0c -> 0x10   get_wiphy_regdom: strh r0,[r1,#12..46]
  = WRITES to genl_info+0x0c..0x2e. On 6.18 those offsets are inside the
  nlhdr/genlhdr POINTER fields -> memory corruption on the FIRST nl80211 command.
- genl_info.nlhdr    0x08 -> 0x0c   27 functions (nl80211 doit/dumpit bodies)
- genl_info.extack   0x20 -> 0x4c   46 functions
- sk_buff.head       0x9c -> 0xa4   nl80211_del_key ldr r3,[r0,#156]
- sk_buff.tail       0x94 -> 0x9c   cfg80211_disconnected / nl80211_trigger_scan
- sk_buff.users      0xa8 -> 0xb0   59 functions
- net_device.state   0x28 -> 0x88   cfg80211_conn_work ldr r3,[r3,#40]
- net_device.min_mtu 0x14c -> 0x170 nl80211_new_interface / nl80211_set_interface

## Known false-positive class (census over-counts)

net_device.header_ops 0x134 "17 funcs" is mostly priv-struct coincidence:
hmac_ap_up_rx_mgmt's [r2,#308] access is on a driver-private pointer, not a
net_device (verified). The census is an upper bound; the verified table above
is the floor, and the floor alone is fatal.

## M3c: vtable order (function-pointer tables consumed by the 6.18 core)

net_device_ops: 5.10=80 members, 6.18=92. 6.18 inserted 4 members at slots
12-15 (ndo_eth_ioctl, ndo_siocbond, ndo_siwandev, ndo_siocdevprivate) shifting
all later slots +4, plus 9 more insertions/removals later on.

Driver's static table (wifi.ko .data+0x1b28): populated slots {2=ndo_open
(wal_netdev_open_ext), 3=ndo_stop, 4=ndo_start_xmit (wal_vap_start_xmit),
9=ndo_set_mac_address, 16=ndo_get_stats64, 19=ndo_get_stats}.

6.18 mapping of those slots: 0-11 IDENTICAL (open/stop/start_xmit/set_mac all
line up); get_stats64 (6.18 slot 20) and get_stats (6.18 slot 23) land on NULL
table entries -> core falls back to embedded stats, NO crash, stats64 lost.
=> netdev_ops vtable drift is PARTIAL and not fatal for THIS driver.

ethtool_ops: 5.10=65 members, 6.18=86, reordered (get_strings 21->79,
get_sset_count 28->78, ...). Driver registers no ethtool_ops table (none found
in .data) -> generic ethtool path, not fatal.

genl_ops (cfg80211.ko nl80211 family): first 7 members {cmd, flags, policy,
doit, dumpit, done, parallel_ops} have the same order in both -> family
registration would succeed; the .doit BODIES then break on genl_info layout
(above). Fatal.
