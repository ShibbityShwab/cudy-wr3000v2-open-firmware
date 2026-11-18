# Remaining drivers: evidence-graded port plans

**Status 2026-11-18: planning document only.** This file contains **no driver code**, and none of
the plans below has run on hardware. It is a new document; it does not modify any shared file (the
files that would need a one-line update *if* these plans are accepted are named at the end).

Context, so the plans are read at the right altitude:

- The kernel platform patch series `patches-6.18/001-009` exists and host-compiles (full
  `make target/linux/compile` RC=0; evidence in [SKELETON-SMOKE.md](SKELETON-SMOKE.md) and the
  per-lane records under [../target-skeleton/docs/port-notes/](../target-skeleton/docs/port-notes/README.md)).
  It has **never booted**.
- OpenWrt target, board DTS, image recipe, install path and driver series are still missing
  ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 9 "Explicitly not met today"; parent `STATUS.md`
  section 3). There is no OpenWrt target/image, so **no 6.18 bench validation route exists today**
  (section 7.2).
- The bench-reversal line is at its documented ceiling (v17; parent `STATUS.md` section 3, ledger
  **2026-11-18 06:50**) and bench fires are stopped. M2 ("no vendor network modules") remains
  **NOT MET** (parent `STATUS.md` section 2, G4).
- Publication remains **NOT READY** ([PUBLISH-READINESS.md](PUBLISH-READINESS.md);
  [../README.md](../README.md)).

## 0. Conventions, scope, and the shared-page contract

### 0.1 Evidence markers

| marker | meaning |
| --- | --- |
| **[V]** | verified by a committed artifact in this repository, a reproducible host-side check, or a recorded measurement whose artifact is named. Where the underlying artifact is gitignored local material (a vendor `.ko`, a flash dump), the citation says so: a fresh clone cannot regenerate that check. |
| **[I]** | inferred or planned: an engineering conclusion drawn from [V] evidence, or a design decision that still needs review/bench confirmation. |
| **[U]** | unknown / unmeasured. State it; do not design around it silently. |
| **[X]** | blocked on an external dependency (vendor GPL source, blocked hardware, or an undecided project question). |

Citation convention (same as [VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md)): a plain path
such as `soc/vendor-dt-notes.md` is in this repository; a `parent:` path is in the private research
repository (not published); "ledger" means the parent `mem-entries.md` (the newest dated entry wins).
Local-only artifacts live under parent `rootfs-2.4.15/`, parent `dumps/` or parent `build/` and are
named so they can be re-derived; none is copied into this repository.

### 0.2 Scope

This document covers the five blocks that still have **no driver in either tree** (no in-tree driver
and no lab driver that became one):

1. peripheral pinctrl (vendor `hi_kpinctrl`; pinmux/pingroups/pinconf, and what the repo does and
   does not know about "strap" bits);
2. thermal (vendor `hi_tvsensor` + the vendor DT trips + the vendor net-cooling device);
3. PCIe host controller (two DWC root complexes, iATU/acp gate) and the PCIe Wi-Fi device;
4. the internal LSW switch (vendor `hi_lsw_*`/`hi_swa`; DSA vs switchdev);
5. the GEMAC TX/RX ring + DMA data path needed to turn patch `009`'s partial GEMAC into a working
   netdev. Section 1 shows that the vendor's data path is not inside the MAC at all.

Out of scope: the Wi-Fi driver itself (**[X]**, [TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.3),
SMP, efuse/nvmem/RNG/LED PWM/PMU PWM/clk-corrector (TARGET-DESIGN section 7.1), the bootloader and
the image recipe. The deferred blocks now have their own decode and decisions in
[MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) (2026-11-18: MAC source resolved, efuse-write/RNG/LED-PWM
not work items, PMU PWM optional, CRG parents/PLLs decoded), and SMP has its own spec. The FMC/CRG/watchdog/MDIO/PHY drivers are landed as patches `004`-`008` and appear
only as dependencies.

### 0.3 The shared-page contract every new driver must keep

Patch `004` makes the CRG node `hisilicon,luofu-crg`, `syscon`, `simple-mfd` the single owner of the
`0x14880000` page; clocks and resets are consumed through the syscon regmap and the documented
two-cell reset form `(CRG page register offset, bit)`, range-checked by the CRG driver to the reset
words `0x2c/0x30/0x34` ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1, row "CRG clocks/reset";
[SKELETON-SMOKE.md](SKELETON-SMOKE.md) fifth session; [../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts)
around the `crg:` node). [V]

Consequences for this document's blocks [I]:

- **No new driver may ioremap `0x14880000`.** The watchdog is a child of the CRG node exactly for
  this reason; the same shape is expected for any future child of that page.
- Each block owns its own register page(s): `0x14900100`/`0x14940000` for pinctrl, `0x14900500` for
  the thermal sensor, the PCIe `dbi`/`cfg` windows, the LSW/PIE windows. Each binding therefore
  declares its own `reg`, and `clocks`/`resets` cells point at the CRG node.
- **No driver may add a reset path.** The only reset writes with on-record evidence are the CRG
  watchdog words: `clr-offset` = `0x70` with the vendor `reset-val0/1` pair, plus the enable/stop
  pairs (section 7.3). The vendor CRG node also declares a reboot command (`reboot-offset = <0x0>`,
  `softrst_val0/1`), but no such write has been measured: treat it as [U] (section 7.3). A driver
  that "resets" unrelated blocks through CRG words is out of bounds; the CRG xlate already rejects
  offsets outside `0x2c/0x30/0x34`.

## 1. Structural finding: where the GEMAC data path actually lives

This finding re-frames block 5 and strengthens block 4; it is placed first because
[TARGET-DESIGN.md](TARGET-DESIGN.md) section 10 (S3) currently assumes "one MAC + fixed-link to prove
the path". That assumption is **not supported** by the vendor modules. [V]

### 1.1 The measured module layering

| evidence | what it shows |
| --- | --- |
| `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kmac.ko` (75,116 B, `vermagic 5.10.201 SMP mod_unload ARMv7`, `depends: hi_kpie,hi_basic,ksecurec`) exports the MAC functions (`hi_gemac_probe`, `hi_mac_config`, `hi_mac_link_up/down`, `hi_mac_hw_*`, `hi_mac_net_xmit`, `hi_net_mii_register`) and imports **`hi_net_dev_probe`, `hi_net_dev_open`, `hi_net_dev_close`, `hi_net_dev_remove`, `hi_net_dev_xmit`** plus the netdev/phylink allocators (`alloc_etherdev_mqs`, `register_netdev`, `free_netdev`, `phylink_*`): `hi_mac_probe` allocates/registers the per-port netdev and phylink, then calls `hi_net_dev_probe(dev, &desc8)` ([V] symbol table; [PIE-DATAPATH.md](PIE-DATAPATH.md) section 2.1). It imports **no** `dma_*` or `napi_*` — it owns the netdev, not a ring. |
| `hi_mac_net_xmit+0x0` disassembly (`arm-none-linux-gnueabihf-objdump`): `add r1, r1, #0x5a0; add r1, r1, #8; b hi_net_dev_xmit` with `R_ARM_JUMP24 hi_net_dev_xmit` — the MAC `ndo_start_xmit` computes `netdev_priv = dev + 0x5a8` and tail-calls the **PIE** helper. [V] |
| `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_kpie.ko` (68,524 B, `depends: ksecurec,hi_basic`) defines `hi_net_dev_probe/open/close/remove/xmit`, `hi_net_rx_proc`, `g_api_priv`, `g_rx_queue`, and the ring/NAPI/DMA implementation: `hi_pie_drv_rx_init/rx_kick/rx_recv/rx_poll`, `hi_pie_drv_tx_init`, `hi_pie_drv_xmit`, `hi_pie_tx_send`, `hi_pie_tx_set_desc/get_desc/get_rptr/set_wptr`, `hi_pie_rx_set_addr/set_owner/set_wptr/get_rptr`, plus `hi_net_tx_hook/rx_hook` and `hi_net_woc_*`. It imports `alloc_netdev_mqs`, `ether_setup`, `netif_napi_add`, `napi_enable`, `dma_alloc_attrs`, `dma_map_page_attrs`. [V] |
| The vendor init script `parent:rootfs-2.4.15/etc/rc.d/luofu` loads `hi_kpie` (and pins four PIE IRQs, `pie-ch0..ch3`, affinity 1/2/1/2) **before** `hi_lsw_*` and **before** `hi_kmdio`/`hi_kphy`/`hi_kmac`; the load order is also recorded in [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) "The load order". [V] |
| The vendor DT `pie@0x10a70000` node carries `interrupts = <SPI 88..91>`, `rx_ring`/`tx_ring`/`woc_rx_ring`/`woc_tx_ring` properties, `dma-coherent`, clock `LUOFU_CLK_PIE` and reset `(0x30, 0x19)` (`soc/luofu-r116-pinned.dts:1253-1269`); `hi_kpie` reads `rx_ring`/`tx_ring` by name (module strings: `get rx_ring fail`, `get tx_ring fail`) and requests four IRQs. [V] |

**Conclusion [V]/[I].** The vendor wired-Ethernet data path is:

`GEMAC port (phylink MAC, config only) <-> internal LSW (forwarding/queues) <-> PIE (packet engine, rings/DMA/NAPI) <-> netdev`

with the per-port netdev and phylink owned by the **MAC** and the rings/DMA/NAPI owned by
**`hi_kpie`** (the MAC registers the netdev and tail-calls the PIE xmit helper; it carries no
descriptor ring of its own). The full evidence-graded specification of the PIE half - window and
variant, register model, ring/descriptor geometry, channel/IRQ model, TX/RX flows, egress
qid/port mapping, DMA/ACP, staging plan - is [PIE-DATAPATH.md](PIE-DATAPATH.md) (2026-11-18,
743 lines), which also corrects this table's earlier "MAC cannot own a netdev" wording.

### 1.2 What this changes

1. **Patch `009` cannot be completed by adding MAC-internal rings.** The missing data path is the
   PIE ring engine plus a minimal LSW egress/port map (sections 5 and 6). Doing block 5 without
   block 4 only proves that the MAC configures a link.
2. **The TARGET-DESIGN S3 interim "one MAC + fixed-link to prove the path" is [U], probably
   impossible.** No evidence shows a host-DMA path in the MAC IP; the vendor MAC driver imports no
   DMA or netdev allocator. Plan the interim as "PIE ring/one queue + one GEMAC port
   configuration", not "MAC ring".
