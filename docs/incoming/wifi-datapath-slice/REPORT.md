# Wi-Fi data-path slice (hdpp_* / alg_*) - REPORT

**Lane:** wifi-datapath-slice (host-only). **Date:** 2026-11-22.
**Deliverables:** this file, SUMMARY.md, mapping/ (4 CSVs), scaffolding/ (2 headers,
2 C skeletons, Kbuild, BUILD-NOTES.md), tools/gen-mapping.py.
**Commit:** submodule `opensource`; hash in SUMMARY.md.

---

## 1. Scope and rules

This slice covers the two remaining chip-specific families of the vendor
`hi5622v100_wifi.ko` that the framework census flagged as ko-only:

| family | functions | bytes | vendor-exported names |
| --- | --- | --- | --- |
| `hdpp_` (host data-path engine) | 238 | 52,428 | 46 |
| `alg_` (algorithm-config command layer) | 239 | 103,476 | 6 functions + 8 `g_alg_*` data |

The semantic twin of `hdpp_` is the `dpe_` data-path engine of the hi1105
tree (hongmengkernel mirror, path
kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi).  That mirror
carries **no license grant** and is used as a **name/structure reference
only**: no text from it is vendored into this repository.  Everything
committed here is original text (mapping metadata, scaffolding C, docs)
derived from the decoded contracts.  The reference clone lives in
/home/shibbityshwab/hongmengkernel-ref (sparse, depth 1) and is not committed.

## 2. Inputs

- Vendor binaries (local, gitignored, NOT committed):
  /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko
  and hi5622v100_plat.ko.
- Census: opensource/docs/incoming/wifi-framework-port/census/ (family counts),
  opensource/docs/incoming/wifi-transport-port/census/family-sizes.csv (byte sums).
- Prior contracts: wifi-driver-re/REPORT.md (dispatch, 39-op map, the hdpp
  g_hdpp register-tab note in its section 4), wifi-transport-port/ (rings,
  mailboxes, H2D/D2H).
- Reference tree: hi1105 wifi subtree (373 .c files; the dpe engine sits in
  dpe/ with 50 .c files across adapter/dfx/features/frame/frw/hal/hcc/main/
  resource/rx/thread/tx; the alg twin file is wal/wal_config_alg_cfg.c).

## 3. Method (evidence chain)

1. **Symbol tables:** nm -S --size-sort on the vendor .ko (ARM EABI5, not
   stripped).  Confirms the census counts: 238 hdpp_ + 239 alg_ functions;
   all of them LOCAL symbols - the vendor build has zero global functions.
2. **Export surface:** the vendor ko exports through __ksymtab (1,162
   entries) even though every function symbol is local; the export NAMES
   live in __ksymtab_strings (dumped with readelf; 778 unique names, of
   which 46 hdpp_ + 6 alg_ + 8 g_alg_ = the 60-name hdpp/alg public
   surface).  Cross-checked against the consumers: wifi_debug.ko imports 62
   hdpp_/alg_ names.
3. **Call graph:** the vendor .ko's ELF header has e_machine=0, which
   defeats native objdump; after patching a local copy's e_machine to
   EM_ARM, arm-openwrt-linux-muslgnueabi-objdump -dr produced 437,446
   disassembly lines; parsing every bl/blx yielded 27,255 resolved edges
   across 3,204 functions (callgraph.json in the local workdir).  108 hdpp_
   and 64 alg_ symbols are direct call targets.
4. **Reference function inventory:** a parser over the hi1105 tree extracted
   264 dpe_ and 46 alg_ function DEFINITIONS with return-kind, parameter
   count and pointer/scalar parameter kinds (parse-funcs.py in the local
   workdir).  A text-level call graph of the same tree (7,576 functions)
   supports the caller/callee correlation.
