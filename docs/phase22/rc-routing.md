# rc-routing: the descriptor-fetch DMA traverses EP0's root complex, the interrupt traverses EP1's (phase 22, 2026-10-02)

Task `st_01a0fbdf`. Read-only analysis of the on-disk register captures plus the two window docs
(`docs/phase20/host-window.md`, `docs/phase18/inbound-map.md`). No router access, no other doc
edited. Every claim is marked **[proven]** (a live captured value, a register/instruction stream,
or a vendor boot log line), **[measured]** (a differential across two captured boots), or
**[inferred]** (mechanism derived from those measurements, not a literal).

**Path note.** The brief names `build/register-dumps/sibep/001_partA_live_vendor.txt`; that file
is on disk at **`build/register-dumps/sibep/000_partA_live_vendor.txt`** (an identical copy with
more RC bring-up lines is at `build/register-dumps/sr2/001_partA_live_vendor.txt`). The second
named capture, `build/register-dumps/srt/001_live_vendor_regs.txt`, exists as named. Both were
read only.

---

## Verdict

- **EP0 `0000:00:00.0` sits behind RC0 = `pcie@0x10160000`** (`hi_pcie 10160000.pcie`, PCI domain
  0, host bridge `0000:00`). Its endpoint iATU is reached at **BAR2 = `0x41800000`**; its region-3
  register viewport at **BAR0 = `0x40000000`** (`ETE` = BAR0+`0x3f2000`). **[proven, vendor dmesg +
  PCI enumeration]**
- **EP1 `0001:00:00.0` sits behind RC1 = `pcie@0x10164000`** (`hi_pcie 10164000.pcie`, PCI domain
  1, host bridge `0001:00`). Endpoint iATU at **BAR2 = `0x59800000`**; region-3 viewport at
  **BAR0 = `0x58000000`**. **[proven]**
- A device **descriptor fetch is a bus-master DMA read** of host memory at a *device VA* the host
  wrote into the SR/DR registers (`pcie_hostca_to_devva` @`0xaefc`, identity for chiptype 0:
  `devva = hostca`). It is translated twice: the **endpoint iATU *outbound* window** on the port
  the DMA egresses (device VA -> PCI bus address), then the **receiving RC's *inbound* window**
  (PCI bus address -> host DRAM). **[proven** code + live windows**]**
- **The fetch egresses EP0's port, so it is EP0's endpoint outbound window and RC0's inbound
  window that translate the fetch address — not the sibling's.** **[measured/inferred]** The
  differential is unambiguous across the captured boots (see B.2): an EP0-decoded takeover reads
  the host SR descriptors (`SR ch0 DEVICE INDEX 0x10 -> 0x400`), an EP1-decoded takeover never
  does (`SR+0x1c` frozen at `0x10`). The completion INTx goes the other way, via EP1/RC1 (irq
  209). The device splits the two halves across its two links ("`is_pcie_cross[1]`").
- **The vendor programs BOTH endpoints' iATU (both RCs): both outbound viewports and both sets of
  six inbound viewports.** **[proven]** Live values in `build/register-dumps/bothep/001_live_vendor_bars2.txt`.
- **What our sibling takeover must add:** keep the registry/interrupt on EP1 (domain 1, irq 209),
  and additionally claim `0000:00:00.0` and program **EP0's iATU via BAR2 `0x41800000`** — the one
  outbound viewport 0 at `+0x000` (the fetch-critical write) and the six inbound viewports at
  `+0x104+0x200*i`. Exact values in Part D.

---

## Part A — the two functions and their root complexes

The SoC has two `hsan,pcie` RC nodes (`docs/soc/luofu-r116.dts` lines 1232/1257), instantiated by
`hi_pcie`. The vendor boot (`build/register-dumps/bothep/002_live_vendor_dmesg.txt`,
`build/register-dumps/srt/042_boot1_full_dmesg.txt`) shows both host bridges and both endpoints:

