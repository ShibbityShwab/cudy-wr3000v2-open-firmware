# Documentation hunt: what exists and what is still missing (updated 2026-11-18)

The rule this session follows: research before declaring something unknown. The first version of
this page (2026-10-01) surveyed what is publicly available for the Hi5671Y SoC and the Hi5622V100
radio. This refresh (2026-11-18) keeps that survey and adds the **documentation gaps the
2026-11-17/18 bench work actually hit**, marks the questions the same work answered, and gives
each open gap a priority, a status and a way to close it.

**Publication status of this repository: NOT READY.** See `../README.md`, `../NOTICE.md` and
`PUBLISH-READINESS.md` (the history purge of ten vendor `.o` files is still pending).
Bench state is **not** recorded here; the live state is the parent repository's `STATUS.md` and
the newest dated entry in its `mem-entries.md`. Claims below cite the parent ledger
(`mem-entries.md`), the parent `EVIDENCE-INDEX.md`, or files in this published half.

Priority scale: **P0** blocks the next bench/port step; **P1** blocks the upstream target; **P2**
hardware understanding only; **P3** no public source exists and none is expected.

---

## 1. Status at a glance

| # | question / document gap | priority | status (2026-11-18) | evidence / where it was answered or is tracked |
| --- | --- | --- | --- | --- |
| 1 | What triggers the `failed to add rootif` path, and where it lives | P0 | **RESOLVED** (2026-11-17 18:55) - userspace writes `/proc/hsan/net/swa/wan_port`; `hi_swa_add_rootif` -> `hi_cfe_add_rootif` -> `hi_net_get_port` fails `0x90000000` | parent `mem-entries.md` 2026-11-17 18:55 |
| 2 | Vendor module dependency graph / load order on our kernel | P0 | **RESOLVED** (2026-11-17 17:45) - `ksecurec` -> `hi_basic` -> `hi_kpie` -> lsw/kcfe/khslink/swa; all 19 init-preloaded modules load `ret=0` | parent `mem-entries.md` 2026-11-17 10:15 and 17:45; `VENDOR-STACK-ON-MAINLINE.md` |
| 3 | Vendor `net_device` layout / WEXT shift | P0 | **RESOLVED** (v13) - vendor has `CONFIG_WIRELESS_EXT=y`; our `netdev_ops`/`flags`/`priv_flags` were shifted by +8 without it | parent `mem-entries.md` 2026-11-17 18:55 |
| 4 | Why the 3dh-class kernel never booted | P0 | **RESOLVED** (2026-11-17 08:45) - `luofu_early()` called `ioremap()` before `mm_init`; with `CONFIG_SLAB`+`CONFIG_TRACING` the kmalloc path NULL-derefs | parent `mem-entries.md` 2026-11-17 08:05/08:45; parent `EVIDENCE-INDEX.md` blocker row |
| 5 | Post-procd RCU stall | P0 | **RESOLVED** (2026-11-17 10:15) - it is a TRACING-block effect; the 3dp/3dq configs ran without it | parent `mem-entries.md` 2026-11-17 10:15 |
| 6 | Vendor `sk_buff` layout and private fields | P0 | **OPEN** - offsets measured, definition missing | section 4.1; parent `mem-entries.md` 2026-11-17 20:40 |
| 7 | `br_*` hook pointer semantics, call sites and the vendor bridge patches | P0 | **PARTIAL** - the pointers are exported variables, not functions; the consumer (patched bridge) is missing | section 4.2; parent `mem-entries.md` 2026-11-17 20:15/20:40, 2026-11-18 06:50 |
| 8 | `hi_kpie` `g_api_priv` / `hi_net_dev_probe` port-registration contract | P0 | **PARTIAL** - entry point and failure mode known; descriptor layout and port/vport numbering unknown | section 4.3; parent `mem-entries.md` 2026-11-17 18:55 |
| 9 | `hi_cfe_add_rootif` / interface tables and the hook at `dev+0x21c` | P1 | **PARTIAL** - call chain known; table and hook structures unknown | section 4.4; parent `mem-entries.md` 2026-11-17 18:55 |
| 10 | Device-tree bindings for the `hsan,*` nodes | P1 | **OPEN** - vendor tree dumped as evidence; only one binding doc exists (CRG); upstream skeleton is not buildable | section 4.5; `soc/vendor-dt-notes.md`; `soc/luofu-r116.dts` |
| 11 | DT properties the `hi_*` modules consume | P1 | **PARTIAL** - node shapes known; per-driver property contract unknown; one probe crash traced to a missing reset | section 4.6; parent `mem-entries.md` 2026-11-17 18:00 |
| 12 | Wi-Fi driver source and firmware redistribution terms | P0 (Wi-Fi decision only) | **OPEN / vendor-held** | section 4.7; `PORT-SURFACE.md` |
| 13 | U-Boot FIP/`bootfip` tooling, 40,732-byte trailer generator, recovery/failover logic | P1 | **OPEN / vendor-held**; cause of the `bootfip` data abort understood | section 4.8; parent `HARDWARE-IDENTITY.md`; `ROADMAP-OPENWRT.md` phase 5 |
| 14 | GPL source request: sent copy, receipt, reply | P0 process gap | **OPEN** - request sent 2026-10-01, no reply or evidence copy held | `GPL-REQUEST.md` (tracking table + escalation ladder) |
| 15 | FCC internal photos / RF front-end part numbers | P2 | **OPEN** - not retrieved; `fcc.report` bot-gated, `fccid.io` reachable 2026-11-18 | section 2 |
| 16 | Hi5671Y / Hi5622V100 datasheets, register maps | P3 | **NOT PUBLIC** (NDA) | section 2 |

