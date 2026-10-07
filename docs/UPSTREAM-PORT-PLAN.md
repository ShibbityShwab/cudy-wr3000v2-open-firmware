# UPSTREAM-PORT PLAN: our own, fully-current OpenWrt on Cudy WR3000 v2.0 (no vendor kernel)

Scope: from the vendor `luofu` stack to a mainline port, an inventory of every block our stack must drive,
a staged bring-up roadmap, an honest cost statement, and the bench work we can start today. This plan is
the goal-level successor to `opensource/docs/HYBRID-IMAGE-PLAN.md` (now a labeled fallback: the lead's
directive 2026-10-05 is no vendor kernel, the deliverable is our own fully-current OpenWrt built from our
kernel plus our drivers). Source: `build/tmp/upstream-spec/upstream.md` (2026-10-05); every mainline claim
carries a URL and the sources are consolidated in section 5.

Target SoC HiSilicon Hi5671Y (platform `hsan luofu`, board R116, dual Cortex-A9, 128 MB NAND + 128 MB
DDR3), Wi-Fi chip Hi5622V100 on PCIe (two endpoints `59e7:0005`). Stock is vendor OpenWrt 22.03.6 /
kernel 5.10.201, roughly 60 binary modules and a closed 928,920-byte firmware blob
(`opensource/docs/TIMELINE-AND-WAY-FORWARD.md:9,255`). Repo cites re-grepped 2026-10-05.

## 1. Subsystem table

The board's reconstructed device tree (`opensource/docs/soc/luofu-r116.dts`, 1,581 lines, machine-produced
by `lab/dt2dts.py`) is the register-level evidence for the SoC-side blocks; the Wi-Fi-chip blocks come from
the phase-18/42/45-49 register maps. "Reuse" means a mainline driver already matches the compatible string.

| Subsystem | Register-level evidence in this repo | Mainline driver (verified) | Difficulty |
| --- | --- | --- | --- |
| Clocks / CRG | `clk@14880000` `hsan,clk` / `-gate` / `-pll` / `-mux` carrying `reg-offset`, `bit`, `status-offset`, `status-bit`, `ctrl-offset`, `mask`, `table` per gate/pll/mux (`luofu-r116.dts:166-370`); `crg@14880000` `hsan,crg` (softrst values, reboot/set-wdg/switch-wdg offsets, `:431`); `reset0` `hsan,hsan-reset` `#reset-cells=<2>` (`:1426`) | None: `hsan,clk`/`hsan,reset` are unmatched vendor bindings, no Hi5671 in `drivers/clk/hisilicon` | from-scratch, but the offsets/bits/masks are already decoded in the DT |
| Pinctrl + IOMUX | `pinctrl@0x14900000` `hsan,luofu-peri-pinctrl` (mux `0x14900100`, cfg `0x14940000`, 7 named pin groups incl. jtag/led/spi/pmu-pwm, `:1298`); `iomux` `hsan,iomux` (jtag sel `:1055`) | None (`hsan,luofu-peri-pinctrl` unmatched) | from-scratch (small: few groups, regs already split) |
| UART | `uart0@0x1010e000` / `uart1@0x1010f000` `snps,dw-apb-uart`, `reg-shift=<2>`, console=uart1 (`:1527,:1538,:146`) | **Reuse**: `8250_dw.c` matches `snps,dw-apb-uart` | reuse (days) |
| NAND (FMC) + SFC | `fmc@10a20000` `hsan,fmc` (quad-mode, SPI-NAND-style controller, full A/B partition table esbc/uboota,b/enva,b/fac/bdinfo/cfga,b/log/pstore/kernela,b/rootfsa,b/rootfs_data/upgrade, `:745-848`); `sfc@0` `hsan,sfc` (`:1449`) | None: the generic `drivers/mtd/spi-nand` framework is reusable but `hsan,fmc` is unmatched | from-scratch controller (hardest storage item) |
| Ethernet: hi-gemac + PHY + switch | 5x `gemac@0x1430..1450` `hsan,mac` (ports 8-12, phy-mode rgmii/gmii, mac_logic/mac resets, `:849-919`); `mdio0` `hsan,mdio` + 5x `ethernet-phy-ieee802.3-c22` (`:1150-1190`); internal LSW `lsw_dp` `hsan,dp` / `lsw_pfe` `hsan,pfe` / `lsw_woe` `hsan,lsw_woe` (`:1104,:1124,:1142`) | No `hsan,mac`. Nearest is `hix5hd2_gmac.c` (`hisilicon,hix5hd2-gmac`, a different HiSilicon family, not a drop-in) | port/from-scratch (the internal switch is a second driver on top) |
| PCIe host bridge | `pcie@0x10160000` / `0x10164000` `hsan,pcie` + `hsan,acp-pcie0/1`, DBI/misc/cfg/mem/io regs, `iatu_rc`/`iatu_ep` window props, apb/pcs/phy/ctrl resets, 2 domains (`:1232-1280`) | **Port**: the core is Synopsys DesignWare; mainline glue `pcie-histb.c` / `pcie-hisi.c` is the reference (different wrapper regs) | port (medium) |
| PCIe inbound/outbound windows | Six iATU viewports at `iatu_bar1` (BAR2 `0x41800000`) `+0x104 + 0x200*i`, ctrl2=`0x80000000`, base/limit/target; six regions ROM_WRAM/TCM/PKTRAM/IO/ACP/ACP-fw; outbound window at `iatu+0x0..0x18` -> `0x80000000` (`opensource/docs/phase18/inbound-map.md:193,213-250,231`) | DWC iATU (handled by the DWC host driver); the `hsan` region table is vendor | port (map already recovered) |
| ctrl-rb / IRQ glue (Wi-Fi chip side) | Doorbell `0x400392d4` (BAR0 `0x3f12d4`), raw `0x2e4`, mask `0x2e8`, status `0x2ec`, clr `0x2f0`; H2D doorbell latches raw+masked (`phase45/reconcile-luofu.md:103-105`, `phase46/intr-fires-at-ctrlrb.md:16-17`); firmware registers+enables GIC source `0x4C`, delivery hop is device-internal (`phase47/THE-LAST-LINK.md:19`) | None (this is the endpoint glue, not a host-IP driver) | from-scratch, but the register map is already recovered end to end |
| GPIO | `gpio0@0x10106000`/`gpio1@0x10107000` `snps,dw-apb-gpio` + `snps,dw-apb-gpio-port` (`:986,:1011`); LEDs `gpio-leds`, keys `gpio-keys-polled` (`:946,:927`) | **Reuse**: `gpio-dwapb.c` matches `snps,dw-apb-gpio`/`-port` | reuse |
| Watchdog | `watchdog` `hsan,hsan-watchdog` (enable/stop/reset magic values, timeout-sec, int-status offset, `:1549-1570`) | None (`hsan,hsan-watchdog` unmatched) | from-scratch (small) |
| Thermal | `tvsensor@14900500` `hsan,tvsensor` + `thermal-zones` cls0 (passive/critical trips + net cooling-device, `:1469-1514`) | None (`hsan,tvsensor` unmatched) | from-scratch (small) |
| Wi-Fi chip driver (the `wifidrv1` line) | Driver is symbol-rich: 3,847 functions / 1,243,224 B code, `hmac_/wal_/mac_/hdpp_/alg_/shuangta_/hal_/oal_` layers (`DRIVER-BLACKBOX.md:25`); our `wifidrv0` already registers a wiphy + netdev on device (`PORT-STATUS.md:20`); `wifidrv1` claims the endpoint, programs rings, loads FIRMWARE.bin, releases CPU 9/9 (`PORT-STATUS.md:45`); the 414-entry `alg` config table is recovered (`DRIVER-BLACKBOX.md` section 14) | **None for Hi5622/Hi5671**: no `drivers/net/wireless` driver, closed NDA BSP only | from-scratch (the multi-year item) |
| Firmware blob handling | `FIRMWARE.bin` 928,920 B, ITCM/DTCM load map from `cfg_hi5622v100_hisi.ini` (`DRIVER-BLACKBOX.md:43`); our `lab/inbound` writes it to `BAR0+0x6f8000` (CA `0x01240000`), `diffs=0` (`phase18/inbound-map.md` Part C) | None (loader is ours) | loader from-scratch but already written; the blob itself is closed |

Already free in mainline (no work): GIC `arm,cortex-a9-gic` (`:1048`), SCU (`:1444`), PL310 L2
(`arm,pl310-cache`, `:1066`), TWD timer (`:1092`), SP804 (`:1506`), DW I2C `snps,designware-i2c` (`:1036`),
`gpio-leds`/`gpio-keys-polled`, `operating-points-v2`/`pwm-regulator` (`:1201,:1402`). These bindings are
already upstream.

"None" above means the DT binds to a vendor `hsan,*` compatible that no mainline driver matches; the
absence is checkable against the per-class trees cited in section 5 (for example `drivers/clk/hisilicon`
stops at hi3519/3559a/3620/3660/3670/6220/hip04/hix5hd2/3798cv200, with no `luofu`/`hsan`/Hi5671).

## 2. Staged roadmap

**Stage 1: upstream kernel boots to UART** (DT + clock/reset + pinctrl + UART + NAND). First steps: (a)
write a `mach-luofu`/`dts/luofu-r116.dts` tree (the future-mainline path; today's recovered DT is `opensource/docs/soc/luofu-r116.dts`) that keeps the recovered DT but re-expresses the vendor `hsan,*`
nodes against new bindings; (b) the clock/reset driver, where the per-gate/pll/mux offsets, bits and tables
are already in the DT, so it is mechanical; (c) the pinctrl driver; (d) UART, already `snps,dw-apb-uart`,
so add the `apb_pclk` reference and 8250_dw brings it up; (e) SMP via `enable-method =
hisilicon,hsan_smp` (port a Hisilicon SMP smc/pen path); (f) the NAND controller so the kernel can read its
own rootfs. Evidence needed: the DT (have it), the CRG register behaviour (offsets/bits in-DT, validate
against the live device via `devmem` on `0x14880000`, which is SoC-side and safe), and U-Boot's DT-passing
path (the Jeton GPL lead).

**Stage 2: storage + userspace** (NAND + OpenWrt rootfs + sysupgrade A/B). First steps: finish the
`hsan,fmc` controller + `spi-nand` glue so `rootfsa/b` mount (the partition table is already in the DT
`:745`); boot an upstream `arm_cortex-a9` rootfs; port the A/B switch to the `hsan,sysenv`/`boot_reg`
mechanism already documented (`sysenv` `:1454`; A/B facts and the `ubiformat` recipe in
`opensource/docs/HYBRID-IMAGE-PLAN.md` sections 3.1-3.3). Evidence: partition geometry (`:745-848`), the
A/B slot facts and the executed build-0.3 precedent (`HYBRID-IMAGE-PLAN.md:92-96,121-124`).

**Stage 3: network** (Ethernet + PCIe + Wi-Fi driver + the firmware blob). First steps: the `hsan,mac`
gemac + `hsan,mdio` + PHY (reference `hix5hd2_gmac.c`, different registers) and the LSW switch driver; the
DWC PCIe host port from `pcie-histb.c` reusing the recovered `iatu_rc`/`iatu_ep` tables and the phase-18
inbound map; then the long pole, the Wi-Fi driver (the `wifidrv1` line) and the firmware blob loader (both
already under construction). Evidence: DT gemac/pcie nodes (`:849-919,:1232-1280`),
`phase18/inbound-map.md` (windows), `phase45/reconcile-luofu.md` (ctrl-rb/IRQ glue), `lab/wifidrv1/`,
`lab/inbound/inbound.c`.

**Stage 4: polish** (calibration, LEDs, watchdog). First steps: LEDs/keys, already described
(`gpio-leds`, `gpio-keys-polled`, `:927,:946`) over `gpio-dwapb`; the watchdog driver from the magic values
(`:1549`); thermal `hsan,tvsensor`; and re-apply the recovered per-unit calibration surface (phases 7-8,
`DRIVER-BLACKBOX.md` sections 5/7) through the new driver instead of the vendor `alg` ioctl. Evidence: the
calibration store and buffer populator (`phase7/calibration-store.md`, `phase8/cali-buffer-populator.md`),
and the `MANIFEST.sha256` rule.

## 3. The honest scale

This is two projects of different sizes fused. The **SoC-side port** (Stages 1-3 minus Wi-Fi: clocks,
reset, pinctrl, SPI-NAND, gemac + MDIO + internal switch, DWC PCIe) is a conventional new-`mach` bring-up;
with the DT and register offsets already recovered, a competent embedded engineer can reach
UART+storage+Ethernet in roughly **3-6 person-months**, in line with new OpenWrt SoC targets (each target's
clock/pinctrl/NAND/eth drivers are typically months of work, e.g. the realtek/microchipsw switch+eth
targets). The **Wi-Fi side is a different magnitude.** Writing our own Hi5622 driver on top of the closed
928,920-byte firmware is the same shape as the clean-room efforts the record already benchmarks:
b43/ath9k-style reverse-engineered wireless drivers are **multi-year, multi-person** efforts
(`opensource/docs/PORT-PLAN.md:44`), and the closest HiSilicon precedent, OpenIPC/openhisilicon, took a
**community and years** for camera SoCs with *simpler* interfaces (`opensource/docs/DOCUMENTATION-HUNT.md:15,25`).
Our position is better than b43's start (the register map, the message protocol, the `alg` table and the
ring/ETE/ctrl-rb machinery are already reversed), so the Wi-Fi driver is realistically **12-24
person-months** for data-path Wi-Fi, and the "fully open" variant, replacing the firmware blob itself,
remains **multi-year**, because the radio ISA and calibration algorithms exist only inside the blob
(`DOCUMENTATION-HUNT.md:25-27`). Total for "fully-current OpenWrt, our driver, vendor blob kept":
**about 2 years of focused single-developer effort**, dominated by Stage 3's Wi-Fi half.