```
[10.750823] PCIe:0 switch to RC mode
[10.769584] hi_pcie 10160000.pcie: host bridge /pcie@0x10160000 ranges:
[10.788380] hi_pcie 10160000.pcie: PCI host bridge to bus 0000:00
[10.879572] PCIe:1 switch to RC mode
[10.896314] hi_pcie 10164000.pcie: host bridge /pcie@0x10164000 ranges:
[10.910057] hi_pcie 10164000.pcie: PCI host bridge to bus 0001:00
...
[PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1.
[PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0.
[PCIEL]raw irq: 209 ; request pcie intx irq 209 succ ; bus_id_hostview[1], phy_devid[0], is_pcie_cross[1]
[PCIEL]raw irq: 207 ; request pcie intx irq 207 succ ; bus_id_hostview[0], phy_devid[1], is_pcie_cross[1]
```

| endpoint | RC node | `hi_pcie` | PCI domain / bus | BAR0 (region-3) | BAR2 (iATU) | cfg[0xff8] | phy_devid | INTx |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `0000:00:00.0` | `pcie@0x10160000` | `10160000.pcie` | 0 / `0000:00` | `0x40000000` | `0x41800000` | `0x00011521` | 1 | 207 (0 deliveries) |
| `0001:00:00.0` | `pcie@0x10164000` | `10164000.pcie` | 1 / `0001:00` | `0x58000000` | `0x59800000` | `0x00011520` | 0 | 209 (the live ISR) |

**[proven]** (dmesg + `sr2/001_partA_live_vendor.txt` config dump: `BAR0 0x40000004` /
`0x58000004`, resources `0x40000000-0x40ffffff` / `0x58000000-0x58ffffff`, iATU
`0x41800000-0x41803fff` / `0x59800000-0x59803fff`, irq 207/209). Both are `59e7:0005` class
`028000`. The host-view bus id is *crossed* against the chip's `phy_devid` (bus 0 has phy 1,
bus 1 has phy 0), which is what `is_pcie_cross[1]` records.

The RC-side windows themselves are the DTS `iatu_rc` lists, programmed by `hi_pcie` at boot:

```
pcie@0x10160000: iatu_rc = <0 4 0x80000000 0x50000000 0 0x57ffffff 0 0
                            1 0 0x80000000 0x40000000 0 0x47ffffff 0x40000000 0
                            2 2 0x80000000 0x48000000 0 0x4fffffff 0x48000000 0>;
pcie@0x10164000: iatu_rc = <0 4 0x80000000 0x68000000 0 0x6fffffff 0 0
                            1 0 0x80000000 0x58000000 0 0x5fffffff 0x58000000 0
                            2 2 0x80000000 0x60000000 0 0x67ffffff 0x60000000 0>;
```

**[proven, DTS]**. These are the same in a vendor boot and a takeover (the `hi_pcie` platform
driver is loaded in both), so *nothing on the RC side changes between boots* — the only boot
delta is the endpoint iATU, which `oal_pcie_dev_init` programs from the vendor stack
(`docs/phase20/host-window.md` A.3). The RC `misc` window `0x10161000` is **not** touched (a
read-only `devmem` of it panics: `docs/phase20/host-window.md` C.4 hazard).

---

## Part B — what a DEVICE DMA read against host memory implies

### B.1 The two translations

`pcie_hostca_to_devva` @`0xaefc` converts every host coherent-DMA address to the device VA written
into the ring registers; for the chiptype-0 window (`devva_base = hostca_base = 0x80000000`,
`devva_end = 0xffffffff`) it is the **identity**: `devva == hostca` (host physical, e.g.
`0x8370b000` / `0x83a58000` — the live ring bases in `srt/001_live_vendor_regs.txt` and
`sibep/000_partA_live_vendor.txt`). **[proven]**

The device then issues the read at that devva. Two windows must both decode it:

1. **Endpoint iATU outbound window** (`oal_pcie_set_outbound_by_membar` inlined at `0x9a38`, the
   block at `iatu+0x000..0x018`), on the port the DMA actually egresses: device VA
   `0x80000000..0xffffffff` -> PCI bus address `0x80000000..` (target = `hostca_base`).
2. **RC inbound window** of the root complex that receives the TLP: PCI bus address -> host DRAM.
   Programmed by `hi_pcie` from `iatu_rc`; fixed across boots.

This is why the endpoint outbound viewport is described as "the window our host buffers are
visible in": the coherent buffers the module posts live at host physical `0x83xxxxxx`, inside
`0x80000000..0xffffffff`, and only the outbound window maps that range out onto PCIe.

