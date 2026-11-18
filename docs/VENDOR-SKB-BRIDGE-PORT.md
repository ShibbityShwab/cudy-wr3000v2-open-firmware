# Vendor sk_buff + bridge/HYFI port: design spec for the optional hybrid lane

> **Status 2026-11-18: design/feasibility spec, work not started, bench fires stopped.**
> This is a design document for the **optional bench-reversal / hybrid lane** ("our kernel +
> the vendor userspace"), not an upstream deliverable and not a release plan. The goal-level
> plan is [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md) and [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md);
> the loader recipe it would extend is [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md);
> the cost measurement behind the vendor-source argument is [PORT-SURFACE.md](PORT-SURFACE.md).
> Nothing in this file authorises a fire. The parent `STATUS.md` section 3 and the ledger entry
> 2026-11-18 06:50 stop bench fires; a new fire also needs the parent `tools/fire-preflight.sh`
> PASS, a READY recovery door, a read-only UART capture and a human at the bench.
>
> **Publication status: NOT READY.** The published half still needs the history purge described in
> [PUBLISH-READINESS.md](PUBLISH-READINESS.md) and the parent `EVIDENCE-INDEX.md` "publish boundary"
> section. This document contains no vendor binaries and no extracted blob content; module names,
> symbol names and numeric offsets are reverse-engineering findings of the kind already published
> in the phase reports. The vendor `.ko` files themselves must stay local.

**Citation convention.** `parent:` marks a file in the private research repository (not in the
published half). A plain path such as `opensource/docs/...` is in this repository. "Ledger" means
`parent:mem-entries.md`; the entries this spec rests on are 2026-11-14 22:41, 2026-11-17 18:00,
18:55, 20:15, 20:40 and 2026-11-18 06:50. Console captures and module fixtures under `parent:build/`
are local-only evidence; `parent:EVIDENCE-INDEX.md` states for each whether a fresh clone can
regenerate it.

## 0. TL;DR

1. **v17 is the current ceiling and it reached it without crashing.** With `hyfi-bridging` not
   preloaded, the slice boots, all 19 preloaded vendor modules load, the lab GMAC brings `eth2`
   up at 1 Gbps, `br-lan` is created - and then HSLINK destroys every interface in a loop at
   t = 33-35 s and the console goes silent at ~54 s. No oops, no panic, no network (ledger
   2026-11-18 06:50).
2. **The next layer is two ABI contracts our kernel does not satisfy.** `hyfi_bridging.ko` was
   built against the vendor kernel's `struct sk_buff` (its `->data` is at **0x118**, ours at
   **0xa0**) and against the vendor's patched bridge (four exported **pointer variables** plus a
   `net_bridge` cache at `netdev_priv + 0x10`). The RE verdict is ledger 2026-11-17 20:40.
3. **Options.** A: keep the current no-hyfi state and document why LAN still fails. B1: adapt
   `struct sk_buff` to the measured vendor offsets under a config switch, with a build-time
   offset probe (recommended *if* the lane is resumed). B2: patch the vendor module's offsets
   (rejected - it breaks a kernel-wide ABI contract, is unmaintainable and does not supply the
   missing bridge patches).
4. **B1 alone is necessary but not sufficient.** Even with the sk_buff layout fixed, the four
   bridge hook call sites and the `net_bridge` cache are vendor bridge-patch code we do not
   have. Recovering them is a **vendor-source dependency** (GPL request sent 2026-10-01, no reply
   held per [GPL-REQUEST.md](GPL-REQUEST.md)), not a kernel-config change.
5. **The 2026-11-14 counter-example matters.** A vendor stack *without hyfi* reached
   `eth2` / `br-lan` 192.168.10.1 / ping 3/3 on our kernel using the vendor MAC modules
   (ledger 2026-11-14 22:41, EVIDENCE-INDEX G4). So "hyfi is missing" is not by itself a complete
   explanation of the v17 failure; the v17 lane also replaced the vendor MAC netdevs with the lab
   GMAC and bypassed the WAN rootif. Section 2.4 and open question Q1 keep this honest.

## 1. Scope, terms, non-goals

| term | meaning in this document |
| --- | --- |
| our kernel | the vanilla 5.10.201 ARM tree in `parent:tools/lab-src` / `parent:build/linux-kbuild/linux-5.10.201`, not the vendor kernel |
| vendor stack | the ~59 patched `hi_*.ko` modules from the stock rootfs, loaded on our kernel as in [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) |
| HYFI / `hyfi_bridging` | the vendor kernel module `hyfi-bridging.ko` plus the vendor userspace `hyfibrd`; it owns the accelerated/offloaded bridge state and talks to userspace over a vendor-private netlink protocol |
| HSLINK | the vendor's `hi_khslink` / `hi_swa` interface-management layer; its console tag is `[HSLINK]` |
| RE | reverse engineering by disassembly of the vendor's GPL-declared binary modules (licence evidence: EVIDENCE-INDEX) |
| v17 | the bench slice `luofu-slice-phase3dq.bin` built 2026-11-17 20:34, 2,906,100 B, md5 `8342ccb9c0167766529277719330f43c` (zImage `a08ffc66...`, init `74033a38...`, DTB `336ba39e...`); final bench state before fires were stopped |
| hook variable | one of `br_get_dst_hook`, `br_multicast_handle_hook`, `br_notify_hook`, `br_port_dev_get_hook`: exported pointer variables, not functions |

