# Wi-Fi framework port — Hi5622V100 host driver vs open HiSilicon WLAN framework

**Lane:** `wifi-framework-port` (host-only, read-only). **Written:** 2026-11-22.
**Builds on:** `wifi-source-hunt` (b0b330d8), `wifi-firmware-re`, `wifi-driver-re`,
`wifi-shim-port` (47f80e5). **No device contact; no vendor binary, firmware blob, INI or
per-unit value is committed.** All derived tables are plain text re-derivable from the named
local artifacts.

---

## 1. Question and answer

**Question:** the Hi5622V100 host driver (`hi5622v100_wifi.ko` 3,581,748 B + 
`hi5622v100_plat.ko` 371,708 B, unstripped, GPL-declared) is known to use the HiSilicon WLAN
SDK symbol families (`wal_cfg80211_*`, `hcc_*`, `hmac_*`, `oam_*`, `oal_*`). OpenHarmony
ships a driver tree for the sibling chip Hi3881 and unofficial mirrors ship more of the family.
**How much of the module pair is now covered by open source, and what exactly remains?**

**Answer:** 1,183 of 4,946 unique function symbols (23.9 %) are name-covered, weighted
**28.5 % of function-code bytes** (403,920 / 1,418,440 B). The framework skeleton compiles as a
real module against our 6.18.60 kernel. The remaining **3,767 functions (1,014,560 B)** are the
hi5622 chip delta: PCIe/BAL transport, hdpp data path, chip HAL and the ko-only hmac/wal/alg
work. The closed firmware makes the whole exercise research, not an upstream submission.

---

## 2. Trees fetched (kept in /home, never committed)

| tree | url | commit | license (verified) | role |
| --- | --- | --- | --- | --- |
| OpenHarmony device_soc_hisilicon | github.com/openharmony/device_soc_hisilicon | 7c1e2d5 | **GPL-2.0 for the driver** (see below) | rank-1 framework |
| themactep/wifi-hi3881 | github.com/themactep/wifi-hi3881 | 867943f | **no license grant** | rank-1 Linux port reference |
| 5-Super-Rookie-5/hongmengkernel (hi11xx/hi1105/wifi) | github.com/5-Super-Rookie-5/hongmengkernel | HEAD (2026-11-21) | **no license grant** (unofficial mirror) | rank-2 supplement |

Workdirs: `/home/shibbityshwab/wifi-framework-port-work/{hi3881-openharmony,wifi-hi3881,hi11xx-hongmeng}`
(sparse checkouts).

### License record (precise)

