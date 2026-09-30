# ALG command table extraction and on-device probing

Source binary: `build/tmp/hi5622v100_wifi.ko` (ARM32 relocatable ELF, full `.symtab`).
Device: `root@192.168.10.1` (read-only `iwpriv ... alg get_*` probes).

## 1. Method used to resolve the table

1. `g_ast_alg_cfg_process_info_table` was located in `.symtab` as a local
   `STT_OBJECT`: `st_shndx = 27` (`.data`), `st_value = 0x35c8`, `st_size = 4968`.
   `4968 / 12 = 414` entries of 12 bytes.
2. The record layout was recovered from the two in-driver lookup routines
   (`alg_cfg_search_process_info_by_cfg_id` @ `0x14b038` and the name/dir matcher @ `0x14afb4`),
   both of which walk the table with `add rX, rX, #0xc` (stride 12) and read:

   | offset | size | field | evidence |
   |---|---|---|---|
   | +0x00 | 4 | `const char *name` | one `R_ARM_ABS32` relocation per entry in `.rel.data` |
   | +0x04 | 2 | `u16 cfg_id` | `ldrh r2, [r3, #4]` in both search routines |
   | +0x06 | 1 | `u8 dir` | `ldrb r2, [r3, #6]` compared against the direction argument |
   | +0x07 | 1 | `u8 pad` | always `0x00` for all 414 entries |
   | +0x08 | 4 | `u32 flags` | constant data, only 4 distinct values (not part of name/cfg_id/dir) |

3. Names were resolved through the table name-pointer relocations: each of the 414
   `.rel.data` relocations at offset `0x35c8 + i*12` targets section 14 (`.rodata.str1.4`)
   with the string offset stored as the addend at the relocation site. Reading the
   NUL-terminated C string at `section.sh_offset + addend` yielded a name for
   **414/414** entries (0 unresolved).
4. Direction semantics were confirmed empirically against the names: every `get_*` name
   has `dir = 1`, every `set_*` name has `dir = 0`. Therefore `dir = 1` is the getter.
   `cfg_id` is shared between the getter and setter of the same parameter (e.g. `401`
   for both `rate_mode` and `get_rate_mode`), so `cfg_id` alone is not unique; the
   `(cfg_id, dir)` pair is.

## 2. Full table (414 entries)

`dir` is shown as `get` (raw byte 1) or `set` (raw byte 0).

