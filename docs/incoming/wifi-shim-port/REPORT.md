# REPORT: vendor 5.10.201 Wi-Fi binaries on our 6.18 image - binary-port feasibility

Lane: Wi-Fi binary-port (ndiswrapper-style). Host-only, read-only. All machine
evidence was regenerated from the shipped .ko binaries, the 6.18 built tree
(vmlinux, .config, headers) and the 5.10.201 GPL tree. Nothing on the device,
the release series, or curated repos was touched. Raw dumps live in raw/
(gitignored); everything committed here is text.

## TL;DR

**VERDICT: NO-GO** for loading the unmodified (vermagic-patched) vendor
binaries on our 6.18.60 kernel. The import layer is shimmable (16 symbols), but
the vendor code carries baked 5.10 struct layouts for net_device, sk_buff and
genl_info - 97 of 153 probed member offsets drifted - with disassembly-verified
live-path hits that no shim can redirect. Exact numbers in M1-M5 below.

---

## M1: IMPORT CLOSURE

Full table: analysis/import-closure.csv (367 rows, one per distinct UND symbol).
Machine summary: analysis/import-closure-summary.md.

| metric | value |
| --- | --- |
| hi5622v100_plat.ko GLOBAL UND | 126 |
| hi5622v100_wifi.ko GLOBAL UND | 308 |
| distinct union | 367 |
| (a) exported by our 6.18 vmlinux (parsed __ksymtab/__ksymtab_gpl, 7539 names) | 148 plain + 26 same-name-risk = 174 |
| (b) provided by vendor .ko (closure, depth<=3) | 177 |
| (c) cfg80211/mac80211/netdev API drifted 5.10->6.18 | 0 unresolved (all 26 wireless imports come from vendor cfg80211.ko) - drift is in STRUCT LAYOUTS, see M3 |
| (d) vendor hi_* platform symbols | all but one provided by vendor .ko; 1 stub (done_path_create) |
| (e) other (needs shim) | 15 |

Provider breakdown: hi5622v100_plat.ko -> wifi.ko 138 symbols (oal_/oam_/hcc_/
dmac_ platform API); cfg80211.ko -> wifi.ko 26 (the whole 5.10 cfg80211 API:
wiphy_new_nm, wiphy_register, cfg80211_scan_done, ...); ksecurec.ko -> both 11
(memcpy_s family); hi_kpie.ko -> both 2. wifi.ko imports NO mac80211 symbol
(fullmac). hi_pcie.ko (bench load companion): 56 UND, 52 from 6.18, 1 vendor,
3 shim (PDE_DATA, of_get_named_gpio_flags, printk).

Closure load order (topological, depth<=3):
hi_basic -> hi_kpie -> ksecurec -> compat -> cfg80211 -> hi_kwificlk(*)
-> hi_pcie -> [shim] -> plat -> wifi.
(*) hi_kwificlk.ko is absent from recovery-extract; get it from the device.

Shim-needed set (16): printk, __kmalloc, kmalloc_order_trace,
kmem_cache_alloc_trace, vmalloc, alloc_workqueue, kfree_skb, consume_skb,
__dev_kfree_skb_any, skb_queue_purge, hrtimer_init, kern_path_create,
done_path_create, dma_sync_single_for_cpu, mem_section, wireless_send_event.
Key 6.18 facts behind them: printk/kfree_skb/consume_skb are NOT exported
(EXPORT_SYMBOL sits under disabled config); allocators renamed *_noprof;
netif_napi_add renamed; hrtimer_init -> hrtimer_setup(4 args, callback moved);
mem_section does not exist (our kernel is FLATMEM, vendor was SPARSEMEM).

## M2: VERMAGIC STRATEGY

analysis/vermagic.md + tools/vermagic-patch.py. DECIDED and TESTED.

Facts: CONFIG_MODVERSIONS off, CONFIG_MODULE_SIG off, CONFIG_MODULE_FORCE_LOAD
off (so modprobe --force-vermagic CANNOT work - the kernel itself rejects a
missing/mismatched vermagic), CONFIG_PREEMPT_NONE, ARM_PATCH_PHYS_VIRT=y.
Our string: "6.18.60 SMP mod_unload ARMv7 p2v8 " (37 B). Vendor string:
"5.10.201 SMP mod_unload ARMv7 " (29 B).

Procedure (local copies only, .ko never committed): rename the old vermagic=
key to xermagic=, write our exact string over the first >=47-byte expendable
.modinfo run (import_ns=HW_RTOS_NS runs in plat/wifi; version= backports
banner in cfg80211/compat), NUL-pad, verify with readelf. PATCH OK verified on
all four modules. Load with insmod (never modprobe).

