# Wi-Fi alg_cfg + wal config-command codec port - REPORT

**Lane:** wifi-algcfg-slice (host-only). **Date:** 2026-11-22.
**Deliverables:** this file, SUMMARY.md, codec/ (port + 6.18 Kbuild), harness/
(17-twin oracle unit test), mapping/ (6 CSVs), vendor/hi3881-wal/ (GPL-2.0 text),
tools/ (regeneration).
**Build:** hi5622_alg_cfg_codec.ko - 97,108 B, license=GPL, vermagic 6.18.60
SMP mod_unload ARMv7 (compiles -Werror against the luofu 6.18.60 tree).
**Oracle:** host-side run of the 17 twin cases: PORT == ORACLE byte-identical,
0 failures (evidence: harness run logs, section 5).

---

## 1. Scope and rules

Port the self-contained alg_cfg command codec + the wal config-command glue
from the GPL-2.0 HiSilicon WLAN framework lineage to 6.18 as vendored-clean
source, pinning behaviour against the local vendor binary
(hi5622v100_wifi.ko - the ORACLE) and its 17 exact-name twins found by the
wifi-datapath-slice (commit 9f0bb41).

Host-only: nothing was written to the device; no vendor binaries/blobs are
committed; all committed C text is GPL-2.0 with HiSilicon copyright headers
kept where the text is vendored.

### 1.1 License finding (task premise vs reality - IMPORTANT)

The task premise said the twin file wal/wal_config_alg_cfg.c lives in "the
GPL-2.0 hi3881 tree (openharmony/device_soc_hisilicon)". It does not:

| tree | clone | license | wal_config_alg_cfg.c? |
| --- | --- | --- | --- |
| OpenHarmony device_soc_hisilicon (hi3881v100 driver) | /home/shibbityshwab/wifi-algcfg-slice-work/hi3881-openharmony (depth 1) | **GPL-2.0-or-later** (file headers) | **NO** - wal/ has 31 files, no alg codec |
| hongmengkernel hi1105 wifi subtree | /home/shibbityshwab/hongmengkernel-ref (sparse) | **no license grant** (no LICENSE, no SPDX anywhere in the 373-file subtree; "Copyright HiSilicon ... All rights reserved") | YES - the 17-twin file |

So the twin-bearing file has NO redistributable license. Per the publication
rules this slice therefore:
- vendors **only** what the GPL-2.0 hi3881 tree actually grants: the wal
  config-command layer wal_hipriv/wal_ioctl/wal_customize/wal_maintenance
  (vendor/hi3881-wal/, headers kept);
- ships the alg codec core as **original GPL-2.0 text** (our spelling), whose
  every struct offset, enum value, error code, string and bound was verified
  against the binary (the ORACLE) and whose runtime behaviour is verified
  against the reference file used as a **local execution oracle only** (the
  hi1105 file stays in /home, gitignored in harness/oracle/).

## 2. Method (evidence chain)

1. Cloned the hi3881 tree into the /home workdir (git clone --depth 1
   https://github.com/openharmony/device_soc_hisilicon, sparse to the
   hi3881v100 driver; NOT committed).
2. Extracted the hi1105 wal_config_alg_cfg.c + alg_cfg.h + wal_config_alg_cfg.h
   to the local oracle dir (harness/oracle/, gitignored).