## 4. First actions we can start NOW on the bench

1. **Dump and pin the DT.** `luofu-r116.dts` already exists (`lab/dt2dts.py`); re-run the extractor against
   the dumped `dumps/mtd*` images to confirm it is current, and diff it against the `opensource/docs/soc/`
   copy. Machine-reproducible, no device.
2. **Read the vendor DTs from the dumped images** (U-Boot and kernel DTBs across `kernela`/`kernelb`) to
   recover the U-Boot-passed bootargs/DT shape and the `sysenv`/`boot_reg` addresses the upstream DT must
   match (`sysenv` `:1454`, bootargs `:159-162`).
3. **Start the mach DT skeleton**: a future `dts/luofu-r116.dts` (the mainline path; the recovered `opensource/docs/soc/luofu-r116.dts` is the input) that flips the free bindings
   (GIC/SCU/PL310/TWD/SP804/UART/GPIO/I2C) to upstream-compatible forms and stubs the `hsan,*` nodes as
   `status=disabled` placeholders for the new bindings. No hardware needed.
4. **Write the ctrl-rb/irq-glue driver spec** from the record's register map
   (`phase45/reconcile-luofu.md:103-105`, `phase46`, `phase47/THE-LAST-LINK.md:19`): doorbell `0x2d4`,
   raw `0x2e4`, mask `0x2e8`, status `0x2ec`, clr `0x2f0`, GIC source `0x4C`. This is the one subsystem
   whose full register behaviour is already measured.
5. **Chase the Jeton GPL lead** (same Hi5671 SoC; `opensource/docs/phase29/jeton-same-soc-lead.md`). A
   Jeton AX3000 GPL tarball would hand us the vendor kernel patches for `luofu`, turning several
   "from-scratch" rows into "port" and cutting Stage 1-3 months. Environment-free.
6. **Pull the vendor gemac/pcie reference sources** from any obtainable HiSilicon SDK (Jeton/ODM) to diff
   against `hix5hd2_gmac.c`/`pcie-histb.c` and bound the port distance before writing code.

Hard rules unchanged for anything that touches the device: never write CA `0x400392f0`; never read
`0x10161000`; never read the IAR `0x4016010c`; device cycles serial/detached via `tools/exp.sh` only;
leave the router healthy.

Pinned artifacts (2026-10-05): `opensource/docs/soc/luofu-r116-pinned.dts` (the STATIC per-board DT as
shipped in flash, machine-extracted, 31,220-byte FDT, sha256 `947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3`)
and `opensource/docs/soc/vendor-dt-notes.md` (the vendor DT read from the dumped images: the three blobs
per flash image, bootargs/`chosen`, memory and reserved-memory, the U-Boot overlay shape). These are the
Step 1 pin and the Step 2 vendor-DT read; companion mapping table `build/tmp/dt-spec/dt.md`.

The EP1 209 endpoint comes into focus for stage 2/3 (2026-10-05, `build/tmp/inta-spec/{ep1.md,twinq.md}`,
ADDENDUM 14): the sibling `0001:00:00.0` binds its `pci_driver` at about t=66.5 s of an insmod boot, and v6's
deterministic `request_irq(209)` arm runs about 23 s EARLIER (t~43.4 s), so it fails `rc=-19` in 3/3 boots and
the host-facing endpoint line is never armed. The upstream `hsan,pcie` port should build the sibling bind and
the EP1 handler together (bind the endpoint, then request its virq, both from the probe) rather than arm
before the bind, and it must keep the same small-K in-ISR bound (`disable_irq_nosync` after K entries,
before any MMIO) that both existing ISRs carry, with NO re-enable after a bound-trip; the twin/copy-B
quiesce predicate (glue stat == 0 AND twin stat == 0) is the port's correctness test for the ctrl-rb IRQ
path, since a copy-A-only zero stays vacuous.

The two clock/reset placeholders are now ONE `crg` node, committed (2026-10-05, submodule commit `8a474dd`
on `omo/phase22-hccaccept`, work `build/tmp/inta-spec/{clocks2.md,dtslint.md}`): `opensource/docs/soc/
luofu-r116.dts` merges the skeleton's `clk:` (pinned `clk@14880000`) and `rst:` (pinned `reset0`,
`#reset-cells=<2>`) into a single `crg: clock-reset-controller@14880000`
(`compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"`, `reg = <0x14880000 0x1000>`,
`#clock-cells = <1>`, `#reset-cells = <2>`), with every consumer repointed to `<&crg LUOFU_CLK_*>` /
`<&crg off bit>` (gpio0/1 `0x2c 0x14/0x15`, i2c0 `0x2c 0x18`, sfc/fmc `0x2c 0x0`, gmac0 `0x30 0xd/0xe`,
pcie0 `0x34 0xc..0xf`; offsets kept verbatim from the pinned tree) plus the `memory` -> `memory@80500000`
unit-address fix, alongside the new `hisilicon,luofu-crg` driver skeleton `opensource/lab/luofu-clk/`
(still NOT-YET-COMPILED against the vendor tree, and `crg-luofu.c`'s Kconfig `core_initcall` placement is
the stage-1 plan). The edit is dtc-verified: `tools/dtc-check.sh` under dtc 1.7.2 compiles the file to
`build/tmp/inta-spec/luofu-r116.dtb`, 4,506 B, sha256
`732104b1818945b69100dc7ad45612360fec0c5297c6ef0ed91689297b5b3946`, errors 0, three cosmetic
`unit_address_vs_reg` warnings (`dtslint.md` runs 1/2; the CRG node itself lints clean). Caveat kept: the
DTB in `build/tmp/` is a local, gitignored artifact - the durable thing is the committed `.dts`.

Clocks land as files + the dtc pipeline becomes the tool of record (2026-10-05, `build/tmp/inta-spec/clocks2.md`, ADDENDUM 13): the skeleton's two placeholders fold into ONE `crg: clock-reset-controller@14880000` node in `opensource/docs/soc/luofu-r116.dts` (`compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"`, `reg = <0x14880000 0x1000>`, `#clock-cells = <1>`, `#reset-cells = <2>`), with every consumer (`gpio0/1`, `i2c0`, `fmc`, `pcie0`) repointed to `<&crg IDX>` / `<&crg off bit>` (working-tree, 23 insertions / 33 deletions), and the driver skeleton lands at `opensource/lab/luofu-clk/` (`luofu-clk.c` scaffold, NOT-YET-COMPILED, `obj-m := luofu-clk.o`), so the new `hisilicon,luofu-crg` compatible has a home for the pinned gate/PLL/mux tables and the `softrst_val0/1` magic. The dtc pipeline that compiles the skeleton is upstream dtc v1.7.2 (built from the `dgibson/dtc` v1.7.2 tarball with MinGW-W64 gcc 16.2.0 plus winflexbison 2.5.25) driven by `cpp -P -nostdinc -x assembler-with-cpp` then `dtc -I dts -O dtb`, producing `build/tmp/inta-spec/luofu-r116.dtb` (4,554 B, sha256 `a1e0d822f23691ff96efaaec3a5def0d26923d642714fa8ab674b2a408f55823`, magic `d00dfeed`, 29 nodes / 145 properties, exit 0, 0 errors, 4 `unit_address_vs_reg` warnings).

Stage 1 skeleton landed (2026-10-05): `opensource/docs/soc/luofu-r116.dts` is the first-cut mach DT skeleton
(Step 3), with every register value, interrupt number and reset/clock cell kept verbatim from the pinned
tree and only the bindings re-expressed (free bindings in mainline form, vendor `hsan,*` blocks as
`hisilicon,luofu-*` placeholders with `status = "disabled"`). It is not buildable yet; the build recipe and
file-by-file rationale live in the stage-1 spec at `build/tmp/inta-spec/stage1.md`.

DTS compile check + clocks decision (2026-10-05, `build/tmp/inta-spec/stage1b.md`): the check ran as a
fallback, because no `dtc`/`fdt*` binary exists anywhere in the repo or on PATH, the repo venv has no pip,
and the PyPI `dtc` package is a dataclass generator, not the compiler. The substitute was the kernel's own
preprocess pass, `cpp -P -nostdinc -x assembler-with-cpp opensource/docs/soc/luofu-r116.dts` (exit 0, empty
stderr, every `LUOFU_CLK_*` macro expanded), followed by a stdlib sanity parse (brace/quote balance, node and
property grammar over 29 nodes, phandle resolution, `compatible` vs the `dt.md` table, clock/reset cell
counts against each provider): CLEAN, 0 errors, 0 off-table compatibles, 0 cell-count mismatches, so the
skeleton demands no DTS edit. Two non-blocking `dtc` warnings remain for a real `make dtbs` (unknown
`hisilicon,luofu-*` compatibles until bindings land, and `L2: l2-cache` carrying `reg` without a unit
address). Decision on clocks (`stage1.md`/`stage1b.md`): the fixed input clocks reuse free bindings today
(`fixed-clock` for the oscillators and AHB/APB, `fixed-factor-clock` for the TWD /4 divider), while the CRG
(gates/PLLs/muxes plus reset) has no mainline driver and needs one new `hisilicon,luofu-crg` binding cloned
from the single-CRG model `drivers/clk/hisilicon/crg-hi3798cv200.c`, with `#clock-cells = <1>` and
`#reset-cells = <2>` (offset, bit) reusing the shared `drivers/clk/hisilicon/reset.c` helper; the pinned
tree already carries the whole gate/PLL/mux offset-bit geometry and the `softrst_val0/1` magic, so the driver
tables are mechanical transcription.