Items 1-5 are answered. Do not re-open them as questions; the remaining work is named in the
sections below.

---

## 2. What is publicly available (2026-10-01 survey; FCC and vendor-page rows re-checked 2026-11-18)

| Source | Status |
| --- | --- |
| Hi5671 / Hi5671Y / Hi5671YV200 datasheet or product brief | **Not public.** HiSilicon ships these under NDA. What circulates is third-party synthesis (carrier product pages, teardown summaries) with no register-level content. |
| Hi5622 / Hi5622V100 datasheet | **Not public**, same situation. The community material is a class description (AX3000, 2x2+2x2, pairs with Hi5671-class SoCs). |
| PCI IDs | `59e7:0005` is not in the public PCI database; the subsystem id decodes as "Huawei Technologies" (`19e5`). No vendor datasheet follows from the id. |
| FCC filings | Cudy's grantee id is `2APRG`. Re-checked 2026-11-18: <https://fccid.io/2APRG> returns HTTP 200 and lists Shenzhen Cudy Technology Co., Ltd. (7F, Lepu Tower (West), 66 Xingke Rd, Nanshan, Shenzhen, 518055 China) with Cudy applications including `2APRGWR3000SE` and `2APRGWR3000H`; this page does **not** establish a WR3000-v2-specific FCC ID. `fcc.report` is bot-gated to plain fetchers (HTTP 403 on 2026-11-18). The internal-photos PDFs have **not** been retrieved; they would show chip markings and the RF front-end parts. |
| HiSilicon's own public trees | `hisilicon/linux-hisi` exists but targets camera/STB SoCs. No router SoC, no `luofu`, no `hi5671`. |
| Community open-SDK work | `OpenIPC/openhisilicon` ("Opensource Hisilicon SoCs SDK") is the closest precedent: a reverse-engineered open SDK for **camera** SoCs, built over years by a community. It does not cover router SoCs, and its interfaces are far better documented than this radio's. |
| Search for `hi5671y` / `luofu` / router SoC ports | Nothing public. The top hit for "hisilicon luofu" is this project's own repository. |
| Sibling chips | The Hi1152 (HiSilicon Wi-Fi 6, same class) has the same verdict in community threads: no open driver, no mainline support. |
| Vendor GPL/support pages | `https://www.cudy.com/pages/gpl` returns HTTP 404 (verified-by-web 2026-11-18; also recorded in parent `HARDWARE-IDENTITY.md`). The Download Center (`https://www.cudy.com/pages/download-center`) returns HTTP 200 but is script-rendered; a WR3000 v2.0 GPL archive is not confirmed there - **NEEDS-VERIFICATION** by a browser session. Contact details and the GPL request status are in `GPL-REQUEST.md`. |