3. Binary table forensics on the local e_machine-patched wifi.ko (technique
   from wifi-datapath-slice REPORT section 3): readelf -sW/-SW/-rW + python
   dumps of .data (tools/dump-alg-tables.py reproduces this):
   - g_ast_alg_cfg_process_info_table @ .data+0x35c8, **0x1368 = 414 entries
     x 12 B** {name*, u16 id, u8 type, u8 resv, 4 need_* flags} - shape
     verified from the search functions' disassembly (ldr r1,[r4] -> strncmp;
     ldrh at +4; ldrb at +6).
   - g_ast_alg_cfg_param_size_tbl @ .data+0x2d9c, 40 x 4 B {u8 type, u8 size},
     type guard ">46 -> 0" in alg_cfg_param_get_tbl_size (0x149d14).
   - value tables: pkt_type 13 x 8 B @ 0x2e4c, tx_mode 13 x 8 B @ 0x301c
     (names in the .rodata cluster @ file 0x23a680), bool 2 x 8 B @ 0x2e3c.
   - dispatch: wal_wlan_cfg_alg_process_entry (0x14ae44) searches a **176-B
     per-command LUT** (g_alg_cfg_lut @ .data+0x4934, 0x11e00 = 416 x 176 B),
     key u32 = (type<<16)|id, same order as the process table - the hi5622
     replacement for the reference's 8-B linear parser table.
   - command NAME strings are loader-patched at runtime (the .data carries a
     4608-pair custom fixup list at 0x0a9874; the standard .rel.data has no
     entries for the table slots). The .rodata carries the full hi5622 name
     vocabulary (txmode_*, ofdma_*, spectral_*, sounding_*, trigger_*,
     ersru_*, smartant_* clusters); per-entry name mapping is a bench probe.
4. Error-code constants pinned from the binary: PTR_NULL=100, ARRAY_OVERFLOW=101,
   CONFIG_ARGS_OVER=1009, ALG_PROCESS_INFO_NOT_FOUND=1014, SUB_ALG_ENTRY_NOT_FOUND=1017,
   OAL_FAIL=35629 (0x8b2d).
5. Port written (codec/), then compiled twice from one harness source
   (harness/test_alg_codec.c): oracle_test (links the local hi1105 reference)
   vs port_test (links the port). Outputs diffed - identical.

## 3. Divergence census (hi5622 vs reference)

| item | reference (hi1105) | hi5622 binary | port |
| --- | --- | --- | --- |
| process-info entries | 33 | **414** (ids 201..4401, 33 ranges) | 33 twin + 414 census rows |
| entry shape | 12 B | 12 B (verified) | same |
| param-size table | 3 entries | 40 entries, 47-type space | 3 twin + dump in mapping/ |
| parser dispatch | 8-B linear table | **176-B LUT**, 416 entries, key (type<<16)|id | twin table + LUT struct documented |
| need_d flags | SET+GET pairs all d|d | 380/414 need_d; 1400-range host-only (35), 1300/1700 mixed, 2901 h-only | census rows carry flags |
| cfg-id numbering | MAC_ALG_CFG_* (0x01..) | hi5622 renumbered 401-422 txmode, 701+, 801+, ... | census CSV |
| pkt-type table | 13 | 13 (verified) | 13 |
| tx-mode value table | 13 | 13 (verified) | 13 |
| error codes | header values | 100/101/1009/1014/1017/35629 (verified) | binary values used |
| names | static strings | runtime-patched (custom fixup list) | recovered vocabulary in mapping/ |

The 17 twins: **all 17 MATCH** the reference semantics at the instruction
level where compared (mapping/twin-divergence.csv has per-twin notes,
addresses and sizes).

### 3.1 hi5622-specific entries still to implement

The census (239 alg_ functions) minus the 17 twins = the hi5622-specific set,
listed in mapping/hi5622-specific.csv (120 annotated rows): 30 spectral_*
parsers, 6 smartant_*, 5 ulsch_*, 5 trigger parsers + 10 trigger output
formatters, 3 ersru_*, 2 sounding_*, 60 output-formatter-shaped functions
(29 *config_param_output_entry + alg_trigger_cfg_param_output_* +
alg_cfg_param_output_*), plus atf/mumimo/ns_aggr/ofdma/tpc/obss/urp/ppet
entries. These need either the bench probe (table contract harvest, section 6)
or a per-command port once the LUT layout is fully decoded.

## 4. What was built