DTS compiled with a REAL dtc (2026-10-05, `build/tmp/inta-spec/dtc.md`): the tool of record is upstream dtc
v1.7.2, built locally from the `dgibson/dtc` v1.7.2 tarball (sha256 `3a3f5804...572e`) with MinGW-W64 gcc
16.2.0 plus winflexbison 2.5.25 (the repo venv has no C toolchain and no pip; the system python 3.12 pip only
fetches sdists, and the PyPI `dtc` package is a dataclass generator, not the compiler, while the prebuilt
Windows binaries have unverified provenance). After the standard `cpp` pass, `dtc -I dts -O dtb` compiles
`opensource/docs/soc/luofu-r116.dts` to `build/tmp/inta-spec/luofu-r116.dtb` (4,554 B, sha256
`a1e0d822f23691ff96efaaec3a5def0d26923d642714fa8ab674b2a408f55823`, magic `d00dfeed`, 29 nodes / 145
properties, every `&label` resolved to a phandle) with **exit 0, 0 errors, 4 warnings**, all four of them
`unit_address_vs_reg` style nits (source hygiene, not syntax; the DTS was NOT edited). Negative controls prove
the binary discriminates: a deliberately broken DTS aborts on a syntax error, an unresolved `&nope`
reference errors, and the raw vendor `luofu-r116-pinned.dts` fails to parse (its injected bytecode dump is
not DTS) while the re-expressed skeleton compiles clean. So the Stage 1 skeleton is now compile-verified by a
real device-tree compiler, and the `stage1b` fallback's `cpp`-plus-sanity-parse result is confirmed.

Clocks decision + DTS node plan (2026-10-05, `build/tmp/inta-spec/clocks2.md`): reuse mainline and
write NO clock driver for the fixed inputs - the board's five always-on sources stay plain DTS nodes
(`fixed-clock` for the oscillators plus AHB 200 MHz and APB 100 MHz, `fixed-factor-clock` for the TWD /4
divider at 250 MHz), so the timers, UART and GPIO consumers just point at `&apb_clk` / `&twd_clk`. The one
thing that needs a driver is the CRG (gates + PLLs + muxes + reset): there is no mainline match for the
vendor `hsan,*` strings, so clone the single-CRG model `hisilicon,hi3798cv200-crg` as a NEW
`hisilicon,luofu-crg` binding, one node = one clock+reset controller, `#clock-cells = <1>` plus
`#reset-cells = <2>` (offset, bit) reusing the shared `drivers/clk/hisilicon/reset.c` helper, with the
Kconfig-gated `crg-luofu.c` at the `core_initcall` level so it runs before the 8250_dw/gpio/i2c/mtd
probes, no regmap at stage 1 (one `devm_platform_ioremap_resource` for the 0x1000 page, `"syscon"` kept in
the compatible so later children can look the page up). The DTS node plan folds the skeleton's two
placeholders, `clk:` (pinned `clk@14880000`, `:201`) and `rst:` (pinned `reset0`, `#reset-cells=<2>`,
`:1394`), into ONE `crg: clock-reset-controller@14880000` node with
`compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"`, then repoints every consumer to
`<&crg IDX>` / `<&crg off bit>` (fmc, gpio0/1, i2c0, pcie0, gmac0), adding a new
`dt-bindings/clock/luofu.h` for the `LUOFU_CLK_*` ids (now COMMITTED as `8a474dd` on
`omo/phase22-hccaccept` - see the arm-A status block above for the dtc verdict); the pinned gate/PLL/mux
offset-bit geometry and the
`softrst_val0/1` magic come across verbatim, since the reference tables are hardcoded per-SoC (a new
compatible is required, not a data entry). Stage 1 stays a pure gate+reset bring-up: gates tagged
`CLK_IGNORE_UNUSED` so `clk_disable_unused()` cannot kill the live console or NAND, no PLL/mux rate
changes, and the TWD/SP804 run off the free fixed clocks. The spec's next bench action is a DETACHED
read-only `devmem` cycle at `0x14880000` to confirm the reconstructed 0x20-group gate offsets, with the
instrument self-disabling after a small K reads.

## 5. Source URLs (mainline evidence)

- OpenWrt targets list, no HiSilicon router target (no `luofu`/`hsan`/Hi5671): https://github.com/openwrt/openwrt/tree/master/target/linux
- Hisilicon Ethernet drivers: `hix5hd2_gmac.c`, `hip04_eth.c`, `hisi_femac.c`, `hns/`, `hns3/` (no `hsan,mac`): https://github.com/torvalds/linux/tree/master/drivers/net/ethernet/hisilicon
- DesignWare PCIe host drivers: `pcie-histb.c`, `pcie-hisi.c`, `pcie-kirin.c` (HiSilicon STB glue): https://github.com/torvalds/linux/tree/master/drivers/pci/controller/dwc
- DesignWare APB UART: `8250_dw.c` (`snps,dw-apb-uart`): https://github.com/torvalds/linux/blob/master/drivers/tty/serial/8250/8250_dw.c
- DesignWare APB GPIO: `gpio-dwapb.c` (`snps,dw-apb-gpio`/`-port`): https://github.com/torvalds/linux/blob/master/drivers/gpio/gpio-dwapb.c
- Hisilicon clock/CRG/reset drivers: hi3519/3559a/3620/3660/3670/6220/hip04/hix5hd2, `crg-hi3798cv200.c`, `reset.c` (no `hsan,clk`/`hsan,crg`/`hsan,reset`): https://github.com/torvalds/linux/tree/master/drivers/clk/hisilicon
- SPI-NAND core + flash drivers: reusable `spi-nand` framework, no `hsan,fmc` controller: https://github.com/torvalds/linux/tree/master/drivers/mtd/nand/spi
- Hi5671YV200/Hi5622V100: no public mainline driver (closed NDA BSP; web search 2026-10-05); closest open precedent OpenIPC/openhisilicon: https://github.com/OpenIPC/openhisilicon

Status (2026-10-05, `build/tmp/inta-spec/{crgci-result.md,dtslint.md,stage2.md}`, ADDENDUM 15): the
`luofu-clk` driver skeleton CROSS-COMPILES GREEN in CI (`gh run list` on commit `dff5925` = success twice;
the `bed58e5` attempt failed first and the block-comment fix carried it, the first cross-build of our own
driver code), the edited `opensource/docs/soc/luofu-r116.dts` lints PASS under dtc 1.7.2 (dtb 4,506 B,
sha256 `732104b1818945b69100dc7ad45612360fec0c5297c6ef0ed91689297b5b3946`, errors 0, three cosmetic
unit-name warnings), and the ranked stage-2 driver inventory is fixed at
`build/tmp/inta-spec/stage2.md` (CRG -> pinctrl -> PCIe RC -> endpoint -> glue -> wifidrv, with the Kconfig
symbols per stage). Stage 2's first driver to write is pinctrl. The ctrl-rb IRQ path's correctness test is
the twin/copy-B quiesce predicate (glue stat == 0 AND twin stat == 0), because a copy-A-only zero stays
vacuous.

Stage-2 driver #2 landed, and the smoke result is the pinctrl skeleton (2026-10-05, submodule commit
`d4875e6` on `omo/phase22-hccaccept`, `build/tmp/inta-spec/pinctrl.md`): `lab/luofu-pinctrl/{luofu-pinctrl.c,
Makefile,README.md}` registers `hsan,luofu-peri-pinctrl` as a platform driver (`of_match_table` +
`module_platform_driver` probe/remove + `pinctrl_register` over the transcribed 37-pin / 24-group /
24-function geometry read out of `hi_kpinctrl.ko`), with `set_mux` and the pinconf setters left as
deliberate NO-OPS so the bootloader's mux state is preserved at stage 2; it maps the pinned `"mux"`
(`0x14900100`, 0x3c) and `"cfg"` (`0x14940000`, 0x100) windows read-only, leaves `dt_node_to_map` NULL for
`pinconf_generic_dt_node_to_map()`, and keeps the reset deassert out (that is a reset write). CI is wired in
both lanes: `lab-module-build.yml` is now a `fail-fast: false` matrix over `[luofu-clk, luofu-pinctrl]`, and
the `master`-triggered `build-load-test-module.yml` carries the matching build step, `vermagic` line and
`luofu-pinctrl-ko` artifact. SMOKE RESULT: the cross-build is CI GREEN - run `37371987343` on `d4875e6`
(`lab-module-build` -> job `build (luofu-pinctrl)`, conclusion success; the sibling `build (luofu-clk)` job
is green too), so the skeleton compiles against the vanilla 5.10.201 arm headers the same way `luofu-clk`
did (`pinctrl.md` section 5; the exact `CC [M]`/`LD [M]` tail is quoted in ADDENDUM 18). Caveats kept: this
lane has no `Module.symvers`/`vmlinux` dump, so the unresolved-symbol check is skipped and the real test
remains loading the `.ko` against the vendor tree; the driver was never loaded and no device cycle was run by
that task. Next stage-2 fill-in is the per-pin `drv_data {reg_off, shift, func}`, the real per-group pin
lists and the `"cfg"` bitfield map behind `set_mux`/`pin_config_set` (`pinctrl.md` section 4).

The take lane's end-to-end health check is now a measured instrument (2026-10-05, take3,
`opensource/docs/phase49/gic-view.md` ADDENDUM 19, `build/tmp/inta-spec/tiebreak.md`): the take3 boot stamped
the port's two endpoint witnesses the stage-2/3 work depends on - `207: 65 ... omo-drv1` and `209: 9 ...
omo-drv1-ep1` in `/proc/interrupts`, with both request_irq calls returning `rc=0` - and exercised the
mandatory bound so the port's no-re-enable discipline is on the record (`207 n=65/64`, `209 n=9/8`, both
self-disabling). One port-relevant defect turned up in a companion tool: `tools/patch_fw_scratch.py`'s
`write_ca_block()` writes `movw r0,#0` for a value larger than `0xFFFF` and never emits the `movt`, so a
32-bit register store silently becomes its low halfword. It is a fw-patch emitter, not kernel driver code, but
the lesson is the one a port has to hold: a build check that re-uses the encoder it is checking can pass while
the emitted bytes are wrong (the take3 pad shipped `0x00000000` to `ICENABLER0` and the self-check agreed).
When the ctrl-rb/IRQ-glue driver gets its own ported write path, assert the emitted value back, not the call
site.