5. **Mapping + arity:** exact-name matching, then caller/callee correlation
   with a semantic filter that ignores generic helpers (oam logs, memset,
   printk, spin/wait plumbing).  For every hdpp/alg symbol with an internal
   call site, a backward data-flow scan of the 14 instructions before each
   bl measured the written register set (arity) and whether r0 is consumed
   after return (int-return flag): evidence for 174/477 symbols.

## 4. Mapping results

### 4.1 hdpp_ -> dpe_

| metric | value |
| --- | --- |
| exact-name twins (hdpp_x == dpe_x) | **3 of 238 (1.3%)** = 532 B of 52,428 B (1.0%) |
| call-correlated twin | 1 (hdpp_user_sync_write <-> dpe_get_tid_ba_info, shared caller hmac_ba_reset_rx_handle) |
| remaining | 234 (no dpe name twin) |

Exact twins:

| vendor symbol | dpe twin | twin file (reference) | twin shape |
| --- | --- | --- | --- |
| hdpp_main_init | dpe_main_init | dpe/main/dpe_main.c | (buf*, buf_len) -> int |
| hdpp_main_exit | dpe_main_exit | dpe/main/dpe_main.c | () -> void |
| hdpp_vap_find_user_by_macaddr | dpe_vap_find_user_by_macaddr | dpe/resource/dpe_wlan_user.c | (vap_id, mac*, user_idx*) -> int |

**Reading:** hdpp and dpe are the same engine in role - the host-side data
plane between hmac and the device rings - but the naming fully diverged
between chip generations.  hi5622 organises the engine as a band/device/
vap/user object tree plus a stat/dfx subsystem (hdpp_stat_*, hdpp_sync_*,
hdpp_*_get_by_id); hi1105 reorganised the same role into config_* sync
functions, ring/thread management (dpe_rx_data_thread, dpe_frw_thread) and
dfx (dpe_wlan_stat).  Function-level name correspondence is therefore
essentially zero; the twin relationship is family-level and call-site-level
(the same hmac/wal layers call into both).  Full per-symbol table:
mapping/hdpp-to-dpe.csv; inventory with sizes and arity evidence:
mapping/hdpp-inventory.csv.
### 4.2 alg_ -> alg_

| metric | value |
| --- | --- |
| exact-name twins | **17 of 239 (7.1%)** = 5,776 B of 103,476 B (5.6%) |
| same-subfamily (weak signal) | 51 (parser/formatter names present in the reference under different cfg ids) |
| no twin | 171 (hi5622-specific cfg ids: ersru, trigger, spectral, smartant, ulsch, ...) |

The 17 exact twins are the generic core of the algorithm-config command
layer (alg_cfg_analysis_args{, _head}, search_process_info_by_{cfg_id,cfg_name}{,_normal},
get_cmd_one_arg, get_match_value, get_pkt_type, param_get_tbl_size, and the
txmode/waterfilling/hardamard args_analysis parsers) - all in
wal/wal_config_alg_cfg.c of the reference.  The reference keeps both an
args_analysis parser family and a param_analyze family; the vendor binary
has the args_analysis side plus hi5622-specific "process" handlers
(alg_cfg_param_analyze_*_process) and 37 *config_param_output_entry
formatters.  Full table: mapping/alg-to-twin.csv.

### 4.3 Combined coverage

| | hdpp | alg | total |
| --- | --- | --- | --- |
| functions | 238 | 239 | 477 |
| bytes | 52,428 | 103,476 | 155,904 |
| exact-name twins | 3 (1.3%) | 17 (7.1%) | 20 (4.2%) |
| exact twin bytes | 532 (1.0%) | 5,776 (5.6%) | 6,308 (4.0%) |

Exported surface: 0 of the 46 exported hdpp_stat_* dfx names have a dpe
twin; 2 of the 6 exported alg_ functions have exact twins
(alg_cfg_get_match_value, alg_cfg_param_get_tbl_size).  Machine-readable
summary: mapping/coverage.csv.

## 5. Behaviour annotation (top symbols by size)

