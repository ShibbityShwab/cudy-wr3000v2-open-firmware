# Wi-Fi wal config-command glue - REPORT

**Lane:** wifi-wal-glue (host-only). **Date:** 2026-11-22.
**Deliverables:** this file, SUMMARY.md, LICENSE-NOTES.md, glue/ (the 6.18
front-end module sources + Kbuild + cfg80211 provider symvers), probes/
(bench probe list), mapping/dispatch-surface.csv (44-row vendor-name map).
**Build:** hi5622_wal_cfg.ko - 222,732 B, license=GPL, vermagic 6.18.60 SMP
mod_unload ARMv7 p2v8, depends=cfg80211, compiles clean (-Werror) against
the luofu 6.18.60 tree with the alg_cfg codec linked in.

---

## 1. Scope and rules

This slice wires the alg_cfg command codec (previous slice
wifi-algcfg-slice, commit 3d1ad9b - 17/17 oracle PASS) into a 6.18 wal
config-command front end:

1. **cfg80211 vendor-command / hipriv-style entry** that dispatches SET/GET
   config commands into the codec (vendor names wal_wlan_cfg_module_*,
   wal_hipriv_alg_cfg);
2. **hmac cfg dispatch stub surface** matching the vendor names from the
   census CSVs (wifi-datapath-slice + wifi-framework-port) and the host
   driver contract (wifi-driver-re/REPORT.md: 39-op map, hcc_msg header,
   dispatch);
3. **LUT/name-fixup loader** reading the runtime table layout documented in
   wifi-algcfg-slice/REPORT.md section 3 (bench will later fill names);
4. **bench probe list** for harvesting the LUT completion + process-table
   names at runtime (extends the algcfg probe list).

Host-only: nothing was written to the device; no vendor binaries/blobs are
committed; all committed text is original GPL-2.0 writing.  The GPL-2.0
hi3881 vendored base (wal_hipriv/ioctl/customize/maintenance) stays in
wifi-algcfg-slice/vendor/hi3881-wal/ and is referenced, not duplicated; the
hi1105/hongmengkernel tree is used locally as a name/structure reference
only (no license grant there - no hi1105 text is committed).

### 1.1 The signature delta hi5622 vs the framework

The framework (hi3881/hi1105 lineage) declares the generic process-entry
type as (wlan_cfg_param_stru *param, uint8_t *msg_type, uint8_t *mem_block,
uint16_t *mem_len, wlan_cfg_process_info_stru *process_info).  The hi5622
binary's wal_wlan_cfg_alg_process_entry (0x14ae44, pinned by the algcfg
slice) takes the flattened (char *args[20], uint8_t args_num, uint8_t
*msg_type, uint8_t *mem_block, uint16_t *mem_len, wlan_cfg_process_info_stru
*process_info) form; this slice keeps that 6-arg codec form and the module
table dispatches (args vector, args count) into it.

---

## 2. Inputs (evidence chain)