3. **The "ring descriptors" the repo already knows are a different engine.** The Wi-Fi host rings
   documented in [phase42/THE-RING-MAP.md](phase42/THE-RING-MAP.md), [phase13/rings.md](phase13/rings.md),
   [phase27/vendor-live-ring-shape.md](phase27/vendor-live-ring-shape.md) and
   [phase43/THE-CHANNEL-MAP.md](phase43/THE-CHANNEL-MAP.md) are the **Wi-Fi endpoint's DR/SR DMA
   rings** (BAR0/BAR2 viewports), not PIE rings. Do not reuse those descriptor layouts; the PIE
   descriptor format is [U] (section 6.2).
4. **The vendor sk_buff ABI is irrelevant to the clean-room plan** ([VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md)
   section 3.1): a port that owns both the netdev and the ring chooses its own skb path; only the
   **hardware** descriptor layout must match what the PIE consumes.

## 2. Block A - peripheral pinctrl (`hi_kpinctrl`)

Framework: the **mainline pinctrl subsystem**, a new driver under `drivers/pinctrl/` plus a YAML
binding. This is the standard pinctrl+pinconf shape; no SoC framework choice is open.

### 2.1 What the repo already knows

**[V] DT description (vendor):** one node `pinctrl@0x14900000` with `compatible =
"hsan,luofu-peri-pinctrl"` and two windows, `reg-names = "mux", "cfg"`: `0x14900100` size `0x3c`
(15 x 32-bit mux registers) and `0x14940000` size `0x100` (64 x 32-bit config registers) —
`soc/luofu-r116-pinned.dts:1270-1360`. The separate `iomux` node (JTAG select offset `0x108`,
value `1`) is at `soc/luofu-r116-pinned.dts:1032-1036`. Eight board pin-state nodes are referenced by the
vendor tree (nine states, since the spi group carries a default and a sleep state): `jtag_gpio2..5_default_state`, `led0_1_default_state`, `led1_1_default_state`,
`pmupwm_default_state` (function `avs`), and a `spi` group with `spi_default_state`/
`spi_sleep_state` (same function/group pair in both states). [V]

**[V] Table content (our transcription of the vendor module):** `opensource/lab/luofu-pinctrl/README.md`
and `opensource/lab/luofu-pinctrl/luofu-pinctrl.c` carry the decoded mapping from
`hi_kpinctrl.ko` (17,636 B, `vermagic 5.10.201 SMP mod_unload ARMv7`): **37 pins** (`pin0..pin36`,
desc array `.data+0x428`), **24 groups with real pin lists** (`.data+0x68`; the lab driver's group
table names all 24: `sfc_grp`, `uart1_grp`, `uart0_grp`, `mdio1_grp` (pins 10,11), `spi_grp` (pins
13,14,15,23), `i2c_grp`, `rgmii0_grp`, five `eth_act_ledN_grp`, `led0_0/led1_0/led0_1/led1_1_grp`,
`clk_test_grp`, `avs_grp`, `phy_clk_grp`, `usb_grp`, `jtag_gpio2..5_grp`), **24 functions** (1:1 with
groups), and per-pin mux data `{reg_off, shift, func_name}` x5 plus `cfg_off = 4*i` (vendor
`.text+0x348` `hi_pinmux_set_mux`; `.text+0xa8` `hi_pin_config_set` for the type dispatch).
`set_mux` in the lab driver is implemented from the vendor algorithm; the **pinconf setters are
no-op stubs**. [V]

**Why it is load-bearing:** parent AGENTS and ledger **2026-11-15 01:30** record that
`hi_kpinctrl` is the first module loaded by `/etc/rc.d/luofu`, and that with it absent "the MDIO
and Ethernet pins are never muxed, no PHY is visible on the bus, eth2 never comes up, and the box is
unreachable" — a box that then needs the watchdog fallback to recover. [V] parent ledger
`mem-entries.md:22196`.

**[V] Strap/board-id facts and their limits:** the 4-bit board strap that selects the U-Boot DT
overlay is a loader input (`board_id=3` inferred from the applied overlay; [SKELETON-SMOKE.md](SKELETON-SMOKE.md)
fourth session, open question 2; [../target-skeleton/docs/TRAILER-FORMAT.md](../target-skeleton/docs/TRAILER-FORMAT.md)
section 6 [I]). It is **not** a register in this pinctrl block and [V] has no pinctrl property. The
separate always-on pinmux/strap-ish window at `0x40000000` (phase-6 dumps; see
[phase6/register-windows.md](phase6/register-windows.md) section 2.1 "efuse / strap / pinmux block",
including the PCIe L1SS pin writes at `0x40000554`/`0x400005bc` from the vendor "plat" object) is a
**different block**: SoC control/efuse words, not the `0x14900000` peripheral pinctrl; writing it is
outside any port plan here (also section 4.3, PCIe L1SS). [V] for the dump record; [U] for the
register semantics.

**[V] A gap in our earlier MDIO framing:** ledger 2026-10-11 (PHASE 53) records that the vendor
`hi_kmdio` read/write is a bit-bang that "switches between TWO PINCTRL STATES per transfer
(`mdio1_mdc1`/`mdio1_mdc2`, `pinctrl_select_state` at 0x2b4)". Our landed MDIO driver (patch
`007`) instead uses the recovered MMIO register/polled handshake and has no pinctrl-state consumer
([../target-skeleton/docs/port-notes/net/SUMMARY.md](../target-skeleton/docs/port-notes/net/SUMMARY.md)
lines 34-42). The two are not equivalent: the port has **not** modelled how the MDIO pins are
muxed/driven. [V] ledger; [U] whether our MMIO path is complete without the state switching.

### 2.2 What is missing and where it must come from

| missing piece | status | source |
| --- | --- | --- |
| pinconf field semantics (vendor `luofu_pin_config_set`, config type dispatch: bias/drive/slew bitfield inserts) — masks, widths, values, apply order | [U] | **further RE** of `hi_kpinctrl.ko` (small: 17,636 B, 50 symbols, table offsets already mapped; about one host session) and/or **GPL request item 2** — `hi_kpinctrl` sources, [GPL-REQUEST.md](GPL-REQUEST.md) "Requested artifacts checklist" item 2 |
| default/initial pin state applied at probe (the load-bearing behaviour) — which mux/cfg bits the vendor probe writes vs. leaves to consumers | [U] | same RE; bench read of both windows on the vendor kernel (read-only devmem, or a 5.10 lab module, section 7.2) |
| reset handling for the block | [U] | the vendor node has **no** `clocks`/`resets` (`soc/luofu-r116-pinned.dts:1270`); do not invent a reset cell. The lab driver's reset-deassert note is a TODO, not evidence. |
| upstream binding: compatible name, group/function naming stability (the vendor uses bare group names such as `mdio1_grp`), whether to carry all vendor state nodes or only what consumers need | [I] | design decision; the 24 groups/functions are transcribed [V] |
| consumer states for our drivers: which groups the MDIO/GEMAC/UART0/UART1/FMC nodes must select | [U] | our DTS currently has no pinctrl consumers; the vendor states listed above are the only states the vendor DT uses |
| iomux JTAG-select node: part of this driver or a separate syscon (vendor binds `hsan,iomux`) | [U] | RE of the vendor tree / the vendor "plat" object |

### 2.3 Port plan

**Target framework:** `drivers/pinctrl/hisilicon/pinctrl-luofu.c` (new), Kconfig
`CONFIG_PINCTRL_LUOFU`, using `pinctrl_register` + `pinmux_ops` + `pinconf_ops` +
`pinconf_generic_dt_node_to_map()` (the lab driver already has the right shape; it needs uphill
cleanup, real pinconf and a binding). [I]

**Binding sketch** (names indicative, not a claim of upstream agreement):

~~~yaml
compatible: "hisilicon,luofu-pinctrl"
reg:
  - description: mux register window (0x14900100, 0x3c)
  - description: pin config register window (0x14940000, 0x100)
reg-names: [mux, cfg]
# pinmux/pinconf states use the standard generic form:
#   <state> { <name>_pins { function = "<function>"; groups = "<group>"; }; };
~~~

Consumers then reference states, e.g. `mdio0 { pinctrl-names = "default"; pinctrl-0 = <&mdio1_default>; }`
once a real state exists. **The vendor state names are not an upstream ABI**; carry only states a
consumer uses, and keep group/function names as the vendor tables define them (they are the
hardware's names). [I]

**Implementation steps:**

1. Host-only: finish the pinconf decode from `hi_kpinctrl.ko` (RE, one session) and transcribe the
   field encodings with a comment per field; no register writes beyond the requested state (the lab
   driver's discipline).
2. Host-only: binding + driver + Kconfig/Makefile + `config-6.18` delta; board DTS consumers
   (MDIO, GEMAC, UART0/1 if not already default, FMC if needed) reference states. Compile in the
   6.18 tree with `CONFIG_WERROR=y`, `dt_binding_check`, `dtbs_check` — the mechanism used by
   patches `004`-`009` ([SKELETON-SMOKE.md](SKELETON-SMOKE.md)).
3. Bench: first confirm the console/UART path is not broken by the driver's default state (the risk
   that matters: a wrong mux on the console or NAND pins); then read both windows back and compare
   with the vendor's values.

**Dependencies:** none hard. CRG cells are **not** used by this node today [V]. Consumers: MDIO
`007` (pin routing is not modelled yet, 2.1), GEMAC `009`/FMC `006`. Interacts with the restart
contract only in that a wrong mux can make the box unreachable (7.3).

**Risks:**

- **The highest practical risk is the load-bearing pin state**: the vendor's probe/initial state is
  what makes the MDIO/ethernet pins work; a clean driver that only serves requested states may
  leave the board unreachable. The plan must reproduce the vendor's initial state (RE + bench
  read-back), not just the table. [V] ledger 2026-11-15 01:30; [U] what exactly it writes.
- The MDIO pin-state question in 2.1: either our `007` MMIO path is independent of muxing, or the
  port needs the `mdio1_mdc1/mdc2` state model. Decide with one read-only comparison; do not assume
  [I].
- Strap bits are **not** part of this block; do not add a "strap" property (no vendor DT property;
  different register window).
- No datasheet exists (Hi5671Y is NDA-only, [DOCUMENTATION-HUNT.md](DOCUMENTATION-HUNT.md) section
  2); the module is the only register source.

**Effort (rough, focused sessions):** 2-3 host sessions + 1-2 attended bench sessions.

## 3. Block B - thermal (`hi_tvsensor`, vendor trips, net-cooling device)

Framework: the **mainline thermal** subsystem (of-thermal sensor + DT thermal zone). The sensor
driver is small; the policy is the interesting part.

### 3.1 What the repo already knows