1. **OpenHarmony tree.** The repo-level `LICENSE` file explicitly carves out
   `./common/platform/wifi/hi3881v100` under **GPL-2.0** (section (2) of that file), while
   other directories are Apache-2.0. All 242 files of the hi3881v100 driver carry
   GPL-2.0-or-later file headers ("This program is free software … GNU General Public License …
   version 2 of the License, or (at your option) any later version", © HiSilicon). **The driver
   files are GPL-2.0 and usable for kernel code** — the Apache-2.0 concern does not apply to
   this subtree. (This matches the source-hunt lane's file-level verification.)
2. **themactep/wifi-hi3881.** No LICENSE file in the repo; GitHub API reports `license: null`.
   The driver files carry only "Copyright (c) Hisilicon Technologies Co., Ltd. 2019-2019. All
   rights reserved." with **no license grant**. Same code family whose GPL-2.0 origin lives in
   the OpenHarmony tree, but this published copy itself carries no grant → treat as
   reference-only; do not vendor its text into an upstream patch series.
3. **hongmengkernel mirror.** Unofficial mirror; 1,246 files under hi11xx carry "All rights
   reserved" vendor headers, 0 GPL/Apache headers. Reference-only for names/semantics
   (provenance caution, per source-hunt).

---

## 3. Symbol census — method and counts

**Method.** (a) `readelf -sW` on both .ko files, every `FUNC` symbol (LOCAL+GLOBAL; the
vendor build localizes all function bindings and exports via `__ksymtab` only). (b) A
signature scanner (strip comments → match `<ret> name(...)` → next line `{`) over every .c
file of the three trees. (c) Set intersection by name. (d) Byte coverage via `st_size` sums.
Scripts and intermediate lists: `/home/shibbityshwab/wifi-framework-port-work/census/`.

**Counts.**

| set | functions |
| --- | ---: |
| hi5622v100_wifi.ko FUNC | 4,245 |
| hi5622v100_plat.ko FUNC | 703 |
| .ko union (unique names) | 4,946 |
| hi3881 OH tree | 2,174 |
| themactep tree | 2,273 |
| hi1105 mirror | 9,106 |
| **shared by name: hi3881 trees** | **560 (11.3 % of .ko)** |
| **shared by name: all 3 trees** | **1,183 (23.9 % of .ko)** |
| **shared bytes (st_size)** | **403,920 B (28.5 % of 1,418,440 B function code)** |
| **hi5622 delta (ko-only)** | **3,767 functions / 1,014,560 B** |

Export surface (ksymtab strings): wifi.ko 778 names, plat.ko 299 names; 430 of the 1,077 exports are name-covered by the open trees (402 wifi + 28 plat).

**Structure of the module pair** (from the symbol tables): wifi.ko carries the wal/hmac/hdpp/
alg/shuangta/oal layers (4,245 fns, 1.30 MB of code), plat.ko the pcie/hcc/bal/oam/firmware
layer (703 fns, 118.7 KB of code). Both modules' defined FUNC symbols are LOCAL-bound; their
cross-module surface runs through `__ksymtab`/`__ksymtab_gpl` (the device loads them in
order hi_kwificlk → hi_pcie → plat → wifi; the import closure is documented in
`wifi-shim-port/analysis/import-closure.csv`).

### 3.1 Two independent cross-checks of the "same framework" claim

- **cfg80211_ops map:** 33 of the 39 populated callbacks from `wifi-driver-re` §2.2 exist
  with identical names in the hi3881 source (`wal_cfg80211_scan`, `wal_cfg80211_connect`,
  `wal_cfg80211_mgmt_tx`, …). Missing in hi3881: dump_survey, pmksa×3, get_channel,
  set_mac_acl, external_auth — the hi5622 extensions.
- **D2H event catalog:** 12 of the 44 decoded handlers (`wifi-driver-re` §3.6) exist in
  hi3881 source — the scan (`hmac_scan_proc_scanned_bss`, `hmac_scan_proc_scan_comp_event`),
  BA (`hmac_create_ba_event`, `hmac_del_ba_event`), connect-sync (`hmac_event_config_syn`,
  `hmac_syn_info_event`) core. Not in hi3881: ACS/DFS/CSI/spectral/TBTT/multiap/11v/pm/temp —
  the hi5622 extensions.

Both say the same thing: **the framework (wal_cfg80211 + mac/common + the core hmac scan/BA/11i
machinery) is shared; the hi5622 delta is concentrated in chip features and transport.**

### 3.2 Family table (all 3 trees; full CSVs in `census/`)

| family | .ko | src(all3) | shared | ko-only | % shared | classification |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| hmac_ | 2029 | 5380 | 526 | 1503 | 25.9 % | MAC/MLME — core shared, big chip delta |
| wal_ | 1014 | 1896 | 330 | 684 | 32.5 % | cfg80211 glue — surface shared |
| mac_ | 367 | 751 | 218 | 149 | **59.4 %** | shared MAC core (best coverage) |
| alg_ | 239 | 46 | 17 | 222 | 7.1 % | hi5622-specific algo/cali config |
| hdpp_ | 238 | 0 | 0 | 238 | 0 % | hi5622 data path (hi1105 names it dpe_) |
| oal_ | 211 | 204 | 41 | 170 | 19.4 % | OS layer — naming diverged |
| shuangta_ | 173 | 0 | 0 | 173 | 0 % | chip-codename layer, no source |
| oam_ | 109 | 20 | 10 | 99 | 9.2 % | logging/ops |
| pcie_ | 101 | 0 | 0 | 101 | 0 % | **hi5622 PCIe/ETE transport** |
| hal_ | 99 | 160 | 8 | 91 | 8.1 % | chip register HAL |
| hcc_ | 87 | 148 | 1 | 86 | 1.1 % | msg layer renamed hi3881→hi5622 |
| hwifi_ | 63 | 313 | 14 | 49 | 22.2 % | platform init (plat.ko side) |
| bal_ | 26 | 0 | 0 | 26 | 0 % | **hi5622 bus abstraction** |
| wlan_ | 20 | 173 | 8 | 12 | 40 % | power/link helpers |
| other | 170 | 1334 | 10 | 160 | 5.9 % | — |

**Requested classification** — `pcie_*` (101/101 ko-only), `bal_*` (26/26 ko-only),
`shuangta_*` (173/173), `hdpp_*` (238/238), `hal_*` (91/99), `alg_*` (222/239) are
**hi5622-specific** (no name match in any open tree). `smac_*` and `dmac_*` have **zero host
symbols** — both run on the firmware side of the bus (`smac_msg_proc_*` names live in
`FIRMWARE.bin` per wifi-firmware-re; the host-side dmac glue appears inside `hmac_dmac_*`/
`alg_*` names). `mac_*` is the opposite: 59.4 % shared — the mac/common core is the
framework, the remaining 149 are hi5622 additions.

---

## 4. Build milestone — the framework skeleton compiles on 6.18

`hi3881_fw_skel.ko` — 892,180 B, ELF32 ARM, `license=GPL`, `vermagic=6.18.60 SMP mod_unload
ARMv7 p2v8`, 28 objects / 307 text symbols — built with the OpenWrt arm toolchain against the
built kernel tree. Contents: libsec (11 files), frw (5), oal_mem, oal_util, oam_log, oam_main,
mac/common (9), module glue. Full fix log: `build/BUILD-NOTES.md` (13 classes).

Highlights of what the port hits (the 5.10→6.18 drift at source level):

1. **`oal_util.h` pins `LINUX_VERSION_CODE = kernel_version(3,10,0)`** — the vendor SDK's
   API-baseline hack. On 6.18 it silently selects every oldest branch (`wait_queue_t`,
   `init_timer`, 3-arg `alloc_netdev`, `mm_segment_t` paths…). Unpinning it to the real
   version flips the guards to the modern branches and exposed the genuine drift below.
2. `linux/version.h` deleted in 6.0 → use `generated/uapi/linux/version.h`.
3. y2038 cleanup: `struct timespec`/`struct timeval`/`do_gettimeofday` gone → `timespec64`
   + `ktime_get_real_ts64`; compat `struct timeval` for the dmac descriptor headers.
4. `set_fs()/get_fs()/KERNEL_DS` gone (5.10) → plain `kernel_write()`.
5. `del_timer* → timer_delete*` (6.11 rename, old names unexported).
6. `sched_setscheduler` unexported → `sched_setattr_nocheck`.
7. `netlink_kernel_cfg.cb_mutex` gone; `random_ether_addr` unexported → `eth_random_addr`.
8. OpenWrt-kernel quirk: stock `-isystem <gcc-include>` dropped → third-party `<stddef.h>`
   users break; and OpenWrt's `minmax.h` backport defines `MIN_T()` → vendor object-like
   `MIN_T` must yield under `__KERNEL__`.

**Not attempted in this lane** (documented as the next stage): wal/ (cfg80211 ops
renumbering — 33/39 callbacks exist in source, the mechanical rewrites are known), hcc/ (the
hi3881 hcc_host is SDIO/ADMA; hi5622 needs the PCIe mailbox/ETE swap), hmac full (the 1,503
ko-only functions have no source anywhere).

---

## 5. Milestone 4 — port plan for a complete hi5622 6.18 driver

### 5.1 Framework-from-source (which files)

From the **GPL-2.0 hi3881 tree** (OpenHarmony), vendorable for kernel code:

- `wal/` — wal_cfg80211.c, wal_scan.c, wal_ioctl.c, wal_hipriv.c, wal_main.c, wal_regdb.c,
  wal_11d.c, wal_event.c, wal_event_msg.c, wal_customize.c, wal_net.c, wal_linux_flowctl.c
  (33/39 ops callbacks + the scan/PMK/11d machinery are here).
- `mac/common/` — mac_data.c, mac_device.c, mac_frame.c, mac_ie.c, mac_user.c, mac_vap.c,
  mac_mib.c, mac_pm_driver.c, mac_resource.c, mac_regdomain.c (59.4 % shared).
- `mac/hmac/` — the 526 shared names: hmac_11i, hmac_scan, hmac_blockack, hmac_config (core),
  hmac_device, hmac_user, hmac_vap, hmac_mgmt_ap/sta/bss_comm, hmac_fsm, hmac_event (subset).
- `frw/`, `oal/` (mem/util/netbuf subset), `oam/`, `libsec/` — the skeleton this lane
  already compiles, plus the missing oal_netbuf/oal_net/oal_cfg80211 pieces.

From the **rank-2 hi1105 mirror** (reference-only — do not vendor its text), the *additional*
623 shared names supply: hmac cmd/connect/protocol/dfx files, wal extensions, oam_adapter,
hal_* accessor names, customize/, main/ scaffolding — i.e. the directories the hi3881 tree
lacks (per source-hunt). Use for structure/names; rewrite text or take it from the GPL-2.0
tree where the name also exists there.

### 5.2 The hi5622 delta (to be reverse-derived, not found anywhere public)

From our unstripped binaries + the decoded firmware contract (`wifi-driver-re` +
`wifi-firmware-re`): **3,767 functions / 1,014,560 B**, of which 445 are ≤16-byte stubs.

| delta family | functions | source of truth for the rewrite |
| --- | ---: | --- |
| pcie_ + bal_ + shuangta_ + hal_ | 101+26+173+91 | `wifi-driver-re` §3-4: mailbox 0x40039010/0x400392d4/0x40039014, ETE rings @0x4003a000, iATU map, doorbell sequence, `wifi-thermal-re` firmware-load + calibration container |
| hdpp_ | 238 | `wifi-driver-re` §7.1 data path; hi1105 dpe/ as semantic reference (names differ) |
| alg_ | 222 | `wifi-firmware-re` §5 item 9 (414-entry alg/cali table), cali .kv containers |
| hmac_ ko-only | 1,503 | MAC D2H catalog (32 ko-only handlers), scan/ACS/DFS/CSI/spectral event flows |
| wal_ ko-only | 684 | private ioctl surface (`wal_config_*/wal_ioctl_*/wal_hipriv_*`), vendor userspace ABI |
| oal_/oam_/hcc_/other | 170+99+86+160 | skeleton already compiles; extend per names |

**Transport swap note:** hi3881's hcc_host.c is SDIO+ADMA; the hi5622 host transport is
PCIe mailbox + ETE rings (`pcie_msg_*` in plat.ko). The port keeps the hcc *message-object*
concept (`hcc_msg_tx/hcc_msg_process`, plat.ko 0x11bf8/0x1204c per driver-re) but the
transport below it is rebuilt from the reversed register map, not from hi3881 code.

**Hi1152 fact** (source-hunt): Hi1152 is the Hi5622 sibling (acwifi.net/19630) — its datasheet
register set is the natural cross-check for the hal_ rewrite; no public hi1152 source either.

### 5.3 Estimated delta size and sequencing

- **Delta: ≈3,767 functions ≈ 1.0 MB of code**, concentrated in hmac_ (1,503) + wal_ (684) +
  hdpp_ (238) + alg_ (222) + transport (pcie_/bal_/shuangta_/hal_: 391).
- Sequencing: (1) skeleton (done) → (2) wal_cfg80211 surface + mac/common + shared hmac subset
  on the 6.18 tree (mechanical cfg80211 rewrites) → (3) PCIe/ETE transport from the reversed
  contract (new code) → (4) hdpp→rings data path → (5) chip hmac/wal features (largest block).
- After (2), the port would bring up wiphy registration + H2D framing without any vendor text;
  a working data path additionally needs (3)+(4), and AP/STA parity needs (5) plus the closed
  firmware's D2H behaviour — which prior lanes showed cannot be reached clean-room in a bounded
  effort (`wifi-firmware-re` §5, phase22-27/49 gate).

---

## 6. Milestone 5 — verdict

**23.9 % of the function inventory and 28.5 % of the function-code bytes of the 3.95 MB module
pair are now name-covered by open trees; the hi3881 GPL-2.0 framework skeleton compiles as a
module on our 6.18.60 kernel.** The coverage is *framework-shaped*: the wal_cfg80211 surface
(33/39 ops), the mac/common core (59.4 %) and the shared hmac scan/BA/11i machinery are source;
the chip-specific transport (pcie_/bal_/shuangta_/hal_ = 391 fns), the hdpp data path (238),
and the ko-only hmac/wal/alg feature work (≈2,409 fns) are original work.

**Remaining reverse-engineering effort:** ≈3,767 functions ≈ 1.0 MB of code, dominated by the
dual-band feature hmac/wal delta — on the order of the 10-16 engineer-week estimate the prior
lanes attached to a source-based 6.18 port, *if* it is done from the framework + the reversed
interface contract. The closed firmware (928,920 B, not reimplementable, D2H gate unresolved)
stays the hard ceiling: even a perfect host port is **upstream-impossible** and incomplete
without it. **The vendor GPL source request (`wifi-driver-re` §7.2 symbol list) remains the
only fast path; this lane's census is the cross-check list to grade whatever source arrives**
— any tree that does not contain the pcie_/bal_/shuangta_/hdpp_ families and ~3.7k ko-only
functions is a framework copy, not the hi5622 SDK.

---

## 7. Evidence ledger

| claim | artifact / command | grade |
| --- | --- | --- |
| symbol counts / exports | `readelf -sW` on the two .ko; pyelftools ksymtab dump (`census/ko_symbols.py`, `ksymtab_dump.py`) | [V] |
| source function counts | `census/src_functions3.py` over the three /home trees | [V] |
| shared/ko-only/family tables | `census/census3.py`, `census/census-families3.csv` | [V] |
| byte coverage 28.5 % | `census/sizecov.py` (st_size sums) | [V] |
| 33/39 ops + 12/44 events shared | name intersection vs `wifi-driver-re` §2.2/§3.6 | [V] |
| skeleton .ko | `build/BUILD-NOTES.md` + local `hi3881_fw_skel.ko` (not committed) | [V] |
| licenses | repo LICENSE text, per-file headers, GitHub API | [V] |

**Redaction:** no vendor .ko, firmware blob, INI, calibration .kv, MAC/serial or per-unit value
is committed; the census CSVs contain only function names (derived data). Third-party source
trees live only in /home workdirs; only URLs + hashes are recorded here.