### B.2 Which port the DMA egresses — the cross-boot differential

**[measured]** Two captured takeover boots, identical except for which endpoint was decoded:

| boot lane | claimed | iATU programmed | SR engine fetch? | INTx |
| --- | --- | --- | --- | --- |
| `txpath` (phase 20f) | EP0 `0000:00:00.0` | EP0 BAR2 `0x41800000` | **yes** — `[post0] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 (host wptr=0x00000400) = SR engine read` | irq 207, 0 deliveries |
| `sibep`/`sr2`/`srt` (phase 21) | EP1 `0001:00:00.0` | EP1 BAR2 `0x59800000` | **no** — `SR+0x1c` stays `0x10` for the whole run; `H2D MASK CLEARED BY DEVICE count = 0` | irq 209, thousands of deliveries |

Evidence: `build/register-dumps/txpath/031_testboot_evidence.txt` (also `033_…`, `030_…` full
dmesg) for the fetch; `build/register-dumps/sibep/062_decisive.txt`,
`build/register-dumps/sr2/` and `build/register-dumps/srt/043_boot1_decisive.txt` for the frozen
SR index. Both `txpath` and `sibep` used the **same** registry instance 0 (glue `0x40039000`,
ETE `0x4003a000`), the same firmware load and the same SR frame — so the only variable that flips
the fetch is which endpoint's iATU is decoded. **Conclusion: the device's descriptor-fetch DMA
egresses the EP0 port and is translated by EP0's outbound window + RC0's inbound window; the
completion INTx is delivered on EP1/RC1.** **[measured]**

**[inferred]** mechanism: the chip is dual-PHY and crossed (`is_pcie_cross[1]`); the register /
interrupt slave side is serviced on the `phy_devid 0` link (EP1, irq 209), while the ETE master's
DMA egress lands on the `phy_devid 1` link (EP0, irq 207). The on-disk `bothep` lane
(`lab/bothep/bothep.c`, header) was written to test exactly this: keep the interrupt on 209 and
additionally decode EP0's RC so both halves are live in one boot.

Consequence for the sibling configuration: `sibep`/`sr2`/`srt` program only EP1's iATU; EP0's
outbound viewport is left at reset in a takeover, so the device's fetch address is never
translated and the engine never reads the posted descriptors — exactly the observed symptom.

---

## Part C — the vendor programs BOTH RCs

**[proven]** `build/register-dumps/bothep/001_live_vendor_bars2.txt` (read-only live vendor
capture) reads **both** endpoints' BAR2 iATU. Both outbound viewport 0 blocks are byte-identical;
the six inbound viewports are per-endpoint (host base differs by `0x18000000`).

Outbound viewport 0 (same at EP0 BAR2 `0x41800000` and EP1 BAR2 `0x59800000`):

| off | value | meaning |
| --- | --- | --- |
| `0x000` | `0x00000000` | CTRL1 = 0 (memory region) |
| `0x004` | `0x80000000` | CTRL2 = enable \| BAR0 |
| `0x008` | `0x80000000` | base_lo = `devva_base` |
| `0x00c` | `0x00000000` | base_hi |
| `0x010` | `0xFFFFFFFF` | limit = `devva_end` |
| `0x014` | `0x80000000` | target_lo = `hostca_base` |
| `0x018` | `0x00000000` | target_hi |
| `0x01c` | `0x00000000` | (unused) |

Inbound viewports (live vendor, BAR2, `+0x104+0x200*i`; CTRL2 `0x80000000`, CTRL1 at `-4` = 0):

| i | off | EP0 base_lo (BAR2 `0x41800000`) | EP1 base_lo (BAR2 `0x59800000`) | base_hi | limit | target_lo | target_hi |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | `0x104` | `0x40000000` | `0x58000000` | 0 | base+`0x1BFFFF` | `0x00000000` | 0 |
| 1 | `0x304` | `0x401C0000` | `0x581C0000` | 0 | base+`0x017FFF` | `0x00400000` | 0 |
| 2 | `0x504` | `0x401D8000` | `0x581D8000` | 0 | base+`0x1DFFFF` | `0x01000000` | 0 |
| 3 | `0x704` | `0x403B8000` | `0x583B8000` | 0 | base+`0x11FFFF` | `0x40000000` | 0 |
| 4 | `0x904` | `0x404D8000` | `0x584D8000` | 0 | base+`0x1DFFFF` | `0x02000000` | 0 |
| 5 | `0xb04` | `0x406B8000` | `0x586B8000` | 0 | base+`0x217FFF` | `0x01200000` | 0 |

