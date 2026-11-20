# LSW-SPEC: the internal LSW switch - an evidence-graded RE specification

**Status 2026-11-18: research document only.** This file contains **no driver code**, it has
**never run under our own driver**, and it does not modify any shared file. It is the block-D
deliverable promised by [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 5 and it resolves the
open question that [PIE-DATAPATH.md](PIE-DATAPATH.md) section 7 deferred to "LSW block-D RE".

**Measured update 2026-11-18 (task-45, read-only):** the first attended read-only inventory has
been taken on the stock vendor boot, and its DT follow-up read the live post-overlay
`lsw_dp`/`lsw_pfe` windows and the `opp` qid tables A/B. Sections 5.2, 5.4, 6.3, 7.3 and 12.3
now carry the measured values; the queue/flow DDR base, the window-name order and the D1b W1
baseline are no longer [U]. The full analysis is [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md);
recorded facts are [V] for that one boot, and **no register write has been made** (D1b write set
and the register-write campaign are untouched). The target-skeleton patch `021` implements the
read-only half: a W1 baseline probe (windows, powered-state gate, 30-word register baseline, both
qid maps re-derived and compared word for word) that contains no register write and always fails
closed; the derived W2 write set is tabulated in section 7.7. It has never run on hardware.

**Decode update 2026-11-19 (task-57, host-only):** the switch egress path needed for TX frames
has been decoded further and the R1/R3/R5 probe-configuration stage is now implemented behind a
third gate, `luofu_switch.egress=1` (patch `025`, never executed). The additions are: the R1/R3/R5
write sequences and derivations (section 7.7, rows R1/R3/R5; R4 is read-only conformance), the
EQM indirect command-word layout (section 7.5), a decode of the `port_bitmap` u64 cell order and
its allocator walk (section 6.3), and a new section 7.8 that separates what is proven about a TX
frame's egress selection from what is still [U]. No register was written for this update; the
validation is source/compile host-side only.

The subject is the internal Ethernet switch of the Hi5671Y "luofu" SoC (Cudy WR3000 v2 board,
`R116`): the vendor modules `hi_lsw_pub.ko`, `hi_lsw_dp.ko`, `hi_lsw_pfe.ko`, `hi_swa.ko`, the
CFE layer `hi_kcfe_pub.ko` + `hi_kcfe_lrn*.ko`, the adapter `hi_kcara.ko`, and the small
consumers (`hi_kport_mirror.ko`, `hi_netif.ko`, `hi_wse.ko`, `hi_khslink.ko`). The RE is done
on the vendor modules as **local, read-only, never-published fixtures** (parent
`rootfs-2.4.15/lib/hisilicon/ko/*.ko`; the same files exist in `rootfs-2.5.24` and are
byte-identical, see section 2.2). None of this code, and no vendor blob, is committed or copied
into the published half.

What this document is **not**:

- not a boot/flash/bench claim - no register was read or written on the router for it;
- not an upstream submission review - the design target here is only "which clean-room driver
  shape is defensible and what is the minimum path to it";
- not a replacement for the GPL request. If the vendor source arrives it converts most of
  sections 5-8 from RE to port ([GPL-REQUEST.md](GPL-REQUEST.md) items 2-4, [X]).

---

## 1. TL;DR: what the RE established

1. **The LSW is not a tag switch; it is a queue/queue-map engine plus a classification/forwarding
   engine.** There is no CPU-port tag format anywhere in the five-module symbol set, and the
   vendor's own kernel integration is switchdev on top of per-port netdevs
   (`hi_swa` imports `switchdev_handle_port_obj_add/del`), not DSA. This answers the DSA-vs-
   switchdev question left open in [TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.1 and
   [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 5.3: **switchdev, not DSA** [V/I].
2. **The vendor `hi_swa` switchdev object handlers are compiled stubs.** `swa_switchdev_event`,
   `swa_handle_port_obj_add`, `swa_handle_port_obj_del` and `swa_netdevice_check` are each
   `mov r0,#0; bx lr` in both rootfs fixtures. The shipped firmware registers the notifier and
   then does nothing with bridge/VLAN objects; the real forwarding work is done by the CFE
   learning stack through the PFE tables. A clean-room D2 offload layer therefore has to be
   written, not copied [V; section 9.2].
3. **The full module layering and the kernel<->LSW API surface are recovered**: register-access
   layer (`hi_lsw_pub`), queue/EQM/EPS/IPS manager (`hi_lsw_dp`), classification/forwarding
   tables (`hi_lsw_pfe`), CFE service (`hi_kcfe_pub`), learning libraries (`hi_kcfe_lrn*`), and
   the PFE adapter (`hi_kcara`) [V; section 4].
4. **The synthetic "module id" register map is decoded.** The shared register layer addresses
   every LSW block as `addr = (module_id << 24) | offset` against a 256-entry table of
   `{base, void __iomem *, size}` records. The DP driver registers ids `0x40..0x44` =
   `dp/eps/eqm/fam/ips`; the PFE driver registers ids `0,1,3,5,6,7,8,9,0xa,0xb` =
   `ifc/l2/napt/mcc/opp/pa/pe/pfe_glb/prbs/pub` (ids 2 and 4 are skipped) [V; section 5.2].
5. **The PIE qid-map open question is resolved.** [PIE-DATAPATH.md](PIE-DATAPATH.md) section 7.4
   asked which physical window "module 6" is: it is PFE id 6 = the `opp` window
   `0x12400000`, and the two qid tables live at `opp+0x42800` and `opp+0x43800` [V; section 7.3].
6. **Port and queue model recovered at the code-shape level**: a 46-entry software port table
   (56-byte entries: valid flag, port id, qid base, q_num with a default of **8**), a
   96-entry queue bitmap walk, an 8-QIDs-per-port mapping loop, and two hardware qid-map tables
   indexed by port-id classes (0-15, 40-47, >47) [V code shape; sections 6-7].
7. **The physical switch port namespace is visible** in the DFX name tables (`port-pie`,
   `port-prbs/loop`, `port8`..`port12`, `port0`, `port-xge`, `port13`, `port-woe`) and in the
   EPS counter labels (`port3(prbs)`, `port4(pie0)`..`port7(pie3)`) [V; section 6.2].
8. **The DT `port_bitmap = <0xf01 0x1ff0>` is read as a u64** by the DP probe and handed to the
   queue manager; what the words mean is still **[U]** and must be answered by bench reads, not
   guessed from bit positions (the values do not decode as a naive 0-12 port bitmap) [V read,
   U semantics; section 6.3].
9. **The CFE/rootif contract is a vendor-userspace compatibility layer, not a hardware API.** A
   rootif is a *named Linux netdev* registered into a 32-entry CFE table by
   `hi_cfe_add_rootif`, driven from netdevice events (NETDEV_UP/DOWN/UNREGISTER), and used to
   attach VLAN filters, sub-interfaces, stats rules, tag actions and FDB entries to a bridge or
   VLAN device created by OpenWrt. The shipped firmware deliberately sets
   `hi_kcfe_pub.g_intf_bypass=1` and *then* loads `hi_swa` ("switch adaptor for openwrt")
   [V; section 9].
10. **Bootable image exists, no network driver has run on hardware.** The bench image boots to
    userspace (2026-11-19 attended bring-up), so the LSW/PIE/GEMAC drivers can now be reached by
    an attended boot, but **no LSW register write and no PIE/GEMAC data-path execution has
    happened yet**: every hardware statement below stays [U] until the W3-W5 campaign in section
    12.3 is run. Patches `021`/`022` implement the read-only baseline and the gated W3/W5 qid-map
    write path; patch `025` adds the R1/R3/R5 probe-configuration stage behind
    `luofu_switch.egress=1` (never executed; see 7.7 and 7.8). The clean-room path stays staged: D0 spec, D1 minimal
    forwarding (bridge in software, no offload), D2 switchdev offload (section 11).

---

## 2. Evidence base and method

### 2.1 Markers and citation convention

Same markers as [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.1:
**[V]** verified by a named artifact/reproducible host-side check, **[I]** inferred/planned,
**[U]** unknown/unmeasured, **[X]** blocked on an external dependency.

Citations: a plain path is in this repository; `parent:` prefixes a path in the private research
repository (never published); "ledger" is the parent `mem-entries.md`. All disassembly offsets
below are **module-relative `.text` offsets** for the exact fixture files named in section 2.2;
they are not hardware addresses and they change if the vendor rebuilds the module.

### 2.2 The fixtures (local-only, not published)

All LSW/CFE fixtures are ARM32 relocatable ELF modules built for
`5.10.201 SMP mod_unload ARMv7`, GPL-declared, "Hisilicon Inc.". They live only in the parent
rootfs trees and must never be committed to the published `opensource/` half (parent
`AGENTS.md` publish boundary; [HAZARDS.md](HAZARDS.md)).

| fixture | size (B) | `depends=` | one-line role |
| --- | --- | --- | --- |
| `hi_lsw_pub.ko` | 26,052 | `ksecurec,hi_basic` | register window plumbing + generic table manager |
| `hi_lsw_dp.ko` | 67,164 | `hi_lsw_pub,hi_basic,hi_kpie,ksecurec` | queues, EQM/EPS/IPS, PIE channel map, mem info |
| `hi_lsw_pfe.ko` | 52,412 | `hi_lsw_pub,hi_lsw_dp,hi_basic,ksecurec` | packet/flow forwarding tables (IFC/NAPT/OPP/...) |
| `hi_kcara.ko` | 32,092 | `hi_kcfe_pub,hi_lsw_pub,hi_lsw_dp,ksecurec` | CFE<->PFE adapter (flow/CAR/stat/MC/VLAN) |
| `hi_kcfe_pub.ko` | 64,928 | `hi_kpie,hi_basic,ksecurec` | CFE service: rootif/subif, FDB mirror, hooks, tags |
| `hi_kcfe_lrn.ko` | 72,152 | `hi_kpie,hi_kcfe_pub,ksecurec,hi_basic` | learning core (hooks, flow handling, aging) |
| `hi_kcfe_lrn_l2.ko` | 9,572 | `hi_kcfe_pub,hi_kcfe_lrn,hi_basic,ksecurec` | L2 MAC learning library (add/del/check/age/dump) |
| `hi_kcfe_lrn_mc.ko` | 14,220 | `hi_kpie,hi_kcfe_lrn,hi_kcfe_pub,...` | multicast learning library |
| `hi_swa.ko` | 11,340 | `hi_kcfe_pub,hi_basic,ksecurec,hi_kpie,hi_kcfe_lrn` | switchdev + rootif glue ("Switch Adaptor") |
| `hi_kport_mirror.ko` | 11,428 | `hi_lsw_pfe,hi_kpie,hi_kcfe_pub,kcfe_lrn,hi_basic` | port mirroring proc/DFX consumer |
| `hi_netif.ko` | 4,616 | `hi_kcfe_pub` | CPU-rate / DDOS proc consumer |
| `hi_wse.ko` | 12,028 | `ksecurec` | per-packet stats helper (no LSW dependency) |

Dependencies/sizes from `modinfo` and `ls -la` on the fixtures. Symbol sets were extracted with
the vendor-toolchain binutils from parent `build/linux-kbuild/toolchain/`
(`arm-none-linux-gnueabihf-nm`, `-objdump -dr --no-show-raw-insn`, `-readelf -s/-S/-r`); the two
rootfs trees carry identical copies (`sha256sum` equal for the LSW modules plus `hi_kcfe_pub`
and `hi_kpie`), so the 2.5.24 stock firmware is the same LSW generation as 2.4.15.

### 2.3 Reproducing the RE (read-only, host only)

From the parent repo root (tool paths relative to the parent checkout):

~~~sh
TB=build/linux-kbuild/toolchain/bin

# 1. module metadata and symbol surface
modinfo rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko      # description/depends/vermagic
$TB/arm-none-linux-gnueabihf-nm -n  rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko
$TB/arm-none-linux-gnueabihf-readelf -s rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko

# 2. disassembly with relocations (call targets and symbol references)
$TB/arm-none-linux-gnueabihf-objdump -dr --no-show-raw-insn \
    rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko > /tmp/hi_lsw_dp.dis

# 3. string tables: property names, proc/DFX names, table field names
strings -a rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_pfe.ko | head -200
~~~

To turn `.LC` symbol references into strings and to decode the pointer tables, a small pyelftools
helper was used (kept as gitignored local evidence under parent
`build/linux-kbuild/waves/2026-11-18-lsw/annotate.py`; it only reads the fixtures). The analysis
artifacts (`.nm`, `.dis`) are in the same local lane directory and are not committed.

**Limit of this evidence:** disassembly of an `-O2` ARM module recovers call graphs, table
layouts, address arithmetic, string names and register offsets; it does **not** recover field
semantics, bitfield meanings, hardware behaviour or ordering requirements. Everything in this
document that is a semantic claim is marked [I] or [U], deliberately.

---

## 3. The wired data path in one picture

The vendor shape, confirmed by symbols and the module graph (cross-checked with
[PIE-DATAPATH.md](PIE-DATAPATH.md) section 2):

~~~
   +-------------------------- Linux ----------------------------+
   |  br-lan (OpenWrt bridge)                                    |
   |     |  switchdev/netdev events (hi_swa: rootif table)       |
   |     v                                                       |
   |  eth0..eth3 per-port netdevs  (created by hi_kmac/patch 009) |
   |     |  ndo_start_xmit -> hi_net_dev_xmit                     |
   |     v                                                       |
   |  hi_kpie: PIE host queues, DMA rings, NAPI, per-port ctx     |
   +-----|-------------------------------------------------------+
         |  queue handle + 8-byte port descriptor {port, vport, 0,0}
         v
   +-----|------------- internal LSW (this document) --------------+
   |  hi_lsw_dp: queue manager (EQM tables, EPS/IPS, PIE map)      |
   |  hi_lsw_pfe: classification/forwarding (IFC/L2/NAPT/OPP/...)  |
   |  hi_lsw_pub: register windows + generic table manager         |
   +-----|-------------------------------------------------------+
         v
   GEMAC 0-4 (port 8..12; ports 9-12 wired on this board) -> MDIO PHYs -> RJ45
   CFE control plane (hi_kcfe_pub + hi_kcfe_lrn* + hi_kcara) sits above,
   programming the same PFE tables through the hi_adp_* / hi_cfe_acc_* API.
~~~

The important structural finding for a clean-room port (already stated in
[REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 1 and [PIE-DATAPATH.md](PIE-DATAPATH.md)
section 2.1): **the MAC is a phylink front end only; PIE owns the rings/DMA; the LSW owns the
egress decision and the queue maps; CFE is an optional software forwarding layer on top.** None
of the vendor modules may be loaded for a clean-room port, so all four roles must be covered by
our own drivers (patch 009 + patch 015 + LSW driver D1/D2).

---

## 4. Module layering and responsibilities

### 4.1 `hi_lsw_pub` - register windows + generic table manager [V]

Exported symbols (`__ksymtab_strings`):

- register access: `hi_lsw_reg_module`, `hi_lsw_unreg_module`, `hi_lsw_read`, `hi_lsw_write`,
  `hi_lsw_read64`, `hi_lsw_write64`, `hi_lsw_read_bits`, `hi_lsw_write_bits`,
  `hi_lsw_read_tab128`, `hi_lsw_write_tab128`, `hi_lsw_wait_set`, `hi_lsw_wait_clr`;
- table manager (exported): `hi_tab_mgr_create`, `hi_tab_mgr_delete`,
  `hi_tab_mgr_alloc_hash(_unlock)`, `hi_tab_mgr_alloc_direct(_unlock)`, `hi_tab_mgr_free(_unlock)`,
  `hi_tab_mgr_hold`, `hi_tab_mgr_get_data(_unlock)`, `hi_tab_mgr_set_data(_unlock)`,
  `hi_tab_mgr_get_id(_unlock)`, `hi_tab_mgr_get_next_data`, `hi_tab_mgr_get_tab_ref`,
  `hi_tab_mgr_get_age`, `hi_tab_mgr_get_max` (internal, not exported: `hi_tab_mgr_init/exit`,
  `tab_mgr_kthread`, `hi_tab_mgr_clr_by_id`, `hi_tab_mgr_dump`).

The register convention is the single most useful RE result (section 5.1). The table manager is
a *software* framework (per-table control objects, hash or direct-indexed data arrays, an aging
kthread controlled by the `mgr_thread_time` module parameter, and DFX registration via
`hi_tab_mgr_dump`). It is what ties hardware tables (`read_tab128`/`write_tab128` through the
same synthetic address space) to software iteration/aging. [V symbols; the cfg struct layout is
partially recovered in section 5.1 and marked [I] where inferred.]

### 4.2 `hi_lsw_dp` - queues, EQM/EPS/IPS and the PIE handoff [V]

Probe `hi_dp_probe@0x8b0`; init order inside the probe is readable end-to-end:

1. read DT u64 property `port_bitmap` (string at `.rodata.str1.4+0x150`); missing property is a
   hard error (`get dts port_bitmap err`, `+0x15c`);
2. read `hwver` (default `0x10` stored first at the `.LANCHOR0` bss cell; the board DT supplies
   `0x250`) and `cell-size` (selector 0..3 for 0x100/0x200/0x400/0x800 cells, stored at
   `.data+0x50`);
3. `devm_clk_bulk_get_all()` + `clk_bulk_prepare/enable` on `lsw_dp_clk` (DT fixed-clock
   166,000,000 Hz = `0x9e4f580`); error path `[LSW]dp clk err`;
4. `devm_reset_control_array_get(dev, shared=1)` for the DT reset pair, then
   deassert/assert/deassert (a reset pulse), error path `[LSW]dp logic reset err`;
   later two named resets `dp_logic` (`.rodata.str1.4+0x1e8`) and `dpu_srst` (`+0x210`) are
   fetched with `__devm_reset_control_get` and pulsed in the same way. Note the DT reset-names
   are `"dp_logic"`/`"dp_srst"` while the code asks for `"dpu_srst"` - a vendor DT/driver
   mismatch to keep in mind ([U] whether that reset is actually obtained on this board);
5. five optional u32 DT properties `wifi_vport`, `plc_vport`, `chba_vport`, `voe_vport`,
   `fttr_vport` (names in a `.data` table at `.data+0x00`, 16-byte stride; default 256 each and
   cumulative offsets are computed into the next entry). The board DT has none of them, so all
   five are "absent"; the resulting configuration is [U];
6. five windows registered into `hi_lsw_pub` (table at `.rodata+0x0c`, 5 x `{module_id, name}`,
   terminator `.rodata+0x34`): id `0x40` -> `dp`, `0x41` -> `eps`, `0x42` -> `eqm`,
   `0x43` -> `fam`, `0x44` -> `ips`. Each window name is looked up with
   `platform_get_resource_byname()` and mapped with `devm_ioremap_resource()`;
7. initial IPS/DP writes: `ips+0x10 |= 0x10000`, `ips+0x08 = 42`,
   `dp+0x68 = 0, dp+0x64 = 1, dp+0x64 = <cell-size selector>,`
   `dp+0x1c = (<u16 field from the probe state> | 0x10000)`; `fam+0x504` read/modify/written against a cell-count
   comparison, `eqm+0xa4` read/modify/written with `(value-0x40)` in bits 0..13;
8. `hi_mem_info_init(dev, hwver)` (`@0x58e0`): walks a list of memory windows resolved by
   compatible string or resource name. Strings present in the fixture: `hsan,queue_reserved`,
   `hsan,flow_reserved`, and resource names `queue`, `flow`, `voip`, `woe`; the {start,size}
   pairs are recorded and later printed by the `memory` DFX dump. The iteration itself is only
   partially recovered - treat the exact per-window mapping as [U], but the *class* of window
   (DDR memory, not register MMIO) is [V] (see section 5.4);
9. `hi_queue_init(hwver)` (`@0x507c`): calls `hi_eqm_hw_init()`, initialises the 46-entry port
   table (section 6.4), reads EQM registers `+0xa4`/`+0x18`, writes `eqm+0x18`, `eqm+0x38`,
   `eqm+0x40` (bit31 set = enable), writes an EQM table entry (table id 19 via
   `hi_eqm_write_tab`) with 1000 in bits 0..13, walks a 96-bit queue bitmap
   (`r7:r6` passed from the probe: the low/high words of the u64 `port_bitmap` - see the caveat
   in 6.3), creates table id `0` with EQM set/dump callbacks, registers DFX nodes and a
   netdevice notifier, and finally installs `hi_dev_queue_enable` through the PIE
   `hi_net_queue_hook` export;
10. `hi_dp_tab_init()` (creates table ids 0x703/0x704), `hi_pie_map_init(hwver)`
    (section 7.4), `hi_dp_dfx_init()`.

Imports that define its place in the system: `hi_net_queue_hook`,
`hi_pie_get_gro_ch_start/num`, `hi_pie_get_woc_ch_start/num` (PIE API). Its ksymtab export
surface (33 entries) is the queue/DFX API that `hi_kcara`/`hi_swa`/`hi_kport_mirror` consume:
`hi_queue_get_qid/get_q_num`, `hi_queue_set/get_{sch,buf,shaping,egr_shaping}`,
`hi_queue_opp_map_hook`, `hi_dev_queue_{set,get}_*`, `hi_eqm_write_tab/read_tab/get_que_port`,
`hi_dev_queue_enable`, `hi_lsw_get_vport`, `hi_flow_hook_{set,get,del}`, `hi_port_set_igr_car`,
`hi_system_mem_info_get`, `hi_dp_get_hwver`, `hi_tpid_get_idx`, `hi_xoe_set_{tx,rx}_cell_th`
(the probe/init functions `hi_dp_probe`, `hi_queue_init/exit`, `hi_pie_map_init`,
`hi_mem_info_init`, `hi_eqm_hw_init` are module-local).

### 4.3 `hi_lsw_pfe` - classification/forwarding tables [V]

Probe `hi_pfe_probe@0x23a4`: registers ten windows (section 5.2), gets `lsw_pfe_clk`
(fixed-clock 333,000,000 Hz = `0x13d92d40`) and the reset pair `pfe_logic`/`pfe_srst`
(assert-then-deassert), reads `hwver` (board `0x250`; the probe's log string hard-codes swver
`v100`), then runs the sub-blocks in this order:
`hi_pfe_init()` -> `hi_pfe_pub_init()` -> `hi_pfe_opp_init()` -> `hi_pfe_mcc_init()` ->
`hi_pfe_napt_init()` -> `hi_pfe_ifc_init()` -> `hi_pfe_dfx_init()`.

Table/flow operation functions defined in the module (all module-local and used as
table-manager callbacks; the **only** ksymtab export of `hi_lsw_pfe` is
`hi_port_set_mirror`): `hi_pfe_ifc_init`, `ifc_write`, `ifc_del`, `ifc_dump`, `nhp_write`,
`nhp_dump`, `tag_write`, `tag_dump`, `l3intf_write`, `l3intf_dump`, `car_write`, `car_del`,
`car_profile_write`, `car_profile_dump`, `hi_pfe_flow_napt_clr_all`,
`pfe_napt_v4/v6_write_flow`, `pfe_napt_v4/v6_del_flow`, `pfe_napt_v4/v6_get_age`,
`pfe_napt_v4/v6_dump_flow`, `hi_pfe_mcc_init`, `hi_pfe_opp_init`, `hi_pfe_pub_init`,
`hi_pfe_init`, `hi_pfe_drop_cnt`, `hi_pfe_tocpu_cnt`, plus `port_qid_map_set`/`qid_map_set`
(section 7.3). Table ids created here are in the `0x700..0x712` range (section 8.1). This is
why the CFE adapter `hi_kcara` reaches the PFE tables through the `hi_tab_mgr_*` API instead
of calling these functions directly: the callbacks are wired into each table at creation.

### 4.4 `hi_kcara` - the CFE<->PFE adapter [V]

Description literally "CFE Adapter for RT Achitecture". It imports `hi_cfe_adp_*_register` from
`hi_kcfe_pub` (lsw/port/mc/ddos registration), the `hi_lsw_pub` table manager
(`hi_tab_mgr_alloc_hash/direct`, `get_data`, `set_data`, `get_age`, `get_id`, `get_max`,
`free`) and the `hi_lsw_dp` queue/shaper API (`hi_queue_set_egr_shaping`,
`hi_queue_get_egr_shaping`, `hi_dev_queue_set_shaping/sch/egr_shaping`,
`hi_port_set_igr_car`). Its own symbols are the flow/ACL/CAR/stat writers:
`hi_cara_flow_add/del/get_age`, `hi_cara_stat_*`, `hi_cara_intf_set_stat`,
`hi_cara_mc_ip_group_add/del`, `hi_cara_cid_*`, `hi_cara_vlan_filter_item_add/del`,
`hi_cara_set_ddos*`, `hi_ca_set_q_sch/egr_rate/pq_rate`, and
`hi_cara_flow_set_act_l3intf/tag/nhp` (strings). The table names it touches appear in its
strings: `TAB_L3INF_STAT_IDX`, `TAB_TAG_ACT`, `TAB_L3INF`, `TAB_NHP_STAT_IDX`, `TAB_NHP`,
`TAB_FLOW_STAT_IDX`, `TAB_FLOW_V4`, `TAB_FLOW_V6`, `TAB_GLB_ATTR_ENABLE_FLOW_AGE`, `TAB_CNT`.
This is the layer that maps a flow/ACL object to PFE table entries; it is **not needed by our
D1**, but it is exactly the reference for D2's offload write path [I].

### 4.5 `hi_kcfe_pub` - CFE service layer [V]

Description "Hisilicon CFE PUB Service". Owns the software model used by the vendor control
plane: interface objects (rootif + subif + attributes), a hash/direct table framework
(`hi_cfe_tab_*`, `hi_cfe_list_*`, same shape as the LSW one), packet hooks
(`hi_cfe_add_hook/del_hook` with hook point names `igr_pre_lrn_done`, `igr_post_lrn_done`,
`egr_pre_vdev_done`, `egr_pre_lrn_done`, `egr_post_lrn_done`, `igr_pre_vdev_done`,
`igr_post_vdev_done`, `egr_post_vdev_done`), VLAN/tag manipulation
(`hi_cfe_tag_add_by_attr`, `hi_cfe_tag_del`, `hi_cfe_tag_filter_add/del`,
`hi_cfe_add_rootif_vlan`/`del_rootif_vlan`), FDB mirroring (`hi_cfe_intf_set_br_fdb`,
`hi_cfe_intf_get_br_fdb`, `hi_cfe_get_intf_by_mac`; dump formats with fields
`mac/dst/updated/used/flag/vid`), netdev vif creation (`alloc_netdev_mqs`, `ether_setup`,
`register_netdevice`, `hi_cfe_netdev_*` ops), and the `hi_adp_*` adaptation API consumed by the
learning libraries. It imports `hi_br_fdb_check_addr()` from the **vendor-patched Linux bridge**
(no vendor `.ko` defines it; it is one of the missing bridge patches listed in
[DOCUMENTATION-HUNT.md](DOCUMENTATION-HUNT.md) section 4.2 and [GPL-REQUEST.md](GPL-REQUEST.md)
item 3) [V symbol, U bridge semantics].

Module parameter `g_intf_bypass` (uint, default 0) is checked in `hi_cfe_intf_set_dev`,
`hi_cfe_add_rootif`, `hi_cfe_del_rootif`, `hi_cfe_add_subif`, `hi_cfe_del_subif`; the shipped
firmware sets it to 1 (see 4.7). [V]

### 4.6 `hi_kcfe_lrn*` - the software learning/forwarding engine [V]

`hi_kcfe_lrn` is the core ("CFE LRN Service", 72 KB): packet RX/TX hooks (`hi_cfe_lrn_rx`,
`hi_cfe_lrn_tx`, `hi_cfe_flow_handle`), per-protocol learn-library registration
(`hi_cfe_lrn_lib_reg/unreg`), flow tables, aging timers and DFX. The per-protocol libraries are
separate modules (`hi_kcfe_lrn_l2/mc/l3/l4/l2tp/pptp/dslite/vxlan/gro/ddos/ucmac_mc` +
`hi_kcfe_mc_list`), each with the same shape: `_add`, `_del`, `_check`, `_get_age`, `_dump`,
`_get_cnt`. The L2 one imports the PFE-facing API
`hi_cfe_acc_set_dir/set_mac/set_qos/set_stat/set_tagact`, `hi_adp_flow_add/del/get_age`,
`hi_cfe_lrn_check_flow_exist`, `hi_cfe_lrn_set_net_port` and `hi_cfe_is_br_forward` - i.e.
learning is implemented in software and programmed into the hardware as flows/IFC entries, with
the hardware L2 block used as a search/age cache (its counters are in `hi_lsw_pfe`'s DFX:
`port_move`, `age_drop`, `sport_clash`, `search_ok_mc`, `search_fail_mc`). [V symbols/counters,
table format U]

This layer is **not needed for D1** (a software-bridged D1 does not program flows), and **not
copied for D2** (we use the Linux FDB/switchdev objects as the source of truth). It is the main
thing the GPL source would make a port instead of an RE, and the reason "flow offload" is a D2+
item, not a D1 requirement.

### 4.7 `hi_swa` - switchdev/rootif glue ("Switch Adaptor Driver") [V]

Symbols: `swa_switchdev_event`, `swa_switchdev_blocking_event`,
`swa_handle_port_obj_add`, `swa_handle_port_obj_del`, `swa_netdevice_event`,
`swa_netdevice_check`, `is_swa_root_dev`, rootif management
(`hi_swa_add_rootif/down_rootif/get_rootif/unregister_rootif/dump_rootif`), and DFX hooks
(`swa_dfx_update_wanif/update_vlan_filter`). Imports: `hi_cfe_add_rootif`, `hi_cfe_del_rootif`,
`hi_cfe_add_subif`, `hi_cfe_del_subif`, `hi_cfe_flush_by_mac`, `hi_net_get_port`,
`switchdev_handle_port_obj_add/del`, `register_switchdev_(blocking_)notifier`,
`ppp_get_relate_dev`, `vlan_dev_real_dev`, `dev_get_by_name`.

Behaviour recovered from disassembly:

- `swa_rootif_init()` walks the net-namespace netdev lists and calls `hi_net_get_port()` on
  each non-loopback device (a PIE per-port lookup; the return value is not used on this path, so
  the net effect is an observation/probe of the port mapping, not a registration).
  `swa_rootif_exit()` walks the same lists and calls `hi_cfe_del_rootif(name)` for each valid
  non-loopback MAC netdev, using a local name helper (it substitutes a fallback for an empty
  name or a name containing `%` - the exact fallback string and the transformation are [U]),
  cleaning up CFE roots at unload;
- `hi_swa_add_rootif(name, a, b)` finds/allocates a 32-entry rootif record
  (`{char *name; void *a; void *b; u32 enable}`), calls `hi_cfe_add_rootif(record)` and sets
  `enable=1` on success. `hi_swa_down_rootif(name)` calls `hi_cfe_del_rootif()` and clears
  `enable`. `hi_swa_get_rootif(name)` is a 32-slot `strcmp` lookup; `hi_swa_unregister_rootif`
  blanks a slot. `hi_swa_dump_rootif` prints one DFX line per live slot;
- `swa_netdevice_event()` filters `is_swa_root_dev()`, then handles NETDEV_UP (add rootif; if
  an entry exists with `enable==0`, re-add using its saved args), NETDEV_DOWN (down rootif) and
  NETDEV_UNREGISTER;
- `swa_switchdev_blocking_event()` dispatches `switchdev_handle_port_obj_add/del` for
  VLAN/MDB-type objects and calls `hi_cfe_flush_by_mac()` when a per-port MAC object is removed;
- **the four switchdev/port-object handlers are stubs**: `swa_switchdev_event@0x47c`,
  `swa_handle_port_obj_add@0x484`, `swa_handle_port_obj_del@0x48c`, `swa_netdevice_check@0x474`
  all `mov r0,#0; bx lr`. The `switchdev_handle_port_obj_add/del()` calls therefore always
  reach a zero-returning handler: no bridge/VLAN offload happens in this build.

Loader context: `hi_swa` is **not** in `etc/rc.d/luofu`. It is insmodded by
`etc/init.d/hsan_prepare` START=09, *after* `/etc/rc.d/luofu` (all vendor modules) and after
`echo 1 > /sys/module/hi_kcfe_pub/parameters/g_intf_bypass`, with the comment "switch adaptor for
openwrt" (parent `rootfs-2.4.15/etc/init.d/hsan_prepare`, lines ~55-71; [V] local fixture). That
ordering is the single most important control-plane fact for a clean-room design: the shipped
firmware uses the CFE in bypass mode and adds switchdev plumbing on top of real OpenWrt netdevs.

### 4.8 Small consumers and what they prove [V]

- `hi_kport_mirror.ko` imports `hi_net_dev_xmit/xmit_lock`, `hi_net_get_port`,
  `hi_cfe_add_hook/del_hook`, `hi_cfe_flush_by_devname`, and depends on `hi_lsw_pfe`; it
  implements proc-driven mirroring by re-injecting skbs through the PIE xmit path and by calling
  the PFE `hi_port_set_mirror()` (symbol in `hi_lsw_pfe`). This is evidence that mirroring is
  a PFE (hardware) feature plus a software re-inject path, not an LSW queue feature.
- `hi_netif.ko` only wraps `hi_cfe_set_cpu_rate`/`hi_cfe_set_ddos` (proc consumers).
- `hi_wse.ko` is a standalone per-packet statistics helper; no LSW dependency.
- `hi_khslink.ko` is HSLINK (ARP/neighbour bookkeeping and skb-header manipulation); it consumes
  CFE hooks and FDB API but owns no switch registers. It is the module whose teardown interacts
  badly with the vendor bridge in the v17 bench line ([VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md)
  section 2.2), which is further evidence that the vendor's control plane is deeply coupled to
  its patched kernel and must not be reproduced.

---

## 5. Register window map

### 5.1 Access convention: synthetic 32-bit addresses, 256 module slots [V]

All LSW register access goes through `hi_lsw_pub`, which owns a 256-entry table (`g_modules`,
3072 B = 256 x 12 B, `.bss`) of records:

~~~
struct { u32 base; void __iomem *iomem; u32 size; };   /* 12-byte stride */
~~~

Registration is `hi_lsw_reg_module(base = module_id << 24, iomem, size)`. It stores
`{base, iomem, size}` in slot `module_id` and fails with `0x8fff_ff7d` for invalid arguments or
`0x8fff_fff1` when the slot is already registered ("reg module not exist reg(%08x)" and
"reg %08x outrange(%08x, %u)" are the shared error strings).

Every accessor decodes the address the same way (`hi_lsw_read@0x2d4`, `hi_lsw_write@0x354`,
`hi_lsw_read64@0x4dc`, `hi_lsw_write64@0x3d0`, `hi_lsw_read_bits@0x44c`,
`hi_lsw_write_bits@0x564`):

~~~
id     = addr >> 24;
offset = addr - (id << 24);
if (!g_modules[id].iomem)         -> printk, return 0 / drop
if (offset >= g_modules[id].size) -> printk, return 0 / drop
return *(u32 *)(g_modules[id].iomem + offset);
~~~

`hi_lsw_read_tab128@0x1fc` / `hi_lsw_write_tab128@0x268` address **16-byte table entries**
(4 consecutive u32 words) as `addr = table_base + index*16`; each word is bounds-checked
individually. This is the primitive for "one 128-bit table row per call", used by the table
manager's hardware callbacks. `hi_lsw_wait_set/clr` poll a register bit with `arm_delay_ops`
(timeouts return `0x8fffffd8`); the two wait helpers work on a raw resolved pointer and a
bitmask (shift of the bit index), i.e. they are a poll-until-set/clear convenience with a
caller-supplied retry count.

Consequences for a clean-room driver (design rules, [I]):

- The synthetic-address layer is a vendor abstraction with no upstream equivalent. Our drivers
  should use `regmap`/`readl`+`writel` on named DT windows directly; do not reproduce the
  256-slot indirection.
- The synthetic addresses in the disassembly (`0x40000068`, `0x42000048`, `0x44000010`,
  `0x06042800`, ...) are **not** physical addresses. They decode as
  `{block id, offset}` and only section 5.2 ties block ids to DT windows.
- The table manager's cfg struct is 44 bytes: name pointer at `+0`, sizing fields at
  `+8/+12/+16` (total/key/data sizes; key <= data is enforced and data must be > 15), a
  flags/type byte at `+26` (bit0 = direct vs hash; bits 1..6 carry the type/aging class), and
  hardware callbacks from `+28` onwards (set/read/get-age/dump/clr; the exact per-field offsets
  beyond the name, sizes and the type byte are [I]).

### 5.2 Module-id -> DT window map [V]

Decoded from the two probe registration tables (pointer tables in `.rodata`, with
`platform_get_resource_byname()` names next to each id; exact locations in 4.2/4.3).

**DP table** (`.rodata+0x0c`, 5 entries, end pointer `.rodata+0x34`):

| module id | DT `reg-names` entry | DT window (board `luofu-r116-pinned.dts`) | size |
| --- | --- | --- | --- |
| `0x40` | `dp` | `0x11000000` | `0x80000` |
| `0x41` | `eps` | `0x11280000` | `0x80000` |
| `0x42` | `eqm` | `0x11200000` | `0x80000` |
| `0x43` | `fam` | `0x11100000` | `0x80000` |
| `0x44` | `ips` | `0x11080000` | `0x80000` |

**PFE table** (`.rodata+0x48c`, 10 entries of `{u32 id, const char *name}`, end
`.rodata+0x4dc`; ids are passed as `id << 24`):

| module id | DT `reg-names` entry | DT window | size |
| --- | --- | --- | --- |
| `0` | `ifc` | `0x12100000` | `0x80000` |
| `1` | `l2` | `0x12180000` | `0x80000` |
| `3` | `napt` | `0x12280000` | `0x80000` |
| `5` | `mcc` | `0x12380000` | `0x80000` |
| `6` | `opp` | `0x12400000` | `0x80000` |
| `7` | `pa` | `0x12080000` | `0x80000` |
| `8` | `pe` | `0x11180000` | `0x80000` |
| `9` | `pfe_glb` | `0x12000000` | `0x80000` |
| `0xa` | `prbs` | `0x11380000` | `0x80000` |
| `0xb` | `pub` | `0x12480000` | `0x80000` |

Ids `2` and `4` are unused by PFE in this build; no module registers them [V - absence in the
decoded table plus the single `hi_lsw_reg_module` call site in each module]. The id space is
per-LSW-instance, not global: DP picks `0x40+` while PFE picks `0x0..0xb`, which is
evidence that the ids are arbitrary private handles, not hardware numbers [I].

A third registration source is ruled out: the only two `hi_lsw_reg_module` call sites in the
fixtures are these two probe loops [V], so no other module registers windows into the shared
layer. That is why the synthetic addresses carried by the qid-map code (`0x0604xxxx`) must map
to one of the ten PFE windows and not to a hidden window.

**Measured live-DT confirmation (2026-11-18) [V]:** the running stock kernel's post-overlay
`/proc/device-tree/lsw_dp/reg` and `lsw_pfe/reg` (raw big-endian bytes saved under
`parent:build/bench-inventory/20261118T130512Z-inv1-run/followup/`) reproduce the two tables
exactly: `lsw_dp` `reg-names = "queue flow dp eps eqm fam ips"` with windows
`0x11000000/0x11280000/0x11200000/0x11100000/0x11080000` (all `0x80000`), and `lsw_pfe`
`reg-names = "ifc l2 napt mcc opp pa pe pfe_glb prbs pub"` with
`0x12100000/0x12180000/0x12280000/0x12380000/0x12400000/0x12080000/0x11180000/0x12000000/0x11380000/0x12480000`
(all `0x80000`). The window map and the DT-name order are therefore measured on the live node,
not only decoded from the fixtures. The only live difference from the base DT is the
queue/flow base (5.4).

### 5.3 Registers observed in the fixtures [V addresses; semantics U unless stated]

Only addresses that appear as literal constants feeding `hi_lsw_read/write/tab128` are listed.
They are **call-site addresses**, i.e. `module window + offset`; "seen once" means the meaning
was not recovered.

| block (id) | offsets observed | context |
| --- | --- | --- |
| `dp` (`0x40`) | `0x1c`, `0x64`, `0x68` | probe: `+0x68` = memory number, `+0x64` = 1 then cell-size selector, `+0x1c` = halfword OR `0x10000` |
| `eps` (`0x41`) | `0x1008`, `0x2084`-`0x20b4` (DFX counter block), `0x50000` (tab128) | queue/DFX; per-port counters incl. `port3..port7`; the channel-map words `+0xB00/+0xB80` (section 7.4) |
| `eqm` (`0x42`) | `0x00`, `0x18`, `0x38`, `0x40`, `0x48`, `0x5c`, `0xa4`, `0x504`, `0x2004`-`0x203c`, `0x600c` | queue table command/status (`+0x48`), write data (`+0x5c`), enable (`+0x40` bit31), queue/cell counters |
| `fam` (`0x43`) | `0x504` | queue-count/cell comparison in probe |
| `ips` (`0x44`) | `0x00`, `0x08`, `0x10`, `0x1120`, `0x1344` | probe init (`+0x10` bit16, `+0x08` = 42), counter reads |
| `ifc` (`0`) | `0x000000`, `0x01040000` (DFX counters) | classifier counters (`ifc_hit`, `ifc_unhit`, ...) |
| `l2`, `napt`, `mcc`, `opp`, `pa`, `pe`, `pfe_glb`, `prbs`, `pub` | DFX counter blocks (`0x00`, `0x1004`, `0x1008`, ...) and the qid tables at `opp+0x42800`/`opp+0x43800` | statistics; qid maps (section 7.3) |

No bit-level meaning is claimed for any of these; the list exists so the D1/D2 register work has
a starting inventory and so a bench read plan can be written without guessing addresses. **Do
not write any of these from a 6.18 kernel before the writes are individually justified and
attended (section 12).** [I]

### 5.4 The `queue` and `flow` windows are DDR memory, not MMIO [V/I]

The DT gives `lsw_dp` seven windows, but the driver registers only five; the two unregistered
ones are `queue` and `flow` (base-DT values `0x87b00000`, 4 MB and `0x87f00000`, 1 MB; the
live post-overlay values are measured below). Three independent observations say they are
**memory the engine consumes**, not register pages:

1. no `hi_lsw_reg_module()` call (and therefore no synthetic-address access) ever covers them;
2. both fall inside the DDR aperture described by the vendor DT (`memory@0x80500000`, 128 MB at
   `0x80500000`), and the U-Boot board overlay *rebases* `queue` from `0x87b00000` to
   `0x80000000` ([soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8) - exactly the sort
   of move done for a reserved DDR region, not for an SoC register page;
3. `hi_mem_info_init()` in `hi_lsw_dp` resolves memory windows named `queue`/`flow` (plus
   `voip`/`woe`) via `hsan,queue_reserved`/`hsan,flow_reserved` compatible strings or
   `platform_get_resource_byname()` and records `{start, size}` for buffer accounting/DFX; the
   `queue` window is also the one that the vendor DT overlay fixes up [V strings; exact
   iteration partially [U]].

**Measured 2026-11-18 - the live base is [V], not [U].** The read-only follow-up read the running
stock kernel's post-overlay node (same boot as the register inventory, boot_id unchanged) and the
raw big-endian `reg` bytes decode to:

| live window | address | size |
| --- | --- | --- |
| `queue` | `0x80000000` | `0x400000` (4 MiB) |
| `flow` | `0x80400000` | `0x100000` (1 MiB) |

So the U-Boot overlay does rebase the queue/flow windows to the bottom of DDR (from the base-DT
`0x87b00000`/`0x87f00000`) on this board, and the engine consumes those regions at those
addresses. The base-DT values in our conversion source are pre-overlay values and must not be
programmed from the base tree. A clean-room driver still must not map these as MMIO: they are
reserved DDR the engine reads/writes, and how to reserve/account for them in the binding is a
design question (D1b). The queue/flow *layout* inside the regions remains [U]. Evidence:
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3.1.

### 5.5 Clocks, resets, IRQs [V from the vendor DT, semantics U]

| item | value in the pinned vendor DT ([soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines 1078-1123) |
| --- | --- |
| `lsw_dp` clocks | `lsw_dp_clk` fixed-clock `0x9e4f580` = 166,000,000 Hz |
| `lsw_dp` resets | `reset-names = "dp_logic" "dp_srst"`, cells `<0xf 0x30 0x0>` / `<0xf 0x30 0x1>` (CRG id 0xf, reset word `0x30`, bits 0/1) |
| `lsw_pfe` clocks | `lsw_pfe_clk` fixed-clock `0x13d92d40` = 333,000,000 Hz |
| `lsw_pfe` resets | `reset-names = "pfe_logic" "pfe_srst"`, cells `<0xf 0x30 0x3>` / `<0xf 0x30 0x4>` |
| `lsw_woe` | no `reg`; `interrupts = SPI 84,83,82,81,80`; `interrupt-names = "band0_comp_irq" "band1_comp_irq" "band2_comp_irq" "band0_txba" "band1_txba"`; `dma-coherent`; `hwver = 0x251` |
| extra driver-side reset | `hi_lsw_dp` also looks up a reset named `dpu_srst` (mismatch with DT `dp_srst`) - whether it resolves is [U] |
| `hwver` | both blocks: `0x250` in the board DT; DP default when absent `0x10`; PFE log generation string `v100` |

The reset cells are exactly the vendor two-cell form `(CRG page register offset, bit)` that
patch `004`/`014` re-express as generic resets
([../target-skeleton/target/linux/hisilicon/patches-6.18/004-arm-clk-reset-luofu-crg.patch](../target-skeleton/target/linux/hisilicon/patches-6.18/004-arm-clk-reset-luofu-crg.patch)).
The driver-side sequences are: DP = deassert/assert/deassert pulse on the array, then
assert/deassert on `dp_logic` and on `dpu_srst`; PFE = assert/deassert on the array. Whether the
order matters (and whether the vendor's `dp_logic`/`dpu_srst` mismatch means one reset is never
pulsed) is **[U]** and is a D1 bench item. No `lsw_woe` driver exists in either rootfs (the
module is absent), so the WoE interrupt path has **no software evidence at all** [V absence].
`hi_lsw_*` request no IRQs itself [V: no `request_irq`/`devm_request_irq` import in any LSW
module]; the only LSW-family interrupt consumers would be PIE (its own IRQs) and the absent WoE
driver.

### 5.6 Ownership overview

~~~
lsw_dp (7 DT windows, 5 registered)             lsw_pfe (10 DT windows, 10 registered)
  queue 0x87b00000/0x80000000 (DDR)              ifc      0x12100000  id 0
  flow  0x87f00000/0x80400000 (DDR)              l2       0x12180000  id 1
  dp    0x11000000  id 0x40                      napt     0x12280000  id 3
  eps   0x11280000  id 0x41                      mcc      0x12380000  id 5
  eqm   0x11200000  id 0x42                      opp      0x12400000  id 6
  fam   0x11100000  id 0x43                      pa       0x12080000  id 7
  ips   0x11080000  id 0x44                      pe       0x11180000  id 8
                                                 pfe_glb  0x12000000  id 9
                                                 prbs     0x11380000  id 0xa
                                                 pub      0x12480000  id 0xb
lsw_woe: no reg, SPI 80-84, no driver in rootfs
~~~

All ten PFE windows are `0x80000` bytes; DP's five register windows are `0x80000` bytes each.
The skeleton draft DTS already carries these nodes with `status = "disabled"`
([../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts) `lsw_dp`/`lsw_pfe`).

---

## 6. Port model

### 6.1 Physical ports [V]

The five `gemac` nodes carry `port = <8..12>`, one `reg` window each, a `phy-handle`, a
`phy-mode`, and a reset pair (per the pinned vendor DT lines 885-955). The board U-Boot overlay
for `board_id=3` disables `gemac0`/`gephy0` ([soc/vendor-dt-notes.md](soc/vendor-dt-notes.md)
section 8), so the live set is ports 9-12 = four MACs. The *measured* mapping from the running
vendor kernel (dmesg + `/sys/class/net/*/phydev`) is
`14380000.gemac eth0 -> mdio0:02`, `14400000.gemac eth1 -> mdio0:03`,
`14480000.gemac eth2 -> mdio0:04`, `14500000.gemac eth3 -> mdio0:05`, `14300000` absent
([ETHERNET-BRINGUP-FRAMING.md](ETHERNET-BRINGUP-FRAMING.md) section 2, measured) - note this
differs from the DT `phy-handle` order and was a real bench bug, so treat the MAC<->PHY mapping
as verified only by that measurement, not by the DT.

The board userspace mapping is `R116: WAN = eth3, LAN = eth0 eth1 eth2`
(parent `rootfs-2.4.15/etc/board.d/02_network`, [V] local fixture), consistent with the live
stack record in [VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) section 2.4. The bench
link is on router LAN `eth2` (parent `BENCH-FACTS.md`).

For the PIE side, the same numbers appear as the 8-bit `port` field in the 8-byte descriptor that
`hi_mac_probe()` passes to `hi_net_dev_probe()` ([PIE-DATAPATH.md](PIE-DATAPATH.md) section 2.2),
so `desc8.port` values are 9..12 for the live ports. The mapping from these MAC port numbers to
the LSW's internal port ids is **[U]** (see 6.2/6.3).

### 6.2 LSW port namespace as visible in the vendor code [V names; mapping U]

The DP DFX/dump tables name the ports (`.rodata.str1.4` strings at `+0x31c`-`+0x37c`):
`port-pie`, `port-prbs/loop`, `port8`, `port9`, `port10`, `port11`, `port12`, `port0`,
`port-xge`, `port13`, `port-woe`. The EPS counter dump (strings at `+0x788`-`+0x7a8`) labels
its queue-port rows `port3(prbs)`, `port4(pie0)`, `port5(pie1)`, `port6(pie2)`, `port7(pie3)`.

Two independent groupings follow from the names alone [I]: the EPS queue ports are a small
numbering (3=PRBS/loopback, 4-7=PIE channels 0-3) while the LSW egress ports include the MAC
ports 8-12 and special engines (`xge`, `woe`, PIE host path). The exact integer assignment of
`port-pie`/`port0`/`port13` and whether `port8..12` are *identical* to the DT MAC `port` numbers
is **[U]** - it is the first item of the D1 bench inventory (section 12.2).

### 6.3 `port_bitmap` - the queue allocation port bitmap [V read; decode 2026-11-19]

`hi_dp_probe` reads `port_bitmap` as a **u64** (`of_property_read_u64` to `sp+40`), and the
board DT supplies two cells `<0xf01 0x1ff0>`. Later the two words are passed as the two 32-bit
halves (`ldrd r2,[sp,#8]` at `+0xca8`) to `hi_queue_init()`. The 96-bit queue walk inside
`hi_queue_init` tests bits of that pair with a 64-bit shift idiom, i.e. the driver treats the
property as a **queue/port enable bitmap**, not a port count.

The cell order is the key. `of_property_read_u64()` assembles the value as
`cell[0] << 32 | cell[1]` (**big-endian assembly**, kernel API contract), so on the little-endian
target the two in-memory 32-bit words are the *second* DT cell first: word 0 = `0x1ff0`
(bits 4..12) and word 1 = `0xf01` (bits 0, 8, 9, 10, 11 of that word -> overall bits 32, 40, 41,
42, 43). The `hi_queue_init` walk visits exactly the port ids **{4..12} and {32, 40..43}** and
calls the qid allocator for each. That reading is **[V]**: it is the only one under which the
allocator output reproduces the measured qid map, and it reproduces it word for word (7.3) -
ports 4..12 take qids 1..72 in order, port 32 has `q_num = 0` (it is allocated a base qid but
consumes no qid run), and ports 40..43 take qids 73..104. The property is therefore the
**queue-allocation port bitmap**: a set bit means "this LSW port owns a contiguous qid run
at init". What remains [U] is why the board's bootloader/DT writes that particular port set, not
what the bits select. The vendor's own U-Boot overlay does not modify `port_bitmap` [V], so the
base-DT value is what the shipped kernel runs with. **Measured 2026-11-18 [V]:** the live
post-overlay `/proc/device-tree/lsw_dp/port_bitmap` bytes are
`00 00 0f 01 00 00 1f f0` -> `<0xf01 0x1ff0>`, identical to the base DT.

Another unresolved probe-level detail worth recording: while iterating the u64, the vendor code
tests bit 16 of the low word (`ubfx r3, r7, #16, #1`) and stores the result into the queue-manager
state, using it to select an initial value 15 (`.data+0x18c`/state[0]). The meaning is **[U]**;
[I] it looks like an "is root/host port present" flag. Either way it shows the bitmap is
interpreted by the queue manager, not by the port table.

### 6.4 The 46-entry software port table [V layout, U semantics of some fields]

`hi_lsw_dp` keeps a global array of 46 entries x 56 bytes in `.data` (reached through the
`.LANCHOR0` anchor at section 28 offset `0xd0`; `hi_queue_init` initialises entries 1..45 by
storing 8 at `+12`; `hi_queue_get_qid@0x3648` / `hi_queue_get_q_num@0x36b4` /
`hi_eqm_get_que_port@0x1ffc` / `hi_queue_port_cnt_get@0x4f08` all walk it with
`cmp index,#46`). Recovered fields:

| offset | field | evidence |
| --- | --- | --- |
| `+0` | valid/enabled flag (must be `1`) | `hi_queue_get_qid`: `ldr r2,[entry]; cmp r2,#1; movne error` |
| `+4` | port id (search key, compared to the API argument) | all four walkers compare `arg == [entry+4]` |
| `+8` | qid base (queue handle; `0` = unassigned) | `hi_queue_get_qid` returns it; `0` -> error `0x8fff_ffee` |
| `+12` | queue count per port; default **8**, set by `hi_queue_init`; `hi_queue_get_q_num` returns it | |
| `+16`, `+20` | second base/group fields used by the queue-shaping helper: if `[+20] > 7` the qid is `arg + [+16]`, else `[+20] + [+16]`; the helper tail-calls `hi_eqm_set_sht` | [V code shape, U meaning] |
| `+24`..`+55` | shaping/scheduling/buffer state (walkers and `hi_queue_set_*` read/write it) | [U] |

The table holds port ids for *all* LSW ports, not only the four MACs (46 entries > 12), and
`hi_queue_get_qid/get_q_num` are exported to PFE (`port_qid_map_set` uses them). This is the
strongest evidence for the model "each LSW port owns a contiguous run of QIDs, default 8".
The default 8 is also independently visible in `port_qid_map_set`'s loop count of 8
(section 7.3).

### 6.5 Virtual ports (wifi/plc/chba/voe/fttr) [V names + defaults, U purpose]

The DP probe reads five optional u32 DT properties - `wifi_vport`, `plc_vport`, `chba_vport`,
`voe_vport`, `fttr_vport` - into a 5 x 16-byte `.data` table. For each entry: `+0` = index
(0..4), `+4` = property-name pointer, `+8` = accumulated offset (default 256 when the property
is missing), `+12` = the value read from DT. The result is a partition/offset table that
`hi_lsw_get_vport@0x7b8` searches by port id (returning the entry whose index field matches).
The board DT has none of these properties [V], so every entry takes the default 256; what the
consumer does with 5 x 256 is **[U]**. [I] This is the vendor's virtual-port (Wi-Fi SSID, PLC,
FTTR, voice) queue-partition mechanism - relevant to Wi-Fi offload only ([X] for us). Our D1
must work with the property absent, i.e. with whatever default the driver computes; the clean
room choice is to *not* model virtual ports at all until they are needed.

### 6.6 How our driver should represent ports [I]

Reference shape for a clean-room switchdev driver (detailed in section 9.4):

- one **netdev per live MAC port**, owned by the GEMAC driver (patch `009`; the vendor's
  `hi_kmac` is the natural model: netdev + phylink, no rings). DTS: `gemac@...` nodes already
  exist; add a phandle to the switch node or a `switch-port` property;
- one **switch core device** (platform driver over the `lsw_dp`+`lsw_pfe` windows, `status =
  "disabled"` until proven) that owns queues/qid maps and the switchdev notifier;
- the **host queue path** (PIE, patch `015` draft) is not a netdev model - it is a hidden engine;
  its "port" for the LSW is the queue-port id recovered in 6.2 [U exact value];
- the **WAN/LAN distinction is a userspace policy**, not hardware (board `02_network`); do not
  bake it into the driver.

---

## 7. Queue / QID model

### 7.1 What a "queue" is in this ecosystem [V/I]

- The hardware has a **QID space** (queue ids) and an **EQM** (egress queue manager) table that
  maps a qid to queue state; `hi_lsw_dp` reads/writes the EQM through a command/data register
  pair (7.5) and an indirect table interface.
- Per port the driver allocates a base qid and a count (`hi_queue_get_qid`,
  `hi_queue_get_q_num`; default count 8 from the port table).
- Above that sit the PFE tables, which decide *which* qid/port a classified packet goes to
  (PFE `[act] ... pq/pq_en/car_id/car_en/statis_idx`, section 8.2).
- PIE then consumes the per-port queue handle through the `g_api_priv` context entry (PIE
  sections 2.2 / 7.1) - the LSW side writes the queue handle; PIE uses it for TX.

So the data path is "classification -> qid -> EQM -> EPS/channel -> PIE ring or MAC port".
The **handoff to PIE is indirect**: it is the queue handle stored in the PIE per-port context,
installed via `hi_net_queue_hook` (PIE export) which `hi_queue_init` calls with
`hi_dev_queue_enable` (`+0x546c`). That is the only PIE<->LSW registration point visible in the
fixtures; the per-port/vport binding timing is still **[U]** (same conclusion as
[PIE-DATAPATH.md](PIE-DATAPATH.md) section 7.2).

### 7.2 The 8-QIDs-per-port loop [V]

`hi_lsw_pfe!port_qid_map_set@0x26b8`:

~~~
qid   = hi_queue_get_qid(port);        /* from hi_lsw_dp; 0 or missing -> return */
q_num = hi_queue_get_q_num(port);      /* default 8 */
if (!qid || !q_num) return;
step  = 8 / q_num;                     /* integer division (__aeabi_idiv) */
for (i = 0; i < 8; i++)
        qid_map_set(port, 0, i, qid + i / step);   /* i/step = i*q_num/8, q_num<=8 */
~~~

For the default `q_num = 8` this is exactly `qid + i`: eight consecutive qids are mapped to the
port's eight qid-map slots. For a port with fewer queues the same base qid is replicated across
slots (`step > 1`); for `q_num > 8` the formula would divide by zero (`step = 0`) - the vendor
never hits it, and any clean-room implementation must treat it as a design error to avoid [I].

### 7.3 `qid_map_set` - the two hardware tables [V]

`qid_map_set(id, unused, index, value)` writes through `hi_lsw_write()` into **module 6**
(decoded in 5.2 as the `opp` window at `0x12400000`), with three id classes:

| `id` class | target table | index arithmetic | entry written |
| --- | --- | --- | --- |
| `0..15` | `opp + 0x42800` | word index = `index + id*8` | one u32 `(0x100 bit set) plus (value & 0xff)` |
| `40..47` | `opp + 0x43800` | byte offset = `(index + (id-40)*8) * 16` | **four** consecutive u32 words, each with the same `0x100 enable bit and low-byte value` |
| `>47` | `opp + 0x42800` | word index = `index/2 + id*4 - 64` | one u32 per slot, written twice per pair of indices |
| `16..39` | none | prints "invalid ..." (`.LC0`) and returns success | nothing |

Evidence: literal constants `0x06043800`/`0x06042800` (synthetic `module 6` addresses) and the
`bfi/orr` value construction with `0x100`, `qid_map_set@0x25dc`..`+0x98`.

Reading this as a hardware model [I]:

- **table A** (`opp+0x42800`): an array of 32-bit entries; the `0x100` bit is an "enable/valid"
  flag and the low byte is the qid. Entries for ids 0-15 are 8 words per port
  (`0x42800..0x429ff`, 512 B = 16 ports x 8 slots x 4 B), and the `>47` class continues the
  same array after word 128 (`port*4 - 64 + index/2`), i.e. **a second, 4-word-per-port layout
  for higher ports**;
- **table B** (`opp+0x43800`): 16-byte entries (4 words with the same value), 8 entries per
  port for 8 ports 40..47 -> `0x400` bytes. This is a different table shape for the
  "special/high" port class (40..47). What makes 40..47 special - the virtual ports of 6.5 are
  indexed 0..4, not 40.. - is **[U]**; [I] it is plausibly the Wi-Fi/DSL/voice port class.

The PIE-DATAPATH statement "qid_map_set writes four consecutive 32-bit words built from
`(id, value)`" is confirmed for the 40..47 class only; the other two classes write one word.
Also confirmed now: **module 6 = opp window `0x12400000`**, offset `0x42800`/`0x43800`
(~266 KB and ~276 KB into the window - within the `0x80000` size, so the bounds checks in
`hi_lsw_write` pass). [V]

**Measured table contents on the stock vendor boot (2026-11-18) [V].** The read-only inventory
read all 128 words of table A and all 256 words of table B; both passes agreed on every word.
Every non-zero entry carries the `0x100` flag and the low byte is the qid:

| table | offsets read | non-zero words | populated ports | qids (low byte) |
| --- | --- | --- | --- | --- |
| A | `opp+0x42800 .. 0x429fc` | 72 / 128 | ports 4..12, 8 slots each; ports 0-3 and 13-15 zero | `0x101..0x148` = qids 1..72, contiguous: port4 q1-8, port5 q9-16, ..., port12 q65-72 |
| B | `opp+0x43800 .. 0x43bfc` | 128 / 256 | ports 40..43, 8 slots each, each 16-byte entry = the same word four times; ports 44-47 zero | `0x149..0x168` = qids 73..104, contiguous: port40 q73-80, ..., port43 q97-104 |

**Correction to an earlier reading:** table B is **not** all-zero on this boot - ports 40..43 are
populated (the earlier "table B all zero" note predates the actual reads). Reading the two tables
together [I]: the vendor allocates **one contiguous qid space 1..104** across the 9 table-A ports
(4..12) and the 4 table-B ports (40..43), 8 qids per port, qid 0 unused; the 40..47 class (only
40..43 active here) is the special/high port class whose meaning is still [U]. [V] the contents,
the `0x100` flag, the port ranges and the slot count; [U]/[I] the port identity and why the
special class exists. This is the D1b **W1** baseline; W2 can now derive the write set on paper,
W3-W6 remain attended write work (12.3). Evidence:
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3.3.

### 7.4 EPS per-channel queue-port map (`hi_pie_map_init`) [V code shape]

`hi_lsw_dp!hi_pie_map_init@0x56e8` is called with `hwver` and binds the PIE GRO/WOC channel
ranges to LSW queue ports:

1. `gro_start = hi_pie_get_gro_ch_start()`, `gro_num = hi_pie_get_gro_ch_num()`,
   `woc_start = hi_pie_get_woc_ch_start()`, `woc_num = hi_pie_get_woc_ch_num()` (four PIE
   exports; the PIE side is documented in [PIE-DATAPATH.md](PIE-DATAPATH.md) section 7).
2. two local helpers write a per-channel table in the **EPS** window at `eps+0xB00` (word
   `ch*4`) and a companion word at `eps+0xB80` (`0x80` above it; the enable word is built from
   `0x8000ffff` and a `0xf0000`-masked field). Helper A (`@0x5544`) writes `num` channels
   starting at `ch_start`, with a packed nibble/bitfield per channel; helper B (`@0x5640`)
   writes 4 channels and also env/en bit fields. The exact bitfield packing is recovered only at
   the "nibble index / enable high bit" level [V shape, U field meaning].
3. generation gating on `hwver`: `hwver < 0x200` with WoC channels present takes one branch,
   `0x200 <= hwver < 0x300` another, `>= 0x300` a third; the board's `0x250` lands in the
   middle branch [V branches; U why].
4. the channel count returned is validated (`> 16` -> error `0x7000000c`).

On the board, PIE wiring is DT-driven by the U-Boot overlay (`pie` fragment sets
`rx_ring/tx_ring/woc_*_ring`, [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8), and
`hi_kpie` exposes the four `hi_pie_get_*_ch_*` getters; the LSW side then maps those channels
to EPS queue ports 4..7 (`port4(pie0)`..`port7(pie3)` from the counter names). [V names,
I mapping]

### 7.5 EQM indirect table access [V]

`hi_eqm_write_tab@0x1cdc` / `hi_eqm_read_tab@0x1e18` implement an indirect access engine:

- a command word built from `{tab, index, data}` is written to `eqm+0x48`; data words go to
  `eqm+0x5c` (plus a small register-address table anchored at `.LANCHOR1` for additional data
  words);
- then `hi_lsw_read(eqm+0x48)` is polled up to **1001** times (= the vendor's ~1 s timeout at
  1 ms `arm_delay_ops`); the top bits of the returned word are: **bit31 = start/busy**
  (still set -> keep polling), **bit29 = error** (return `0x8fff_ffd3`), and on timeout the
  fields printed are bits 20..26, bits 0..15 and bit 30; timeout return `0x8fff_ffd8`;
- table ids are 7-bit (`tab & 0x7f`), indices 16-bit, and up to 5 data words per operation
  (`min(num,5)`); the highest table id used in DP is 19 (`hi_queue_init` writes a 1000-cell
  entry at table 19).

**Command-word decode (2026-11-19, host-only) [V code shape; field semantics I/U].** The
disassembly of `hi_eqm_write_tab`/`hi_eqm_read_tab` is more complete than the earlier note:
the word written to `eqm+0x48` is

~~~
bits [15:0]  index (16-bit)
bits 20..26  table id (7-bit, from tab & 0x7f, placed at bit 20)
bit  [29]    error (status read-back only)
bit  [30]    1 = write, 0 = read
bit  [31]    1 = start/busy (write: set in the command; read: polled until clear)
~~~

The remaining upper bits are constant (`0xc000` for a write, `0x8000` for a read, before the
table id is OR-ed into bit 20).  Data words go to `eqm+0x5c` (word 0), then to the register
addresses in `g_eqm_tab_ba` at `.rodata+0x220`: write addresses `eqm+0x60/0x64/0x68/0x70` and
read addresses `eqm+0x50/0x54/0x58` (word 0 of a read uses `eqm+0x4c`).  That is the bit layout
a clean-room driver would need; what is **not** recovered is the *contents* of most of the
tables (entry field layouts) and the engine's use of them.  **The D1 driver still must not
write EQM tables**: the command word is decoded, but the write set for a working queue table is
not (section 7.7 row E1). [V/I]

### 7.6 Queue state visible to userspace (vendor) [V]

The DP DFX dumps expose per-queue state with these strings: `port=%u,pri=%u,qid=%u`,
`,cell=%u,priv=%u`, `,in=%llu,out=%llu,drop=%llu,tail=%u,low=%u`, `[in] qid=%u`,
`[out] qid=%u,echid=%u`, `total_share_buffer_size=%u`, `pkt_share_usecnt:%u pkt_total_usecnt:%u
cell_share_usecnt:%u cell_total_usecnt:%u`, `error vp used. qid:%u, sch_id:%u, input:%u,
is_root:%u`, and the EQM drop counters `drop_total/in_q/to_eps/rx_q2_total/q2_drop/in_q2/
over_share_drop/qtail_drop/gtail_drop/gchip_drop/cong_drop/qmc_drop/sch_drop`. This is where
the *meaning* of the queue structure could be read on a live vendor kernel (read-only procfs
dump) - it is the cheapest bench inventory item and is listed in section 12.2. [V strings]

### 7.7 D1b W2: the derived write set (paper only, 2026-11-18) [I]

Patch `021` in the target skeleton turns the W1 measurement into a checkable
model and validates it **read-only**; patch `022` implements the qid-map write
step (the table rows below marked implemented) and patch `025` (2026-11-19) adds
the R1/R3/R5 probe-configuration step, each behind explicit attended gates.  The
table is what the attended W3-W6 campaign would execute, with the evidence grade
of every row.  The driver's register-map header
(`luofu-switch-regs.h`) is the single source of truth for the offsets; this
table is the review view.  **No write in this table has been executed on
hardware.**

| # | write | value / shape | evidence | status |
| --- | --- | --- | --- | --- |
| C1 | CRG gate enable (`lsw_dp_clk`, `lsw_pfe_clk`) | set gate bits: word `0x20` bits 0/1, through the clock API | final state measured enabled [V]; 1 = on [V]; "clocks before reset" order [I] (vendor probe shape) | **implemented behind `luofu_switch.arm=1`** (W3, patch `022`); never executed |
| C2 | CRG reset deassert (`dp_logic`/`dp_srst`/`pfe_logic`/`pfe_srst`) | active-low: **set** reset word `0x30` bits 0/1/3/4 (patch `014` polarity) | released state measured [V]; polarity [V] from the measured words + patch 014; array pulse vs one deassert [U] | **implemented behind `luofu_switch.arm=1`** (W3, patch `022`); never executed |
| R1 | `dp` probe config | `dp+0x68 = 0`, `dp+0x64 = 1` then the cell-size selector (`1` = 0x200-byte cells), `dp+0x1c = (u16 queue DDR base \| BIT(16)) = 0x00018000` | code shape [V]; the u16 is the low half of the live `queue` window base, so the value is derived from the checked DT window [V]; measured final values [V]; field meaning [U] | **implemented behind `luofu_switch.arm=1 luofu_switch.egress=1`** (W4, patch `025`); never executed |
| R2 | `eps` queue/DFX | `eps+0x1008` | measured 0 [V]; writer/meaning [U] | not in write set |
| R3 | `eqm` probe init | `eqm+0xa4 = (found & ~0x3fff) \| ((fam_low14 - 0x40) & 0x3fff)`; `eqm+0x18 = (found & ~0x80003fff) \| ((eqm+0xa4 + 0x3d00) & 0x3fff) \| bit31`; `eqm+0x38 = (found & ~0x3fff) \| ((eqm+0xa4 & 0x3fff) >> 2)`; `eqm+0x40 \|= bit31` | code shape [V] (exact vendor derivation); measured final values `0x1fc0 / 0x80001cc0 / 0x7f0 / 0x80000000` [V]; register semantics [U] | **implemented behind `luofu_switch.arm=1 luofu_switch.egress=1`** (W4, patch `025`); never executed |
| R4 | `fam` queue/cell comparison | read-only conformance: `(fam+0x504 & 0x3fff)` must equal `(queue_size / cell_size) & ~7 = 0x2000`; the vendor only *lowers* the found word (clamp case [U]) | code shape [V]; measured value [V]; the write is deliberately not replayed [U] | **read-only check in the W4 stage** (patch `025`); not written |
| R5 | `ips` probe init | `ips+0x10 \|= 0x10000`, `ips+0x08 = 42` | code shape [V]; measured final values [V] | **implemented behind `luofu_switch.arm=1 luofu_switch.egress=1`** (W4, patch `025`); never executed |
| Q1 | qid table A | one u32 `0x100 \| qid` at `opp+0x42800`, word index `port*8 + slot`, ports 0..15 | code shape [V]; measured contents [V]; derived model reproduced read-only [V for this boot] | **implemented behind `luofu_switch.arm=1 luofu_switch.qidmap=1`** (W5, patch `022`); never executed |
| Q2 | qid table B | `0x100 \| qid` written to all four u32 of the 16-byte entry at `opp+0x43800`, byte offset `((port-40)*8 + slot)*16`, ports 40..47 | code shape [V]; measured contents [V] (four identical words per entry) | **implemented behind `luofu_switch.arm=1 luofu_switch.qidmap=1`** (W5, patch `022`); never executed |
| Q3 | qid allocation | one contiguous qid space from **1**, 8 qids per port, qid 0 unused; populated rows are table-A ports 4..12 (qids 1..72) and table-B ports 40..43 (qids 73..104) | measured contents [V]; allocator policy [I] | model only |
| Q4 | qid map, port > 47 class | word index `slot/2 + port*4 - 64` of table A, one word per slot written twice per pair of slots | code shape only partially recovered [V]/[U] | deliberately not modelled |
| P1 | `port_bitmap` | devicetree `<0xf01 0x1ff0>` -> u64 `0xf01_0000_1ff0`; the allocator walk visits ports {4..12, 32, 40..43} | value [V] in the base DT and on the live node; decode [V] (6.3, reproduces the measured qid map) | baseline check only, never used to enable anything |
| D1 | queue/flow DDR | `queue = 0x80000000/4 MiB`, `flow = 0x80400000/1 MiB` | base/size measured live after the loader fixup [V]; layout inside [U]; how the engine is told the base [U] | validated from DT, never ioremapped or written |
| E1 | EQM indirect table (queue table 19 / 1000-cell entry, tables 16/32/34/35/37/38), IFC/L2/VLAN tables | command word (decoded 2026-11-19, 7.5); tables **19/32/35/17/16/34** entry shapes and per-entry values fully decoded 2026-11-19 (7.9) and implemented behind a gate; table 38 (vendor table-manager id) and the ports 40..43 VP class remain [U], and so do the IFC/L2/VLAN tables | command word [V]; decoded entry shapes/values [V]; field semantics [U]; table 38 / VP class / IFC-L2-VLAN [U] | **tables 19/32/35/17/16/34 implemented behind luofu_switch.qminit=1** (W7, patch 032); table 38 + VP class excluded by design; IFC/L2/VLAN untouched |

**What patch `021` verified without writing (W2 exit, read-only).**  The
derived allocation model (Q1-Q3) reproduces all 384 measured qid-map words
(128 table-A words + 256 table-B words) on a block that passes the powered-state
gate and matches the 30-word W1 register baseline.  That is the evidence the
write set rests on: the *write shape* (Q1/Q2) is decoded from the vendor code,
the *values* are measured, and the model is checked against the silicon before
any write.  What it still does not prove is that writing them has the intended
effect (W5), the correct values for our own (non-vendor-initialised) block
state (W3/W4 readback), or the meaning of the port ids the rows belong to
(13.2 item 2).

**What patch `022` adds (W3/W5 staged, implemented, never executed).**
`luofu-switch.c` now has two explicit opt-in gates on top of the read-only
default: `luofu_switch.arm=1` performs C1+C2 (enable the two gate clocks, release
the four reset lines), re-checks the powered state through the CRG syscon and
the reset API, then snapshots the 30 measured baseline words and the two qid
tables *read-only* and refuses (no switch register is written in this step);
`luofu_switch.arm=1 luofu_switch.qidmap=1` additionally programs Q1+Q2 for the 13
populated rows, reads every programmed word back and restores the found values
on the first mismatch.  The pre-write conformance gate refuses before the first
write if any *unpopulated* qid-map word is non-zero, because that means the
block is not in the measured state the model was derived against.  The default
boot keeps the patch-`021` read-only behaviour: no parameter, no write.  The
attended sequence, the exact expected console lines and the rollback plan are
in [SKELETON-SMOKE.md](SKELETON-SMOKE.md) ("connectivity session" section).
What was still open after patch `022`: the port ids the rows belong to are the
measured MAC port numbers (declared in the devicetree as `hisilicon,port` and
validated against the populated rows), but how a PIE TX descriptor selects an
egress queue/port remains [U], and the dp/eqm/ips probe initialization
(R1/R3/R5) was not in any write set.

**What patch `025` adds (W4 staged, implemented, never executed; 2026-11-19).**
`luofu-switch.c` gains a third opt-in gate, `luofu_switch.egress=1`, placed after
the W3 power step and before the qid-map write: it snapshots the R1/R3/R5 target
words, runs the pre-write conformance gate (EQM indirect interface idle, every
target word stable across two reads, `fam+0x504` low14 equal to the computed
cell count `(queue_size / cell_size) & ~7` - the measured state the R3 formulas
are derived against), programs the nine words of R1/R3/R5 in the vendor order,
reads every one of them back against the value computed from the pre-write
snapshot, and restores the found state on the first mismatch (also in
`remove()`).  R4 is read-only.  The step refuses without `luofu_switch.arm=1`;
a run with `egress=1` but without `qidmap=1`, and a run with `qidmap=1` but
without `egress=1`, both end in a refused probe with the found block state
restored and the block powered back down.  Only `arm=1 egress=1 qidmap=1` with
both write sets verified publishes `luofu_switch_ready()`.  The R1/R3/R5
sequences and derivations are in 7.7 rows R1/R3/R5, the words in
`luofu-switch-regs.h`, and the attended sequence/expected console lines are in
[SKELETON-SMOKE.md](SKELETON-SMOKE.md) ("LSW egress session").  What this does
**not** prove: that the written configuration enables a data path (no EQM queue
table is programmed - row E1 - and no frame has been transmitted), and that the
per-frame egress selection works (7.8).


### 7.8 TX frame egress: what the vendor writes init-time, per frame, and what is still [U] (2026-11-19)

This is the decode the D1 TX path needed: which writes a TX frame depends on, and which of them
are in the reviewed write set.  It separates three layers.

**Per frame: the LSW is written zero times.** No vendor path issues a hi_lsw_write() per
transmitted frame (the only hi_lsw_* write call sites in hi_lsw_dp are the probe/queue-init
paths and the qid-map hook; hi_kpie's TX path only writes PIE ring doorbells and DMA
descriptors).  A frame reaches the LSW as a 32-byte hardware TX descriptor produced by
hi_pie_tx_set_desc (v250, PIE-DATAPATH.md section 5.2).  The source fields come from the
vendor's 32-byte TX request built in hi_pie_drv_xmit@0x42ac: descriptor bytes 0-3 (the
desc8 {port, vport, ...} from the MAC netdev) land at request +8..+11; the per-port
"queue handle" from the PIE per-port context lands in request +14 bits 4..; the rest are skb
type/length/address fields.  **The queue handle is zero in this build**: hi_net_dev_probe
initialises the context word +0 to the previous value (0), hi_net_dev_open only sets the up
flag at +4, and no vendor module writes it ([V] all call sites).  So the only egress identity
a vendor MAC TX descriptor carries is the port/vport word pair; whether the engine uses that
word directly as the egress port or selects through a per-queue EQM entry is **still [U]** -
software cannot answer it, a bench trace or GPL source can.

**Init-time: the vendor does write the queue/egress tables, but not in our write set.** While a
MAC netdev probes, hi_dev_queue_enable (the PIE netdev hook installed by hi_lsw_dp) allocates
the port's contiguous qid run and calls the registered qid_map_set hook for all 8 slots; this is
the port->qid direction that the measured opp+0x42800/0x43800 tables contain (7.3).  The reverse
qid->egress-path state is programmed earlier, in hi_queue_init@0x507c, through the EQM indirect
interface: table 19 entry 0 (a 2-word cell-count record, {0xffffffff, 0xffffc3e8}), table 16
(16 qids per row; each byte has a 5-bit selector plus a valid bit), table 32 (per queue-port byte
0 = port id plus 3-bit per-slot priorities), table 34 (per qid: 6-bit queue-port field, 4-bit
slot field, valid bit), tables 35/37/38 (per-qid scheduling/shaping state), and the three direct
words eqm+0x18/+0x38/+0x40 (row R3).  Entry shapes are [V] code shape (the writers were
disassembled); the *meaning* of every field and the contents needed for our own block are [U].
Row E1 therefore stays excluded: programming that state is the next RE/bench item, not part of
patch 025.

**The reviewed TX-adjacent write set as of patch 032:** W3 (CRG gates + resets, patch 022), W4
(R1/R3/R5 probe configuration, patch 025), W5 (qid maps, patch 022), W6 (EPS channel map, patch
031) and, since 2026-11-19, W7 (the EQM queue-manager init tables 19/32/35/17/16/34, patch 032 -
section 7.9).  That is the set whose *values* are pinned by the 2026-11-18 measurement (W3-W6) or
derived from the vendor boot code plus build-time tables (W7), and whose readback is computable;
it is still not proven to be sufficient for forwarding, because table 38 (per-qid scheduler
state) and the virtual-port class are excluded, and because no frame has ever been transmitted
through the block.  The attended W7 run is the first one that can answer the per-frame egress
question: with the queue/deque configuration in place, a single ping either reaches the wire or
the descriptor port tag is provably not enough.

### 7.9 W7: the EQM queue-manager init write set (2026-11-19, patch 032)

This is the decode that closes the "queue-manager/EQM init" half of row E1: what the vendor
writes into the EQM indirect tables at boot, with which values, and how the per-port MAC/queue
group maps onto the EPS dequeues.  Source: the pinned vendor fixture
rootfs-2.4.15/lib/hisilicon/ko/hi_lsw_dp.ko (sha256 in the lane SUMMARY), functions
hi_queue_init@0x507c, hi_dp_probe@0x8b0, hi_eqm_write_tab@0x1cdc, hi_eqm_read_tab@0x1e18, the
helpers at 0x20f8, 0x2184, 0x22ec, 0x223c, 0x24d0, 0x2b40 and 0x31d4, and the module's build-time
queue-group table in .data+0xd0 (46 entries x 56 B).  Host-only read-only disassembly; no vendor
binary is in this tree and no register was touched.

**The indirect engine (both directions) [V code shape, corroborated by a measurement].**  The
command word written to eqm+0x48 is index[15:0] | table[26:20] | bit30 write | bit31 start;
eqm+0x48 is polled until bit31 clears; bit29 is the operation-error status.  Write data goes to
eqm+0x5c/+0x60/+0x64/+0x68/+0x70, read data comes from eqm+0x4c/+0x50/+0x54/+0x58/+0x6c, and one
operation moves **at most five words** (the vendor clamps its 8-word buffer to 5).  The 2026-11-18
measured idle word eqm+0x48 = 0x41100012 decodes as a completed *write* of table 17, index 18, and
eqm+0x5c = 0xbb8 = 3000 is exactly the rate of that profile write - the vendor's last queue-init
operation, i.e. independent measured corroboration of the command-word decode and of the table-17
profile shape.  (Section 7.5's statement that bit29 is the error bit is right; patch 025's driver
header had recorded bit5 and patch 032 corrects it.)

**The write set (value derivation per row).**

| # | table, index | vendor write shape | value source |
| --- | --- | --- | --- |
| EQM-1 | 19, 0 | 0xff-filled 8-word buffer with word 1 low14 = 1000, then 5 words written: {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff} | vendor constant [V] |
| EQM-2 | 32, P (P = port 4..12) | read 5 words, write byte0 = P*8, then 3-bit slot priorities in word 1 bits 0..23 = 7..0 for slots 0..7, other bits preserved | vendor formula [V]; P*8 is the group's build-time table field (+36) [V] |
| EQM-3 | 35, qid | 5 words: {0x00070000, 0, 0, 0, 0} (byte 2 = 7) | vendor constant [V] |
| EQM-4 | 17, profile | 5 words: (rate & 0x1fff), 0x10000000, (0xffff0000 \| rate), ((sch & 0x1ff) << 20) \| cells, 0x07f03fff; cells = rate if (eqm+0x38 & 0x3fff) < rate else rate / 2 (divider = the vendor's build-time 2) | vendor formula [V] |
| EQM-5 | 16, qid>>4 | read 5 words, byte (qid & 15) = 0x80 \| (profile & 0x1f), bits 6:5 preserved (16 qids per row) | vendor formula [V] |
| EQM-6 | 34, qid | read 5 words, word 0 = queue-port in bits 5:0 (= the LSW hardware port P), slot in bits 17:14, bit 13 = 1, bit 18 = 1; bits 6..12 and 19..31 preserved | vendor formula [V] |

The per-group constants are the vendor's build-time queue-group table rows for ports 4..12:
{profile base, profile selector, scheduler id, rate} = {0x10, 1, 0x20, 3000} for port 4,
{0x10, 2, 0x20, 3000} for ports 5..7 and {0x00, 8, 0x20, 2500} for ports 8..12;
profile(port, slot) = base + (selector <= 7 ? selector : slot) and
qid(port, slot) = 1 + port_index*8 + slot, with port_index the position in the populated-port
allocation model (7.2/7.3, the same model patch 022 programs and validates).  Per group that is
one table-32 record, and per qid three records (35/16/34) plus one shared profile record (17):
nine wired groups -> **169 entries / 845 words** for qids 1..72 and profiles {0..7, 17, 18}.

**What this says about the MAC TX egress selection.**  The per-frame path stays [U] but is now
bounded: a PIE TX descriptor carries the egress port tag (+8 bits 3:0) and its 1 << port bitmap,
and no queue id or queue handle (the vendor's per-port queue handle in the PIE context is zero in
this build, 7.8).  The vendor programs tables 32 and 34 before any frame flows, i.e. the engine has
a queue-port/deque map for every populated port; whether a tagged descriptor then selects the
queue directly or needs one of the excluded records (table 38, the VP class) is the silicon
question the W7 attended run can now answer.

**Deliberately excluded from W7 (documented, not forgotten):**
- **table 38** (per-qid scheduler/buffer state): the vendor writes a table-manager id obtained
  from its own software table manager (hi_tab_mgr_alloc_hash) into byte 0; no clean driver can
  reproduce that id space, and the validity semantics are [U];
- **the ports 40..43 virtual-port class** (table 32 indices 35..38 and table 34 indices 227..230
  in the vendor code): the wired GEMAC egress path does not use the Wi-Fi/VP class on this board
  and its index space is [U];
- the queue/flow DDR contents (never mapped or written) and the IFC/L2/VLAN tables (D2 scope).

**Gate and safety model.**  Patch 032 adds the fifth attended gate luofu_switch.qminit=1 (requires
arm=1 egress=1 qidmap=1 eps=1).  It refuses unless the EQM indirect interface is idle, snapshots
all 169 target entries (each read twice and required stable), computes the expected content from
the snapshot plus the formulas above, writes every entry through the decoded engine, verifies the
entries whose readable fields the bench has proven, and restores those entries on the first
mismatch (also in remove()).  luofu_switch_ready() requires W3+W4+W5+W6+W7, so the GEMAC front end
still cannot register a netdev on a partial data path (7.8, SKELETON-SMOKE "LSW QM/EQM session").

**Bench results and the verification fix (2026-11-19, three hardware rounds) [V].**  The decisive
finding across rounds is that **the engine accepts every write but implements only part of each
record as readable**, so a readback contract may only claim what the bench has proven.

- *Round 1 (image 14791958, all five gates).*  W7 snapshot passed (169 entries / 845 words read,
  stable, indirect interface idle); the first write was acknowledged, but strict readback failed
  on the first entry: table 19 entry 0 word 0 read 0x3fff, expected 0xffffffff.  The stage
  restored and refused fail-closed (GEMAC did not register).  Decode showed table 19 is
  **write-only in the vendor code**: hi_queue_init@0x5164..0x5198 is its only user and the EQM
  table accessors are module-local to hi_lsw_dp, so no vendor reader exists.
- *Round 2 (image 5d2c5476, table-19 acknowledge fix).*  The table-19 write was acknowledged and
  its read view went from 0x0 0x0 0x0 0x0 0x0 (fresh block snapshot) to
  **0x3fff 0x3e8 0x3fff 0x1ffffff 0x0** - per-word field-width masks of the written
  {0xffffffff, 0xffffc3e8, 0xffffffff, 0xffffffff, 0xffffffff}: the write took effect, the read
  view does not expose the record.  Verification then passed **every table-32 and table-35 entry**
  and failed on table 17 entry 17: read 0x0 0x10000000 0xffff0bb8 0x2000bb8 0x0 against written
  0xbb8 0x10000000 0xffff0bb8 0x2000bb8 0x07f03fff.  Words 1..3 came back **exactly at their own
  indices** (so the write/read data-register mapping is not shifted or off by one), while words 0
  and 4 read 0 although written: the engine does not implement those two words of a table-17
  record as readable.  The stage restored and refused again; GEMAC stayed unregistered.
- *Fix (patch 032 revision 3).*  Verification is now tiered and only bench-proven fields fail
  closed: **STRICT** = table 32 slot priorities, table 35 word 0 bytes 1-2 and table 17 words 1..3
  (all read back exactly on hardware); **DIAG** = tables 16 and 34 (not reached on the bench yet:
  writes command-acknowledged, read views logged against the vendor-read mask, to be promoted to
  STRICT once a run shows which words are readable); **ACK** = write-only table 19 (command
  acknowledgement only).  Differences outside the proven fields are diagnostics.
- *Restore.*  On a STRICT mismatch the **bench-proven** entries are restored from the pre-write
  snapshot.  Table 19 and tables 16/34 are deliberately left as written: their read views cannot
  be trusted to restore a record, so writing them back could corrupt it.  The failure path powers
  the switch block down and refuses readiness, so nothing stays armed, and every W7 attempt
  rewrites the full vendor init record; the restore log states the restored/skipped counts.
- *Diagnosability.*  The mismatch log prints the bench-proven field mask, the pre-write snapshot
  value and the whole 5-word read view, so the next attended run is diagnosable from the console
  alone; the success log separates bench-proven verified entries, diagnostic entries and the
  write-only acknowledgement.

A W7 boot still means "the recovered write set was accepted/verified on the bench-proven fields",
not "frames flow": the next attended measurement is whether a tagged PIE TX descriptor egresses
once the queue manager is configured.


### 7.10 W8: the PFE forwarding-engine base init (2026-11-20, patch 036)

Round 3 on hardware (image 75868fe0) finally **armed the data path**: W3-W7 verified, W7 programmed
169 entries, the driver published readiness, GEMAC registered all four netdevs, eth2 linked at
1 Gbps/Full and br-lan put eth2 into forwarding.  And yet **no frame crossed the switch**: the PIE
RX poll/interrupt trace never fired (zero RX poll lines), the host sniffer saw no box-originated
frames, and the PIE TX descriptors were consumed (doorbells accepted, TX read pointer advancing)
without anything appearing on the wire.  So the DP/EQM/EPS/opp init is necessary but not
sufficient: the vendor runs a **second engine's probe after the DP probe**, the PFE (packet
forwarding engine) module hi_lsw_pfe, and it programs that engine's globals before its CFE/rootif
layer is usable.

**Decoded PFE bring-up [V code shape; field semantics U].**  `hi_pfe_probe@0x23a4` registers the ten
PFE windows (id map 5.2: 0=ifc 1=l2 3=napt 5=mcc 6=opp 7=pa 8=pe 9=pfe_glb 0xa=prbs 0xb=pub),
enables its clock, pulses its resets, then calls `hi_pfe_init@0x2278` -> `hi_pfe_pub_init@0x3400`
-> `hi_pfe_opp_init@0x271c` -> `hi_pfe_mcc_init` -> `hi_pfe_napt_init` -> `hi_pfe_ifc_init@0x794`.
The whole direct-register part of that base init is four words:

| write | value | source |
| --- | --- | --- |
| pe+0x400 | 2 | hi_pfe_init@0x229c |
| pfe_glb+0x2c | read, write back `value & 0xffff` (the upper 16 bits are the global-attr field) | hi_pfe_init@0x22a8..0x22bc; write_glb_attr@0x2210 sets bits 31:16 |
| pfe_glb+0x204 | 1 | hi_pfe_init@0x22cc |
| opp+0x110 | 0 | hi_pfe_opp_init@0x2750, after the port qid-map walk |

The rest of `hi_pfe_init`/`hi_pfe_pub_init` creates software table managers; `hi_pfe_napt_init` and
`hi_pfe_mcc_init` program the NAPT/MCC tables (not needed for plain L2 traffic); and
`hi_pfe_ifc_init` writes the **IFC default-classification table** through the shared
`hi_lsw_write_tab128` helper: 16-byte entries at `ifc+0x50000 + index*16` (index 1, 2, 3, ...),
built from 12-byte descriptors plus a table-manager allocation.  That table is decoded at the
call-shape level but its entry fields are [U]; it is deliberately **not** in the W8 write set yet -
it needs its own attended verification like every other table.

**Patch 036 (W8) implementation.**  A sixth attended gate, `luofu_switch.pfe=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1`), programs exactly those four words: each target is
snapshotted, required stable across two reads, the expected value is computed (two vendor
constants, one vendor read-modify-write keeping bits 15..0, one zero), every word is read back and
the snapshot is restored on a mismatch.  `luofu_switch_ready()` now requires W3+W4+W5+W6+W7+W8, so
the GEMAC front end only registers with the PFE base init in place.  Patch 036 also fixes the
revision-3 **tier-count bug**: the success line printed "0 verified ... 168 acknowledged" because
every masked entry incremented the diagnostic counter; revision 4 counts STRICT entries that pass
their bench-proven mask as verified (91: 9 table-32 + 72 table-35 + 10 table-17) and only tables
16/34 as diagnostic (77).

**Still open after W8 (the next layers, in the order the vendor stack needs them).**

- **IFC default-classification table** (`ifc+0x50000`, 16-byte entries from `hi_pfe_ifc_init`):
  the likely reason a frame has no forwarding decision at all - decoded call shape, [U] fields;
- **L2 / VLAN tables** (`l2`, id 1) and the CFE/rootif contract (`hi_kcfe_pub`, `hi_kcara`,
  `hi_swa`): how the vendor's software bridge learns and what it programs per flow;
- **NAPT/MCC init** (`hi_pfe_napt_init`/`hi_pfe_mcc_init` writes) - not needed for plain L2,
  so out of scope until there is traffic;
- **table 38 per-qid scheduler state and the ports 40..43 VP class** (7.9) - still excluded;
- **per-port MAC configuration**: our GEMAC driver only writes the measured 1 Gbps link words;
  the vendor MAC driver's full `hi_mac_hw_config` write set (MTU, pause, RX/TX enable bits) has
  not been compared against it yet - a second candidate for the dead wire in either direction;
- **PIE<->LSW per-port queue handle**: the PIE per-port context queue handle is zero in the vendor
  build (7.8), and its hardware effect is still [U].

The next attended run is the first one with W8 in place: it should tell us whether the PFE base
init alone reopens the wire, or which of the layers above is the actual gate (the PIE RX poll
trace and the switch/EPS drop counters are the readout).

### 7.11 W9: the MAC maximum-frame register (2026-11-20, patch 038)

Round 4 on hardware (image 6cb3d908) added the W8 PFE base init to the armed chain: W8
programmed and verified (`pe+0x400=0x2`, `pfe_glb+0x2c=0x34`, `pfe_glb+0x204=0x1`,
`opp+0x110=0x0`), the driver published readiness with six gates, GEMAC registered, eth2 linked at
1 Gbps/Full and br-lan put eth2 into forwarding - and **still no frame moved**: zero PIE RX poll
lines, no box-originated frames on the wire, PIE TX descriptors consumed.  W8 is necessary but not
sufficient.

The next decoded candidate is the **MAC device configuration** itself.  The vendor MAC driver
(hi_kmac.ko) has a per-port hardware config path that our GEMAC front end never reproduced:

- `hi_mac_hw_mtu@0x38c` reads register **0x100**, replaces its **low 14 bits with (MTU + 4)** and
  writes the word back (the helpers are plain base+offset MMIO:
  `hi_reg_hw_read@0x41b8` / `hi_reg_hw_write@0x41c4`).  The vendor's `hi_mac_change_mtu@0x428c`
  calls it on every MTU change.
- The MAC counts `rx_overmax_pkt` / `tx_overmax_pkt` (the hi_kmac statistics strings), i.e. frames
  longer than this limit are dropped **in both directions**; a zero/reset max-frame value would
  explain exactly the observed symptom - link up, PIE RX silent, TX descriptors consumed but no
  wire frame.
- The rest of `hi_mac_hw_config@0x258` (register 0x14 speed config, 0x18, 0x1c, and a read-modify
  of 0x40 preserving bits 8..15 with bit 24 set) overlaps what our driver already writes from
  measured link words (0x40 port mode, 0x1b4 mode-change latch, 0x0 MAC enable), so W9
  deliberately touches **only** the maximum-frame register.

**Patch 038 (W9) implementation.**  New gate `luofu_gmac.hwinit=1`: once the accepted attended
path is reached (PIE armed, LSW data path complete), the GEMAC driver refuses to register a netdev
unless W9 is armed and verified.  W9 snapshots register 0x100, requires it stable across two reads,
computes `(pre & ~GENMASK(13,0)) | ((mtu + 4) & GENMASK(13,0))`, writes it, reads it back, restores
the snapshot on a mismatch, and re-runs on `ndo_change_mtu`.  The write shape is [V] from the
vendor; the field semantics beyond "low 14 bits = frame length limit" are [U].

**Correction and crash finding from the round-4b bench runs (patch 040).**  The seven-gate image
a6b57361 programmed W9 on all four GEMACs (read back `0x334005e0`, i.e. 1504) against the found
default `0x334005ee` (1518): the vendor `hi_mac_hw_mtu` adds 4 to a frame-length argument
(MTU + 14), so the correct register value is **MTU + 18** = 1518 and patch 038's MTU + 4 would
have marked every full-size frame over-max in both directions.  The stable 1-CPU image 45d248ff
(001-038, seven gates, no early probe) then booted cleanly and reset/fell back to stock **during
the traffic test**, while the six-gate image 6cb3d908 (no W9) survived that same test, so the
reset correlates with W9 plus traffic, not with boot.  Patch 040 therefore turns W9 into a
**read-only diagnostic** (logs the vendor maximum-frame value on every GEMAC, no write) and moves
the write behind a separate `luofu_gmac.hwinitwrite=1` gate that touches **only** the port named by
`luofu_gmac.hwinit_port` (default 11 = the bench eth2), with the corrected MTU + 18 formula that
never lowers a found limit and with pre/post values logged around the write.  It also adds
`luofu_gmac.trace=1` instrumentation that logs the MAC control/max-frame/port-mode words before
and after every GMAC xmit (first 32 frames), so a watchdog reset can be attributed to the enqueue
or to the engine processing the descriptor.

**Vendor ordering of the 0x100 write [I].**  In hi_kmac.ko the only writer of MAC register 0x100 is
`hi_mac_hw_mtu`, referenced solely from the GEMAC ops table (`.data+0x4`; `hi_gemac_probe` passes
`.data` into `hi_mac_probe`), and the ops table is invoked from the MAC config/link callbacks
(`hi_mac_config`/`hi_mac_link_up` path) - i.e. the vendor programs the maximum-frame value as part
of link configuration, after the MAC is probed and reset, not from an earlier bare probe step.  It
is therefore not a pre-link enable of anything: it only sets a length limit, and the MAC TX enable
stays in the control word (0x0) that our link-up path already writes.

**Round-4c diag run: the MAC control word and the TX hang (patch 041).**  The diag-safe image
(6cf5de37, W9 read-only, trace=1, oops=panic) booted, armed all gates and logged
`max-frame 0x100 = 0x334005ee (limit 1518), ctrl 0x0 = 0x0, port_mode 0x40 = 0xc0408` at probe and
`ctrl=0x0 maxframe=0x334005ee port_mode=0x10c0008` around the one TX, then the box hung and the
hardware watchdog reset it after one queued descriptor/doorbell (no oops/panic - a bus hang, not a
fault).  Two decoded facts follow from that:

- **Reg 0x0 is not the MAC enable/control word.**  The vendor config path
  (`hi_mac_hw_config`, called from the link-up ops chain) writes only 0x18=1 (reset assert),
  0x14 = speed/duplex code (bits 1..0 = 2/1/0 for 1000/100/other, bit 2 = full duplex), 0x18=0
  (release), 0x1c=1, and a read-modify of 0x40 that clears bits 8..15 + bit 24 and sets bit 24 -
  there is **no write to 0x0**, and the bench reads 0x0 back as 0 even after our write.  The port-mode
  RMW from the found baseline 0xc0408 gives exactly 0x10c0008, the value our driver writes.  So the
  attested MAC enable/config is the 0x14/0x18/0x1c/0x40 sequence plus the port mode, not 0x0.
- **The hang happens when the engine processes the descriptor after the doorbell.**  Exactly one
  frame was handed down; gmac-trace pre and post both printed, the pie-trace descriptor/doorbell
  printed, then silence until the watchdog reset - the transport (descriptor build, DMA map, doorbell
  write) completed, so the next step is to bisect the doorbell itself.

Patch 041 therefore adds three attended diagnostics: `luofu_pie.nodoorbell=1` (build and publish
descriptors, never ring the doorbell - proves whether the hang is before or after engine processing),
`luofu_gmac.macconf=1` (runs the recovered vendor config sequence at link-up with every pre/post
register value logged, instead of the unattested 0x0 write), and `luofu_gmac.txhalt=1` (stops the TX
queue after the first xmit so at most one descriptor can reach the engine).

**Round-4d bisection: no-engine-interaction still hangs (patch 042).**  Image 2d904721 (001-041,
seven gates, W9 read-only, gmac.trace=0, luofu_pie.nodoorbell=1, oops=panic) booted, showed four
`TX doorbell SUPPRESSED` lines (no doorbell writes, no xmit MMIO reads) and still hung under the
host traffic test: last lines are the second eth2 link-up / br-lan forwarding at 40.4 s, two more
xmit attempts and eth3's PHY init at 40.95 s, then silence - no Oops, no fallback (physical recovery
needed).  The 001-037 six-gate image survived the same test before, so the remaining suspects are
the only W9 runtime effects present in a no-engine run (probe-time diag reads of 0x100/0x0/0x40 on
all four GEMACs and the extra ndo_change_mtu op) or the SMP patch delta (039/040-arm), which the
001-037 image predates.

Patch 042 makes W9 fully removable at runtime: new master switch `luofu_gmac.w9` (default OFF).
With `w9=0` the GEMAC probe does **no** W9 register read or write, applies no registration gate,
and installs the pre-038 netdev ops table (no change_mtu); `w9=1` re-enables hwinit/hwinitwrite.
This allows a seven-gate image with **zero W9 code executed**.  Proposed isolation matrix:

1. **Idle boot of the current full series, no traffic test** - if it still hangs around the same
   phase, the trigger is not traffic at all; if it survives, traffic/engine is involved.
2. **001-042 full, cmdline WITHOUT luofu_gmac.w9** (W9 code inert) + nodoorbell=1, trace=0: survives
   => W9 probe reads/MTU op implicated; hangs => delta is elsewhere (first test: SMP 039/040-arm).
3. **001-037 only** (control): re-run the exact traffic test on the known-stable subset.
4. **001-037 + SMP 039 + SMP 040-arm** (no 038/040-net/041): isolates the SMP delta directly.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4k: all decodable init matches stock; the W14 page scan (patch 049).**  W13_RXCFG programmed
pie+0x3404 = 0x1b04 and verified it; the gmac dump works; the full chain W3-W13 is armed with a
stable heartbeat - and RX still never fires.  Every comparable programmable register now matches
the stock inventory, so the remaining class is the runtime flow/port-forwarding state that the
vendor CFE modules install on netdev/port events.  Decode note: hi_kcfe_pub itself imports only
netdev/netdevice-notifier APIs (no hardware access), and the table-manager consumers are
hi_kcara/hi_swa - none of which can load on this 6.18 kernel, so our image never writes those
tables.  The W11 dump sampled only a few offsets per window; the CFE tables can live anywhere in
the 15 MMIO windows.

Patch 049 (W14) adds a page-granularity read-only scan: luofu_switch.scan=1 reads one word per
4 KiB page of every LSW MMIO window (15 windows x 128 pages, non-zero pages printed) and
luofu_pie.scan=1 does the same for the 16 PIE pages.  Nothing is written.  The same page addresses
are read on a stock boot with devmem; the diff of non-zero pages lists exactly which table pages
exist on stock and not on ours - the location of the CFE/rootif tables to replay in the next gated
stage.  Stock scan command (one line per window base):

  for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
           0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
           0x12480000; do for o in $(seq 0 4096 520192); do v=$(devmem $(printf 0x%x $((w+o)))); \
           [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"; done; done

and for the PIE window: base 0x10a70000, offsets 0x0..0xf000 in 0x1000 steps.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4k: all decodable init matches stock; the W14 page scan (patch 049).**  W13_RXCFG programmed
pie+0x3404 = 0x1b04 and verified it; the gmac dump works; the full chain W3-W13 is armed with a
stable heartbeat - and RX still never fires.  Every comparable programmable register now matches
the stock inventory, so the remaining class is the runtime flow/port-forwarding state that the
vendor CFE modules install on netdev/port events.  Decode note: hi_kcfe_pub itself imports only
netdev/netdevice-notifier APIs (no hardware access), and the table-manager consumers are
hi_kcara/hi_swa - none of which can load on this 6.18 kernel, so our image never writes those
tables.  The W11 dump sampled only a few offsets per window; the CFE tables can live anywhere in
the 15 MMIO windows.

Patch 049 (W14) adds a page-granularity read-only scan: luofu_switch.scan=1 reads one word per
4 KiB page of every LSW MMIO window (15 windows x 128 pages, non-zero pages printed) and
luofu_pie.scan=1 does the same for the 16 PIE pages.  Nothing is written.  The same page addresses
are read on a stock boot with devmem; the diff of non-zero pages lists exactly which table pages
exist on stock and not on ours - the location of the CFE/rootif tables to replay in the next gated
stage.  Stock scan command (one line per window base):

  for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
           0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
           0x12480000; do for o in $(seq 0 4096 520192); do v=$(devmem $(printf 0x%x $((w+o)))); \
           [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"; done; done

and for the PIE window: base 0x10a70000, offsets 0x0..0xf000 in 0x1000 steps.

**Round-4l: the W14 page map is runtime counter state, not missing configuration (patch 050).**
The W14 page scan on our image was diffed against a stock devmem page map.  Fifteen pages are
non-zero on stock and zero/different on ours, but the classification says they are **not**
persistent configuration:

- every differing offset (+0x1000/+0x2000/+0x6000/+0x40000/+0x41000/+0x42000/+0x46000 in the pe,
  eqm, eps, pa, ifc, l2, napt, mcc, opp and fam windows) appears in the vendor fixtures **only as a
  read inside hi_dp_dfx_dump_cnt / hi_pfe_dfx_dump_cnt** - the DFX counter-dump functions - and
  never as an init write;
- the same value 0x1b6 shows up in five engines (pe+0x1000, pe+0x41000, eqm+0x2000, eqm+0x42000,
  eps+0x6000), the two fam pages read 0x2f9/0x2fa (one apart, sampled microseconds apart), and our
  own values are small running counters (0x11) - all signatures of runtime counters/age registers
  updated by the vendor table-manager kthread/CFE aging path, which cannot run on this kernel;
- the persistent-looking pages already **match** on both sides (pe+0x2000/0x42000 0xffff0000,
  eqm+0x0/0x40000, eps+0x0, pfe_glb 0x7af, mcc+0x0 0x201410, pub+0x57000 0x10001).

Replaying counters would be wrong, so patch 050 (W15) delivers the content-level comparison instead
of a write: luofu_switch.pagedump=1 prints the first 16 words of every differing page (plus the
matched control pages), reads each page twice 10 ms apart and labels it STABLE (persistent
candidate) or lists the words that CHANGED (runtime counter/status, do not replay).  A stock
devmem dump of the same pages confirms the labels; only pages that are stable on both sides become
the following gated write set.  This keeps the fail-closed, evidence-graded discipline.

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4k: all decodable init matches stock; the W14 page scan (patch 049).**  W13_RXCFG programmed
pie+0x3404 = 0x1b04 and verified it; the gmac dump works; the full chain W3-W13 is armed with a
stable heartbeat - and RX still never fires.  Every comparable programmable register now matches
the stock inventory, so the remaining class is the runtime flow/port-forwarding state that the
vendor CFE modules install on netdev/port events.  Decode note: hi_kcfe_pub itself imports only
netdev/netdevice-notifier APIs (no hardware access), and the table-manager consumers are
hi_kcara/hi_swa - none of which can load on this 6.18 kernel, so our image never writes those
tables.  The W11 dump sampled only a few offsets per window; the CFE tables can live anywhere in
the 15 MMIO windows.

Patch 049 (W14) adds a page-granularity read-only scan: luofu_switch.scan=1 reads one word per
4 KiB page of every LSW MMIO window (15 windows x 128 pages, non-zero pages printed) and
luofu_pie.scan=1 does the same for the 16 PIE pages.  Nothing is written.  The same page addresses
are read on a stock boot with devmem; the diff of non-zero pages lists exactly which table pages
exist on stock and not on ours - the location of the CFE/rootif tables to replay in the next gated
stage.  Stock scan command (one line per window base):

  for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
           0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
           0x12480000; do for o in $(seq 0 4096 520192); do v=$(devmem $(printf 0x%x $((w+o)))); \
           [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"; done; done

and for the PIE window: base 0x10a70000, offsets 0x0..0xf000 in 0x1000 steps.

**Round-4l: the W14 page map is runtime counter state, not missing configuration (patch 050).**
The W14 page scan on our image was diffed against a stock devmem page map.  Fifteen pages are
non-zero on stock and zero/different on ours, but the classification says they are **not**
persistent configuration:

- every differing offset (+0x1000/+0x2000/+0x6000/+0x40000/+0x41000/+0x42000/+0x46000 in the pe,
  eqm, eps, pa, ifc, l2, napt, mcc, opp and fam windows) appears in the vendor fixtures **only as a
  read inside hi_dp_dfx_dump_cnt / hi_pfe_dfx_dump_cnt** - the DFX counter-dump functions - and
  never as an init write;
- the same value 0x1b6 shows up in five engines (pe+0x1000, pe+0x41000, eqm+0x2000, eqm+0x42000,
  eps+0x6000), the two fam pages read 0x2f9/0x2fa (one apart, sampled microseconds apart), and our
  own values are small running counters (0x11) - all signatures of runtime counters/age registers
  updated by the vendor table-manager kthread/CFE aging path, which cannot run on this kernel;
- the persistent-looking pages already **match** on both sides (pe+0x2000/0x42000 0xffff0000,
  eqm+0x0/0x40000, eps+0x0, pfe_glb 0x7af, mcc+0x0 0x201410, pub+0x57000 0x10001).

Replaying counters would be wrong, so patch 050 (W15) delivers the content-level comparison instead
of a write: luofu_switch.pagedump=1 prints the first 16 words of every differing page (plus the
matched control pages), reads each page twice 10 ms apart and labels it STABLE (persistent
candidate) or lists the words that CHANGED (runtime counter/status, do not replay).  A stock
devmem dump of the same pages confirms the labels; only pages that are stable on both sides become
the following gated write set.  This keeps the fail-closed, evidence-graded discipline.

**Round-4m: the GEMAC window diff and W16 (patch 051).**  The captured GEMAC dump (eth2/port 11,
offsets 0x0..0x2fc) was compared with a stock devmem read of window 0x14480000 (0x0..0x23c):
144 common words, six differences.  Decode of hi_kmac.ko classifies them:

- 0x1c: static config; hi_mac_hw_config writes it as 1 (the same word the macconf diagnostic
  sequence writes); stock is 1, ours 0 because macconf was off in the dump image.  Replay 0x1c=1.
- 0x100: max-frame + pause word.  hi_mac_hw_mtu sets the low 14 bits to (frame-length argument + 4);
  hi_mac_hw_set_pause preserves everything and only rewrites the pause bits in the MSB byte
  (word bits 28-29).  Stock holds 0x33400600, i.e. frame limit 0x600 = 1536 - 18 bytes above our
  MTU+18 value 0x5ee = 1518, so the vendor frame-length convention adds 36, not 18 (formula still
  [U]; measured value [V]).  W16 keeps the upper 18 bits and writes low 14 = 0x600.
- 0xa4, 0xac, 0xb0, 0xb8: statistics/counter words.  They are read through the read-op in
  himac_read_stats / himac_get_stats64 (32-bit counter pairs); there is no init write to them in
  the recovered paths.  Their stock/ours differences are runtime counter values - deliberately NOT
  replayed.

Patch 051 adds the gated W16 stage luofu_gmac.macregs=1 (required for netdev registration,
fail-closed): snapshot, stable-two-reads, exact readback and restore-on-mismatch for 0x1c=1 and the
stock-observed max-frame 0x100 = (pre & ~0x3fff) | 0x600; the counter words are left alone.
Expected lines: W16_MACREG: gmac+0x1c pre=... -> 1; gmac+0x100 pre=... -> ... (stock-observed
max-frame 1536, our W9 formula was 1518); counter words 0xa4/ac/b0/b8 are statistics, not replayed
then W16_MACREG: static GEMAC config programmed and verified (0x1c=1, 0x100=...).

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4k: all decodable init matches stock; the W14 page scan (patch 049).**  W13_RXCFG programmed
pie+0x3404 = 0x1b04 and verified it; the gmac dump works; the full chain W3-W13 is armed with a
stable heartbeat - and RX still never fires.  Every comparable programmable register now matches
the stock inventory, so the remaining class is the runtime flow/port-forwarding state that the
vendor CFE modules install on netdev/port events.  Decode note: hi_kcfe_pub itself imports only
netdev/netdevice-notifier APIs (no hardware access), and the table-manager consumers are
hi_kcara/hi_swa - none of which can load on this 6.18 kernel, so our image never writes those
tables.  The W11 dump sampled only a few offsets per window; the CFE tables can live anywhere in
the 15 MMIO windows.

Patch 049 (W14) adds a page-granularity read-only scan: luofu_switch.scan=1 reads one word per
4 KiB page of every LSW MMIO window (15 windows x 128 pages, non-zero pages printed) and
luofu_pie.scan=1 does the same for the 16 PIE pages.  Nothing is written.  The same page addresses
are read on a stock boot with devmem; the diff of non-zero pages lists exactly which table pages
exist on stock and not on ours - the location of the CFE/rootif tables to replay in the next gated
stage.  Stock scan command (one line per window base):

  for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
           0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
           0x12480000; do for o in $(seq 0 4096 520192); do v=$(devmem $(printf 0x%x $((w+o)))); \
           [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"; done; done

and for the PIE window: base 0x10a70000, offsets 0x0..0xf000 in 0x1000 steps.

**Round-4l: the W14 page map is runtime counter state, not missing configuration (patch 050).**
The W14 page scan on our image was diffed against a stock devmem page map.  Fifteen pages are
non-zero on stock and zero/different on ours, but the classification says they are **not**
persistent configuration:

- every differing offset (+0x1000/+0x2000/+0x6000/+0x40000/+0x41000/+0x42000/+0x46000 in the pe,
  eqm, eps, pa, ifc, l2, napt, mcc, opp and fam windows) appears in the vendor fixtures **only as a
  read inside hi_dp_dfx_dump_cnt / hi_pfe_dfx_dump_cnt** - the DFX counter-dump functions - and
  never as an init write;
- the same value 0x1b6 shows up in five engines (pe+0x1000, pe+0x41000, eqm+0x2000, eqm+0x42000,
  eps+0x6000), the two fam pages read 0x2f9/0x2fa (one apart, sampled microseconds apart), and our
  own values are small running counters (0x11) - all signatures of runtime counters/age registers
  updated by the vendor table-manager kthread/CFE aging path, which cannot run on this kernel;
- the persistent-looking pages already **match** on both sides (pe+0x2000/0x42000 0xffff0000,
  eqm+0x0/0x40000, eps+0x0, pfe_glb 0x7af, mcc+0x0 0x201410, pub+0x57000 0x10001).

Replaying counters would be wrong, so patch 050 (W15) delivers the content-level comparison instead
of a write: luofu_switch.pagedump=1 prints the first 16 words of every differing page (plus the
matched control pages), reads each page twice 10 ms apart and labels it STABLE (persistent
candidate) or lists the words that CHANGED (runtime counter/status, do not replay).  A stock
devmem dump of the same pages confirms the labels; only pages that are stable on both sides become
the following gated write set.  This keeps the fail-closed, evidence-graded discipline.

**Round-4m: the GEMAC window diff and W16 (patch 051).**  The captured GEMAC dump (eth2/port 11,
offsets 0x0..0x2fc) was compared with a stock devmem read of window 0x14480000 (0x0..0x23c):
144 common words, six differences.  Decode of hi_kmac.ko classifies them:

- 0x1c: static config; hi_mac_hw_config writes it as 1 (the same word the macconf diagnostic
  sequence writes); stock is 1, ours 0 because macconf was off in the dump image.  Replay 0x1c=1.
- 0x100: max-frame + pause word.  hi_mac_hw_mtu sets the low 14 bits to (frame-length argument + 4);
  hi_mac_hw_set_pause preserves everything and only rewrites the pause bits in the MSB byte
  (word bits 28-29).  Stock holds 0x33400600, i.e. frame limit 0x600 = 1536 - 18 bytes above our
  MTU+18 value 0x5ee = 1518, so the vendor frame-length convention adds 36, not 18 (formula still
  [U]; measured value [V]).  W16 keeps the upper 18 bits and writes low 14 = 0x600.
- 0xa4, 0xac, 0xb0, 0xb8: statistics/counter words.  They are read through the read-op in
  himac_read_stats / himac_get_stats64 (32-bit counter pairs); there is no init write to them in
  the recovered paths.  Their stock/ours differences are runtime counter values - deliberately NOT
  replayed.

Patch 051 adds the gated W16 stage luofu_gmac.macregs=1 (required for netdev registration,
fail-closed): snapshot, stable-two-reads, exact readback and restore-on-mismatch for 0x1c=1 and the
stock-observed max-frame 0x100 = (pre & ~0x3fff) | 0x600; the counter words are left alone.
Expected lines: W16_MACREG: gmac+0x1c pre=... -> 1; gmac+0x100 pre=... -> ... (stock-observed
max-frame 1536, our W9 formula was 1518); counter words 0xa4/ac/b0/b8 are statistics, not replayed
then W16_MACREG: static GEMAC config programmed and verified (0x1c=1, 0x100=...).

**Round-4e macconf run and the next layer: the IFC default-classification table (patch 043).**
Image f58c516e (six gates + PFE, W9 inert, heartbeat, macconf=1) stayed stable through the traffic
test and logged both eth2 link-ups with the vendor MAC sequence: before
`0x14=0x6 0x18=0x0 0x1c=0x0 0x40=0x10c0008 0x0=0x0`, after `0x1c=0x1`, 0x40 already correct,
0x0 still 0 - so the MAC config sequence is complete but **traffic is still dead**: the MAC is not
the missing layer, the classifier is.

**W10 decode.**  `hi_pfe_ifc_init@0x794` writes eight default entries through two interfaces,
both now decoded and replayed by the driver:

- **command/data engine** (ifc window): data `0x138/0x13c/0x140/0x144/0x148` (5 words), command
  `0x120 = 0xc0000000 | (index & 0x7f)`, then poll until bit31 clears (<=1001 tries), bit29 =
  error - the same protocol as the EQM indirect engine.  There is no read path in the recovered
  code, so these writes are acknowledged, not read back (like write-only table 19).
- **tab128 default entry**: four words at `ifc+0x50000 + index*16` (the shared
  `hi_lsw_write_tab128` primitive).  This region is directly readable and is verified STRICTLY.

The eight payload pairs were extracted by emulating `hi_pfe_ifc_init` from the pinned
`hi_lsw_pfe.ko` (52,412 B, sha256
`12ea9fcb90bef0b77f8ba54b4781f72de4af91b2a24c31d68813878212c49d1a`) and cross-checked by hand for
entries 1-5.  The only runtime-dependent field is tab128 word0 of entries 3..7, built from the
vendor table-manager allocation id (0 on a fresh manager -> `0x1d00`).

**Patch 043 (W10) implementation.**  New gate `luofu_switch.ifc=1` (requires
`arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1`) programs the eight entries: the command engine must
be idle before the first write, every tab128 word is snapshotted and required stable across two
reads, each entry is written engine-first then tab128, and the stage verifies all tab128 words by
readback; on an engine error or a readback mismatch the tab128 snapshot is restored and the stage
fails closed (the engine entries cannot be restored - stated in the log and here).  Readiness now
requires W3-W10, so the GEMAC netdevs only register with the classifier programmed.

**Bench verification for the next run.**  Expect the block of W10 start/idle lines, then
`W10: IFC default-classification table programmed and verified: 8 entries ...`, then the usual
connectivity-armed line, GEMAC registration and the heartbeat.  After the traffic test look for: a
host ARP request reaching the box followed by a box ARP reply on the wire (or PIE RX trace lines),
and no watchdog hang.  Failure signatures: `W10: IFC entry N command failed (bit29)` or a poll
timeout (engine protocol mismatch), or `tab128 readback mismatch ... restoring the snapshot`
(wrong payload/word order) - in both cases the box stays up (fail-closed, no data path).

If W10 alone still does not move traffic, the next ordered layers remain L2/VLAN tables and the
CFE/rootif contract, then the remaining MAC config words and table 38/VP.

**Round-4g: W10 verified on silicon, still no traffic - the read-only state dump (patch 045).**
Image 24794d64 ran the corrected W10 gate: `luofu_switch.ifc=1` programmed and verified the eight
IFC default entries (engine idle, tab128 read back exactly), the armed line included the IFC table,
GEMACs registered, eth2 1 Gbps, br-lan forwarding, heartbeat stable - and every frame is still
dropped (no PIE RX traces, no box frames on the wire).  All ten stages W3-W10 are verified, so the
missing state is the runtime forwarding layer the vendor installs later: the L2/VLAN/port-forwarding
configuration lives in the `hi_kcfe_lrn_*` / `hi_kcfe_pub` / `hi_kcara` module family, which is
built for the vendor 5.10 kernel and cannot load on our 6.18 kernel.  The PIE `g_api_priv` per-port
queue handle is a software structure in the vendor driver (not a hardware register pair), and the
hardware TX descriptor carries only the port tag - so it is not a missing hardware write either.

Patch 045 (W11) therefore adds a **read-only state dump** instead of another guessed write: with
`luofu_switch.dump=1` the armed driver prints a bounded snapshot of the candidate forwarding
areas (l2 config/table area, ifc defaults, dp/eps/pe/eqm/opp state, fam/ips) and writes nothing.

**Attended procedure for the stock-vs-ours diff.**
1. Boot the current series with the full chain plus `luofu_switch.dump=1` and capture the
   `lsw-dump ...` lines (~100 lines).
2. On a stock boot read the same window+offset addresses (window bases: l2 0x12180000, ifc
   0x12100000, dp 0x11000000, eps 0x11280000, pe 0x11180000, eqm 0x11200000, opp 0x12400000,
   fam 0x11100000, ips 0x11080000).
3. Every range where stock is non-zero and ours is zero is a missing init; that list becomes the
   next write set, decoded against the vendor module that owns it (kcfe_lrn_l2 for L2/VLAN, kcara
   for port state).

**Round-4h: the stock-vs-ours dump diff and W12 (patch 046).**  The W11 read-only dump was compared
with a stock-boot devmem read (evidence build/evidence-bench-gates/2026-11-20-lsw-diff.txt).
Three persistent differences and one non-configuration class were found:

1. **W10 tab128 word0 values were wrong at runtime.**  The vendor primitive writes entry idx at
   ifc+0x50000 + idx*16; our implementation wrote the 0-based array index i, i.e. every entry one
   slot too low, and assumed the vendor table-manager allocation id was 0 (0x1d00) where the stock
   hardware holds 0x1d01 (first hash id = 1).  Patch 046 writes entries 1..8 at their correct offsets
   with stock-observed word0 values: entry1 0x0, entry2 0x00100800, entries3..7 0x1d01, entry8
   0x00100800 [V for 1..7 from the diff; entry8 from the emulated init, marked [I]].  The W11 dump
   range now covers entry8 so the next run verifies it.
2. **16 extended EPS words were unprogrammed** (the second channel-map/companion bank that W6 does
   not cover): eps+0xb20..0xb3c = {0xa0,0x810a0,0x820a0,0x830a0, 0xb0,0x810b0,0x820b0,0x830b0} and
   eps+0xba0..0xbbc = {0x8004ffff,0x8105ffff,0x8106ffff,0x8107ffff} repeated twice [V measured].
3. **dp+0x1c** is 0x10000 on our boot but 0x18000 on stock.  Decode: the vendor DP probe writes
   BIT(16) | field16, where field16 is the upper half of a value from hi_system_mem_info_get (it read
   0 on our boot, hence W4's 0x10000); stock runtime additionally carries BIT(15).  The bit15 meaning
   is [U] (a vendor memory/runtime field, not a forwarding enable); W12 replays the stock-observed
   value 0x18000 [V measured].
4. **Not configuration:** the eqm+0x48..0x70 differences are the indirect command/data registers
   holding each side's last queue-init op (stock: 0x41100012 = table 17 index 18 write; ours: our
   last W7 write).  They are deliberate non-replays.

Patch 046 adds the W12 stage `luofu_switch.ext=1` (requires arm/egress/qidmap/eps/qminit/pfe/ifc):
snapshot / stable-two-reads / exact readback / restore-on-mismatch for all 16 EPS words and dp+0x1c,
fail-closed, readiness now W3-W12.  Expected console lines: the W12 start line, then
`W12: extended EPS/DP runtime words programmed and verified: 16 EPS words + dp+0x1c=0x18000`, then
the armed line; on mismatch the offending word, its read/snapshot values and a restore notice.

**Round-4k: all decodable init matches stock; the W14 page scan (patch 049).**  W13_RXCFG programmed
pie+0x3404 = 0x1b04 and verified it; the gmac dump works; the full chain W3-W13 is armed with a
stable heartbeat - and RX still never fires.  Every comparable programmable register now matches
the stock inventory, so the remaining class is the runtime flow/port-forwarding state that the
vendor CFE modules install on netdev/port events.  Decode note: hi_kcfe_pub itself imports only
netdev/netdevice-notifier APIs (no hardware access), and the table-manager consumers are
hi_kcara/hi_swa - none of which can load on this 6.18 kernel, so our image never writes those
tables.  The W11 dump sampled only a few offsets per window; the CFE tables can live anywhere in
the 15 MMIO windows.

Patch 049 (W14) adds a page-granularity read-only scan: luofu_switch.scan=1 reads one word per
4 KiB page of every LSW MMIO window (15 windows x 128 pages, non-zero pages printed) and
luofu_pie.scan=1 does the same for the 16 PIE pages.  Nothing is written.  The same page addresses
are read on a stock boot with devmem; the diff of non-zero pages lists exactly which table pages
exist on stock and not on ours - the location of the CFE/rootif tables to replay in the next gated
stage.  Stock scan command (one line per window base):

  for w in 0x11000000 0x11080000 0x11100000 0x11180000 0x11200000 0x11280000 0x11380000 \
           0x12000000 0x12080000 0x12100000 0x12180000 0x12280000 0x12380000 0x12400000 \
           0x12480000; do for o in $(seq 0 4096 520192); do v=$(devmem $(printf 0x%x $((w+o)))); \
           [ "$v" != "0x00000000" ] && echo "$w+0x$(printf %x $o) $v"; done; done

and for the PIE window: base 0x10a70000, offsets 0x0..0xf000 in 0x1000 steps.

**Round-4l: the W14 page map is runtime counter state, not missing configuration (patch 050).**
The W14 page scan on our image was diffed against a stock devmem page map.  Fifteen pages are
non-zero on stock and zero/different on ours, but the classification says they are **not**
persistent configuration:

- every differing offset (+0x1000/+0x2000/+0x6000/+0x40000/+0x41000/+0x42000/+0x46000 in the pe,
  eqm, eps, pa, ifc, l2, napt, mcc, opp and fam windows) appears in the vendor fixtures **only as a
  read inside hi_dp_dfx_dump_cnt / hi_pfe_dfx_dump_cnt** - the DFX counter-dump functions - and
  never as an init write;
- the same value 0x1b6 shows up in five engines (pe+0x1000, pe+0x41000, eqm+0x2000, eqm+0x42000,
  eps+0x6000), the two fam pages read 0x2f9/0x2fa (one apart, sampled microseconds apart), and our
  own values are small running counters (0x11) - all signatures of runtime counters/age registers
  updated by the vendor table-manager kthread/CFE aging path, which cannot run on this kernel;
- the persistent-looking pages already **match** on both sides (pe+0x2000/0x42000 0xffff0000,
  eqm+0x0/0x40000, eps+0x0, pfe_glb 0x7af, mcc+0x0 0x201410, pub+0x57000 0x10001).

Replaying counters would be wrong, so patch 050 (W15) delivers the content-level comparison instead
of a write: luofu_switch.pagedump=1 prints the first 16 words of every differing page (plus the
matched control pages), reads each page twice 10 ms apart and labels it STABLE (persistent
candidate) or lists the words that CHANGED (runtime counter/status, do not replay).  A stock
devmem dump of the same pages confirms the labels; only pages that are stable on both sides become
the following gated write set.  This keeps the fail-closed, evidence-graded discipline.

**Round-4m: the GEMAC window diff and W16 (patch 051).**  The captured GEMAC dump (eth2/port 11,
offsets 0x0..0x2fc) was compared with a stock devmem read of window 0x14480000 (0x0..0x23c):
144 common words, six differences.  Decode of hi_kmac.ko classifies them:

- 0x1c: static config; hi_mac_hw_config writes it as 1 (the same word the macconf diagnostic
  sequence writes); stock is 1, ours 0 because macconf was off in the dump image.  Replay 0x1c=1.
- 0x100: max-frame + pause word.  hi_mac_hw_mtu sets the low 14 bits to (frame-length argument + 4);
  hi_mac_hw_set_pause preserves everything and only rewrites the pause bits in the MSB byte
  (word bits 28-29).  Stock holds 0x33400600, i.e. frame limit 0x600 = 1536 - 18 bytes above our
  MTU+18 value 0x5ee = 1518, so the vendor frame-length convention adds 36, not 18 (formula still
  [U]; measured value [V]).  W16 keeps the upper 18 bits and writes low 14 = 0x600.
- 0xa4, 0xac, 0xb0, 0xb8: statistics/counter words.  They are read through the read-op in
  himac_read_stats / himac_get_stats64 (32-bit counter pairs); there is no init write to them in
  the recovered paths.  Their stock/ours differences are runtime counter values - deliberately NOT
  replayed.

Patch 051 adds the gated W16 stage luofu_gmac.macregs=1 (required for netdev registration,
fail-closed): snapshot, stable-two-reads, exact readback and restore-on-mismatch for 0x1c=1 and the
stock-observed max-frame 0x100 = (pre & ~0x3fff) | 0x600; the counter words are left alone.
Expected lines: W16_MACREG: gmac+0x1c pre=... -> 1; gmac+0x100 pre=... -> ... (stock-observed
max-frame 1536, our W9 formula was 1518); counter words 0xa4/ac/b0/b8 are statistics, not replayed
then W16_MACREG: static GEMAC config programmed and verified (0x1c=1, 0x100=...).

**Still open (next layers, ordered).**  The IFC default-classification table (`ifc+0x50000`, 16-byte
entries written by `hi_pfe_ifc_init` through `hi_lsw_write_tab128`) is decoded at call-shape level
only: each entry embeds an id returned by the vendor's software table-manager allocation
(`hi_tab_mgr_alloc_hash`), so reproducing the exact payloads needs that allocator's id sequence
decoded first; it stays out of W9.  Then: L2/VLAN tables and the CFE/rootif contract
(`hi_kcfe_pub`, `hi_kcara`, `hi_swa`), the remaining MAC config words (0x14/0x18/0x1c and the 0x40
read-modify), NAPT/MCC, table 38 / VP class, and the PIE per-port queue handle.
---

## 8. Forwarding, MAC learning, VLAN and ACL tables

### 8.1 The generic table manager: id space and shapes [V]

`hi_lsw_pub!hi_tab_mgr_create(id, cfg)` (id < 8192, `id & 0xc0 == 0` check; control array
`g_tab_ctrl` = 8192 B in `.bss`, one pointer per id) creates a table object whose cfg carries
name, key/data sizes, total entries, a type/mode byte, and hardware callbacks. The manager has
two allocation modes (`hi_tab_mgr_alloc_hash`, `hi_tab_mgr_alloc_direct`), aging
(`hi_tab_mgr_get_age`, `tab_mgr_kthread`, module param `mgr_thread_time`), and DFX dump/clear
hooks. Hardware callbacks are what actually write the silicon: e.g. the DP queue table
(`id 0`) passes `hi_eqm_set_sht`/`hi_eqm_dump_sht`; the PFE tables pass their own
write/read/dump/age functions (`hi_tab_write/read/dump_ifc_cnt`, `pfe_napt_v4/v6_*`,
`hi_tab_write/read_l3_cnt`, `hi_tab_write/read_nhp_cnt`, `write/read/dump_glb_attr`, ...).

Table ids observed in the fixtures (all [V]; the mapping id -> logical table is [I] except where
the creating function/string says so):

| id | created in | logical table |
| --- | --- | --- |
| `0` | `hi_lsw_dp!hi_queue_init` | EQM queue table (callbacks `hi_eqm_set_sht`/`hi_eqm_dump_sht`) |
| `0x700` | `hi_lsw_pfe!hi_pfe_init` | global attributes (`read/write/dump_glb_attr`) |
| `0x701` | `hi_pfe_ifc_init` | IFC-related |
| `0x702` | `hi_pfe_napt_init` area | flow counters (`hi_tab_write/read/dump_flow_cnt`) |
| `0x703` | `hi_lsw_dp!hi_dp_tab_init` | L3 counters (`hi_tab_write/read_l3_cnt`) |
| `0x704` | `hi_lsw_dp!hi_dp_tab_init` | NHP counters (`hi_tab_write/read_nhp_cnt`) |
| `0x705` | `hi_pfe_ifc_init` | IFC counters (`hi_tab_read/write_ifc_cnt`) |
| `0x70d` | `hi_pfe_napt_init` | NAPT flow table (v4/v6 callbacks) |
| `0x70f` | `hi_pfe_ifc_init` | IFC table (`hi_tab_dump_cnt` + count-name string) |
| `0x712` | `hi_pfe_mcc_init` | MCC user link list |

### 8.2 The IFC classifier: the ACL/forwarding key [V field list, U encoding]

`ifc_write`/`ifc_dump`/`ifc_del` in `hi_lsw_pfe`, plus the ADP flow writers in `hi_kcara`,
program the "IFC" table. The dump strings enumerate the key fields the vendor's debug output
prints (`.rodata.str1.4` of `hi_lsw_pfe`): `dmac, smac, stpid, spbit, svlan, ctpid, cpbit,
cvlan, etype, session_id, ppp_protocol, dipv4, sipv4, ip_protocol, sipv6_l, sipv6_h, dipv6_l,
dipv6_h, next_hd, dport, sport, icmpv6_type, eth_encp, ppp_encp, frm_type, tag_sum_rf5,
tag_type, ip_frame, pkt_tunnel, ip_frag, ptype, flength, ip_length, range0_match..range3_match,
xdf_udf0..xdf_udf3, dos_type`. The action side is
`[act]: id, igr_bitmap, label, map_pri, eof, pq, pq_en, car_id, car_en, statis_idx`
(+ `mask` per key field). This is a full **L2-L4 classifier with VLAN/PPP/tunnel awareness,
4 match ranges, 4 user-defined fields and a DoS type** - i.e. the ACL/datapath offload engine.

The field *encodings* (bit offsets, widths, table row layout) are **[U]**: the dump prints them
by name, but the write functions encode them through register writes whose bit arithmetic is
only partially recovered. A clean-room ACL/flow offload (D3+, out of scope for this document's
D1/D2) needs either GPL source or a bench campaign with `ifc_dump` on a known flow.

### 8.3 L2 / MAC learning [V counters and software table, U hardware format]

The hardware L2 block (`l2` window, id 1, `0x12180000`) exists (DT + `hi_lsw_pfe` DFX counters
`port_move`, `age_drop`, `sport_clash`, `search_ok_mc`, `search_fail_mc`), but **no L2 table
write function exists in `hi_lsw_pfe`'s exported surface**; the vendor's L2 learning is the
software library `hi_kcfe_lrn_l2` (`hi_cfe_lrn_l2_add/del/check/get_age/dump/cnt`), which calls
the CFE acceleration API (`hi_cfe_acc_set_mac/set_dir/set_qos/set_stat/set_tagact`) and
`hi_adp_flow_add/del` - i.e. learning produces **flow/IFC entries**, not raw L2 table rows.
The generic CFE table framework (`hi_cfe_tab_*`) holds the software FDB (dump format
`dst updated used flag vid`, MAC bytes, per-interface counters), and `hi_cfe_intf_set_br_fdb`/
`get_br_fdb` mirror to the bridge FDB (with the vendor-patched `hi_br_fdb_check_addr`).

Implications [I]:

- D1 (no offload) needs **no L2 table**: the Linux bridge learns in software; the LSW must be
  in a "forward everything to the host / port-to-port default" mode. The minimal requirement is
  to find the default-forwarding configuration, which is one of the D1 bench questions.
- D2 switchdev offload should map **Linux FDB entries** to the hardware L2/flows. Whether the
  hardware L2 table is writable directly (a table shape we have not recovered) or only through
  the IFC/flow path is **[U]**; the vendor's own choice (flow path) is the safer model.
- The `age_drop`/`port_move` counters prove the hardware does *something* on MAC move/aging,
  so a D1 caveat: leaving the L2 engine enabled with an empty table may drop or misforward
  unknown unicast. The D1 bring-up must start from a documented all-ports-forward state. [I]

### 8.4 VLAN [V API surface, U table format]

Three mechanisms exist in the vendor stack:

1. **tag action table** in PFE: `tag_write`/`tag_dump` (PFE) and the ADP
   `hi_cara_flow_set_act_tag` string; the CFE side has
   `hi_cfe_tag_add_by_attr`/`hi_cfe_tag_del`/`hi_cfe_tag_del_by_cb`/`hi_cfe_tag_get_svlan`,
   with an attribute list including `tag_type`, `stpid`, `ctpid`, `svlan`, `cvlan`.
2. **VLAN filter items**: `hi_adp_vlan_filter_item_add/del`,
   `hi_cfe_add_rootif_vlan/del_rootif_vlan`, `hi_cfe_tag_filter_add/del/enable`, and the
   `hi_swa` proc interface `vlan_filter` (`swa_dfx_update_vlan_filter`,
   `dev=%s,intf_type=%d,vlan_filter=%u,enable=%u`). This is the vendor's per-interface VLAN
   enable/filter model, driven from userspace (`/proc/hsan/net/swa/...`).
3. **sub-interfaces**: `hi_cfe_add_subif/del_subif/set_subif_attr`, with the string
   "Do not support second layer untag subif yet" - i.e. the vendor model is a VLAN-tag
   sub-interface on top of a rootif.

Which hardware table stores the per-port PVID/VLAN membership is **[U]**; the candidates are the
`l2` window, the tag-action table and the IFC key fields (`stpid/spbit/svlan/ctpid/...`).
Given the D2 scope ("bridge/VLAN object offload"), this is the first D2 RE item. For D1, VLANs
are software-only (Linux 8021q/bridge) and the hardware must not filter; whether the LSW can be
put in that mode is **[U]** and is a D1 bench question.

### 8.5 CAR, mirroring, NAPT, multicast (D2/D3, evidence for scope only)

- CAR/shaper: `car_write/car_del/car_profile_write/dump` (PFE), `hi_port_set_igr_car`,
  `hi_queue_set_shaping/egr_shaping/sch/buf`, `hi_dev_queue_*` (DP), and the ADP
  `hi_cara_cid_*`/`hi_ca_set_*` wrappers. The thermal net-cooling device `hsan,net-cdev` uses
  the CAR API ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) block B), which is why "CAR" is
  called out as a cross-block dependency there [V symbols].
- Mirroring: `hi_port_set_mirror` (PFE) + `hi_kport_mirror` proc logic, using the PIE xmit path
  to re-inject mirrored frames [V].
- NAPT: `pfe_napt_v4/v6_*` and the `TAB_FLOW_V4/V6` strings in `hi_kcara`; this is stateful
  flow offload, D3 [V].
- Multicast: `hi_cfe_lrn_mc` + `hi_kcfe_mc_list` + `TAB_MCC`/`hi_pfe_mcc_init`; group tables and
  per-user link lists [V symbols].
- `lsw_woe` (WoE offload engine, SPI 80-84) has no driver module and no consumer symbols; its
  DT node with 5 IRQs is all the evidence [V absence]. Out of scope for D1/D2 [I].

### 8.6 What the vendor does *not* do [V]

- No tag-based CPU port, no DSA switch tag, no `dsa_*` symbols anywhere in either rootfs.
- No `switchdev_ops` on any netdev (only the notifier blocks registered by `hi_swa`, whose
  handlers are stubs, section 4.7).
- No per-port netdev creation in the LSW modules: netdevs are created by `hi_kmac` and the CFE
  vif code; the LSW only consumes them by name/index (`hi_net_get_port`, `hi_cfe_*intf*`).
- No devlink, no ethtool per-port ops, no `ndo_bridge_*` in the LSW modules.

---

## 9. The CFE/rootif contract and the switchdev design

### 9.1 Why the vendor has a rootif write at all [V + I]

The vendor kernel stack is not a hardware-forwarding switch driver; it is a **software flow
engine grafted onto the Linux stack**, and it needs a device identity to hang policy on:

- the CFE keeps a name-indexed interface list (rootif + subif). A rootif is a *Linux netdev*
  (bridge, VLAN device or plain port device) that has been bound into the CFE model by
  `hi_cfe_add_rootif()`. The record carries the name, a device pointer pair and the enable
  flag; `hi_cfe_del_rootif()` releases it.
- the CFE uses that binding to attach **VLAN filters** (`add_rootif_vlan`/`del_rootif_vlan`),
  **sub-interfaces** (VLAN tag vifs: `hi_cfe_add_subif`), **stats rules**
  (`hi_cfe_intf_set_stat`), **tag actions** and **FDB entries** to the interface. The interface
  attribute dump format is `father interface / dev / attr type / vid / vlan_filter_en / mc_vid /
  mc_en / pri / pq / [stat info] rx_en rx_idx tx_en tx_idx rx_ruleid tx_ruleid` and the FDB
  format is `dst updated used flag vid` - all [V] strings.
- the CFE packet path (`hi_cfe_intf_rx/tx`) maps an ingress (port, skb) to an interface, applies
  the learned flow/tag/forward decision and either forwards in hardware or passes to Linux.
- the userspace contract (`/proc/hsan/cfe/*`, `/proc/hsan/net/swa/*`, `hi_cfm`) and the OpenWrt
  hotplug scripts (`08-wan-detect` reads `/proc/hsan/cfe/intf/dump_intf`) depend on those names
  existing [V: parent `rootfs-2.4.15/etc/hotplug.d/gmac/08-wan-detect`].

`hi_swa` exists to bridge the *OpenWrt* world into that model when the CFE is in bypass mode:
it watches netdevice events and adds/removes rootifs for real netdevs (bridges, VLANs, PPPoE
related devices via `ppp_get_relate_dev`), and it registers switchdev notifier blocks so that
the kernel's switchdev events can be routed to `hi_cfe_flush_by_mac()` (MAC/port object
deletion). The actual `port_obj_add/del` handlers are stubs [V 4.7].

**So the rootif write is not a hardware operation and is not required for forwarding in
hardware - it is the vendor's control-plane bookkeeping for its own userspace.** A clean-room
driver must not reproduce it; our userspace contract is the standard Linux one (bridge/VLAN
netlink, switchdev, ethtool, devlink if needed). [I]

### 9.2 Reference table: vendor object -> our object [I]

| vendor concept (evidence) | hardware reality | clean-room home |
| --- | --- | --- |
| rootif (`hi_cfe_add_rootif`, 32-entry table in `hi_swa`) | none (software identity) | Linux bridge / VLAN upper device; no driver object |
| subif (`hi_cfe_add_subif`, VLAN vif) | none (Linux netdev) | `8021q`/bridge VLAN; switchdev `BR_VLAN` object in D2 |
| intf attr (vid, vlan_filter_en, pri, pq, rx/tx rule ids) | IFC/L2/tag tables [U] | switchdev VLAN/port attrs in D2 |
| CFE FDB (`hi_cfe_tab_*`, `hi_cfe_intf_set_br_fdb`) | L2/flow table [U] | Linux bridge FDB (+ offload flags) |
| CFE hooks (8 hook points) | none | not modelled; use TC/switchdev hooks |
| ADP flow (`TAB_FLOW_V4/V6`) | NAPT/IFC | D3 flow offload (out of scope) |
| port id / vport (`hi_lsw_get_vport`) | LSW port | switch port object / port netdev |
| queue handle per port (`hi_queue_get_qid`) | EQM qid | internal to switch core driver |
| qid map (`port_qid_map_set`) | opp+0x42800/0x43800 | internal to switch core driver |
| EPS channel map (`hi_pie_map_init`) | eps+0xB00/0xB80 | internal to switch core driver |
| mirror (`hi_port_set_mirror`) | PFE mirror | D3 (devlink/TC mirror) |
| CAR (`car_write`) | PFE CAR | D3 (devlink rate) |

### 9.3 switchdev, not DSA [V/I]

Evidence for the decision (same conclusion as [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
section 5.3, now with module-level proof):

- no tag protocol / CPU-port framing anywhere: no `dsa_*` symbols, no tag-driver-shaped code,
  no `ndo_start_xmit` tag push in the LSW modules; the MAC netdevs are ordinary netdevs whose
  xmit goes to PIE (`hi_net_dev_xmit`);
- per-port netdevs exist by construction (MAC-owned), which is the switchdev model
  (switchdev notifier on each port netdev);
- the actual vendor integration registers the switchdev notifier blocks (`hi_swa` imports
  `register_switchdev_notifier`/`register_switchdev_blocking_notifier` and the two
  `switchdev_handle_port_obj_*` helpers), even though its handlers are stubs;
- the switch is a flow/queue engine fronted by per-port MACs; DSA would force a tag/CPU-port
  model that has no hardware counterpart (and no evidence in five modules).

Design consequence: **one switch core platform device with N port netdevs**, a switchdev
notifier on those ports, and (later) a standard `swdev` attribute set. DSA can only be revisited
if a future GPL source or bench trace reveals a tag format - none exists today [U].

### 9.4 Clean-room driver shape [I]

~~~
ethernet-switch@... {                          /* lsw_dp windows + queues/qid maps */
    compatible = "hisilicon,luofu-lsw";        /* name TBD */
    reg = <queue ...>, <dp ...>, <eps ...>, <eqm ...>, <fam ...>, <ips ...>;
    reg-names = "queue", "dp", "eps", "eqm", "fam", "ips";
    clocks = <&crg LUOFU_CLK_LSW_DP>;
    resets = <&crg CRG_RESET(LUOFU_LSW_DP_LOGIC)>, ...;
    status = "disabled";                       /* until D1 is proven on the bench */
    ports {
        #address-cells = <1>; #size-cells = <0>;
        port@0 { reg = <0>; label = "lan0"; ethernet = <&gemac1>; };
        ...
    };
};
switch-parser@... {                            /* lsw_pfe windows */
    compatible = "hisilicon,luofu-lsw-pfe";    /* or one node with 17 ranges */
    reg = <ifc ...> ...; status = "disabled";
};
~~~

- The port netdev stays owned by the GEMAC driver (patch `009`): that matches the hardware
  split (MAC/phylink vs queue engine) and avoids two drivers fighting over one netdev.
- The switch driver validates its resources and registers the switchdev notifier; it must
  **not** change any register in probe until the D1 campaign has verified the write set
  (section 10.4). Until then the node stays `"disabled"` - a probe that writes unverified LSW
  registers is a bench hazard, not a milestone.
- The PIE driver (patch `015`, draft) keeps rings/DMA/NAPI and the per-port queue handle;
  switch <-> PIE coupling is a small in-kernel API (exported from PIE: channel start/num,
  queue hook, netdev xmit), not DT phandles for every queue.
- The upstream `switchdev` API is stable; the vendor's shape is a guide to *which objects exist*,
  not an ABI. We implement our own `switchdev_ops` callbacks with the Linux objects as truth.

### 9.5 What D1 means concretely [I]

D1 is "our kernel forwards between two wired ports with no vendor module bound"
([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 5.3). Given the RE, the minimal D1
configuration is:

1. PIE up on all four wired channels (patch `015` + PIE-DATAPATH plan);
2. LSW core up with the four MAC ports enabled and **default forwarding** configured: the LSW
   must deliver frames received on one MAC port to the host queues (destination port = host
   queue port) and accept host-queue frames destined for each MAC port. This is the
   queue/port default-map problem: the D1 driver programs the qid map for the MAC ports and
   the host path, with the mapping constants measured on the bench (section 12);
3. no learning, no VLAN filtering, no ACL/flow offload in hardware;
4. Linux bridge in software between eth0..eth3 (or a subset), WAN as a separate netdev (policy).

D1 explicitly does **not** require: rootif/CFE, the `pfe` classifier tables (IFC), NAPT, CAR,
mirror, WoE, virtual ports, the qid-map high port classes (40..47, >47) unless the MAC/host
ports surprise us, and the `queue`/`flow` DDR layout (it is required only if the engine's
descriptors live there - [U], see section 5.4).

---

## 10. Init/order requirements and dependencies

### 10.1 Vendor load order [V]

`/etc/init.d/hsan_prepare` (START=09) calls `/etc/rc.d/luofu` then sets
`hi_kcfe_pub.g_intf_bypass=1` and loads `hi_swa`. `/etc/rc.d/luofu` order (parent
`rootfs-2.4.15/etc/rc.d/luofu`; [V] local fixture):

1. `hi_kpinctrl`, gpio-generic/dwapb, RNG (`hi_kjent_rng`), cpufreq chain (`hi_efuse`,
   `hi_kpmupwm`, pwm-regulator, `hi_clk_corrector`, `hi_cpufreq`);
2. `hi_sysctrl`, `hi_acp` (ACP/coherency);
3. **`hi_kpie`**, then four PIE IRQ SMP-affinity pins (`pie-ch0..3`);
4. **`hi_lsw_pub` -> `hi_lsw_dp` -> `hi_lsw_pfe`**;
5. `hi_kmdio`, `hi_kphy`, `hi_kmac` (MAC netdevs appear here; phylink starts);
6. `hi_kteam`, `hi_woc_wifi_rtx`;
7. `hi_kcfe_pub`, `hi_kcfe_lrn`, `hi_kcfe_lrn_l3`, `_l4`, `_mc`, `hi_kcfe_mc_list`,
   `_vxlan`, `hi_kcara`, `hi_khslink`;
8. temperature (`hi_knet_cdev`, `hi_tvsensor`), then (S09) the bypass flag + **`hi_swa`**.

Hard ordering constraints visible in the code: PIE exports are imported by `hi_lsw_dp` (so PIE
must be up before the LSW queue manager initialises), the LSW pub layer must exist before dp/pfe,
`hi_kcfe_pub` needs `hi_kpie` (`hi_net_*`), the learning libs need `hi_kcfe_pub` + `hi_cfe_lrn`,
`hi_kcara` needs pub+dp, and `hi_swa` needs CFE+learning+pie. [V `depends=` chains + imports]

### 10.2 Our dependency DAG (patch numbers) [V files; I ordering]

| driver step | depends on | why |
| --- | --- | --- |
| platform/DT (`001`, `002`, `003`) | - | mach descriptor, TEXT_OFFSET; the LSW nodes are already in the draft DTS, disabled |
| CRG clocks/resets (`004`, `014`) | - | `lsw_dp_clk`/`lsw_pfe_clk` are fixed-clock nodes today (166/333 MHz); the resets are CRG word `0x30` bits 0/1/3/4; **no new driver may ioremap `0x14880000`** - consume generic resets per the shared-page contract in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.3 |
| MDIO (`007`) + PHY (`008`) + GEMAC (`009`) | `004` | the four port netdevs and PHYs |
| PIE (`015`, draft) | `004` | rings/DMA/NAPI and the per-port queue handle; `hi_pie_get_*_ch_*` equivalents |
| LSW D1 core | `004` + `015` + `009` | qid map needs the queue manager (ours, in the LSW driver) and PIE channel map; ports need MAC netdevs; reset/clock needs CRG |
| LSW D2 switchdev | D1 + `009` | bridge/VLAN objects attach to the port netdevs |
| Wi-Fi port / WoE / flow offload | [X] | Wi-Fi is [X] ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.3) and additionally needs the PCIe host (patch `013`) plus the endpoint driver; the internal LSW serves the wired MAC ports only, so `013` is **not** a dependency of LSW D1/D2; WoE has no driver; NAPT is optional |

The two links that are *not* optional and often forgotten: **(a)** PIE exports are the LSW's only
host-side registration point ([PIE-DATAPATH.md](PIE-DATAPATH.md) sections 2.1/7), so D1 cannot be
built before the PIE driver has at least the channel-map and queue-hook API in place; **(b)** the
vendor's probe order inside `hi_lsw_dp` shows PIE channel info is read at LSW probe time
(`hi_pie_map_init`), so a real DT order/`-EPROBE_DEFER` rule is needed between PIE and LSW, not
just link order.

### 10.3 Reset/clock order inside the LSW probe [V code, U necessity]

DP: clocks prepared/enabled before reset pulse; reset pulse = deassert/assert/deassert of the
DT array, then `dp_logic` and `dpu_srst` reset controls are respectively asserted then
deasserted; then the five windows are registered and the first register writes happen. PFE:
clock get/prepare/enable, then assert/deassert of the array, then windows and sub-block init.
Whether the resets must be asserted at all (as opposed to just deasserted once) and whether the
extra `dpu_srst` is real cannot be answered without a bench trace of the vendor probe [U].

### 10.4 The "no unverified register writes" rule for our probe [I]

Mandated by this project's fire rules (parent `LESSONS.md`): the D0/D1 driver's `.probe()` must

1. bind and validate DT resources (windows, sizes, reg-names, clock/reset counts);
2. map windows with `devm_ioremap_resource()` (mapping is safe; it does not touch the block);
3. optionally enable clocks/deassert resets **only** if a bench vetting session has signed off
   the exact CRG writes (this is a separate gate from the LSW register writes);
4. never issue `hi_lsw_*`-equivalent register writes until the D1 write set has been validated
   by the attended campaign (section 12) - the first version must exit probe with a clear
   "register programming not verified" message and leave the hardware untouched;
5. keep the DT node `disabled` in the shipped board DTS until D1 passes, so an unproven probe
   is never run by a user.

This rule makes D0 (this document) and "probe skeleton" safe host-only work while the hardware
campaign is still blocked ([X] no 6.18 bootable target today).

---

## 11. Staged plan: D0 -> D1 -> D2

Estimates are engineering effort, not measurements; "host" = host-only session, "bench" =
attended hardware session with the recovery door armed (parent `BENCH-FACTS.md`).
[REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 5.3 gives the coarse estimate (D1 15-25
host + 4-8 bench sessions; D2 +10-15 host); the staging below refines it.

### D0 - RE and specification (this document) and the bench inventory

| item | deliverable | gate / exit criterion |
| --- | --- | --- |
| module layering, register map, port/queue model | this file | reviewed by the target/RE leads |
| reproduce the disassembly evidence | local lane `build/linux-kbuild/waves/2026-11-18-lsw/` | commands in 2.3 reproduce every table |
| read-only bench inventory plan | section 12.2 | plan reviewed; no writes |
| GPL asks updated if new artifacts are needed | [GPL-REQUEST.md](GPL-REQUEST.md) follow-up | items 2-4 cover hi_lsw/hi_kcfe sources |

Effort: 2-4 host sessions (this lane) + 1-2 attended bench sessions for the read-only inventory
(only possible once a 6.18 target exists; the same inventory can be taken on the *stock* vendor
kernel where the same vendor modules run - the current bench line - because it is read-only).

### D1 - minimal forwarding, one bridge, no offload

Split into four gates so each has a measurable exit:

| gate | work | exit criterion | bench? |
| --- | --- | --- | --- |
| D1a: LSW probe skeleton | platform driver, DT binding, window validation, no writes; Kconfig `LUOFU_LSW` (=n in the image until proven), YAML, DTS node disabled; compile + dt_binding_check | full compile RC=0, binding check PASS, probe not run | host |
| D1b: clock/reset/queue-map write set | define the minimal write set: clock enable, reset deassert, qid-map table programming for host + 4 MAC ports, EQM queue table for 4+1 ports | a written, reviewed "write set" table with every write justified by RE or bench measurement; host compile | host + 1-2 bench (vendor register traces on stock kernel, read-only + vendor-side proc dumps) |
| D1c: host queue path | PIE <-> LSW queue handle binding, TX path from port netdev to a MAC port, RX path from a MAC port to the host queue; default-forwarding only | one ping between two wired ports with our drivers bound (attended) | bench (2-3 sessions) |
| D1d: bridge | Linux bridge over eth0..eth3, no hardware offload; WAN/UCI policy; M2 measurement ("no vendor modules bound") | L2 forwarding between LAN ports and to WAN, one variable per session; M2-equivalent recorded | bench (2-3 sessions) |

Effort: 15-25 host sessions for the RE/implementation (dominated by D1b's write-set derivation)
+ 4-8 attended bench sessions. If GPL source arrives, D1 collapses to ~5-8 host sessions (port
instead of RE) - the single biggest schedule change ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
section 5.3).

### D2 - switchdev offload

| item | content | gate |
| --- | --- | --- |
| VLAN offload | map bridge VLAN objects (vid, pvid, tagged/untagged) to the LSW VLAN mechanism (8.4); requires the D2 RE item "where is the per-port VLAN/PVID table" solved | `bridge vlan` offloaded, no data-path regression |
| FDB offload | map bridge FDB entries to the hardware L2/flow path (8.3); start with static entries, then learning+aging | `bridge fdb` offload count > 0 and traffic still correct |
| port attrs | learning/state/flood flags via switchdev attrs; STP states | mdb/fdb/attr matrix |
| D3 (optional, not v1) | NAPT/flow offload, CAR, mirror, WoE | separate plan |

Effort: 10-15 host sessions + 3-5 attended bench sessions; must not gate the target's first
submission ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 7).

### Alternative path if the GPL source arrives ([X] today)

Source for `hi_lsw_*`/`hi_kcfe_*`/`hi_kcara` converts sections 5-8 into a port of known code
(licensing: GPL-2.0, see [GPL-REQUEST.md](GPL-REQUEST.md)), and D1 drops from 15-25 to 5-8 host
sessions. The plan above is written so that either path lands the same clean-room interface;
the vendor source would be used to *check* the RE, not copied into the driver (clean-room
constraint, [DRIVER-BLACKBOX.md](DRIVER-BLACKBOX.md)).

---

## 12. Bench-only steps and risks

### 12.1 Hard constraints

No UART writes, no fastboot/TFTP/fire by this document, no `/dev/mem` pokes from userspace
into LSW registers; every hardware step is attended with the recovery door armed, one variable
at a time, recovery plan current (parent `BENCH-FACTS.md`, `LESSONS.md`). Nothing here changes
those rules.

### 12.2 Read-only inventory (D0/D1b, stock vendor kernel is acceptable)

These are read-only and can be taken on the current bench line without touching our drivers:

1. `/proc/hsan/...` dumps: the DP/PFE/queue DFX dumps listed in 7.6,
   `/proc/hsan/cfe/intf/dump_intf`, `/proc/hsan/net/swa/*`, `/proc/hsan/cfe/*` - they name the
   live ports, queue counts and interface mapping (strings already recovered from the modules).
2. `/sys/class/net/*` and `dmesg`: MAC<->PHY mapping (already measured, section 6.1), netdev
   list, phylink state.
3. DT readback: `/proc/device-tree` (or a decompiled live DTB) of `lsw_dp`/`lsw_pfe`/`pie`
   after U-Boot's overlay - this answers the **queue window base** question (5.4) definitively
   without any register access.
4. `/proc/meminfo`/reserved-memory and `dmesg` early lines for the `queue`/`flow` reserved
   regions - cross-check of 5.4.
5. `hi_cfm` (userspace) read commands: board/port info; read-only.
6. vendor module parameter state: `/sys/module/hi_kcfe_pub/parameters/g_intf_bypass` (=1 in the
   stock boot, 4.5).

Each item must be captured to an absolute path under `build/uart-logs/` or
`build/linux-kbuild/waves/<date>-lsw/` as evidence with the command line. **No register writes.**

**Status 2026-11-18:** item 3's core question is **closed** - the live post-overlay `lsw_dp`
reg gives `queue = 0x80000000/0x400000` and `flow = 0x80400000/0x100000` (5.4), and the
register inventory captured the W1 baseline (7.3) plus the window-name order (5.2). The
`/proc/meminfo`/reserved-memory early-dmesg cross-check (item 4) was not taken, nor were the
`/proc/hsan` DFX dumps (items 1, 6); both remain separate bounded reads.

### 12.3 Register-write campaign (D1b, attended, one variable at a time)

Only after a bootable 6.18 target exists (or with a dedicated safe test image) - the bench image now boots to userspace (2026-11-19), so W3 is the first step that needs an attended boot with the recovery door armed:

| step | read/write | question answered | rollback |
| --- | --- | --- | --- |
| W1 | read `dp+0x68/0x64/0x1c`, `eps+0x1008`, `eqm+0x48`, `opp+0x42800` before/after vendor init (stock kernel, via vendor proc dumps or a read-only debug module) | baseline values and which fields the probe writes - **DONE 2026-11-18 (read-only inv1):** the post-vendor-boot baseline is measured (values in [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3, incl. the qid table A/B contents). W2 can be derived from it; no write was made | power cycle |
| W2 | write nothing; finalise the D1b write set on paper and compare to W1 | does the write set exist / is it complete - **DONE 2026-11-18 (read-only, host-only):** derived write set tabulated in section 7.7; patch `021` re-derives the qid-map model and validates it against all 384 measured table words without writing, and gates the read on the measured powered state; write implementation was patches `022`/`025` | n/a |
| W3 | first own-kernel write: clock enable + reset deassert only, no LSW register writes; observe link state | does the block survive reset/clock handling - **implemented behind `luofu_switch.arm=1` (patch `022`); never executed**; the first attended run should use this gate alone and record the snapshot | power cycle |
| W4 | R1/R3/R5 probe configuration only (`luofu_switch.arm=1 luofu_switch.egress=1`); no qid-map write | are the three probe-configuration derivations right and does the block survive them - **implemented behind `luofu_switch.egress=1` (patch `025`); never executed.** The probe refuses after the step when the qid-map gate is absent, restores the found words and powers the block down; the stage's success evidence is its readback log | power cycle |
| W5 | qid-map write for **one** host queue + **one** MAC port; add the remaining populated rows | is the qid-map layout/model right - **all 13 populated rows are implemented behind `luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1` (patches `022`/`025`); never executed.** The "one port only" first run is not implemented; the attended plan should start at W3, add W4 in a second boot and add the qid-map gate in a third | power cycle |
| W6 | EPS channel-map/companion programming (`luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1 luofu_switch.eps=1`) | does the MAC/PIE channel to EPS queue-port map take - **implemented behind luofu_switch.eps=1 (patch `031`); never executed.** Bench run 2026-11-19 (patch 031): the stage programmed and verified, and the PIE TX descriptors were still consumed without a frame on the wire | power cycle |
| W7 | EQM queue-manager init tables (`... luofu_switch.qminit=1`) then the default-forwarding/TX test | D1c exit - **two hardware attempts 2026-11-19, both refused fail-closed with GEMAC unregistered. Round 1 (image 14791958): table-19 strict readback failed; decode proved table 19 write-only. Round 2 (image 5d2c5476): tables 32 and 35 verified on silicon, table 17 failed on its unreadable words 0/4 (words 1-3 read back exactly, so the data-register mapping is correct); patch `032` now uses tiered verification (STRICT only on bench-proven fields, DIAG for tables 16/34, ACK for table 19) and restores only bench-proven entries (7.9). This is the next boot that can answer whether a tagged PIE TX descriptor egresses once the queue manager is configured**; table 38 and the VP class remain excluded (7.9) | power cycle |
| W8 | PFE forwarding-engine base init (`... luofu_switch.pfe=1`) | is the missing forwarding-engine init the reason no frame crosses the switch - **implemented behind luofu_switch.pfe=1 (patch `036`); never executed. Round-3 evidence (image 75868fe0): W3-W7 armed, GEMAC registered, eth2 up 1 Gbps, br-lan forwarding, but no PIE RX and no wire egress; patch `036` adds the vendor PFE base init (pe/pfe_glb/opp direct words) and fixes the W7 tier-count report (7.10)** | power cycle |
| W9 | MAC maximum-frame register (`luofu_gmac.hwinit=1`) | is the MAC dropping both directions as over-max - **implemented behind `luofu_gmac.hwinit=1` (patch `038`); never executed. Round-4 evidence (image 6cb3d908): W8 verified but zero PIE RX and no wire egress; the vendor `hi_mac_hw_mtu@0x38c` writes reg 0x100 low14 = MTU+4 and the MAC counts rx/tx_overmax_pkt (7.11)** | power cycle |
| W10 | IFC default-classification table + L2/VLAN + CFE/rootif | D1c exit - IFC entries at ifc+0x50000 embed a software table-manager id (needs the allocator sequence decoded); then L2/VLAN tables, the CFE/rootif contract, the remaining MAC config words, NAPT/MCC, table 38/VP class, PIE queue handle (7.11) | power cycle |

The campaign must be re-planned after W1; the order above is a hypothesis, not a schedule.

### 12.4 Risks

| risk | severity | mitigation |
| --- | --- | --- |
| wrong queue/flow DDR base (`0x87b00000` vs `0x80000000`) corrupts DDR used by Linux | high | answer from live DT readback first (12.2 item 3); DT node disabled until then |
| LSW register writes hang the CPU/console path | high | write set only after W1/W2; attended; recovery armed; one write per power cycle if needed |
| wrong reset pulse kills the switch and needs power cycle | med | reset deassert last; test on stock first |
| unknown EQM command-word encoding | med | command word decoded 2026-11-19 (7.5); table *contents* and the queue->egress use remain [U], so do not write EQM tables until traced |
| probe configuration written but no EQM queue tables -> no forwarding, misleading "ready" | med | `luofu_switch_ready()` only means the reviewed write set (now including the W7 EQM tables) is verified, not that a frame flows; the attended note must check counters/traffic before treating a link as working (7.8/7.9) |
| W7 writes EQM tables whose field semantics are [U]; a wrong but self-consistent write passes readback | med | values are the vendor's own init writes (code shape + build-time tables), the snapshot restores on any readback mismatch, the indirect interface must be idle and every entry stable before the first write, and the block is powered down on refusal; table 38/VP class are excluded rather than guessed |
| hardware L2 defaults (unknown unicast) misforward in D1 | med | measure from W6; possibly keep bridge promisc until D2 |
| port-id mapping between MAC `port` and LSW ids unknown | med | read the vendor port/queue dumps (12.2) |
| vendor userspace expectations (`hi_cfm`, `/proc/hsan`) not met | low for D1 | our userspace is standard OpenWrt; document the divergence |
| GPL source not arriving | medium | D1 plan does not depend on it ([X] only for the fast path) |
| no bootable 6.18 target | high (schedule) | D0/D1a are host-only; bench gates wait for the target ([TARGET-DESIGN.md](TARGET-DESIGN.md)) |

---

## 13. Summary table and the [U] list

### 13.1 Summary (the format used in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 9)

| block | evidence | missing | plan | depends on | can do offline |
| --- | --- | --- | --- | --- | --- |
| **LSW register windows / ids** | [V] 256-slot `{base,iomem,size}` table (`g_modules`, 3072 B), accessors + bounds checks decoded; DP ids `0x40..0x44` and PFE ids `0,1,3,5,6,7,8,9,0xa,0xb` decoded from probe pointer tables; 7+10 live DT windows with names, and the **live DDR `queue`/`flow` base** measured 2026-11-18 (5.4) | [U] meaning of most offsets/fields; why ids 2/4 skipped; queue/flow *layout* inside the DDR regions | D1a binding + validate-only probe; D1b write set | 004 (CRG), PIE 015 | **Yes** (code/compile) |
| **Port model** | [V] MAC ports 8-12, live 9-12; board WAN=eth3/LAN=eth0-2; EPS port labels 3=prbs, 4-7=pie0-3; DFX names port0/8-13/xge/woe; 46x56 B port table with qid base + q_num=8; `port_bitmap` read as u64 from DT `<0xf01 0x1ff0>` and confirmed identical on the live node (2026-11-18), **decoded 2026-11-19 as the queue-allocation port bitmap {4..12, 32, 40..43}** (6.3); 5 virtual-port DT props (absent); patch `021` models the two DDR windows, the port_bitmap value check and per-port owner-device resolution (port netdevs stay with the GEMAC side) | [U] why the loader picks that port set; LSW internal port ids vs MAC port numbers; 40..47 class meaning (ports 40-43 observed populated in table B, 2026-11-18) | inventory 12.2; D1 bench W4/W5 | 007/008/009 | **Yes** (code); mapping bench-only |
| **Queue/qid model** | [V] per-port qid/q_num API and 8-QID loop; two qid tables `opp+0x42800`/`0x43800` with id-class index arithmetic and `0x100`-flag values, **runtime contents measured 2026-11-18** (table A ports 4-12 qids 1-72, table B ports 40-43 qids 73-104, 7.3); module 6 = opp; EPS channel map `eps+0xB00`/`0xB80` from `hi_pie_map_init`, PIE GRO/WOC getters, hwver branches; EQM command/data indirect engine (`eqm+0x48`/`+0x5c`, timeout/error bits) **command word decoded 2026-11-19** (7.5); **W2 model derived and validated read-only 2026-11-18** (patch `021`: the contiguous qid 1..104 allocation reproduces all 384 measured qid-map words; 7.7); R1/R3/R5 probe-configuration stage implemented behind `luofu_switch.egress=1` (patch `025`, 7.7/7.8) | [U] EQM queue-table contents/use for qid->egress; queue/flow DDR *layout*; PIE<->LSW binding timing; virtual-port defaults consumer; whether the writes have the intended effect (W4/W5); per-frame TX egress selection (7.8) | D1b write set; D1c host path | PIE 015; eqm traced values | **Yes** (code); writes bench-only |
| **MAC learning / FDB** | [V] hardware L2 window + counters (`port_move/age_drop/sport_clash/search_ok_mc/search_fail_mc`); CFE software FDB (`hi_cfe_tab_*`, `hi_cfe_intf_set_br_fdb/get_br_fdb`, `hi_br_fdb_check_addr` patched bridge); learning libs `hi_kcfe_lrn_l2` etc. call `hi_cfe_acc_*`/`hi_adp_flow_*`; no L2 table writer symbol | [U] L2 table format/access; whether FDB offload must go through flows; aging/port-move semantics | D2 | D1 | **Partly** (D2 design only) |
| **VLAN / ACL** | [V] tag action table (`tag_write/dump`, `hi_cfe_tag_*`), VLAN filter API + `vlan_filter` proc, subif model (with its "no second-layer untag subif" limitation); IFC classifier field list (~40 L2-L4 fields), action fields pq/pq_en/car/statis; CAR/mirror/NAPT/MCC symbols | [U] per-port VLAN/PVID table location and encoding; IFC row encoding; ACL semantics | D2 (VLAN/FDB), D3 (flow) | D1, GPL src optional | **No** (bench/RE needed) |
| **CFE/rootif contract** | [V] rootif = named netdev in 32-slot CFE table; NETDEV_UP/DOWN/UNREGISTER driven; interface attrs/FDB/hooks/tags/netdev vif; `g_intf_bypass=1` then `hi_swa` loaded ("switch adaptor for openwrt"); `hi_swa` switchdev handlers are stubs | [U] full CFE hook semantics; why bypass mode exists beyond the 5 bypass branches seen | **not reproduced**; standard Linux switchdev contract | - | **Yes** (design) |
| **switchdev design** | [V] no tag/CPU-port; per-port netdevs; vendor switchdev notifier registration; stubs prove no offload in this build; DSA rejected | [U] exact switchdev object mapping to tables (VLAN/FDB) | D2 | D1 | **Yes** (host) |

### 13.2 Unknowns that block or shape the plan

1. ~~`port_bitmap` words' semantics~~ **RESOLVED 2026-11-19 (host-only):** the u64 cell order decodes to
   the queue-allocation port bitmap {4..12, 32, 40..43}, which reproduces the measured qid map
   (6.3). The "root bit" (bit 16) interpretation remains [U]; the bit is clear on this board.
2. LSW internal port numbering vs MAC `port` numbers and the special port classes 40..47 and >47
   (6.2, 7.3).
3. ~~Whether the `queue`/`flow` windows are the DDR regions and their live base after the U-Boot
   overlay (5.4).~~ **RESOLVED 2026-11-18:** live base `queue = 0x80000000/4 MiB`,
   `flow = 0x80400000/1 MiB` after the overlay ([INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md)
   section 3.1). The layout inside the regions remains [U].
4. ~~EQM indirect table command-word encoding~~ **RESOLVED 2026-11-19 (host-only, code shape):**
   index bits 15..0, table id bits 26..20, bit30 write/read, bit31 start/busy, bit29 error (7.5).
   The EQM table *contents* needed for a working queue map remain [U].
5. Meaning of the DP probe's five virtual-port properties (absent on this board) and the
   cumulative/default-256 computation (6.5).
6. The exact reset sequence requirement (`dp_logic` vs `dpu_srst` mismatch; assert-vs-pulse) (5.5).
7. The EQM queue-table entry layout/contents (the 1000-cell write in `hi_queue_init`, table 19;
   tables 16/32/34/35/37/38) and their use for qid->egress selection (7.5, 7.8).
8. Where per-port VLAN membership/PVID is stored and its encoding (8.4).
9. The L2 hardware table access/format, and whether D2 FDB offload must go through IFC/flows (8.3).
10. ~~The 96-bit queue walk vs 64-bit DT property discrepancy~~ **RESOLVED 2026-11-19:** the walk
    only visits the low 64 bits and the u64 cell order makes the visited ids {4..12, 32, 40..43}
    (6.3); bits 64..95 of the walk are dead code for this DT value.
11. IFC classifier row encoding and action semantics (8.2).
12. Wi-Fi/WoE/NAPT virtual-port and offload semantics (out of scope, [X] for Wi-Fi).

### 13.3 External blockers ([X])

- **No bootable OpenWrt 6.18 target/image** on this bench: all hardware validation waits on
  [TARGET-DESIGN.md](TARGET-DESIGN.md)/[TARGET-SKELETON.md](TARGET-SKELETON.md) landing.
- **Wi-Fi port** ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.3): not part of the wired
  switch scope; its virtual-port/queue shapes are only referenced here.
- **GPL source** ([GPL-REQUEST.md](GPL-REQUEST.md) items 2-4): optional fast path, not a gate for
  the clean-room plan.
- **Vendor userspace compatibility**: we deliberately do not provide `hi_cfm`/`/proc/hsan`; that
  is a recorded design decision, not a missing feature ([I],
  [DEPLOYMENT-DECISION.md](DEPLOYMENT-DECISION.md)).

---

## 14. Evidence map and reproduction

| claim | artifact / command |
| --- | --- |
| module sizes, `depends=`, descriptions | `modinfo parent:rootfs-2.4.15/lib/hisilicon/ko/<mod>.ko`; `ls -la` |
| module symbol sets (defined/undefined) | `arm-none-linux-gnueabihf-nm [-u] <mod>.ko` |
| synthetic-address convention and bounds check | `hi_lsw_pub` `hi_lsw_reg_module@0x0`, `hi_lsw_read@0x2d4`, `hi_lsw_write@0x354`, `read_tab128@0x1fc`, `write_tab128@0x268` (disassembly) |
| DP module-id table (`0x40..0x44`) | `hi_lsw_dp` `.rodata+0x0c` (5 x `{id,str}` pairs, end `.rodata+0x34`), strings `dp/eps/eqm/fam/ips` |
| PFE module-id table (0,1,3,5,6,7,8,9,0xa,0xb) | `hi_lsw_pfe` `.rodata+0x48c` (10 x `{id,str}`, end `.rodata+0x4dc`), strings `ifc/l2/napt/mcc/opp/pa/pe/pfe_glb/prbs/pub` |
| DP probe order and properties | `hi_dp_probe@0x8b0` (strings `port_bitmap`, `hwver`, `cell-size`, `dp_logic`, `dpu_srst`, vport names) |
| 46-entry port table and qid API | `hi_lsw_dp` `.LANCHOR0` (section 28 `.data`, offset `0xd0`), `hi_queue_init@0x507c`, `hi_queue_get_qid@0x3648`, `hi_queue_get_q_num@0x36b4`, `hi_eqm_get_que_port@0x1ffc` |
| 8-QID loop and qid-map tables | `hi_lsw_pfe` `port_qid_map_set@0x26b8`, `qid_map_set@0x25dc` (constants `0x06042800`, `0x06043800`, `0x100`) |
| EPS channel map | `hi_lsw_dp` `hi_pie_map_init@0x56e8`, helpers `@0x5544`/`@0x5640` (base `(ch + 0x104002c0)*4` = `eps+0xB00+ch*4`, companion `+0x80`) |
| EQM indirect engine | `hi_lsw_dp` `hi_eqm_write_tab@0x1cdc`, `hi_eqm_read_tab@0x1e18`, `hi_eqm_tab_ready@0x1f5c` (`eqm+0x48`, `+0x5c`, 1001 retries, errors `0x8fffffd3`/`0x8fffffd8`) |
| table manager ids and shapes | `hi_lsw_pub` `hi_tab_mgr_create@0x1b58`, `g_tab_ctrl` (8192 B `.bss`), `read_tab128`; PFE `hi_pfe_ifc_init` (`hi_tab_mgr_create(0x700/0x701/0x705/0x70f)`), `hi_dp_tab_init` (`0x703`,`0x704`), `hi_pfe_napt_init` (`0x702`,`0x70d`), `hi_pfe_mcc_init` (`0x712`) |
| IFC key/action field names | `hi_lsw_pfe` `.rodata.str1.4` dump strings (`[key]:id=%u`, `[act]:id=%u`, `,igr_bitmap=0x%x`, `,pq=%u`, ... plus the field-name list in 8.2) |
| CFE interface/FDB/hook model | `hi_kcfe_pub` symbols `hi_cfe_add_rootif/del_rootif/add_subif/del_subif/set_subif_attr`, `hi_cfe_intf_set_br_fdb/get_br_fdb`, `hi_cfe_add_hook`, `hi_cfe_tag_*`; dump-format strings in 9.1 |
| learning stack | `hi_kcfe_lrn` exports/imports; `hi_kcfe_lrn_l2` imports `hi_cfe_acc_set_*`, `hi_adp_flow_*`; `hi_lsw_pfe` L2 counters |
| switchdev stubs | `hi_swa` `@0x474/0x47c/0x484/0x48c` = `mov r0,#0; bx lr`; notifier registration in `swa_register_notifier_blocks@0x598` |
| CFE bypass + load order | `parent:rootfs-2.4.15/etc/init.d/hsan_prepare` lines ~55-71; `parent:rootfs-2.4.15/etc/rc.d/luofu` |
| board port mapping | `parent:rootfs-2.4.15/etc/board.d/02_network`; [ETHERNET-BRINGUP-FRAMING.md](ETHERNET-BRINGUP-FRAMING.md) section 2 (measured vendor dmesg) |
| DT windows/clocks/resets/IRQs | [soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines 1078-1123, 885-955, 122-127; overlay note [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8 |
| live post-overlay windows + queue/flow base + port_bitmap (2026-11-18) | `parent:build/bench-inventory/20261118T130512Z-inv1-run/followup/{lsw_dp_reg.bin,lsw_dp_reg_names.txt,lsw_pfe_reg.bin,lsw_pfe_reg_names.txt,port_bitmap.bin}`; analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3 |
| qid table A/B runtime contents and D1b W1 baseline (2026-11-18) | `parent:build/bench-inventory/20261118T130512Z-inv1-run/inventory.json` entries group `lsw` (`opp+0x42800`/`0x43800`); analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) sections 3.2-3.3 |
| skeleton placeholders | [../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts) (`lsw_dp`/`lsw_pfe` disabled nodes) |
| PIE contract (cross-ref) | [PIE-DATAPATH.md](PIE-DATAPATH.md) sections 2, 7 |

Local RE artifacts (gitignored, regenerable with 2.3): parent
`build/linux-kbuild/waves/2026-11-18-lsw/{*.nm,*.dis,annotate.py,consts.py}`.

---

## 15. Open questions, corrections, and what would change this document

**Corrections/answers to earlier documents:**

1. [PIE-DATAPATH.md](PIE-DATAPATH.md) section 7.4 asked which DT window "module 6" is:
   **`opp` (`0x12400000`)**, with the qid tables at `+0x42800`/`+0x43800` (7.3). Also the
   exact `qid_map_set` formulas replace the earlier "port, 0, n, qid + q_num/8 * n" sketch: the
   code computes `8/q_num` and divides the slot index by it, which for `q_num=8` is `qid + i`.
2. [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 5.1 lists "8 QIDs/port" as speculation
   in the PIE doc; here it is [V] from three places (port table default `+12 = 8`, the loop
   count 8, and `qid_map_set`'s slot arithmetic).
3. The vendor switchdev path is **plumbing only** in this build (stub handlers, 4.7); the
   "vendor uses switchdev, not DSA" claim in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
   section 5.1 stands, but should not be read as "vendor offloads bridge objects today".
4. [TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.1's DSA-vs-switchdev question: **switchdev**
   (9.3), with the qualification that DSA remains theoretically possible only if a tag format
   is ever found (none today).

**Open questions** (each maps to an item in 13.2 and a bench step in 12.3): the EQM command
word; `port_bitmap` decoding; the internal port id mapping; where VLAN/PVID lives; L2 table
access; the virtual-port defaults; the `dpu_srst` mismatch; EQM table-19 layout; IFC row
encoding. **The queue/flow DDR base is resolved** (live `0x80000000`/`0x80400000`, 5.4) and the
D1b W1 baseline was measured on 2026-11-18 ([INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md));
what remains for D1b is W2 (the written write-set derivation) and the W3-W6 write experiments.

**What would change this document:** (a) the GPL source arriving - sections 5-8 become a port
check, and 13.2 shrinks; (b) a bootable 6.18 target - the bench campaign becomes possible and
several [U] items will be answered in days; (c) a bench trace showing a tag protocol or a
different CPU-port model - would reopen 9.3. Absent any of these, this document is the
strongest evidence-graded statement the repository can make about the internal LSW.

