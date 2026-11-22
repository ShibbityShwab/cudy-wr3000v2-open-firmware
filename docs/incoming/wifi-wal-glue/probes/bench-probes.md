# Bench probe list - wal_cfg glue (extends wifi-algcfg-slice probe list)

**Lane:** wifi-wal-glue (host-side deliverable; every probe below is for a
future **attended bench session** - nothing here was run against the device).
**Gates before any bench run (per BENCH-FACTS/AGENTS):** `tools/bench-link.sh`
PASS, recovery door READY (`tools/recovery-card.sh`), UART capture armed
(`tools/uart-capture.sh`, read-only), `tools/check-init-shadow.sh` PASS.
Device access via `tools/rsh.sh`; every write is a config-command replay on
the vendor CLI only (no filesystem/flash writes).

These probes extend wifi-algcfg-slice/REPORT.md section 6 (items 1-4).  They
harvest the two runtime-only contracts this slice models but cannot pin
host-side: the **process-table NAME fixups** and the **176-B LUT completion**.

## Harvest outputs -> consumers

| output artifact | consumer |
| --- | --- |
| fixup CSV: process-table row idx -> real command name (414 rows) | regenerate the compiled-in fixup list in glue/wal_cfg_lut.c + the census names in wifi-algcfg-slice/codec/alg_cfg_hi5622_census.h |
| LUT field map: which 176-B offsets hold fn pointers / aux words | pin WAL_CFG_LUT_SLOT_* offsets in glue/wal_cfg_lut.c |
| param-size type names 29..46 | complete g_alg_cfg_param_size_tbl names |
| formatter arity + output samples per family | implement the 60 formatter entries (alg_*_config_param_output_entry) |
| parser oracle traces (trigger/ersru/sounding/spectral/smartant/ulsch) | implement the 60 parser entries of the 120-row hi5622 set |

## P0 - read the loaded-module layout (one-time, every session)

1. `rsh cat /sys/module/hi5622v100_wifi/sections/.data` -> runtime .data base B.
2. Verify the static-layout assumption at runtime: the vendor loader writes
   its own tables; compare the static .ko .data dump (tools/dump-alg-tables.py,
   local) against memory where readable.  Targets (from wifi-algcfg-slice
   REPORT s3): process table B+0x35c8 (414x12), param-size B+0x2d9c (40x4),
   value tables B+0x2e3c/0x2e4c/0x301c, LUT B+0x4934 (416x176), fixup list
   B+0x0a9874 (4608 8-B pairs).
3. Memory-read route in preference order:
   a. vendor debug CLI dump commands (no memory access needed);
   b. /proc/kcore if CONFIG_PROC_KCORE is on;
   c. a read-only trace/kprobe if the vendor kernel has it;
   d. (last resort, human-attended) a tiny read-only module built from the
      local 5.10.201 tree that prints the tables and is rmmod'ed - NOT loaded
      on the running vendor modules' critical path, and never the 0x400392f0
      / 0x10161000 registers.
   Record which route worked + the raw capture in build/ evidence.

## P1 - process-table NAME harvest (algcfg s6 item 1)

Goal: the vendor loader patches the 414 process-table name slots at runtime
(the static .ko has no .rel.data entries for them; names come from the
.rodata cluster @ file 0x23a680).

1. Vendor CLI inventory: run the debug surface candidates and capture output
   (UART + rsh log):
   - `iwpriv wlan0 help` / `iwpriv wlan0 alg` / `iwpriv wlan0 alg_cfg`
   - `echo "vap0 alg_cfg help" > /proc/wifi/hipriv` (or the sysfs hipriv node
     the vendor registers - find it: `find /sys /proc -name '*hipriv*'`)
   - the vendor shell `hi_wifi` / `atcmd` alg subcommands (DRIVER-BLACKBOX
     s12 lists the 0x0101 alg / 0x0102 cali dispatch).
   A read-side table dump (the hi3881 wal_hipriv_sys_read pattern lists every
   command name) is the jackpot: it yields the accepted NAME vocabulary.
2. Per-census-row replay (fills the id->name mapping): for each of the 414
   census rows, replay a GET/SET by the recovered vocabulary name where known
   (`... alg_cfg <name> <arg>`) and log the vendor reply (OK / ERROR code).
   Accepted = name binds to that row's (id, type).  Rate-limit; no firmware
   writes beyond the config commands themselves (these are the vendor's own
   debug commands).
