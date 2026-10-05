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

Stage 1 skeleton landed (2026-10-05): `opensource/docs/soc/luofu-r116.dts` is the first-cut mach DT skeleton
(Step 3), with every register value, interrupt number and reset/clock cell kept verbatim from the pinned
tree and only the bindings re-expressed (free bindings in mainline form, vendor `hsan,*` blocks as
`hisilicon,luofu-*` placeholders with `status = "disabled"`). It is not buildable yet; the build recipe and
file-by-file rationale live in the stage-1 spec at `build/tmp/inta-spec/stage1.md`.

## 5. Source URLs (mainline evidence)

- OpenWrt targets list, no HiSilicon router target (no `luofu`/`hsan`/Hi5671): https://github.com/openwrt/openwrt/tree/master/target/linux
- Hisilicon Ethernet drivers: `hix5hd2_gmac.c`, `hip04_eth.c`, `hisi_femac.c`, `hns/`, `hns3/` (no `hsan,mac`): https://github.com/torvalds/linux/tree/master/drivers/net/ethernet/hisilicon
- DesignWare PCIe host drivers: `pcie-histb.c`, `pcie-hisi.c`, `pcie-kirin.c` (HiSilicon STB glue): https://github.com/torvalds/linux/tree/master/drivers/pci/controller/dwc
- DesignWare APB UART: `8250_dw.c` (`snps,dw-apb-uart`): https://github.com/torvalds/linux/blob/master/drivers/tty/serial/8250/8250_dw.c
- DesignWare APB GPIO: `gpio-dwapb.c` (`snps,dw-apb-gpio`/`-port`): https://github.com/torvalds/linux/blob/master/drivers/gpio/gpio-dwapb.c
- Hisilicon clock/CRG/reset drivers: hi3519/3559a/3620/3660/3670/6220/hip04/hix5hd2, `crg-hi3798cv200.c`, `reset.c` (no `hsan,clk`/`hsan,crg`/`hsan,reset`): https://github.com/torvalds/linux/tree/master/drivers/clk/hisilicon
- SPI-NAND core + flash drivers: reusable `spi-nand` framework, no `hsan,fmc` controller: https://github.com/torvalds/linux/tree/master/drivers/mtd/nand/spi
- Hi5671YV200/Hi5622V100: no public mainline driver (closed NDA BSP; web search 2026-10-05); closest open precedent OpenIPC/openhisilicon: https://github.com/OpenIPC/openhisilicon