| # | name | cfg_id | dir | interpretation |
|---:|---|---:|---|---|
| 0 | rate_mode | 401 | set |  |
| 1 | get_rate_mode | 401 | get | read rate mode |
| 2 | ar_scen_mode | 422 | set |  |
| 3 | fec_coding | 402 | set |  |
| 4 | get_fec_coding | 402 | get | read fec coding |
| 5 | ar_probe | 419 | set |  |
| 6 | gi_type | 418 | set |  |
| 7 | mu_fec_coding | 403 | set |  |
| 8 | get_mu_fec_coding | 403 | get | read mu fec coding |
| 9 | protect_mode | 404 | set |  |
| 10 | get_protect_mode | 404 | get | read protect mode |
| 11 | he_ltf_type | 405 | set |  |
| 12 | get_he_ltf_type | 405 | get | read he ltf type |
| 13 | dcm_enable | 406 | set |  |
| 14 | get_dcm_enable | 406 | get | read dcm enable |
| 15 | freq_bw_mode | 407 | set |  |
| 16 | get_freq_bw_mode | 407 | get | read freq bw mode |
| 17 | debug_log_switch | 408 | set |  |
| 18 | get_debug_log_switch | 408 | get | read debug log switch |
| 19 | ar_dev_param | 409 | set | parameter: ar dev |
| 20 | get_ar_dev_param | 409 | get | read ar dev param |
| 21 | sigb_dcm | 420 | set |  |
| 22 | get_sigb_dcm | 420 | get | read sigb dcm |
| 23 | tx_cnt | 410 | set |  |
| 24 | get_tx_cnt | 410 | get | read tx cnt |
| 25 | hi_rate | 411 | set |  |
| 26 | get_hi_rate | 411 | get | read hi rate |
| 27 | he_sigb_rate | 412 | set |  |
| 28 | get_he_sigb_rate | 412 | get | read he sigb rate |
| 29 | mu_tx_cnt | 413 | set |  |
| 30 | get_mu_tx_cnt | 413 | get | read mu tx cnt |
| 31 | mu_rate | 414 | set |  |
| 32 | get_mu_rate | 414 | get | read mu rate |
| 33 | mu_rts_mode | 415 | set |  |
| 34 | get_mu_rts_mode | 415 | get | read mu rts mode |
| 35 | legacy_rate | 416 | set |  |
| 36 | get_legacy_rate | 416 | get | read legacy rate |
| 37 | preamble_type | 417 | set |  |
| 38 | get_preamble_type | 417 | get | read preamble type |
| 39 | get_current_ration | 421 | get | read current ration |
| 40 | sch_mode | 701 | set |  |
| 41 | get_sch_mode | 701 | get | read sch mode |
| 42 | max_pe | 702 | set |  |
| 43 | get_max_pe | 702 | get | read max pe |
| 44 | su_sch_user | 801 | set |  |
| 45 | su_sch_tid | 802 | set |  |
| 46 | su_sch_tid_del | 804 | set |  |
| 47 | su_sch_multi_tid | 803 | set |  |
| 48 | set_su_sch_debug_switch | 805 | set | write su sch debug switch |
| 49 | get_su_sch_debug_switch | 805 | get | read su sch debug switch |
| 50 | get_su_sch_mode | 806 | get | read su sch mode |
| 51 | set_su_sch_mode | 806 | set | write su sch mode |
| 52 | get_su_sch_rr_vi_delay | 807 | get | read su sch rr vi delay |
| 53 | set_su_sch_rr_vi_delay | 807 | set | write su sch rr vi delay |
| 54 | get_ring_refresh_qtable_switch | 808 | get | read ring refresh qtable switch |
| 55 | set_ring_refresh_qtable_switch | 808 | set | write ring refresh qtable switch |
| 56 | dl_mumimo_sch_user | 901 | set |  |
| 57 | dl_mumimo_sch_tid | 902 | set |  |
| 58 | dl_mumimo_sch_multi_tid | 903 | set |  |
| 59 | dl_mumimo_sch_nss | 904 | set |  |
| 60 | dl_mumimo_group_add | 905 | set |  |
| 61 | dl_mumimo_enable | 906 | set |  |
| 62 | dl_mumimo_sch_ack_type | 907 | set |  |
| 63 | dl_mumimo_altx_bw | 908 | set |  |
| 64 | dl_mumimo_gi_ltf_comb | 909 | set |  |
| 65 | dl_mumimo_hac_calc_enable | 912 | set |  |
| 66 | dl_mumimo_dbg_log | 910 | set |  |
| 67 | dl_mumimo_11ac_gi_type | 911 | set |  |
| 68 | ul_mumimo_enable | 1001 | set |  |
| 69 | get_ul_mumimo_enable | 1001 | get | read ul mumimo enable |
| 70 | ul_mumimo_pair_user | 1002 | set |  |
| 71 | ul_mumimo_dbg_log | 1003 | set |  |
| 72 | get_ul_mumimo_dbg_log | 1003 | get | read ul mumimo dbg log |
| 73 | ul_mumimo_fix_rate | 1004 | set |  |
| 74 | get_ul_mumimo_fix_rate | 1004 | get | read ul mumimo fix rate |
| 75 | ul_mumimo_fix_txtime | 1005 | set |  |
| 76 | get_ul_mumimo_fix_txtime | 1005 | get | read ul mumimo fix txtime |
| 77 | ofdma_user_num | 1101 | set |  |
| 78 | get_ofdma_user_num | 1101 | get | read ofdma user num |
| 79 | puncture_mode | 1102 | set |  |
| 80 | get_puncture_mode | 1102 | get | read puncture mode |
| 81 | ru_alloc | 1103 | set |  |
| 82 | get_ru_alloc | 1103 | get | read ru alloc |
| 83 | ofdma_mac_addr | 1104 | set |  |
| 84 | get_ofdma_mac_addr | 1104 | get | read ofdma mac addr |
| 85 | ofdma_auto_mac_addr | 1105 | set |  |
| 86 | user_ru | 1106 | set |  |
| 87 | get_user_ru | 1106 | get | read user ru |
| 88 | ofdma_user_tid | 1107 | set |  |
| 89 | get_ofdma_user_tid | 1107 | get | read ofdma user tid |
| 90 | ofdma_aggr_num | 1108 | set |  |
| 91 | get_ofdma_aggr_num | 1108 | get | read ofdma aggr num |
| 92 | ofdma_aggr_size | 1109 | set |  |
| 93 | get_ofdma_aggr_size | 1109 | get | read ofdma aggr size |
| 94 | ofdma_bw | 1110 | set |  |
| 95 | get_ofdma_bw | 1110 | get | read ofdma bw |
| 96 | ofdma_ltf_gi | 1111 | set |  |
| 97 | get_ofdma_ltf_gi | 1111 | get | read ofdma ltf gi |
| 98 | ofdma_calc_sigb | 1112 | set |  |
| 99 | ofdma_mu_seq_type | 1116 | set |  |
| 100 | get_ofdma_mu_seq_type | 1116 | get | read ofdma mu seq type |
| 101 | ofdma_dl_sch_enable | 1114 | set |  |
| 102 | get_ofdma_dl_sch_enable | 1114 | get | read ofdma dl sch enable |
| 103 | ofdma_loss_inc | 1118 | set |  |
| 104 | get_ofdma_loss_inc | 1118 | get | read ofdma loss inc |
| 105 | ofdma_loss_dec | 1119 | set |  |
| 106 | get_ofdma_loss_dec | 1119 | get | read ofdma loss dec |
| 107 | ofdma_auto_puncture | 1115 | set |  |
| 108 | get_ofdma_auto_puncture | 1115 | get | read ofdma auto puncture |
| 109 | ofdma_dl_debug_enable | 1117 | set |  |
| 110 | get_ofdma_dl_debug_enable | 1117 | get | read ofdma dl debug enable |
| 111 | ofdma_certify_use_106_ru | 1113 | set |  |
| 112 | get_ofdma_certify_use_106_ru | 1113 | get | read ofdma certify use 106 ru |
| 113 | ofdma_backoff_time_th | 1120 | set |  |
| 114 | get_ofdma_backoff_time_th | 1120 | get | read ofdma backoff time th |
| 115 | ofdma_pk_mode_enable | 1121 | set |  |
| 116 | get_ofdma_pk_mode_enable | 1121 | get | read ofdma pk mode enable |
| 117 | ofdma_seq_user_num_thrd | 1122 | set |  |
| 118 | get_ofdma_seq_user_num_thrd | 1122 | get | read ofdma seq user num thrd |
| 119 | ofdma_sch_cnt_th | 1123 | set |  |
| 120 | get_ofdma_sch_cnt_th | 1123 | get | read ofdma sch cnt th |
| 121 | ul_ofdma_enable | 1201 | set |  |
| 122 | get_ul_ofdma_enable | 1201 | get | read ul ofdma enable |
| 123 | edca_prot_mode_opt | 207 | set |  |
| 124 | get_edca_prot_mode_opt | 207 | get | read edca prot mode opt |
| 125 | rts_on_collision_th | 208 | set |  |
| 126 | get_rts_on_collision_th | 208 | get | read rts on collision th |
| 127 | set_nav_duration | 211 | set | write nav duration |
| 128 | get_nav_duration | 211 | get | read nav duration |
| 129 | rts_off_fail_ratio_th | 209 | set |  |
| 130 | get_rts_off_fail_ratio_th | 209 | get | read rts off fail ratio th |
| 131 | mu_rts_opt_mode | 210 | set |  |
| 132 | get_mu_rts_opt_mode | 210 | get | read mu rts opt mode |
| 133 | edca_opt_rssi_th | 212 | set |  |
| 134 | get_edca_opt_rssi_th | 212 | get | read edca opt rssi th |
| 135 | edca_expand_probe | 213 | set |  |
| 136 | get_edca_expand_probe | 213 | get | read edca expand probe |
| 137 | get_edca_opt_en_ap | 201 | get | read edca opt en ap |
| 138 | al_tx | 1301 | set |  |
| 139 | get_al_tx_info | 1301 | get | read al tx info |
| 140 | al_rx | 1302 | set |  |
| 141 | get_al_rx_info | 1302 | get | read al rx info |
| 142 | al_tx_single_tone | 1303 | set |  |
| 143 | get_al_tx_single_tone | 1303 | get | read al tx single tone |
| 144 | tpc_mode | 1801 | set |  |
| 145 | get_tpc_mode | 1801 | get | read tpc mode |
| 146 | tpc_pow_lvl | 1802 | set |  |
| 147 | get_tpc_pow_lvl | 1802 | get | read tpc pow lvl |
| 148 | tpc_code | 1803 | set |  |
| 149 | get_tpc_code | 1803 | get | read tpc code |
| 150 | pow_mode | 1804 | set |  |
| 151 | get_pow_mode | 1804 | get | read pow mode |
| 152 | far_dist_switch | 1807 | set |  |
| 153 | get_far_dist_switch | 1807 | get | read far dist switch |
| 154 | near_dist_switch | 1808 | set |  |
| 155 | get_near_dist_switch | 1808 | get | read near dist switch |
| 156 | resp_chain_sel | 1805 | set |  |
| 157 | get_resp_chain_sel | 1805 | get | read resp chain sel |
| 158 | set_sta_id | 1806 | set | write sta id |
| 159 | waterfilling_mode | 1809 | set |  |
| 160 | get_waterfilling_mode | 1809 | get | read waterfilling mode |
| 161 | temp_limit | 1901 | set |  |
| 162 | get_temp_limit | 1901 | get | read temp limit |
| 163 | temp_log | 1902 | set |  |
| 164 | get_temp_log | 1902 | get | read temp log |
| 165 | get_temp_state | 1903 | get | read temp state |
| 166 | temp_comp | 1904 | set |  |
| 167 | get_temp_comp | 1904 | get | read temp comp |
| 168 | set_gain | 3101 | set | write gain |
| 169 | rx_ant | 3102 | set |  |
| 170 | dyn_cali | 3104 | set |  |
| 171 | online_iq | 3103 | set |  |
| 172 | dpd_cmd_ctrl | 3105 | set |  |
| 173 | online_dpd | 3111 | set |  |
| 174 | spectral_scan_en | 1401 | set |  |
| 175 | get_spectral_scan_en | 1401 | get | read spectral scan en |
| 176 | spectral_ofdm_det | 1402 | set |  |
| 177 | get_spectral_ofdm_det | 1402 | get | read spectral ofdm det |
| 178 | spectral_11b_det | 1403 | set |  |
| 179 | get_spectral_11b_det | 1403 | get | read spectral 11b det |
| 180 | spectral_nb_det | 1404 | set |  |
| 181 | get_spectral_nb_det | 1404 | get | read spectral nb det |
| 182 | fft_size | 1405 | set |  |
| 183 | get_fft_size | 1405 | get | read fft size |
| 184 | fft_period | 1406 | set |  |
| 185 | get_fft_period | 1406 | get | read fft period |
| 186 | fft_count | 1407 | set |  |
| 187 | get_fft_count | 1407 | get | read fft count |
| 188 | spectral_rssi_thr | 1408 | set |  |
| 189 | get_spectral_rssi_thr | 1408 | get | read spectral rssi thr |
| 190 | spectral_rssi_nb_thr | 1409 | set |  |
| 191 | get_spectral_rssi_nb_thr | 1409 | get | read spectral rssi nb thr |
| 192 | spectral_power_thr | 1410 | set |  |
| 193 | get_spectral_power_thr | 1410 | get | read spectral power thr |
| 194 | nb_thr | 1411 | set |  |
| 195 | get_nb_thr | 1411 | get | read nb thr |
| 196 | rpt_mode | 1412 | set |  |
| 197 | get_rpt_mode | 1412 | get | read rpt mode |
| 198 | fftin_type | 1413 | set |  |
| 199 | get_fftin_type | 1413 | get | read fftin type |
| 200 | ant_index | 1414 | set |  |
| 201 | get_ant_index | 1414 | get | read ant index |
| 202 | deci_coef_en | 1415 | set |  |
| 203 | get_deci_coef_en | 1415 | get | read deci coef en |
| 204 | deci_coef_man | 1416 | set |  |
| 205 | get_deci_coef_man | 1416 | get | read deci coef man |
| 206 | get_data_abnormal | 1418 | get | read data abnormal |
| 207 | agc_lock_en | 1417 | set |  |
| 208 | get_agc_lock_en | 1417 | get | read agc lock en |
| 209 | csi_en | 1701 | set |  |
| 210 | get_csi_en | 1701 | get | read csi en |
| 211 | csi_location_vap | 1702 | set |  |
| 212 | get_csi_location_vap | 1702 | get | read csi location vap |
| 213 | csi_frame_type | 1703 | set |  |
| 214 | get_csi_frame_type | 1703 | get | read csi frame type |
| 215 | csi_band_width | 1704 | set |  |
| 216 | get_csi_band_width | 1704 | get | read csi band width |
| 217 | csi_whitelist | 1705 | set |  |
| 218 | get_csi_whitelist | 1705 | get | read csi whitelist |
| 219 | csi_resp_rpt_flag | 1706 | set |  |
| 220 | get_csi_resp_rpt_flag | 1706 | get | read csi resp rpt flag |
| 221 | trigger_fill_mode | 2001 | set |  |
| 222 | get_trigger_fill_mode | 2001 | get | read trigger fill mode |
| 223 | trigger_basic_trg_sw | 2002 | set |  |
| 224 | get_trigger_basic_trg_sw | 2002 | get | read trigger basic trg sw |
| 225 | trigger_bsrp_trg_sw | 2003 | set |  |
| 226 | get_trigger_bsrp_trg_sw | 2003 | get | read trigger bsrp trg sw |
| 227 | trigger_aggr_basic_trg_ra_addr_sw | 2004 | set |  |
| 228 | get_trigger_aggr_basic_trg_ra_addr_sw | 2004 | get | read trigger aggr basic trg ra addr sw |
| 229 | trigger_tb_ppdu_ofdma_ba_sw | 2010 | set |  |
| 230 | get_trigger_tb_ppdu_ofdma_ba_sw | 2010 | get | read trigger tb ppdu ofdma ba sw |
| 231 | trigger_comm1 | 2005 | set |  |
| 232 | get_trigger_comm1 | 2005 | get | read trigger comm1 |
| 233 | trigger_comm2_fix | 2006 | set |  |
| 234 | get_trigger_comm2_fix | 2006 | get | read trigger comm2 fix |
| 235 | trigger_comm2_calc | 2007 | set |  |
| 236 | get_trigger_comm2_calc | 2007 | get | read trigger comm2 calc |
| 237 | trigger_user | 2008 | set |  |
| 238 | get_trigger_user | 2008 | get | read trigger user |
| 239 | trigger_user_var | 2009 | set |  |
| 240 | get_trigger_user_var | 2009 | get | read trigger user var |
| 241 | trigger_mu_ltf_mode | 2013 | set |  |
| 242 | set_ulsch_dev_sch_mode | 2601 | set | write ulsch dev sch mode |
| 243 | get_ulsch_dev_sch_mode | 2601 | get | read ulsch dev sch mode |
| 244 | set_ulsch_dev_tx_trigger_cycle | 2602 | set | write ulsch dev tx trigger cycle |
| 245 | get_ulsch_dev_tx_trigger_cycle | 2602 | get | read ulsch dev tx trigger cycle |
| 246 | set_ulsch_dev_interval_param | 2603 | set | write ulsch dev interval param |
| 247 | get_ulsch_dev_interval_param | 2603 | get | read ulsch dev interval param |
| 248 | set_ulsch_dev_twt_sch_interval | 2604 | set | write ulsch dev twt sch interval |
| 249 | get_ulsch_dev_twt_sch_interval | 2604 | get | read ulsch dev twt sch interval |
| 250 | set_ulsch_dev_bsrp_trigger_param | 2605 | set | write ulsch dev bsrp trigger param |
| 251 | get_ulsch_dev_bsrp_trigger_param | 2605 | get | read ulsch dev bsrp trigger param |
| 252 | set_ul_sch_sw | 4001 | set | write ul sch sw |
| 253 | get_ul_sch_sw | 4001 | get | read ul sch sw |
| 254 | set_ul_sch_cycle | 4002 | set | write ul sch cycle |
| 255 | get_ul_sch_cycle | 4002 | get | read ul sch cycle |
| 256 | set_ul_sch_user | 4003 | set | write ul sch user |
| 257 | get_ul_sch_user | 4003 | get | read ul sch user |
| 258 | set_ulsch_user_in_bsr_list | 2606 | set | write ulsch user in bsr list |
| 259 | get_ulsch_user_in_bsr_list | 2606 | get | read ulsch user in bsr list |
| 260 | set_ulsch_user_twt_enable | 2607 | set | write ulsch user twt enable |
| 261 | get_ulsch_user_twt_enable | 2607 | get | read ulsch user twt enable |
| 262 | txmode_mode_sw | 2101 | set |  |
| 263 | get_txmode_mode_sw | 2101 | get | read txmode mode sw |
| 264 | txmode_user_fix_mode | 2107 | set |  |
| 265 | get_txmode_user_fix_mode | 2107 | get | read txmode user fix mode |
| 266 | txmode_user_fix_chain | 2108 | set |  |
| 267 | get_txmode_user_fix_chain | 2108 | get | read txmode user fix chain |
| 268 | txmode_all_user_fix_mode | 2103 | set |  |
| 269 | txmode_mgmt_chain | 2104 | set |  |
| 270 | get_txmode_mgmt_chain | 2104 | get | read txmode mgmt chain |
| 271 | txmode_11b_tx_sw | 2105 | set |  |
| 272 | get_txmode_11b_tx_sw | 2105 | get | read txmode 11b tx sw |
| 273 | txmode_bcast_data_chain | 2106 | set |  |
| 274 | get_txmode_bcast_data_chain | 2106 | get | read txmode bcast data chain |
| 275 | txmode_chain_prob_sw | 2102 | set |  |
| 276 | txmode_debug_log_switch | 2109 | set |  |
| 277 | get_txmode_debug_log_switch | 2109 | get | read txmode debug log switch |
| 278 | ack_opt | 4401 | set |  |
| 279 | get_ack_opt | 4401 | get | read ack opt |
| 280 | aggr_mode | 2301 | set |  |
| 281 | get_aggr_mode | 2301 | get | read aggr mode |
| 282 | su_aggr_time | 2302 | set |  |
| 283 | get_su_aggr_time | 2302 | get | read su aggr time |
| 284 | su_aggr_log | 2303 | set |  |
| 285 | get_su_aggr_log | 2303 | get | read su aggr log |
| 286 | su_aggr_probe_limit | 2304 | set |  |
| 287 | get_su_aggr_probe_limit | 2304 | get | read su aggr probe limit |
| 288 | su_aggr_sw | 2305 | set |  |
| 289 | get_su_aggr_sw | 2305 | get | read su aggr sw |
| 290 | mu_aggr_mode | 2306 | set |  |
| 291 | get_mu_aggr_mode | 2306 | get | read mu aggr mode |
| 292 | mu_aggr_time | 2307 | set |  |
| 293 | get_mu_aggr_time | 2307 | get | read mu aggr time |
| 294 | sounding_sch_mode | 2401 | set |  |
| 295 | get_sounding_sch_mode | 2401 | get | read sounding sch mode |
| 296 | sounding_period_group | 2403 | set |  |
| 297 | get_sounding_period_group | 2403 | get | read sounding period group |
| 298 | sounding_group_property | 2409 | set |  |
| 299 | get_sounding_group_property | 2409 | get | read sounding group property |
| 300 | sounding_user_codebook_size | 2413 | set |  |
| 301 | get_sounding_user_codebook_size | 2413 | get | read sounding user codebook size |
| 302 | sounding_user_grouping | 2414 | set |  |
| 303 | get_sounding_user_grouping | 2414 | get | read sounding user grouping |
| 304 | sounding_user_nc | 2415 | set |  |
| 305 | sounding_user_fback_type | 2416 | set |  |
| 306 | get_sounding_user_fback_type | 2416 | get | read sounding user fback type |
| 307 | get_sounding_grp_info | 2404 | get | read sounding grp info |
| 308 | sounding_trig_cnt | 2405 | set |  |
| 309 | get_sounding_trig_cnt | 2405 | get | read sounding trig cnt |
| 310 | sounding_trigger | 2406 | set |  |
| 311 | sounding_norm_log_sw | 2407 | set |  |
| 312 | get_sounding_norm_log_sw | 2407 | get | read sounding norm log sw |
| 313 | sounding_rssi_limit | 2410 | set |  |
| 314 | get_sounding_rssi_limit | 2410 | get | read sounding rssi limit |
| 315 | smartant_mode | 2501 | set |  |
| 316 | get_smartant_mode | 2501 | get | read smartant mode |
| 317 | fix_ant | 2502 | set |  |
| 318 | get_fix_ant | 2502 | get | read fix ant |
| 319 | train_period | 2503 | set |  |
| 320 | get_train_period | 2503 | get | read train period |
| 321 | smartant_debug | 2504 | set |  |
| 322 | get_smartant_debug | 2504 | get | read smartant debug |
| 323 | smartant_train_th | 2505 | set |  |
| 324 | get_smartant_train_th | 2505 | get | read smartant train th |
| 325 | smartant_su_rx_pk | 2506 | set |  |
| 326 | get_smartant_su_rx_pk | 2506 | get | read smartant su rx pk |
| 327 | virtual_user_tx | 2901 | set |  |
| 328 | scen_dbg_en | 3401 | set |  |
| 329 | get_scen_dbg_en | 3401 | get | read scen dbg en |
| 330 | get_xo_ducy_cali_param | 3501 | get | read xo ducy cali param |
| 331 | set_xo_ducy_cali_param | 3501 | set | write xo ducy cali param |
| 332 | curve_param | 3518 | set | parameter: curve |
| 333 | set_2g_all_curve_param | 3508 | set | write 2g all curve param |
| 334 | get_2g_all_curve_param | 3508 | get | read 2g all curve param |
| 335 | set_5g_all_curve_param | 3509 | set | write 5g all curve param |
| 336 | get_5g_all_curve_param | 3509 | get | read 5g all curve param |
| 337 | set_2g_curve_factor | 3510 | set | write 2g curve factor |
| 338 | get_2g_curve_factor | 3510 | get | read 2g curve factor |
| 339 | set_5g_curve_factor | 3511 | set | write 5g curve factor |
| 340 | get_5g_curve_factor | 3511 | get | read 5g curve factor |
| 341 | power_diff | 3519 | set |  |
| 342 | set_2g_upc | 3512 | set | write 2g upc |
| 343 | get_2g_upc | 3512 | get | read 2g upc |
| 344 | set_5g_upc | 3513 | set | write 5g upc |
| 345 | get_5g_upc | 3513 | get | read 5g upc |
| 346 | save_2g_upc | 3523 | get |  |
| 347 | save_5g_upc | 3524 | get |  |
| 348 | power_cali | 3516 | set |  |
| 349 | power_cali_mimo | 3517 | set |  |
| 350 | set_2g_power_param | 3502 | set | write 2g power param |
| 351 | get_2g_power_param | 3502 | get | read 2g power param |
| 352 | set_5g_power_param | 3504 | set | write 5g power param |
| 353 | get_5g_power_param | 3504 | get | read 5g power param |
| 354 | set_2g_low_power_param | 3503 | set | write 2g low power param |
| 355 | get_2g_low_power_param | 3503 | get | read 2g low power param |
| 356 | set_5g_low_power_param | 3505 | set | write 5g low power param |
| 357 | get_5g_low_power_param | 3505 | get | read 5g low power param |
| 358 | adjust_ppm | 3514 | set |  |
| 359 | xo_ppm_cali | 3515 | get |  |
| 360 | set_xo_ppm_cali_param | 3506 | set | write xo ppm cali param |
| 361 | get_xo_ppm_cali_param | 3506 | get | read xo ppm cali param |
| 362 | fem_check | 3522 | get |  |
| 363 | efuse_test | 3525 | get |  |
| 364 | rssi_cali | 3520 | set |  |
| 365 | rssi_cali_ant | 3521 | set |  |
| 366 | set_rssi_param | 3507 | set | write rssi param |
| 367 | get_rssi_param | 3507 | get | read rssi param |
| 368 | atf_mode | 3601 | set |  |
| 369 | get_atf_mode | 3601 | get | read atf mode |
| 370 | set_atf_vap_value | 3602 | set | write atf vap value |
| 371 | get_atf_vap_value | 3602 | get | read atf vap value |
| 372 | del_atf_vap_value | 3603 | set |  |
| 373 | set_atf_ac_value | 3604 | set | write atf ac value |
| 374 | get_atf_ac_value | 3604 | get | read atf ac value |
| 375 | del_atf_ac_value | 3605 | set |  |
| 376 | set_atf_user_value | 3606 | set | write atf user value |
| 377 | get_atf_user_value | 3606 | get | read atf user value |
| 378 | del_atf_user_value | 3607 | set |  |
| 379 | atf_min_prop | 3608 | set |  |
| 380 | get_atf_min_prop | 3608 | get | read atf min prop |
| 381 | atf_cycle_time | 3609 | set |  |
| 382 | get_atf_cycle_time | 3609 | get | read atf cycle time |
| 383 | get_atf_all_user_info | 3610 | get | read atf all user info |
| 384 | set_atf_debug_switch | 3611 | set | write atf debug switch |
| 385 | get_atf_debug_switch | 3611 | get | read atf debug switch |
| 386 | set_tx_empty_sch_en | 4151 | set | write tx empty sch en |
| 387 | get_tx_empty_sch_en | 4151 | get | read tx empty sch en |
| 388 | set_tx_empty_sch_num | 4152 | set | write tx empty sch num |
| 389 | get_tx_empty_sch_num | 4152 | get | read tx empty sch num |
| 390 | set_tx_empty_sch_empty_num | 4153 | set | write tx empty sch empty num |
| 391 | get_tx_empty_sch_empty_num | 4153 | get | read tx empty sch empty num |
| 392 | set_urp_sw | 3801 | set | write urp sw |
| 393 | get_urp_sw | 3801 | get | read urp sw |
| 394 | set_urp_replace_thd | 3802 | set | write urp replace thd |
| 395 | get_urp_replace_thd | 3802 | get | read urp replace thd |
| 396 | set_amsdu_debug | 3901 | set | write amsdu debug |
| 397 | set_cca_th | 302 | set | write cca th |
| 398 | get_cca_th | 302 | get | read cca th |
| 399 | set_ersru_dev_cap | 2801 | set | write ersru dev cap |
| 400 | get_ersru_dev_cap | 2801 | get | read ersru dev cap |
| 401 | set_ersru_user_enable | 2802 | set | write ersru user enable |
| 402 | get_ersru_user_enable | 2802 | get | read ersru user enable |
| 403 | get_ersru_user_state | 2803 | get | read ersru user state |
| 404 | tx_ant | 4201 | set |  |
| 405 | get_tx_ant | 4201 | get | read tx ant |
| 406 | set_ns_aggr_param | 4301 | set | write ns aggr param |
| 407 | get_ns_aggr_param | 4301 | get | read ns aggr param |
| 408 | set_ns_aggr_log_sw | 4302 | set | write ns aggr log sw |
| 409 | set_ns_rate_param | 4305 | set | write ns rate param |
| 410 | get_ns_rate_param | 4305 | get | read ns rate param |
| 411 | set_ns_rate_log_sw | 4306 | set | write ns rate log sw |
| 412 | set_ns_rate_protect_mode | 4307 | set | write ns rate protect mode |
| 413 | get_ns_rate_protect_mode | 4307 | get | read ns rate protect mode |