**[V] DT (vendor):** `tvsensor@14900500` with `compatible = "hsan,tvsensor"`, `reg = <0x14900500
0x14>`, `hwver = <3>`, `int-base-offset = <0xc>`, `trim = <0 0>`, `addition-en`,
`efuse-trim-en`, `interrupts = <SPI 0>` (`soc/luofu-r116-pinned.dts:1477-1489`);
`thermal-zones/cls0` with `polling-delay = <1000>`, `sustainable-power = <2000>`,
`thermal-sensors = <&tvsensor0>`, one cooling map and two trips
(`soc/luofu-r116-pinned.dts:1432-1468`):

| trip | type | `temperature` | `hysteresis` |
| --- | --- | --- | --- |
| `trip-point@0` | passive | `<115000>` (115 C in millidegrees) | `<3000>` |
| `trip-point@2` | critical | `<125000>` (125 C) | `<3000>` |

(`trip-point@1` is absent from the vendor tree; the `@0`/`@2` naming is the vendor's. The vendor
script loads `hi_knet_cdev` before `hi_tvsensor` — the cooling device must exist first.) [V]

**[V] Sensor conversion (decoded and numerically verified):** `hi_tsensor_hw_v4_get_temp` reads
`[base + 0x24] & 0xffff` and computes `((165 * raw - 32835) / 100) * 1000 - 40000` millidegrees C
(raw 240 -> 27.0 C; slope 1.65 C/LSB). This is written out in
`opensource/lab/luofu-thermal/luofu-thermal.c` lines 1-107 and is the one thermal fact beyond
doubt. [V] Note the **window-size mismatch** [V]/[U]: the driver reads `+0x24` while the vendor DT
declares a `0x14`-byte `reg`; a real driver needs the window widened to cover every register it
touches (the vendor's `devm_ioremap(platform_get_resource())` page-granular mapping masked this).

**[V] Vendor module surface:** `parent:rootfs-2.4.15/lib/hisilicon/ko/hi_tvsensor.ko` (9,840 B,
`vermagic 5.10.201 SMP mod_unload ARMv7`, 60 symbols, no depends): `hi_tsensor_hw_get_temp` (v1,
reads `+0x4`), `hi_tsensor_hw_v4_get_temp` (`+0x24`), `hi_tsensor_hw_v4_set_th`, `set_trim`,
`set_int_mask`, `clr_int`, `hi_tvsensor_hw_v4_drv_register`, `hi_tvsensor_irq_handler`,
`hi_tvsensor_critical_event_work`; imports `devm_thermal_zone_of_sensor_register`,
`devm_request_threaded_irq`, `syscon_regmap_lookup_by_phandle`, `regmap_read/write`, `ioremap`. So
the vendor driver is a standard thermal sensor driver plus a threshold/interrupt path (trim from
syscon/efuse; interrupt-driven threshold events and a critical-event work). [V] (symbols/imports
from the local fixture).

**[V] The cold end of the thermal policy:** the cooling map points at `cooling_dev0` =
`compatible = "hsan,net-cdev"`, `#cooling-cells = <2>`, `car-rate-table = <0x0 0x7a120 0x1e848
0x7d00>`, `dev-names = "eth0" "eth1" "eth2" "eth3" "eth4"` (`soc/luofu-r116-pinned.dts:419-426`;
the map at 1432-1468). The driver is `hi_knet_cdev.ko` (5,156 B): a
`devm_thermal_of_cooling_device_register` cooling device named `hi_net_cdev` with
`get_max_state/get_cur_state/set_cur_state`, strings `car-rate-table`, `dev-names`,
`net-flow-car`. The throttle target is therefore **network rate limiting** (committed access
rate/flow shaping), i.e. the LSW, not CPU frequency. [V] The exact `car-rate-table` field semantics
are [U]; this cooling device is **not** part of a first thermal port (it needs the LSW queue/CAR
model, section 5).

**[V] An independent (historical) temperature anchor:** phase-6 dumps of the `0x40000000` window
name offset `0x4110` `temp` (die temperature) as printed by the vendor `dev_status_check`
([phase6/register-windows.md](phase6/register-windows.md) section 2.4). That is a read-only
cross-check anchor, not the tvsensor block.

### 3.2 What is missing and where it must come from

| missing piece | status | source |
| --- | --- | --- |
| threshold/interrupt/enable register model (`set_th`, `set_int_mask`, `clr_int`, `set_en`, `set_mode`, trim fields) — offsets and encodings | [U] | **further RE** of `hi_tvsensor.ko` (small, well symbolized; temp is `+0x24`, threshold area near `+0x14`/`+0x20`, trim/enable near `+0x0c`/`+0x10` — do not trust those guesses until decoded function-by-function) and/or **GPL item 2** |
| whether the 115 C/125 C trips are the right policy for our kernel (vendor values may assume the net cooling device; with no cooling device a passive trip only logs) | [U] | bench measurement against the vendor's thermal zone (read-only sysfs) + a decision; do not invent trip values |
| `sustainable-power`/`polling-delay` units and whether the sensor is polled or IRQ-driven on this board (vendor IRQ = SPI 0, shared with nothing else in the DT) | [U] | RE + bench |
| DT window size (reg `0x14` vs used `0x24`) and whether the block needs clocks/resets (vendor node has none) | [U] | RE / bench read |
| cooling policy for v1: sensor-only (no cooling maps), cpufreq cooling (no upstream cpufreq driver yet), or port `hi_knet_cdev` after LSW | [I] | design decision; recommendation: sensor + trips first, cooling deferred |

### 3.3 Port plan

**Target framework:** `drivers/thermal/luofu_tvsensor.c` (new), `CONFIG_LUOFU_TVSENSOR`; sensor
registered through `devm_thermal_zone_of_sensor_register`; board DTS carries the thermal zone with
the vendor trips (or revised ones after bench). [I]

**Binding sketch:**

~~~yaml
compatible: "hisilicon,luofu-tvsensor"
reg: <0x14900500 0x100>          # widened from the vendor 0x14 to cover the used registers
#thermal-sensor-cells: 0
interrupts: optional (SPI 0)
# no clocks/resets unless bench proves otherwise
~~~

~~~dts
thermal-zones {
    cls0 {
        polling-delay = <1000>;
        thermal-sensors = <&tvsensor0>;
        trips {
            trip-point@0 { temperature = <115000>; hysteresis = <3000>; type = "passive"; };
            trip-point@2 { temperature = <125000>; hysteresis = <3000>; type = "critical"; };
        };
        /* v1: no cooling-maps; the vendor net-cdev path is deferred to the LSW lane */
    };
};
~~~

**Implementation steps:** RE the register model (host, 0.5-1 session) -> driver + binding + DTS
zone (host, 1 session) -> compile/`dt_binding_check`; bench: read the sensor through our driver on a
booted 6.18 kernel and compare with the vendor sysfs thermal zone at the same time, then decide the
trip table. [I]

**Dependencies:** none hard: the tvsensor node has no clocks/resets and does not use the CRG
syscon. **The optional cooling device depends on the LSW** (CAR/shaping, section 5) and is out of v1
scope. Warn: the vendor's passive trip currently has **no consumer** in a clean port; leaving the
map in place with no cooling device is a DT warning and a bad review smell, so v1 should drop the
cooling map and say why. [I]

**Risks:** treating the vendor 115/125 C trips as validated (they are vendor data, not our
measurement); the IRQ path (if used) needs the threshold semantics; a wrongly programmed trim could
disable the sensor (prefer read-only first: the conversion is proven store-free).

**Effort:** 1-2 host sessions + 1 attended bench session (plus 0.5-1 host session if
`hi_knet_cdev` is ported later, which also needs the LSW).

## 4. Block C - PCIe host controller and the PCIe Wi-Fi device

Split verdict: **the host controller is a legitimate port** (DWC IP + recoverable vanilla
programming); **the Wi-Fi endpoint device is [X]** in the same sense as
[TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.3, because its driver is a closed GPL-declared
module and the radio firmware is a blob.

### 4.1 What the repo already knows - host controller

**[V] DT description (two root complexes):** `pcie@0x10160000` (domain 0, channel 0) and
`pcie@0x10164000` (domain 1, channel 1), `compatible = "hsan,pcie"` + `"hsan,acp-pcie0"` /
`"hsan,acp-pcie1"`, `reg-names = "dbi" "misc" "cfg" "mem" "io"`:

| RC | dbi | misc | cfg | mem | io |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x10160000 0x1000` | `0x10161000 0x3000` | `0x50000000 0x1000` | `0x40000000 0x2000000` | `0x48000000 0x800000` |
| 1 | `0x10164000 0x1000` | `0x10165000 0x3000` | `0x68000000 0x1000` | `0x58000000 0x2000000` | `0x60000000 0x800000` |

plus `bus-range = <0 0xa>`, `linux,pci-domain`, `interrupts = <radm SPI 59, linkdown SPI 69>`
(RC0) and SPI `63/70` (RC1), `resets = <&crg 0x34 0xc>..<&crg 0x34 0xf>` (RC0) /
`0x10..0x13` (RC1), `clocks = <&crg LUOFU_CLK_PCIE0|1>`, `acp-gate = <0x10100174 0x2000000 0>`
(RC0) / `<0x10100174 0x4000000 0>` (RC1), and the vendor `iatu_rc`/`iatu_ep` tables
(`soc/luofu-r116-pinned.dts:1205-1252`; decode in [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md)
section 5). [V]

The two `iatu_rc` tables are the outbound window plan; three viewports each, e.g. RC0:
`{0,4, 0x80000000, 0x50000000, 0..0x57ffffff, 0}` (cfg), `{1,0, 0x80000000, 0x40000000,
0..0x47ffffff, 0x40000000}` (mem), `{2,2, 0x80000000, 0x48000000, 0..0x4fffffff,
0x48000000}` (io), and the same shape on RC1 with bases `0x68000000/0x58000000/0x60000000`
(`soc/luofu-r116-pinned.dts:1216`, `:1240`; corroborated by [phase18/inbound-map.md](phase18/inbound-map.md)
Part A and the vendor `pcie_iatu_mp13.c` region names in ledger **2026-10-12 PHASE 70**). [V]

**[V] Measured vanilla-programming facts (live vendor RC, read-only):** `hi_pcie.ko` (21,596 B,
`depends: hi_basic`, 173 symbols) is a modern PCI host-bridge driver: it imports
`devm_pci_alloc_host_bridge`, `pci_scan_root_bus_bridge`, `pci_bus_assign_resources`,
`pci_bus_size_bridges`, `pci_add_resource`, `devm_request_pci_bus_resources`, `devm_clk_bulk_get`,
`reset_control_*(assert/deassert)`, `of_get_named_gpio_flags`, and exposes
`hi_pcie_probe/remove`, `hi_pcie_get_ltssm/enable_ltssm/disable_ltssm`,
`hi_pcie_enable/disable/clr_linkdown_irq`, `hi_pcie_get_link_status`, `hi_pcie_read_conf`,
`hi_pcie_enable_dev/disable_dev`, `hi_pcie_enable_aspm`. [V] local fixture.

Measured RC state (recorded in [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md), PCIe design and
arm-B4 addenda): `dbi+0x004 = 0x0007` (vendor-written command), `dbi+0x07c = 0x734c12` (Link
Capabilities), `dbi+0x082 = 0x7012` (Link Status; DL_ACTIVE bit 13 set, 5 GT/s x1),
`dbi+0x080 = 0x0000`, `dbi+0x80c = 0x012c`, iATU selector `dbi+0x900 = 2` with the `iatu_rc[2]`
base/limit matching the DT, and both RCs' endpoint `cfg` reads
`{0x000=0x59e7, 0x004=0x0006, 0x008=0x02800000, 0x02c=0x19e5}` — all four match. [V] (local bench
evidence; reproduced in the committed plan file).

**[V] Hard access rules the port must keep:** the `misc`/`0x10161000` window is **write-only**; a
userspace devmem read of it panicked the box ([phase20/host-window.md](phase20/host-window.md)
C.4; [TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1 PCIe row). Every `dbi`/`cfg` register access
must be a **4-byte-aligned dword** read (a 16-bit `readw` at `dbi+0x082` external-aborted just like
the misaligned `readl`); the lab driver's aligned-dword accessor and the LTSSM read from
`DBI+0x728/0x72c` are the safe substitutes ([../lab/luofu-pcie/README.md](../lab/luofu-pcie/README.md)
"DWC-style register accessors"; [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md) arm-B4 entry). [V]

**[V] Not a working host controller today:** `opensource/lab/luofu-pcie/luofu-pcie.c` (1,664
lines) is a read-only inventory frame with a commented host-bridge scaffold; it does not call
`pci_host_probe()`, programs no iATU window, and its compile-gated write path is not validated
([TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1 PCIe row). [V]

### 4.2 What the repo already knows - the PCIe device (Wi-Fi)

**[V]** two endpoints, `0000:00:00.0` and `0001:00:00.0` (one per radio), vendor/device `59e7:0005`,
class `0x028000`, subsystem vendor `19e5`, 64-bit non-prefetchable BARs of 16 MB (BAR0), 16 KB
(BAR2), 8 MB (BAR4), MSI count 1/1, IRQ 207/209 ([DRIVER-BLACKBOX.md](DRIVER-BLACKBOX.md) section
1). The endpoint driver side is split between `hi5622v100_plat.ko` (371,708 B; `bal_bus_*` PCIe
bus-abstraction layer using `pci_enable_device`, `pci_read_config_*`, `dma_map/unmap`, HCC
callbacks; imports `hi_net_woc_skb_free`) and `hi5622v100_wifi.ko` (3,564,728 B; 0 mac80211
symbols; closed FIRMWARE.bin; [PORT-SURFACE.md](PORT-SURFACE.md); [DRIVER-BLACKBOX.md](DRIVER-BLACKBOX.md)
sections 2-3). The device-side inbound window map (six iATU viewports on BAR2 `0x41800000`; register
map in [phase18/inbound-map.md](phase18/inbound-map.md); region names in ledger 2026-10-12 PHASE 70)
is recovered but belongs to the **endpoint** driver, not the host controller. [V]

**A host-controller port does not need the Wi-Fi driver.** It needs only to program the RC, map
ranges, scan the bus, assign resources and enumerate the two functions — a worthwhile, testable
milestone even with the radio driver [X]. [I]

**[V] Endpoint power/reset GPIOs are board-overlay data**, not base-DT data: both pcie nodes get
`capability = 3`, `gpio-delay = "2"`, and `pcie-gpios` (gpio `0x14`/`0x15`/`0x17` on board_id 0,
`0x14`/`0x15`/`0x1c` on board_id 3) from board overlay fragments 1/2
([soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8; [TARGET-DESIGN.md](TARGET-DESIGN.md)
section 6.2). [V] The `capability`/`gpio-delay` properties are vendor-only; upstream needs a
documented GPIO binding or an equivalent (4.3).

### 4.3 What is missing and where it must come from

| missing piece | status | source |
| --- | --- | --- |
| a real host-controller implementation: RC mode write, iATU viewport programming from `ranges`, cfg accessor (aligned dword window), LTSSM poll, linkdown/radm IRQ, bus scan/assign | [U] | host-only implementation from the measured facts + RE of `hi_pcie.ko` (21,596 B, 173 symbols; the vendor `hi_pcie_probe` order is recoverable) and/or GPL item 2; the DWC host framework `drivers/pci/controller/dwc/pcie-histb.c`/`pci-hisi.c` gives the shape (not a drop-in; the wrapper regs differ) |
| write-path validation: no store has ever been measured on the RC (the lab write path is compile-gated only) | [U] | **attended bench**; must keep the misc window write-only and every access aligned |
| `acp-gate` semantics: `hi_acp.ko` scans `hsan,acp-*` nodes, reads `acp-gate`/`acp-pre-cfg-attr`/`acp-later-cfg-attr`, ioremaps `0x10100174` and programs cache/coherency attributes through a blocking notifier; it is self-contained (no other module imports it) | [V] mechanism; [U] whether a Linux port needs it for correct PCIe/PIE DMA coherency | RE of `hi_acp.ko` (5 symbols, tiny) + bench; model as a small SoC driver only if bench shows a need. Do **not** read `0x10161000` to test this |
| L1SS pinmux writes done by the vendor "plat" object at `0x40000554`/`0x400005bc` (phase-6 window) | [V] record; [U] relevance if L1SS/ASPM stays disabled | bench/RE; a Linux DWC port can keep ASPM/L1SS off initially (the vendor has `hi_pcie_enable_aspm` and `hi_pcie_init_cmd_status_reg`) |
| endpoint power GPIO sequencing / reset delays (`pcie-gpios`, `gpio-delay`, `capability`) | [V] data; [U] upstream binding shape | design decision: model as documented board GPIOs (`reset-gpios` etc.) or a small "pcie power" node; do not carry vendor property names |
| MSI/INTx routing for the endpoints, and whether the host must program an ACP/RC window for the endpoint firmware's inbound map | [U] | bench enumeration first; endpoint functionality is [X] |
| the Wi-Fi endpoint driver + firmware licensing | **[X]** | GPL items 7-8; [GPL-REQUEST.md](GPL-REQUEST.md); [PORT-SURFACE.md](PORT-SURFACE.md) |

### 4.4 Port plan

**Target framework:** a DWC-based host driver, most likely
`drivers/pci/controller/dwc/pcie-luofu.c` on top of `pcie-designware-host.c` (or a standalone
`drivers/pci/controller/pci-luofu.c` if the `misc` block turns out non-DWC enough — the vendor has a
4 KB dedicated `cfg` window instead of ECAM, so the DWC ECAM path may not fit; that decision is an
output of step 1, not an assumption). [I]

**Binding sketch:**

~~~yaml
compatible: "hisilicon,luofu-pcie"
reg:
  - description: DWC DBI (0x10160000, 0x1000)
  - description: RC misc (0x10161000, 0x3000)     # write-only; never read
  - description: config window (0x50000000, 0x1000)
  - description: memory window (0x40000000, 0x2000000)
  - description: I/O window (0x48000000, 0x800000)
reg-names: [dbi, misc, cfg, mem, io]
ranges: ...        # derived from the iatu_rc table, one entry per viewport
interrupts: radm + linkdown (SPI 59/69 rc0, 63/70 rc1)
interrupt-names: [radm, linkdown]
clocks: <&crg LUOFU_CLK_PCIE0>
resets: <&crg 0x34 0xc>, <&crg 0x34 0xd>, <&crg 0x34 0xe>, <&crg 0x34 0xf>
reset-names: [apb, pcs, phy, ctrl]
# board GPIOs for endpoint power/reset (names TBD in review)
~~~

**Implementation steps:**

1. Host-only: decide DWC-core vs standalone from the vendor probe RE (the 14-step order recorded in
   [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md): clk_bulk_enable -> 4 reset deassert -> misc
   RC-mode write -> iATU -> endpoint power -> `DBI+0x04=7` -> LTSSM); write the driver against the
   measured windows; convert the `iatu_rc` tables into `ranges` (the driver derives viewports from
   `ranges`, not from a vendor DT property).
2. Host-only: binding + DTS nodes (status `disabled` until bench), `dt_binding_check`,
   `dtbs_check`; `config-6.18` delta (`CONFIG_PCI`, `CONFIG_PCI_DOMAINS`, `CONFIG_PCIEPORTBUS`,
   the controller symbol, `CONFIG_PCI_MSI` — note `CONFIG_PCIE_DW` is select-only in 6.18,
   [SKELETON-SMOKE.md](SKELETON-SMOKE.md) second session).
3. Bench (attended): boot the 6.18 kernel (section 7.2), confirm link-up via the safe predicates
   (`DBI+0x082` aligned dword / `DBI+0x728`), then `lspci` enumerates `59e7:0005` twice. Never read
   `0x10161000`; never write the CA `0x400392f0`.
4. Only after a clean enumeration: endpoint BAR probe via config space (read-only), integer
   assignment, and record whether the vendor userspace still loses Wi-Fi (expected).

**Dependencies:** CRG patch `004` (gate clocks + four resets), and the restart contract (7.3) for a
safe bench. Interacts with the ACP question (4.3) and, for endpoint DMA, with the Wi-Fi lane [X]. No
dependency on FMC/MDIO/PHY for host enumeration.

**Risks:** the forbidden misc window and the aligned-access rule are the two that can cost a box
(the project already has a panic and a fire-window from getting these wrong); the RC write path is
unvalidated; L1SS/ASPM pinmux and ACP attributes are additional unknowns; Wi-Fi will not work even
after enumeration, so this block must not be sold as "Wi-Fi support".

**Effort:** 6-10 host sessions + 2-3 attended bench sessions. The endpoint device is [X] and not
included.

## 5. Block D - internal LSW switch (`hi_lsw_*`, `hi_swa`)

This is the largest SoC-side item in the project ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 8.1).
There is **no driver source in either tree** — the lab tree has neither Kconfig nor source
([../lab/luofu-kernel/fragment-audit.md](../lab/luofu-kernel/fragment-audit.md): "the other three
(`HWRNG`, `EFUSE`, `LSW`) have no source in the lab tree, and no Kconfig entry in the build tree").
[V]

The evidence-graded LSW specification - module layering, the synthetic module-id register map, the
port/queue/qid model, the switchdev design, the rootif correction and the D0-D2 plan - is
[LSW-SPEC.md](LSW-SPEC.md) (2026-11-18, 1,375 lines). It records two findings that change this
section's assumptions: (a) `hi_swa`'s switchdev/port-object handlers are compiled **stubs** in the
shipped build, and the vendor sets `hi_kcfe_pub.g_intf_bypass=1` before loading it, so the vendor
does not offload bridge/VLAN objects; (b) the rootif write is a vendor-userspace compatibility
layer (a 32-slot name-indexed CFE table driven by NETDEV_UP/DOWN/UNREGISTER), not a hardware API,
so our driver must not reproduce it.

### 5.1 What the repo already knows

**[V] Register description (vendor DT):**

| node | windows (reg-names) | resets / clocks | other |
| --- | --- | --- | --- |
| `lsw_dp` (`compatible = "hsan,dp"`, `hwver = <0x250>`) | `queue 0x87b00000 0x400000`, `flow 0x87f00000 0x100000`, `dp 0x11000000 0x80000`, `eps 0x11280000 0x80000`, `eqm 0x11200000 0x80000`, `fam 0x11100000 0x80000`, `ips 0x11080000 0x80000` | resets `(0x30,0)`/`(0x30,1)`; clock `lsw_dp_clk` | `port_bitmap = <0xf01 0x1ff0>` |
| `lsw_pfe` (`compatible = "hsan,pfe"`, `hwver = <0x250>`) | `ifc 0x12100000 0x80000`, `l2 0x12180000`, `napt 0x12280000`, `mcc 0x12380000`, `opp 0x12400000`, `pa 0x12080000`, `pe 0x11180000`, `pfe_glb 0x12000000`, `prbs 0x11380000`, `pub 0x12480000` (all `0x80000`) | resets `(0x30,3)`/`(0x30,4)`; clock `lsw_pfe_clk` | — |
| `lsw_woe` (`compatible = "hsan,lsw_woe"`, `hwver = <0x251>`) | — | — | `interrupts = SPI 84,83,82,81,80`; `interrupt-names = band0_comp_irq, band1_comp_irq, band2_comp_irq, band0_txba, band1_txba`; `dma-coherent` |

(`soc/luofu-r116-pinned.dts:1078-1123`; the overlay **rebases the `queue` window
`0x87b00000 -> 0x80000000`**, [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8 — which
base a Linux driver must program is [U]; do not "fix" the address without measuring.) [V]

**Port model facts:** the five GEMAC nodes carry `port = <8..12>`; the board overlay disables
gemac0/port 8, so the four live ports are 9-12 = `eth0..eth3`; the vendor board layer maps
`R116: WAN = eth3, LAN = eth0 eth1 eth2` (`parent:rootfs-2.4.15/etc/board.d/02_network`, [V] local
fixture; consistent with the 2026-11-14 live stack "br-lan members eth0...eth3" in
[VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) section 2.4). The exact bit meaning of
`port_bitmap = <0xf01 0x1ff0>` (which word indexes which block, how it maps to `port = <8..12>`) is
**[U]**; `hi_lsw_dp` reads the property by name (module string `get dts port_bitmap err`). [V]

**[V] Vendor module surface (local fixtures; sizes/symbols):**

| module | size / depends | what the symbols say |
| --- | --- | --- |
| `hi_lsw_pub.ko` | 26,052 B; `ksecurec,hi_basic` | register access + table manager: `hi_lsw_read/write/read64/write64/read_bits/write_bits/read_tab128/write_tab128`, `hi_lsw_wait_set/clr`, `hi_lsw_reg_module/unreg_module`, `hi_tab_mgr_*` (create/alloc hash/direct/get_data/aging) — the shared register/table access layer for the blocks below |
| `hi_lsw_dp.ko` | 67,164 B; `hi_lsw_pub,hi_basic,hi_kpie,ksecurec` | datapath/queue manager: `hi_queue_init/exit`, `hi_queue_get_buf`, `hi_queue_get_q_num`, `hi_queue_set_shaping/egr_shaping/sch/buf`, `hi_queue_opp_map_hook`, `hi_dev_queue_*`, `hi_lsw_get_vport`, `hi_eqm_hw_init/read_tab/write_tab`, `hi_ips_queue_init`, `hi_flow_hook_set/get/del`, `hi_pie_map_init`, `hi_mem_info_init`, `hi_dp_probe`; undefined imports include `hi_pie_get_gro_ch_num/get_gro_ch_start/get_woc_ch_num/get_woc_ch_start` and `hi_net_queue_hook` |
| `hi_lsw_pfe.ko` | 52,412 B; `hi_lsw_pub,hi_lsw_dp,hi_basic,ksecurec` | packet/flow forwarding engine: `hi_pfe_ifc_init`, `hi_pfe_napt_init/exit`, `hi_pfe_mcc_init`, `hi_pfe_opp_init`, `hi_pfe_pub_init`, `hi_pfe_init`, table ops (`ifc_write/dump/del`, `nhp_write/dump`, `tag_write/dump`, `l3intf_write/dump`), `port_qid_map_set`, `qid_map_set`, `hi_port_set_mirror`, CAR (`car_write/car_del/car_profile_*`), NAPT flow v4/v6 write/del/dump, `hi_pfe_drop_cnt`/`hi_pfe_tocpu_cnt` |
| `hi_swa` (`hi_swa.ko`) | 11,340 B; `hi_kcfe_pub,hi_basic,ksecurec,hi_kpie,hi_kcfe_lrn` | **switchdev glue**: `swa_switchdev_event`, `swa_switchdev_blocking_event`, `swa_handle_port_obj_add/del`, `swa_netdevice_event/check`, rootif management `hi_swa_add_rootif/down_rootif/get_rootif/unregister_rootif/dump_rootif`, `swa_dfx_update_wanif/update_vlan_filter`; strings `dev=%s,intf_type=%d,vlan_filter=%u,enable=%u`, `wan_port`, `rootif_dump`; imports `hi_cfe_add_rootif/add_subif/del_rootif/del_subif/flush_by_mac`, `hi_net_get_port` |
| `hi_kcfe_pub.ko` + `hi_kcfe_lrn*` | CFE layer | classifier/learning: `hi_cfe_add_rootif`/`add_subif`/`del_*`, `hi_cfe_flush_by_mac`, `hi_cfe_tab_*`, `hi_cfe_add_hook`, `hi_cfe_get_igr_port`, plus L2/L3/L4/multicast/VXLAN learning modules; `hi_cfe_intf_rx` exists in `hi_kcfe_pub` ([VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) section 3.4) |
| `hi_kpie.ko` | 68,524 B | the host packet engine that owns the netdev/rings (section 1); `hi_net_dev_probe` fills `g_api_priv` and is what `hi_kmac` calls with the port descriptor |
| `hi_khslink.ko` | 21,508 B | HSLINK: ARP table and skb-header manipulation (`hi_hslink_get_skbmacheader`, `hi_hslink_monitor`, `hi_hslink_tx_urarp`, `hi_hslink_arp_tab_*`); its interface-teardown events are the v17 blocker ([VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) section 2.2) |

[V] all symbol findings from the local fixtures (`arm-none-linux-gnueabihf-nm`); module sizes and
`depends` from `modinfo`.

**[V] The vendor uses switchdev, not DSA (strong evidence):** `hi_swa` contains
`swa_switchdev_event`, `swa_switchdev_blocking_event`, `swa_handle_port_obj_add`,
`swa_handle_port_obj_del`; each wired port already has its own netdev (created by the MAC/PIE
layering, section 1); there is no tag protocol or CPU-port framing anywhere in the record. The
vendor switch management surface in userspace is `swconfig`/`ethswctl`/procfs
(`/proc/hsan/net/swa/wan_port`, `/proc/hsan/cfe/*` — parent
`rootfs-2.4.15/etc/hotplug.d/gmac/08-wan-detect`, [V] local fixture). [V]

**[V] BSP prior art is not a source:** the Banana Pi TR6560 BSP (phase 50) ships its switch/MAC/PHY
runtime as prebuilt `tri_sdk_*`/`tri_kport.ko` etc. bound to `tri,*`; it has no `hsan,dp/pfe` source
and its bindings never match ([phase50/bsp-build-status.md](phase50/bsp-build-status.md) section 1,
rows "Ethernet / LSW", "Platform driver set"). [V]

### 5.2 What is missing and where it must come from

| missing piece | status | source |
| --- | --- | --- |
| every register/table semantic: port/vport numbering, queue<->port mapping, `ops/eqm/ips/fam` windows, PFE flow/ifc/l2/napt table formats, CAR/mirror/qid map, switchdev offload objects | [U] | **RE of `hi_lsw_pub`/`hi_lsw_dp`/`hi_lsw_pfe`/`hi_swa`** (the largest RE job in the project: 26 KB + 67 KB + 52 KB + 11 KB of ARM code with symbol tables; no source, no datasheet) and/or **GPL item 2** — source arrival converts most of this from RE to port |
| the port model (`port_bitmap` words, port numbers 8..12 vs 0..4, WAN/LAN assignment, how the LSW sees the PIE host queues) | [U] | RE + bench (procfs/switchdev state on the vendor kernel) |
| the queue window base (vendor DT `0x87b00000` vs overlay-rebased `0x80000000`) | [U] | bench read/RE; this is explicitly flagged "do not fix without measuring" in
[../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts) |
| PIE<->LSW queue interface and the `port_qid_map_set`/`qid_map_set` contract | [U] | RE of `hi_lsw_pfe` + `hi_lsw_dp`; bench |
| rootif/subif contract: `hi_kpie`'s `g_api_priv` table, the `hi_net_dev_probe(dev,&desc8)` descriptor layout, what `hi_cfe_add_rootif` expects, the `dev+0x21c` CFE hook | [U] | already an open gap for the bench line: [DOCUMENTATION-HUNT.md](DOCUMENTATION-HUNT.md) section 4.3; needs RE of `hi_kpie`/`hi_cfe_pub` and/or GPL items 2-4 |
| switchdev object semantics that `hi_swa` offloads (VLAN filter, port objects) | [U] | RE of `hi_swa` handlers; the upstream switchdev API itself is stable and the vendor's shape is a guide, not an ABI |
| WoC/offload features (NAPT, CAR, mirror, WoE) scope for v1 | [I] | decision: out of v1; only minimal port forwarding + bridge/VLAN offload |

### 5.3 Port plan

The full register model, table formats, port model, switchdev object mapping and the D0/D1/D2
staging plan are in [LSW-SPEC.md](LSW-SPEC.md); this section keeps the summary view and the
dependency/effort framing. The recommended driver shape is switchdev over per-port netdevs (not
DSA: no tag/CPU-port evidence, per-port MAC netdevs exist).

**Framework decision - recommendation: switchdev, not DSA.** [I] Derivation from the evidence:

- DSA expects a switch addressable through a **tagged CPU port** (DSA/EDSA tag, or per-port netdevs
  attached to one conduit). The vendor model has **no tag protocol**: the five MAC ports are
  ordinary netdevs and the CPU/host side is a queue engine (PIE) — there is no "CPU port" with a
  tag in the DT or the module symbol set (the DSA-required tag driver has no evidence here).
- The vendor's actual kernel integration is **switchdev** (`swa_switchdev_*`,
  `swa_handle_port_obj_add/del`), matching "per-port netdev + hardware offload of bridge/VLAN
  objects". Upstream switchdev gives the same model with a clean userspace contract.
- The LSW is **flow/queue based** (PFE flow/ifc/napt tables, qid maps, CAR) — closer to a
  switchdev/NETDEV offload engine than to a simple VLAN switch; DSA offers no home for the PIE
  queue model anyway.

DSA can be revisited only if a container/tag format is found in the RE (none today). This is a
design decision for the target PR review, recorded here so it is not re-litigated from scratch.

**Binding sketch (shape only):**

~~~yaml
switch@...:                       # one node per block family, or one switch node with two ranges
  compatible: "hisilicon,luofu-switch"   # name TBD; dp/pfe windows modelled as ranges
  reg: ...                        # dp: 7 windows; pfe: 10 windows (names from the DT above)
  reg-names: [queue, flow, dp, eps, eqm, fam, ips,
              ifc, l2, napt, mcc, opp, pa, pe, pfe_glb, prbs, pub]
  clocks: <&crg LUOFU_CLK_LSW_DP>, <&crg LUOFU_CLK_LSW_PFE>
  resets: <&crg 0x30 0x0>, <&crg 0x30 0x1>, <&crg 0x30 0x3>, <&crg 0x30 0x4>
  # ports as child nodes with a documented port number and a phandle to the MAC netdev;
  # CPU/host port points at the PIE node (section 6). WoE interrupts on a subnode if in scope.
~~~

**D1a is written** as patch `016` (2026-11-18, host-only, zero devices): one
`hisilicon,luofu-switch` node combining the five queue-manager and ten
classifier/forwarding MMIO windows of the vendor `lsw_dp`/`lsw_pfe` nodes (the vendor
`queue`/`flow` DDR regions are not modelled: memory the engine consumes, live base [U]),
two clocks, four resets, and an `ethernet-ports` child model whose port nodes name the
GEMAC node that owns each port's netdev. The driver validates all of it and then refuses
to program the switch (`-EOPNOTSUPP`; no MMIO access at all); the DTS node stays
`disabled`. Nothing below changes: D1b-D1d still need the attended inventory and the
reviewed write set.

**Implementation steps (two phases):**

- **Phase D1 - minimal forwarding (the wired milestone):** port map, VLAN/PVID handling for the
  four external ports, host-queue port mapping to PIE, MAC learning or static forwarding as the
  minimum to let LAN traffic flow; register through switchdev so bridge/VLAN offload can follow.
  The measurement for "done": our kernel forwards between two wired ports with no vendor module
  bound (M2-equivalent, TARGET-DESIGN section 9 item 7).
- **Phase D2 - offload features:** bridge/VLAN object offload, then (optional) flow offload
  (NAPT/CAR/mirror/WoC). D2 must not gate the target's first submission; v1 can run the LSW in a
  simple forwarding mode and leave flow offload to software.

**Dependencies:** CRG `004` (clocks/resets); the PIE data path (section 6) for the host side; MDIO
`007` / PHY `008` / GEMAC `009` for the external ports; the Wi-Fi offload path is [X] and not
required for wired LAN. Optional `hi_knet_cdev` thermal cooling depends on D1's CAR/shaping.

**Risks:** the largest technical unknown in the project; no register map, no source, no datasheet;
touching switch registers can cut the CPU/console path — every bench step must be attended with
recovery armed and one variable at a time; the `port_bitmap`/window-base ambiguity has already
produced one explicit "do not fix without measuring" note; the vendor userspace contract
(`/proc/hsan`, switchdev objects) is not required for our own userspace but must not be assumed.

**Effort:** 15-25 host sessions for D1 (RE dominates) + 4-8 attended bench sessions; D2 adds 10-15
host sessions. If the GPL source arrives, D1 drops to roughly 5-8 sessions (port instead of RE) —
the single biggest schedule change in this document.

## 6. Block E - GEMAC TX/RX ring + DMA data path (PIE)

### 6.1 What the repo already knows

**[V] The MAC side is already ported to its natural shape:** patch `009`
([../target-skeleton/target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch](../target-skeleton/target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch))
binds `hisilicon,luofu-gmac`, validates `reg`/`clocks`/`resets`/`phy-mode`/`phy-handle` and
**refuses to probe** (`-EOPNOTSUPP`, "TX/RX ring DMA is not implemented; refusing to register a
non-functional netdev"), so no netdev exists and the DTS nodes stay `disabled`
([SKELETON-SMOKE.md](SKELETON-SMOKE.md) sixth session;
[../target-skeleton/docs/port-notes/net/SUMMARY.md](../target-skeleton/docs/port-notes/net/SUMMARY.md)
lines 54-61). The lab 5.10 GEMAC frame is the same shape (688/717 lines, `ndo_start_xmit` drops
frames, its "rings are staged" line is a comment, not code —
`opensource/lab/luofu-gmac/luofu-gmac.c:307-311`). [V]

**[V] The MAC register facts that are measured, not guessed:** the vendor MAC is a phylink driver;
the link-up port value on this silicon is `PORT_MODE = 0x010c0008` (`0x14380000` unlinked reads
`0x000c0408`; mainline's different IP revision uses another encoding); offsets `PORT_MODE 0x0040`,
`MODE_CHANGE_EN 0x01b4`, `BIT_MODE_CHANGE_EN BIT(0)`; the MAC<->PHY map is
`14380000->phy2, 14400000->phy3, 14480000->phy4, 14500000->phy5` (`14300000` appears nowhere in
vendor dmesg) ([ETHERNET-BRINGUP-FRAMING.md](ETHERNET-BRINGUP-FRAMING.md) sections 2-3; lab driver
header lines 115-146). These remain valid for the MAC half. [V]

**[V] The data-path side is the PIE engine (section 1):**

- vendor DT `pie@0x10a70000`: `reg = <0x10a70000 0x10000>`, `interrupts = <SPI 88> <SPI 89>
  <SPI 90> <SPI 91>`, `dma-coherent`, `clocks = <&crg LUOFU_CLK_PIE>`, `resets = <&crg 0x30
  0x19>`, `rx_ring = <0xa 0x800 0xa 0x800 0xa 0x800 0xa 0x800>`, `tx_ring = <0xc 0x4>`,
  `woc_rx_ring = <same as rx_ring>`, `woc_tx_ring = <0xc 0x4>` (values are driver data/overlay
  sizing, not decoded semantics; `soc/luofu-r116-pinned.dts:1253-1269`, and the board overlay sets
  them per [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8). [V]
- vendor init pins four PIE IRQs (`pie-ch0..ch3`) — four channels, matching the four DT interrupts.
  [V]
- `hi_kpie` names the ring programming surface: `hi_pie_drv_rx_init/rx_kick/rx_recv/rx_poll`,
  `hi_pie_rx_init_chan/set_addr/set_owner/set_wptr/get_rptr/get_desc`, `hi_pie_drv_tx_init`,
  `hi_pie_tx_init_chan/set_desc/get_addr/get_len/get_rptr/set_wptr`, `hi_pie_drv_xmit`,
  `hi_pie_tx_send`, plus `hi_pie_get_hw_ctrl_v100/v200/v250/v300` (hwver variants). The descriptor
  layout is **not** in the repo; these function names are the RE entry points, not a spec. [V]
  (symbol table) / [U] (layout)
- the PIE node also carries `acp-gate = <0x10100174 0x1000000 0>` and
  `acp-pre-cfg-attr = <0x1010015c 0xffffffff 0x1b1b1b1b 0x10100160 0xffffffff 0x16161717>`
  (`soc/luofu-r116-pinned.dts:1254-1255`) — the SoC coherency/ACP attribute configuration for PIE
  DMA is a vendor driver (`hi_acp`) concern (4.3); its Linux equivalent is an open risk. [V]

**Not to be confused with:** the Wi-Fi endpoint's DR/SR ring maps (phases 13/25/27/42/43) — a
different engine with different descriptors (section 1.2). [V]

### 6.2 What is missing and where it must come from

| missing piece | status | source |
| --- | --- | --- |
| PIE register model: per-channel base/offset, ring head/tail/owner/status words, interrupt enable/status/clr, TX/RX descriptor format (address/length/owner/port/qid fields), buffer sizes | [U] | **RE of `hi_kpie.ko`** (68,524 B, 403 symbols; the best-symbolized vendor module on the board) and/or **GPL item 2**; module is a local fixture |
| how a netdev's TX selects the egress port/qid (the `qid_map_set`/`port_qid_map_set` from `hi_lsw_pfe`, section 5) | [U] | RE of `hi_lsw_pfe` + `hi_kpie`; bench |
| DMA coherency: PIE `dma-coherent` + the ACP attribute words; does Linux need an ACP equivalent for RX/TX on this non-coherent A9? | [U] | `hi_acp` RE + bench (cache-incoherent symptoms) |
| the ring-size properties' semantics (`<0xa 0x800 ...>` / `<0xc 0x4>`) and whether firmware/bootloader pre-initialises PIE | [U] | RE of `hi_kpie` (the `get rx_ring fail` parser) + bench |
| whether PIE requires the WoC rings/channels to be initialised for the wired path | [U] | RE (init order in `hi_kpie`) + bench |
| a netdev/DMA integration design (who owns the netdev: GEMAC driver vs PIE driver) | [I] | design decision (6.3) |

### 6.3 Port plan

The detailed PIE register/ring/descriptor/channel/egress specification and the staged
implementation plan are in [PIE-DATAPATH.md](PIE-DATAPATH.md) (2026-11-18); this section keeps
the summary view and the dependency/effort framing.

**Design fork to settle first (host-only):**

- **Option E1 (vendor shape): a packet-engine driver (PIE) owns the netdevs/NAPI/DMA and exposes a
  queue API; the GEMAC driver stays phylink/config.** This mirrors the hardware and the vendor
  layering (section 1) and works with the LSW switchdev model (per-port netdevs). Recommended for
  the LAN milestone. [I]
- **Option E2 (upstream-pretty shape): one "switch + packet engine" driver creates the CPU netdev
  and per-port netdevs (DSA/switchdev hybrid).** Bigger rewrite and ties into DSA, which section
  5.3 recommends against. [I]
- **Option E3 (reuse the existing partial GEMAC as the netdev, PIE as an internal helper):** keep
  patch `009`'s netdev for the MAC and have it call into a PIE helper for TX/RX. It reads well for a
  single-port submission but still needs PIE + LSW underneath, so it does not shrink the work; it is
  the most reviewable path to a first wired netdev outside the switchdev series. [I]

The plan below is framework-agnostic (the PIE ring work is common); the split point is an open
design question (section 9).

**Target framework:** a new DMA/ring driver for the PIE block (netdev + NAPI + DMA mapping) plus the
GEMAC phylink/config half already in patch `009`; the LSW egress mapping from section 5 (D1) is a
hard dependency for real traffic. [I]

**Binding sketch:**

~~~yaml
pie@10a70000:
  compatible: "hisilicon,luofu-pie"
  reg: <0x10a70000 0x10000>
  interrupts: <SPI 88>, <SPI 89>, <SPI 90>, <SPI 91>
  interrupt-names: [ch0, ch1, ch2, ch3]      # names TBD; vendor uses affinity, no names
  clocks: <&crg LUOFU_CLK_PIE>
  resets: <&crg 0x30 0x19>
  dma-coherent;                              # plus the ACP question (above)
# ring geometry: driver data if fixed; board-specific ring sizes (vendor overlay) argue for a
# documented optional property, e.g. hisilicon,rx-ring = <...>/<...>; decide in review.
~~~

The GEMAC netdev side keeps patch `009`'s binding; the link between a GEMAC port and a PIE
queue/port is the open design point (phandle vs driver data vs a single combined node). [I]

**Implementation steps:**

1. Host-only RE of `hi_kpie`: ring init/TX/RX/IRQ paths -> a register+descriptor specification
   document (mirroring [phase50/nand-fmc-port-spec.md](phase50/nand-fmc-port-spec.md)'s style)
   before any driver code. This is the largest single RE task after the LSW.
2. Host-only: implement the PIE ring driver (DMA coherent allocations, NAPI, RX refill, TX queue,
   IRQ ack, hwver variants), then wire the GEMAC netdev TX/RX to it (per the E1/E3 decision); DTS
   node + binding; compile with `CONFIG_WERROR=y`; no hardware writes before bench.
3. Bench: first a read-only PIE inventory on a booted kernel (registers, channel state), then a
   single-queue loopback (if the engine supports it) or one port with a fixed link, then the
   minimal LSW forwarding (section 5 D1). Every step attended with recovery armed.
4. Only then: enable GEMAC DTS nodes, attach the PHY through phylink (patches `007`/`008`), and
   measure traffic (ping + counters). M2-equivalent is the milestone.

**Dependencies:** patches `007` (MDIO), `008` (PHY), `009` (GEMAC), `004` (CRG), and the LSW D1
egress mapping (section 5); optional ACP equivalent (4.3); restart contract for bench (7.3).

**Risks:** the descriptor layout is not "mostly known" — it is unknown; 4 PIE channels vs 5 MAC
ports needs a mapping decision; RX buffer/DMA coherency on a non-coherent A9 without the vendor ACP
path can produce silent corruption; touching PIE registers can wedge the packet path and the only
remote access to the box (recovery card mandatory); the Wi-Fi side of PIE (`woc_*`) is out of scope
and must not be initialised speculatively.

**Effort:** 8-15 host sessions (RE 4-7, driver 3-5, binding/DTS 1-2) + 3-5 attended bench sessions.
With GPL source: roughly 4-6 host sessions.

## 7. Recommended order, bench gates, and the restart contract

### 7.1 Recommended order

| order | block | why this order |
| --- | --- | --- |
| 1 | **thermal** (block B) | smallest; independent of the data path; conversion already proven; a sensor+zone driver is a clean first upstream series after the landed CRG/watchdog trio. Offline-complete. |
| 2 | **pinctrl** (block A) | needed before any network bring-up (the vendor's load-bearing pin state) and by the MDIO question; small; offline-complete except validation. |
| 3 | **PCIe host** (block C) | independent of wired networking; testable by enumeration; can be developed entirely offline in parallel with items 1-2; does not block anything else. Keep the Wi-Fi endpoint out of scope. |
| 4 | **PIE ring model + GEMAC data path** (block E) | its RE gates both the wired and switch work; start the spec early (offline) even while 1-3 land. |
| 5 | **LSW minimal switch** (block D1) | the long pole; depends on the PIE queue/port model from item 4. Start its RE after item 4's spec exists, not before, because the port/qid mapping crosses both. |
| 6 | **LSW offload features** (block D2) + thermal net-cooling device | optional for v1; after wired forwarding works. |

Rationale against alternatives: doing pinctrl before thermal is also fine, and doing PCIe last is
also fine (it is not on the wired critical path) — but do **not** start the LSW/packet-engine code
before the PIE/port model is written down, because the two share the qid/port mapping and a guessed
split would be rework. [I]

### 7.2 Offline work vs attended bench work

**Can be done without the router (host-only):** all RE work for every block (vendor modules are
local fixtures), all driver/binding/DTS code, compile + `dt_binding_check` + `dtbs_check`, the
image/config deltas, and review/checkpatch. That is the bulk of blocks A, B, C and the RE half of
E/D: roughly **20-35 host sessions before the first hardware-dependent step.**

**Needs an attended bench boot (recovery door armed, human present, one variable per fire):** every
register write to pinctrl, PCIe, LSW and PIE; thermal trip/threshold programming; PCIe enumeration
on our kernel; any traffic test. Blocks A/B/C/E/D each have a bench phase in their sections. There
is **no way to validate these blocks without the router**, and no driver in this plan may be
described as working until that bench exists.

**The bench prerequisite currently missing:** the 6.18 kernel cannot boot yet (no image/install
path; [TARGET-DESIGN.md](TARGET-DESIGN.md) section 9 "explicitly not met"). The current bench line
(v17) is a **5.10 lab kernel** with vendor userspace at its documented ceiling, and it is stopped.
Two routes, both gated by the parent rules:

- **Route A (interim, read-only):** the 5.10 lab-kernel module lane (the v17 fire machinery; bench
  fires currently stopped) can host read-only probes of pinctrl/tvsensor/PIE/PCIe registers and
  compare against the vendor kernel. This validates *register models* only; it cannot validate a
  6.18 driver and it is not a release path. Requires a resumed bench line and the parent fire gates.
- **Route B (the real one):** build and boot the 6.18 target image; that needs the S1/S2 image path
  (CRG/watchdog/pinctrl/FMC + a rootfs) which is itself still unproven
  ([TARGET-DESIGN.md](TARGET-DESIGN.md) section 10, S1/S2). Until then, no block above can pass its
  bench phase.

### 7.3 The restart contract (read before any bench write)

The repo's restart evidence, which this document does **not** extend or replace:

- The machine descriptor in patch `001` has **no** `.restart` (documented TODO;
  [../target-skeleton/README.md](../target-skeleton/README.md) patch-001 row), and the watchdog
  driver (patch `005`) has **no restart handler** ("the vendor reset path writes
  sysctrl/boot-selector registers that are not understood, and a wrong reset sequence would strand
  the board") — [SKELETON-SMOKE.md](SKELETON-SMOKE.md) fifth session; [TARGET-DESIGN.md](TARGET-DESIGN.md)
  section 7.1 watchdog row. [V]
- Ledger **2026-10-12 PHASE 85** records the failure mode: a lab `luofu_restart()` wrote the vendor
  reset pair to the watchdog `clr-offset` (`0x70`, `reset-val0/1 = 0x55aa5a5a/0xaa55a5a5`), and on
  failure parked forever — while `init_machine()` had already disarmed the SoC watchdog — stranding
  the board with no timer and no reset. The recorded fix: try the reset pair, then **arm the
  watchdog before parking**, so a failed reset is recovered by a hardware watchdog reset (which
  re-runs U-Boot and the slot fallback). [V] parent `mem-entries.md:15993`.
- The vendor CRG node also declares a reboot command: `reboot-offset = <0x0>`,
  `reboot-base-index = <0x0>`, `softrst_val0/1 = 0x51162100/0xaee9deff`
  (`soc/luofu-r116-pinned.dts:467-482`). No write of that pair has ever been measured; the draft
  board DTS still carries "no SoC reset/reboot path is modelled" as a TODO
  ([../target-skeleton/dts/luofu-r116.dts](../target-skeleton/dts/luofu-r116.dts), `crg` node).
  [V] property data; [U] semantics.
- A companion lane's machine-restart patch (`010-arm-luofu-restart.patch`, present in the patch
  directory as an uncommitted draft while this document was being written:
  `drivers/power/reset/luofu-reboot.c`) is a restart policy over the two shared CRG regmaps with a
  watchdog fallback; its own header marks the CRG command PROVISIONAL, it is **outside the validated
  `001`-`009` series, and it has never booted**. Nothing in this document depends on it. Every bench
  step here depends on the same reset facts: the only measured reset is the CRG watchdog magic, a
  wrong reset write can strand the board, and no driver in this plan may add reset/restart semantics
  of its own. If a bench step hangs the packet/PCIe path, recovery is the physical RESET+TFTP card,
  not a driver. [V] + [I]

## 8. Evidence map and open questions

### 8.1 Claim -> artifact

| claim used above | artifact |
| --- | --- |
| vendor module sizes, exports/imports, depends, vermagic | parent `rootfs-2.4.15/lib/hisilicon/ko/*.ko` (local fixtures); `modinfo` and `arm-none-linux-gnueabihf-nm`; reproducible where the fixture exists, not from a fresh clone |
| `hi_mac_net_xmit` tail-calls `hi_net_dev_xmit`; PIE owns netdev/NAPI/DMA | `nm`/`objdump -dr` on `hi_kmac.ko` (0x4300, `R_ARM_JUMP24 hi_net_dev_xmit`) and `hi_kpie.ko` (0x637c `hi_net_dev_xmit`, `g_api_priv`, ring/NAPI/DMA imports) |
| vendor load order, four PIE IRQs, thermal load order | parent `rootfs-2.4.15/etc/rc.d/luofu`; summarised in [VENDOR-STACK-ON-MAINLINE.md](VENDOR-STACK-ON-MAINLINE.md) "The load order" |
| pinctrl tables, load-bearing rule, MDIO state switching | `../lab/luofu-pinctrl/README.md`, `../lab/luofu-pinctrl/luofu-pinctrl.c`, parent ledger 2026-11-15 01:30 (`mem-entries.md:22196`) and 2026-10-11 PHASE 53 (`mem-entries.md:8559`) |
| thermal conversion, trips, cooling device | `../lab/luofu-thermal/luofu-thermal.c`; `soc/luofu-r116-pinned.dts` 419-426, 1432-1489; `hi_tvsensor.ko`/`hi_knet_cdev.ko` symbol tables; parent ledger 2026-10-11 PHASE 53 |
| PCIe DT, iATU tables, measured RC values, forbidden misc window, aligned-dword rule | `soc/luofu-r116-pinned.dts` 1205-1252; [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 5; [UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md) PCIe addenda; [../lab/luofu-pcie/README.md](../lab/luofu-pcie/README.md); [phase20/host-window.md](phase20/host-window.md) C.4; [phase18/inbound-map.md](phase18/inbound-map.md); parent ledger 2026-10-12 PHASE 70 (`mem-entries.md:15132`) |
| Wi-Fi endpoint identity/BARs, driver layer split, firmware blob | [DRIVER-BLACKBOX.md](DRIVER-BLACKBOX.md) sections 1-3; [PORT-SURFACE.md](PORT-SURFACE.md); `hi5622v100_plat.ko`/`hi5622v100_wifi.ko` symbol tables |
| LSW DT windows, port bitmap, module surface, switchdev evidence, board port map | `soc/luofu-r116-pinned.dts` 1078-1123; [soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) sections 7-8; [VENDOR-SKB-BRIDGE-PORT.md](VENDOR-SKB-BRIDGE-PORT.md) sections 2.3-2.4 and 3.4; `hi_lsw_*`/`hi_swa`/`hi_khslink`/`hi_kpie` symbol tables; parent `rootfs-2.4.15/etc/board.d/02_network`; [DOCUMENTATION-HUNT.md](DOCUMENTATION-HUNT.md) sections 4.3-4.4 |
| PIE DT node/rings, four channels, ACP words | `soc/luofu-r116-pinned.dts` 1253-1269; `hi_kpie`/`hi_acp` symbol tables and strings; parent `rootfs-2.4.15/etc/rc.d/luofu` |
| ACP gate mechanism | `hi_acp.ko` strings/imports (`acp-gate`, `acp-pre-cfg-attr`, `acp-later-cfg-attr`, `hsan,acp-*`, `blocking_notifier*`, `of_find_compatible_node`) |
| BSP is not a source for LSW/PCIe/thermal | [phase50/bsp-build-status.md](phase50/bsp-build-status.md) section 1 (Ethernet/LSW row, PCIe row, platform-driver row) |
| restart failure mode and fix | parent ledger 2026-10-12 PHASE 85 (`mem-entries.md:15993`); [SKELETON-SMOKE.md](SKELETON-SMOKE.md) fifth session; [TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1 |
| upstream status / no boot claims | parent `STATUS.md`; [TARGET-DESIGN.md](TARGET-DESIGN.md) section 9; [SKELETON-SMOKE.md](SKELETON-SMOKE.md); [GPL-REQUEST.md](GPL-REQUEST.md) |

### 8.2 Open questions this document does not close

1. **LSW framework**: switchdev is recommended (5.3). Is there any tag/CPU-port evidence that would
   reopen DSA? (Search target: vendor sources, or any register table naming a tag.)
2. **Netdev ownership** for the wired path: PIE owns netdevs (vendor shape, 6.3 E1) vs the GEMAC
   driver owns them with a PIE helper (E3). Needs the PIE spec before code.
3. **ACP/coherency**: does a Linux port need the vendor `hi_acp` attribute programming for PCIe/PIE
   DMA, or is the bootloader/default coherent enough? This can slow down or sink the GEMAC/PIE bench
   (silent-corruption class).
4. **PIE ring semantics**: what do the overlay `rx_ring = <0xa 0x800 ...>` values mean, and are four
   channels enough for five GEMAC ports?
5. **Thermal policy**: adopt the vendor 115/125 C trips as-is, or revise after bench? Is the
   net-cooling device required by the SoC thermal contract (the vendor loads it first)?
6. **Pinctrl load-bearing detail**: which mux/cfg state must the driver reproduce at probe, and does
   the MDIO path need the two-state bit-bang model?
7. **PCIe endpoint binding shape**: how to encode the overlay's `pcie-gpios`/`gpio-delay`/
   `capability` as a reviewable upstream binding without vendor names.
8. **Publication**: the extracted vendor DT render and the module-name/offset evidence need the
   publish-boundary treatment in [PUBLISH-READINESS.md](PUBLISH-READINESS.md); this document adds no
   binaries and no blob content, but its evidence trail references parent-local fixtures.

### 8.3 Files that would need a one-line update if these plans are accepted (not edited here)

- [TARGET-DESIGN.md](TARGET-DESIGN.md) section 7.1 rows for pinctrl/thermal/PCIe/LSW/GEMAC: replace
  "still unwritten"/"largest new work item" wording with a link to this file.
- [SKELETON-SMOKE.md](SKELETON-SMOKE.md): add a session note only when a driver actually lands
  (this document is not a smoke result).
- [../target-skeleton/README.md](../target-skeleton/README.md): the driver table rows for the same
  five blocks can gain a "port plan: docs/REMAINING-DRIVERS.md" link.
- [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md): the phase plan can reference this file for the
  remaining-driver ordering (7.1-7.2).

This document is a plan. Its [U] rows are work items, not conclusions; its [I] rows are opinions
subject to review; its [V] rows are the only ones that may be quoted without the word "planned".

## 9. Summary table

| block | evidence (what the repo knows) | missing | plan | depends | can-do-offline |
| --- | --- | --- | --- | --- | --- |
| **pinctrl** | [V] DT windows `0x14900100/0x14940000`; 37 pins/24 groups/24 functions/per-pin mux data transcribed (`../lab/luofu-pinctrl/`); 8 vendor state nodes; `hi_kpinctrl.ko` 17,636 B; load-bearing (ledger 2026-11-15 01:30); `mdio1_grp`/`rgmii0_grp` in the table | [U] pinconf field semantics; probe/default pin state; MDIO state-switch question; reset (none in vendor node); binding | mainline pinctrl driver + YAML; reproduce vendor initial state; consumer states (MDIO/GEMAC); 2-3 host + 1-2 bench | CRG only indirectly (no reset cell today); consumers MDIO `007` / GEMAC `009` | **Yes** except validation |
| **thermal** | [V] sensor node `0x14900500` (hwver 3), conversion formula verified; trips 115000 passive / 125000 critical (hysteresis 3000), poll 1000 ms, sustainable-power 2000; `hi_tvsensor` 60 syms (v1/v4); net-cooling device `hsan,net-cdev` (CAR on eth0-4) | [U] threshold/int/enable/trim registers; trip policy vs our cooling; window size; IRQ use | mainline thermal sensor + of-thermal zone; v1 no cooling map; 1-2 host + 1 bench | none hard; optional cooling device -> LSW CAR (block D) | **Yes** except validation |
| **PCIe host** | [V] two DWC RCs, window table dbi/misc/cfg/mem/io CAs, `iatu_rc` 3 viewports/RC, 4 CRG resets + gate clocks, radm/linkdown IRQs; measured DBI/cfg values incl. both endpoints `59e7:0005`; misc write-only/never read, aligned-dword rule; vendor `hi_pcie` uses `devm_pci_alloc_host_bridge`/`pci_scan_root_bus_bridge`; endpoint power GPIOs from overlay; endpoint driver `hi5622v100_plat`+wifi [X] | [U] enumeration/write-path validation, ACP/coherency, L1SS pinmux, endpoint binding shape; Wi-Fi [X]. **Draft host driver written 2026-11-18 as patch `013`** (resets/RC mode/`ranges`/iATU/link handling/`pci_host_probe`, hard rules encoded; endpoint nodes disabled); **reset polarity fixed by patch `014`** | DWC host driver (`pcie-luofu`), `ranges` from `iatu_rc`, binding; enumeration milestone; 6-10 host + 2-3 bench | CRG `004`+`014`; restart contract; ACP question; no FMC/MDIO dependency | **Yes** for code/compile; enumeration bench-only |
| **LSW switch** | [V] DT windows dp(7)+pfe(10)+woe(5 IRQs), `port_bitmap`, resets/clocks; modules `hi_lsw_pub/dp/pfe`, `hi_swa`, `hi_kcfe_*`, `hi_khslink`; load order; board map WAN=eth3, LAN=eth0-2; **no source in either tree**, BSP is blobs; **spec written 2026-11-18 as [LSW-SPEC.md](LSW-SPEC.md)** (synthetic module-id register map, port/queue/qid model, table formats, switchdev stubs + rootif correction) | [U] EQM command word, `port_bitmap` semantics, queue/flow DDR base, L2/VLAN table formats, ACL bits; D1/D2 implementation | **switchdev** over per-port netdevs (not DSA) two-phase: D1 minimal forwarding, D2 offload; 15-25 host + 4-8 bench (D2 +10-15) | PIE/GEMAC block E; CRG `004`; MDIO/PHY/GEMAC `007`-`009`; optional Wi-Fi [X] | **Yes** for RE/design/code; **no** for any hardware validation |
| **GEMAC data path (PIE)** | [V] patch `009` refuses probe (no ring code); MAC phylink/config facts + PORT_MODE `0x010c0008`; **data path is PIE** (`hi_kpie` owns netdev/NAPI/DMA; `hi_mac_net_xmit` tail-calls `hi_net_dev_xmit`; 4 channels/IRQs, pie DT rings, `dma-coherent`, ACP words); 403-symbol module is the RE source; **spec committed 2026-11-18 as [PIE-DATAPATH.md](PIE-DATAPATH.md) and a refusing draft driver as patch `015`** (resources + ring scaffolding, no MMIO/clock/reset/IRQ/NAPI in probe) | [U] PIE descriptor/ring register model; qid/port egress mapping; DMA coherency/ACP; ring-size semantics; netdev ownership design; attended P3 inventory | spec written; next is P3 read-only inventory, then ring+NAPI bring-up, then GEMAC netdev wiring + minimal LSW; 8-15 host + 3-5 bench | patch `009` + `015` + `007` + `008` + `004`; LSW D1 for egress; ACP; restart | **Yes** for RE/code; traffic bench-only |

**Bottom line:** after the landed `001`-`009`, the five blocks are roughly **32-55 focused host
sessions + 11-19 attended bench sessions** (thermal+pinctrl ~4-5 host, PCIe ~6-10, PIE/GEMAC ~8-15,
LSW ~15-25), i.e. the SoC-side remainder of the "3-6 person-months" range in
[UPSTREAM-PORT-PLAN.md](UPSTREAM-PORT-PLAN.md) section 3, before the Wi-Fi decision. GPL source
arrival (items 1-2 of [GPL-REQUEST.md](GPL-REQUEST.md)) would cut the LSW and PIE estimates roughly
in half; it changes the schedule more than any code decision in this document. **No block here is
proven on hardware; none may be presented as working until an attended bench boots it.**