Annotations below combine (a) the binary call graph, (b) the reference
architecture, (c) the prior transport/driver-re contracts.  "Probe" marks
what only the bench can decide (see section 6).

### 5.1 hdpp_ top by size

| symbol | B | what it does | evidence | probe |
| --- | --- | --- | --- | --- |
| hdpp_tx_msdu_stat_update | 1256 | per-band/vap/user TX MSDU counter update on TX ring post; resolves device/vap by id, bumps oal stat counters, feeds shuangta dscr-info counters | caller hmac_tx_msdu_ring_post_proc; callees hdpp_device_get_by_id, hdpp_vap_get_by_id, oal_stat_inc_num, shuangta_get_tx_msdu_dscr_info_cnt | counter layout |
| hdpp_rx_msdu_stat_update | 940 | RX counterpart from the RX ring frame processing | caller hmac_rx_ring_entry_proc_frame; device/user/vap lookups | counter layout |
| hdpp_stat_dump_{rx,tx}_ppdu_record | 836/808 | dfx: dump recorded PPDU descriptors to the kernel log | callees printk, memset_s, oam logs | command-table params |
| hdpp_stat_save_file_{rx,tx}_ppdu_record | 772/652 | dfx: persist PPDU records to a file | callee oal_kernel_file_print | file path/format |
| hdpp_sync_read_lock / hdpp_sync_write_lock | 660/636 | the object-sync guard: spinlock + wait-event + schedule_timeout used around every *_sync_write to the device | callees _raw_spin_lock, prepare_to_wait_event, hdpp_smp_{rd,wr}_wait_event_condition, schedule_timeout | wakeup contract |
| hdpp_stat_{tx,rx}_packets_dump | 624/624 | dfx: print TX/RX packet-rate snapshots | callee printk | - |
| hdpp_stat_get_core_name | 572 | stat name resolution through the oal stat table | callee oal_stat_get_name_by_cfg_tab | - |
| hdpp_stat_tx_wifi_stay_time_alloc_space | 544 | allocates the stay-time histogram buffer (tx) | callee oal_memalloc | histogram format |
| hdpp_stat_dump_help | 544 | dfx help text | caller hdpp_user_init | - |
| hdpp_hisi_thread_init | 500 | creates the per-core data-path worker thread | callees kthread_create_on_node, kthread_bind, wake_up_process; caller hdpp_main_init | thread fn/params |
| hdpp_tx_wifi_stay_time_set_point_cnt | 496 | stay-time histogram bucket config (tx) | exported, fn-ptr registered | params |

Context map (from the call graph): hdpp_user_get_valid (45 sites, r0 used
as boolean in 95%), hdpp_vap_get_by_id (106 sites), hdpp_user_get_by_idx,
hdpp_device_get_by_id - the object lookups backing the whole hmac layer;
hdpp_*_sync_write entered from hmac_band_exit, hmac_device_sync_add,
hmac_tx_ba_session_incr/dec, wal_dfr_power_down_dev etc. - the device sync
writers; hdpp_stat_*_save_start/end, hdpp_stat_rx_queue_len_save,
hdpp_stat_tx_wifi_depth_save_* are the hot-path sampling hooks called from
hmac_rxdata_thread, hmac_tx_data, hmac_tx_skb_pop_tid, hal_device_rx_mpdu_que_pop
and shuangta_irq_host_mac_isr; hdpp_ping_diag is called from 12 TX/RX hot
paths (hmac_tx_data, hmac_ap_rx_data_pre_proc, ...).

### 5.2 alg_ top by size