## 3. Categorized summary

Counts by `dir`:

| dir | count |
|---|---:|
| get (1) | 189 |
| set (0) | 225 |
| **total** | **414** |

Counts by name prefix:

| prefix | count |
|---|---:|
| `get_` | 184 |
| `set_` | 48 |
| other | 182 |
| **total** | **414** |

Cross-cutting details:

- `get_*` names: 184, all with `dir = get`. Of these, 14 also end in `_param`.
- `set_*` names: 48, all with `dir = set`.
- names ending in `_param`: 29 total, of which 14 start with `get_` (dir=get) and 15 do not (dir=set).
- `dir = get` names that neither start with `get_` nor end in `_param`: 5 (`save_2g_upc`, `save_5g_upc`, `xo_ppm_cali`, `fem_check`, `efuse_test`).
- distinct `cfg_id` values: 237; 177 of them appear twice (get/set pairs share the id).
- `flags` word values observed: `0x01010000` x359, `0x00000101` x47, `0x01010101` x7, `0x00010001` x1.

## 4. On-device probe results

Probe set: every table name whose `dir` marks it as a getter **and** that starts with
`get_` or ends in `_param` -> **184** names (exactly the 184 `get_*` names; all `_param`
entries are setter-side except those already named `get_..._param`).

Method: each command was issued as `iwpriv vap0 alg <name>`, wrapped in `timeout 3`,
over one read-only SSH session. Raw output below is the command output with newlines
collapsed to spaces; the leading `vap0      alg:` prompt prefix is preserved.
All 184 probes returned shell exit code 0 (no timeouts).