The forced probe reads the real CRG, and the PCIe RC design lands (2026-10-06, arm B,
`build/tmp/inta-spec/{crgprobe.md,pcierc.md}`): the `luofu-clk` skeleton now has its FIRST
device-side proof, and stage-2's third row has a written design. THE FORCED PROBE (crgprobe, task
`st_01a111c4`, runner `build/tmp/wifidrv1-art/run-crgprobe.sh`, ko md5
`b1a60c988c5dd704a500159cdc9483d2`, staged AS `/tmp/wifidrv1.ko`): one serial device action
`insmod force_probe=1` -> `rmmod`, gate OK (`boot_id=fec0f09c...`, uptime 1244s, 0 leftovers, 2
wiphys, 6 ifaces, cal `[SUCC]` 2g+5g), and the driver reads the LIVE pinned CRG `0x14880000` with no
DT match (`read-only devm_ioremap` + `readl` inventory, 0 writes): `[0x090] CRG_STATUS = 0x6a010008`
(PLL cpu-lock=1 lsw-lock=1, `rst_reason=4`; lock mask `0x48000000` fully set) and `[0x100]
WDT_ISTATUS = 0x00000000` (benign), logging `FORCED probe PASS: 2/2 status regs read, 0 writes`.
`RMMOD_RC=0`, post-health `POST_WIPHY=2 POST_IFACE=6 POST_CAL2G=1 POST_CAL5G=1 POST_LUOFU=0
POST_LEFTOVERS=0`, `boot_id` UNCHANGED (no reboot). So the reconstructed 0x20-group CRG geometry the
stage-1 driver transcribes is now CONFIRMED against the live part through its own code, not devmem.
Two caveats carried: the vendor `hsan,rstinfo` node exposes no `reset_reason` sysfs attr (so the
consistency check is the internal two-read agreement, `0x6a090008` @15:07Z vs `0x6a010008` @15:10Z,
same lock bits 30/27 and `rst_reason=4`), and sec.6 of the receipt records a NON-FATAL driver defect -
the synthetic `luofu-crg` platform_device has no `.release`, so `platform_device_unregister()` in
`luofu_crg_exit` warns at `drivers/base/core.c:1836` on EVERY unload (kernel taint gains `W`); it is
emitted after the runner's dmesg snapshot so the harness never saw it, and the fix (fdev `.dev.release`
or `platform_device_register_simple`) needs a CI rebuild and is out of scope for a receipt. THE PCIe RC
DESIGN LANDS (task `st_01a111be`, `build/tmp/inta-spec/pcierc.md`): stage-2 row 3 (the DWC RC, the
block after the CRG and pinctrl) now has a register-level design read from the vendor `hi_pcie.ko`
(disassembled with `lab/ko_disasm.py`) against the pinned DTS - the five-window layout (`dbi
0x10160000`, `misc 0x10161000` write-only, `cfg 0x50000000`, `mem 0x40000000`, `io 0x48000000`, RC1
`+0x4000`/`+0x18000000`), the verbatim `iatu_rc` table (3 viewports: CFG0/MEM/IO) the vendor writes to
`DBI+0x900+0x200*i`, the 14-step `hi_pcie_probe @0xb8c` init order (clk_bulk_enable -> 4
`reset_control_deassert` -> `misc+0x00 = 0x40000000` RC mode -> iATU -> endpoint power -> `DBI+0x04 = 7`
-> LTSSM), why it is a from-scratch host controller rather than a drop-in DWC core (non-DWC `misc`
block + a dedicated 4 KB `cfg` window instead of ECAM; reuse `pcie-histb.c`'s SHAPE, keep `ranges`
providing the windows explicitly), and the three port-only constraints (clocks/resets from `&crg`,
link-up polled via `DBI+0x82`/`cfg+0x82` DL_ACTIVE instead of the read-forbidden `misc+0x100`, `misc`
writes only). The companion DT node is ALREADY IN THE TREE: `opensource/docs/soc/luofu-r116.dts`
carries `pcie0: pcie@10160000 { compatible = "hisilicon,luofu-pcie"; ... status = "disabled"; }` with
the five `reg`/`reg-names`, `interrupts = <0 0x3b 4> (radm, SPI 59) / <0 0x45 4> (linkdown, SPI 69)`,
`<&crg LUOFU_CLK_PCIE0>` and the four `&crg 0x34 0xc..0xf` resets (committed at submodule
`fa11572` on `omo/phase22-hccaccept`; the `pcie1` sibling and the `iatu_rc`/`iatu_ep`/`pcie-gpios`
properties are still spec-only, `pcierc.md` sec.3). NO CODE AND NO CI YET: there is no
`lab/luofu-pcie/` tree, no `luofu-pcie-ko` artifact and no workflow matrix entry, so the design is the
landed artifact and the driver skeleton (misc-write-only, log the transcribed `iatu_rc`, no DT match on
a vendor boot so no probe runs) is the plan's stated next step.

The PCIe RC skeleton lands, compiles CI-green, and its smoke PANICS at one misaligned read (2026-10-06, arm B2,
`build/tmp/inta-spec/{pciskel.md,pciskel-ci.md,pciskel-smoke.md}`, submodule commit `5079cc1` on
`omo/phase22-hccaccept`): the read-only `lab/luofu-pcie/` skeleton (map `dbi`+`cfg` READ-ONLY, `misc 0x10161000`
NOT mapped, ZERO writes - the read-only bar proved at the instruction level, since `readl()` inlines to a plain
`ldr`; 49 stores, none a window base) cross-builds GREEN on the first push (workflow `lab-module-build`, run
`37488653830`, `verdict: success`, `vermagic=5.10.201 mod_unload ARMv7`, artifact `luofu-pcie-ko`,
`build/tmp/wifidrv1-art/pciskel/luofu-pcie.ko` md5 `503f9580c29f555a47755ca93e61feeb`), and the smoke runner
`build/tmp/wifidrv1-art/run-pciskel.sh` is delivered with a fail-closed gate + self-test. THE SMOKE IS A RUN
FAILURE, NOT A READ FAILURE (`build/register-dumps/exp/20261006T1540Z-pciskel/`, pstore `dmesg-pstore_blk-2`
Panic#2): gate CLEAN (`boot_id=518f5479...`, uptime 1071s, pat=0, 0 leftovers, 2 wiphys, 6 ifaces, cal `[SUCC]`
2g+5g, stock md5 pinned), one serial `insmod force_probe=1` - and the probe DID read the live vendor-owned RC
with 0 writes, then took an **imprecise external abort at the misaligned Link Status read `dbi+0x082`**
(`readl()` at a 2-byte-aligned PCIe-cap register; the fault address `0xc800a082` = the mapped DBI base +0x82),
so the kernel panic'd and the box rebooted (NEW `boot_id=7afb325f...`, router healthy, 2 wiphys / 6 ifaces / cal
`[SUCC]`). A PARTIAL PASS and a real defect, both on the record: the RC's DBI IS live PCI config space with the
link UP (`dbi+0x004` = `0x00100007`, Command=0x7 = the vendor's write; the aligned `dbi+0x080` word carries
`Link Status = 0x7012` = **DL_ACTIVE bit 13 set**, 2.5 GT/s x1 = the `pcierc.md` sec 4b safe link-up witness),
while every word at or past `dbi+0x082` (the iATU readback `0x900+`, the whole `cfg` inventory) was never reached.
The fix a rerun needs: `readw()` for the 16-bit cap registers (`0x082`; and `0x07c`/`0x080` are Link
Capabilities / Link Control in the PCIe cap layout), re-pin the `0x080` predictor (ASPM off -> `0x0`, so `expect
0x3` is a BAD PREDICTION and the `match=NO` is a lost prediction, not a lost measurement), and take link status
from the aligned `0x080` upper half or the `cfg` copy. The defect is in the skeleton's register TABLE (`readl`
for every entry, including the one misaligned offset), not the RC design; do NOT re-run the `503f9580...` ko - it
panics deterministically. Hard rules held by the task: no read of `0x10161000`, no host read of the IAR
`0x4016010c`, no write of CA `0x400392f0`, ko staged ALWAYS as `wifidrv1.ko`, and the reboot was gated on a NEW
`boot_id`. Push authorization respected: only the submodule branch `omo/phase22-hccaccept`, never `master`.