**What that means, stated plainly.** The documents that would make this "fast and mistake-free"
do not exist publicly: the SoC register map, the radio firmware interface, and - what this
refresh adds - the **vendor kernel's own interface contracts** (the `sk_buff` layout, the bridge
hooks, the CFE/HSLINK port tables). Those three interface contracts are not in any datasheet; they are in
the vendor's Linux tree, which is why the GPL source request (section 4.9) is the highest-leverage
input. The public precedents show the shape of the work and its cost: a router SoC plus a closed
Wi-Fi radio is strictly harder than the camera-SoC SDK effort.

---

## 3. What the 2026-11-17/18 bench work answered (do not re-open)

- **Rootif trigger path.** The vendor userspace writes `/proc/hsan/net/swa/wan_port`
  (`hsan_appm:130`, the hotplug/wandetect path); the kernel side is
  `swa_dfx_update_wanif` -> `hi_swa_add_rootif` -> `hi_cfe_add_rootif` (in `hi_kcfe_pub`),
  which calls `hi_net_get_port(dev,&ports,2)` and fails `0x90000000` when the netdev is not in
  `hi_kpie`'s `g_api_priv` table. What remains open is the registration contract, section 4.3.
  Evidence: parent `mem-entries.md` 2026-11-17 18:55.
- **Module dependency graph and load order.** `ksecurec` first (it exports the `memcpy_s` family
  that `hi_basic` imports), then `hi_basic` (itself the `hi_dfx_*` provider), `hi_kpie`, then
  the switch/CFE/HSLINK/SWA chain; all 19 init-preloaded modules load `ret=0` and `eth2` comes
  up. Evidence: parent `mem-entries.md` 2026-11-17 10:15 and 17:45; `VENDOR-STACK-ON-MAINLINE.md`
  (load-order section).
- **Vendor `net_device` layout.** The +8 shift was `CONFIG_WIRELESS_EXT`: with it enabled,
  `netdev_ops` 0x128, `flags` 0x138, `priv_flags` 0x13c match the vendor modules. Evidence:
  parent `mem-entries.md` 2026-11-17 18:55.
- **3dh-class no-boot root cause.** `luofu_early()` called `ioremap()` before `mm_init`; with
  `CONFIG_SLAB` + `CONFIG_TRACING` that compiles to a kmalloc-cache path whose caches do not
  exist yet. The fix (move the work to an `early_initcall`) made 3dn boot. Evidence: parent
  `mem-entries.md` 2026-11-17 08:05/08:45.
- **Post-procd RCU stall.** A TRACING-block effect, not a generic kernel problem: the no-TRACING
  builds (3dp/3dq) run past that point. Evidence: parent `mem-entries.md` 2026-11-17 10:15.
- **The HYFI crash is a `sk_buff` ABI mismatch, not the bridge hooks.** `hyfi_netlink_receive`
  did `skb_clone()`, read `[clone+0x118]` (vendor `skb->data`) and stored through it; our
  `sk_buff` is smaller and has `data` at 0xa0. The four `br_*` symbols are exported pointer
  variables, not functions. Evidence: parent `mem-entries.md` 2026-11-17 20:15/20:40.
- **U-Boot `bootfip` data abort was caused by our DTS node shape.** A simplified vendor-MAC node
  made U-Boot's own DT fixups abort; reverting the DTS removed it. This is why the vendor DTS
  sources are requested, section 4.8. Evidence: parent `mem-entries.md` 2026-11-17 18:55.

---

## 4. Open documentation gaps (detail)

### 4.1 Vendor `struct sk_buff` layout and private fields - P0, OPEN