Result: **142 SUCC / 42 FAIL**.
Commands whose getter needs an argument (packet type / station id / param count) return
`[FAIL][Error]Invalid CMD input...`; commands the firmware could not serve return bare `[FAIL]`.

| # | literal command | observed output |
|---:|---|---|
| 1 | `iwpriv vap0 alg get_rate_mode` | `vap0      alg:[SUCC][iw]get_rate_mode: auto` |
| 2 | `iwpriv vap0 alg get_fec_coding` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 3 | `iwpriv vap0 alg get_mu_fec_coding` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 4 | `iwpriv vap0 alg get_protect_mode` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 5 | `iwpriv vap0 alg get_he_ltf_type` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 6 | `iwpriv vap0 alg get_dcm_enable` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 7 | `iwpriv vap0 alg get_freq_bw_mode` | `vap0      alg:[FAIL]` |
| 8 | `iwpriv vap0 alg get_debug_log_switch` | `vap0      alg:[SUCC][iw]debug_log_switch:disable!` |
| 9 | `iwpriv vap0 alg get_ar_dev_param` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 10 | `iwpriv vap0 alg get_sigb_dcm` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 11 | `iwpriv vap0 alg get_tx_cnt` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 12 | `iwpriv vap0 alg get_hi_rate` | `vap0      alg:[FAIL]` |
| 13 | `iwpriv vap0 alg get_he_sigb_rate` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 14 | `iwpriv vap0 alg get_mu_tx_cnt` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 15 | `iwpriv vap0 alg get_mu_rate` | `vap0      alg:[FAIL]` |
| 16 | `iwpriv vap0 alg get_mu_rts_mode` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 17 | `iwpriv vap0 alg get_legacy_rate` | `vap0      alg:[FAIL]` |
| 18 | `iwpriv vap0 alg get_preamble_type` | `vap0      alg:[FAIL][Error]Invalid CMD input, pkt type[13] invalid` |
| 19 | `iwpriv vap0 alg get_current_ration` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 20 | `iwpriv vap0 alg get_sch_mode` | `vap0      alg:[SUCC]sch_mode = auto` |
| 21 | `iwpriv vap0 alg get_max_pe` | `vap0      alg:[SUCC]max_pe = 16us` |
| 22 | `iwpriv vap0 alg get_su_sch_debug_switch` | `vap0      alg:[SUCC]cur debug switch[0]` |
| 23 | `iwpriv vap0 alg get_su_sch_mode` | `vap0      alg:[SUCC] cur mode [ATF] fix_flag[0], change_id[3]` |
| 24 | `iwpriv vap0 alg get_su_sch_rr_vi_delay` | `vap0      alg:[SUCC] cur sch_rr_vi_delay [0]` |
| 25 | `iwpriv vap0 alg get_ring_refresh_qtable_switch` | `vap0      alg:[SUCC]cur ring_refresh_qtable [0]` |
| 26 | `iwpriv vap0 alg get_ul_mumimo_enable` | `vap0      alg:[SUCC]ul_mumimo_enable = 1 ul_mumimo_enable set succ!` |
| 27 | `iwpriv vap0 alg get_ul_mumimo_dbg_log` | `vap0      alg:[SUCC]ul_mumimo_dbg_log = 0 ul_mumimo_dbg_log set succ!` |
| 28 | `iwpriv vap0 alg get_ul_mumimo_fix_rate` | `vap0      alg:[SUCC]ul_mumimo_fix_rate = 0 ul_mumimo_fix_rate set fail` |
| 29 | `iwpriv vap0 alg get_ul_mumimo_fix_txtime` | `vap0      alg:[SUCC]ul_mumimo_fix_txtime = 0 ul_mumimo_fix_txtime set fail` |
| 30 | `iwpriv vap0 alg get_ofdma_user_num` | `vap0      alg:[SUCC]ofdma_user_num = 16` |
| 31 | `iwpriv vap0 alg get_puncture_mode` | `vap0      alg:[SUCC]puncture_mode = 00000000` |
| 32 | `iwpriv vap0 alg get_ru_alloc` | `vap0      alg:[SUCC]ru_alloc = 0 0 0 0 0 0 0 0 ru_alloc_1ch = 0 0 0 0 ru_alloc_2ch = 0 0 0 0` |
| 33 | `iwpriv vap0 alg get_ofdma_mac_addr` | `vap0      alg:[SUCC]ofdma_user[0] = 00:00:00:00:xx:xx ofdma_user[1] = 00:00:00:00:xx:xx ofdma_user[2] = 00:00:00:00:xx:xx ofdma_user[3] = 00:00:00:00:xx:xx ofdma_user[4] = 00:00:00:00:xx:xx ofdma_user[5] = 00:00:00:00:xx:xx ofdma_user[6] = 00:00:00:00:xx:xx ofdma_user[7] = 00:00:00:00:xx:xx ofdma_user[8] = 00:00:00:00:xx:xx ofdma_user[9] = 00:00:00:00:xx:xx ofdma_user[10] = 00:00:00:00:xx:xx ofdma_user[11] = 00:00:00:00:xx:xx ofdma_user[12] = 00:00:00:00:xx:xx ofdma_user[13] = 00:00:00:00:xx:xx ofdma_user[14] = 00:00:00:00:xx:xx ofdma_user[15] = 00:00:00:00:xx:xx` |
| 34 | `iwpriv vap0 alg get_user_ru` | `vap0      alg:[SUCC]user_ru[0] = 0 user_ru[1] = 0 user_ru[2] = 0 user_ru[3] = 0 user_ru[4] = 0 user_ru[5] = 0 user_ru[6] = 0 user_ru[7] = 0 user_ru[8] = 0 user_ru[9] = 0 user_ru[10] = 0 user_ru[11] = 0 user_ru[12] = 0 user_ru[13] = 0 user_ru[14] = 0 user_ru[15] = 0` |
| 35 | `iwpriv vap0 alg get_ofdma_user_tid` | `vap0      alg:[SUCC]user[0].tid =  user[1].tid =  user[2].tid =  user[3].tid =  user[4].tid =  user[5].tid =  user[6].tid =  user[7].tid =  user[8].tid =  user[9].tid =  user[10].tid =  user[11].tid =  user[12].tid =  user[13].tid =  user[14].tid =  user[15].tid =` |
| 36 | `iwpriv vap0 alg get_ofdma_aggr_num` | `vap0      alg:[SUCC]user[0].aggr_num =  user[1].aggr_num =  user[2].aggr_num =  user[3].aggr_num =  user[4].aggr_num =  user[5].aggr_num =  user[6].aggr_num =  user[7].aggr_num =  user[8].aggr_num =  user[9].aggr_num =  user[10].aggr_num =  user[11].aggr_num =  user[12].aggr_num =  user[13].aggr_num =  user[14].aggr_num =  user[15].aggr_num =` |
| 37 | `iwpriv vap0 alg get_ofdma_aggr_size` | `vap0      alg:[SUCC]user[0].aggr_size =  user[1].aggr_size =  user[2].aggr_size =  user[3].aggr_size =  user[4].aggr_size =  user[5].aggr_size =  user[6].aggr_size =  user[7].aggr_size =  user[8].aggr_size =  user[9].aggr_size =  user[10].aggr_size =  user[11].aggr_size =  user[12].aggr_size =  user[13].aggr_size =  user[14].aggr_size =  user[15].aggr_size =` |
| 38 | `iwpriv vap0 alg get_ofdma_bw` | `vap0      alg:[SUCC]ofdma_bw = 160M` |
| 39 | `iwpriv vap0 alg get_ofdma_ltf_gi` | `vap0      alg:[SUCC]ofdma_ltf_gi = 4x_gi4` |
| 40 | `iwpriv vap0 alg get_ofdma_mu_seq_type` | `vap0      alg:[SUCC]ofdma_mu_seq_type = follow_mubar` |
| 41 | `iwpriv vap0 alg get_ofdma_dl_sch_enable` | `vap0      alg:[SUCC]ofdma_dl_sch_enable 1` |
| 42 | `iwpriv vap0 alg get_ofdma_loss_inc` | `vap0      alg:[SUCC]ofdma_loss_inc = 30` |
| 43 | `iwpriv vap0 alg get_ofdma_loss_dec` | `vap0      alg:[SUCC]ofdma_loss_dec = 5` |
| 44 | `iwpriv vap0 alg get_ofdma_auto_puncture` | `vap0      alg:[SUCC]ofdma_auto_puncture 0` |
| 45 | `iwpriv vap0 alg get_ofdma_dl_debug_enable` | `vap0      alg:[SUCC]ofdma_dl_debug_enable 0` |
| 46 | `iwpriv vap0 alg get_ofdma_certify_use_106_ru` | `vap0      alg:[SUCC]ofdma_ofdma_certify_use_106_ru_enable 0` |
| 47 | `iwpriv vap0 alg get_ofdma_backoff_time_th` | `vap0      alg:[SUCC]ofdma_backoff_time_th = 300` |
| 48 | `iwpriv vap0 alg get_ofdma_pk_mode_enable` | `vap0      alg:[SUCC]ofdma_pk_mode_enable 0` |
| 49 | `iwpriv vap0 alg get_ofdma_seq_user_num_thrd` | `vap0      alg:[SUCC]seq_chosen_user_num_thrd = 17` |
| 50 | `iwpriv vap0 alg get_ofdma_sch_cnt_th` | `vap0      alg:[SUCC]ofdma_sch_cnt_th 1` |
| 51 | `iwpriv vap0 alg get_ul_ofdma_enable` | `vap0      alg:[SUCC]` |
| 52 | `iwpriv vap0 alg get_edca_prot_mode_opt` | `vap0      alg:[SUCC]prot_mode[2], fix_protect_mode[1]` |
| 53 | `iwpriv vap0 alg get_rts_on_collision_th` | `vap0      alg:[SUCC]rts_on_collision_th[110].` |
| 54 | `iwpriv vap0 alg get_nav_duration` | `vap0      alg:[SUCC]rx_nav_duration_th[32767].` |
| 55 | `iwpriv vap0 alg get_rts_off_fail_ratio_th` | `vap0      alg:[SUCC]rts_off_fail_ratio_th[80].` |
| 56 | `iwpriv vap0 alg get_mu_rts_opt_mode` | `vap0      alg:[SUCC]mu_rts_opt_mode[1].` |
| 57 | `iwpriv vap0 alg get_edca_opt_rssi_th` | `vap0      alg:[SUCC]edca_opt_rssi_th[-127].` |
| 58 | `iwpriv vap0 alg get_edca_expand_probe` | `vap0      alg:[SUCC]edca_opt_expand_probe[0].` |
| 59 | `iwpriv vap0 alg get_edca_opt_en_ap` | `vap0      alg:[SUCC]edca_opt_en[2]` |
| 60 | `iwpriv vap0 alg get_al_tx_info` | `vap0      alg:[SUCC]al_tx_flag[0] interval[0] payload_flag[0] payload_len[0]` |
| 61 | `iwpriv vap0 alg get_al_rx_info` | `vap0      alg:[SUCC]sw[0] ppdu[0] normal[0] ampdu[0] delim[0] fcs[0] hesu_ok[14] hemu_ok[0] heext_ok[0] hetrig_ok[0] dotb_ok[5242] ht_ok[522] vht_ok[178] lega_ok[2268] hesu_err[0] hemu_err[0] heext_err[0] hetrig_err[0] dotb_err[1302] ht_err[262] vht_err[68] lega_err[2879] hesu_freq_err[0] rssi_ant0[0] rssi_ant1[0] evm_ss0[0] evm_ss1[0] lna_c0[0] lna_c1[0] vga_c0[0] vga_c1[0] evm_ss0_max_idx[0] evm_ss1_max_idx[0]` |
| 62 | `iwpriv vap0 alg get_al_tx_single_tone` | `vap0      alg:[SUCC]sw[0] chain[0] freq[0] amp[0]` |
| 63 | `iwpriv vap0 alg get_tpc_mode` | `vap0      alg:[SUCC]tpc_mode = 1` |
| 64 | `iwpriv vap0 alg get_tpc_pow_lvl` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 65 | `iwpriv vap0 alg get_tpc_code` | `vap0      alg:[FAIL][Error]get_tpc_code, uc_param_num(0) should be 2!` |
| 66 | `iwpriv vap0 alg get_pow_mode` | `vap0      alg:[SUCC]pow_mode = init` |
| 67 | `iwpriv vap0 alg get_far_dist_switch` | `vap0      alg:[SUCC]far_dist_pow_switch[0] = 0` |
| 68 | `iwpriv vap0 alg get_near_dist_switch` | `vap0      alg:[SUCC]near_dist_pow_switch[0] = 1` |
| 69 | `iwpriv vap0 alg get_resp_chain_sel` | `vap0      alg:[SUCC]resp_chain_sel = 1` |
| 70 | `iwpriv vap0 alg get_waterfilling_mode` | `vap0      alg:[SUCC]wf_mode = 1` |
| 71 | `iwpriv vap0 alg get_temp_limit` | `vap0      alg:[SUCC]temp_limit = 1` |
| 72 | `iwpriv vap0 alg get_temp_log` | `vap0      alg:[SUCC]temp_log = 0` |
| 73 | `iwpriv vap0 alg get_temp_state` | `vap0      alg:[SUCC] band=0 temperature=58 state=safe policy=0x0  policy: rf_off[0], pwr_reduce[0], dutu_cys_reduce[0]` |
| 74 | `iwpriv vap0 alg get_temp_comp` | `vap0      alg:[SUCC]temp_comp = 1` |
| 75 | `iwpriv vap0 alg get_spectral_scan_en` | `vap0      alg:[SUCC]spectral_scan : 0` |
| 76 | `iwpriv vap0 alg get_spectral_ofdm_det` | `vap0      alg:[SUCC]spectral_ofdm_det : 1` |
| 77 | `iwpriv vap0 alg get_spectral_11b_det` | `vap0      alg:[SUCC]spectral_11b_det : 1` |
| 78 | `iwpriv vap0 alg get_spectral_nb_det` | `vap0      alg:[SUCC]spectral_nb_det : 0` |
| 79 | `iwpriv vap0 alg get_fft_size` | `vap0      alg:[SUCC]fft_size : 2` |
| 80 | `iwpriv vap0 alg get_fft_period` | `vap0      alg:[SUCC]fft_period : 40` |
| 81 | `iwpriv vap0 alg get_fft_count` | `vap0      alg:[SUCC]fft_count : 2` |
| 82 | `iwpriv vap0 alg get_spectral_rssi_thr` | `vap0      alg:[SUCC]spectral_rssi_thr : -85` |
| 83 | `iwpriv vap0 alg get_spectral_rssi_nb_thr` | `vap0      alg:[SUCC]spectral_rssi_nb_thr : -70` |
| 84 | `iwpriv vap0 alg get_spectral_power_thr` | `vap0      alg:[SUCC]spectral_power_thr : 12` |
| 85 | `iwpriv vap0 alg get_nb_thr` | `vap0      alg:[SUCC]nb_thr : 12` |
| 86 | `iwpriv vap0 alg get_rpt_mode` | `vap0      alg:[SUCC]rpt_mode : 1` |
| 87 | `iwpriv vap0 alg get_fftin_type` | `vap0      alg:[SUCC]fftin_type : 1` |
| 88 | `iwpriv vap0 alg get_ant_index` | `vap0      alg:[SUCC]ant_index : 0` |
| 89 | `iwpriv vap0 alg get_deci_coef_en` | `vap0      alg:[SUCC]deci_coef_en : 0` |
| 90 | `iwpriv vap0 alg get_deci_coef_man` | `vap0      alg:[SUCC]deci_coef_man : 0` |
| 91 | `iwpriv vap0 alg get_data_abnormal` | `vap0      alg:[SUCC]datain_abnormal : 0 dataout_abnormal : 0` |
| 92 | `iwpriv vap0 alg get_agc_lock_en` | `vap0      alg:[SUCC]agc_lock_en : 1` |
| 93 | `iwpriv vap0 alg get_csi_en` | `vap0      alg:[SUCC]csi_en : 0` |
| 94 | `iwpriv vap0 alg get_csi_location_vap` | `vap0      alg:[SUCC]csi_location_vap : 0` |
| 95 | `iwpriv vap0 alg get_csi_frame_type` | `vap0      alg:[SUCC]csi_frame_type : 0` |
| 96 | `iwpriv vap0 alg get_csi_band_width` | `vap0      alg:[SUCC]csi_band_width : 0` |
| 97 | `iwpriv vap0 alg get_csi_whitelist` | `vap0      alg:[SUCC]csi_whitelist_num:[0].` |
| 98 | `iwpriv vap0 alg get_csi_resp_rpt_flag` | `vap0      alg:[FAIL][Error]get resp_rpt_flag, uc_param_num(0) should be 1!` |
| 99 | `iwpriv vap0 alg get_trigger_fill_mode` | `vap0      alg:[SUCC]get_trigger_fill_mode = auto_sch` |
| 100 | `iwpriv vap0 alg get_trigger_basic_trg_sw` | `vap0      alg:[SUCC]get_trigger_basic_trg_sw = enable` |
| 101 | `iwpriv vap0 alg get_trigger_bsrp_trg_sw` | `vap0      alg:[SUCC]get_trigger_bsrp_trg_sw = disable` |
| 102 | `iwpriv vap0 alg get_trigger_aggr_basic_trg_ra_addr_sw` | `vap0      alg:[SUCC]get_trigger_aggr_basic_trg_ra_addr_sw = disable` |
| 103 | `iwpriv vap0 alg get_trigger_tb_ppdu_ofdma_ba_sw` | `vap0      alg:[SUCC]get_trigger_tb_ppdu_ofdma_ba_sw = disable` |
| 104 | `iwpriv vap0 alg get_trigger_comm1` | `vap0      alg:[SUCC]get_trigger_comm1 = bw[20M], gi_ltf[1x], stbc[disable], power[0]` |
| 105 | `iwpriv vap0 alg get_trigger_comm2_fix` | `vap0      alg:[SUCC]get_trigger_comm2 = length[0], ldpc_ext[0], packet_ext[0]` |
| 106 | `iwpriv vap0 alg get_trigger_comm2_calc` | `vap0      alg:[SUCC]get_trigger_comm2 = length[0], ldpc_ext[0], packet_ext[0]` |
| 107 | `iwpriv vap0 alg get_trigger_user` | `vap0      alg:[SUCC]get_trigger_user = idx[0], fec[bcc], mcs[mcs0], dcm[disable], ss[1], rssi[0]` |
| 108 | `iwpriv vap0 alg get_trigger_user_var` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 109 | `iwpriv vap0 alg get_ulsch_dev_sch_mode` | `vap0      alg:[SUCC]get dev_sch_mode,  sch_mode = [auto],` |
| 110 | `iwpriv vap0 alg get_ulsch_dev_tx_trigger_cycle` | `vap0      alg:[SUCC]get dev_tx_trigger_cycle, trigger_cycle = [0]` |
| 111 | `iwpriv vap0 alg get_ulsch_dev_interval_param` | `vap0      alg:[SUCC]get dev_interval_param, interval = [65535], interval_step = [1], inteval_min = [65535], interval_max = [65535].` |
| 112 | `iwpriv vap0 alg get_ulsch_dev_twt_sch_interval` | `vap0      alg:[SUCC]get twt_sch_interval, twt_sch_interval = [2], cnt [0]` |
| 113 | `iwpriv vap0 alg get_ulsch_dev_bsrp_trigger_param` | `vap0      alg:[SUCC]get bsrp_trigger_param, bsrp_trigger_cycle_ms = [100], bsrp_expired_time_ms = [200], bsrp_query_bytes_thrsh = [100000], bsrp_trigger_queue_state = [0],` |
| 114 | `iwpriv vap0 alg get_ul_sch_sw` | `vap0      alg:[SUCC]get_ul_sch_sw = alg_sw[disable], alg_basic_trig_log_sw[enable], alg_su_log_sw[disable] , alg_ul_sch_bypass[disable]` |
| 115 | `iwpriv vap0 alg get_ul_sch_cycle` | `vap0      alg:[SUCC]get_ul_sch_cycle = basic_trig_cycle[1000], basic_trig_probe_interval[80] , bebk_can_ul_sch_min_mpdu_num[25] , bebk_can_ul_sch_min_user_num[4]` |
| 116 | `iwpriv vap0 alg get_ul_sch_user` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 117 | `iwpriv vap0 alg get_ulsch_user_in_bsr_list` | `vap0      alg:[FAIL][Error]config in_bsr_list,param_num(0) should >= 1!` |
| 118 | `iwpriv vap0 alg get_ulsch_user_twt_enable` | `vap0      alg:[FAIL][Error]config twt_enable, uc_param_num(0) should >= 1!` |
| 119 | `iwpriv vap0 alg get_txmode_mode_sw` | `vap0      alg:[SUCC]get_txmode_mode_sw = enable` |
| 120 | `iwpriv vap0 alg get_txmode_user_fix_mode` | `vap0      alg:[FAIL][Error]config get_txmode_user_fix_mode, uc_param_num(0) should be 1!` |
| 121 | `iwpriv vap0 alg get_txmode_user_fix_chain` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 122 | `iwpriv vap0 alg get_txmode_mgmt_chain` | `vap0      alg:[SUCC]get_mgmt_tx_chain = 1` |
| 123 | `iwpriv vap0 alg get_txmode_11b_tx_sw` | `vap0      alg:[SUCC]get_txmode_11b_tx_sw = enable` |
| 124 | `iwpriv vap0 alg get_txmode_bcast_data_chain` | `vap0      alg:[SUCC]get_bcast_data_tx_chain = 0` |
| 125 | `iwpriv vap0 alg get_txmode_debug_log_switch` | `vap0      alg:[SUCC][iw]get_txmode_debug_log_switch:disable!` |
| 126 | `iwpriv vap0 alg get_ack_opt` | `vap0      alg:[SUCC]trigger_debug[0], trigger_ack_enable[0], trigger_tx_time[300]` |
| 127 | `iwpriv vap0 alg get_aggr_mode` | `vap0      alg:[SUCC]get_su_aggr_time_mode = alg_aggr_mode = [auto]` |
| 128 | `iwpriv vap0 alg get_su_aggr_time` | `vap0      alg:[SUCC]su_aggr_time = [4000]` |
| 129 | `iwpriv vap0 alg get_su_aggr_log` | `vap0      alg:[SUCC]su_alg_aggr_log =  alg_aggr_log = [disable],` |
| 130 | `iwpriv vap0 alg get_su_aggr_probe_limit` | `vap0      alg:[SUCC]probe_aggr_en = [0] probe_aggr_limit = [0]` |
| 131 | `iwpriv vap0 alg get_su_aggr_sw` | `vap0      alg:[SUCC]su_alg_aggr_sw =  alg_aggr_sw = [enable],` |
| 132 | `iwpriv vap0 alg get_mu_aggr_mode` | `vap0      alg:[SUCC]mu_aggr_time_sw =  alg_mu_aggr_mode = [auto],` |
| 133 | `iwpriv vap0 alg get_mu_aggr_time` | `vap0      alg:[SUCC]mu_aggr_time = [0]` |
| 134 | `iwpriv vap0 alg get_sounding_sch_mode` | `vap0      alg:[SUCC]get_sounding_sch_mode = auto auto` |
| 135 | `iwpriv vap0 alg get_sounding_period_group` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 136 | `iwpriv vap0 alg get_sounding_group_property` | `vap0      alg:[FAIL][Error]config get_sounding_group_property, uc_param_num(0) should be 2!` |
| 137 | `iwpriv vap0 alg get_sounding_user_codebook_size` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 138 | `iwpriv vap0 alg get_sounding_user_grouping` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 139 | `iwpriv vap0 alg get_sounding_user_fback_type` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 140 | `iwpriv vap0 alg get_sounding_grp_info` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 141 | `iwpriv vap0 alg get_sounding_trig_cnt` | `vap0      alg:[SUCC]get_sounding_trig_cnt = 125706` |
| 142 | `iwpriv vap0 alg get_sounding_norm_log_sw` | `vap0      alg:[SUCC]get_sounding_norm_log_sw = disable` |
| 143 | `iwpriv vap0 alg get_sounding_rssi_limit` | `vap0      alg:[SUCC]get_sounding_rssi_limit: diasble_rssi[127] recover_offset[0]` |
| 144 | `iwpriv vap0 alg get_smartant_mode` | `vap0      alg:[SUCC]mode[disable]` |
| 145 | `iwpriv vap0 alg get_fix_ant` | `vap0      alg:[FAIL]` |
| 146 | `iwpriv vap0 alg get_train_period` | `vap0      alg:[SUCC]train period[0]s` |
| 147 | `iwpriv vap0 alg get_smartant_debug` | `vap0      alg:[SUCC]debug[0]` |
| 148 | `iwpriv vap0 alg get_smartant_train_th` | `vap0      alg:[SUCC]train_th[0]` |
| 149 | `iwpriv vap0 alg get_smartant_su_rx_pk` | `vap0      alg:[SUCC]su_rx_pk[0]` |
| 150 | `iwpriv vap0 alg get_scen_dbg_en` | `vap0      alg:[SUCC]scen_dbg_en = 0` |
| 151 | `iwpriv vap0 alg get_xo_ducy_cali_param` | `vap0      alg:[SUCC]000016da` |
| 152 | `iwpriv vap0 alg get_2g_all_curve_param` | `vap0      alg:[SUCC]feda0000 083c0556 fe670000 077105e9 0a73f50a 101dfd31 fa21036b 03fc0965 ec1f1283 fde611d9 dd541ffa f3a01c89` |
| 153 | `iwpriv vap0 alg get_5g_all_curve_param` | `vap0      alg:[SUCC]78420492 2df042d7 0aef046a 390417b4 f7470402 1f580e0c fb7f018f 20390bd2 fbb50142 22d20bc7 fa28022e 1f210cbf f2dc067d 1c39108d f60004a7 1c4e0ef1 f40005b3 1a871027 f39105be 17c010b2 efe1087c 19cb11dd f26006f0 1aaa1095 f1da07c7 1bff1063 f0df080d 1a5c112f 381d0000 4309f111 1d490000 3c780f13` |
| 154 | `iwpriv vap0 alg get_2g_curve_factor` | `vap0      alg:[SUCC]00060e17` |
| 155 | `iwpriv vap0 alg get_5g_curve_factor` | `vap0      alg:[SUCC]02070f17` |
| 156 | `iwpriv vap0 alg get_2g_upc` | `vap0      alg:[SUCC]00000148 0000013e 0000012a 0000014a 00000140 00000137` |
| 157 | `iwpriv vap0 alg get_5g_upc` | `vap0      alg:[SUCC]00000072 00000074 00000081 00000068 00000058 00000059 00000059 00000076 00000057 0000007a 00000073 0000006f 0000006d 0000007a 00000082 00000078 00000075 00000073` |
| 158 | `iwpriv vap0 alg get_2g_power_param` | `vap0      alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff` |
| 159 | `iwpriv vap0 alg get_5g_power_param` | `vap0      alg:[SUCC]00000000 0004ff00 0801000b ff000b00 0009fd02 09000904 0009fe00 0c030009 00000904 00000000 160f140f 080c0500 0c060016 0f001a08 001a1617 1a151813 151b1600 0000001a` |
| 160 | `iwpriv vap0 alg get_2g_low_power_param` | `vap0      alg:[SUCC]08080814 08080814 08080814 08080714 08080714 08080613 08080613 08080613 08080613 08080612 08080612 08080612 08080612 0c080814 0c080814 0c080814 0b070713 0b070713 0b070712 0b070712 0b070711 0a060611 0a060611 0a060610 0a060610 0a060610` |
| 161 | `iwpriv vap0 alg get_5g_low_power_param` | `vap0      alg:[SUCC]00000000 000cff00 0801000a fd000a00 0006fd02 10000bfd 000bfb00 0c030006 00000604 00000000 160e140f 080c0500 0c060016 0f001a0b 001a1617 1a1a1d16 151e0d00 0000001a` |
| 162 | `iwpriv vap0 alg get_xo_ppm_cali_param` | `vap0      alg:[SUCC]00006060` |
| 163 | `iwpriv vap0 alg get_rssi_param` | `vap0      alg:[SUCC]00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000` |
| 164 | `iwpriv vap0 alg get_atf_mode` | `vap0      alg:[SUCC]atf_mode = 2` |
| 165 | `iwpriv vap0 alg get_atf_vap_value` | `vap0      alg:[SUCC]vap set prop= 0` |
| 166 | `iwpriv vap0 alg get_atf_ac_value` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 167 | `iwpriv vap0 alg get_atf_user_value` | `vap0      alg:[FAIL][Error]Invalid CMD input, param num[0] invalid` |
| 168 | `iwpriv vap0 alg get_atf_min_prop` | `vap0      alg:[SUCC]min prop = 1` |
| 169 | `iwpriv vap0 alg get_atf_cycle_time` | `vap0      alg:[SUCC]cycle_time_ms = 1000` |
| 170 | `iwpriv vap0 alg get_atf_all_user_info` | `vap0      alg:[SUCC]  MAC/Real Percentage/Config Percentage/Alloc Time(ms) default[0]:      0     | 0     | 0` |
| 171 | `iwpriv vap0 alg get_atf_debug_switch` | `vap0      alg:[SUCC]debug switch info[0]` |
| 172 | `iwpriv vap0 alg get_tx_empty_sch_en` | `vap0      alg:[FAIL]` |
| 173 | `iwpriv vap0 alg get_tx_empty_sch_num` | `vap0      alg:[FAIL]` |
| 174 | `iwpriv vap0 alg get_tx_empty_sch_empty_num` | `vap0      alg:[FAIL]` |
| 175 | `iwpriv vap0 alg get_urp_sw` | `vap0      alg:[SUCC]get_urp_sw = alg_sw[enable], replace_sw[enable], log_sw[disable]` |
| 176 | `iwpriv vap0 alg get_urp_replace_thd` | `vap0      alg:[SUCC]get_urp_replace_thd = 63` |
| 177 | `iwpriv vap0 alg get_cca_th` | `vap0      alg:[SUCC]ed_high_20th = [-57], ed_high_40th = [-59], ed_high_80th = [-56]` |
| 178 | `iwpriv vap0 alg get_ersru_dev_cap` | `vap0      alg:[SUCC]get cap, cap = [1],` |
| 179 | `iwpriv vap0 alg get_ersru_user_enable` | `vap0      alg:[FAIL][Error]config er_su_enable, uc_param_num(0) should >= 1!` |
| 180 | `iwpriv vap0 alg get_ersru_user_state` | `vap0      alg:[FAIL][Error]config er_su_state, uc_param_num(0) should >= 1!` |
| 181 | `iwpriv vap0 alg get_tx_ant` | `vap0      alg:[SUCC]tx_ant_mask[0]` |
| 182 | `iwpriv vap0 alg get_ns_aggr_param` | `vap0      alg:[FAIL][Error]get ns max_aggr, param_num[0] should be 1!` |
| 183 | `iwpriv vap0 alg get_ns_rate_param` | `vap0      alg:[FAIL][Error]get ns max_rate, param_num[0] should be 1!` |
| 184 | `iwpriv vap0 alg get_ns_rate_protect_mode` | `vap0      alg:[SUCC]ns_protect_mode_cfg: rotect_mode:none` |