The arm-B3 RC probe is complete as a run whose gate REFUSED the image slot (2026-10-07, task `st_01a113d1`,
report `build/tmp/inta-spec/pciskel-smoke2.md`, raw `build/tmp/wifidrv1-art/pciskel2/{_bootB3-gate-run.log,
RESULT.txt,_bootB3-dryrun.txt}`): the width-fixed skeleton (`pciskel2`, ko md5 `51376f7608d6e5d60b0bb6fe09eb068e`,
CI run `37553187016`, submodule commit `6e4cc73`) reached the `insmod force_probe=1` lane with `SELFTEST PASS`
(the v1 lane's verifier proved to have teeth, A-F), and the run then STOPPED at THE SLOT CHECK before any
mutation: `GATE FAILED: SLOT(want=mtd14:rootfsb got=mtd13:rootfsa)` - `boot_id=f47bbc77...`, uptime 335 s,
`slot=mtd13:rootfsa (rom=/dev/ubiblock0_0 dmesg_attach=13)`, the stock 2.4.15, cross-checked three ways
(`/sys/class/ubi/ubi0/mtd_num`=13, `/proc/mtd` mtd13=`rootfsa`, the boot dmesg `ubi0: attached mtd13`). So the
run is a GATE-REFUSED, not a probe result, and the device was NOT touched: nothing staged, no insmod, no rmmod,
no reboot - the health half was GREEN at the same instant (0 `.omo-pat`, 0 `.omo-off`, 0 leftovers,
`luofu_pcie`/`wifidrv1` absent, `hi_pcie` + the vendor pair loaded, 2 wiphys, 6 ifaces, cal `[SUCC]` 2g+5g,
`STOCK_MD5` at the pin), so the refusal is the slot alone. The run's FINDING, second observation after
`race.md` sec.6: a boot-id-gated reboot does NOT flip the slot - the sibling reboot lane (`_capstone-reboot.log`,
`st_01a113cf`) rebooted 00:42:54Z (`boot_id` `e8e60346` -> `f47bbc77`) and the box returned on
`NEW_MTD_NUM=13 rootfsa`, `NEW_ATTACH=mtd13`, `boot_reg=10`, the same slot. THE SLOT CHECK is now IN the runner
(`run-pciskel.sh`, line 167ff + selftest F): `slot_fail()` requires `/sys/class/ubi/ubi0/mtd_num`=14, its
`/proc/mtd` name `rootfsb`, and the dmesg attach line to agree, and ABORTs on `mtd13`/`rootfsa`, an empty
`mtd_num` or an ident conflict; ARM-A seriality rides the same gate (while a cycle marker `.omo-pat` / a hidden
`.omo-off` / an `omo-*-guard.sh` is present, re-read up to 12x10 s = 120 s, then ABORT, never a 2nd device
action). THE READ SET THE PROBE WILL DELIVER is fixed by the `pciskel2` fix and stays unspent on the device:
`readw()` for the 16-bit cap registers (the misaligned `readl` at `dbi+0x082` was the v1 external abort), the
full DBI + `cfg` inventory over a single width-aware accessor, and the **Link Status** read at `dbi+0x082` /
`cfg+0x082` (`expect 0x2000 mask 0x2000` = DL_ACTIVE), plus `cfg+0x000` re-pinned to `0x000059e7 mask
0x0000ffff` and `dbi+0x07c` correctly W32 `Link Capabilities` - all four defects the decoded `.rodata` tables
and the inlined `ldrh` arm (5x `ldrh`) prove at the byte and instruction level (`_ko-bar.txt`,
`_ko-symcheck.log`, `_ko-storecensus.log`). THE UNBLOCK is a flash/env lever with its own gate, not a reboot:
restore slot B per `build/custom/FLASH-PLAN.md` "Slot switch recipe" (the B env block to mtd3+mtd4 + the
selector in `/sys/devices/platform/sysenv/boot_reg`), reboot, confirm `attached mtd14`, then the one-command
re-run `bash build/tmp/wifidrv1-art/run-pciskel.sh` - the verifier then requires `INSMOD_RC=0`, the rc0 banner +
window plan, `rc0 [0x082] dbi+0x082` (the Link Status via `readw` - the fix), the 5 rc0 `match=YES` predictions,
2x `FORCED probe PASS ... 0 writes`, `RMMOD_RC=0`, `LUOFU_AFTER_RMMOD=0`, `STAGED_LEFT=0`, no kernel fault and
the `boot_id` UNCHANGED end to end. Hard rules held: CA `0x400392f0` never written, `0x10161000` never
read/mapped (the module carries it only as a `%lx` argument, 0 occurrences in the ko), the host-side IAR
`0x4016010c` never read, the device action serial + gate-checked, ko always staged as `wifidrv1.ko`, the router
left healthy (untouched). ASSUMPTION carried (report sec.6): the brief's GATE names THE SLOT CHECK, so it was
applied literally instead of running the read-only, slot-agnostic probe on the stock image; to take the value
from the stock slot too, drop the `slot_fail` call from `run-pciskel.sh`.

The arm-B4 RC probe is COMPLETE, and it is the FIRST device run this RC line has ever finished: the RC inventory
is wide open (2026-10-07, task `st_01a11400`, report `build/tmp/inta-spec/pciskel-smoke3.md` in flight, README
`build/tmp/wifidrv1-art/pciskel3/README.md`, verdicts `build/register-dumps/diffs/20261007T0135Z-vrunB5/
verdict.txt` (ALL THREE CLAIMS CONFIRMED) and `build/tmp/wifidrv1-art/pciskel3/{RESULT.txt,SELFTEST.txt,KO.md5,
run-pciskel.log}`). THE FIX: the read is now an ALIGNED-DWORD one - `luofu_pcie_read()` does one `readl(base +
(off & ~3u))`, shifts by `(off & 3u) * 8`, then masks to the field width - because the width theory was wrong:
the v2 `readw()` ko (`51376f76...`) re-crashed at the SAME RC0 `+0x082` address (`0xc800a082`) on an `ldrh`, so
a *narrower* access is not a legal one, only an *aligned* access is (the two live pstore records, `pciskel3/
{_ko-bar.txt,pstore-blk-2-v1.txt,pstore-blk-0-v3.txt}`); the fix is submodule commit `c35af1a` on
`omo/phase22-hccaccept`, CI `lab-module-build` run `37556699784` completed/success, ko md5
`976a706a3184bdf32fd63b858cd67315`, `readl()` again the module's ONLY MMIO op. THE RUN (gate ADMITTED, not
refused, `GATE OK: SLOT=mtd14:rootfsb` at `boot_id=131068c7-45e4-4480-a1c1-3f64e2ff8e21`, 2026-10-07T01:34:07Z ->
01:34:15Z, one serial `insmod force_probe=1` -> `rmmod`, ko staged AS `/tmp/wifidrv1.ko`): BOTH RCs' full DBI +
`cfg` inventories ran to a clean `FORCED probe PASS ... 0 writes` with NO FAULT, and the read that killed both
prior kos (imprecise external abort `0x1406`) now RETURNS - `rc0 dbi+0x082 = 0x7012 match=YES` and `rc1
dbi+0x082 = 0x7012 match=YES` (`DL_ACTIVE` bit 13 SET, `link-training=1 neg-speed=2 (5GT/s)`), with `EC` = 0x0000`,
`dbi+0x004 = 0x0007`, `dbi+0x07c = 0x734c12` (Link Capabilities), `dbi+0x080 = 0x0000`, `dbi+0x80c = 0x012c`.
THE NEW LIVE FACTS: `rc1`'s DBI answers exactly as `rc0`'s (`0x0000`/`0x0007`/`0x734c12`/`0x0000`/`0x7012`/
`0x012c`), so the DWC port logic is identical on both domains and the aligned-dword read is proven on BOTH; the
iATU selector `[0x900] = 0x00000002` with `iatu_rc[2]` base/limit `0x48000000`/`0x4fffffff` (rc0) and
`0x60000000`/`0x67ffffff` (rc1), both `match=YES`, joins the `pcierc.md` window plan as measured; and the
endpoint side reads `cfg+0x000 = 0x59e7`/`cfg+0x004 = 0x0006`/`cfg+0x008 = 0x2800000`/`cfg+0x02c = 0x19e5` - all
four `match=YES` from BOTH RCs' CFG windows, which no prior ko had ever reached. ONE OPEN SPLIT, flagged not
explained: both RCs' `cfg+0x082` reads `0x1012` (`DL_ACTIVE`=0, the endpoint-side Link Status) while the DBI's
reads `0x7012` (=1) - a `match=NO` PREDICTOR disagreement (the endpoint link is still training), not a lost
measurement. THE UNLOAD AND THE BOX: `RMMOD_RC=0`, `LUOFU_AFTER_RMMOD=0`, `STAGED_LEFT=0`, `POST_WIPHY=2`,
`POST_IFACE=6`, `POST_CAL2G=1`, `POST_CAL5G=1`, `POST_LUOFU_PCIE=0`, `POST_VENDOR_WIFI=1`,
`POST_VENDOR_PLAT=1`, `POST_OMO_PAT=0`, `POST_LEFTOVERS=0`, and `boot_id` UNCHANGED (`131068c7...` the same boot
the run started on - the fix does not crash). `PCISKEL RESULT: PASS`. The unblock this class of record named was
executed first: the slot was restored via the record's own verified recipe (`build/custom/env-bootflag-b.bin`
into BOTH env copies + `boot_reg 0x21` for slot B; the arm-B3 lane's literal slot check refused `mtd13:rootfsa`
an hour earlier - `diffs/20261007T0051Z-vrunB4`), and the SAME take6f capstone boot served both halves
(ADDENDUM 25a). This closes the RC design's open read question: the port's five windows, the iATU table and the
vendor-written `DBI+0x04 = 7` are now measured, and what remains for the stage-2 RC driver is the FROM-SCRATCH
host controller itself (iATU programming, LTSSM, `cfg` accessors), not another measurement. The runner left in
place carries the SLOT CHECK + arm-A seriality, so the next run refuses before any mutation. Hard rules held
this session: CA `0x400392f0` never written; `0x10161000` never read or mapped (the ko carries it only as a
`%lx` argument and its own line says `write-only misc 0x10161000 NOT mapped`); the host-side IAR `0x4016010c`
never read; the device action serial + gate-checked; ko staged ALWAYS as `wifidrv1.ko`; NO device cycle was run
by the B4 verifier itself (it performed ONE read-only health/slot probe and made no commit, pushing nothing).

The arm-B5 verdict (the adversarial verification of the arm-B4 aligned-dword receipt) is CONFIRMED on all three claims and closes the arm-B measurement phase (2026-10-07, verdict `build/register-dumps/diffs/20261007T0135Z-vrunB5/verdict.txt`, `ALL THREE CLAIMS CONFIRMED`, task `st_01a11400`, runner script `tools/finish-evidence.sh --verdict vrunB5`). The verifier ran NO device cycle and made NO commit (its ONE device interaction was a read-only health/slot probe). C1 THE DEVICE RECEIPTS EXIST: the fixed ko (md5 `976a706a3184bdf32fd63b858cd67315`) ran on the LIVE RC at slot `mtd14:rootfsb` (`GATE OK: SLOT=mtd14:rootfsb`, `boot_id=131068c7-45e4-4480-a1c1-3f64e2ff8e21`, 2026-10-07T01:34:07Z -> 01:34:15Z, one serial `insmod force_probe=1` -> `rmmod`, ko staged AS `/tmp/wifidrv1.ko`) and produced, with NO fault, BOTH RCs' full DBI+CFG inventories - `rc0/rc1 dbi+0x082 = 0x7012 match=YES` (the `DL_ACTIVE` bit-13 read that external-aborted ko `503f9580` at a `readl` and ko `51376f76` at a `readw` now RETURNS on both domains), `rc1` reached live for the FIRST time, `EC=0x0000`, `dbi+0x004=0x0007`, `dbi+0x07c=0x734c12` (Link Capabilities), `dbi+0x080=0x0000`, `dbi+0x80c=0x012c`, iATU selector `[0x900]=0x00000002` with `iatu_rc[2]` = `0x48000000`/`0x4fffffff` (rc0) and `0x60000000`/`0x67ffffff` (rc1) both `match=YES`, and the endpoint CFG identity all four `match=YES` (`cfg+0x000=0x59e7`, `cfg+0x004=0x0006`, `cfg+0x008=0x2800000`, `cfg+0x02c=0x19e5`) - plus the exact clean unload the ask names (`RMMOD_RC=0`, `LUOFU_AFTER_RMMOD=0`, `STAGED_LEFT=0`, `PCISKEL RESULT: PASS`, `RESULT.txt`=`PASS`, `SELFTEST.txt`=`SELFTEST PASS`) and the device-up state on the SAME boot (`POST_WIPHY=2`, `POST_IFACE=6`, `POST_CAL2G=1`, `POST_CAL5G=1`, `POST_LUOFU_PCIE=0`, `POST_LEFTOVERS=0`). C2 THE CI LANE IS GREEN at the arm head: `lab-module-build` run `37556699784`, branch `omo/phase22-hccaccept`, headSha `c35af1a7bb2dc081cf9af7bd9d4a827afd481795`, status `completed` / conclusion `success` (== the tracked submodule HEAD / `origin` head / `RUN.head.txt`). C3 THE FIX IS REAL AT THE INSTRUCTION LEVEL: the shipped ko's `luofu_pcie_read()` fetches every field from its containing 4-byte-aligned dword - one `readl(base + (off & ~3u))`, shift `(off & 3u) * 8`, mask to width - with the `off & ~3` step compiled in as `bic r0, r3, #3`, the single MMIO read a 32-bit `ldr` + `dsb sy`, every `ldrh`/`ldrb` a fixed table-row read (never MMIO), no write token, and both `0x082` entries still W16 fields (`KO BAR: PASS`); `readl()` is again the module's ONLY MMIO op. ONE OPEN SPLIT, flagged not explained: both RCs' `cfg+0x082` reads `0x1012` (`DL_ACTIVE`=0, the endpoint-side Link Status) while the DBI's reads `0x7012` (=1) - a `match=NO` PREDICTOR disagreement (the endpoint link is still training), not a lost measurement. HARD-RULE AUDIT (the verifier's own pass): CA `0x400392f0` never written, `0x10161000` never read or mapped (the ko carries it only as a `%lx` argument and its own line says `write-only misc 0x10161000 NOT mapped`), the host-side IAR `0x4016010c` never read, the device action serial + gate-checked, ko staged ALWAYS as `wifidrv1.ko`, push authorization respected (only the submodule branch `omo/phase22-hccaccept`, never `master`), and the router left ONLINE on `mtd14:rootfsb` (2.5.24), healthy and untouched (2 wiphys, 6 interfaces, cal `[SUCC]` 2g+5g, no `.omo-pat`/`.omo-off`, no leftovers, stock `FIRMWARE.bin` md5 `0e530b976d5a20e87358671f1a577695` at the pin). WHAT REMAINS for the real RC bring-up: the measurement phase is CLOSED - the port's five windows, the iATU table and the vendor-written `DBI+0x04 = 7` are now measured on BOTH domains, so the remaining work is the FROM-SCRATCH host controller itself (iATU programming, LTSSM, and `cfg` accessors), not another measurement, and `run-pciskel.sh` stays in place carrying the SLOT CHECK + arm-A seriality so the next run refuses before any mutation.

