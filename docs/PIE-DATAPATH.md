# PIE datapath: evidence-graded RE specification (Hi5671Y luofu packet engine)

**Status 2026-11-18 (task-22, lane `pie-datapath-re`): specification only - no driver code, no
shared-file edits, no device access.** Host-only reverse engineering of the vendor packet engine
("PIE", `hsan,pie` @ `0x10a70000`) from the local module fixtures; the driver has never been
booted or run on the router. The companion plan in section 11 is the minimal path from patch
`009`'s partial GEMAC to a working netdev. Publication remains **NOT READY**
([../README.md](../README.md)); this document does not change that.

**Measured update 2026-11-18 (task-45, read-only):** the P3 register inventory has now been taken
on one stock vendor boot with the read-only inventory tool and analysed in
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md). Sections 4.5 and 5.4 record the measured
global/per-channel words, the active-channel model, the ring/channel configs and the live
post-overlay `rx_ring`/`tx_ring` values; where the measurements differ from the base-DT-derived
model the text below is corrected and the delta is called out. PIE still has no booted driver and
no write to the block has ever been made from our side. [V] measured, single boot, no writes.

**Decode update 2026-11-19 (task-56, lane `pie-decode`): the RX completion descriptor and the
normal non-TSO TX descriptor are decoded, and patch `024` implements them.** The v250 descriptor
layout in section 5.2 was recovered by disassembling the vendor `hi_kpie.ko` functions
`hi_pie_rx_get_desc`, `hi_pie_drv_rx_recv`, `hi_net_rx_proc`, `hi_pie_tx_send`,
`hi_pie_tx_set_desc` and its type-0 descriptor helper, plus the consumer arithmetic of the ring
pointers. The driver now decodes length/status/port from an RX completion, delivers the frame to
the net_device that registered the port and re-arms with a fresh buffer; its TX path writes the
full recovered normal-descriptor shape and rings the doorbell with the masked ring index. None of
this has run on hardware: every write is still behind `luofu_pie.arm=1`, and section 10 lists
what the decode does not settle.

Read [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 1 and section 6 first: they establish
*that* the wired data path is PIE and *what* was missing. This document supplies the missing
register/descriptor/channel model and the implementation plan, and corrects one factual claim in
that planning document (section 12.1).

**Evidence markers** are the repository convention ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
section 0.1): **[V]** verified from a named local artifact; **[I]** inferred/planned conclusion or
design decision; **[U]** unknown/unmeasured (do not design around it silently); **[X]** blocked on
an external dependency. All vendor-module citations are **read-only disassembly/symbol-table reads**
of parent-repository fixtures; the fixture bytes are not copied or committed.

**The fixture.** `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kpie.ko` (68,524 B) and
`parent:rootfs-2.5.24/lib/hisilicon/ko/hi_kpie.ko` are byte-identical
(sha256 `333cb968eed464ebab89636fa31d9db43db2b5c2766417d3e380e4a827590072`); same for
`hi_kmac.ko` (75,116 B, sha256 `3936719cfb403936c996ede7fefcc446e91cfc3d5d00a0369d1cb459fe31b544`).
`nm` reports **403 symbols** for `hi_kpie.ko`, matching the count in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
section 6.2. Toolchain: `build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-{nm,readelf,objdump}`;
raw dumps are kept (gitignored) under
`parent:build/linux-kbuild/waves/2026-11-18-pie-datapath-re/` (commands in section 13.2).

---

## 1. TL;DR

1. **The MAC does not own the packet path, but it does own the net_device.** `hi_kmac.ko` calls
   `alloc_etherdev_mqs()`, `hi_net_dev_probe(dev, &desc8)` and `register_netdev()`; its
   `ndo_start_xmit` tail-calls `hi_net_dev_xmit(skb, dev + 0x5a8)`. `hi_kpie.ko` owns the DMA
   rings, the NAPI instances and the per-port context table; its own netdev (`"cpu%d"`) is a
   private, **never-registered** NAPI container. See section 2. [V]