### Probe failures grouped by reason

| reason | count | names |
|---|---:|---|
| [Error]Invalid CMD input, param num[0] invalid | 16 | `get_mu_fec_coding`, `get_he_ltf_type`, `get_dcm_enable`, `get_ar_dev_param`, `get_current_ration`, `get_tpc_pow_lvl`, `get_trigger_user_var`, `get_ul_sch_user`, `get_txmode_user_fix_chain`, `get_sounding_period_group`, `get_sounding_user_codebook_size`, `get_sounding_user_grouping`, `get_sounding_user_fback_type`, `get_sounding_grp_info`, `get_atf_ac_value`, `get_atf_user_value` |
| [Error]Invalid CMD input, pkt type[13] invalid | 8 | `get_fec_coding`, `get_protect_mode`, `get_sigb_dcm`, `get_tx_cnt`, `get_he_sigb_rate`, `get_mu_tx_cnt`, `get_mu_rts_mode`, `get_preamble_type` |
| (bare [FAIL]) | 8 | `get_freq_bw_mode`, `get_hi_rate`, `get_mu_rate`, `get_legacy_rate`, `get_fix_ant`, `get_tx_empty_sch_en`, `get_tx_empty_sch_num`, `get_tx_empty_sch_empty_num` |
| [Error]get_tpc_code, uc_param_num(0) should be 2! | 1 | `get_tpc_code` |
| [Error]get resp_rpt_flag, uc_param_num(0) should be 1! | 1 | `get_csi_resp_rpt_flag` |
| [Error]config in_bsr_list,param_num(0) should >= 1! | 1 | `get_ulsch_user_in_bsr_list` |
| [Error]config twt_enable, uc_param_num(0) should >= 1! | 1 | `get_ulsch_user_twt_enable` |
| [Error]config get_txmode_user_fix_mode, uc_param_num(0) should be 1! | 1 | `get_txmode_user_fix_mode` |
| [Error]config get_sounding_group_property, uc_param_num(0) should be 2! | 1 | `get_sounding_group_property` |
| [Error]config er_su_enable, uc_param_num(0) should >= 1! | 1 | `get_ersru_user_enable` |
| [Error]config er_su_state, uc_param_num(0) should >= 1! | 1 | `get_ersru_user_state` |
| [Error]get ns max_aggr, param_num[0] should be 1! | 1 | `get_ns_aggr_param` |
| [Error]get ns max_rate, param_num[0] should be 1! | 1 | `get_ns_rate_param` |