**Known (measured from disassembly and live traces):** the vendor struct differs from ours -
`len` 0xc8 vs 0x54, `data_len` 0xcc vs 0x58, headers 0x106/0x108/0x10a vs 0x8e/0x90/0x92,
tail/end/head 0x10c/0x110/0x114 vs 0x94/0x98/0x9c, `data` 0x118 vs 0xa0. That is +0x74 from
`len`/`data_len` and +0x78 from the headers on, with one 4-byte vendor field between
`data_len` and the bitfields. Anchors: `hi_hslink_tx_urarp` (skb_reserve),
`hyfi_bridge_get_dst` (EtherType from head+mac_header), `mc_find_acl_rule` (len-data_len).
Evidence: parent `mem-entries.md` 2026-11-17 20:40; parent `EVIDENCE-INDEX.md` bench line.

**Missing:** the vendor `struct sk_buff` definition itself, its private/tail fields, what
`skb->cb` is used for by the vendor modules, and which fields the bridge/HYFI path reads beyond
the ones already measured. Offsets can be recovered offline; **field semantics and the struct
definition come from the vendor kernel source** (GPL request item 4).

**Why it blocks:** `hyfi_bridging` cannot run on the vendor `sk_buff` ABI; v17 removed it from
the preload to keep the bench stack alive, and the vendor bridge/HYFI userspace contract is still
unsatisfied (br-lan created/destroyed in HSLINK cycles). This is the largest single unknown in
the bench line.

### 4.2 `br_*` hook pointer semantics, call sites and the vendor bridge patches - P0, PARTIAL

