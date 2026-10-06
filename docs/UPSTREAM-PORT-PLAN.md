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
