# M3c vtable order drift

```
== net_device_ops: 5.10 has 80 members, 6.18 has 92 ==
  slot  12 ndo_set_config                     moved to 6.18 slot  16
  slot  13 ndo_change_mtu                     moved to 6.18 slot  17
  slot  14 ndo_neigh_setup                    moved to 6.18 slot  18
  slot  15 ndo_tx_timeout                     moved to 6.18 slot  19
  slot  16 ndo_get_stats64                    moved to 6.18 slot  20
  slot  17 ndo_has_offload_stats              moved to 6.18 slot  21
  slot  18 ndo_get_offload_stats              moved to 6.18 slot  22
  slot  19 ndo_get_stats                      moved to 6.18 slot  23
  slot  20 ndo_vlan_rx_add_vid                moved to 6.18 slot  24
  slot  21 ndo_vlan_rx_kill_vid               moved to 6.18 slot  25
  slot  22 ndo_poll_controller                moved to 6.18 slot  26
  slot  23 ndo_netpoll_setup                  moved to 6.18 slot  27
  slot  24 ndo_netpoll_cleanup                moved to 6.18 slot  28
  slot  25 ndo_set_vf_mac                     moved to 6.18 slot  29
  slot  26 ndo_set_vf_vlan                    moved to 6.18 slot  30
  slot  27 ndo_set_vf_rate                    moved to 6.18 slot  31
  slot  28 ndo_set_vf_spoofchk                moved to 6.18 slot  32
  slot  29 ndo_set_vf_trust                   moved to 6.18 slot  33
  slot  30 ndo_get_vf_config                  moved to 6.18 slot  34
  slot  31 ndo_set_vf_link_state              moved to 6.18 slot  35
  slot  32 ndo_get_vf_stats                   moved to 6.18 slot  36
  slot  33 ndo_set_vf_port                    moved to 6.18 slot  37
  slot  34 ndo_get_vf_port                    moved to 6.18 slot  38
  slot  35 ndo_get_vf_guid                    moved to 6.18 slot  39
  slot  36 ndo_set_vf_guid                    moved to 6.18 slot  40
  slot  37 ndo_set_vf_rss_query_en            moved to 6.18 slot  41
  slot  38 ndo_setup_tc                       moved to 6.18 slot  42
  slot  39 ndo_fcoe_enable                    moved to 6.18 slot  43
  slot  40 ndo_fcoe_disable                   moved to 6.18 slot  44
  slot  41 ndo_fcoe_ddp_setup                 moved to 6.18 slot  45
  slot  42 ndo_fcoe_ddp_done                  moved to 6.18 slot  46
  slot  43 ndo_fcoe_ddp_target                moved to 6.18 slot  47
  slot  44 ndo_fcoe_get_hbainfo               moved to 6.18 slot  48
  slot  45 ndo_fcoe_get_wwn                   moved to 6.18 slot  49
  slot  46 ndo_rx_flow_steer                  moved to 6.18 slot  50
  slot  47 ndo_add_slave                      moved to 6.18 slot  51
  slot  48 ndo_del_slave                      moved to 6.18 slot  52
  slot  49 ndo_get_xmit_slave                 moved to 6.18 slot  53
  slot  50 ndo_fix_features                   moved to 6.18 slot  55
  slot  51 ndo_set_features                   moved to 6.18 slot  56
  slot  52 ndo_neigh_construct                moved to 6.18 slot  57
  slot  53 ndo_neigh_destroy                  moved to 6.18 slot  58
  slot  54 ndo_fdb_add                        moved to 6.18 slot  59
  slot  55 ndo_fdb_del                        moved to 6.18 slot  60
  slot  56 ndo_fdb_dump                       moved to 6.18 slot  62
  slot  57 ndo_fdb_get                        moved to 6.18 slot  63
  slot  58 ndo_bridge_setlink                 moved to 6.18 slot  69
  slot  59 ndo_bridge_getlink                 moved to 6.18 slot  70
  slot  60 ndo_bridge_dellink                 moved to 6.18 slot  71
  slot  61 ndo_change_carrier                 moved to 6.18 slot  72
  slot  62 ndo_get_phys_port_id               moved to 6.18 slot  73
  slot  63 ndo_get_port_parent_id             moved to 6.18 slot  74
  slot  64 ndo_get_phys_port_name             moved to 6.18 slot  75
  slot  65 ndo_udp_tunnel_add                 REMOVED in 6.18
  slot  66 ndo_udp_tunnel_del                 REMOVED in 6.18
  slot  67 ndo_dfwd_add_station               moved to 6.18 slot  76
  slot  68 ndo_dfwd_del_station               moved to 6.18 slot  77
  slot  69 ndo_set_tx_maxrate                 moved to 6.18 slot  78
  slot  70 ndo_get_iflink                     moved to 6.18 slot  79
  slot  71 ndo_change_proto_down              REMOVED in 6.18
  slot  72 ndo_fill_metadata_dst              moved to 6.18 slot  80
  slot  73 ndo_set_rx_headroom                moved to 6.18 slot  81
  slot  74 ndo_bpf                            moved to 6.18 slot  82
  slot  75 ndo_xdp_xmit                       moved to 6.18 slot  83
  slot  76 ndo_xsk_wakeup                     moved to 6.18 slot  85
  slot  77 ndo_get_devlink_port               REMOVED in 6.18
  slot  78 ndo_tunnel_ctl                     moved to 6.18 slot  86
  slot  79 ndo_get_peer_dev                   moved to 6.18 slot  87
  NEW in 6.18 slot  12: ndo_eth_ioctl
  NEW in 6.18 slot  13: ndo_siocbond
  NEW in 6.18 slot  14: ndo_siocwandev
  NEW in 6.18 slot  15: ndo_siocdevprivate
  NEW in 6.18 slot  54: ndo_sk_get_lower_dev
  NEW in 6.18 slot  61: ndo_fdb_del_bulk
  NEW in 6.18 slot  64: ndo_mdb_add
  NEW in 6.18 slot  65: ndo_mdb_del
  NEW in 6.18 slot  66: ndo_mdb_del_bulk
  NEW in 6.18 slot  67: ndo_mdb_dump
  NEW in 6.18 slot  68: ndo_mdb_get
  NEW in 6.18 slot  84: ndo_xdp_get_xmit_slave
  NEW in 6.18 slot  88: ndo_fill_forward_path
  NEW in 6.18 slot  89: ndo_get_tstamp
  NEW in 6.18 slot  90: ndo_hwtstamp_get
  NEW in 6.18 slot  91: ndo_hwtstamp_set

== ethtool_ops: 5.10 has 0 members, 6.18 has 0 ==
```
== ethtool_ops: 5.10 has 65 members, 6.18 has 86 ==
  slot  10 get_eeprom_len                 moved to 6.18 slot  11
  slot  11 get_eeprom                     moved to 6.18 slot  12
  slot  12 set_eeprom                     moved to 6.18 slot  13
  slot  13 get_coalesce                   moved to 6.18 slot  14
  slot  14 set_coalesce                   moved to 6.18 slot  15
  slot  15 get_ringparam                  moved to 6.18 slot  16
  slot  16 set_ringparam                  moved to 6.18 slot  17
  slot  17 get_pause_stats                moved to 6.18 slot  18
  slot  18 get_pauseparam                 moved to 6.18 slot  19
  slot  19 set_pauseparam                 moved to 6.18 slot  20
  slot  20 self_test                      moved to 6.18 slot  21
  slot  21 get_strings                    moved to 6.18 slot  79
  slot  22 set_phys_id                    moved to 6.18 slot  23
  slot  23 get_ethtool_stats              moved to 6.18 slot  24
  slot  24 begin                          moved to 6.18 slot  25
  slot  25 complete                       moved to 6.18 slot  26
  slot  26 get_priv_flags                 moved to 6.18 slot  27
  slot  27 set_priv_flags                 moved to 6.18 slot  28
  slot  28 get_sset_count                 moved to 6.18 slot  78
  slot  29 get_rxnfc                      moved to 6.18 slot  30
  slot  30 set_rxnfc                      moved to 6.18 slot  31
  slot  31 flash_device                   moved to 6.18 slot  32
  slot  32 reset                          moved to 6.18 slot  33
  slot  33 get_rxfh_key_size              moved to 6.18 slot  35
  slot  34 get_rxfh_indir_size            moved to 6.18 slot  36
  slot  35 get_rxfh                       moved to 6.18 slot  37
  slot  36 set_rxfh                       moved to 6.18 slot  38
  slot  37 get_rxfh_context               REMOVED in 6.18
  slot  38 set_rxfh_context               REMOVED in 6.18
  slot  39 get_channels                   moved to 6.18 slot  44
  slot  40 set_channels                   moved to 6.18 slot  45
  slot  41 get_dump_flag                  moved to 6.18 slot  46
  slot  42 get_dump_data                  moved to 6.18 slot  47
  slot  43 set_dump                       moved to 6.18 slot  48
  slot  44 get_ts_info                    moved to 6.18 slot  49
  slot  45 get_module_info                moved to 6.18 slot  51
  slot  46 get_module_eeprom              moved to 6.18 slot  52
  slot  47 get_eee                        moved to 6.18 slot  53
  slot  48 set_eee                        moved to 6.18 slot  54
  slot  49 get_tunable                    moved to 6.18 slot  55
  slot  50 set_tunable                    moved to 6.18 slot  56
  slot  51 get_per_queue_coalesce         moved to 6.18 slot  57
  slot  52 set_per_queue_coalesce         moved to 6.18 slot  58
  slot  53 get_link_ksettings             moved to 6.18 slot  59
  slot  54 set_link_ksettings             moved to 6.18 slot  60
  slot  55 get_fecparam                   moved to 6.18 slot  62
  slot  56 set_fecparam                   moved to 6.18 slot  63
  slot  57 get_ethtool_phy_stats          moved to 6.18 slot  64
  slot  58 get_phy_tunable                moved to 6.18 slot  65
  slot  59 set_phy_tunable                moved to 6.18 slot  66
  slot  60 get_sset_count                 moved to 6.18 slot  78
  slot  61 get_strings                    moved to 6.18 slot  79
  slot  62 get_stats                      moved to 6.18 slot  80
  slot  63 start_cable_test               moved to 6.18 slot  84
  slot  64 start_cable_test_tdr           moved to 6.18 slot  85
  NEW in 6.18 slot  10: get_link_ext_stats
  NEW in 6.18 slot  34: get_rx_ring_count
  NEW in 6.18 slot  39: get_rxfh_fields
  NEW in 6.18 slot  40: set_rxfh_fields
  NEW in 6.18 slot  41: create_rxfh_context
  NEW in 6.18 slot  42: modify_rxfh_context
  NEW in 6.18 slot  43: remove_rxfh_context
  NEW in 6.18 slot  50: get_ts_stats
  NEW in 6.18 slot  61: get_fec_stats
  NEW in 6.18 slot  67: get_module_eeprom_by_page
  NEW in 6.18 slot  68: set_module_eeprom_by_page
  NEW in 6.18 slot  69: get_eth_phy_stats
  NEW in 6.18 slot  70: get_eth_mac_stats
  NEW in 6.18 slot  71: get_eth_ctrl_stats
  NEW in 6.18 slot  72: get_rmon_stats
  NEW in 6.18 slot  73: get_module_power_mode
  NEW in 6.18 slot  74: set_module_power_mode
  NEW in 6.18 slot  75: get_mm
  NEW in 6.18 slot  76: set_mm
  NEW in 6.18 slot  77: get_mm_stats
  NEW in 6.18 slot  81: get_plca_cfg
  NEW in 6.18 slot  82: set_plca_cfg
  NEW in 6.18 slot  83: get_plca_status