| symbol | B | what it does | evidence | probe |
| --- | --- | --- | --- | --- |
| alg_ofdma_config_param_output_entry | 2068 | formatter: render the OFDMA cfg row as text | callees alg_cfg_param_get_tbl_size, snprintf_s | arity/format |
| alg_cfg_args_analysis_trigger_user | 1380 | parser: "trigger user ..." args into the trigger cfg | callees alg_cfg_get_match_value, alg_cfg_get_tail_num, oal_atoi | cfg-id semantics |
| alg_cfg_args_analysis_tpc_code | 1292 | parser: TPC code args | alg_cfg_get_match_value, oal_atoi | - |
| alg_cfg_args_analysis_sounding_group_property | 1256 | parser: sounding group property args | alg_cfg_get_match_value, alg_cfg_get_tail_num | - |
| alg_atf_config_param_output_entry_sub1 | 1244 | formatter helper of the ATF entry | caller alg_atf_config_param_output_entry | - |
| alg_cfg_args_analysis_mac_addr | 1212 | parser: MAC address args (memcpy_s the address) | memcpy_s, oal_atoi | - |
| alg_cfg_args_analysis_user_{ru,tid} | 1052/1040 | parsers: per-user RU / TID args | oal_atoi, snprintf_s | - |
| alg_cfg_args_analysis_aggr_{size,num} | 1036/1008 | parsers: aggregation size/count args | oal_atoi | - |
| alg_trigger_cfg_param_output_user | 996 | formatter: trigger cfg per-user output | alg_cfg_get_match_string, memcpy_s | - |
| alg_schedule_su_sch_config_param_output_entry | 952 | formatter: SU-scheduling cfg row | - | arity |
| alg_trigger_cfg_param_output_comm1 | 948 | formatter: trigger common-part output | alg_cfg_get_match_string | - |
| alg_cfg_args_analysis_txmode_user_fix_mode | 940 | parser, EXACT reference twin: (param, mem_block, len) -> int | twin in wal/wal_config_alg_cfg.c | port 1:1 |

The alg_ layer is a table-driven text-command codec: a process-info table
(name -> cfg id, SET/GET type, need_h_process / need_w4_h / need_d_process /
need_w4_d flags) drives parser (analysis) and formatter (output) function
pointers.  The reference file header dates the design to 2018-06; the
vendor binary extends it with hi5622 cfg ids (ersru, trigger, spectral,
smartant, ulsch, ofdma, atf, ns_aggr, ...).  The 8 exported g_alg_* data
symbols are the table plumbing the debug modules reach into
(g_alg_cfg_process_find_by_cfg_id/_by_name = lookup function pointers,
g_alg_cfg_*_tbl_size = per-table sizes).
## 6. What needs bench probing to implement (and what to probe)

These functions cannot be implemented correctly from static analysis alone;
each needs an attended bench session (UART read-only, recovery armed):

1. **hdpp_main_init / hdpp_main_exit** - probe: init message dispatch
   (dmi-style per the dpe twin), the handler tables written into the g_hdpp
   global (wifi-driver-re section 4), init order of the object tree and the
   hisi thread.  Gate: vendor boot logs on UART after the transport slice
   reaches the ready-id checkpoints.
2. **hdpp_hisi_thread_init** - probe: thread function name, CPU binding and
   the two parameters (call site shows 2 registers).
3. **hdpp_{band,device,sys,user,vap}_sync_write** (5 functions) - probe:
   one no-op object sync per kind, then watch the D2H bitmap/ack at
   0x40039014 (transport contract) and the device-side log.
4. **hdpp_sync_read_lock / hdpp_sync_write_lock + hdpp_smp_*_wait_event_condition**
   - probe: who wakes the waiters (D2H event? timeout?) and the lock
   ordering vs the transport doorbell.
5. **hdpp_tx_msdu_stat_update / hdpp_rx_msdu_stat_update** - probe: counter
   struct layout and the shuangta dscr-info counters, using a single
   associated STA and known traffic.
6. **hdpp_d2h_user_twt_syn / hdpp_d2h_user_ersru_syn** - probe: D2H payload
   format for per-user TWT/ERSRU updates (feed vendor firmware a TWT
   enable, decode the message).