ARM A - the CRG WRITE PATH: STAGE 1 RAN AND ITS NO-OP CLAIM FAILED, so the write half is NOT closed (2026-10-07, task `st_01a1142c` worker + `st_01a11437` verifier): the `luofu-clk` CRG write path is now IMPLEMENTED, CI-built and MEASURED on the live part, but the proof it was built to deliver - "the stage-1 knob sequence is a byte-identical no-op" - did NOT hold, so the staged write half stops at stage 1 and the stage-2 `pcie0_clk` flip stays QUEUED. THE CODE (submodule commit `5419532a7d5399dc31ccbd26f457ad5e09c37a03` on `omo/phase22-hccaccept`, the only file touched `opensource/lab/luofu-clk/luofu-clk.c`; the design docs were `build/tmp/inta-spec/{wrspec.md,wrdesign.md}`, ADDENDUM 26/27's lane): every store goes through one `luofu_crg_rmw()`, which REFUSES past `LUOFU_WRITE_BUDGET` (`16*3 + 2 = 50`, compiled in as `cmp #0x31`/`#0x32`) and REFUSES any unaligned offset (`off & 3`, compiled in as `tst r1, #3`); the gate table is exactly 16 entries (5 at `0x14`, 11 at `0x20`), the instruction census proves exactly ONE store through the arg0-derived CRG base (`str r6, [r4]` in `luofu_crg_rmw`), and the stage-2 rank write is compile-gated out (`WRITE_FLIP` ABSENT from the artifact, `WRITE_TEST` present) - `CRGWRITE-CENSUS: PASS`. THE CI: `lab-module-build` run `37562151645`, head `5419532` on `omo/phase22-hccaccept`, conclusion **success**, artifact `luofu-clk-ko`, ko md5 `eca78db418dc420932114fec064e3651` (the pin the run fetched), `vermagic=5.10.201 SMP mod_unload ARMv7`. THE RUN (task `st_01a11433`, `build/tmp/wifidrv1-art/crgwrite/`, one serial `insmod force_probe=1 write_test=1` -> dmesg -> `rmmod`, ko staged AS `/tmp/wifidrv1.ko`, NO reboot): gate CLEAN at `boot_id=ab073ef2-d57d-42ad-971d-c42bc92f80b8`, slot `mtd14:rootfsb`, `INSMOD_RC=0`, the banner `FORCED probe (no DT match) base=0x14880000 size=0x1000 write_test=1 write_flip=0`, 16 `[STEP]` lines ids 0..15 over the 16 gates, and 48 bounded dword-aligned stores (16 gates x set/clear/restore, under the 50 budget, no refusal line, no loop). STAGE 1's RESULT: `WRITE_TEST FAIL 15/16 gates no-op (1 flips), 48 stores`; `RESULT.txt` = `FAIL [a-gate-read-back-flipped] [write_test-pass]`. THE MISMATCH (the whole point of the staged probe, and it is a real live finding): gate 4 `led_pwm` (`0x14`, bit `0x0e`) is WRITE-1-SETTABLE but NOT WRITE-0-CLEARABLE - `pre 0x01360ef1 -> set 0x01364ef1 -> clear 0x01364ef1 -> back 0x01364ef1 (FLIP)`, so the set took, the clear did NOT, and the restore-to-pre did NOT; the register is LEFT at `0x14880014 = 0x01364ef1` (bit `0x0e` set). The other 15 gates report 0 flips, which masks a non-taking store (the `0x20` group's `0x02f8300b` already has b0/b1/b3/b12/b13 set, and `0x14`'s other four bits did not take the SET at all) - so "0 flips" is NOT the same as "the write landed": the vendor `hi_clk_gate_disable` clears the same shape and would be a no-op on this bit too (the `wrdesign.md` sec-3 caveat, now measured on the live latch). THE VERDICT (task `st_01a11437`, `build/register-dumps/diffs/20261007T0236Z-vrunW/verdict.txt`, host-only, NO device cycle, NO commit): C1 receipts faithful CONFIRMED, C2 the no-op property FAIL (the equality on gate 4, not the receipts, not the code), C3 CI real and the artifact run == the artifact fetched CONFIRMED, C4 the write-path code matches `wrspec.md` CONFIRMED, C5 safety/bound honoured CONFIRMED (`48 <= 50` stores, no refusal, no kernel fault, no reboot, `boot_id` unchanged, no forbidden register touched) => ARM A is NOT CONFIRMED as a no-op. THE SAFETY RECORD: `0x14880014` is a cosmetic `led_pwm` gate latch that clears on the next SoC reset (the residual side effect named, not hidden); the box stayed healthy on the SAME boot - `POST_BOOT=ab073ef2-...` (unchanged), `RMMOD_RC=0`, `LUOFU_AFTER_RMMOD=0`, `STAGED_LEFT=0`, 2 wiphys, 6 interfaces, cal `[SUCC]` 2g+5g, `mtd14:rootfsb`, 2.5.24, no `.omo-pat`/`.omo-off`/leftovers; `CRG_STATUS = 0x6a010008` (PLLs locked, `rst_reason=4`) and `WDT_ISTATUS = 0x00000000` unchanged; hard rules held (CA `0x400392f0` never written; the RC misc `0x10161000` never read or mapped; the host-side IAR `0x4016010c` never read; dword-aligned accesses throughout; ko staged ALWAYS as `wifidrv1.ko`; push authorization used only on `omo/phase22-hccaccept`, never `master`). WHAT REMAINS for the write half (all three gates named by the smoke, `crgwrite-smoke.md` sec 5): (1) a driver build whose CLEAR is PROVEN to take on `0x14` bit `0x0e`, or that table entry re-derived against the pinned DTS; (2) a stage-1 re-run returning `WRITE_TEST PASS 16/16` on a FRESH boot; (3) only then the stage-2 `pcie0_clk` flip - whose own precondition (the `0x20` bit `0x0c` must PRE-read 0) is refused anyway today, since `0x20` reads `0x02f8300b` with bit `0x0c` already 1 - plus a build with `LUOFU_CRG_FLIP` compiled IN, which the current artifact does not carry. The runner (`build/tmp/wifidrv1-art/run-crgwrite.sh`) stays in place with its fail-closed gate + `--selftest` and asserts the `boot_id` UNCHANGED; do NOT re-run ko `eca78db418dc420932114fec064e3651` expecting a clean stage 1 - it fails deterministically on gate 4 and leaves `0x14880014` bit `0x0e` set.

ARM B - THE CRG WRITE HALF'S REVERSIBLE FLIP: THE ARMED STAGE-2 FLIP RAN ON THE LIVE CRG AND IS CONFIRMED, AND IT FAILED CLOSED (2026-10-07, task `st_01a1144c` worker + `st_01a11450` verifier; verdict `build/register-dumps/diffs/20261007T1005Z-vrunF/verdict.txt`, all six claims CONFIRMED, `RESULT.txt` = `PASS`, `SELFTEST.txt` = `SELFTEST PASS`, evidence `build/tmp/wifidrv1-art/crgflip/`, runner `build/tmp/wifidrv1-art/run-crgflip.sh`, spec `build/tmp/inta-spec/flip.md`): this is the row ARM A left QUEUED - now that stage 1's write path is proven on the live part, the one-way rank-1 flip (`flip.md` sec 3 rank 1 / sec 4) is ARMED and RUN. THE CODE (submodule commit `3d2e4cdf640d943d2a7d86c7f7056e2f64529b38` on `omo/phase22-hccaccept`, head `3d2e4cd feat(lab): luofu-clk - ARM the stage-2 CRG flip (the ranked i2c0_clk clear-flip)`, the only file touched `opensource/lab/luofu-clk/luofu-clk.c`): the target is the benign gate `0x14` bit `0x18` (`i2c0_clk`, the ranked i2c0/uart clock), the sequence is `read -> REFUSE unless the bit is SET -> CLEAR (store 1) -> OBSERVE (0x90/0x100, read-only) -> SET-BACK (store 2) -> PASS iff post == pre`, the new bound is `LUOFU_FLIP_WRITES = 2` on top of stage 1's 48 = 50 stores total (`LUOFU_WRITE_BUDGET`, compiled in as `cmp #0x32`, unchanged at 2 as sec 6 requires), the retired `pcie0_clk` target is absent from the artifact, and the instruction census (`build/tmp/wifidrv1-art/crgflip-tools/_ko-crgflip-census.py` -> `CRGFLIP-CENSUS: PASS`) shows the bound and the dword-alignment guard (`tst r1, #3`) compiled in, exactly ONE store site through the arg0-derived CRG base (`str r6, [r4]` in `luofu_crg_rmw`), exactly five RMW call sites (stage 1's three + the flip's two), the gate offsets only from `.rodata` (`0x14`/`0x20`), and the probe carrying `0x1000000` (BIT(0x18)), `0x14`, `0x90`/`0x100`. THE CI: `lab-module-build` run `37564223517`, headSha `3d2e4cdf640d943d2a7d86c7f7056e2f64529b38` on `omo/phase22-hccaccept`, status `completed` / conclusion **success**, artifact ko md5 `84a2f1f461cf18b95670c85e10051617` == `KO.md5` pin == the staged file, `vermagic=5.10.201 SMP mod_unload ARMv7`, all flip strings present (`OK store 1/2 = the clear`, `OK store 2/2 = the set-back`, `OK PASS post == pre`, `OK no pcie0_clk flip string`). THE RUN (`build/tmp/wifidrv1-art/run-crgflip.sh`, ONE serial `insmod force_probe=1 write_flip=1` -> dmesg -> `rmmod`, ko staged ALWAYS as `/tmp/wifidrv1.ko`, NO reboot, window 2026-10-07T03:00:43Z -> 03:00:50Z): gate CLEAN at `boot_id=faef074c-987f-4d9f-9c90-4feb75152120` (uptime 545s), slot `mtd14:rootfsb` (the custom image, 2.5.24), guard 0; the pre-read found bit `0x18` SET, so the refuse-unless-set precondition HELD and the flip proceeded; then the decisive lines, verbatim: `WRITE_FLIP i2c0_clk off=0x14 bit=0x18 pre=0x01364ef1 store 1/2 = the clear`, `WRITE_FLIP observe [0x090]=0x6a010008 [0x100]=0x00000000 (dynamic - NOT flip evidence)`, `WRITE_FLIP clear read-back=0x01364ef1 i2c0 bit=1 (the clear was IGNORED - write-1-set latch confirmed on a second gate)`, `WRITE_FLIP i2c0_clk store 2/2 = the set-back`, `WRITE_FLIP i2c0_clk: pre=0x01364ef1 -> clear=0x01364ef1 -> set-back=0x01364ef1: PASS post == pre (50 stores total)`, `WRITE_FLIP rc=0`, `FORCED probe PASS: 2/2 status regs read, 50 writes`. THE FINDING (the write half's second live datum): this CRG lock class is WRITE-1-SET-ONLY, not W1C - the CLEAR (`pre & ~BIT(0x18) = 0x00364ef1`) was IGNORED, so the flip could not take and the bit is 1 THROUGHOUT - which is the same write-1-set-only latch class ARM A found on `led_pwm` (bit `0x0e`), now confirmed on a SECOND gate, and it is exactly the shape a real driver must program by SET + an external reset, never by a clear (the `flip.md` sec 4 premise). Because the clear did not take, the instrument FAILED CLOSED: the store budget is the module's own counter (`FORCED probe PASS ... 50 writes` = stage 1's 48 + the flip's 2, exactly AT the budget, `cmp #0x32`), dmesg carries NO `write budget ... exhausted, refusing RMW` refusal line and NO `FAIL post != pre`, and no third store is available (`LUOFU_FLIP_WRITES = 2` is compiled in). THE RESTORATION IS EXACT (what "reversible" has to mean): the target register `0x14880014` reads `pre == clear read-back == set-back == 0x01364ef1` (bit `0x18` untouched), `0x14880020` reads `pre -> post 0x02f8300b -> 0x02f8300b` (all 11 gates already 1, unchanged), and the flip's OBSERVE words are dynamic, not flip evidence (`0x14880090 0x6a090008 -> 0x6a010008`, PLL cpu+lsw locked, `rst_reason=4`; `0x14880100` = `0x00000000` WDT_ISTATUS, benign). THE ONE NAMED RESIDUAL: `0x14` ENTERED the run at the fresh-boot `0x01364ef1`, not the pristine `0x01360ef1`, because stage 1's own 16-gate sweep had already SET `led_pwm` (`[STEP 4] led_pwm off=0x14 bit=0x0e pre=0x01360ef1 -> set=0x01364ef1 -> clear=0x01364ef1 -> back=0x01364ef1 (FLIP)`, `WRITE_TEST FAIL 15/16 gates no-op (1 flips), 48 stores`) - so the entry state carried ARM A's known, cosmetic, non-restorable `led_pwm` bit `0x0e` = 1, which clears on the next SoC reset; it is on a DIFFERENT bit from the flip target, so the flip's `post == pre` equality is a genuine measurement of its own gate and not a tautology of a wrong baseline. THE HEALTH of the router, gate / post / final all on the SAME boot: `GATE_BOOT=POST_BOOT=FINAL=faef074c-987f-4d9f-9c90-4feb75152120` (UNCHANGED, `F_UPTIME=588`, `F_REV=2.5.24`), slot `GATE_MTD_NUM=14 GATE_MTD_NAME=rootfsb GATE_ATTACH=14 GATE_GUARD=0`, `POST_WIPHY=2 POST_IFACE=6 POST_CAL2G=1 POST_CAL5G=1 POST_VENDOR_WIFI=1 POST_VENDOR_PLAT=1 POST_LEFTOVERS=0`, `RMMOD_RC=0 LUOFU_AFTER_RMMOD=0 STAGED_LEFT=0 RUN_DONE`, zero `wifidrv1`/`luofu`/guard/`.omo-*` leftovers, staged ko removed, no kernel fault (the final probe's `F_FAULT=1` is its crude regex counting the BOOT-TIME info line `[ 7.569200] pstore_zone: registered pstore_blk as backend for kmsg(Oops,panic_write)` at t=7.5s - not a run fault, named not hidden). THE VERDICT (`build/register-dumps/diffs/20261007T1005Z-vrunF/verdict.txt`, host-only re-read of the frozen receipts, NO device cycle by the verifier, NO commit, NO push; sibling arm-A verdict `build/register-dumps/diffs/20261007T0236Z-vrunW/verdict.txt`): C1 the flip ran and the equality holds CONFIRMED, C2 the restoration is exact CONFIRMED, C3 the health block holds CONFIRMED, C4 the CI artifact is real and IS the artifact the run fetched CONFIRMED, C5 the code matches `flip.md` sec 6's arming change (target / polarity / sequence / bound) CONFIRMED, C6 the hard bars were held CONFIRMED => ARM B IS CONFIRMED: the armed flip DROVE the live CRG through exactly two bounded, dword-aligned stores on the sanctioned benign gate, the CLEAR did not take, and the instrument left the register bit-for-bit as found. Overall arm-B tally on this line stays `B1..B6 B CONFIRMED` (the measurement phase closed at B5; the write half's stage 2 is now MEASURED, not merely queued), with the honest counterweights named: the verifier's own precision gaps (the flip observed only the CLEAR-IGNORED outcome, so the "the clear TOOK -> a real, exactly-restored flip" branch is present in the code and the selftest's mutation C but is NOT exercised by this run; the verdict skeleton was filled by hand from the `vrec23` template, `tools/finish-evidence.sh` was NOT re-run against this dir; and the run's own follow-up CI run `37565449816` - "close the flip lane" - is NOT asserted by the verdict, whose C4 rests on the RUN's CI/artifact `37564223517` / md5 `84a2f1f4...`), and the residual (`led_pwm` bit `0x0e` set) clears on the next SoC reset. WHAT REMAINS for the whole write half: (1) NOTHING further on the flip's own claim - it is measured and closed; (2) a real driver must program this latch class by SET + external reset (a CLEAR is ignored, now confirmed on two gates); (3) the stage-2 path against the PRISTINE `0x01360ef1` entry state is still owed if a clean `post == pre` on an UNMOVED baseline is wanted (a fresh boot with stage 1 skipped, or a stage-1 re-run that returns `WRITE_TEST PASS 16/16`, ARM A's own owed item); (4) the actual PCIe/i2c0 bring-up work stays the subsystem table's `from-scratch` items, not another CRG probe. The runner (`build/tmp/wifidrv1-art/run-crgflip.sh`) stays in place with its fail-closed gate, its `--selftest` mutation battery (`bad-step`/`bad-flip`/`bad-refused`/`bad-count`) and its `boot_id` UNCHANGED assertion; hard rules held this session: CA `0x400392f0` never written; the RC misc `0x10161000` never read or mapped; the host-side IAR `0x4016010c` never read; dword-aligned accesses; the device action serial + gate-checked (and it refused to proceed until a stale zero-byte `/tmp/omo-t7-hide.done` from the CLOSED take7 lane was cleared, so no mutation preceded a clean gate); ko staged ALWAYS as `wifidrv1.ko`; push authorization used ONLY on the submodule branch `omo/phase22-hccaccept`, never `master`; NO reboot.