## M3: STRUCT-LAYOUT AUDIT (decisive)

3a) Offset drift - tools/layout-probe.c compiled against BOTH trees.
analysis/struct-layout-drift.md: **97 of 153 probed members drifted**.

- net_device: essentially total reorganization (name 0x000->0x0d4,
  netdev_ops 0x128->0x008, mtu 0x148->0x030, state 0x028->0x088,
  ieee80211_ptr 0x1d0->0x284, rx_handler 0x1f4->0x0c4, ...). Vendor
  sizeof(net_device)=1344 B vs 6.18's ~2.6 KB.
- sk_buff: head block stable (len@84 both), but data/head/tail/end/users/
  network_header/mac_header/mark/hash/... uniformly +8.
- genl_info: nlhdr +4, genlhdr +4, extack +44.
- device: dma_mask/coherent_dma_mask -168, of_node -172.
- wiphy/wireless_dev/channel/chan_def/supported_band: drifted too, but they
  are OWNED by the vendor cfg80211.ko on this path -> consistent, not fatal.

3b) Member-access census - tools/m3-member-scan.py (hint-gated upper bound) +
3d) manual verification - tools/m3-verify.py. See
analysis/member-audit-summary.md and per-module CSVs.

| module | KERNEL-owned hits (drifted) | VENDOR-owned hits |
| --- | --- | --- |
| wifi.ko | 28 (21) | 37 |
| plat.ko | 15 (4) | 0 |
| cfg80211.ko | 30 (20) | 64 |

VERIFIED-REAL drifted accesses on live paths (disassembly-confirmed):
- wifi.ko ndo_set_mac_address reads net_device.state @0x28 (6.18: 0x88);
- wifi.ko mtu-change writes net_device.mtu @0x148 (6.18: 0x30);
- wifi.ko rx/tx paths read sk_buff head/tail/end/mac_header/users/mark @5.10;
- plat.ko netbuf alloc reads sk_buff head/data/truesize @5.10;
- cfg80211.ko nl80211 doit handlers WRITE genl_info.genlhdr @0x0c (6.18: inside
  the nlhdr pointer) - first nl80211 command corrupts memory;
- cfg80211.ko reads sk_buff.head @0x9c (nl80211_del_key), net_device.state
  @0x28 (conn_work), 59 functions read skb->users @0xa8.

3c) Vtable order - tools/vtable-diff.py. analysis/vtable-order-drift.md.
net_device_ops: 80->92 members, 4 inserted at slots 12-15 + 9 later changes.
BUT the driver's static table (.data+0x1b28) populates only slots
{2 open, 3 stop, 4 xmit, 9 set_mac, 16 stats64, 19 stats}: slots 0-11 map
IDENTICALLY in 6.18 (core calls the right functions); stats slots land on NULL
-> core fallback. => netdev_ops vtable drift is PARTIAL, not fatal here.
ethtool_ops 65->86 reordered, but the driver registers none -> generic path.
genl_ops first 7 members same order -> nl80211 family would register; its
.doit bodies then die on genl_info layout (3b).

## M4: SHIM DESIGN + PROTOTYPE

shim/wifi_shim.c (exports the 16 drifted symbols with 5.10 semantics on 6.18,
plus PDE_DATA / of_get_named_gpio_flags for hi_pcie.ko), shim/Makefile,
shim/README.md. **Compiles cleanly against the 6.18 built tree** (cross gcc
14.4.0, COMPILE-OK). Techniques used: #define-rename of 6.18 static inlines
before including their headers; _noprof wrappers; vprintk-based printk;
reconstructed kern_path_create/done_path_create pair; replicated 6.18
dma_sync_single_for_cpu inline body; hrtimer_init -> hrtimer_setup with stub
callback; FLATMEM mem_section stand-in (flagged as unverified/dangerous);
no-op wireless_send_event.

cfg80211 entry-point translation (registration-time cfg80211_ops rewrite) was
designed for the alternative 6.18-native-stack path (find_module +
core_layout.base + baked symbol offsets, rebuilding the 112-slot ops table in
6.18 layout) - but M3 shows that path dies too: wiphy.priv -64,
wiphy.registered -136, wireless_dev.conn -56, plus the driver's own
net_device/sk_buff hits remain. Documented as non-viable; the vendor-stack
path (no ops translation needed) is the only one that reaches the layout wall.

Vendor hi_* satisfaction: loading the other vendor .ko binaries (order above)
satisfies 177 imports; the shim stubs exactly 1 vendor-namespace symbol
(done_path_create) and 15 kernel-drift symbols.