7. **hdpp_rxdata_netbuf_enqueue / hdpp_rxdata_sched** - probe: rxdata
   thread handoff and netbuf ownership rules (stage B2 of the transport
   slice, ETE data movement).
8. **hdpp_record_sequence_of_skb / hdpp_record_rx_sequence_of_skb** -
   probe: the sequence/reorder bookkeeping fields per user.
9. **hdpp_ping_diag** - probe: trigger condition and output format.
10. **40 of 46 exported hdpp_stat_* dfx handlers** - probe: the debug-command
    table they register into and the parameter format of each
    dump/clear/switch/set handler (static arity impossible: they are only
    referenced via function-pointer tables, R_ARM_ABS32 relocations).
11. **The alg table contract** - probe: the process-info table contents
    (cfg id list, SET/GET type, need_d_process flags) and the values of
    g_alg_cfg_bool_tbl_size etc.; harvest by replaying the vendor
    userspace's cfg commands and logging which ids arrive.
12. **alg_*_config_param_output_entry arities** - partial static evidence
    only (2..4 registers seen); confirm the (param, out, len) shape per
    family on the bench.

## 7. Scaffolding

scaffolding/ contains the linkable public surface (all ORIGINAL text):

- hi5622_hdpp.h - the hdpp contract: opaque object handles, lifecycle,
  object model, sync layer, device sync writers, data-path hooks, stat
  core, and the 46-name exported dfx API with the documented PROVISIONAL
  signature policy.
- hdpp_skel.c - stub bodies with per-function TODO semantics; exports the
  46 dfx names exactly as the vendor __ksymtab does (plus main_init/exit).
- hi5622_alg.h / alg_skel.c - the alg contract: dispatch, table lookup,
  argument helpers, the 17 twin parsers (reference shape, our spelling),
  the param_analyze process handlers, 7 top formatters, and the 8 exported
  g_alg_* data symbols.
- Kbuild + BUILD-NOTES.md.

**Build:** compiles clean against the 6.18.60 luofu tree into
hi5622_hdpp_alg_skel.ko (73,404 B, vermagic 6.18.60, GPL).  Export check:
all 60 vendor hdpp/alg/g_alg export names present (60/60), +2 intentional
extras.  Details and evidence sha256: scaffolding/BUILD-NOTES.md.

## 8. Recommended next slice

**alg_cfg + wal config-command port** (the ~103 KB alg_ family minus the
already-twinned core).  Reasons: (a) it is a self-contained string codec -
no device writes, so it can be developed and unit-tested host-only against
the 17 exact twins as an oracle; (b) it delivers the process-info table
contract that every later wal/hmac slice needs; (c) it validates the
clean-room methodology end-to-end before the riskier hdpp work.  After
that: **hdpp object model + stat dfx** (~52 KB), which depends on the
transport bench stages (B2) and the probe list of section 6; it is the
slice that turns the skeleton here into real code.

## 9. Regeneration / provenance

- mapping/*.csv regenerate from local artifacts via
  tools/gen-mapping.py (needs the vendor .ko symbol/export/call-graph
  dumps and the reference-tree inventory - all local, see the script
  header for the exact inputs; the local workdir is
  /home/shibbityshwab/wifi-datapath-slice-work).
- The reference clone: git clone --depth 1 --filter=blob:none --sparse
  https://github.com/5-Super-Rookie-5/hongmengkernel.git with sparse-checkout
  of kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi (NOT
  committed; reference-only, no license grant).
- The vendor .ko symbol data used here comes from the local
  recovery-extract tree and is never committed (no blobs policy).

## 10. License

All committed text in this slice is original writing by this lane
(GPL-2.0 for the C scaffolding, matching the kernel it links against).
No hongmengkernel text was copied; mapping tables carry structural metadata
(names, sizes, arities, parameter kinds) only.  No vendor binaries, object
code, firmware or calibration data are committed anywhere.