**Non-goals.** No clean-room drivers, no Wi-Fi data path, no upstream artifact, no release image,
no change to the publish boundary. This document does not design the OpenWrt target/DTS/image
recipe (that is [TARGET-DESIGN.md](TARGET-DESIGN.md)'s scope) or the GPL request follow-up
([GPL-REQUEST.md](GPL-REQUEST.md)).

## 2. What v17 established (the ceiling)

### 2.1 The fire, as recorded

From ledger 2026-11-18 06:50 ("v17 fire: no crash with hyfi removed, but br-lan is created/destroyed
in HSLINK cycles and userspace goes silent"):

- v17 = no `hyfi-bridging` preload; the four `br_*` symbols are writable exported **pointer
  variables**; bridge + bridge-netfilter + tracepoints + WEXT ABI are in the config; patched
  `hsan_appm` / `wandetect` / `hotplug` scripts redirect the WAN-rootif write; 59 patched
  modules staged.
- U-Boot booted the slice normally (no data abort), **all 19 modules returned `ret=0`**,
  `S20network` OK, lab GMAC `eth2` up 1 Gbps (ledger 23250-23251).
- **No oops/panic** (the v16 hyfi crash is gone), but `br-lan` is created and immediately
  destroyed in a repeating HSLINK loop (`swa_netdevice_event` + `[HSLINK]dev(br-lan) is destroy`
  at t = 34-35 s) and the console goes silent at ~54 s. No network, box unreachable.
- Conclusion recorded: the remaining gap is vendor-bridge/HYFI userspace integration; hyfi needs
  the vendor `sk_buff` ABI (+0x74/+0x78) and vendor bridge patches (hook-pointer call sites,
  `net_bridge` cache at `netdev_priv+0x10`) that our tree does not contain. "That is a
  multi-step port, not a single fire." Bench fires stopped.

### 2.2 Console corroboration (local capture, not a fresh-clone receipt)

`parent:build/uart-logs/20261117T233458Z-uart-v17.log` (local-only; cited in parent
`EVIDENCE-INDEX.md` section "Bench kernel line, 2026-11-17/18"). Boot 1 of that capture:

| t (s) | event |
| ---: | --- |
| - | U-Boot reads our uImage: `Data Size: 2865304 Bytes = 2.7 MiB` (line 241); kernel banner `5.10.201 ... #261 SMP Tue Nov 17 20:34:15 +07 2026` (line 247) |
| - | built-in cmdline includes `panic=5 ... panic_on_oops=1 luofu_pet_beats=30` (line 288) |
| 8.745617 | `luofu-gmac 14480000.ethernet: Link is Up - 1Gbps/Full - flow control rx/tx` (line 1188) |
| 11.289-11.299 | `/vendor/upper/etc/rc.d/S18hyfibrd OK`, `S20network OK`, `S95done OK` (lines 1225-1227) |
| 11.361-12.039 | `load <module> fd=0 ret=0` for the 19 modules, `ksecurec` first, `hi_swa` last (lines 1233-1334) |
| 11.956 | `[HSLINK]HSLINK install successfully ...!` (line 1321) |
| 33.298-33.510 | `[HSLINK]dev(eth3/eth2/eth1/eth0) is destroy` (lines 1991-2004) |
| 33.704 | first `[HSLINK]dev(br-lan) is destroy` (line 2010) |
| 34.019 / 35.364 | `Could not find PHY for device 'radio0'` / `'radio1'` (lines 2013, 2037) |
| 34.17-35.59 | six more `[HSLINK]dev(br-lan) is destroy` (lines 2017-2045) |
| 44.511 | `done (3432): drop_caches: 3` (line 2046) |
| 53.826-53.865 | five `nf_nat_*: module PLT section(s) missing` lines; last console text before silence (lines 2047-2051) |
| 155.039 | `luofu: pet limit reached; selected stock A, watchdog will reset` (line 2052) |

After that, U-Boot reports `Hard Power-on reset for internal POR circuit timing`, prints
`bootflag b bootreg 0`, re-initialises the selector (`bootreg init, set 11`), and boots our
slice a second time (line 2109 is the second `Data Size: 2865304`). The second boot repeats the
same pattern and ends with `Hard Hardware watchdog reset` -> `bootflag b bootreg 10` ->
`boot from kernelb failed, change to kernela` -> stock kernel (line 3981: `Data Size: 4294096
Bytes = 4.1 MiB`, then `Uncompressing Linux...`).

### 2.3 Why LAN still fails at v17

The ledger's explanation (2026-11-18 06:50, lines 23255-23257) is the operative one: the vendor
HYFI/bridge userspace contract is missing. Concretely, three layers are absent or bypassed:

1. **HYFI is not loaded.** `hyfi-bridging.ko` must not run on the vendor `sk_buff` ABI (v16
   oops, section 2.5), so v17 removed it from the preload list. The vendor userspace
   (`/usr/sbin/hyfibrd`) therefore has no kernel counterpart for its netlink protocol.
2. **HSLINK owns interface lifecycle and tears the current netdevs down.** Under v17 the
   interface list is destroyed repeatedly (HSLINK destroys `eth3/eth2/eth1/eth0` at
   t=33.3-33.5 s and then `br-lan` six more times between t=34.17 and t=35.59 s), so the created
   `br-lan` never becomes the stable bridge the network config expects, and the Wi-Fi PHY lookup
   fails.
3. **The WAN rootif bookkeeping is bypassed in userland.** v14-v17 avoid the
   `/proc/hsan/net/swa/wan_port` -> `hi_cfe_add_rootif` -> `hi_net_get_port` path by patching
   the three userspace writers (ledger 18:55, 20:15); the switch's rootif state is therefore not
   established. That was declared a LAN-only workaround for the lab boot, not a fix.

What still works in this state (Option A, section 4.1): boot chain, module load, lab GMAC link,
bridge create, vendor rc.d semantics up to `S95done`, console capture, and the watchdog
pet-limit fallback to stock.

### 2.4 The 2026-11-14 counter-example (what "no hyfi" alone does and does not prove)

The record contains a working router state *without* `hyfi-bridging` in the preload list:
2026-11-14 22:41 (ledger 22088-22110) - the init loaded the 22-module vendor dependency chain
(`hi_basic ... hi_kmdio -> hi_kphy -> hi_kmac -> ... -> hi_swa`), and on our kernel:

- `Hi-GEMAC 14480000.gemac eth2` bound with PHY `14000000.mdio0:04` (vendor `hi_kphy`),
- `br-lan` came up at 192.168.10.1/24 with members `eth0 vap0 eth1 vap3 vap8 eth2 vap11`,
- ping 192.168.10.1 from the host: 3/3, 0% loss.

The later lane that ends at v17 changed two things relative to that state: the **vendor MAC
netdevs were replaced by the lab GMAC** (`CONFIG_LUOFU_GMAC/MDIO/PHY` back on, vendor
`hi_kmdio/hi_kphy/hi_kmac` not preloaded) because the vendor-DT lane failed (v9: `hi_mdio_probe`
reset-control crash and `hi_kmac` `-2`; v13: adding `hsan,himac`/`hsan,mdio` nodes to our DTB
made U-Boot's bootfip fixups data-abort - ledger 18:00/18:55), and **the WAN rootif writes were
masked in userspace**. So the honest statement is: the ledger's stated cause (HYFI must run to
keep the vendor bridge state) is the cause recorded for the *current lane*, but the record does
not prove a no-hyfi stack cannot reach LAN; the 2026-11-14 evidence shows it did when the vendor
MAC drivers owned the netdevs. Which delivery difference is decisive is **open question Q1**;
it matters before spending a single fire on the sk_buff port.

### 2.5 The v16 crash this port would have to survive

Local capture `parent:build/uart-logs/20261117T124213Z-uart-recovA.log` (v16 boot, local-only):

| t (s) | event |
| ---: | --- |
| 33-35 | same HSLINK teardown pattern as v17 |
| 35.332845 | `Unable to handle kernel NULL pointer dereference at virtual address 00000028` |
| 35.347244 | `Internal error: Oops: 805 [#1] SMP ARM` |
| 35.352014 | module list includes `hyfi_bridging(O)` |
| 35.396319 | `PC is at hyfi_netlink_receive+0x40/0xdac [hyfi_bridging]` |
| 35.449106 | `Process hyctl (pid: 1808)` |

then `panic_on_oops=1` panics and the box reboots into the same slot (ledger 2026-11-17 20:15:
bootreg 11). The RE verdict (ledger 20:40): in `hyfi_netlink_receive` the module does
`skb_clone()`, loads `[clone+0x118]` (vendor `skb->data`) and stores to `[r6+0x40]`; in our
struct `sk_buff` `->data` is at 0xa0 and the struct is only 0xb0 bytes, so the load is
out of bounds. Spot-checking the local module fixture (not committed) confirms the instruction:
`hyfi_netlink_receive+0x28` is `ldr r6, [r5, #280] ; 0x118`, followed by byte stores at
`[r6+0x28..0x2b]` and a `dev_get_by_name` call whose netdev is then offset by `#1344 ; 0x540`
(netdev private area) before `hyfi_bridge_get` is called.

## 3. The vendor ABI the current kernel does not satisfy

### 3.1 `struct sk_buff`

Measured from the vendor modules by the 2026-11-17 20:40 RE (ledger 23220-23227); "ours" is the
current lab 5.10.201 build (`sizeof = 0xb0`). All values are byte offsets within the object.

| field | ours | vendor | delta |
| --- | ---: | ---: | ---: |
| `len` | **0x54** | **0xc8** | +0x74 |
| `data_len` | **0x58** | **0xcc** | +0x74 |
| `transport_header` | **0x8e** | **0x106** | +0x78 |
| `network_header` | **0x90** | **0x108** | +0x78 |
| `mac_header` | **0x92** | **0x10a** | +0x78 |
| `tail` | **0x94** | **0x10c** | +0x78 |
| `end` | **0x98** | **0x110** | +0x78 |
| `head` | **0x9c** | **0x114** | +0x78 |
| `data` | **0xa0** | **0x118** | +0x78 |
| `sizeof(struct sk_buff)` | **0xb0** | **0x128** (derived, **UNVERIFIED**) | +0x78 (derived) |

The two deltas differ by 4: everything from the header fields onward is shifted **+0x78**, while
`len`/`data_len` are shifted **+0x74**. The ledger therefore records "an extra 4-byte vendor
field between `data_len` and the bitfields" (i.e. between 0xcc and the header block). The
composition of the +0x74 block before `len` is **not** recorded by the RE; only its size is.

**Measuring anchors** (ledger 23226-23227; the first three are the RE's, the parenthesised
corroborations are local disassembly spot-checks of the patched module fixtures, 2026-11-18, not
committed):

| anchor | module / function | what it proves |
| --- | --- | --- |
| `hi_hslink_tx_urarp` (skb_reserve-style update) | `hi_khslink.ko` | reads `head` 0x114 and `data` 0x118, writes `network_header` 0x108 and `mac_header` 0x10a (local spot-check: `ldr r3,[r4,#0x118]`, `ldr r0,[r4,#0x114]`, `movw r1,#0x10a`, `strh r3,[r4,#0x108/0x10a]`) |
| `hyfi_bridge_get_dst` (EtherType from `head + mac_header`) | `hyfi-bridging.ko` | reads `len` 0xc8, `head` 0x114, `mac_header` 0x10a (local spot-check: `ldr r3,[r0,#0xc8]`; `movw r3,#0x10a`; `ldr r7,[r0,#0x114]`) |
| `mc_find_acl_rule` (`len - data_len`) | `hyfi-bridging.ko` | reads `len` 0xc8 and `data_len` 0xcc |
| `hyfi_netlink_receive` | `hyfi-bridging.ko` | `skb_clone()` then `[clone+0x118]` = vendor `data`; also `dev + 0x540` = netdev private area |
| `hi_hslink_get_skbmacheader` | `hi_khslink.ko` | reads `mac_header` 0x10a and `head` 0x114, returns `head + mac_header` (local spot-check) |
| `hi_hslink_monitor` | `hi_khslink.ko` | reads a **16-bit vendor-private field at 0xb4** in the same object whose `mac_header` (0x10a) and `head` (0x114) it also reads (local spot-check) |

**Observed vendor-private skb bytes.** The 2026-11-17 RE session reported vendor-private skb
bytes at **0x51 / 0x53 / 0xb4 / 0xb8** and the `hi_cfe_intf_rx` consumer as the expected
next faults after a layout fix. Status of those four numbers:

- **0xb4 is corroborated**: `hi_khslink.ko`'s `hi_hslink_monitor` loads a u16 from `[skb+0xb4]`
  next to its 0x10a/0x114 skb reads (local spot-check).
- **0x51 / 0x53 / 0xb8 and the `hi_cfe_intf_rx` behaviour are UNVERIFIED here**: no committed
  artifact records them; the RE's disassembly notes were not committed (EVIDENCE-INDEX notes the
  RE reports lived in subagent sessions). `hi_cfe_intf_rx` does exist: it is a symbol in
  `hi_kcfe_pub.ko` (local symbol-table check), alongside `hi_cfe_add_rootif`/`hi_cfe_add_subif`.
  Treat 0x51/0x53/0xb8 and the `hi_cfe_intf_rx` expectations as **TODO: commit the RE notes**.

**Layout model.** The measured table is exactly reproduced by two insertions relative to the
current struct in `parent:tools/lab-src/include/linux/skbuff.h:716-931`:

1. an opaque **0x74-byte vendor block inserted after `cb`** (at 0x48): this keeps
   `next/prev/dev/sk/tstamp/cb` at their current offsets (dev 0x8, cb 0x18), moves
   `_skb_refdst`/`destructor`/`_nfct` to 0xbc/0xc0/0xc4, puts `len` at 0xc8 and
   **contains all four private-byte candidates** (0x51, 0x53, 0xb4, 0xb8);
2. a **4-byte vendor field after `data_len`** (0xd0), which shifts `mac_len/hdr_len/`headers to
   0x106/0x108/0x10a and `tail/end/head/data` to 0x10c/0x110/0x114/0x118; with
   `CONFIG_SKB_EXTENSIONS=y` the resulting size is 0x128.

This is a *model that reproduces the anchors*, not a decoded vendor struct: the 0x74 block could
equally be spread config divergence in the vendor's build. The exact placement of the 4-byte field
inside the post-`data_len` span is not pinned by the anchors (the header offsets are). The model
assumes the v17 config, which has `CONFIG_NF_CONNTRACK=y`; without `_nfct` the pre-header block is
4 bytes shorter and `len` would land at 0xc4, so the guard must be built from the same config.
Both are open question Q2; an offset probe validates the arithmetic but not the vendor field
semantics.

**Why it crashes today.** All 19 modules loaded in v17 were built for these offsets. The ones that
touch an skb in the loaded path do OOB accesses against our 0xb0 struct: `hi_hslink_tx_urarp`
writes 16 bits at +0x108/+0x10a and reads +0x114/+0x118; `hyfi_netlink_receive` reads +0x118
and writes into that value as a pointer. The v17 box did not oops because the affected transmit
paths were not reached before HSLINK tore the interfaces down - **the current "known-good without
hyfi" stack is ABI-dirty, not ABI-clean**; that is an argument against treating Option A as a
stable research platform for anything that exercises the data path.

### 3.2 `struct net_device` / WEXT

The v13 fix enabled `CONFIG_WIRELESS_EXT` under `COMPILE_TEST` so our `struct net_device`
matches the vendor layout (parent `tools/lab-src/net/wireless/Kconfig:2-7`, comment: "the
vendor modules were built against a kernel WITH wireless extensions (their net_device expects
wireless_handlers/wireless_data before netdev_ops: an 8-byte shift)"). Measured state after v13
(ledger 23150-23157, 23233):

| field | ours after WEXT | vendor | notes |
| --- | ---: | ---: | --- |
| `netdev_ops` | **0x128** | 0x128 | was +0x120 before WEXT |
| `flags` | **0x138** | 0x138 | |
| `priv_flags` | **0x13c** | 0x13c | |
| `ALIGN(sizeof(struct net_device), 32)` = `netdev_priv` base | **0x540** | 0x540 | vendor `mac80211.ko` uses `add r4,r5,#0x540`; measured on fw 2.5.24, see [phase14/wifidrv0.md](phase14/wifidrv0.md) |

The measurement method and the limits are committed in [phase14/abi-match.md](phase14/abi-match.md)
and [phase14/wifidrv0.md](phase14/wifidrv0.md): offsets were read from the vendor's own
`mac80211.ko`/`cfg80211.ko` instructions, not guessed. **Only these four offsets are proven.**
`hi_net_get_port`/`hi_cfe` also assume a vendor net_device layout (ledger 18:55: "hi_cfe then
writes a CFE hook into dev+0x21c"); `hyfi_netlink_receive` additionally touches
`dev+0x30c` (our local spot-check: per-CPU decrement idiom after `mrc` CPU-id read). Field-by-field
equality of the whole 0x540 struct is **UNVERIFIED** and is open question Q4.

**What hyfi expects beyond the layout:** it computes `netdev_priv = dev + 0x540` and then expects
a **`net_bridge` cache pointer at `priv + 0x10`**, populated by the vendor's bridge patches
(ledger 23234, 23256-23257). Our tree does not populate anything of the sort. Note the trap: in
upstream 5.10 `struct net_bridge` the field at +0x10 happens to be `dev` (parent
`net/bridge/br_private.h` in the kernel tree: `struct net_bridge { spinlock_t lock; spinlock_t
hash_lock; struct list_head port_list; struct net_device *dev; ... }`), so a nonzero read at
`priv+0x10` is **not** evidence of a correct cache. Do not accept "it read a plausible pointer"
as validation.

### 3.3 The four `br_*` exported symbols

These are **pointer variables**, not functions. The 2026-11-17 RE established that
`hyfi_bridging.ko` only **stores callbacks** into them, with no calls and no reads (ledger
23228-23232). Our tree now exports writable, zeroed pointers (parent
`tools/lab-src/arch/arm/mach-luofu/luofu.c:1014-1028`; the comment records exactly this:

    /* VENDOR BRIDGE-HOOK VARIABLES (2026-11-17). These four symbols are exported *pointer
     * variables* in the vendor's patched bridge, not functions: the vendor modules only store
     * callbacks into them ... Our previous function stubs were ABI-wrong and a store would have
     * overwritten their first instruction word. ... (The vendor bridge call sites that would
     * consume them are not in our tree.) */

| exported variable | callback stored by hyfi | symbol checked in the local fixture |
| --- | --- | --- |
| `br_get_dst_hook` | `hyfi_bridge_get_dst` | `hyfi-bridging.ko` has `hyfi_bridge_get_dst` (local symbol table) |
| `br_port_dev_get_hook` | `hyfi_bridge_port_dev_get` | `hyfi-bridging.ko` has `hyfi_bridge_port_dev_get` |
| `br_notify_hook` | `hyfi_br_notify` | `hyfi-bridging.ko` has `hyfi_br_notify` |
| `br_multicast_handle_hook` | `mc_process` | `hyfi-bridging.ko` has `mc_process` |

Zero-valued pointers are safe *for the store*: hyfi's init only writes them. But the **consumer**
side is the vendor's patched bridge, and those four call sites are not in our tree (the four symbols
are referenced nowhere in `parent:tools/lab-src/net/bridge/`; they exist only in `luofu.c`).
The v15 experiment proved no-op function stubs were not enough; v17 proved that writable pointer
variables plus no hyfi avoid a crash, not that the hooks work.

### 3.4 The WAN rootif context (currently bypassed)

The "failed to add rootif" path is a userspace -> kernel -> switch registration chain (ledger 18:55;
local symbol-table spot-checks confirm module ownership):

| step | module | evidence |
| --- | --- | --- |
| vendor userspace writes `/proc/hsan/net/swa/wan_port` (`hsan_appm:130`, `wandetect`, hotplug `08-wan-detect`) | userspace | ledger 23144-23146 |
| `swa_dfx_update_wanif` -> `hi_swa_add_rootif` | `hi_swa.ko` | ledger; `hi_swa.ko` defines both symbols (local symbol table) |
| `hi_cfe_add_rootif` | `hi_kcfe_pub.ko` (imported by `hi_swa.ko`) | ledger; local symbol table: defined in `hi_kcfe_pub.ko` |
| `hi_net_get_port(dev, &ports, 2)` fails `0x90000000` unless the netdev is in `hi_kpie`'s `g_api_priv` table | `hi_kpie.ko` | ledger 23146-23150; `hi_kpie.ko` defines `hi_net_get_port`/`hi_net_dev_probe` locally |
| table is only filled by `hi_net_dev_probe(dev, &desc8)` with `desc = {port, vport, ...}` from DT (ports 8..12) | `hi_kmac.ko` imports it; defined in `hi_kpie.ko` | ledger 23148-23149; local symbol table: `hi_net_dev_probe` is a defined `F` symbol in `hi_kpie.ko` and `*UND*` in `hi_kmac.ko` |
| `hi_cfe` then writes a CFE hook into `dev+0x21c` and assumes the vendor net_device layout | vendor kernel | ledger 23149-23152 |

The current v14-v17 lane **bypasses** this: the three userspace writers are patched to redirect
the `/proc/` write (build recipe `parent:build/linux-kbuild/build-3dq.sh:82-85`; init-side
bind masks `parent:tools/lab-src/kbuild/init-vendorroot.c:69-81,128-142`, applied at line 632).
Ledger 2026-11-17 20:15 records that the pre-pivot bind-mount variant failed and persistent
overlay copies were used instead; **which mechanism actually redirected the write in the v17 fire
is not settled by committed artifacts** (the v17 capture shows no `rootif` failure, so something
did work). TODO: record the v17 mechanism as a receipt before reusing it as an experimental
control (open question Q6).

## 4. Options

### 4.1 Option A - status quo (no hyfi)

**What it is.** The v17 configuration as fired: our kernel + our DTB + lab GMAC/MDIO/PHY, 19
preloaded vendor modules (no `hyfi-bridging`), bridge core + bridge-netfilter + tracepoints +
WEXT ABI, four zeroed exported hook pointers, patched rootif userspace, transient/persistent test
masks. No bench fires currently (ledger 2026-11-18 06:50).

**What works (evidence in section 2.2):** U-Boot boots the slice (Data Size 2,865,304 B, no
`Uncompressing`); our kernel banner; 19 modules `ret=0`; lab GMAC `eth2` up 1 Gbps;
vendor rc.d shadows run to `S95done`; bridge created; HSLINK installs; console capture works;
panic/oops absent; watchdog pet-limit/stock fallback eventually returns to stock.

**What fails:** HSLINK destroys `eth0-eth3` and `br-lan` in a loop at t=33-35 s; the Wi-Fi
radios are not found ("Could not find PHY for device 'radio0'/'radio1'"); the console goes
silent at ~54 s; no LAN, no SSH on our kernel; every fire costs the operator's time and can still
need a physical RESET+TFTP.

**Why LAN fails:** section 2.3 - missing HYFI kernel counterpart, HSLINK lifecycle rejection of
the lab-GMAC netdevs, and the bypassed rootif bookkeeping. Note section 2.4: the 2026-11-14
vendor-MAC lane reached LAN *without* hyfi, so "no hyfi" is not a complete explanation of the v17
failure; the MAC-ownership/delivery difference is unresolved (Q1).

**Cost/decision:** zero new work; the state is already documented; it is a stable *boot* platform
for non-data-path instrumentation but not a working router and not ABI-clean for the loaded
vendor modules (section 3.1, "why it crashes today"). Keep Option A until someone commits to the
port or recovers the 2026-11-14 lane.

### 4.2 Option B1 - adapt `struct sk_buff` to the measured vendor layout

Recommended *if* the hybrid lane is resumed, and only as a build-time-guarded experiment: it is
the one part of the problem we can reconstruct from the vendor binaries we have.

#### 4.2.1 Design sketch (code-level, not a patch)

Put the layout behind a new config symbol, e.g. `CONFIG_LUOFU_VENDOR_SKB_ABI`, so the default
lab kernel never changes for non-hybrid work. Inside the conditional in
`include/linux/skbuff.h`:

1. Keep everything through `cb[48]` unchanged (dev 0x8, cb 0x18 - the usercopy whitelist offset
   `offsetof(cb)` and `sizeof_field(cb)` in `skb_init()` then stay automatically correct).
2. Insert an opaque `u8 vendor_skb_priv[0x74]` block after `cb`; document known bytes
   (0x51/0x53/0xb4/0xb8 candidates) in a comment and zero-initialise. Name it "opaque" in code:
   it is not decoded vendor fields, it is the space the vendor struct has. Bytes our kernel never
   writes stay zero unless a deliberate seed is added.
3. Keep `_skb_refdst`/`destructor`/`_nfct`, then `len, data_len`; add a `u32
   vendor_field_post_data_len` immediately after `data_len`; then `mac_len, hdr_len` and the
   rest unchanged. The measured anchors then land at 0xc8/0xcc/0x106/0x108/0x10a/0x10c/0x110/
   0x114/0x118 and (with `CONFIG_SKB_EXTENSIONS=y`) `sizeof` is 0x128.
4. Decide and document `skb_clone`/copy semantics for the new bytes. `__copy_skb_header()`
   (parent `net/core/skbuff.c:960-1006`) copies `cb`, `_skb_refdst`, nfct and the
   `headers_start..headers_end` span by name/offsetof; the opaque pre-`len` block is **not**
   copied and the post-`data_len` vendor word is outside the header span. If the vendor expects
   these to survive a clone, add explicit copies; currently UNVERIFIED (Q2).
5. Leave `headers_start/headers_end` and the existing `CHECK_SKB_FIELD` `BUILD_BUG_ON`s
   intact - they enforce the copy-range invariants. Do not put kernel semantics into the vendor
   block; it must stay opt-in and zeroed by default.
6. Keep `memset(skb, 0, offsetof(struct sk_buff, tail))` (`net/core/skbuff.c:225,320`) as the
   allocator zeroing path: with `tail` at 0x10c it now zeroes the whole vendor area, good.

#### 4.2.2 Impact analysis

- **Size and memory.** Struct 0xb0 -> 0x128: +0x78 = **+120 B per sk_buff shell (+68 %)**.
  `SKB_TRUESIZE(X)` includes `SKB_DATA_ALIGN(sizeof(struct sk_buff))`
  (`parent:include/linux/skbuff.h:237-239`); this kernel has `CONFIG_ARM_L1_CACHE_SHIFT=6`
  (64-byte cache line), so the aligned shell goes 192 -> 320 B and **`skb->truesize` grows by
  128 B for every skb**. Scenario arithmetic: 4,096 in-flight skbs ~ +512 KiB; 65,536 ~ +8 MiB.
  `skbuff_head_cache` is `SLAB_HWCACHE_ALIGN` with `sizeof(struct sk_buff)` object size and an
  `skbuff_fclone_cache` of `sizeof(struct sk_buff_fclones)` (2 shells + refcount), so the fclone
  cache grows by roughly 2x the shell delta (allocation code: `net/core/skbuff.c:4331-4346`).
  Exact current object sizes can be read from `/proc/slabinfo` on the lab kernel
  (`skbuff_head_cache`, `skbuff_fclone_cache`) before/after; that is a cheap validation and is
  **TODO** (not measured here).
- **Blast radius.** One central struct and one cache; kernel code uses member names, so the build
  is consistent by construction. No ARM assembly hardcodes `sk_buff` offsets (`grep -rn sk_buff`
  over the full `parent:build/linux-kbuild/linux-5.10.201/arch/arm` tree returns no match), so the
  usual assembly-ABI trap does not apply here.
- **Vendor-module correctness.** This is the win: every module built against the vendor ABI (all
  59 staged ones) starts seeing the struct it was compiled for, including the ones that
  demonstrably use sk_buff offsets (`hi_khslink` reads 0x114/0x118 and writes 0x108/0x10a). But
  it also means **any path that was silently not touching an skb now starts to**, so new faults
  can appear where v17 was quiet. Stage 1 (section 7) tests exactly that.
- **Networking stack behaviour.** `skb->truesize` growth changes socket buffer accounting
  (rmem/wmem limits trigger earlier); not functional for a bench, but it is measurable and should
  be noted in the fire readout. Cache-line alignment/performance effects are minor here.
- **No net_device change.** WEXT stays as v13 configured; the net_device delta is out of B1's
  scope (Q4).
- **Publish boundary.** This is a kernel patch under GPL-2.0 in `lab/` - publishable once the
  history purge is done; it contains no vendor binary content. The offsets themselves are
  findings and may be published (they already are in the parent docs).

#### 4.2.3 Validation plan (build + probe, no fire)

1. Implement the conditional layout and the `BUILD_BUG_ON` guard (section 6.3).
2. Build both configs and record: default config offsets unchanged (0x54/.../0xa0/0xb0); vendor-ABI
   config offsets equal the table (0xc8/0xcc/0x106/.../0x118/0x128).
3. Build and load the offset probe module on the lab kernel; it must print the expected table
   (section 6). A mismatch is a build bug, not a fire question.
4. Optional: build a `CONFIG_DEBUG_INFO=y` vmlinux once and compare `pahole -C sk_buff` /
   `gdb ptype /o` against the probe. Current vmlinux has no DWARF/BTF
   (`readelf -S vmlinux` shows no `.debug*/.BTF`; `.config` has no `CONFIG_DEBUG_INFO`), so
   this needs a throwaway debug build - host-only, no fire.
5. Only then schedule a fire (section 7, stage 1) as a single-variable check that the layout
   change alone does not regress the v17 baseline.

#### 4.2.4 Expected follow-on faults (stop conditions, not fixes)

These are what the RE lane expects *after* the `sk_buff` offsets are right (ledger 23234,
23255-23257 + RE-session notes). They are out of B1's scope; each is a stop-and-RE decision:

1. **Vendor-private bytes 0x51/0x53/0xb4/0xb8 are zero/meaningless.** They are in-bounds after
   B1 but carry none of the vendor's semantics. A reader like `hi_hslink_monitor` (u16 at 0xb4)
   or a hyfi aggregate path can compute a wild length/index from 0. Expected symptom: no crash
   (in-bounds) but wrong behaviour, or a downstream fault when the value is used as a pointer.
2. **`hi_cfe_intf_rx` (in `hi_kcfe_pub.ko`) expectations.** The RE session flagged this as the
   next consumer; its contract is not in any committed artifact (TODO: commit the notes).
3. **The missing bridge call sites.** `hyfi_bridge_get_dst` etc. are stored but never called by
   our bridge, so offload/acceleration paths stay dead; worse, hyfi's netlink handler may
   dereference state those callbacks were supposed to create (the v16 reading of the first crash
   was exactly this before the sk_buff verdict). Expected symptom: oops in a netfilter/bridge
   callback after hyfi starts receiving.
4. **`net_bridge` cache at `netdev_priv+0x10`.** Not populated correctly (our +0x10 is `dev`);
   a nonzero read can still be a *wrong* pointer. If hyfi dereferences it, the fault address will
   look plausible - do not "fix" it by writing a plausible pointer without the vendor's
   definition.
5. **Remaining net_device mismatches.** hyfi touches `dev+0x30c`; the rootif path assumes
   `dev+0x21c` (section 3.2). Only four net_device offsets are proven.

Every one of these needs either the vendor GPL bridge/kernel source or a new disassembly report,
not a config flag. That is why B1 alone cannot reach the LAN acceptance test.

### 4.3 Option B2 - patch the vendor module's offsets (not recommended)

The alternative is to leave our `struct sk_buff` at 0xb0 and rewrite `hyfi_bridging.ko` (and
any other module in the path) to use 0x54/0x58/0x8e/... instead of 0xc8/0xcc/0x106/.... Rejected:

1. **It fixes one module, not the contract.** The ABI is shared by every vendor networking module
   (`hi_swa`, `hi_khslink`, `hi_kcfe_*`, hyfi, ...) *and* by the skbs our kernel allocates and
   hands around. Patching only hyfi makes it disagree with `hi_khslink`/`hi_swa` when an skb
   crosses between them (hyfi's callbacks operate on skbs produced by other modules). Patching all
   of them is a from-binary reconstruction of the vendor's networking stack - strictly more work
   than B1 for a worse artifact.
2. **It is unmaintainable and unauditable.** Every offset site is a patch site; a wrong one is
   silent corruption (offset patches do not fault cleanly). Re-patching must be repeated for every
   re-staged 59-module set (the loader already patches .plt sections with
   `opensource/tools/patch-vendor-modules.sh`; offset patching would add a second, semantic
   patch layer).
3. **It does not supply the missing bridge patches.** The hook call sites and `net_bridge` cache
   are still absent; B2 cannot fix them either.
4. **Publish/legal hygiene.** Modified vendor GPL binaries are local-lab fixtures; they must never
   enter the published half (EVIDENCE-INDEX publish boundary; [PUBLISH-READINESS.md](
   PUBLISH-READINESS.md)). A lane whose core artifact is a patched vendor binary has no
   publishable outcome.
5. **No validation story.** The only way to validate an offset patch is to run the same bench
   experiments B1 would need - but with an artifact whose provenance is a binary diff. B1's
   probe/guard gives a build-time proof; B2 gives none.

**One narrow exception, for the record only:** patching could be used as a *local diagnostic*
(one module, one offset, md5-pinned, never published) to confirm a hypothesis about a specific
field after B1 is in place. It does not replace B1.

### 4.4 Recommendation

1. **Do not resume bench fires for this lane on the strength of this document.** v17 is the
   documented ceiling; a fire needs a written single-variable change, preflight PASS, a READY
   recovery door and a human at the bench (parent STATUS section 3; `tools/fire-preflight.sh`).
2. **If the lane is pursued, do B1 first, host-only:** layout patch + `BUILD_BUG_ON` + probe
   module + offset table. That is cheap, publishable, and removes the guesswork from any later
   fire. Stage-1 fire (v18) verifies no regression; stage-2 (v19) readds hyfi and is expected to
   stop at the vendor-private bytes / `hi_cfe_intf_rx` / hook-consumer faults.
3. **Treat the bridge-patch contract as a vendor-source dependency.** Without the vendor's
   patched bridge source (or an equivalent committed RE report), B1 cannot reach the LAN
   acceptance test; the GPL request is the unblocking action ([GPL-REQUEST.md](GPL-REQUEST.md);
   [PORT-SURFACE.md](PORT-SURFACE.md) argues the same for the vendor stack generally).
4. **Alternatively, re-examine the 2026-11-14 lane** (vendor DTB/vendor MAC, no hyfi) before
   investing in B1: it has the only LAN-green evidence for a vendor stack on our kernel
   (EVIDENCE-INDEX G4). Its blockers (U-Boot bootfip data abort when vendor nodes are added to
   *our* DTB; `hi_mdio` probe crash on our DT) are different and may be cheaper or harder;
   the record does not answer that (Q1).

## 5. Required vendor bridge patches (the part we cannot reconstruct)

### 5.1 The four hook call sites

`hyfi_bridging.ko` only *stores* callbacks into the exported pointer variables (section 3.3).
The consumer code is in the vendor's **patched bridge**, which is built into the vendor kernel,
not shipped as a module. Our `net/bridge/` has no call sites. To make the hooks real we would
need, for each hook, the call site, the calling convention and the expected return/state:

| hook | likely consumer location in a patched bridge (UNVERIFIED) | what would validate it |
| --- | --- | --- |
| `br_get_dst_hook` | bridge forwarding path (destination/offload decision) | bridge create + port add + a forwarded frame; hook counter increments; br-lan persists |
| `br_port_dev_get_hook` | bridge port lookup / `->dev` resolution | port add/remove events; hook counter; port remains in br-lan |
| `br_notify_hook` | bridge netdevice notifier | create/delete br-lan; hook counter; no teardown loop |
| `br_multicast_handle_hook` | bridge multicast path | IGMP/multicast frame on br-lan; counter; no crash |

This table is a hypothesis space, not a recovered design: **the vendor source is required to
name the call sites, signatures and expected state**. Nothing in this repository records them.

### 5.2 The `net_bridge` cache at `netdev_priv + 0x10`

hyfi computes the private area from the device (`dev + 0x540`) and expects a bridge cache at
`+0x10`, populated by the vendor bridge patches (ledger 23234, 23256-23257). In upstream 5.10
`struct net_bridge` starts `spinlock_t lock; spinlock_t hash_lock; struct list_head port_list;
struct net_device *dev;` - so +0x10 is `dev` in **our** layout. That means the vendor's
`net_bridge` (or the vendor's idea of "cache") differs from ours at that offset, and we cannot
populate it correctly without the vendor definition. **Validation trap:** a nonzero value read at
`priv+0x10` on our kernel is the upstream `dev` pointer, not proof that the cache works.

### 5.3 Why this is a source-request dependency

Both the hook consumers and the `net_bridge` cache are in the vendor kernel patch series. The
options are: (a) obtain the vendor GPL source (request sent 2026-10-01, no reply held per
[GPL-REQUEST.md](GPL-REQUEST.md); the vendor modules themselves are `license=GPL`, EVIDENCE-INDEX),
or (b) reverse the vendor kernel image's bridge code - a much larger disassembly target than the
modules, with no committed receipts today. Until one of those exists, the hybrid lane has a hard
stop after B1.

### 5.4 What "validation" can honestly look like before the source arrives

- **Hook-store validation (cheap, no fire):** a small GPL probe module that declares the four
  exported pointers, logs them before/after `hyfi_bridging` init, and asserts each becomes a
  plausible module-text address after the store. This proves hyfi binds *our* variables (a real
  bug class after the v15 function-stub mistake) and nothing more. Do **not** call the callbacks:
  their prototype is unknown, and a wrong call is a willful crash.
- **Reader validation (needs B1 + a fire):** once hyfi runs without an sk_buff oops, the netlink
  handler's progress lines and the absence of new oopses are the only observable; there is no
  ground truth for the cache.
- Everything else waits for source. Mark as blocked, not TODO.

## 6. Offset probe design (kbuild)

### 6.1 What already exists

`opensource/lab/abiprobe/abiprobe.c` is the committed pattern: an external module that prints
`sizeof`/`offsetof` values at load time (currently wiphy/net_device), built twice under two
configs and compared in dmesg. Its limits are stated in
[phase14/abi-match.md](phase14/abi-match.md) sections "Limits, stated plainly" and "The method
generalises...". It is the right base; no new mechanism is needed for sk_buff.

### 6.2 Probe module (design)

Add to the same lab module pattern (a new `lab/skbprobe/` or an extension of `abiprobe` - the
probe can be one source built under both configs):

1. Print the config state that is known to move sk_buff fields: `CONFIG_WIRELESS_EXT`,
   `CONFIG_NF_CONNTRACK`, `CONFIG_NET_SCHED`, `CONFIG_XPS`, `CONFIG_NETWORK_SECMARK`,
   `CONFIG_TLS_DEVICE`, `CONFIG_NET_SWITCHDEV`, `CONFIG_NET_CLS_ACT`, `CONFIG_NET_REDIRECT`,
   `CONFIG_KCOV`, `CONFIG_SKB_EXTENSIONS`, `CONFIG_NET_RX_BUSY_POLL`.
2. Print `sizeof(struct sk_buff)` and every offset in the table: `next`, `dev`, `sk`,
   `tstamp`, `cb`, `_skb_refdst`, `destructor`, `_nfct` (if present), `len`, `data_len`,
   `mac_len`, `hdr_len`, `queue_mapping`, `headers_start`, `headers_end`, `transport_header`,
   `network_header`, `mac_header`, `tail`, `end`, `head`, `data`, `truesize`, `users`,
   `extensions` (conditional fields must be `#ifdef`-guarded).
3. Print the two derived anchors the RE used: `offsetof(head) + offsetof(mac_header)` arithmetic
   is not meaningful as a single number - instead print the two offsets and `offsetof(tail) -
   offsetof(len)` / `offsetof(data) - offsetof(head)` so a reviewer can check the deltas
   without doing the arithmetic.
4. On the vendor-ABI build, `WARN_ONCE` (and one summary line) if any value differs from the
   expected table; on the default build, print the current values for the record.
5. Keep it host-loadable and harmless: prints only, no device access, no writes (same limits as
   abiprobe: no PCI/MMIO, no netdev, no data path).

### 6.3 Build-time guard (`BUILD_BUG_ON`)

The probe validates a build; the guard prevents a silent drift. In the patch that adds the
vendor layout (header next to the struct, not in a random driver), under
`CONFIG_LUOFU_VENDOR_SKB_ABI`:

    BUILD_BUG_ON(offsetof(struct sk_buff, len)              != 0xc8);
    BUILD_BUG_ON(offsetof(struct sk_buff, data_len)         != 0xcc);
    BUILD_BUG_ON(offsetof(struct sk_buff, transport_header) != 0x106);
    BUILD_BUG_ON(offsetof(struct sk_buff, network_header)   != 0x108);
    BUILD_BUG_ON(offsetof(struct sk_buff, mac_header)       != 0x10a);
    BUILD_BUG_ON(offsetof(struct sk_buff, tail)             != 0x10c);
    BUILD_BUG_ON(offsetof(struct sk_buff, end)              != 0x110);
    BUILD_BUG_ON(offsetof(struct sk_buff, head)             != 0x114);
    BUILD_BUG_ON(offsetof(struct sk_buff, data)             != 0x118);
    BUILD_BUG_ON(sizeof(struct sk_buff)                     != 0x128);

    /* also keep the copy-range invariant explicit */
    BUILD_BUG_ON(offsetof(struct sk_buff, headers_end) <= offsetof(struct sk_buff, headers_start));

Design rules: put the guard where no config can silently compile it out (next to the struct or in
the same translation unit as `skb_init`); add a second, unconditional assertion in the probe
module so an incremental/stale build cannot pass; and **record the expected table in this
document** as the source of truth. The guard only proves arithmetic, not vendor semantics.

### 6.4 vmlinux/pahole alternative - current status

A script deriving offsets from `vmlinux` needs type information. The current
`parent:build/linux-kbuild/linux-5.10.201/vmlinux` has **no DWARF/BTF** (`readelf -S` shows no
`.debug*`/`.BTF` sections; `.config` does not enable `CONFIG_DEBUG_INFO`/BTF). So
`pahole -C sk_buff vmlinux` **cannot** be used as-is; the probe-module route is the actual
verification path. If a debug build is wanted, a one-off `CONFIG_DEBUG_INFO=y` build of the
same tree (host-only, no fire) would allow `pahole -C sk_buff` / `gdb -batch -ex 'ptype /o
struct sk_buff'` as a cross-check. Mark the debug build **TODO/optional**; do not treat it as
required for B1.

### 6.5 Probe acceptance

- default config: prints `sizeof=176` and the current offsets; matches `parent:tools/lab-src/
  include/linux/skbuff.h` (0x54/0x58/0x8e/0x90/0x92/0x94/0x98/0x9c/0xa0).
- vendor-ABI config: prints `sizeof=296` and 0xc8/0xcc/0x106/0x108/0x10a/0x10c/0x110/0x114/0x118;
  satisfies the `BUILD_BUG_ON`s.
- at least one negative control on the host: flip one expected constant and confirm the build
  fails (the guard is not vacuous).

## 7. Staged experiment plan (single-variable fires)

**Fires are stopped.** This plan is the minimum honest path *if and only if* the lane is resumed
with a written single-variable change, a preflight PASS, a READY recovery door, a live read-only
capture and a human at the bench. Every stage ends with a recovery decision; no stage is
self-recovering.

### 7.1 Existing safety mechanisms (what they can and cannot guarantee)

| mechanism | where | what it guarantees | caveat |
| --- | --- | --- | --- |
| `panic_on_oops=1` in the built-in cmdline + `CONFIG_PANIC_ON_OOPS` | v17 cmdline (`panic=5 ... panic_on_oops=1 luofu_pet_beats=30`, local capture line 288); build recipe parent `build-3dq.sh:33,57-58` | an oops panics instead of continuing with corrupted state; `panic=5` reboots | the reboot without a slot selector re-enters the test slice (v16: bootreg 11 loop; ledger 20:15) |
| pet-limit stock-A selection | parent `tools/lab-src/arch/arm/mach-luofu/luofu.c:890-934`: after N beats (cmdline `luofu_pet_beats=30`) the timer writes `0x10` to SYSCTRL `0x10100000+0xc08` (the bootreg slot selector, `luofu.c:74`), reports `pet limit reached; selected stock A`, and stops petting the hardware watchdog; the restart handler also writes `0x10` (`luofu.c:154-172`) | eventually returns the box to stock when the kernel/userspace hangs while the timer still runs | **not proven self-recovery**: in the v17 capture the first expiry was followed by a U-Boot POR that re-initialised bootreg to 11 and booted the test slice again; only the second expiry produced `bootreg 10` -> `boot from kernelb failed, change to kernela` -> stock. Budget for a physical RESET+TFTP anyway |
| transient test masks | initramfs bind mounts/patched copies for `kmodloader`, `S99nft-qos`, and the three rootif writers (parent `tools/lab-src/kbuild/init-vendorroot.c:69-81,128-142`; recipe parent `build-3dq.sh:82-85`) | the masks live in RAM and a stock boot must not see them | the v14 ledger records the pre-pivot bind variant failing and persistent overlay copies being used instead; which one was active in v17 is unverified (Q6). A persistent copy is *not* transient and must be re-checked before claiming stock-identical fallback |
| stock fallback | U-Boot: `boot from kernelb failed, change to kernela`; stock uImage `Data Size: 4294096` prints `Uncompressing Linux` while our slice `Data Size: 2865304` does not (parent `tools/fire-preflight.sh:193-194`; local v17 capture lines 3964-3981) | a pre-U-Boot/boot-fail path and a healthy stock boot exist | stock fallback depends on the vendor env/bootreg path; verify per fire from the console, never assume |
| fire gate | parent `tools/fire-preflight.sh`: slice magic/trailer/size <= 8,650,752 B, shadow guard, snapshot guard, bench link, READY recovery (TFTP + `recovery.bin` md5), live read-only capture, attended flag, device health (wiphys, calibration, no leftovers) | catches the known hazards before a write | it cannot prove the delivery lane or the kernel; slot-A manual handover is *not* established (line 171) - fires target slot B only |
| read-only UART | parent `tools/uart-capture.sh`; never transmit (tx pin is dead) | a capture is the only readout on a silent boot | tty re-open can miss the first seconds after a reset |

### 7.2 Stages

| stage | variable (single) | artifacts | expected result | pass/fail rule |
| --- | --- | --- | --- | --- |
| **E0** host-only | none (no fire) | B1 patch + `BUILD_BUG_ON` + probe module + expected table | probe prints the table; default config unchanged; negative-control build fails | no fire is authorised by a package that fails E0 |
| **E1** (v18) fire | `CONFIG_LUOFU_VENDOR_SKB_ABI=y` + the layout patch; hyfi still **not** preloaded | slice with E0 patch; v17-equivalent config otherwise | v17 baseline reproduces: U-Boot reads our slice, 19 modules `ret=0`, `eth2` up 1 Gbps, br-lan created, HSLINK destroy pattern at ~33-35 s, silence ~54 s, pet-limit -> stock; **no oops** | any oops/panic, any different early-failure, or a failure to reach `S95done` = fail; recover, stop, do not add hyfi |
| **E2** (v19) fire | add `hyfi-bridging` to the preload list (single new variable) | E1 slice + hyfi staged/patched | hyfi loads (`ret=0`), registers its private netlink protocol, receives the first vendor messages; if the layout fix is right, the old `hyfi_netlink_receive+0x40` crash should be gone; the **next** fault is expected at a vendor-private byte / `hi_cfe_intf_rx` / a hook consumer (section 4.2.4) | any new oops = expected research result, not a pass; capture the PC/module/args, recover, and stop for RE; no repeated fires without a new written change |
| **E3** (blocked) | implement one hook call site with the vendor signature | requires vendor source or a committed RE report | hook consumer runs, br-lan survives | blocked: no source/report exists |
| **E4** (blocked) | populate/verify the `net_bridge` cache | requires vendor source | hyfi bridge state works / LAN ping | blocked |

### 7.3 Abort rules

- No recovery door READY or no human at the bench -> no fire (preflight fails closed).
- Oops/panic -> capture the full oops (PC, module, args, module list), wait for the reboot, verify
  the landing slot from the console, then **stop**; the next step is an offline report, not
  another fire.
- Console silent with no pet-limit line within ~200 s of a previous successful baseline -> abort
  the experiment, assume the box needs hands, recover.
- Any fire that requires a second variable to "make it work" is a failed single-variable fire;
  record it as such in the ledger.

### 7.4 Post-fire bookkeeping

Per parent AGENTS.md: append a dated `parent:mem-entries.md` entry, regenerate the ledger index,
add the claim/artifact/regeneration row to parent `EVIDENCE-INDEX.md` (slice md5, capture path,
Data Size, module load lines, pet/fallback lines), run the repo checks and commit. A fire whose
console evidence is not quoted in the entry is not a receipt.

## 8. Effort, risks, acceptance criteria

### 8.1 Effort (estimates, not measured)

| item | estimate | notes |
| --- | --- | --- |
| B1 layout patch + `BUILD_BUG_ON` + probe module + host validation (E0) | **1-2 focused days** | host-only, no fire; the abiprobe pattern exists |
| E1 fire (no-regression) | 1 attended fire + recovery budget | expected to reproduce the v17 timeline; if not, stop and investigate |
| E2 fire (hyfi) | 1 attended fire + recovery budget | expected to produce a new fault; stops the lane for RE |
| vendor-private field RE (0x51/0x53/0xb4/0xb8, `hi_cfe_intf_rx`) | **days to weeks of disassembly** | no committed report; unbounded until scoped |
| vendor bridge hook call-site port | **blocked on vendor source** | if the source arrives: an estimated 1-2 weeks to rebase/integrate; without it: not honestly estimable |
| net_bridge cache | blocked on vendor source | same |

This is **not** the multi-year clean-room effort measured in [PORT-SURFACE.md](PORT-SURFACE.md)
and [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md); it is a bounded bench experiment whose
end state is still blocked by missing vendor source. It does not advance the upstream roadmap and
must not be presented as upstream progress ([ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md) header).

### 8.2 Risks

| risk | impact | mitigation |
| --- | --- | --- |
| vendor modules now exercise data paths that v17 never reached | new oopses, slab corruption; bench recovery cycles | stage it (E1 no-hyfi first, E2 hyfi second); panic_on_oops; pet-limit; physical recovery budget |
| struct change silently drifts from vendor | wrong offsets, memory corruption instead of a clean fault | `BUILD_BUG_ON` + probe + negative control (E0) |
| vendor-private bytes are not modelled | in-bounds garbage -> misbehaviour or pointer faults | stop at E2, RE first; do not patch values without a field map |
| hook consumers absent | hyfi's bridge state never works; br-lan still torn down | treat as vendor-source dependency; do not claim LAN success from "no oops" |
| `net_bridge` cache false positive | writing/reading `dev` as a cache, subtle corruption | document the trap; no writes based on plausibility |
| safety mechanism over-trusted | box stranded / loop boots | section 7.1 caveats; recovery door READY; attended only; no self-recovery claims |
| scope creep into the publish boundary | vendor binaries in the published half | local fixtures only; history purge before publication ([PUBLISH-READINESS.md](PUBLISH-READINESS.md)) |
| effort without a decision | bench time spent on a lane that is not the goal | [PORT-SURFACE.md](PORT-SURFACE.md) recommendation: prefer the vendor GPL source path |

### 8.3 Acceptance criteria

**For B1/E0 (host).**

1. Default build offsets equal the current table (0xb0/0xa0/...) and the vendor-ABI build offsets
   equal 0xc8/0xcc/0x106/0x108/0x10a/0x10c/0x110/0x114/0x118 with `sizeof = 0x128`
   (probe output pasted into the report; the `BUILD_BUG_ON`s compile).
2. The negative control (one wrong expected constant) fails the build.
3. No change to default-config artifacts used by the non-hybrid lane.

**For E1 (fire).**

4. Our slice is read by U-Boot (`Data Size: 2865304`-class), the kernel banner matches the build,
   19 modules `ret=0`, `S20network` OK, `eth2` up 1 Gbps, `br-lan` created, **no oops/panic**,
   pet-limit/stock fallback observable, stock boot healthy (calibration `[SUCC]` both bands,
   SSH restored or recovery done). The v17 timeline is reproduced within the expected variance.

**For E2 (hyfi, the lane's real gate).**

5. `hyfi-bridging` loads (`ret=0`, module list contains it; no `module PLT section(s) missing`,
   no `-22`) and its private netlink messages are received without the
   `hyfi_netlink_receive+0x40` oops for **>= 5 minutes** of captured runtime.
6. `br-lan` **survives** > 60 s with at least the `eth2` port, instead of the HSLINK
   create/destroy loop; the console does not go silent.
7. LAN ping: host (192.168.10.44/24, per parent BENCH-FACTS) -> 192.168.10.1, 3/3, 0 % loss.
8. Stock fallback after the experiment: reboot lands on stock, 2 wiphys, calibration `[SUCC]`
   both bands, no `.omo-off` leftovers, SSH/console state verified.
9. Every criterion has a quoted console line or capture path in the ledger entry and an
   `EVIDENCE-INDEX` row; otherwise it is not met.

**Today: none of criteria 4-8 have been met; criterion 5 failed at v16 (oops) and was never
reached at v17 (hyfi not loaded); criterion 7 was last met on 2026-11-14 with the vendor-MAC lane,
not this one.**

## 9. Open questions

- **Q1 (scope).** The 2026-11-14 lane reached LAN with the vendor MAC modules and without hyfi
  (section 2.4). Is the v17 failure caused by the missing HYFI layer, or primarily by the
  lab-GMAC/rootif change? The answer decides whether B1 is the right next step at all. No
  committed artifact resolves it; it needs either a delivery experiment on the 11-14 lane or a
  decision to stay on v17.
- **Q2 (vendor sk_buff composition).** What is the vendor's +0x74 pre-`len` block, and where
  exactly does the extra 4-byte post-`data_len` field sit? Candidates: the vendor kernel's config
  divergence (more networking options enabled) or vendor-added fields. Check for
  `/proc/config.gz` on stock (`CONFIG_IKCONFIG`) after recovery; census the vendor modules for
  skb offset uses; commit the RE notes (the 0x51/0x53/0xb4/0xb8 and `hi_cfe_intf_rx` items
  currently have no committed artifact).
- **Q3 (vendor sk_buff size).** Is the vendor size really 0x128, and does the vendor have
  `CONFIG_SKB_EXTENSIONS` (an extensions pointer at 0x124)? Not measured; derived from the table.
- **Q4 (net_device completeness).** Only `netdev_ops` 0x128, `flags` 0x138, `priv_flags`
  0x13c and `sizeof` 0x540 are proven. hyfi also touches `dev+0x30c`; `hi_cfe` writes a CFE hook
  at `dev+0x21c` (ledger 18:55). Extend `abiprobe` with net_device offsets and compare against
  the vendor modules before trusting the rootif path. **TODO.**
- **Q5 (bridge contract).** Signatures/semantics of the four hooks and the `net_bridge` cache
  layout are unknown and are the hard stop (section 5). Blocked on vendor GPL source or a
  committed RE report.
- **Q6 (rootif mechanism).** Which mechanism actually redirected the `/proc/hsan/net/swa/wan_port`
  writes in the v17 boot (pre-pivot bind mask that the ledger says failed, or persistent overlay
  copies)? A later fire that depends on it needs a receipt. **TODO.**
- **Q7 (safety).** Is the pet-limit/stock fallback reliable enough to fire an experiment that may
  hang the box? The v17 capture shows one expiry returning to the test slice and a second one
  landing on stock; self-recovery is not proven. The recovery door remains mandatory.
- **Q8 (box state).** The 2026-11-18 06:50 ledger and parent STATUS section 3 say the box needs a
  physical RESET+TFTP recovery, but the same fire's later capture/fire log (~06:54-06:56) show
  the vendor slot booted with a new `boot_id` after the pet-limit fallback
  (`parent:build/linux-kbuild/fire-3dq17.out.log`: polls 2-92 read `up=0`, then poll 93 reads
  `up=100`, `boot_id=5128abd9-...`, and the capture's final segment is the stock kernel). The
  current box side is **UNVERIFIED**;
  check with the bench read-only before planning anything.
- **Q9 (priority).** Is this lane worth resuming before the upstream target/DTS work and the GPL
  follow-up? This document takes no position beyond: do not fire without E0, and treat Q5 as
  blocking for LAN.

## 10. Evidence map (claim -> source -> status)

| claim | source | status |
| --- | --- | --- |
| v17 boot/module/eth2/br-lan/HSLINK/silence numbers | ledger 2026-11-18 06:50; local capture `20261117T233458Z-uart-v17.log` | ledger committed; capture local-only |
| v16 oops in `hyfi_netlink_receive+0x40` | ledger 2026-11-17 20:15; local capture `20261117T124213Z-uart-recovA.log` lines 15162-15178 | ledger committed; capture local-only |
| sk_buff vendor offsets (+0x74/+0x78, 0x118 data) | ledger 2026-11-17 20:40; RE anchor functions | ledger committed; RE notes not committed; local spot-checks corroborate |
| 4-byte vendor field between data_len and headers | ledger 2026-11-17 20:40 | committed summary; field identity UNVERIFIED |
| vendor-private bytes 0x51/0x53/0xb4/0xb8; `hi_cfe_intf_rx` | 2026-11-17 RE session (not committed); 0xb4 spot-checked locally | **UNVERIFIED/TODO** |
| net_device WEXT offsets (0x128/0x138/0x13c/0x540) | ledger 2026-11-17 18:55/20:40; parent `tools/lab-src/net/wireless/Kconfig`; [phase14/wifidrv0.md](phase14/wifidrv0.md), [phase14/abi-match.md](phase14/abi-match.md) | committed |
| `br_*` are exported pointer variables; store-only use | ledger 2026-11-17 20:40; parent `tools/lab-src/arch/arm/mach-luofu/luofu.c:1014-1028` | committed |
| hooks' consumer call sites are absent from our tree | local grep of `parent:tools/lab-src/net/bridge`; `luofu.c` comment | committed source state |
| `net_bridge` cache at `netdev_priv+0x10` | ledger 2026-11-17 20:40/2026-11-18 06:50 | committed summary; semantics UNVERIFIED |
| rootif chain (userspace -> `hi_swa` -> `hi_kcfe_pub` -> `hi_kpie`; `hi_kmac` registration) | ledger 2026-11-17 18:55; local symbol-table check of the fixture set | ledger committed; module map local-only |
| 2026-11-14 LAN-green without hyfi | ledger 2026-11-14 22:41; EVIDENCE-INDEX G4; [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) | committed |
| pet-limit stock-A selection and its caveat | ledger 2026-11-17 17:45 (v8) + 2026-11-18 06:50; parent `luofu.c:890-934,154-172`; v17 capture | committed code + ledger; capture local-only |
| probe-module method/limits | [phase14/abi-match.md](phase14/abi-match.md); `opensource/lab/abiprobe/abiprobe.c` | committed |
| current vmlinux has no DWARF/BTF | local `readelf -S` on `parent:build/linux-kbuild/linux-5.10.201/vmlinux` | local check; **recheck before relying on it** |
| effort/risks/acceptance criteria | this document | **proposal**, not measured |

## 11. References

**Committed, this repository**

- [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) - the loader recipe this lane extends.
- [PORT-SURFACE.md](PORT-SURFACE.md) - measured cost/blockers of the vendor Wi-Fi stack; the
  vendor-source argument.
- [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md) - goal-level plan (snapshot; superseded by
  [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md)).
- [PORT-STATUS.md](PORT-STATUS.md) - history only (superseded 2026-11-15).
- [HYBRID-IMAGE-PLAN.md](HYBRID-IMAGE-PLAN.md) - fallback hybrid plan; this lane is adjacent but
  not the same thing.
- [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md) - current aspirational upstream plan.
- [GPL-REQUEST.md](GPL-REQUEST.md) - status of the vendor-source request that blocks section 5.
- [PUBLISH-READINESS.md](PUBLISH-READINESS.md) - publication is NOT READY; history purge pending.
- [phase14/abi-match.md](phase14/abi-match.md), [phase14/wifidrv0.md](phase14/wifidrv0.md) -
  measured struct-ABI method, limits, and the abiprobe pattern.
- `lab/abiprobe/abiprobe.c` - the committed probe module.
- `tools/patch-vendor-modules.sh` - reproducible .plt patching for the vendor module set.
- `lab/vendor-stack/init-vendorroot.c` - the published init (loader; the v17 no-hyfi change lives
  in the parent mirror `tools/lab-src/kbuild/init-vendorroot.c:93-114`).

**Parent repository (private; `parent:`)**

- `parent:STATUS.md` sections 3-4 - live state, bench-fires-stopped decision.
- `parent:EVIDENCE-INDEX.md` - "Bench kernel line, 2026-11-17/18"; G4 row; publish boundary.
- `parent:mem-entries.md` 2026-11-14 22:41; 2026-11-17 18:00/18:55/20:15/20:40; 2026-11-18 06:50.
- `parent:tools/lab-src/` - tracked kernel mirror (`include/linux/skbuff.h`,
  `net/core/skbuff.c`, `net/wireless/Kconfig`, `net/bridge/`, `arch/arm/mach-luofu/luofu.c`,
  `kbuild/init-vendorroot.c`).
- `parent:tools/fire-preflight.sh`, `parent:build/linux-kbuild/build-3dq.sh` - fire gate and
  v17-era build recipe (local-only).
- `parent:build/uart-logs/20261117T233458Z-uart-v17.log`,
  `parent:build/uart-logs/20261117T124213Z-uart-recovA.log`,
  `parent:build/linux-kbuild/fire-3dq17.out.log` - local-only captures/log cited above.
