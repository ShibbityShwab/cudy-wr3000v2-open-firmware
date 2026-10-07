# luofu-kernel

Stage-1/2 **kernel-config inventory** for the HiSilicon Hi5671Y "luofu" SoC
(board R116, Cudy WR3000 v2.0) - the `CONFIG_*` list our own, fully-current
kernel needs, as a single mergeable fragment.

`luofu.fragment` is a **config fragment** deltas on top of the vanilla ARMv7
multi-platform base, `arch/arm/configs/multi_v7_defconfig`, kernel **5.10.201**
(the exact version the vendor firmware ships). It is host-side only; it touches
no device and follows none of the port's register hazards.

    make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- multi_v7_defconfig
    scripts/kconfig/merge_config.sh .config lab/luofu-kernel/luofu.fragment
    make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig

## What it covers

The inventory is grouped by the stage-2 bring-up order
(`build/tmp/inta-spec/stage2.md` sec.3, the ranked driver list; the PCIe row is
detailed in `build/tmp/inta-spec/pcierc.md` sec.5):

| group | why the kernel needs it (evidence) |
| --- | --- |
| SoC / platform | `ARCH_HISI` + the new `ARCH_LUOFU` mach; `SMP` for the dual Cortex-A9 (`enable-method = "hisilicon,hsan_smp"`) |
| CRG (clocks + reset) | `hi_crg` taints the kernel first at 0.98 s (`dmesg`), `hi_clk`/`hi_kreset` back every later block; one `hisilicon,luofu-crg` node provides `#clock-cells`/`#reset-cells` |
| pinctrl / IOMUX | `hi_kpinctrl` (5 refs); the mux+cfg windows at `0x14900100`/`0x14940000` |
| DWC PCIe | two `hsan,pcie` domains, `PCIe:0/1 switch to RC mode` -> buses `0000:00`/`0001:00` |
| UART | `dw-apb-uart 1010f000.uart1: forbid DMA for kernel console` |
| NAND (FMC) + UBI/pstore | `hi_nand_init HSAN FMC Controller`; `ubi`/`mtdpstore`/`pstore_blk`/`pstore_zone` |
| Ethernet (GMAC + MDIO + LSW) | `hi_kmac`/`hi_kphy`/`hi_kmdio` + the `hi_lsw_*` internal switch |
| Wireless | `cfg80211`/`mac80211` for our own `wifidrv1` (the blob is kept) |

## The rules this fragment holds to

1. **Prompted symbols only.** A fragment cannot set a *promptless* (hidden)
   symbol - `make alldefconfig` drops the request and `merge_config.sh` reports
   `Value requested for CONFIG_X not in final .config`. The hidden framework
   symbols the port needs (`PCIE_DW`, `PCIE_DW_HOST`, `PCI_DOMAINS`,
   `PCI_DOMAINS_GENERIC`, `PCI_MSI_IRQ_DOMAIN`, `MTD_NAND_CORE`,
   `MTD_NAND_ECC_SW_HAMMING`, `GENERIC_PINCONF`, `REGMAP`, `REGMAP_MMIO`,
   `MDIO_BUS`, `THERMAL_OF`, `PSTORE_ZONE`) are therefore listed in the
   `# selected indirectly by ...` blocks and must be `select`ed by the matching
   new driver's Kconfig entry. Verified against the 5.10.201 Kconfig tree.
2. **No trailing comments on a `CONFIG_` line.** `merge_config.sh` compares
   whole lines, so a trailing comment reads as a value change (and a merged
   `.config` would carry junk). Rationale sits in the comment block above.
3. **`NEW` marks our symbols.** The 13 `CONFIG_*_LUOFU` symbols (plus
   `ARCH_LUOFU`) have no Kconfig entry yet - each needs one, with a prompt and
   the matching `select` of rule 1. Until then the CI lane reports exactly
   those as "requested but not in final `.config`", which is the to-do list.
4. **Baked in, not modules.** The vendor carries many of these as `.ko`; a
   self-contained OpenWrt target wants them `=y` (stage2.md sec.3 note).

## Build / verify

- `opensource/.github/workflows/luofu-kernel-config.yml` (trigger `omo/**` +
  `workflow_dispatch`) runs the kernel's own `merge_config.sh` +
  `olddefconfig` against the real 5.10.201 Kconfig and asserts every
  non-`NEW` symbol survived; a full `zImage` build is the opt-in
  `build_kernel` dispatch input (the boilerplate OpenWrt/vanilla evaluation).
- The reasoning and the symbol-by-symbol classification live in
  `build/tmp/inta-spec/kcfg.md`.