The vendor dmesg corroborates the double programming: `sibep/000_partA_live_vendor.txt` and
`sr2/001_partA_live_vendor.txt` show **both** `[oal_pcie_region_inbound_cfg]PCIe inbound bus
addr` lists (`0x58000000…0x586b8000` for bus 1, `0x40000000…0x406b8000` for bus 0) and **both**
`[oal_pcie_set_outbound_by_membar:642]PCIe outbound bus addr:0x80000000` lines. **[proven]**

So: **BOTH RCs are programmed** — the vendor's single driver binds both functions and runs the
same inbound+outbound init per endpoint (`oal_pci_lres_init` → `pci_dev_res_init`, which loops the
whole per-chip device array). Our earlier single-endpoint takeovers decoded only one, which is
why neither ever had both the fetch and the interrupt at once.

---

## Part D — the concrete programming our takeover must add (sibling configuration)

Keep everything the sibling bring-up already does on EP1: claim `0001:00:00.0` (domain 1), BAR0
`0x58000000` region-3 viewport, BAR2 `0x59800000` six inbound + one outbound viewport,
`PCI_COMMAND=7`, firmware, rings, release, `request_irq(209)`. **Add a second claim** on the
`phy_devid 1` / irq-207 function and decode its RC:

1. **Claim + map the second endpoint** `0000:00:00.0` (domain 0):
   `pci_get_domain_bus_and_slot(0,0,0)`; `pci_enable_device`; `pci_request_mem_regions`;
   `pci_iomap(dev, 0)` -> BAR0 `0x40000000` (region-3 viewport, ETE at BAR0+`0x3f2000`);
   `pci_iomap(dev, 2)` -> **BAR2 `0x41800000`** (iATU). Do **not** request irq 207 (it never
   fires; the vendor's ep0 INTx count is 0).
2. **Program EP0's outbound viewport 0 — the fetch-critical write.** Through BAR2
   `0x41800000 + off`, in the vendor's order:

   | order | BAR2 offset | value | name |
   | --- | --- | --- | --- |
   | 1 | `0x41800000 + 0x000` | `0x00000000` | CTRL1 = 0 (memory region) |
   | 2 | `0x41800000 + 0x004` | `0x80000000` | CTRL2 = enable \| BAR0 |
   | 3 | `0x41800000 + 0x008` | `0x80000000` | base_lo = `devva_base` |
   | 4 | `0x41800000 + 0x00c` | `0x00000000` | base_hi |
   | 5 | `0x41800000 + 0x010` | `0xFFFFFFFF` | limit = `devva_end` |
   | 6 | `0x41800000 + 0x014` | `0x80000000` | target_lo = `hostca_base` |
   | 7 | `0x41800000 + 0x018` | `0x00000000` | target_hi |

   This is the same window the sibling already programs at `0x59800000 + 0x000`; adding it on
   EP0 maps the device-VA range `0x80000000..0xffffffff` (which contains every host coherent
   buffer the module posts, e.g. `0x8370b000`) out through the port the fetch actually uses.
3. **Program EP0's six inbound viewports** (`0x41800000 + 0x104 + 0x200*i`, `i = 0..5`), the
   vendor's values in the Part C table (EP0 column): CTRL2 `0x80000000`, base = `0x40000000 +
   {0, 0x1c0000, 0x1d8000, 0x3b8000, 0x4d8000, 0x6b8000}`, limit = base+size-1, target =
   `{0x00000000, 0x00400000, 0x01000000, 0x40000000, 0x02000000, 0x01200000}`. These complete the
   vendor's per-endpoint decode and make the device's regions reachable through EP0's BAR0
   viewport; they are not the fetch path themselves (the fetch is device->host, i.e. outbound).
4. Optionally mirror the SR/DR ring programming through EP0's BAR0 `0x40000000+0x3f2000` if the
   sequence test (`omo_seq`) is used to isolate which BAR's write coincides with the fetch; the
   register instance is the same instance 0 either way.

No RC-side write is added: RC0's `iatu_rc` inbound windows are already programmed by `hi_pcie`
in a takeover boot (Part A), and the RC `misc` window `0x10161000` must stay untouched.

---

## Part E — proven vs inferred

| claim | status |
| --- | --- |
| EP0 `0000:00:00.0` behind RC0 `pcie@0x10160000` (domain 0), BAR0 `0x40000000`, BAR2 `0x41800000` | **proven** (vendor dmesg + config dump) |
| EP1 `0001:00:00.0` behind RC1 `pcie@0x10164000` (domain 1), BAR0 `0x58000000`, BAR2 `0x59800000` | **proven** |
| both endpoints alias one on-chip register space; only instance 0 (glue `0x40039000`, ETE `0x4003a000`) initialised | **proven** (`sr2/001` alias proof; `sibep/000` instance dump) |
| descriptor fetch = device DMA read at a devva from `pcie_hostca_to_devva` (identity for chiptype 0) | **proven** (`0xaefc` code + live ring bases) |
| vendor programs BOTH endpoints' iATU: both outbound viewports (identical) and both six-inbound sets | **proven** (`bothep/001_live_vendor_bars2.txt` + both dmesg lists) |
| EP0-decoded takeover fetches host SR descriptors (`SR ch0 DEVICE INDEX 0x10 -> 0x400`) | **measured** (`txpath/031`, `033`, `030`) |
| EP1-decoded takeover never fetches (`SR+0x1c` frozen at `0x10`; H2D mask never cleared) | **measured** (`sibep/062`, `srt/043`) |
| therefore the fetch is translated by EP0's outbound window + RC0's inbound window | **measured/inferred** (differential + dual-PHY cross; bothep dual claim is the confirmation experiment) |
| the INTx / completion path is EP1 / RC1 (irq 209) | **proven** (live `/proc/interrupts`, `pcie_intr_handle` stack) |
| concrete EP0 iATU values to add | **proven** (vendor live values, `bothep/001`) |
| exact hardware reason the master egress differs from the interrupt link | **inferred** (not exposed as a literal; `is_pcie_cross[1]` + the measured differential) |

### Values quoted

- Outbound viewport 0 (both endpoints): `0x000:0`, `0x004:0x80000000`, `0x008:0x80000000`,
  `0x00c:0`, `0x010:0xFFFFFFFF`, `0x014:0x80000000`, `0x018:0`.
- Sibling EP1 EP0-mirror host bases: EP0 `0x40000000`, EP1 `0x58000000` (offset `0x18000000`).
- Device-visible window: `devva_base=0x80000000`, `devva_end=0xffffffff`,
  `hostca_base=0x80000000`.
- Host coherent ring bases seen live: `SR0 0x84573000` (`sibep/000`/`srt`), `0x8370b000` /
  `0x83a58000` (`srt/043`, `sibep/062`).

### Artifacts read (read-only)

```
build/register-dumps/sibep/000_partA_live_vendor.txt     vendor dmesg (both endpoints), config, alias proof
build/register-dumps/sr2/001_partA_live_vendor.txt       vendor RC bring-up, both endpoint region sets
build/register-dumps/srt/001_live_vendor_regs.txt        live ETE instance-0 register dump
build/register-dumps/srt/042_boot1_full_dmesg.txt        hi_pcie host bridges + ep1 iATU programming
build/register-dumps/srt/043_boot1_decisive.txt          ep1 no-fetch decisive lines
build/register-dumps/bothep/001_live_vendor_bars2.txt    BOTH endpoints' BAR2 iATU live values  <-- key
build/register-dumps/bothep/002_live_vendor_dmesg.txt    both host bridges + phy_devid/cross
build/register-dumps/txpath/031_testboot_evidence.txt    ep0-decoded: SR engine read (fetch)
build/register-dumps/txpath/033_testboot2_evidence.txt   ep0-decoded: SR engine read (reproduced)
build/register-dumps/sibep/062_decisive.txt              ep1-decoded: irq fires, no fetch
lab/bothep/bothep.c                                      the dual-claim lane this verdict feeds
docs/phase20/host-window.md, docs/phase18/inbound-map.md window semantics and RC-side notes
docs/soc/luofu-r116.dts                                  the two RC nodes' iatu_rc/iatu_ep
```
