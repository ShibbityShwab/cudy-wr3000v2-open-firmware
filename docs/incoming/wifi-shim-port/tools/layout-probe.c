/* M3 layout probe - compiles against BOTH the 5.10.201 tree (VERSION510) and the
 * built 6.18.60 tree. Emits offsetof() values into a .offs section; tools extract
 * raw 4-byte LE values. Member lists are conservative (present in both generations);
 * members that differ between generations are guarded per-version. */
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/ieee80211.h>
#include <net/cfg80211.h>
#if defined(CONFIG_MAC80211)
#include <net/mac80211.h>
#endif
#include <net/netlink.h>
#include <net/genetlink.h>

#define P(ty, mem) \
  static const unsigned long probe_##ty##_##mem __attribute__((used, section(".offs"))) = offsetof(ty, mem);

typedef struct wiphy wiphy_t;
typedef struct net_device net_device_t;
typedef struct sk_buff sk_buff_t;
typedef struct wireless_dev wireless_dev_t;
typedef struct ieee80211_channel ieee80211_channel_t;
typedef struct ieee80211_supported_band ieee80211_supported_band_t;
typedef struct cfg80211_chan_def cfg80211_chan_def_t;
typedef struct ieee80211_conf ieee80211_conf_t;
typedef struct ieee80211_key_conf ieee80211_key_conf_t;
typedef struct cfg80211_bss cfg80211_bss_t;
typedef struct cfg80211_scan_request cfg80211_scan_request_t;
typedef struct cfg80211_connect_params cfg80211_connect_params_t;
typedef struct device device_t;
typedef struct genl_info genl_info_t;

#ifdef VERSION510
#define SIZEOF(w) \
  static const unsigned long probe_sizeof_##w __attribute__((used, section(".offs"))) = sizeof(w##_t);
SIZEOF(wiphy)
SIZEOF(net_device)
SIZEOF(sk_buff)
SIZEOF(wireless_dev)
SIZEOF(ieee80211_channel)
SIZEOF(ieee80211_supported_band)
SIZEOF(cfg80211_chan_def)
SIZEOF(ieee80211_conf)
SIZEOF(ieee80211_key_conf)
SIZEOF(cfg80211_bss)
SIZEOF(cfg80211_scan_request)
SIZEOF(cfg80211_connect_params)
SIZEOF(device)
SIZEOF(genl_info)
#endif

P(wiphy_t, dev)
P(wiphy_t, priv)
P(wiphy_t, bands)
P(wiphy_t, interface_modes)
P(wiphy_t, flags)
P(wiphy_t, signal_type)
P(wiphy_t, registered)
P(wiphy_t, regulatory_flags)
P(wiphy_t, coverage_class)
P(wiphy_t, max_scan_ssids)
P(wiphy_t, max_sched_scan_ssids)
P(wiphy_t, max_match_sets)
P(wiphy_t, max_scan_ie_len)
P(wiphy_t, max_sched_scan_ie_len)
P(wiphy_t, cipher_suites)
P(wiphy_t, n_cipher_suites)
P(wiphy_t, n_iface_combinations)
P(wiphy_t, iface_combinations)
P(wiphy_t, software_iftypes)
P(wiphy_t, n_addresses)
P(wiphy_t, retry_short)
P(wiphy_t, retry_long)
P(wiphy_t, frag_threshold)
P(wiphy_t, rts_threshold)
P(wiphy_t, max_acl_mac_addrs)
P(wiphy_t, privid)
P(wiphy_t, max_num_pmkids)
P(wiphy_t, max_num_csa_counters)

P(net_device_t, name)
P(net_device_t, dev)
P(net_device_t, state)
P(net_device_t, flags)
P(net_device_t, mtu)
P(net_device_t, type)
P(net_device_t, hard_header_len)
P(net_device_t, needed_headroom)
P(net_device_t, addr_len)
P(net_device_t, dev_addr)
P(net_device_t, broadcast)
P(net_device_t, rx_handler)
P(net_device_t, rx_handler_data)
P(net_device_t, netdev_ops)
P(net_device_t, ethtool_ops)
P(net_device_t, header_ops)
P(net_device_t, watchdog_timeo)
P(net_device_t, real_num_tx_queues)
P(net_device_t, num_tx_queues)
#ifdef VERSION510
P(net_device_t, priv_flags)
#endif
P(net_device_t, ieee80211_ptr)
P(net_device_t, min_mtu)
P(net_device_t, max_mtu)
P(net_device_t, ifindex)
P(net_device_t, neigh_priv_len)
P(net_device_t, ip_ptr)
P(net_device_t, ip6_ptr)
P(net_device_t, vlan_features)
P(net_device_t, hw_features)
P(net_device_t, features)
P(net_device_t, addr_assign_type)
P(net_device_t, needs_free_netdev)
P(net_device_t, gro_flush_timeout)
P(net_device_t, napi_defer_hard_irqs)
P(net_device_t, dev_port)
P(net_device_t, nd_net)