| input | where | used for |
| --- | --- | --- |
| codec port + 17-twin oracle | opensource/docs/incoming/wifi-algcfg-slice/codec/ | linked core: process table (414), parser dispatch (17), entry fn |
| runtime table layout | wifi-algcfg-slice/REPORT.md s3 | LUT loader layout descriptor + fixup semantics |
| host-driver contract | wifi-driver-re/REPORT.md s2.4/s3 | hcc_msg header, dispatch algo, registration, rsp pairing, 0x0101/0x0102 ioctl dispatch |
| census CSVs | wifi-framework-port/census/, wifi-datapath-slice/mapping/ | the exact vendor names implemented (wal_wlan_cfg_module_*, hmac_sync_dmac_*, wal_config_process_*, g_alg_*) |
| vendor ksymtab dumps (local) | /home/shibbityshwab/wifi-framework-port-work/census/*.ksymtab.txt | the exact 14-name alg export surface + hmac_config_sync_cmd_common |
| vendored hi3881 GPL-2.0 text | wifi-algcfg-slice/vendor/hi3881-wal/ | hipriv entry shape (wal_hipriv_alg_cfg), cmd-table semantics |
| hi1105 reference (local, no license) | /home/.../hi11xx-hongmeng/.../wal/wal_config_alg_cfg.c | module-entry architecture reference only |

### 2.1 The vendor export surface this slice reproduces

From the local ksymtab dumps (never committed): the vendor hi5622v100_wifi.ko
exports 6 alg_ functions + 8 g_alg_ data names:

- functions: alg_cfg_args_analysis_param, alg_cfg_args_param_analyse_equipment_param,
  alg_cfg_get_match_string, alg_cfg_get_match_value, alg_cfg_get_tail_num,
  alg_cfg_param_get_tbl_size
- data: g_alg_cfg_bool_tbl_size, g_alg_cfg_process_find_by_cfg_id,
  g_alg_cfg_process_find_by_name, g_alg_cfg_search_alg_debug_lut,
  g_alg_cfg_search_dbg_cfg_name_by_cfg_id, g_alg_cfg_snd_fback_type_tbl_size,
  g_alg_cfg_snd_user_mat_type_tbl_size, g_alg_cfg_su_aggr_mode_tbl_size

The codec slice's skel_mod.c guessed 8 names slightly differently; this
slice exports the **vendor-exact 14** plus continuity extras (g_alg_cfg_tx_mode_tbl_size,
g_wal_hipriv_alg_cfg_get_cfg_id) and the glue entry points, and wires the
two debug-lut searcher slots to the real LUT loader.

---

## 3. What was built (glue/)

One module, hi5622_wal_cfg.ko, linking the codec objects
(wal_config_alg_cfg.o + kernel_util.o from wifi-algcfg-slice/codec/) into
the front end.

| file | contents |
| --- | --- |
| wal_cfg_glue.h | wlan_cfg_param_stru, module process table row, wal write msg, hcc_msg header (driver-re s3.1), 16-B handler record, runtime layout descriptor, fixup pair, full glue surface |
| wal_cfg_module.c | wal_wlan_cfg_module_process_entry (module dispatch: lookup -> codec parse -> need_* host/device legs), wal_wlan_cfg_module_host/device_process_entry stubs, wal_hipriv_alg_cfg + wal_hipriv_alg_cfg_get_cfg_id(_normal), wal_config_process_* + wal_msg_request_* + wal_set_dev_msg_response_by_addr + wal_check_and_release_msg_resp + wal_sync_query_dmac_sta_info stubs (request-ring with the 0x0d01xxxx id convention, driver-re s3.7) |
| wal_cfg_lut.c | 416-entry x 176-B LUT builder (key (type<<16)|id, msg_flags 0x000b0001/0x000b0000, need_flags 0x00010101, fixed_field 0x0a - all binary-verified), name-fixup loader (compiled-in pair list, empty until bench; runtime fixup_add), layout descriptor as data (all s3 offsets), wal_cfg_search_alg_debug_lut / wal_cfg_search_dbg_cfg_name_by_cfg_id |
| hmac_cfg_stubs.c | hmac_event_config_syn (hcc_msg_process-shaped dispatch over a stub reg tab), hmac_config_sync_cmd_common (exported, like the vendor), hmac_config_sync_ssid, hmac_sync_dmac_alg_cfg_rsp_entry / _get_entry_normal / hmac_sync_dmac_cali_cfg_rsp_entry (decode the rsp req-id word, complete the pending request), hmac_chan_tx_cali_sync, hcc_msg_register_tab{,_chip,_core,_customise} stubs (12->16-B record merge, core registers both core groups) |
| wal_cfg_frontend.c | module init/exit, /sys/kernel/wal_cfg/ sysfs surface (cmd, help, process_table, lut, layout, param_size_tbl, fixup_add, fixup_list), wal_cfg_exec_command_line (hipriv + cfg-id + cali forms), cfg80211 vendor-command registration helper (wal_cfg_register_vendor_commands + doit, subcmd SET/GET, policy VENDOR_CMD_RAW_DATA, 6.18 alloc_reply_skb API), the export surface |

### 3.1 Entry surfaces

- **hipriv-style (live, self-contained):** `echo "alg_cfg txmode_user_fix_mode 3 0x1f" > /sys/kernel/wal_cfg/cmd`.
  Dispatches through the codec: twin rows run the oracle-verified parsers;
  hi5622 census rows return 1017 (SUB_ALG_ANALYSIS_ENTRY_NOT_FOUND) until
  their families are ported; "help", "257 ..."/"0x0101 ..." (module entry by
  cfg id) and "cali ..." (not ported) also work.
- **cfg80211 vendor-command (API surface):** wal_cfg_register_vendor_commands(wiphy)
  is exported for the future ported driver; this module registers nothing
  itself (no fake wiphy).  The luofu tree builds CONFIG_CFG80211=n (OpenWrt
  ships cfg80211 as a package), so the two provider symbols are declared in
  glue/cfg80211-provider.symvers exactly the way the vendor .ko itself
  imports 25 cfg80211 symbols; the module's depends= line says cfg80211.
- **dispatch stub surface (hmac/wal names):** 22 vendor-named stubs, each
  with the documented contract in its comment; signatures that are only
  partially measured in the binary are marked PROVISIONAL (same policy as
  wifi-datapath-slice/scaffolding).

### 3.2 LUT / name-fixup loader

Builds the binary-verified LUT shape from the codec tables, keeps the vendor
layout as data (process table .data+0x35c8, param-size +0x2d9c, value tables
+0x2e3c/+0x2e4c/+0x301c, LUT +0x4934 416x176, fixup list +0x0a9874 4608
8-B pairs, name vocab .rodata+0x23a680), and applies name fixups: compiled-in
(empty until the bench harvest - probes/bench-probes.md P1) or runtime via
/sys/kernel/wal_cfg/fixup_add.  The vendor fixup path patches both process-table
name slots AND LUT descriptor fields; the loader models both (host fixup pair
= {target, value}, targets < 414 name rows, targets in the LUT range patch
descriptor fields).

---

## 4. Build status (evidence)

KDIR = the luofu 6.18.60 tree; TC = the OpenWrt arm toolchain (exact paths
in glue/Kbuild).  make ARCH=arm CROSS_COMPILE=... modules ->
**hi5622_wal_cfg.ko, 222,732 B, 0 errors 0 warnings**; .modinfo:
license=GPL, description set, depends=cfg80211, vermagic=6.18.60 SMP
mod_unload ARMv7 p2v8.

Notes on the build: the two codec translation units live outside the module
dir, so the Kbuild carries explicit compile rules for them; MODULE_INFO(description,...)
is used instead of MODULE_DESCRIPTION() because the OpenWrt tree sets
CONFIG_MODULE_STRIPPED (which disables the latter and makes modpost warn);
the codec is linked in, so this module must NOT be loaded together with
hi5622_alg_cfg_codec.ko (duplicate symbols - documented in glue/README.md).

Export check: all 14 vendor-exact alg exports + hmac_config_sync_cmd_common
(vendor-exported too) + the 8 glue/extras are present in __ksymtab_strings
(mapping/dispatch-surface.csv has the full 44-row map).

### 4.1 What this slice deliberately does not do

No device contact, no fake wiphy, no module load anywhere, no commit of
binaries (.ko/.o are gitignored), no hi1105 text.  The 4 exported alg_
functions that are not codec twins are stubs with the vendor names.

---

## 5. What remains: the hi5622 parser families (120 annotated entries)

mapping/hi5622-specific.csv of the algcfg slice lists the 120 hi5622-specific
rows this slice did NOT port (they need the bench probes first):

| family | rows | kind | port gate |
| --- | ---: | --- | --- |
| formatter (alg_*_config_param_output_entry, trigger output, param_output_temp) | 60 | output formatters | probes P2 (LUT field map) + P4 (arity + golden outputs) |
| spectral_* | 30 | parsers | P5 oracle traces |
| smartant_* | 6 | parsers | P5 |
| ulsch_* | 5 | parsers | P5 |
| trigger parsers | 5 | parsers | P5 |
| ersru_* | 3 | parsers | P5 |
| ns_aggr | 3 | parsers | P5 |
| mumimo | 3 | parsers | P5 |
| sounding_* | 2 | parsers | P5 |
| tpc / ofdma / atf | 1+1+1 | parsers | P5 |

Plus the 4 vendor-exported function stubs (alg_cfg_get_match_string,
alg_cfg_get_tail_num, alg_cfg_args_analysis_param,
alg_cfg_args_param_analyse_equipment_param) and the cali entry - all part of
the same 120-list/adjacent families.

Until P1 runs, the 381 census rows carry placeholder names
(hi5622_cmd_<id>_<s|g>); until P2 runs, the LUT fn slots for these rows are
NULL and every SET/GET on them correctly returns the vendor's 1017.

---

## 6. Bench probes (probes/bench-probes.md)

P0 runtime layout readout -> P1 process-table NAME harvest (fixup list walk
+ CLI replay) -> P2 176-B LUT completion harvest (fixup pairs classify the
patched offsets; per-family behaviour) -> P3 param-size type names 29..46 ->
P4 formatter arities + golden outputs -> P5 parser oracle traces for the 60
parser rows -> P6 get_cfg_id edge semantics.  Each probe names its output
artifact and which deliverable consumes it; all are UART read-only + vendor
CLI replay, no flash writes.

---

## 7. Exact next bench-gated step

1. **Attended bench session** (recovery armed, UART capture, bench-link PASS):
   run probes **P1 and P2** (probes/bench-probes.md).  P1 yields the 414-row
   name fixup CSV; P2 yields the LUT field map.
2. **Next slice (host-side, after P1+P2):** port the 120 hi5622 parser
   families (s5 table) against the completed tables - regenerate the census
   names + the compiled-in fixup list in wal_cfg_lut.c, pin the LUT slot
   offsets, then implement the 60 formatters (P4 outputs as golden strings)
   and the 60 parsers (P5 error-code matrices as oracle cases), and extend
   the harness with per-family twin-style cases.
3. After that: cali entry + the hdpp object model / stat dfx slice (per the
   datapath slice's roadmap).

---

## 8. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| module compiles clean, size/vermagic/license | glue build log; modinfo above | [V] |
| 44-name dispatch surface (14 vendor-exact + hmac + stubs) | mapping/dispatch-surface.csv vs census CSVs + local ksymtab dumps | [V] |
| LUT layout + fixup semantics | wifi-algcfg-slice/REPORT.md s3 + alg_cfg.h lut entry | [V] |
| hcc_msg header/dispatch/registration | wifi-driver-re/REPORT.md s3.1-3.3 | [V] |
| 120-family status | wifi-algcfg-slice/mapping/hi5622-specific.csv | [V] |
| 17-twin codec behaviour | wifi-algcfg-slice harness (17/17 oracle PASS) | [V] |

---

## 9. Regeneration / provenance

- module build: see glue/Kbuild header (KDIR/TC exports).
- dispatch-surface.csv: derived from the committed census CSVs + the local
  ksymtab dumps (never committed) - cross-check with the names in glue/*.c.
- vendor .ko offsets: local e_machine-patched hi5622v100_wifi.ko +
  wifi-algcfg-slice/tools/dump-alg-tables.py (local-only inputs).

## 10. License

glue/, probes/, mapping/, this report: original writing, GPL-2.0 (HiSilicon
copyright headers retained as the interface lineage, same policy as the
codec slice).  Referenced vendored text: wifi-algcfg-slice/vendor/hi3881-wal/
(GPL-2.0-or-later, headers kept there).  No hi1105/hongmengkernel text, no
binaries, no blobs, no firmware, no calibration data committed.
