# Firmware symbol map (phase 6, 2026-10-01)

The vendor Wi-Fi firmware (`FIRMWARE.bin`, 928,920 bytes) ships no symbol table, but it carries three
kinds of name lists that map to code and parameters. With the `-0x40000` file bias established in
`docs/phase4/firmware-disasm.md` (file offset = table address - 0x40000, odd means Thumb), these become
a symbol map.

Method (all reproducible with capstone/pyelftools, see the commands in the phase-4 report):

- scan the tables region `0xC3000..0xE2C98` for 8-byte `{code_addr, name_ptr}` pairs where
  `code_addr - 0x40000` falls in the code range and `name_ptr - 0x40000` resolves to a printable string;
- extract `smac_*` and parameter-style names from the tail strings directly;
- verify each resolved address disassembles into a plausible Thumb function.

## 1. Radar / DFS control table - 30 named entries (addresses verified)

`cac`, `cac_silent_enable`, `cacenable`, `ctsdura`, `debug`, `detect_check`, `dfsdebug`, `dfstrig`,
`enabletimer`, `get_detect_check_info`, `get_radar_th`, `log_switch`, `non_occupancy_period`,
`octo_filter_enable`, `offcactime`, `offchanenable`, `offchannum`, `offchantime`,
`one_pulse_chirp_enable`, `operntime`, `pulse_check_filter`, `radar_phy_enable`, `radarfilter`,
`radarfilter_get`, `read_pulse`, `set_5g_channel_bitmap`, `set_next_chan`, `set_radar_th`,
`sudden_bad_delta_gdpt_ratio`, `sudden_good_delta_gdpt_ratio`

The nine entries of the handler table at `0xC5B84` (`dfsenable`, `cacenable`, `dfsdebug`, `offchannum`,
`ctsdura`, `radarfilter`, `radarfilter_get`, `enabletimer`, `offchanenable`) were confirmed to decode
into clean Thumb prologues in the phase-4 report. The remaining names sit in adjacent tables with the
same structure.

**Reading:** the firmware itself owns channel-availability checking and radar detection. The driver only
configures thresholds and receives results, which is why the driver's `alg` command set contains the
matching getters and setters.

## 2. Firmware message vocabulary - 45 `smac_*` names (name-only list in the tail)

Two groups:

- **ISR names:** `smac_backoff_timeout_2g/5g_isr`, `smac_coex_pta_rx_abort_timeout_2g/5g_isr`,
  `smac_coex_pta_tx_abort_timeout_2g/5g_isr`, `smac_coex_rx_abort_end_2g/5g_isr`,
  `smac_coex_tx_abort_end_2g/5g_isr`, `smac_common_timer_2g/5g_isr`, `smac_lifetime_expire_2g/5g_isr`.
- **Message processors:** `smac_msg_proc_ac_suspend_req`, `smac_msg_proc_beacon_tx_resume_req`,
  `smac_msg_proc_beacon_tx_suspend_req`, `smac_msg_proc_clr_hw_fifo_req`,
  `smac_msg_proc_disable_trx_req`, `smac_msg_proc_enable_trx_req`,
  `smac_msg_proc_rx_ring_reset_req`, `smac_msg_proc_set_cali_ppdu_tx_num_req`, and more of the same
  `smac_msg_proc_*_req` shape.

**Reading:** this is the firmware side of the wire protocol decoded in
`docs/phase5/message-decode.md`. `smac_msg_proc_set_cali_ppdu_tx_num_req` in particular is a
calibration message, and the `2g/5g` split matches the two radios.

## 3. Rate-control parameter set - 26 descriptors at `0xC48F8`

`a_ascend_protocol_per_thrd`, `ascend_bw_gdpt_better_thrd`, `ascend_bw_probe_pktcnt_min_thrd`,
`ascend_bw_rate_keep_cnt_thrd`, `b_ascend_protocol_per_thrd`, `cfg_vi_spec_per`, `cfg_vo_spec_per`,
`descend_bw_event_trig_per_thrd`, `descend_bw_gdpt_better_thrd`, `descend_bw_min_pktcnt_thrd`,
`descend_bw_probe_pktcnt_min_thrd`, `descend_protocol_pre_thrd`, `gi1_with_2xltf_en`,
`ht_priv_2_vht_probe_switch`, `probe_gi_start_intvl_pktnum`, `probe_per_init_strategy`,
`probe_reduction_strategy`, `probe_update_best_diff_nss_th`, `probe_update_best_opt_en`,
`probe_update_best_same_nss_th`, `rx_rate_aging_time`, `sudden_bad_delta_gdpt_ratio`,
`sudden_good_delta_gdpt_ratio`, `suspect_collision_ppdu_cnt_th`, `tx_rate_aging_time`,
`vht_priv_rate_probe_switch`.

**Reading:** the firmware runs its own rate adaptation (ascend/descend thresholds, probing policy, aging
timers, `2xLTF`/`1xGI` and VHT-private-probe switches). These names are the tuning surface a future
open implementation would have to reproduce, and they explain why the driver's `iw list` shows
"HT TX MCS rate indexes are undefined": rate selection lives on the firmware side.

## 4. Limits

- Only the DFS/radar tables produced verified `{address, name}` pairs; the `smac_*` and parameter names
  are name-only lists in the tail, with no addresses recovered yet.
- The lists are complete as far as the string scan goes (45 `smac_*`, 26 descriptors, 30 radar names);
  whether every handler in the firmware is named in these tables is unknown.
- No attempt was made to disassemble and identify the semantics of individual radar functions; the
  phase-4 report established only that the nine table entries decode into real functions.