2. **The board runs the v250 hardware generation.** The vendor DT pie node carries
   `hwver = <0x250>` (missing from [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 6.1); the
   driver selects its v250 ops table (recovered at `.rodata+0x118`) for `0x250 <= hwver < 0x300`.
   [V]
3. **Register model (recovered):** per-channel TX block at `0x2000 + ch*0x10`, RX block at
   `0x3000 + ch*0x10`; doorbells are writes to `+0x08` (TX/RX write pointer), completion pointers
   are `+0x0c` (16-bit read pointer); RX interrupt status `0x0300 + ch*4`, mask
   `0x0400 + ch*4`; global enable `0x3200` bit 0. Full table in section 4. [V] offsets, some
   field semantics [I].
4. **Ring geometry from the live board DT:** the *base* DT carries `rx_ring = <0xa 0x800 ...>`
   (four pairs) and `tx_ring = <0xc 0x4>`, but the **live post-U-Boot-overlay node the stock driver
   actually parses** carries `rx_ring = <0x8 0x600 0x8 0x600>` (two pairs) and
   `tx_ring = <0xc 0x2>` - measured 2026-11-18 (section 5.4). The driver allocates descriptor
   rings as `32 B << N` and computes descriptor count `1 << N`, with `N` taken from the first
   word of each ring pair, and uses the second word as the RX buffer size (clamped to <= 9600 B,
   default 2048) / TX channel count. The readback of the programmed channel-config registers
   confirms the live first words: TX `0x2004=0xc`, RX `0x3004=0x8`. Descriptor stride is 32
   bytes; the second-word/0x3504 semantics are still [I] (see section 5.4). [V] parse and
   allocation code + live readback, [I] decoding of the size word.
5. **Four interrupt vectors do not mean four channels.** The driver requests 4 IRQs
   (SPI 88-91) whose per-vector device data names two channel indices each -
   `pie-ch0,7`, `pie-ch1,6`, `pie-ch2,5`, `pie-ch3,4` - over an 8-channel slot space; 16 NAPI
   instances exist for the 16-channel hardware generation. This refines the "4 channels/IRQs"
   summary in [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 6.1. [V] names/counts,
   [I] the 4-wired + 4-WoC channel split. Measured correction (2026-11-18): on the stock wired
   boot only channels 0 and 1 are configured (the live overlay sizes 2 RX pairs / 2 TX channels),
   so only the first two vectors' wired channels do work here; the 4 vectors are a hardware
   property, not the live channel count (section 5.4).
6. **Egress port/qid is a cross-module contract, not a PIE register.** A netdev TX resolves its
   port context through `g_api_priv[(port<<8) | vport]`, then hands a per-port queue handle (put
   there by the LSW's queue hook) plus a port descriptor to the PIE TX engine. The switch side
   programs port->qid tables (`hi_lsw_pfe!port_qid_map_set/qid_map_set`). The PIE TX channel
   itself is chosen by CPU id, not by egress port. Section 7. [V] code shape, [U] table addresses.
7. **DMA is coherent-only in the vendor design** (`dma-coherent` property, coherent
   `dma_alloc_attrs`/`dma_map_page_attrs` rings, no cache maintenance). The ACP block is the
   vendor's coherency programming path; whether 0x250 needs it (the ACP call recovered from the
   module is in the **v300** register-init path only) is [U] and bench-only. Section 8.
8. **Nothing may touch the PIE MMIO on hardware before an attended, recovery-armed step.**
   Patch `009`'s header records that an access to this block before it was ready hung the board's
   AHB bus; there is no restart path ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 7.3).
   The read-only inventory has since been taken (P3 above) and patch `020` implements the ring
   engine, but its arm path is gated behind an explicit `luofu_pie.arm=1` command-line opt-in so
   a normal boot still never touches the block. Section 12.2. [V]
9. **Both descriptors are decoded and patch `024` implements them.** RX completion: u16 frame
   length at +0, status/error code at +8 bits 6:0 (0 = no error), port/vport at +16 (or the packed
   +23/+24 fields), tag class at +10 bits 5:4, second flag class at +13 bits 6:5 [U]. TX normal
   non-TSO: u16 length at +0, valid/class/offload byte at +3, DMA address at +4, port tag
   (`port | 1<<port`) at +8, type-0 control words at +11..+19. The ring pointer words are ring
   indices: the TX doorbell is the masked next-free index and completion is poll-only; the RX
   pointer word packs the last-produced and "sf" indices. Full tables in section 5.2. [V] code,
   [U] hardware acceptance. Section 11 status block.

---

## 2. Module layering: what owns what

### 2.1 Measured layering

| artifact | measured facts |
| --- | --- |
| `hi_kmac.ko` (75,116 B; `depends=hi_kpie,hi_basic,ksecurec`; aliases `hsan,mac`/`hsan,himac`; description "Hisilicon Ethernet MAC controller") | Imports `alloc_etherdev_mqs`, `register_netdev`, `free_netdev`, `phylink_*`, `hi_net_dev_probe`, `hi_net_dev_open`, `hi_net_dev_close`, `hi_net_dev_remove`, `hi_net_dev_xmit`. It imports **no** `dma_*`, `napi_*` or ring code. `hi_mac_probe` (defined in `.text.unlikely` at offset `0x0`, 1800 B) calls `alloc_etherdev_mqs(sizeof_priv = 0xb0, 1, 1)` at `+0x5c`, builds the 8-byte port descriptor and calls `hi_net_dev_probe(netdev, &desc8)` at `+0x474`, then `register_netdev()` at `+0x4a0`. `hi_mac_net_xmit` at `0x4300` is 12 bytes: `add r1,r1,#0x5a0; add r1,#8; b hi_net_dev_xmit` (the tail call passes `dev + 0x5a8`). |
| `hi_kpie.ko` (68,524 B; `depends=ksecurec,hi_basic`; alias `of:N*T*Chsan,pie`; description "Hisilicon Pie Driver") | Defines the ring/DMA/NAPI engine and the per-port context table: `hi_pie_tx_send@0x3250`, `hi_pie_drv_tx_init@0x3614`, `hi_pie_drv_rx_recv@0x3870`, `hi_pie_drv_rx_addbuf@0x3a24`, `hi_pie_drv_rx_kick@0x3c5c`, `hi_pie_drv_rx_init@0x3ca0`, `hi_pie_rx_interrupt@0x3f88`, `hi_pie_rx_poll@0x4058`, `hi_pie_drv_xmit@0x42ac`, `hi_pie_drv_init@0x4378`, `hi_net_dev_probe@0x5eec`, `hi_net_dev_xmit@0x637c`, `hi_net_rx_proc@0x6568`, plus the four per-generation register-op tables (v100/v200/v250/v300). Its probe allocates a *private* netdev named `"cpu%d"` (`alloc_netdev_mqs(0, "cpu%d", 0, ether_setup, 1, 1)` at `hi_pie_probe+0x1d0`) and never registers it - `register_netdev` is not an import; the netdev is the container `netif_napi_add()` is called on. |
| `hi_kmac.ko` exported symbols (`__ksymtab_strings`) | `hi_net_mii_register`, `hi_net_mii_unregister`, `hi_reg_hw_spi_read`, `hi_reg_hw_spi_write` only - the MAC exports PHY-register helpers, not the datapath. |
| `hi_kpie.ko` exported symbols (`__ksymtab_strings`) | `hi_net_dev_probe/remove/open/close/xmit/xmit_lock`, `hi_net_get_port/get_dev`, `hi_net_{rx,tx}_hook/unhook`, `hi_net_queue_hook`, `hi_net_team_hook/add/del`, `hi_net_dev_rx_hook`, `hi_pie_get_{gro,woc}_ch_{start,num}`, `hi_net_port_is_same`, and the WoC/spec-xmit helpers (`hi_net_woc_*`, `hi_net_spec_xmit`, `hi_woc_skb_flush_by_cpu`). This is the in-kernel API the MAC and the LSW consume. |
| vendor init order (`parent:rootfs-2.4.15/etc/rc.d/luofu` lines 28-54) | `hi_kpinctrl` -> ... -> `hi_sysctrl` -> `hi_acp` -> **`hi_kpie`** (then four `/proc/irq/*/smp_affinity` pins, `pie-ch0..ch3` -> 1/2/1/2) -> `hi_lsw_pub/dp/pfe` -> `hi_kmdio` -> `hi_kphy` -> **`hi_kmac`** -> `hi_kteam` -> `hi_woc_wifi_rtx`. PIE is initialized before the LSW and before any MAC. [V] |

The resulting data path is:

```
GEMAC port (phylink/config only)  <->  internal LSW (forwarding, queues, qid/port map)
                                        ^
                                        |  queue handle + port descriptor
                                        v
   per-port netdev (owned/registered by hi_kmac) -> hi_net_dev_xmit -> PIE rings/DMA/NAPI -> DDR
```

The MAC is a phylink front end; PIE is the packet engine; the LSW is the egress/queue glue.
This is the vendor shape and it is the shape a clean-room port must reproduce: **patch 009 cannot
be completed by adding rings inside the MAC driver**, because the MAC has no ring-register or DMA
knowledge at all (section 2.2 of [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)). [V]

### 2.2 The port descriptor (`desc8`) contract

`hi_mac_probe` builds an 8-byte descriptor from netdev-private fields and passes it to
`hi_net_dev_probe`; the PIE TX path receives the same structure as its second argument. The layout
recovered from both sides is:

| offset | field | evidence |
| --- | --- | --- |
| +0 | `u8 port` | `hi_mac_probe` writes the DT `port` value into the netdev-private area at `netdev+0x5a8` (the pointer the xmit path passes to PIE), and `hi_net_dev_probe@+0x34` reads byte 0, rejecting `port > 31`; `hi_pie_drv_xmit@+0x38` copies descriptor bytes 0-3 into the TX request. [V] |
| +1 | `u8 vport` | same code path, byte 1; used as the low byte of the `g_api_priv` index. [V] |
| +2..3 | reserved (zero) | the descriptor word is stored into the context entry; the vendor's `vport` property defaults to 0 and the DT carries no vport for this board. [I] |
| +4..7 | `u32` opaque per-port word | copied into context entry+16; the board DT does not set it and `alloc_etherdev_mqs()` zeroes the private area, so it is 0 on this board. [V] code, [I] value |

`hi_net_dev_probe` keys the global table as `g_api_priv[(port<<8) | vport]` (array of 64-byte
entries, `kmalloc(64)`; the table itself is `g_api_priv` = `.bss+0x1c18`, size `0x803c`), stores
the netdev at entry+8, the descriptor word at entry+12 and the opaque word at entry+16, and calls
the registered queue hook (section 7). [V]

---

## 3. Hardware window and variant selection

**Window:** `0x10a70000-0x10a7ffff` (64 KB), from the pinned vendor DTS
([soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts), lines 1253-1269). The target DTS already
carries a placeholder node ([../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts),
`pie@10a70000`, currently `status = "disabled"` with no resets/hwver). [V]

**DT facts on the board node** ([soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines
1253-1269; [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8):

| property | value | meaning |
| --- | --- | --- |
| `compatible` | `"hsan,pie" "hsan,acp-pie"` | the second string is the ACP block's binding, not a driver compatible |
| `reg` | `<0x10a70000 0x10000>` | 64 KB window [V] |
| `interrupts` | SPI 88, 89, 90, 91 | four vectors [V] |
| `dma-coherent` | present | coherent-DMA declaration [V] |
| `hwver` | `<0x250>` | selects the v250 descriptor/register generation [V] |
| `clocks` | one clock (the CRG gate `pie_clk0`, id 15, gate1 bit 0x19 in patch `004`) | [V] |
| `resets` | `(0x30, 0x19)` in the vendor CRG cell form | [V] vendor DT; the target CRG binding uses the same two-cell form ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1; [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 0.3) |
| `rx_ring` / `tx_ring` / `woc_rx_ring` / `woc_tx_ring` | base DT: `<0xa 0x800 ...>` (4 pairs) / `<0xc 0x4>` / same-as-rx / same-as-tx. **Live (after the U-Boot overlay, measured 2026-11-18): `<0x8 0x600 0x8 0x600>` (2 pairs) / `<0xc 0x2>`; `woc_*` live values not read** | driver ring sizing data; the live values are what the stock driver parses (section 5.4) |

**Variant selection is recovered.** `hi_pie_probe` reads the DT `hwver` (property name string at
`.rodata.str1.4+0x5c0`) and passes it to `hi_pie_drv_init`; the dispatch at
`hi_pie_drv_init+0x3c` is a four-way threshold:

| hwver | ops table | getter |
| --- | --- | --- |
| < `0x200` | `.rodata+0x0` via `.LANCHOR0` | `hi_pie_get_hw_ctrl_v100@0x910` |
| `0x200..0x24f` | `.rodata+0x60` via `.LANCHOR0` | `hi_pie_get_hw_ctrl_v200@0xdec` |
| `0x250..0x2ff` | `.rodata+0x118` | `hi_pie_get_hw_ctrl_v250@0x1878` |
| >= `0x300` | `.rodata+0x1d0` | `hi_pie_get_hw_ctrl_v300@0x2744` |

So **the board uses the v250 table** (`0x250` falls in row 3). All offsets in section 4 are from
that table unless stated. [V]

**The ops table** (v250, `.rodata+0x118`, 0x60 bytes; every entry verified by `R_ARM_ABS32`
relocation in `.rel.rodata`):

| offset | field | v250 target | notes |
| --- | --- | --- | --- |
| +0x00 | `u32` | `0x20` | RX ring descriptor-allocation unit (bytes) - used as `dma_alloc(size = unit << N)` [V value, I meaning] |
| +0x04 | `u32` | `0x20` | TX ring descriptor-allocation unit (bytes) [V value, I meaning] |
| +0x08 | function | `hi_pie_reg_init@0x148c` | global + per-channel register init |
| +0x0c | function | `hi_pie_rx_init_chan@0x1378` | program one RX channel |
| +0x10 | function | `hi_pie_tx_init_chan@0x1334` | program one TX channel |
| +0x14 | function | `hi_pie_rx_enable@0xe60` | global RX enable bit |
| +0x18 | function | `hi_pie_rx_open_irq@0xe20` | mask register: 0 = enabled |
| +0x1c | function | `hi_pie_rx_close_irq@0xe0c` | mask register: 1 = masked |
| +0x20 | function | `hi_pie_rx_get_irq_en@0xe34` | returns 1 when enabled |
| +0x24 | function | `hi_pie_rx_get_irq_sta@0xe4c` | status bit 0 |
| +0x28 | function | `hi_pie_rx_clr_irq@0xdf8` | write 1 to clear |
| +0x2c | function | 0 for v250 | `rx_set_gro_len` exists only in v300 (`.rodata+0x1fc`) |
| +0x30 | function | `hi_pie_rx_set_wptr@0xe74` | 16-bit write pointer in bits 0-15 |
| +0x34 | function | `hi_pie_rx_get_rptr@0xe94` | 16-bit read pointer |
| +0x38 | function | `hi_pie_rx_set_sf_rptr@0xeac` | 16-bit "sf" pointer in bits 16-31 |
| +0x3c | function | `hi_pie_tx_set_wptr@0xecc` | full 32-bit write pointer (TX doorbell) |
| +0x40 | function | `hi_pie_tx_get_rptr@0xee4` | 16-bit read pointer (TX completion) |
| +0x44 | function | `hi_pie_rx_set_owner@0xefc` | descriptor byte 3 bit 2 |
| +0x48 | function | `hi_pie_rx_set_addr@0xf0c` | descriptor u32 at +4 |
| +0x4c | function | `hi_pie_rx_get_desc@0xf14` | HW RX descriptor -> 12-byte software summary |
| +0x50 | function | `hi_pie_tx_get_hw_feature@0x1328` | returns `0x00110013` (TX offload feature mask) |
| +0x54 | function | `hi_pie_tx_get_len@0xfd8` | u16 at software descriptor +0 |
| +0x58 | function | `hi_pie_tx_get_addr@0xfe0` | u32 at software descriptor +4 |
| +0x5c | function | `hi_pie_tx_set_desc@0x11ac` | build one hardware TX descriptor |

The same table shape exists for v100/v200/v300 at the other three addresses; the v300 table adds
`+0x2c` (`rx_set_gro_len@0x1f24`) and its register-init function also invokes the ACP notifier
(section 8). [V]

---

## 4. Register model

All offsets are from the PIE window base (`0x10a70000`). "ch" is a channel index. Register access
is plain MMIO (the v250 write helpers use `dmb ishst`; the v100 helpers use `arm_heavy_mb()` and
`dsb`). [V]

### 4.1 Ring blocks

| offset | width | access | meaning |
| --- | --- | --- | --- |
| `0x2000 + ch*0x10 + 0x00` | 32 | W | TX ring base address (DMA) |
| `0x2000 + ch*0x10 + 0x04` | 32 | W | TX channel config word (driver writes the ring-size shift `N`) [V write, I meaning] |
| `0x2000 + ch*0x10 + 0x08` | 32 | W | **TX write pointer / doorbell** (full 32-bit write; adding descriptors and writing this starts transmission) |
| `0x2000 + ch*0x10 + 0x0c` | 32 | R | TX read pointer (completion), low 16 bits used |
| `0x3000 + ch*0x10 + 0x00` | 32 | W | RX ring base address (DMA) |
| `0x3000 + ch*0x10 + 0x04` | 32 | W | RX channel config word (ring-size shift `N`) [V write, I meaning] |
| `0x3000 + ch*0x10 + 0x08` | 32 | R/W | RX write pointer (bits 15:0) and "sf" pointer (bits 31:16) |
| `0x3000 + ch*0x10 + 0x0c` | 32 | R | RX read pointer (completion), low 16 bits used |

Evidence: `hi_pie_rx_get_rptr@0xe94` reads `base+0x300c+ch*0x10` and `uxth`; `hi_pie_rx_set_wptr@0xe74`
does a read-modify-write of the same word with `bfi #0,#16`; `hi_pie_rx_set_sf_rptr@0xeac` writes the
upper half; `hi_pie_tx_set_wptr@0xecc` stores a full word at `base+0x2008+ch*0x10`;
`hi_pie_tx_get_rptr@0xee4` reads `base+0x200c+ch*0x10` and `uxth`; the init-channel functions
write base/config/zero-wptr at the corresponding `+0x00/+0x04/+0x08` offsets. [V]

### 4.2 Interrupt registers

| offset | width | access | meaning |
| --- | --- | --- | --- |
| `0x0300 + ch*4` | 32 | R/W1C | RX interrupt status, bit 0. `rx_get_irq_sta` returns bit 0; `rx_clr_irq` writes 1. |
| `0x0400 + ch*4` | 32 | R/W | RX interrupt mask: 1 = masked. `rx_open_irq` writes 0, `rx_close_irq` writes 1; `rx_get_irq_en` returns `!bit0`. |

[V] `hi_pie_rx_get_irq_sta@0xe4c` / `clr@0xdf8` / `close@0xe0c` / `open@0xe20` / `get_irq_en@0xe34`.
The IRQ is acknowledged by mask + write-1-to-clear (the hard handler masks and clears; poll
completion re-opens the mask). [V]

### 4.3 Global / per-channel control

**`0x1000` - global control.** `hi_pie_reg_init` reads `0x1000`, clears bit 0, and writes bits 6-8
(`0x1c0`) set. Every other bit is preserved. [V]

**`0x3200` - RX enable / mode.** Bit 0 is the global RX enable (`hi_pie_rx_enable@0xe60` does
`bfi #0,#1` on it, and the init path re-writes it). `hi_pie_reg_init` also programs a per-channel
group in the vicinity:

| offset | value written by `reg_init` (v250) | notes |
| --- | --- | --- |
| `0x3210 + ch*4` | `3` | per-channel, unidentified [U] |
| `0x3290 + ch*4` | `2` | per-channel, unidentified [U] |
| `0x3310 + ch*4` | `8` | per-channel, unidentified [U] |
| `0x3204` | `0` | [U] |
| `0x3208` | `0x7d` (125) | [U] |
| `0x3200` bits 0-7 | `(old & 0xfc)` with bit 1 set iff `ch_num == 1` | "single-channel" mode encoding [I] |
| `0x0180 + ch*4` | `1` | per-channel, unidentified [U] |
| `0x0200 + ch*4` | `1` | per-channel, unidentified [U] |
| `0x0380 + ch*4` | `1` | per-channel, unidentified [U] |
| `0x0400 + ch*4` | `1` | RX interrupt mask (section 4.2) - all channels start masked |
| `0x3404` | `0` initially | 2 bits per channel, `cfg[16] & 3`, programmed per channel by `rx_init_chan` [V write, I meaning] |
| `0x3408` | `0` initially | per-channel bitmap: bit ch is set by `rx_init_chan` when the channel's ring size is > 9600 B [V write, I meaning] |
| `0x3700` | a driver-provided size value minus 1, written only when a GRO ring is configured | the value comes from the controller's ring/config block; not established for the board (DT has no `gro_ring`) [U] |

**`0x3500 + ch*0x10 + 0x04` - RX ring-size configuration.** `rx_init_chan` computes
`ring_size >> 7` and writes it there (the code has a second layout when the hardware reports 16
channels: then the same value goes into `0x3200` bits 16-25). [V] offset/encode, [I] "size in
128-byte units". The board uses the `0x3504 + ch*0x10` layout because the channel count is 8, not
16 (the driver forces 16 only for `hwver == 0x260`). [V]

**`0x2200`.** Clear-then-set of bit 21 appears in the DFX counter path
(`hi_pie_dfx_write_chl_cnt`); this is a diagnostic register, not needed for the data path. [V
offset, U meaning]

### 4.4 What is driver-private (not hardware)

The vendor driver keeps: a controller block at `.bss+0` (`g_ctrl`) with the ops-table pointer at
+0x588, per-channel state arrays, a 16-entry NAPI array at `.bss+0x598`, the 4-entry IRQ table in
`.data`, `g_rx_queue` at `.bss+0x1504` and the per-port context table `g_api_priv` at
`.bss+0x1c18`. These offsets are **driver-private**, useful only to understand the vendor module; a
clean-room driver does not need to reproduce them. [V]

### 4.5 Measured register state on the stock vendor boot (2026-11-18) [V]

Read-only inventory of the running vendor system (one boot, repeat=2, every value identical in
both passes; evidence `parent:build/bench-inventory/20261118T130512Z-inv1-run/`, analysis in
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 2). Only offsets that were in the
allowlist are listed; ch4-7 ring blocks were **not** read (the P3 plan below reads ring blocks
for ch0-3 only).

| offset | value | reading |
| --- | --- | --- |
| `0x1000` | `0x000a01c8` | bit0 clear, bits6-8 set - matches the reg_init model (check pass) |
| `0x2200` | `0x0045e000` | raw; meaning [U] |
| `0x3200` | `0x00002001` | bit0 global RX enable set (also bit13 set) |
| `0x3204` / `0x3208` | `0x0` / `0x7d` | match the reg_init write values |
| `0x3404` | `0x00001b04` | raw; the "2 bits per channel" [I] model is not contradicted but not decoded |
| `0x3408` | `0x0` | no channel has a ring over 9600 B (matches) |
| `0x3700` | `0x0000ffff` | **not zero/unwritten although the board DT has no `gro_ring`** - either the loader or an unconditional reg_init wrote it; semantics [U] |
| `0x0180/0x0200/0x0380 + ch*4` | `1` each, for **all** ch0-7 | reg-init per-channel defaults; written for all 8 slots, not only wired ch0-3 |
| `0x3210/0x3290/0x3310 + ch*4` | `3` / `2` / `8` each, for **all** ch0-7 | reg-init per-channel defaults; field meanings [U] |
| `0x0300 + ch*4` RX IRQ status | `0` for ch0-7 | idle boot, nothing pending at sample time |
| `0x0400 + ch*4` RX IRQ mask | ch0=0, ch1=0, ch2=1, ch3=1, ch4-7=0 | configured channels unmasked; unconfigured ch2/ch3 masked. The 0 on ch4-7 does not fit the "all start masked" write model - raw value recorded, [U] |
| `0x3504/0x3514/0x3524/0x3534` | `0xc` each | see 5.4 - tracks the live size word (0x600>>7), not the descriptor-ring byte size |

---

## 5. Rings, channels, descriptors

### 5.1 Ring geometry

The probe parses the DT ring properties through `of_property_read_variable_u32_array()` and
validates them before ring allocation: [V]

| property | parse | validation |
| --- | --- | --- |
| `rx_ring` | up to 32 u32; the returned element count is halved into pair count (an odd count is silently rounded down); pair count at ctrl+0x20 | each second word (the per-channel size word) clamped: if > 9600 (`0x2580`) the probe logs `"mac mtu(%d) invalid"` and forces 2048. The DT values are `{0xa, 0x800}` x4 - 2048 is inside the limit. |
| `gro_ring` | same pair form, appended to the RX channel count | optional; absent on this board |
| `tx_ring` | 1-2 u32 (the board has one pair) | second word must be <= 15 (`"ch_num err(%d)"`) - the DT value `0x4` gives 4 TX channels |
| `woc_rx_ring` / `woc_tx_ring` | WoC ring sizing | WoC (Wi-Fi offload) channels; **must not be initialized for the wired path** - a separate subsystem inside PIE [I] |

The allocation path then multiplies the ops-table descriptor unit (32 bytes, section 3) by
`1 << N` and allocates:
- RX: ring = `dma_alloc_attrs(dev, 32 << N)` coherent; per-descriptor software nodes `24 B << N`;
  data buffers `__netdev_alloc_skb_ip_align(size = cfg[12] + 128)` (the extra 128 is headroom).
- TX: ring = `dma_alloc_attrs(dev, 32 << N)`; per-descriptor software nodes `20 B << N`.

[V] code shape; the "N = first ring-pair word" assignment is [I] but is the only consistent reading
of the recovered code: the same word is programmed into the channel config register and shifted to
derive both the descriptor count and the ring byte size. With the DT values that means:
RX: `N = 0xa` -> 1024 descriptors, 32 KiB ring per channel, 2048-byte buffers;
TX: `N = 0xc` -> 4096 descriptors, 128 KiB ring per channel. [I]

The ring is not a special end-of-ring state: the channel's descriptor count `1<<N` is what the
driver uses to wrap the pointers (it masks them with `(1 << N) - 1` before comparing). [V code,
I hardware behaviour]

### 5.2 Descriptor format

Both rings use a **32-byte descriptor** (the ops-table unit; section 3). The recovered fields:

**RX completion descriptor (v250).** Device-written after completion; the driver pre-fills
only the buffer address and the owner bit.

| offset | width | field | evidence / consumer |
| --- | --- | --- | --- |
| +0 | u16 | **frame length** | `rx_get_desc@0xf14` copies this word to software summary +4; `hi_pie_drv_rx_recv@0x3870` calls `skb_put(skb, u16 summary+4)` |
| +2 | u16 | device-written; not read by the v250 decoder | no reader in `rx_get_desc`; leave unknown |
| +3 bit 2 | 1 | **owner** - driver sets 1 when arming; the engine clears it on completion [I direction] | `rx_set_owner@0xefc` `bfi #2,#1`; `rx_get_desc` does not test it (completions are consumed by the read pointer) |
| +4 | u32 | buffer DMA address (driver-written) | `rx_set_addr@0xf0c`; `hi_pie_drv_rx_addbuf` maps the skb data and calls it |
| +8 bits 6:0 | 7 | **status/error code**, 0 = no error | `rx_get_desc` `ubfx #0,#7` -> summary +6; bit 0 of summary +7 is set to 1 iff this field is zero; `hi_net_rx_proc@0x6568` drops the frame and bumps an error counter when bit 0 is clear |
| +8 bit 7 | 1 | not copied by the v250 decoder | - |
| +9 bits 7:4 | 4 | port, used on the error path | `rx_get_desc` error branch |
| +10 bits 5:4 | 2 | **tag class flag** | `rx_get_desc` -> summary +7 bits 4:3; `hi_net_rx_proc` lifts the u16 words at frame +12/+14 into vendor-private skb fields when set. Exact enum [U]; the tag stays in the frame |
| +13 bits 6:5 | 2 | **status flag class 2** | `rx_get_desc` -> summary +7 bits 2:1; no consumer in `hi_kpie.ko`; [U], possibly checksum-related |
| +16 | u32 | normal path: **port = byte 0, vport = byte 1** | `rx_get_desc` `[hw+16] != 0` branch; `hi_net_rx_proc` keys `g_api_priv[(port<<8) + vport]` (the two bytes are disjoint) |
| +18 | u16 | normal path: second summary word | copied to summary +2 |
| +20 bits 3:0 | 4 | no-status path: low nibble of summary +2 | `rx_get_desc` `[hw+16] == 0` branch |
| +23 bits 7:4 | 4 | no-status path: **port** | same branch |
| +24 bits 4:0 | 5 | no-status path: **vport** | same branch |
| +24 bits 6:1 | 6 | no-status path: upper bits of summary +2 | packed with the +20 nibble |

The 12-byte software summary is therefore fixed [V]: `+0` port, `+1` vport, `+2..3` u16,
`+4..5` **frame length**, `+6` status code, `+7` flags (bit 0 = "no error", bits 2:1 and
bits 4:3 = the two flag classes above). `hi_pie_drv_rx_recv` unmaps the buffer and applies
`skb_put(length)`; `hi_net_rx_proc` then checks bit 0, looks up `(port, vport)` in
`g_api_priv`, reads the tag class, runs `eth_type_trans()` and delivers with `netif_rx()`.
The vendor does **not** report a Linux checksum verdict from bits 2:1 in this module, so the
clean driver keeps `CHECKSUM_NONE` and reports no checksum offload. [V] decode, two flag
classes still [U] semantically.

**TX descriptor (v250, driver-written).** The table is the complete byte image the vendor
builds for one **normal non-TSO** frame descriptor, recovered from `hi_pie_tx_set_desc@0x11ac`
plus its type-0 helper at `0xfe8`; the TX request buffer is zeroed first, so every byte not
listed is 0. The vendor builds one descriptor per skb fragment; a clean driver that linearizes
the frame builds exactly this one descriptor.

| offset | width | value / field | evidence |
| --- | --- | --- | --- |
| +0 | u16 | **frame length** | `tx_set_desc` `ldrh [req+4]; strh [hw]` |
| +2 | u16 | 0 in the normal branch (the TSO branch packs header offsets here) | normal branch does not write it |
| +3 bit 2 | 1 | **owner/valid** = 1 | `orr r2,r2,#4` |
| +3 bit 4 | 1 | **normal-descriptor class** = 1 | `orr r2,r2,#0x10` |
| +3 bits 6:5 | 2 | class selector, 3 for a single linear buffer (2 when fragments follow) | `((req[7] & 2) + (req[7] & 1)) << 5`; the request byte is 2 plus `clz(nr_frags)>>5` (0 or 1) |
| +4 | u32 | buffer DMA address | `ldr [req+20]; str [hw+4]` |
| +8 bits 3:0 | 4 | **egress port number** | type-0 helper `bfi r3, req[0], #0, #4` |
| +8 bits 26:11 | 16 | **egress port bitmap**: `1 << port` | type-0 helper `bfi r3, (1 << (req[0] & 0xf)), #11, #16` |
| +11 bits 4:3 | 2 | 2 | `tx_set_desc` `bfi ... #3,#2 = 2` |
| +11 bits 6:5 | 2 | 1 | type-0 helper `bfi 1, #5,#2` |
| +12 bits 6:3 | 4 | 4 (type-0 control word selector) | type-0 helper `bfi 4, #3,#4` |
| +12 bit 2 | 1 | 0 for a frame without offload (from request byte) | helper |
| +12 bits 7:0 / +13 bits 4:0 | - | 0 (L2/L3/L4 offset words from the request; the normal request leaves them zero) | helper |
| +14 bit 0 | 1 | 1 (control word bits 16:15 = 2) | helper `bfi 2, #15,#2` of the +12 dword |
| +14 bits 6:1 | 6 | 0 (request byte) | helper |
| +15 bits 3:0 | 4 | 0 (fragment-count byte; only written when the request is non-zero) | helper |
| +15 bits 6:4 | 3 | 0 (from the request's queue/flags byte) | helper |
| +16 bits 3:0 | 4 | 0 (reserved descriptor byte 2) | helper |
| +17 | u8 | **vport** (bits 4:0) and reserved bits from descriptor byte 2 | helper |
| +19 bits 4:3 | 2 | copy of +3 bits 4:3 (= 2 for the normal descriptor) | `tx_set_desc` tail |
| +20..+31 | 12 | 0 | zeroed request buffer |

The descriptor type is selected by the **request byte at +9** (`& 3`): the type-0 helper path
above is the L2/normal one; type 1/2/3 write 8/9/6 into +12 bits 6:3 and shared the rest of the
same control block (not implemented here). A descriptor whose request byte at +7 has bits 6:4 =
0b010 takes the separate TSO branch, which writes +2 and a different +8..+19 control block; that
branch is decoded only at the branch level and is **not** implemented. The TX egress *queue*
handle the vendor carries through its per-port `g_api_priv[]` context is not visible in this
descriptor shape beyond the port tag; section 7 and section 10 keep that open. [V] fields,
[U] request metadata semantics.

**Ring producer/consumer semantics (v250).** [V] code shape, [I] the ring-index reading:

- **TX.** The driver owns the write pointer; it is a ring index. After writing a descriptor the
  vendor rings the doorbell at `0x2008 + ch*0x10` with `(last produced index + 1) & ring mask`,
  i.e. the index of the next free descriptor, and the engine transmits up to it. Completion is
  poll-only: the 16-bit read pointer at `0x200c + ch*0x10` is the ring index of the next
  descriptor to complete; the vendor masks it with the ring mask and reclaims/frees every
  descriptor before it. There is **no TX completion interrupt**.
- **RX.** The pointer word at `0x3008 + ch*0x10` has two 16-bit halves: bits 15:0 are the index
  of the **last descriptor the driver handed to the engine** (the vendor writes the last
  *produced* index, not the next one, when it arms a ring), bits 31:16 are the "sf" pointer, the
  index of the **first buffer still outstanding**, which the vendor updates after every
  completion. The engine's read pointer at `0x300c + ch*0x10` is the ring index of the next
  descriptor to complete; the driver consumes completions in ring order from its own consumer
  index and re-arms each slot.

### 5.3 Channel model

- **8 channel slots** in the driver's global "number of PIE channels" variable (`.data+0`, initial
  value 8, read by `rx_init_chan/tx_init_chan/reg_init` for bounds). A 16-channel mode exists:
  `reg_init` forces 16 when `hwver == 0x260` and changes the ring-size register layout. [V]
- **4 RX channels and 4 TX channels configured on this board:** `rx_ring` has 4 pairs and the
  parser's channel count (ctrl+4) is 4; `tx_ring`'s second word is 4 and becomes the TX channel
  count (ctrl+5). [V]
- **4 interrupt vectors** (SPI 88-91) map to channel pairs: the per-vector driver data names
  `pie-ch0,7`, `pie-ch1,6`, `pie-ch2,5`, `pie-ch3,4` - i.e. each IRQ services channel `i` and
  channel `7-i`. [V] names/count, [I] the reading "4 wired + 4 WoC channels, one of each per
  vector"; the trailing digit in the vendor name strings is unexplained but consistent with this
  pairing.
- **16 NAPI instances** (`netif_napi_add` called 16 times over `.bss+0x598 + n*0xd8`); the NAPI
  index is the channel index, and the default weight 64 comes from an optional DT property
  `rx_poll_weight` (string at `.rodata.str1.4+0x4d4`). This is sized for the 16-channel hardware
  generation; only channels 0-3 do wired RX work on this board. [V]
- **TX channel selection is per-CPU, not per-port.** `hi_pie_tx_send` selects a TX channel
  index from a per-CPU value read out of the current thread_info area (the code masks the stack
  pointer to the kernel-stack base and loads a byte at +16), then requires
  `index < tx_ch_num` (4) and rejects with the `chan err %u` message otherwise. With 2 CPUs that
  is safe. [V] code, [I] that the value is the CPU index.
- The vendor init script pins the four IRQ affinities to CPUs 1/2/1/2
  (`parent:rootfs-2.4.15/etc/rc.d/luofu` lines 28-45). [V]

### 5.4 Measured ring/channel configuration (2026-11-18) [V]

The live post-overlay pie node is the configuration the stock driver parsed; the follow-up
readback of `/proc/device-tree/pie@0x10a70000/{rx_ring,tx_ring}` (raw big-endian bytes saved in
`parent:build/bench-inventory/20261118T130512Z-inv1-run/followup/`) gives:

    rx_ring = <0x8 0x600 0x8 0x600>     # 2 pairs: 2 RX channels, N=8, size word 0x600
    tx_ring = <0xc 0x2>                 # N=0xc, 2 TX channels

The measured programmed state matches that live node and settles the active-channel model:

| register | ch0 | ch1 | ch2 | ch3 |
| --- | --- | --- | --- | --- |
| TX base `0x2000 + ch*0x10` | `0x825c0000` | `0x82600000` | `0` | `0` |
| TX config `0x2004 + ch*0x10` | `0xc` | `0xc` | `0` | `0` |
| RX base `0x3000 + ch*0x10` | `0x82420000` | `0x82518000` | `0` | `0` |
| RX config `0x3004 + ch*0x10` | `0x8` | `0x8` | `0` | `0` |
| pointers `+0x08/+0x0c` | `0x639 / 0x06390639` (TX), `0xe600e5 / 0xe7` (RX) | analogues, nonzero | `0` | `0` |

The pointer values fit the ring sizes (TX 0x639 < 4096, RX read/write halves < 256) and are
consistent with the **ring-index** reading of section 5.2: the TX word is the write pointer, the
TX read word carries the read pointer in bits 15:0; the RX word packs the write pointer (bits
15:0) and the "sf" pointer (bits 31:16), and the RX read word is the 16-bit read pointer. They
moved between the two inventory passes, so they are live counters, not static configuration. [V]
raw values, [I] the ring-index reading.

Consequences for the model in 5.1/5.3:

- **The ring-pair first word is the programmed channel config** (TX `0xc` = live `tx_ring[0]`;
  RX `0x8` = live `rx_ring[0]`). This closes the P3 question "do the ring pair first words really
  encode N?" as [V] for the live values; the descriptor-count arithmetic `1 << N` stays [I]
  (still not observable from a register value alone).
- **The live board is a 2 RX + 2 TX channel configuration**, not 4+4. Ring sizes implied by the
  code shape: RX 32 B << 8 = 8 KiB ring, 256 descriptors; TX 32 B << 12 = 128 KiB ring, 4096
  descriptors. The base-DT 4-pair/4-channel configuration is superseded by the U-Boot overlay
  (section 3). The 8 channel slots and 4 IRQ vectors are unchanged hardware facts.
- `0x3504 + ch*0x10` reads `0xc` on all four ch0-3 addresses (including the unconfigured
  ch2/ch3, whose ring/config registers are zero). `0xc` equals the live size word `0x600 >> 7`;
  it does **not** equal the descriptor-ring byte size `0x2000 >> 7 = 0x40` and not the base-DT
  `0x100`. [I] the register tracks the per-channel size word in 128-byte units (or a
  global/mirrored copy of it); the exact semantic and why inactive channels read the same value
  remain [U]. Do not use it as a per-channel ring-size register until that is settled.
- P3 is **closed** for the inventory questions in section 10 that are register-readable on one
  stock boot: every read returned a plausible value, static words were stable, the programmed
  configs match the live DT, and the dynamic pointers moved. P3 is **not** a green light for
  writes or for a traffic claim: the untouched [U] rows below (descriptor bit maps, RX status
  semantics, behavior under traffic, ACP, pre-Linux state) remain, and the ch4-7/WoC ring state
  was outside the run's allowlist.

---

## 6. TX and RX flows (as implemented by the vendor module)

### 6.1 TX

1. The kernel calls the MAC netdev's `ndo_start_xmit` (`hi_mac_net_xmit@0x4300`), which tail-calls
   `hi_net_dev_xmit(skb, dev + 0x5a8)` (the pointer to the port descriptor). [V]
2. `hi_net_dev_xmit@0x637c` looks up `g_api_priv[(port<<8) | vport]` from the descriptor (with a
   fallback to the port's `vport = 0` entry); a missing entry or a cleared up-flag goes to the
   accounted drop/TX_BUSY path, and the TX-hook list is consulted before the packet is handed to
   the engine. [V]
3. `hi_pie_drv_xmit@0x42ac` builds a 32-byte request from the skb fields + descriptor (port,
   vport, queue handle) and calls `hi_pie_tx_send(skb, request, g_ctrl)`. [V]
4. `hi_pie_tx_send@0x3250` selects the per-CPU channel, reads the TX read pointer
   (`tx_get_rptr`), reclaims completed descriptors below it, maps the skb/data pages coherently,
   fills one or more 32-byte TX descriptors (one per fragment, section 5.2), bumps the software
   write index, and finally writes the TX write pointer register (`tx_set_wptr` = doorbell at
   `0x2008 + ch*0x10`) with `(last produced index + 1) & ring mask`. The reclaim walk stops at
   the used-list node whose software ring index equals the masked read pointer, i.e. everything
   before the read pointer is complete. [V]
5. The 32-byte request passed to the descriptor builder is a different structure from the
   hardware descriptor: `hi_pie_drv_xmit@0x42ac` fills its first bytes with the 8-byte port
   descriptor (port, vport, reserved word), the mapped DMA address and a per-frame flags byte,
   and `hi_pie_tx_send` appends the channel index and length before the builder consumes it
   (section 5.2's table is the resulting hardware bytes). [V]
6. Completion is poll-only through the same read pointer (no TX IRQ). [V]

### 6.2 RX

1. `hi_pie_rx_interrupt@0x3f88` is the hard IRQ handler for all four vectors. For each channel in
   the vector's device data: check the mask register (skip when masked), check the status bit;
   if set: `napi_schedule_prep`, mask the channel (`rx_close_irq`), write-1-to-clear
   (`rx_clr_irq`), `__napi_schedule`. [V]
2. `hi_pie_rx_poll@0x4058` maps the NAPI instance back to its channel and loops: one
   `hi_pie_drv_rx_recv` per completed descriptor, one `hi_pie_drv_rx_addbuf` to re-arm it, then
   `hi_net_rx_proc` with the 12-byte summary. After the batch it kicks the RX pointer word
   (`hi_pie_drv_rx_kick` -> `rx_set_wptr@0x3008`) and re-opens the channel mask when the poll
   completes. [V]
3. `hi_pie_drv_rx_recv@0x3870` takes the software node at the head of the used list (the
   outstanding descriptor with the lowest ring index), reads the hardware read pointer and
   compares it with the node's ring index: equal means "next descriptor still to complete", so
   the poll stops. Otherwise it decodes the descriptor with `rx_get_desc` into the summary,
   unmaps the buffer, `skb_put()`s the u16 length from summary +4, records the new used-list head
   (next outstanding index) into the RX pointer word's "sf" half, zeroes the 32-byte descriptor,
   and moves the node to the free list. [V]
4. `hi_pie_drv_rx_addbuf@0x3a24` allocates a new skb with
   `__netdev_alloc_skb_ip_align()` for (RX buffer size + 128) bytes, reserves a further 128
   bytes of headroom, maps the data page (`dma_map_page_attrs`, direction 2 = FROM_DEVICE),
   writes the DMA address at +4 and the owner bit, and links the node back onto the used list;
   the pointer word's write half is then advanced to the last re-armed descriptor
   (`hi_pie_drv_rx_kick`). [V]
5. `hi_net_rx_proc@0x6568` checks the error flag (summary +7 bit 0); on error it bumps an error
   counter and frees the skb. Otherwise it resolves `(port, vport)` to the netdev via
   `g_api_priv`, applies the tag class (summary +7 bits 4:3) by copying the in-frame tag words into
   vendor-private skb fields, runs the optional per-port RX hook (the LSW's learning/CFE hook),
   calls `eth_type_trans()` and delivers with `netif_rx()`. [V]

---

## 7. Egress port/qid mapping to the LSW

This is the part that makes the netdev carry traffic, and it is **not** a PIE register: the vendor
splits it between PIE's per-port context table and the LSW queue manager. Recovered interface:

1. **PIE context table** (`g_api_priv`, section 2.2): one 64-byte entry per `(port, vport)`.
   Fields recovered: +0 queue handle (forwarded to the TX engine as the per-port queue context;
   zero until an LSW queue hook fills it), +4 up flag (set by `hi_net_dev_open`, cleared by
   `hi_net_dev_close`), +8 netdev,
   +12 descriptor word 0 (port | vport<<8 | ...), +16 descriptor word 1, +20/+24 RX-hook function
   and data (set by `hi_net_dev_rx_hook`), +28 group/team id used by `hi_net_get_port`. [V offsets]
2. **Registration hooks.** `hi_net_dev_probe` calls a global hook at `[.bss+0x9c18+0x24]` with
   `(netdev, desc)` before publishing the entry; `hi_net_queue_hook@0x5990` is 12 bytes that store
   its argument in that slot. `hi_lsw_dp.ko` imports `hi_net_queue_hook` and `hi_pie_get_gro_ch_*`
   / `hi_pie_get_woc_ch_*` and defines `hi_pie_map_init@0x56e8`, so the LSW's queue-manager driver
   is the layer that binds PIE channels/queues; the exact port/vport binding point is [U]. [V
   symbols and call sites]
3. **queue handle -> port/qid on the switch side.** `hi_lsw_pfe.ko` defines
   `port_qid_map_set@0x26b8` and `qid_map_set@0x25dc`. `port_qid_map_set(port)` gets the queue id
   (`hi_queue_get_qid`) and queue count, then loops 8 times calling
   `qid_map_set(port, 0, n, qid + q_num/8 * n)` - i.e. **each switch port gets 8 QIDs derived from
   the queue manager's base QID**. `qid_map_set` writes four consecutive 32-bit words built from
   `(id, value)` through `hi_lsw_write()` at a table indexed by the first argument. [V code shape]
4. **LSW register access convention.** `hi_lsw_pub!hi_lsw_write` decodes bits 31:24 of its address
   argument as a *registered module id* (`hi_lsw_reg_module(base, iomem, size)`) and bounds-checks
   the remaining offset before storing. The qid-map writes use synthetic addresses in module 6
   (`0x06043800 + …` and `0x06042800 + …`); which physical DT window module 6 is depends on the
   private registration tables in `hi_lsw_dp/pfe` and is **[U]** (it is LSW block-D RE, not PIE
   RE). [V convention, U mapping]

**Consequence for a clean-room port:** a first netdev needs the PIE ring engine plus *some* egress
decision. Without the LSW queue map, TX descriptors can be produced (ring pointers move, errors
visible in counters) but no frame is guaranteed to reach a MAC port; conversely RX may receive
frames only if the switch delivers them to a host queue. The wired milestone therefore needs the
LSW's minimal port/qid map, not just PIE. This matches [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
sections 5.3 and 6.3. [I]

Patch `022` now implements the W2 qid-map write (the populated table-A/table-B rows) behind
`luofu_switch.arm=1 luofu_switch.qidmap=1`, which is the first concrete form of that egress map, but
*which* queue/port a PIE TX descriptor selects remains [U]: the draft TX descriptor carries only
length/address/owner, so the map is programmed but the TX->egress binding is unproven.

**What is explicitly not invented here:** the numeric mapping between DT `port = <8..12>`, the
LSW's internal port/vport ids (the `qid_map_set` code branches on ids 0-15, 40-47 and >47), and
the PIE queue handle value. All three are [U]; section 11 gates them behind bench reads.

---

## 8. DMA coherency and ACP

**What the vendor driver does:** [V]

- `hi_pie_probe` reads the DT boolean `dma-coherent` (property string at
  `.rodata.str1.4+0x5e4`) and logs `"open dma-coherent %x"`. It passes the result into the
  register-init config.
- Rings are allocated with `dma_alloc_attrs(dev, size, &handle, GFP_KERNEL=0xcc0, attrs=0)` and
  buffers are mapped with `dma_map_page_attrs(...)`/`dma_unmap_page_attrs(...)`; no explicit cache
  maintenance (`dma_sync_*` or flush) appears in the module. This is exactly what `dma-coherent`
  selects in the kernel's DMA ops, so the vendor depends on the hardware being coherent. [V]
- The driver rejects DMA-mapping `vmalloc` memory (it checks `is_vmalloc_addr` and logs
  `rejecting DMA map of vmalloc memory` on failure) - a normal constraint for ARM32 DMA. [V]
- The module does not program any coherency attributes on this path.

**The ACP block is the vendor's coherency programming path.** [V]

- DT: the pie node carries the compatibles `"hsan,pie" "hsan,acp-pie"` and the ACP properties
  `acp-gate = <0x10100174 0x1000000 0>` and
  `acp-pre-cfg-attr = <0x1010015c 0xffffffff 0x1b1b1b1b 0x10100160 0xffffffff 0x16161717>`
  ([soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines 1253-1255). These are
  (address, mask, value) triples.
- `hi_acp.ko` (6,756 B) is a self-contained driver: it scans the
  `hsan,acp-{fmc,dmac,sec,usb,usb2,usb3,pcie0,pcie1,pcie2,pie,pcu,woe,mmc0}` nodes, reads
  `acp-gate` and the `acp-pre-cfg-attr`/`acp-later-cfg-attr` triple arrays, ioremaps each 4 KB
  target and applies a read-modify-write `(value & mask) | (old & ~mask)`
  (`hi_acp_hw_set_cache_attr`). It exports `hi_acp_notifier_call_chain(event, data)` and registers
  a blocking notifier.
- `hi_kpie.ko` resolves that symbol with `__symbol_get("hi_acp_notifier_call_chain")` and calls it
  with event id 9 (= the "pie" entry of the ACP's 13-entry block table) **only from the v300
  register-init function**, and only when the DT says `dma-coherent`. The strings
  `"pie acp config fail"` / `"hi_acp_notifier_call_chain not support"` are the error paths. [V]

**Open question for 0x250.** The board runs the v250 register-init path, which does not contain
that call; the ACP attributes either come from the bootloader, from the ACP driver's own init scan,
or are already in reset defaults. A Linux port must settle this *on the bench* before claiming
coherent DMA works: run with `dma-coherent` copied from the vendor DT and check for
cache-coherency symptoms under traffic (dropped/corrupted frames, RX checksum errors that disappear
when buffers are mapped non-coherent with explicit sync). [U]/bench-only. Do **not** add an ACP
driver speculatively; [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 4.3 already records that
model as "small SoC driver only if bench shows a need".

**Linux-side rule that is safe now:** allocate rings and buffers with the DMA API only, honour the
DT `dma-coherent` flag, keep 32-bit DMA addresses, and never map `vmalloc` memory. That reproduces
the vendor contract without inventing register writes. [I]

---

## 9. Initialization and ordering requirements

Recovered from `hi_pie_probe@.text.unlikely+0x0` and `hi_pie_drv_init@0x4378`: [V]

1. **chip/clipper init** (`hi_clipper_init`; reads the chip name - the module contains the literal
   `"Hi5671Y V200"`).
2. **read `hwver`** from the pie DT node (-> 0x250).
3. **clock**: `devm_clk_get(dev, NULL)`, `clk_prepare()+clk_enable()` - the CRG PIE gate
   (patch `004`: clock id 15, gate1 bit 0x19).
4. **reset**: `devm_reset_control_array_get()` then **assert then deassert** immediately; the
   vendor DT cell is `(0x30, 0x19)` (CRG reset word 0x30 bit 0x19). The target CRG binding's
   two-cell reset form already accepts that shape ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
   section 0.3).
5. **ioremap** the 64 KB window (`platform_get_resource` + `devm_ioremap_resource`); on failure
   `"pie ioremap fail"`.
6. **four IRQs** by index 0..3 (`platform_get_irq`); on failure `"no irq"`.
7. **DMA coherency property** read (section 8).
8. **private NAPI-container netdev** `alloc_netdev_mqs(0, "cpu%d", …)`.
9. **ring properties** parsed and validated (section 5.1).
10. **`hi_pie_drv_init`**: select the ops table by `hwver`; publish the ops pointer; copy channel
    counts and ring config into per-channel state; call `ops->reg_init(base, hwver, cfg)` (which
    programs the global and per-channel registers of section 4 and leaves all RX interrupts
    masked); `netif_napi_add` x16; `devm_request_threaded_irq` x4; `hi_pie_drv_rx_init` (allocate
    and arm RX rings); `hi_pie_drv_tx_init` (allocate TX rings); then per configured channel
    `ops->rx_open_irq` (unmask) and finally `ops->rx_enable` (global RX enable). WoC init follows
    (`hi_woc_skb_init`, `hi_woc_drv_init`) and DFX registration last.
11. **probe tail**: `hi_pie_api_init` (wires the exported API), `hi_pie_dfx_init`, dev_info
    `"Hisilicon pie swver=%s,hwver=v%x"`.

Ordering constraints that matter to a clean-room design: [V]/[I]

- The PIE probe must complete before any MAC netdev probe that calls its API (vendor init script
  loads `hi_kpie` before `hi_kmac`; the MAC calls `hi_net_dev_probe` synchronously from its probe).
- `hi_lsw_dp` must be up before MAC probe so the queue hook can bind the `(port,vport)` entry
  (vendor load order: PIE -> LSW -> MDIO/PHY -> MAC). [V]
- The ring write pointer must only be pushed after the RX buffers are populated and the channel is
  otherwise configured; the vendor does exactly this (arm buffers, then set wptr, then unmask). [V]
- Nothing in the wired path needs the `woc_*` rings or the WoC/spec-xmit parts of `hi_kpie`; do
  not initialize them. [I]

---

## 10. What remains unknown or bench-only

| item | status | how it is closed |
| --- | --- | --- |
| Full TX descriptor bit map, especially the type-specific words at +8..+19 and the checksum/VLAN classification bits | **decoded 2026-11-19:** the normal non-TSO byte image is section 5.2 [V]; the type-1/2/3 and TSO branches are decoded only at branch level [I] | minimal TX with the section-5.2 image, then compare the hardware result/descriptor state on the bench with offloads off; parse the TSO branch only if offload is ever needed |
| Exact RX status bit meanings (+8/+10/+13, the two +16/+18 descriptor paths) and error/checksum reporting | **decode recovered 2026-11-19 [V]:** length +0, status/error +8 bits 6:0, port/vport +16 or +23/+24, tag class +10 bits 5:4; the semantic meaning of the two flag classes (+13 bits 6:5, +10 bits 5:4 beyond "tag") and any checksum verdict is still [U] | bench: capture descriptors of known frames (ping/ARP/ICMP) and correlate the flag bits; do not set CHECKSUM_UNNECESSARY until then |
| Ring pair-word semantics (`0xa`/`0xc` as log2 descriptor count, second RX word as buffer size) | [I] | bench readback of the register the driver programmed (`0x3004`/`0x2004`/`0x3504`) vs the DT values; a read-only inventory settles it |
| Per-channel words `0x180/0x200/0x3210/0x3290/0x3310/0x3204/0x3208/0x0380` | [U] | same inventory: read them back after the vendor boot vs after our init |
| `0x3404` 2-bit per-channel field, `0x3408` large-ring bitmap, `0x3700` GRO size | [I] writes recovered, semantics unknown | bench with/without the corresponding config |
| Whether the bootloader/firmware pre-initializes PIE, and whether 0x250 needs the ACP attributes | [U] | read-only inventory after a stock boot (before any driver write) + ACP experiments under traffic |
| Channel pairing model (4 wired + 4 WoC vs interleaved) and the "pie-chN,M" trailing digit | [I]/[U] | read-only IRQ status/mask inventory; one-frame RX on a known port |
| qid/port map: DT `port` (8..12) vs LSW id space vs PIE queue handle | [U] | LSW block-D RE + bench register reads; do not guess |
| Whether a netdev can pass traffic without the LSW map | [I] no | stage 4 bench (section 11) |
| TX completion without TX IRQ (poll-only) | [V] code, [U] silicon behaviour under load | bench under sustained TX |
| RX/TX pointer words are ring indices, not free-running counters | [I] from the vendor consumer arithmetic (vendor masks read pointers with the ring mask and compares a node ring index to a raw read pointer) | bench: watch the pointer words while sending a known number of frames; a 16-bit counter that grows past the ring size disproves the model |
| TX egress queue handle (the vendor's per-port `g_api_priv[].queue` word): it is not part of the descriptor byte image beyond the port tag | [U] | LSW block-D RE + bench; patch 024 leaves the field zero |
| Hardware acceptance of the section-5.2 descriptor image (does a linear frame with these control words leave the port?) | [U] | attended bench: arm, per-port counters, one ping while watching the TX read pointer and the switch counters |
| Restart/recovery after a PIE wedge on the AHB | [U]/bench-only | no driver may add reset semantics; recovery is the physical RESET+TFTP card ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 7.3) |

**Measured status 2026-11-18 (inv1 read-only run, [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md)).**
The rows that are register-readable on one stock boot are now closed or partly closed: "ring
pair-word semantics" is [V] for the first word (programmed config == live DT first word); the
per-channel words, `0x3404/0x3408/0x3700` and the RX IRQ status/mask are [V] raw values (their
bit meanings remain [U]); the bootloader/firmware pre-initialization row is partially closed
(post-boot state only); the channel-pairing row is partially closed (the live active set is
ch0/ch1 of 8 slots; the `pie-chN,M` pairing digit is still [U]). The descriptor bit maps and the
RX status semantics were then closed **from the vendor module disassembly** on 2026-11-19 (this
document's section 5.2) - that is a static-code result, not a measurement. Traffic behavior, the
ACP question, the TX queue handle and any pre-Linux state are **not** closed - nothing has
exercised the engine or written a register.

---

## 11. Minimal implementation plan: from patch 009 to a working netdev

> **Status 2026-11-18 (PIE-datapath session):** P1/P2 are implemented as patches `015` +
> `020` (`drivers/net/ethernet/hisilicon/luofu-pie.c`). Patch `020` is the draft ring engine
> built from the P3 measured baseline: coherent 32-byte descriptor rings for the two active RX/TX
> channels, the recovered v250 global/per-channel register initialization, mask/unmask + NAPI
> receive sequencing, the global RX enable at `0x3200` bit 0, a TX producer with poll-only
> completion, and a read-back verification that tears the engine down on any mismatch. The engine
> is **armed only with the explicit `luofu_pie.arm=1` command-line opt-in**; without it probe
> validates every resource and refuses before the first register write, clock enable or reset
> toggle. The RX completion **decode is still [U]** - the poll consumes completions by read
> pointer and re-arms the descriptors without handing buffers to the stack - and patch `009`'s
> GEMAC is now a draft phylink/netdev front end that only registers when this backend is armed
> (P4 partial). Traffic delivery (P5) still needs the LSW egress map; the remaining [U] rows are
> listed in section 10.
>
> **Status 2026-11-19 (connectivity session):** P4 is now readiness-gated end to end and the LSW
> half of P5 is implemented - none of it executed on hardware. Patch `023` makes the GEMAC netdev
> register only when *both* `luofu_pie_armed()` and the switch egress map
> (`luofu_switch_ready()`, patch `022`) hold; it also fixes the phylink type to
> `PHYLINK_NETDEV` - the draft used `PHYLINK_DEV`, which never drives the carrier from the
> resolve work, so a real link would have looked dead to userspace - and adds the netdev address
> source (standard devicetree MAC properties, else a random locally-administered fallback). Patch
> `022` programs the LSW qid map behind `luofu_switch.arm=1 luofu_switch.qidmap=1` and only reports the
> map ready after a full readback verification. The board DTS enables the pie, lsw and gmac1-4
> nodes; a normal boot refuses in every driver without touching the hardware. P5's RX half is
> unchanged (the PIE RX completion decode is [U], so no frame is delivered to the stack) and the
> TX egress selection from a descriptor is still [U]; the attended boot sequence is
> `luofu_switch.arm=1 luofu_switch.qidmap=1 luofu_pie.arm=1`.
>
> **Status 2026-11-19 (completion-decode session, patch `024`):** the v250 RX completion
> descriptor and the normal non-TSO TX descriptor are now decoded (section 5.2) and implemented in
> `luofu-pie.c`/`luofu-gmac.c`: the NAPI poll decodes length/status/port, delivers the skb to
> the net_device that registered the completed internal-switch port and re-arms the slot with a
> fresh coherent buffer; the TX producer writes the full recovered normal-descriptor byte image
> (including the port tag) and rings the doorbell with the masked ring index. The GEMAC front end
> now reads the vendor `port` value from the board devicetree (`hisilicon,port`, 8..12) and
> registers/unregisters its port around open/stop, replacing the vendor `g_api_priv[]` lookup.
> The arm gate `luofu_pie.arm=1` and the fail-closed probe are unchanged. None of this has run on
> hardware; the attended checks are in the port-notes lane summary. P5's remaining half is the
> switch-side egress queue binding (section 7) and the [U] rows of section 10.

### 11.1 Shape decision (recommended)

**Keep the vendor split, with our own in-kernel interface: a PIE ring engine driver owns
DMA/NAPI/rings, and the GEMAC driver (patch `009`'s netdev half) owns the net_device, phylink and
the netdev ops.** This is the shape the hardware and the vendor module layering actually have
(section 2), it matches the LSW switchdev model (per-port netdevs, no CPU-port tag), and it keeps
the submission reviewable as "MAC front end + packet engine + switch". [I]

Concretely, in one new module (or two modules linked by `EXPORT_SYMBOL_GPL` - one module is fewer
ABI surfaces): [I]

```
luofu-pie.c        PIE: clocks/resets, MMIO, op table per hwver, rings, DMA, NAPI,
                   IRQs, per-port context table + an internal API:
                     luofu_pie_netdev_probe(dev, desc)   (mirror of hi_net_dev_probe)
                     luofu_pie_netdev_remove/dev_open/dev_close(dev)
                     luofu_pie_xmit(skb, dev)            (mirror of hi_net_dev_xmit)
                     luofu_pie_rx_hook(dev, hook)        (for the LSW when it exists)
luofu-gmac.c       MAC: phylink + net_device (patch 009 extended); ndo_start_xmit ->
                   luofu_pie_xmit; ndo_open/stop -> phylink + luofu_pie_open/close;
                   probe registers the netdev instead of refusing.
```

The vendor `desc8` becomes our documented resource: port and vport are DT data on the gmac nodes
(`port = <8..12>` exists in the vendor DT; `vport` is absent and defaults to 0). Ring geometry is
driver data for v1 (the board values from the vendor overlay) rather than vendor DT property names;
if a binding property is wanted, use documented `hisilicon,rx-ring`-style properties, not
`rx_ring`/`tx_ring`. [I]

### 11.2 Staged plan

| stage | work (host-only unless stated) | acceptance gate |
| --- | --- | --- |
| **P0 - spec** (this document) | register/descriptor/channel/qid/ACP model; fixture hashes; commands | document review; links resolve; no invented values (every numeric cited) |
| **P1 - PIE binding + probe skeleton** | new `hisilicon,luofu-pie.yaml` (reg 64 KiB, 4 interrupts, clocks/resets, `dma-coherent`, SoC-variant data instead of the vendor `hwver` property); `CONFIG_LUOFU_PIE` (tristate, `depends on OF`) with a `[N]` provenance entry in the config; driver probe does clock/reset/ioremap/IRQ acquisition + `reg_init` ordering, then stops before enabling anything | `make target/linux/prepare` and `make target/linux/compile` RC=0 with `CONFIG_WERROR=y`; `dt_binding_check` PASS; board DTS zero dtc warnings; config re-derivation idempotent (only the new `[N]` line changes). No hardware. |
| **P2 - ring engine** | coherent ring allocation (32-byte descriptors, board ring sizes), RX refill path, TX producer, IRQ handler + NAPI, doorbell writes, the internal API of 11.1; RX/TX channel tables from the v250 ops table; all WoC paths omitted | **DONE 2026-11-18 as patch `020`, extended 2026-11-19 by patch `024`** (host-compiled only, arm-gated): rings, recovered init/mask sequencing, NAPI RX poll with the decoded completion path and stack delivery, full normal TX descriptor, exported backend + port registration; compile/checkpatch/bindings/DTS clean. No hardware. |
| **P3 - bench read-only inventory** (attended, recovery armed) | boot the vendor system or the lab kernel and read - with read-only access such as a write-protected probe module - the PIE window: `0x1000`, `0x2000`/`0x3000` ring blocks for ch 0-3, `0x0300/0x0400` status/mask, `0x3200/0x3404/0x3408/0x3504`, and the IRQ status words. Compare against this model; record in the wave evidence dir | **DONE 2026-11-18** on the stock vendor boot (read-only, no writes): every allowlisted offset returned a plausible value, static words were stable, the dynamic pointers moved, and the programmed TX/RX channel configs equal the live overlay `tx_ring[0]/rx_ring[0]` (section 5.4; evidence `parent:build/bench-inventory/20261118T130512Z-inv1-run/` + `followup/`). Acceptance met for the register-readable [I]/[U] rows; see section 10 for what the run does **not** settle. |
| **P4 - netdev wiring** | extend patch `009`: probe builds the netdev (phylink + `alloc_etherdev_mqs`), checks that the PIE backend is armed **and the LSW egress map is ready**, registers the netdev; `ndo_start_xmit` -> PIE; `ndo_open/stop` -> phylink; enable the pie/lsw nodes and gmac1-4 in the DTS via `shared.patch` | **DONE as host-compiled code in patches `020`+`023`** (never executed): netdev/phylink glue + TX forwarding + the two readiness gates + `PHYLINK_NETDEV` carrier fix + MAC address source; DTS nodes enabled. Bench gate unchanged: netdev appears, carrier up, TX doorbell advances the TX read pointer, no lockup; **no traffic claim** |
| **P5 - minimal LSW egress + RX delivery** | block-D1 subset: LSW probe, port/qid map for the populated rows, host-queue binding to PIE's per-port context, RX hook delivery. This is the hard dependency for traffic (section 7) | **PARTLY DONE as host-compiled code (patches `022`, `024`; never executed):** the W2 qid-map write set (table A ports 4-12, table B ports 40-43) is programmed behind `luofu_switch.arm=1 luofu_switch.qidmap=1` with readback verification; the device-tree port model carries the measured hardware port number (`hisilicon,port` 9-12); the PIE RX completion decode is implemented and delivers to the net_device registered for the completed port; the TX descriptor is tagged with the port. **Still open:** the switch-side egress queue handle the vendor carries in `g_api_priv[]` [U], the PIE<->LSW host-queue hook [U], and hardware acceptance of the descriptor. M2-equivalent (ping host<->LAN port) is not reachable from the code yet |
| **P6 - upstream series** | split patches (PIE engine, GEMAC netdev, LSW minimal), bindings, config, DTS; document hardware-unverified behaviour honestly | review/checkpatch; bench evidence recorded separately |

Patch numbering: the landed platform series is `001`-`012`; take the next free number in the
shared patch directory (the DWC PCIe lane is task-21 and the shared directory may have moved on by
the time this plan is executed). `shared.patch` will carry the `config-6.18`
`[N]` entries, the DTS deltas (pie node: clocks/resets + variant data; gmac3 enabled at bench
stage) and any `README`/`TARGET-DESIGN` status lines the lead wants; this document edits no shared
file. [I]

### 11.3 Dependencies

| dependency | why it is required | state |
| --- | --- | --- |
| patch `004` CRG clock/reset | PIE gate clock (id 15, gate1 bit 0x19) and reset `(0x30,0x19)`; no PIE register write can happen with the block unclocked/unreset | landed, host-compiled only |
| patch `007` MDIO | MAC phylink needs the shared MDIO bus for the Genius PHYs | landed, host-compiled only |
| patch `008` PHY | link up/down for the MAC port | landed, host-compiled only |
| patch `009` GEMAC | the netdev/phylink half this plan extends; its binding stays valid | landed, refuses probe by design |
| LSW block D1 (port/qid map + host queue) | frame delivery in both directions (section 7) | not started; the long pole |
| bench restart/recovery contract | every hardware step; nothing in the plan may add reset semantics | [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 7.3; patch `010` is provisional |

### 11.4 Effort (estimates, not measured)

P1+P2: 4-6 host sessions (the register/descriptor model in this document removes the largest
unknown; the remaining work is writing the ring engine). P3: 1-2 attended bench sessions. P4: 2-3
host sessions + 2-3 bench sessions. P5: 5-10 host sessions + 2-3 bench sessions (LSW RE dominates).
P6: 2-3 sessions. These refine [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 6.3's
8-15 host + 3-5 bench estimate downward for PIE itself and leave the LSW as the long pole. [I]

---

## 12. Risks and corrections

### 12.1 Corrections to earlier planning documents

1. **"The MAC cannot own a netdev" is inaccurate as written.** [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md)
   section 1.1 says `hi_kmac` imports no netdev allocator because it imports no
   `alloc_netdev_mqs`. The module does not import `alloc_netdev_mqs` directly, but it imports
   `alloc_etherdev_mqs` (which wraps it) and `register_netdev`/`free_netdev`, and
   `hi_mac_probe` calls them (section 2.1). The correct structural statement is: **the MAC owns the
   net_device and phylink; PIE owns the rings, DMA, NAPI and the per-port data path.** Patch `009`
   is therefore the right place for the netdev half - it needs a data-path backend, not a
   different owner.
2. **"4 PIE channels" needs detail.** Four is the number of **interrupt vectors** (and of RX/TX
   rings configured in the *base* DT); the engine has 8 channel slots on 0x250 (16 on the 0x260+
   layout) and each vector services two channels (section 5.3). A driver that assumes four IRQs ==
   four channels will mis-mask the second channel of each vector. **Measured correction
   (2026-11-18):** on the stock board the U-Boot overlay sizes 2 RX pairs and 2 TX channels, so
   only ch0/ch1 are armed and only the vectors serving them do wired work (section 5.4).
3. **The pie node's `hwver = <0x250>` is not mentioned in the block-E summary.** It is the single
   most important input to the ops-table choice (section 3) and must be reproduced (as driver data
   or a variant compatible) in our binding.
4. **The "interim: one MAC + fixed-link to prove the path" S3 idea** ([TARGET-DESIGN.md](TARGET-DESIGN.md)
   section 10) cannot work as written: the MAC has no rings. The interim is "PIE ring engine +
   one MAC port configuration", with traffic still requiring the LSW map (section 7).

### 12.2 Bench risk

- Patch `009`'s header records the project rule that an access to the MAC/PIE block before it is
  ready has hung the board's AHB bus. The same caution applies to PIE: every MMIO write in P3/P4
  is a bench-safety event, not a host test.
- There is no machine restart/reboot path validated (patch `001` has no `.restart`; patch `005`
  has no restart handler; patch `010` is provisional) - recovery is the physical RESET+TFTP card
  ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 7.3). No driver added by this plan may add
  reset semantics of its own.
- Do not initialize the WoC rings/channels speculatively; they belong to the Wi-Fi offload lane
  ([REMAINING-DRIVERS.md](REMAINING-DRIVERS.md) section 6.3 risk list).
- DMA-coherency mistakes on this non-coherent A9 can corrupt silently; use the vendor's coherent
  allocation model and the ACP question (section 8) as an explicit bench variable.
- The vendor userspace/ABI contract (HYFI/HSLINK/rootif) is irrelevant to our clean-room netdev;
  do not reproduce it ([VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) sections 2.3-2.4,
  3.4). [I]

---

## 13. Evidence map and reproduction

### 13.1 Claim -> artifact

| claim | artifact / check |
| --- | --- |
| MAC owns netdev/phylink, imports PIE data-path helpers, tail-calls PIE xmit with `dev+0x5a8` | `hi_kmac.ko` `.rel.text.unlikely` / `__ksymtab_strings` / `.text.unlikely` disassembly (offsets in section 2.1); `hi_mac_net_xmit@0x4300` |
| PIE owns rings/DMA/NAPI and an unregistered `"cpu%d"` NAPI-container netdev | `hi_kpie.ko` imports (`nm`/`readelf -sW`), `hi_pie_probe@.text.unlikely+0x0`, `hi_pie_drv_init@0x4378` |
| 403 symbols; fixture bytes identical in both rootfs images | `nm hi_kpie.ko` count; sha256 table in the header |
| hwver selection and v250 ops-table layout | `hi_pie_drv_init+0x3c`; `hi_pie_get_hw_ctrl_v250@0x1878` returning `.rodata+0x118`; `.rel.rodata` relocations at `+0x120..+0x174`; v250 functions disassembled at the quoted offsets |
| register offsets (rings, IRQ, enable, per-channel words) | `hi_pie_rx_get_rptr/set_wptr/set_sf_rptr/tx_set_wptr/tx_get_rptr/rx_set_owner/rx_set_addr/rx_get_desc/tx_set_desc/reg_init` (v250 copies at `0xdf8-0x1700`) |
| ring DT parsing/validation and allocation sizes | `hi_pie_probe` strings at `.rodata.str1.4+0x628` (`rx_ring`), `+0x660` (`gro_ring`), `+0x66c` (`tx_ring`), `+0x69c/0x6a8` (`woc_*`), `+0x648` (`"mac mtu(…)"`); `hi_pie_drv_tx_init@0x3614`, `hi_pie_drv_rx_init@0x3ca0` |
| 4 IRQs, per-vector channel pairs, names, affinities | `.data+0x4` table + `.rodata.str1.4+0x548..0x56c`; `hi_pie_drv_init` IRQ loop; `parent:rootfs-2.4.15/etc/rc.d/luofu` lines 28-45 |
| TX/RX flows and descriptor ownership/doorbells | `hi_net_dev_xmit@0x637c`, `hi_pie_drv_xmit@0x42ac`, `hi_pie_tx_send@0x3250`, `hi_pie_rx_interrupt@0x3f88`, `hi_pie_rx_poll@0x4058`, `hi_pie_drv_rx_recv@0x3870`, `hi_pie_drv_rx_addbuf@0x3a24`, `hi_net_rx_proc@0x6568` |
| PIE P3 measured register/channel state (2026-11-18, stock boot) | `parent:build/bench-inventory/20261118T130512Z-inv1-run/inventory.json` entries group `pie`; `followup/pie_rx_ring.bin`, `followup/pie_tx_ring.bin`; analysis [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) sections 2 and 6 |
| qid/port mapping split across PIE context and LSW | `g_api_priv` entry layout from `hi_net_dev_probe@0x5eec`/`hi_net_dev_xmit`/`hi_net_dev_open`; `hi_lsw_dp` imports and `hi_pie_map_init@0x56e8`; `hi_lsw_pfe!port_qid_map_set@0x26b8`/`qid_map_set@0x25dc`; `hi_lsw_pub!hi_lsw_write@0x354` |
| DMA coherency/ACP facts | pie DT properties ([soc/luofu-r116-pinned.dts](soc/luofu-r116-pinned.dts) lines 1253-1269); `hi_kpie` `dma-coherent`/`dma_*` call sites and `__symbol_get` at `+0x2420` (v300 reg-init), strings `"hi_acp_notifier_call_chain"` etc.; `hi_acp.ko` (6,756 B) notifier and RMW writer |
| init order | `hi_pie_probe` / `hi_pie_drv_init` call order; vendor `rc.d/luofu`; target DTS and patches `004`/`007`/`008`/`009` |
| patch 009 is the partial GEMAC and its refusal is deliberate | [../target-skeleton/target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch](../target-skeleton/target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch) (refusal at line 150); [../target-skeleton/docs/port-notes/net/SUMMARY.md](../target-skeleton/docs/port-notes/net/SUMMARY.md); [SKELETON-SMOKE.md](SKELETON-SMOKE.md) sixth session |
| RX completion decode (length +0, status/error +8 bits 6:0, port/vport +16 or +23/+24, tag class +10 bits 5:4, summary layout) | `hi_kpie.ko` v250 `hi_pie_rx_get_desc@0xf14` (196 B), `hi_pie_drv_rx_recv@0x3870` (summary +4 -> `skb_put`), `hi_net_rx_proc@0x6568` (error flag, port lookup, tag class); commands in section 13.2 [V] |
| TX normal non-TSO descriptor byte image (length, flags/class, DMA address, port tag, type-0 control words) | `hi_pie_tx_set_desc@0x11ac` (380 B), type-0 descriptor helper at `0xfe8` (452 B), `hi_pie_tx_send@0x3250`, `hi_pie_drv_xmit@0x42ac`, `hi_pie_drv_tx_init@0x3614` (ring sizing/reclaim), section 5.2 [V] |
| ring pointer semantics (TX doorbell = masked next index, TX read pointer = completion index, RX write/sf halves, poll-only TX completion) | `hi_pie_tx_send@0x3250` (reclaim walk + `(index+1)&mask` doorbell), `hi_pie_rx_poll@0x4058`, `hi_pie_drv_rx_kick@0x3c5c`, `hi_pie_drv_rx_recv@0x3870` [V] code, [I] ring-index reading |
| patch 024 implements the decode + delivery behind the arm gate | [../target-skeleton/target/linux/hisilicon/patches-6.18/024-net-pie-completion-decode-luofu.patch](../target-skeleton/target/linux/hisilicon/patches-6.18/024-net-pie-completion-decode-luofu.patch); port notes `pie-decode/SUMMARY.md`; [SKELETON-SMOKE.md](SKELETON-SMOKE.md) PIE completion-decode session |
| MAC register facts (PORT_MODE) that the port half keeps using | [../lab/luofu-gmac/luofu-gmac.c](../lab/luofu-gmac/luofu-gmac.c) lines 120-146; [ETHERNET-BRINGUP-FRAMING.md](ETHERNET-BRINGUP-FRAMING.md) sections 1-3 |

### 13.2 Reproduction commands (read-only, local fixtures)

```sh
cd <parent repo root>
T=build/linux-kbuild/toolchain/bin
K=rootfs-2.4.15/lib/hisilicon/ko
$T/arm-none-linux-gnueabihf-nm -S $K/hi_kpie.ko             # symbols (403 lines)
$T/arm-none-linux-gnueabihf-readelf -sW -rW $K/hi_kpie.ko   # symbol/relocation tables
$T/arm-none-linux-gnueabihf-objdump -dr $K/hi_kpie.ko       # disassembly + relocations
$T/arm-none-linux-gnueabihf-objdump -s -j .rodata -j .data -j .rodata.str1.4 $K/hi_kpie.ko
$T/arm-none-linux-gnueabihf-objdump -s -j .rodata $K/hi_lsw_pfe.ko
```

The lane kept the raw dumps under `parent:build/linux-kbuild/waves/2026-11-18-pie-datapath-re/`
(gitignored; not required to reproduce - the commands above regenerate them). No vendor binary,
blob, or extracted artifact is committed to this repository.

---

## 14. Open questions

1. Are the IRQ vectors' second channels the WoC channels, and does the wired-only driver need to
   service them at all? (section 5.3) [I]/[U]
2. Do the ring pair first words really encode `log2(descriptor count)`? **Answered for the live
   values (2026-11-18):** the programmed config registers equal the live DT first words (TX
   `0x2004=0xc` = `tx_ring[0]`, RX `0x3004=0x8` = `rx_ring[0]`, section 5.4), so the driver
   programs the first word as the shift; the descriptor-count arithmetic itself remains [I], and
   the second-word/`0x3504` encoding is still [U].
3. Which LSW register window is synthetic module 6, and what are the port/vport/QID number spaces?
   (section 7) [U] - block-D RE, not PIE RE.
4. Does 0x250 require ACP attribute programming by software, or is the bootloader's configuration
   already correct? (section 8) [U]/bench.
5. Does the board firmware pre-initialize PIE (rings, channel config) before Linux? (section 10)
   [U]/bench.
6. Is the section-5.2 normal non-TSO descriptor (including the port tag and the type-0 control
   words) accepted by the engine and does the frame leave the intended switch port? [V] bytes
   from disassembly, [U] hardware acceptance; bench-only.
7. Do the two flag classes in the RX summary (+7 bits 2:1 and 4:3) have a checksum meaning, or is
   the only consumer in this vendor build the tag class? [U] - the descriptor positions are [V],
   the semantics are not; keep `CHECKSUM_NONE` until a known-frame capture settles it.
8. Is the LSW egress queue handle (not part of the descriptor byte image beyond the port tag)
   required for a frame to reach a switch port, and where does the vendor's per-port queue hook
   put it? [U] - section 7 and the LSW lane.

---

**Bottom line.** The vendor PIE is now specified far enough to write the ring engine without
guessing its register block, descriptor size, doorbell/ownership model, RX completion decode or
normal TX descriptor shape. What it cannot do by itself is deliver frames: the egress queue
decision crosses into the internal LSW, so the honest milestone ordering is PIE rings -> one
GEMAC netdev -> minimal LSW map -> traffic. The first device contact has now happened - a
read-only, attended inventory on 2026-11-18 plus its DT follow-up, which closed P3 and corrected
the live channel model - and the descriptor decode closed the remaining static unknowns on
2026-11-19. Nothing in the *driver* plan is hardware-verified: no PIE register write has been
made, the descriptor bytes have never been accepted by the engine, and the LSW queue-handle,
flag-semantics and ACP questions remain open.

## 11. 2026-11-20 RX completion decode: vendor-exact + accepterr experiment (patch 053)

W16 opened the RX path: PIE RX ch0 completed 60-byte frames every ~1 s with status 0x25 (37).  The
vendor hi_pie_rx_get_desc@0xf14 decode shows status = byte[+8] & 0x7f; nonzero is the exception/error
path, where port = byte[+9] bits 7:4 (0xb = 11 = eth2), vport = byte[+24] bits 4:0.  The frames were
the host's ingress frames punted to the CPU.  Patch 053 implements the decode for all three paths
(error, normal +16, no-status +23/+24), adds luofu_pie.rxdiag=1 (full descriptor + decoded fields)
and luofu_pie.accepterr=1 (deliver exception frames to the error-path port's netdev; the vendor
drops them).

## 12. 2026-11-20 W20: CPU-copy delivery - the data path works (patch 057/058)

With the switch CPU-copy mirror at destination 4, normal-path completions arrive with port 0 and
vport 0 (13 frames len=98 status=0 during the dst=4 dwell window, none outside it).  luofu_pie has no
netdev for port 0, so they were dropped.  Patch 057 adds luofu_pie.cpucopy_deliver=1 (and
cpucopy_port, default 11): port-0 completions are handed to the mirror source port's netdev instead,
logged as RXCPUCOPY, while every other port and all exception completions keep the vendor behaviour
(exceptions still follow luofu_pie.accepterr).  With patches 001-057 the host gets sustained ping
replies (50/50, 0% loss) and SSH works on the OpenWrt 6.18 image - the full wire->PIE RX->CPU->kernel
->PIE TX->wire loop is functional via the mirror workaround.