3. Cross-check: walk the runtime fixup list (B+0x0a9874, 4608 pairs) via the
   P0 memory route; pairs targeting the process-table slots carry the name
   pointers -> the authoritative id->name table without any command replay.
4. Deliverable: fixup CSV + regenerated fixup list + census names; commit as
   the next slice's input.

## P2 - LUT completion harvest (algcfg s6 item 2)

Goal: the 176-B per-command LUT (416 entries @ B+0x4934) is only partially
understood statically (verified: key@+0, msg_flags@+12 0x000b0001/0x000b0000,
need_flags@+16 0x00010101 SET, fixed_field@+20 0x0a).  The vendor fixup path
populates the rest at runtime.

1. Static half (host-side, already done): read the 4608 fixup pairs from the
   .ko file (B+0x0a9874 in the FILE dump - tools/dump-alg-tables.py can be
   extended) - each pair {target, value} names the exact slots the loader
   writes, including which LUT offsets receive fn pointers.  This can be
   harvested WITHOUT the bench from the local .ko; do it first.
2. Runtime half: after boot, capture the LUT memory (P0 route).  Diff
   static-file LUT vs runtime LUT: every patched offset that is NOT a
   process-table name slot is a LUT descriptor field.  Classify fields:
   fn-pointer slots (values land in .text), aux words (.data/.rodata),
   per-entry flags.
3. Behaviour: issue one command per family (spectral/smartant/ulsch/trigger/
   ersru/sounding/ofdma/atf) and re-capture; observe which entries change and
   whether msg_flags/need_flags differ per family (e.g. the 1400-range
   host-only rows should never flip need_device_process).
4. Deliverable: LUT field map -> pin WAL_CFG_LUT_SLOT_* in wal_cfg_lut.c and
   the alg_cfg_lut_entry_stru fields in wifi-algcfg-slice/codec/alg_cfg.h.

## P3 - param-size type names 29..46 (algcfg s6 item 3)

1. From the runtime param-size table (B+0x2d9c, 40 rows): the sizes are
   known; only the TYPE NAMES for 29..46 are unknown.
2. For each value-table-backed type (types 14/15/16/17/18/19 etc. that index
   string tables), issue GETs of representative ids from each family and
   capture the formatter output - the printed value strings name the type
   vocabulary (the .rodata cluster @ file 0x23a680 carries it).
3. Deliverable: type-name table -> complete the param-size names in the
   codec + the g_alg_cfg_snd_*_tbl_size values.

## P4 - formatter arities + output samples (algcfg s6 item 4)

1. For each of the 60 formatter rows (alg_*_config_param_output_entry +
   alg_trigger_cfg_param_output_* + alg_cfg_param_output_*), issue the GET
   through the vendor CLI and capture the reply text (UART + rsh log).
2. Static arity evidence (2..4 registers) gets pinned per family by matching
   the reply format against the LUT fn slots harvested in P2.
3. Deliverable: per-formatter (params, out, len[, extra]) contract + one
   golden output sample each -> the port of the 60 formatters.

## P5 - parser oracle traces for the 60 parser rows

Goal: turn the remaining 60 parser families into oracle-pinned ports the way
the 17 twins were (wifi-algcfg-slice REPORT s5).

1. For each family (30 spectral, 6 smartant, 5 ulsch, 5 trigger parsers,
   3 ersru, 2 sounding, 3 ns_aggr, 3 mumimo, tpc, ofdma, atf): issue the SET
   with boundary args (min/max/invalid) through the vendor CLI; record the
   reply error codes (100/101/1009/1014/1017/35629 vocabulary).
2. Capture the resulting hcc/D2H traffic only if a read-only trace route
   exists (P0 route c/d) - otherwise CLI replies only.
3. Deliverable: per-parser error-code matrix -> the port + harness cases.

## P6 - wal_hipriv_alg_cfg_get_cfg_id edge semantics

1. Vendor behaviour probes: prefix vs exact name match (issue a name prefix
   and a name + suffix), the _normal variant's arg_size semantics.
2. Deliverable: pins the two lookups this slice wires into
   g_wal_hipriv_alg_cfg_get_cfg_id.

## Ordering and next-slice gate

P1+P2 are the gate for the **next slice** (hi5622 parser families port):
names (P1) make the 414-row table real, the LUT field map (P2) makes the
dispatch honest.  P3-P6 feed the same slice's family ports.  Without P1/P2
the parser ports would hard-code unverified dispatch - so the next slice is
explicitly **bench-gated on P1+P2**.
