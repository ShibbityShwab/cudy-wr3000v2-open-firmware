# TABLES - the decoded alg_cfg / rate-control tables (wifi-rate-ctl)

All values are derived from the local pinned vendor module (sha256
22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2) by
tools/decode_alg_engine.py.  The complete machine dump is
evidence/TABLES-generated.md (and evidence/alg-engine.json); this file is the
curated subset the driver patch and the fire recipe use.

## T1. The alg_cfg blob (10-byte header + value) [V]

    +0  u16 cfg id          +2 u8 cfg_type (0 SET / 1 GET)
    +3  u8  pkt_type        +4 u32 dev_ret (host 0)
    +8  u8  need_w4 (1)     +9 u8 reserved
    +10 value[]

Carrier: HCC group 0 id 2, key 0x0101; payload cookie0|cookie1|key|len|blob;
frame total = 12 + 16 + len.  Blob len = LUT+14 (per command).

## T2. Rate-control command family (401..422)

| id | SET | GET | blob | value fields | enum / table |
| ---: | --- | --- | ---: | --- | --- |
| 401 | rate_mode | get_rate_mode | 11 | u8 @10 | auto=0, fixed=1 |
| 402 | fec_coding | get_fec_coding | 11 | u8 @10 | bcc=0, ldpc=1 |
| 403 | mu_fec_coding | get_mu_fec_coding | 12 | u8 pkt@10, u8 @11 | bcc/ldpc |
| 404 | protect_mode | get_protect_mode | 14 | u8 x4 @10..13 | none=0, rts=1, scts=2 |
| 405 | he_ltf_type | get_he_ltf_type | 12 | u8 pkt@10, u8 @11 | 1x=0, 2x=1, 4x=2 |
| 406 | dcm_enable | get_dcm_enable | 12 | u8 pkt@10, u8 @11 | disable=0, enable=1 |
| 407 | freq_bw_mode | get_freq_bw_mode | parser | dedicated | bandwidth table (20M=0,40M=1,80M=2,160M=3) |
| 408 | debug_log_switch | get_debug_log_switch | 11 | u8 @10 | disable/enable |
| 409 | ar_dev_param | get_ar_dev_param | 15 | u8 knob@10, u32 value@11 (LE) | T3 (26 knobs) |
| 410 | tx_cnt | get_tx_cnt | 14 | u8 x4 @10..13 | counters |
| 411 | hi_rate | get_hi_rate | parser | dedicated | T4 |
| 412 | he_sigb_rate | get_he_sigb_rate | 11 | u8 @10 | 6-row MCS table |
| 413 | mu_tx_cnt | get_mu_tx_cnt | 11 | u8 @10 | counter |
| 414 | mu_rate | get_mu_rate | parser | dedicated | pkt@10, user@11, nss@12 [V writes] |
| 415 | mu_rts_mode | get_mu_rts_mode | 11 | u8 @10 | disable/enable |
| 416 | legacy_rate | get_legacy_rate | parser | dedicated | T4 |
| 417 | preamble_type | get_preamble_type | 11 | u8 @10 | short=0, long=1 |
| 418 | gi_type | - | 11 | u8 @10 | normal=0, fixed=1 |
| 419 | ar_probe | - | 11 | u8 @10 | normal=0, slow=1 |
| 420 | sigb_dcm | get_sigb_dcm | 11 | u8 @10 | disable/enable |
| 421 | - | get_current_ration | 30 | MAC[6] @10 (GET) | answer: tx/rx rate report |
| 422 | ar_scen_mode | - | 11 | [U] | normal=0, move=1 (table exists, LUT descriptor empty) |

## T3. ar_dev_param knobs (id 409 value byte at +10)

    0 sudden_good_delta_gdpt_ratio      13 rx_rate_aging_time
    1 sudden_bad_delta_gdpt_ratio       14 descend_bw_event_trig_per_thrd
    2 descend_protocol_pre_thrd         15 probe_update_best_opt_en
    3 11b_ascend_protocol_per_thrd      16 probe_update_best_same_nss_th
    4 11a_ascend_protocol_per_thrd      17 probe_update_best_diff_nss_th
    5 ascend_bw_rate_keep_cnt_thrd      18 vht_priv_rate_probe_switch
    6 descend_bw_min_pktcnt_thrd        19 ht_priv_2_vht_probe_switch
    7 ascend_bw_probe_pktcnt_min_thrd   20 suspect_collision_ppdu_cnt_th
    8 descend_bw_probe_pktcnt_min_thrd  21 cfg_vi_spec_per
    9 probe_gi_start_intvl_pktnum       22 cfg_vo_spec_per
   10 probe_reduction_strategy          23 gi1_with_2xltf_en
   11 probe_per_init_strategy           24 descend_bw_gdpt_better_thrd
   12 tx_rate_aging_time                25 ascend_bw_gdpt_better_thrd

## T4. Fixed-rate blocks (dedicated parsers; wire length [U] pending probe)