ARM B - THE DESIGN AND THE NEXT STAGE: the write half's stage 2 is MEASURED and its next step is a NON-LATCHING target, not another clear (`build/tmp/inta-spec/flip.md` §0-§7; arm-B record `build/tmp/wifidrv1-art/crgflip/{RESULT.txt,SELFTEST.txt,README.txt}`; verdict `build/register-dumps/diffs/20261007T1005Z-vrunF/verdict.txt`). THE DESIGN AS WRITTEN: §1 FIND 1 - the gate latches are **SET-ONLY** (the smoke's hidden `clear=` column: every clear (`v & ~BIT`) left its word identical on 16/16 bits, including the four `0x14` 1-bits and the eleven reconstructed `0x20` bits; the sole store that took, 1/1, was the SET of `0x14` bit `0x0e` `led_pwm`), so the vendor's own write-0 `hi_clk_gate_disable` shape (`hi_clk.ko` @0x1184) is inert on this part and the gates stay as U-Boot left them; §2 FIND 2 - the direction is forced, because the SET's undo is the ignored write-0 while the CLEAR's undo IS the proven SET, so the flip must be launched in the CLEAR direction and can only fail closed; §3 - the candidates ranked by blast radius (`0x14` b18 `i2c0_clk` rank 1: no i2c client node in the pinned tree, no `i2c` line in a boot log -> nothing behind the gate moves; rank 2 `gemac_clk0`, rank 3 `gpio0/1_clk` (a 20 ms `gpio-keys-polled` access -> the external-abort/panic class), rank 4 `mdio_clk0`, rank 5 `pcie0/1_clk` (a live Wi-Fi endpoint - the `wrdesign.md` sec 4 rank 1 pick, overturned), rank 6 `lsw_dp/pfe`, rank 7 `gemac_clk1..4`, rank 8 `pie_clk0`; `sfc_clk` NEVER, it is the rootfs); §4 - the exact armed sequence `pre = readl(0x14); refuse unless BIT(0x18) SET; CLEAR (store 1); OBSERVE 0x90/0x100 (the only words the pinned tree licenses as pure reads); SET-BACK (store 2); PASS iff post == pre`, budget `LUOFU_FLIP_WRITES = 2` on top of stage 1's 48 = 50, self-terminating with no 3rd store, and the two outcomes are both safe (expected: the clear is a no-op -> write-1-set confirmed on a second gate; or the clear TOOK -> a real, exactly-restored flip, and only THEN is the instrument admissible on a live gate); §5 - the recovery bullets (watchdog armed before staging, pstore across a reset, a reboot gates on a NEW `boot_id`, the slot gate `mtd14`/`rootfsb` BEFORE staging, one serial action, ko always `wifidrv1.ko`, vendor modules never `rmmod`ed, health on 2.5.24 with 2 wiphys / 6 interfaces / cal `[SUCC]`); §6 - the target/aim repair the run then applied (the compile-gated path at `luofu-clk.c:367-392` flipped `0x20` b`0x0c` `pcie0_clk`, whose precondition is FALSE live (`0x20 = 0x02f8300b`, bit `0x0c` already 1 -> the `-EBUSY` refusal) AND whose restore is the ignored write-0 AND which sits on a live Wi-Fi endpoint's clock gate, so the arming change is target `0x20`/`0x0c` -> `0x14`/`0x18`, polarity inverted to refuse-unless-set, sequence clear-first, bound unchanged at 2); §7 - open items NOT asserted (whether a companion DISABLE register exists for this latch class - the Hisi `clkgate-separated.c` pattern, answerable only by an undocumented CRG read that is itself unproven-safe; and the `0x2c`/`0x30`/`0x34` reset bits, a DIFFERENT class (`hi_kreset`: assert = clear, deassert = set) and the only one the vendor exercises in both directions, so a reset-class flip is the natural follow-on instrument). WHAT THE RUN RETURNED (arm B, `st_01a1144c` + verifier `st_01a11450`, verdict vrunF = all six claims CONFIRMED, `RESULT.txt` = `PASS`, `SELFTEST.txt` = `SELFTEST PASS`): the armed instrument ran and failed CLOSED - `0x14 = pre 0x01364ef1 -> clear 0x01364ef1 -> set-back 0x01364ef1`, bit `0x18` left 1, `WRITE_FLIP rc=0`, `50 stores total` inside the bound, no refusal, no 3rd store, `boot_id` UNCHANGED, router healthy. THE NEXT STAGE (the write half's step after this record): (1) do NOT re-run `write_flip=1` against a set-only latch - a measured no-op repeats; (2) the admissible target is a NON-LATCHING one, i.e. the `0x2c`/`0x30`/`0x34` reset-class (`hi_kreset`) or a companion disable register if one is ever proven to exist (`flip.md` §7) - the gate class the vendor's own `hi_clk_gate_disable` shape does not write; (3) a stage-1 re-run returning `WRITE_TEST PASS 16/16` on a FRESH boot with `0x14` measured from the PRISTINE `0x01360ef1` is still owed (ARM A's item, `led_pwm` bit `0x0e` being the logged one-way residual) - only then can `post == pre` be shown against a pristine baseline rather than ARM A's post-sweep `0x01364ef1`; (4) the actual PCIe/i2c0 bring-up work is the subsystem table's `from-scratch` items, NOT another CRG probe; the driver-skeleton carve-out (the synthetic `luofu-crg` platform_device has no `.release`, so `platform_device_unregister()` warns `drivers/base/core.c:1836` on EVERY unload - needs `.dev.release` or `platform_device_register_simple`, CI rebuild) is a code fix in `lab/luofu-clk`, not a probe. THE RC DEPENDENCY: the CRG is the FIRST dependency of the stage-3 PCIe RC line - the pcie0/pcie1 gates are `0x20` b0c/b0d (`flip.md` §0/§3 rank 5, the reconstruction the stage-1 probe then CONFIRMED against the live part), and because the class is set-only, the flip of a clock gate can never be undone in software, so a driver that needs a gate OFF must be reset-class or must find the companion disable register FIRST - i.e. the from-scratch DWC RC (`pcie-histb.c` shape, the five windows / `iatu_rc` table / LTSSM already measured by the arm-B4/B5 RC inventories) cannot rely on the CRG's gate-clear path; this is the dependency the arm-B4/B5 RC work and this arm-B flip now agree on. Hard rules unchanged for anything that touches the device: never write CA `0x400392f0`; never read `0x10161000`; never read the IAR `0x4016010c`; device cycles serial/detached via `tools/exp.sh` only (`run-crgflip.sh` carries the fail-closed gate + `--selftest` battery + the `boot_id`-UNCHANGED assertion); ko staged ALWAYS as `wifidrv1.ko`; leave the router healthy on 2.5.24; push authorization ONLY on the submodule branch `omo/phase22-hccaccept`, never `master`.