P(sk_buff_t, next)
P(sk_buff_t, prev)
P(sk_buff_t, dev)
P(sk_buff_t, len)
P(sk_buff_t, data_len)
P(sk_buff_t, mac_len)
P(sk_buff_t, hdr_len)
P(sk_buff_t, priority)
P(sk_buff_t, protocol)
P(sk_buff_t, sk)
P(sk_buff_t, head)
P(sk_buff_t, data)
P(sk_buff_t, tail)
P(sk_buff_t, end)
P(sk_buff_t, truesize)
P(sk_buff_t, users)
P(sk_buff_t, network_header)
P(sk_buff_t, transport_header)
P(sk_buff_t, mac_header)
P(sk_buff_t, destructor)
P(sk_buff_t, hash)
P(sk_buff_t, mark)
P(sk_buff_t, queue_mapping)
P(sk_buff_t, vlan_tci)
P(sk_buff_t, vlan_proto)
P(sk_buff_t, skb_iif)

P(wireless_dev_t, wiphy)
P(wireless_dev_t, iftype)
P(wireless_dev_t, list)
P(wireless_dev_t, netdev)
#ifdef VERSION510
P(wireless_dev_t, current_bss)
P(wireless_dev_t, chandef)
P(wireless_dev_t, ssid)
P(wireless_dev_t, ssid_len)
#endif
P(wireless_dev_t, conn)
P(wireless_dev_t, ps)

P(ieee80211_channel_t, band)
P(ieee80211_channel_t, hw_value)
P(ieee80211_channel_t, flags)
P(ieee80211_channel_t, center_freq)
P(ieee80211_channel_t, max_power)
P(ieee80211_channel_t, max_reg_power)
P(ieee80211_channel_t, beacon_found)
P(ieee80211_channel_t, orig_flags)
P(ieee80211_channel_t, orig_mag)
P(ieee80211_channel_t, orig_mpwr)
P(ieee80211_channel_t, max_antenna_gain)

P(ieee80211_supported_band_t, channels)
P(ieee80211_supported_band_t, band)
P(ieee80211_supported_band_t, n_channels)
P(ieee80211_supported_band_t, bitrates)
P(ieee80211_supported_band_t, n_bitrates)
P(ieee80211_supported_band_t, ht_cap)
P(ieee80211_supported_band_t, vht_cap)
P(ieee80211_supported_band_t, n_iftype_data)
P(ieee80211_supported_band_t, iftype_data)

P(cfg80211_chan_def_t, chan)
P(cfg80211_chan_def_t, width)
P(cfg80211_chan_def_t, center_freq1)
P(cfg80211_chan_def_t, center_freq2)



P(cfg80211_bss_t, channel)
P(cfg80211_bss_t, bssid)
P(cfg80211_bss_t, capability)
P(cfg80211_bss_t, beacon_interval)

P(cfg80211_scan_request_t, n_channels)
P(cfg80211_scan_request_t, ie)
P(cfg80211_scan_request_t, ie_len)
P(cfg80211_scan_request_t, n_ssids)
P(cfg80211_scan_request_t, ssids)
P(cfg80211_scan_request_t, wiphy)
P(cfg80211_scan_request_t, wdev)
P(cfg80211_scan_request_t, flags)
P(cfg80211_scan_request_t, rates)

P(cfg80211_connect_params_t, channel)
P(cfg80211_connect_params_t, bssid)
P(cfg80211_connect_params_t, ssid)
P(cfg80211_connect_params_t, ssid_len)
P(cfg80211_connect_params_t, auth_type)
P(cfg80211_connect_params_t, privacy)
P(cfg80211_connect_params_t, key)
P(cfg80211_connect_params_t, key_len)
P(cfg80211_connect_params_t, key_idx)
P(cfg80211_connect_params_t, crypto)

P(device_t, parent)
P(device_t, driver)
P(device_t, of_node)
P(device_t, dma_mask)
P(device_t, coherent_dma_mask)
P(device_t, bus)

P(genl_info_t, snd_seq)
P(genl_info_t, nlhdr)
P(genl_info_t, genlhdr)
P(genl_info_t, attrs)
P(genl_info_t, extack)