- **codec/** - the port: alg_cfg.h (types, verified offsets), wal_config_alg_cfg.h,
  wal_config_alg_cfg.c (tables + 17 twins + dispatch + process entry),
  alg_cfg_hi5622_census.h (414 generated census rows), kernel_util.c (securec/
  oal shims), skel_mod.c (exports the vendor's 8-name g_alg_* surface shape),
  Kbuild. Builds -Werror to hi5622_alg_cfg_codec.ko (97,108 B, GPL, 6.18.60).
- **harness/** - Makefile + test_alg_codec.c (17 twin cases: 60+ assertions +
  byte dumps + error strings) + shims/. oracle/ is gitignored (reference).
- **mapping/** - alg-process-table.csv (414), alg-param-size-table.csv (40),
  alg-value-tables.csv (28), twin-divergence.csv (17), hi5622-specific.csv (120).
- **vendor/hi3881-wal/** - wal_hipriv/ioctl/customize/maintenance .c/.h with
  GPL-2.0 headers kept (the hi3881 "wal config-command codec"; 6.18 adaptation
  of these files is tracked for the wal slice - they compile only inside the
  full oal/hmac environment, out of scope for this codec slice).
- **tools/dump-alg-tables.py** - regenerates the process/param-size/value table
  CSVs from the local .ko (regeneration command in section 7).

## 5. Oracle results (17 twins, host-side)

Both binaries built from the same harness; run and diffed:

- ./oracle_test (links the local hi1105 reference) - exit 0, "PASS: 0 failures"
- ./port_test   (links the committed port)      - exit 0, "PASS: 0 failures"
- diff oracle-run.out port-run.out -> **empty (byte-identical)**

Cases: T01 get_cmd_one_arg (incl. overflow/empty/NULL/offset=14), T02/T03 name
lookup, T04/T05 id lookup, T06 all 13 pkt types + miss, T07 head parsing with
pkt-type prefix, T08 full analysis, T09 match-value, T10 table sizes, T11-T17
the seven parsers (valid + error strings + the int8 wrap quirk in
waterfilling_limit preserved on both sides).

**Result: 17/17 twins PASS, port == oracle.**

## 6. Bench probes needed later

1. Process-table NAME mapping (loader fixup): replay vendor cfg commands on the
   bench and log which ids arrive, or dump .data at runtime (g_alg_cfg_process
   finder addresses are exported via __ksymtab).
2. The 176-B LUT completion (fn-pointer fields, need-flag semantics per field).
3. The 40-entry param-size table's type names for ids 29..46.
4. Per-family output_entry formatter arities (2..4 registers seen statically).

## 7. Regeneration / provenance

- mapping CSVs: python3 tools/dump-alg-tables.py (needs the local
  e_machine-patched hi5622v100_wifi.ko - never committed) + the census CSVs
  from wifi-datapath-slice.
- oracle run: cd harness && make && make check (oracle/ needs the local hi1105
  reference copy; the committed tree ships port_test only).
- hi3881 tree: git clone --depth 1 https://github.com/openharmony/device_soc_hisilicon
  (kept in /home, not committed).
- module build: see codec/Kbuild header (KDIR = the luofu 6.18.60 build tree,
  STAGING_DIR set).

## 8. License

codec/, harness/ (except oracle/) and skel text: original writing, GPL-2.0
(HiSilicon copyright headers retained as the interface lineage). vendor/
hi3881-wal/: verbatim GPL-2.0-or-later text with headers intact. No
hongmengkernel text is committed; no binaries, blobs, firmware or calibration
data anywhere in the slice.

## 9. Next slice

**wal config-command glue + hmac cfg dispatch** - take the codec core from
here and wire it into the wal layer: (a) the 6.18 cfg80211/hipriv entry
(wal_wlan_cfg_module_process_entry + the vendor debug CLI surface), using the
vendored wal_hipriv/wal_customize as the GPL base; (b) decode the LUT
completion at runtime on the bench (probe 2); (c) implement the hi5622 parser
families (spectral/smartant/ulsch/trigger/ersru/sounding) against the census
tables. After that: hdpp object model + stat dfx (per the datapath slice).