hi_rate value (after the mandatory pkt-type name token and 5 args
"<rank> <protocol> nss<N> mcs<M> <gi>"):

    +10 rank (0..3)   +11 protocol enum   +12 nss-1   +13 mcs (0..32)
    +14 gi enum (non-HT short=1/long=0; HT+ gi1=0/gi2=1/gi4=2)
    protocol enum: 11b=0, 11ag=1, 11n=2, 11n_green=3, 11ac=4, 11ax_su=8,
                   11ax_mu=9, 11ax_er_su=10
    accepted pkt types: ucast_data(0), ndp(9), ndpa(10), other_ctrl(12)

legacy_rate value:

    +10 pkt_type (0..3 arg)  +11 rate enum  +12 protocol (0=11b,1=ofdm)
    +13 rank (prot_ctrl 3-arg form only)
    rate enum: 1M=0, 2M=1, 5.5M=2, 11M=3, 6M=11, 9M=15, 12M=10, 18M=14,
               24M=9, 36M=13, 48M=8, 54M=12

## T5. TPC family (1801..1809)

| id | SET | GET | blob | value |
| ---: | --- | --- | ---: | --- |
| 1801 | tpc_mode | get_tpc_mode | 11 | u8 numeric |
| 1802 | tpc_pow_lvl | get_tpc_pow_lvl | 12 | u8 mode@10 (data=0,mag=1,ctrl=2), u8 level@11 |
| 1803 | tpc_code | get_tpc_code | parser | dedicated; no response formatter |
| 1804 | pow_mode | get_pow_mode | 11 | u8 (init=0, sleep=1, third=2) |
| 1805 | resp_chain_sel | get_resp_chain_sel | 11 | u8 numeric |
| 1806 | set_sta_id | - | parser | dedicated |
| 1807 | far_dist_switch | get_far_dist_switch | parser | dedicated; no response formatter |
| 1808 | near_dist_switch | get_near_dist_switch | parser | dedicated; no response formatter |
| 1809 | waterfilling_mode | get_waterfilling_mode | 11 | u8 numeric |

## T6. The response path

D2H g1 id3, tag 0x5A5A:

    payload = cookie0 u32 | cookie1 u32 | u16 syn_id | u16 body_len |
              body[body_len] | 4 B pad
    syn_id 0x0101 = alg cfg; body = the request blob with dev_ret updated
    (SET) / value filled (GET)

Dispatch (hmac_sync_dmac_alg_cfg_rsp_entry 0x87b58) by body[0] through
g_ast_hmac_config_alg_output_table (32 ranges):

    401..422   alg_autorate_config_param_output_entry
    1801..1809 alg_tpc_bbit_config_param_output_entry
    3501..3525 alg_eqment_config_param_output_entry
    1901..1904 alg_otp_config_param_output_entry
    4304..4308 alg_ns_rate_config_param_output_entry
    ... (see evidence/TABLES-generated.md for all 32)

Per-id formatter functions (source of the GET value semantics):

    401 alg_cfg_process_rate_mode_output       409 alg_cfg_process_ar_dev_param_output
    402 alg_cfg_process_fec_coding_output      410 alg_cfg_process_tx_cnt_output
    403 alg_cfg_process_mu_fec_coding_output   411 alg_cfg_process_hi_rate_output
    404 alg_cfg_process_protect_mode_output    412 alg_cfg_process_he_sigb_rate_output
    405 alg_cfg_process_he_ltf_type_output     413 alg_cfg_process_mu_tx_cnt_output
    406 alg_cfg_process_dcm_enable_output      414 alg_cfg_process_mu_rate_output
    407 alg_cfg_process_freq_bw_mode_output    415 alg_cfg_process_mu_rts_mode_output
    408 alg_cfg_process_debug_log_switch_output 416 alg_cfg_process_legacy_rate_output
    417 alg_cfg_process_preamble_type_output   418 alg_cfg_process_gi_type_output
    419 alg_cfg_process_ar_probe_output        420 alg_cfg_process_sigb_dcm_output
    421 alg_cfg_process_curr_ration_output     422 alg_cfg_process_ar_mode_output
    1801 alg_cfg_param_output_tpc_mode         1804 alg_cfg_param_output_tpc_pow_mode
    1802 alg_cfg_param_output_tpc_fix_pow_lvl  1805 alg_cfg_param_output_resp_chain_sel
    1806 alg_cfg_param_output_sta_id           1809 alg_cfg_param_output_wf_mode

Gap [V]: 1803, 1807, 1808 have no per-id formatter row in the TPC output table.

## T7. GET value semantics (from the formatter format strings) [V]

    401: "[iw]rate_mode set succ!" / get prints the mode enum
    409: SET/GET "ar_dev_param [name] [value]" using the T3 name table
    411: "PKT_TYPE[%d] SET RANK[%d] to PROTOCOL[%d] NSS[%d]" /
         "RATE[MCS%d], GI[%d]"
    416: "PKT_TYPE[%d] GET RANK[%d], PROTOCOL[%d] RATE[%d]"
    421: "get_current_ration[%02X:%02X:%02X:XX:XX:XX] user not exist!" /
         "tx_phymode = %s / tx_bw = %s / tx_nss = %u / tx_gi = %s /
          tx_mcs = mcs%u" and the rx mirror
    1801: "tpc_mode = %u"
    1802: "tpc_pow_level = %u"
    1804: "pow_mode = %s"