**Known:** `br_get_dst_hook`, `br_multicast_handle_hook`, `br_notify_hook` and
`br_port_dev_get_hook` are exported **pointer variables** in the vendor bridge, not functions.
`hyfi_bridging` only stores callbacks into them (`bridge_get_dst`, `port_dev_get`,
`br_notify`, `mc_process`); the code that would call them is in the vendor's patched
`net/bridge`, which is not in our tree. A v16 fire with no-op function stubs was ABI-wrong (a
store would overwrite the stub's first instruction); v17 replaced them with writable zeroed
exported pointer variables, which load but have no consumer. `hyfi` also needs the
`net_bridge` cache at `netdev_priv+0x10`, a vendor-only bridge field. Evidence: parent
`mem-entries.md` 2026-11-17 20:15/20:40; 2026-11-18 06:50; parent `EVIDENCE-INDEX.md` bench line.

**Missing:** the vendor `net/bridge` patch series (call sites, ordering, locking, what state
the hook is expected to install), the `net_bridge` private-field layout, and the sign of the
contract: what `hyfi` expects the bridge to have set up before its netlink receive runs.

**Why it blocks:** with the patches absent, "real" hook semantics cannot be reconstructed offline
from our tree; no-op or writable-pointer stubs both leave the userspace bridge contract
unsatisfied. This is why GPL request item 3 asks for the patch series by name.

### 4.3 `hi_kpie` `g_api_priv` / `hi_net_dev_probe` port-registration contract - P0, PARTIAL

**Known:** `hi_cfe_add_rootif` calls `hi_net_get_port(dev,&ports,2)` and fails `0x90000000`
when the netdev is not present in `hi_kpie`'s `g_api_priv` table. That table is filled only by
`hi_net_dev_probe(dev,&desc8)` (exported by `hi_kpie`); stock `hi_kmac` calls it at probe with
`desc = {port, vport, ...}` taken from DT (ports 8..12). After the lookup succeeds,
`hi_cfe` writes a CFE hook into `dev+0x21c`. Evidence: parent `mem-entries.md`
2026-11-17 18:55.

**Missing:** the `desc8` descriptor layout and field semantics, the `g_api_priv` table size and
indexing, the port/vport numbering, whether registration requires a vendor-type netdev, and the
expected CFE hook contract at `dev+0x21c`. Closing this is what would let either the vendor MAC
driver or our lab GEMAC satisfy HSLINK's rootif check.

### 4.4 `hi_cfe_add_rootif` / interface tables - P1, PARTIAL

**Known:** `hi_swa` imports `hi_cfe_flush_by_mac`, `hi_net_get_port` and `hi_cfe_add_subif`
from the CFE modules (see `VENDOR-STACK-ON-MAINLINE.md`), and the rootif path lands in
`hi_cfe_add_rootif` (section 4.3). **Missing:** the CFE interface table structures (rootif and
subif bookkeeping), what `hi_cfe_add_subif` expects, and how the hook at `dev+0x21c` is
invoked on the datapath. Same source request as section 4.3 (items 2-3).

### 4.5 Device-tree bindings for the `hsan,*` nodes - P1, OPEN

**Known:** the vendor device trees are fully dumped and re-rendered as evidence in
`soc/vendor-dt-notes.md` and `soc/luofu-r116-pinned.dts`: base DTB 31,220 bytes (sha256
`947ec62d...`) plus two board overlays (4,592 and 4,920 bytes), the `hsan,*` compatible
census (clk/CRG, pinctrl, FMC, mac, mdio, dp/pfe, lsw_woe, pie, pcie, watchdog, tvsensor, sysenv,
sysctrl, rstinfo, efuse, flashinfo, rng, ledpwm, pmu-pwm, net-cdev, woe_*, clk-corrector), and
the note that U-Boot applies an overlay selected by `board_id` and injects `ubootver-offset`
before handing the tree to Linux.

**Missing:** upstream bindings for every `hsan,*` compatible (only
`../lab/luofu-clk/bindings/hisilicon,luofu-crg.md` exists), node shapes that both the vendor
modules and U-Boot fixups accept, and a single-source DTS. The current
`soc/luofu-r116.dts` and `../lab/luofu-kernel/dts/luofu-r116.dts` are stage-1 skeletons: the
header of the former says "Not yet buildable: no dt-bindings/ headers exist". The published half
must not absorb the extracted vendor tree itself; the host-side evidence is parent-repo-local.

**Why it blocks:** the OpenWrt target needs a submittable DTS; the vendor modules need the
`hsan,*` node properties; U-Boot fixups crash on node shapes they do not expect (section 4.8).

### 4.6 DT properties consumed by the `hi_*` modules - P1, PARTIAL

**Known:** from the dumped vendor DT the nodes carry `port = <8..12>` on the gemac nodes,
`phy-mode`/`phy-handle`, mdio/PHY child nodes, reset and clock references, iATU tables on PCIe,
and ring sizes on `pie`. The v9 fire showed `hi_kmdio`'s probe crashing on a `reset_control`
lookup against our DT - i.e. the modules read specific properties and fail hard when a node is
missing or shaped differently. Evidence: `soc/vendor-dt-notes.md`; parent `mem-entries.md`
2026-11-17 18:00.

**Missing:** the per-driver property contract - which properties each `hi_*` driver reads,
which are required vs optional, and any vendor-private encoding. The complete answer is in the
module sources (GPL request item 2); a per-module `of_property_read_*` RE is the offline
alternative.

### 4.7 Wi-Fi firmware / source licensing - P0 for the Wi-Fi decision, OPEN / vendor-held

**Known:** `hi5622v100_wifi.ko` (3,564,728 bytes, `vermagic=5.10.201`, `license=GPL`) is not a
mac80211 driver: 15 cfg80211 symbols, 0 `ieee80211_` symbols, 4,330 vendor strings in the
`wal_`/`oal_`/`hcc_`/`bal_`/`hmac_`/`dmac_` namespaces, and **211 imports through the vendor
`HW_RTOS_NS` namespace**. The on-chip firmware is `/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 bytes, md5
`0e530b976d5a20e87358671f1a577695`) with two configuration files beside it. Evidence:
`PORT-SURFACE.md`; `DRIVER-BLACKBOX.md`.

**Missing:** the driver sources, the provider of the `HW_RTOS_NS` symbols, and a written
license/redistribution statement for the firmware blob. Until that arrives, the port is blocked at
link time without the vendor tree, and the upstream Wi-Fi decision (M4 in
`ROADMAP-OPENWRT.md`) stays open. Tracked by `GPL-REQUEST.md` items 7-8.

### 4.8 U-Boot FIP/`bootfip` tooling and the trailer - P1, OPEN / vendor-held

**Known:** U-Boot 2022.07 for luofu; `bootcmd=mtd read kernel${bootflag} ${loadaddr}; bootfip
${loadaddr}`; the loader accepts only FIP packages; our slice is a uImage plus the fixed
40,732-byte FIP/DTBO trailer; there are two U-Boot slots (uboota/ubootb) and a failover path; the
trailer is vendor-derived and cannot currently be regenerated from published material. U-Boot is
also what applies the board DT overlay and performs the fixups that crashed on our simplified
nodes. Evidence: parent `HARDWARE-IDENTITY.md` (U-Boot section); `ROADMAP-OPENWRT.md` phase 5;
`PUBLISH-READINESS.md` reproducibility item; parent `mem-entries.md` 2026-11-17 18:55.

**Missing:** U-Boot source, board configuration, the FIP/trailer packaging tool, env defaults and
the overlay/fixup code. The public TR6560 BSP (`BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP`, cited in
parent `HARDWARE-IDENTITY.md`) is a reference for shared U-Boot behaviour only - it is not this
board's source and has not been used to regenerate the trailer. Tracked by `GPL-REQUEST.md`
item 6.

### 4.9 The vendor source request itself - P0 process gap

The request that would supply sections 4.1-4.4 and 4.7-4.8 at once was sent 2026-10-01 and remains
unanswered; the repository holds no sent copy or receipt. The status block, tracking table,
ready-to-send follow-up and escalation ladder are in `GPL-REQUEST.md`. This page does not
repeat them; treat that file as the single source for the request.

### 4.10 FCC internal photos and RF front-end part numbers - P2, OPEN

Retrieving the FCC internal photos (a browser session; `fccid.io` is reachable, `fcc.report`
is bot-gated) would confirm the RF front-end part numbers. That does not unlock a driver, but it
closes the last open hardware-identification questions. Low priority: it does not block the bench
line, the bridge port, or the OpenWrt target work.

---

## 5. The one action that would change the calculus

**The vendor's answer is still the single highest-leverage input.** One reply can supply the
bridge patch series, the vendor `sk_buff` definition, the `hi_kpie` port-registration contract,
the CFE tables, the device trees and the U-Boot/FIP tooling - the six things sections 4.1-4.6 and
4.8 otherwise have to be recovered module by module. The follow-up and escalation ladder are
written and waiting in `GPL-REQUEST.md`; they need a human to send.

**Second, offline:** reverse the port-registration contract (sections 4.3-4.4) from the modules
already on record. That is the next design input for the bench line, and it needs no fire. Do not
re-open the questions in section 3; they are answered.

**Third, low priority:** the FCC photos (section 4.10), only to close hardware identification.

---

## 6. How to check the claims in this file

- Answered questions and offsets: parent `mem-entries.md` entries 2026-11-17 08:05, 08:45, 10:15,
  17:45, 18:00, 18:55, 20:15, 20:40 and 2026-11-18 06:50; durable summary in parent
  `EVIDENCE-INDEX.md` ("Bench kernel line, 2026-11-17/18").
- Vendor DT evidence: `soc/vendor-dt-notes.md`, `soc/luofu-r116-pinned.dts`; baseline artifacts
  are local-only (parent `build/tmp/dt-spec/`), so re-derive from a dump before relying on them.
- Wi-Fi surface numbers: `PORT-SURFACE.md`; load order and imports: `VENDOR-STACK-ON-MAINLINE.md`.
- Web checks: <https://www.cudy.com/pages/contact> (contact details, HTTP 200 on 2026-11-18),
  <https://fccid.io/2APRG> (grantee record, HTTP 200 on 2026-11-18),
  <https://www.cudy.com/pages/gpl> (HTTP 404 on 2026-11-18). These are living pages; re-check
  before citing them in a letter.