ARM B - THE WRITE-ONLY misc WINDOW'S FIRST DELIBERATE WRITE: THE STAGE-2b RESET-CLASS FLIP IS IMPLEMENTED AND CI-GREEN, AND THE RUN IS QUEUED, NOT SAMPLED (2026-10-07, task `st_01a11479`; submodule commit `78afefd feat(lab): luofu-clk - the stage-2b RESET-class flip (crgnext rank 1: 0x2c bit 0x18 i2c0_rst)` on `omo/phase22-hccaccept`, the only file touched `opensource/lab/luofu-clk/luofu-clk.c`; spec `build/tmp/inta-spec/crgnext.md`, the design record `build/tmp/wifidrv1-art/crgnext/README.txt`, runner `build/tmp/wifidrv1-art/run-crgnext.sh`): this is the follow-on the previous arm-B bullet named as ADMISSIBLE - the flip moved OFF the set-only gate class and ONTO the `0x2c` RESET class, because a gate clear can never be undone in software on this part. THE CLASS CHANGE: the target is `0x2c` bit `0x18` (`i2c0_rst`), i.e. the `hi_kreset` shape the vendor exercises in BOTH directions (assert = store 0, deassert = store 1), so unlike the `0x14`/`0x20` gates both stores are expected to take and the instrument's two outcomes are a real flip and an exact restore; the sequence is the armed `read -> REFUSE unless the deasserted (SET) precondition holds -> ASSERT (store 1) -> OBSERVE `0x90`/`0x100` (read-only) -> DEASSERT (store 2) -> PASS iff post == pre`, the bound is `LUOFU_FLIP_WRITES = 2` on top of stage 1's 48 = the same 50 (`LUOFU_WRITE_BUDGET`, `cmp #0x32`, unchanged), and the `0x14`/`0x20` gate pairs stay compiled OUT of this build. THE CI: `lab-module-build` on `omo/phase22-hccaccept`, headSha `78afefdf...`, run `37570189974`, status `completed` / conclusion **success**, artifact `luofu-clk-ko` (`build/tmp/wifidrv1-art/crgnext/luofu-clk.ko`, md5 `2b0e1d41e6a99b4b4de2b90060f660f7`), `vermagic=5.10.201 SMP mod_unload ARMv7`. THE STATE: the ko is built and pinned and the runner is in place, but NO device cycle ran for this lane - the `0x2c` bit `0x18` target is NOT yet measured on the live part, so this bullet records a PREPARED instrument, not a result, and the write-half claims stay exactly where the previous bullets left them (stage 1 FAIL on gate 4, the `0x14`/`0x18` clear-flip CONFIRMED as a no-op). WHY THAT IS THE RIGHT STOP: the instrument is admissible precisely because the reset class is the one the vendor writes in both directions, and its failure mode is the same fail-closed one (`post != pre` -> FAIL, the 3rd store unavailable, the budget is the module's own counter). WHAT REMAINS: (1) the one-command run `bash build/tmp/wifidrv1-art/run-crgnext.sh` behind the fail-closed gate (health + SLOT CHECK `mtd14`/`rootfsb` + no active cycle), staged ALWAYS as `wifidrv1.ko`, one serial action, `boot_id` UNCHANGED asserted; (2) only a run returning `post == pre` with the ASSERT/deassert read-backs PROVEN to take earns the stage-2 reset-class flip its CONFIRMED - if the `0x2c` class also ignores the write-0, the instrument fails closed and the residual is the `hi_kreset` decode, not the probe; (3) the gate-class work (ARM A's owed `WRITE_TEST PASS 16/16` on a FRESH boot with `0x14` measured from the pristine `0x01360ef1`) is unchanged and independent; (4) the actual PCIe/i2c0 bring-up stays the subsystem table's `from-scratch` items. Hard rules held this lane: NO device was touched, so CA `0x400392f0` was never written, `0x10161000` never read or mapped, the host-side IAR `0x4016010c` never read, the dword-alignment guard (`off & 3`, compiled in as `tst r1, #3`) and the store bound are compiled into the shipped artifact, ko staged ALWAYS as `wifidrv1.ko` when run, and push authorization was used ONLY on the submodule branch `omo/phase22-hccaccept`, never `master`.

THE 2026-10-07 CODE SPRINT - SIX LANES - **SPRINT RESULT: COMPLETE**: the port's open threads became loadable (and compilable) code, five lanes CI-green, four of them smoked live on the router, and the class stayed clear of the data path and of every vendor register. This is the sprint's status block, written by task `st_01a114c1` from the artifacts and verdicts, not from a device action of its own; no commit accompanied it. (1) **DT** - `opensource/docs/soc/luofu-r116.dts`, 20 new nodes re-expressed against `hisilicon,luofu-*` (the full CRG with its 2 PLLs and 2 muxes and the binary-only `rstinfo` folded in, `pinctrl_peri`, `pcie0`/`pcie1` with their `iatu_rc`/`iatu_ep` tables and `linux,pci-domain` ids, `uart0`/`uart1`, `fmc` + 17 A/B partitions, 4 gemacs + `mdio0` + 5 PHYs, the 3 LSW blocks, `pie`), the `LUOFU_CLK_*` block grown 7 -> 16 gates, every new vendor node `status = "disabled"`, the efuse/pwm-regulator/thermal subsystems deliberately left out. CI = `tools/dtc-check.sh` under dtc 1.7.2: `PASS size=10807 sha256=c3b2c607409b1c10a9b4864e67fc973426dee8cd7e60567a9e7fef4202549ee1 errors=0 warnings=3` (the 3 are the pre-existing `unit_address_vs_reg` nits). No smoke (a DTS is not a module). Next rung: the `mach-luofu` Kconfig entry and the in-tree `crg-luofu.c` landing. (2) **CRG** - `opensource/lab/luofu-clk/` (1,014 lines + `bindings/hisilicon,luofu-crg.md`): the pinned 16-gate/2-mux/2-PLL geometry in structs through the generic `clk-provider.h` API, all clocks `CLK_IGNORE_UNUSED`, a DT onecell provider. CI = `lab-module-build` run `37573235229` on head `4f0373f` (success, all four matrix jobs, artifact `luofu-clk-ko`) with the `CRG rule census` step (exactly one `writel()` = the RMW primitive, no sub-word stores, `off & 3` guard). SMOKE (`build/tmp/inta-spec/sprint-smokes.md` sec.1): ko md5 `763c346a78d9b549edc787c1efd620e1`, `insmod force_probe=1` rc=0 / `rmmod` rc=0, banner `write_test=0 write_flip=0 rst_flip=0 read-only`, `[0x090] CRG_STATUS = 0x6a010008`, `[0x100] WDT_ISTATUS = 0x00000000`, `0 writes`. Next rung: the `reset_controller_dev` the driver still owes (the DEASSERT direction is proven live by the stage-2b run, `0x2c` bit `0x18` `i2c0_rst` cleared then restored exactly; the `0x14` clock-gate class is set-only, so a gate can never be flipped back in software), then the PLL/mux rate math and the move into `drivers/clk/hisilicon`. (3) **pinctrl** - `opensource/lab/luofu-pinctrl/` (611 lines): the placeholder skeleton replaced by the vendor's real tables read out of `hi_kpinctrl.ko` and cited per entry (37 pins `.data+0x428`, 24 groups `.data+0x68` with pins `.rodata+0x32c..0x3e4`, 24 functions `.rodata+0x20c`, 37 x 5 mux `drv_data` `.data+0x644..0xf84`); `set_mux` and the pinconf setters stay no-op stubs. CI = `lab-module-build` run `37573603109` on head `5a049e15`, green after one repair (`5a049e1`: `pinctrl_utils_free_map` is not exported in vanilla 5.10, so the lane switched to `pinconf_generic_dt_node_to_map_all` plus `pinconf_generic_dt_free_map`). SMOKE (sec.2): ko md5 `3a09887a62a52e9c43218d46a320e9fa`, `insmod` returned rc=16 (Resource busy) with `Error: Driver 'luofu-pinctrl' is already registered, aborting...` - the vendor `hi_kpinctrl` owns that exact platform-driver name and is bound to the same live DT node, so nothing loaded and the device was left alone. A naming finding, not a device anomaly. Next rung: rename `.driver.name` (or drop the DT match) and re-run. (4) **wifi** - `opensource/lab/luofu-wifi/` (822 lines): our own `struct pci_driver` for `59e7:0005` - four pages of the region-3 inbound window, the announce/handshake state machine, the glue/mailbox on copy A and the twin, the SR/DR ring and credit bookkeeping, every read through the aligned accessor and no store expression anywhere in the source; it also encodes the ETE register-block correction (`SR {0x400,0x450,0x4a0}`, `DR {0x590,0x5e0,0x630,0x680}`, 0x50 apart, not the `0x114`/`0x6c` host struct strides `credit2.md` sec.5 retracts). CI = `lab-module-build` `37573603109` (the `master`-only `build-load-test-module.yml` lane was not exercised by this push and mirrors the others). SMOKE (sec.3): ko md5 `2fb63afedc8bc335f8a4c4a0eca92309`, `insmod`/`rmmod` rc=0 on the default `hw=0` path (registration/ABI only, zero PCI contact); `hw=1` was deliberately not exercised because the live endpoint is vendor-bound on both domains. Next rung: the glue ISR (`request_irq` the bound endpoint's INTx virq, dispatch on status mask `0x3d8`, with the in-ISR bound), then the H2D/D2H service, the rings, the wiphy. (5) **PCIe** - `opensource/lab/luofu-pcie/` (1,005 lines): the platform-driver frame plus dword-aligned read AND write accessors (`luofu_pcie_read` / `luofu_pcie_write`), the LTSSM read-state machine over the DBI `PORT_LOGIC_DEBUG0/1` words, a compile-time alignment rule (`luofu_pcie_check_aligned()` BUILD_BUG_ONs every raw-dword offset) and a commented host-bridge/CRG scaffold; the `misc` window stays mapped WRITE-ONLY and is never read. CI = `lab-module-build` `37573603109` with the new `alignment rule` step (fail on any `readb/readw/writeb/writew`, the panic class, and on a misaligned literal in a raw `readl/writel`). SMOKE (sec.4): ko md5 `385fc736ca6088c7b8ebe14cd59d4549`, `insmod force_probe=1` rc=0 / `rmmod` rc=0, the DBI inventory matching 3/3 and the config space 4/4, and `rc0 [0x082] Link Status = 0x7012` read through the aligned accessor, the very word whose `readw` panicked the box earlier; the aggregate `link DOWN` is the AND of the two `DL_ACTIVE` predicates, a measurement rather than a failure, while the DWC LTSSM independently reads L0. Next rung: wire the CRG and pinctrl providers, then land the staged write path and the `pci_host_probe` registration. (6) **kernel config** - `opensource/lab/luofu-kernel/{luofu.fragment,README.md}` plus the new lane `.github/workflows/luofu-kernel-config.yml`: a delta on `multi_v7_defconfig` at 5.10.201, symbols `=y`, 47 non-`LUOFU` symbols and 13 new ones, each carrying its vendor evidence in the lane README. CI = that lane's own three runs, and it earned its keep: run `37572753552` on `6ad9e48` FAILED because `merge_config.sh` runs `make alldefconfig` internally with no `ARCH`, so the inner make resolved a host x86 config (fix `78823b3`, `export ARCH=arm`); run `37572932947` on `78823b3` then caught a genuine finding, `PSTORE_BLK` depends on the promptless `BROKEN` symbol, so pstore-on-MTD is unreachable in a vanilla tree without the vendor patch (fix `c40637a`); run `37573150202` on `c40637a` is SUCCESS with 47/47 requested symbols `=y` and the 13 `LUOFU` ones listed as a to-do. No device smoke applies to a config file. Next rung: the 13 Kconfig entries with their `select`s, which is also what turns the lane's opt-in `kernel` job from boilerplate into a real build. THE LIVE PASS: the four module lanes were walked clk, pinctrl, wifi, pcie in that order, ONE serial device action per module, each preceded by its OWN gate (health plus SLOT CHECK `mtd14`/`rootfsb` plus no active cycle); three load and unload CLEANLY and the fourth is refused by the kernel. `boot_id 4f8f2940-d1f3-41d7-a61d-67f00de1e41c` was unchanged end to end, no reboot happened, every ko was staged AS `/tmp/wifidrv1.ko`, and the final health reads `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 LEFTOVERS=0`, stock md5 `0e530b976d5a20e87358671f1a577695` at pin, on `mtd14:rootfsb` 2.5.24. THE HONEST LIMIT OF THE CLASS: the five module lanes were not byte-frozen into one shared artifact set, since they live in a single submodule; the CI receipts and the live smoke ran against the same head (`5a049e15`), which is as close to frozen artifacts as a single-checkout sprint gets. None of the six lanes has a data path, an ISR, or a write to any vendor register, and the one deliberate device write in this sprint's neighbourhood was the stage-2b RESET-class CRG flip (`0x2c` bit `0x18`, cleared then restored exactly, PASS), not a lane smoke. Hard rules held: CA `0x400392f0` never written, the RC misc `0x10161000` never read or mapped, the host-side IAR `0x4016010c` never read, dword-aligned accesses throughout (CI-gated in `luofu-pcie`/`luofu-clk`), the take-family ko staged ALWAYS as `wifidrv1.ko`, push authorization used ONLY on the submodule branch `omo/phase22-hccaccept`, never `master`, and the router was left healthy on 2.5.24. The full per-lane record is `opensource/docs/phase49/gic-view.md` ADDENDUM 33 and the lane notes under `build/tmp/inta-spec/`.