## M5: VERDICT + BENCH PLAN

NO-GO. Numbers: 367 distinct imports = 174 satisfied by 6.18 + 177 by vendor
modules + 16 by shim. Blocked-by-struct-layout: 21 (wifi.ko) + 20 (cfg80211.ko)
+ 4 (plat.ko) drifted KERNEL-owned member hits in the census, of which >=12 are
disassembly-verified on live paths (state/mtu/skb-head/tail/end/mac_header/
users/mark/genl-genlhdr/nlhdr/extack), plus the genl_info WRITE corruption,
plus hrtimer/mem_section layout loss. The vendor cfg80211.ko (mandatory: our
6.18 has NO wireless stack) is itself broken on 6.18 genl/skb/netdev layouts,
and the 6.18-native-stack alternative breaks on wiphy/wdev layout drift.
=> binary path not viable; blocker list below.

### Bench test sequence (to empirically confirm the NO-GO when a human is present)

Prep (host): copy vendor .ko set + regulatory.db from the vendor rootfs;
run tools/vermagic-patch.py on hi_basic, hi_kpie, ksecurec, compat, cfg80211,
hi_pcie, hi5622v100_plat, hi5622v100_wifi (LOCAL copies); build wifi_shim.ko
against the 6.18 tree; fire only with fire-preflight.sh PASS and the recovery
door armed. NEVER rmmod the vendor modules once loaded (power-cycle to reset);
UART capture read-only throughout.

1. insmod wifi_shim.ko          -> dmesg: "wifi_shim: ... loaded"
2. insmod hi_basic, hi_kpie, ksecurec, compat (patched)  -> no "Unknown symbol"
3. insmod cfg80211 (patched)    -> nl80211 family registers (genl_register OK)
4. iw dev                       -> EXPECTED FAILURE (NO-GO checkpoint 1):
   the .doit handler writes genl_info.genlhdr at the 5.10 offset -> dmesg
   shows a null-deref/oops in the cfg80211 netlink path. STOP if it does not:
   record and re-verify.
5. insmod hi_pcie, hi5622v100_plat (patched) -> PCIe init; watch
   oal_dma_map_single's mem_section stand-in (garbage-tolerant check).
6. insmod hi5622v100_wifi (patched) -> hmac init; firmware path per
   wifi-firmware-re: contiguous copy to CA 0x01240000, 0x5a5a->0x40000108,
   0xcece->0x4000010c, ready poll 0x40039014 (never write 0x400392f0).
7. ip link set wlan0 up        -> EXPECTED FAILURE (NO-GO checkpoint 2):
   ndo_open/ndo_set_mac_address read netdev->state @0x28 (6.18 field differs)
   -> wrong carrier/state decisions; dmesg checkpoints.
8. Traffic/scan if it ever comes up -> sk_buff +8 drift corrupts head/tail/
   users in the rx/tx paths (NO-GO checkpoint 3).

Any checkpoint passing where NO-GO was predicted must be recorded as evidence
and the analysis revisited - the predictions above are falsifiable.

### Blocker list (exact, for the roadmap)

1. net_device layout drift: 100% of probed members moved; verified live hits.
2. sk_buff layout drift: +8 shift on the data-path field block; verified hits
   in driver, plat and cfg80211 paths.
3. genl_info layout drift: cfg80211.ko nl80211 handlers write through the
   6.18 nlhdr/genlhdr pointer fields (memory corruption).
4. cfg80211.ko itself is 5.10-layout code running on 6.18 core structs
   (mandatory dependency; our 6.18 kernel has no wireless stack).
5. hrtimer struct rework (hrtimer_setup 4-arg, driver writes timer->function
   at 5.10 offset).
6. mem_section: SPARSEMEM->FLATMEM; no faithful 6.18 equivalent.
7. net_device_ops/ethtool_ops vtable order drift: PARTIAL (verified survivable
   for the driver's populated slots; fatal for any driver populating >=12).
8. 16 shim symbols required even before layout issues (all mapped; see M4).

## Evidence / regeneration

- import closure: python3 tools/m1-import-closure.py --plat ... --wifi ...
  --vendor-dirs ... --vmlinux <6.18 vmlinux> --out-dir analysis
- layout drift: compile tools/layout-probe.c against both trees (flags in
  shim/README.md), then python3 tools/layout-diff.py ...
- member census: python3 tools/m3-member-scan.py --dis raw/<m>.dis ...
- vtable order: python3 tools/vtable-diff.py ...
- vermagic: python3 tools/vermagic-patch.py <in.ko> <out.ko>
- raw dumps: raw/ (objdump -dr of the vendor .ko, gitignored; never committed)