## 5. Names that could not be resolved

**Table extraction: none.** All 414/414 `name` pointers resolved to readable
NUL-terminated strings in `.rodata.str1.4`; no empty, null, or out-of-range names.

**On-device getter probes: 42 of 184 could not be resolved** (the firmware returned
`[FAIL]`). These are not name-resolution failures: the command exists in the table but
the firmware rejected the call. Breakdown:

- [Error]Invalid CMD input, param num[0] invalid: `get_mu_fec_coding`, `get_he_ltf_type`, `get_dcm_enable`, `get_ar_dev_param`, `get_current_ration`, `get_tpc_pow_lvl`, `get_trigger_user_var`, `get_ul_sch_user`, `get_txmode_user_fix_chain`, `get_sounding_period_group`, `get_sounding_user_codebook_size`, `get_sounding_user_grouping`, `get_sounding_user_fback_type`, `get_sounding_grp_info`, `get_atf_ac_value`, `get_atf_user_value`
- [Error]Invalid CMD input, pkt type[13] invalid: `get_fec_coding`, `get_protect_mode`, `get_sigb_dcm`, `get_tx_cnt`, `get_he_sigb_rate`, `get_mu_tx_cnt`, `get_mu_rts_mode`, `get_preamble_type`
- bare `[FAIL]`: `get_freq_bw_mode`, `get_hi_rate`, `get_mu_rate`, `get_legacy_rate`, `get_fix_ant`, `get_tx_empty_sch_en`, `get_tx_empty_sch_num`, `get_tx_empty_sch_empty_num`
- [Error]get_tpc_code, uc_param_num(0) should be 2!: `get_tpc_code`
- [Error]get resp_rpt_flag, uc_param_num(0) should be 1!: `get_csi_resp_rpt_flag`
- [Error]config in_bsr_list,param_num(0) should >= 1!: `get_ulsch_user_in_bsr_list`
- [Error]config twt_enable, uc_param_num(0) should >= 1!: `get_ulsch_user_twt_enable`
- [Error]config get_txmode_user_fix_mode, uc_param_num(0) should be 1!: `get_txmode_user_fix_mode`
- [Error]config get_sounding_group_property, uc_param_num(0) should be 2!: `get_sounding_group_property`
- [Error]config er_su_enable, uc_param_num(0) should >= 1!: `get_ersru_user_enable`
- [Error]config er_su_state, uc_param_num(0) should >= 1!: `get_ersru_user_state`
- [Error]get ns max_aggr, param_num[0] should be 1!: `get_ns_aggr_param`
- [Error]get ns max_rate, param_num[0] should be 1!: `get_ns_rate_param`

**Not probed (outside the requested filter):** the 5 `dir = get` names that neither
start with `get_` nor end in `_param` were deliberately skipped: `save_2g_upc`, `save_5g_upc`, `xo_ppm_cali`, `fem_check`, `efuse_test`.

