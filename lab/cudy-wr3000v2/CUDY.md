# CUDY.md - Cudy WR3000 v2 -> BPI TR6560 BSP port plan

Port plan for building the Cudy WR3000 v2.0's own firmware from the Banana Pi
TR6560 BSP (our device's cousin silicon). This file is the map. The workflow that
builds the baseline is `.github/workflows/bsp-tr6560-build.yml`; the literal local
reproduction commands are in `README.md` next to this file.

## 1. The BSP we build against

| | |
| --- | --- |
| upstream | `BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP` (the Banana Pi BPI-Wifi6 fork) |
| base | OpenWrt 22.03-era, kernel 5.10 |
| target | `tr6560` / subtarget `generic` / device `THG6500-TAX2` |
| config | a full `.config` committed at the BSP repo root (`CONFIG_TARGET_tr6560_generic_DEVICE_THG6500-TAX2=y`) |
| arch | `arm_cortex-a9` + musl (the same package arch as the vendor's own 22.03.6 userland) |
| images | `bin/targets/tr6560/generic/` -> `sysupgrade.bin`, `fullimage.bin` (plus `BurningImage.bin` when `BURNING` is set) |

The BSP is the closest public OpenWrt tree to this silicon: same SoC family
(Triductor TR6560), same register map, same 5.10 kernel line.

## 2. Delta vs the BPI board (from the dtscmp lane)

The dtscmp lane (`build/tmp/bsp-notes/dtscmp/dtscmp.py`) decompiled our base DTB
(`build/tmp/uboot-art/vendor_base.dtb` -> `cudy.dts`) and diffed it against the
BSP's `triductor-tr6560.dtsi` + `THG6500-TAX2.dts`. The full output is `delta.md`
in the same directory; this is the summary of what differs.

1. **Compatible namespace.** Ours is `hsan-luofu` with the `hsan,` vendor prefix;
   the BSP is `triductor,tr6560` with the `tri,` prefix. Every SoC-identity node
   (GIC, SCU, L2, TWD timer, SP804, both UARTs, the FMC flash controller, both
   PCIe RC/EP ports, the GPIO blocks) sits on the **same MMIO addresses** - only
   the binding names differ, so the port is a driver-binding + board-layer job,
   not a memory-map job.
2. **Clock/reset model.** Our DTB carries a full clock tree (`/clk@14880000` with
   ~20 PLL/mux/gate children, `/reset0`, `/rstinfo`, an OPP table with 3 OPPs).
   The BSP collapses this to a minimal clock model (fixed clocks + bare
   `crg@14880000` / `iomux@14900000` nodes) and has no reset controller, no OPP
   table, no clock-gate model.
3. **Flash layout.** Both are A/B, but different shapes - see section 3.
4. **Ethernet/switch.** Ours models the switch as separate `hsan,lsw_*` nodes +
   five `gemac@...` (`hsan,mac`) MACs + `mdio0` (`hsan,mdio`) + `pie` (`hsan,pie`);
   the BSP folds it into a single `pie` node (`tri,pie`) holding the `mdio-bus`,
   plus a `halport0` whose `wan`/`lan1..lan4` ports are filled in by the board
   file. Our `hsan,pfe`/`hsan,dp`/`hsan,pie` nodes must be re-expressed in the
   BSP's tree.
5. **Wireless attachment.** Neither base blob describes a radio. Ours is the
   Hi5622V100 behind PCIe (`docs/FLASH-PLAN.md`); the BSP board declares
   `tr5220`/`pcie2_0` on its `board` node.
6. **Console.** Ours `console=ttyS0` on `uart1@0x1010f000`; the BSP uses
   `ttyS1` (same two UARTs, same addresses, different naming).
7. **SMP.** Ours `enable-method = "hisilicon,hsan_smp"` via
   `system-controller@10100000` (`smp-offset = <0xc00>`); the BSP
   `tri,tr6560-cpu-method` via `sysctrl` (`smp-offset = <0xc08>`).
8. **Board layer.** The BSP board file adds `leds`/`keys`/`board`; on ours that
   board identity lives in the U-Boot overlays (`kernela_overlay_board{0,3}.dtbo`,
   board_id 3 = this unit's R116), not in the base blob.

Net: **the Cudy did not re-map the SoC** - the register map is the reference
platform's. The work is (i) the `hsan,` -> driver binding, (ii) the clock/reset/OPP
tree the BSP omits, (iii) the 17-partition dual-slot flash layout, (iv) the switch
block, and (v) board identity.

## 3. Flash layout, side by side

Offsets are quoted from the tracked sources, not inferred.

### Cudy WR3000 v2 (`docs/soc/luofu-r116.dts`, lines 387-403)

| label | offset | size |
| --- | --- | --- |
| esbc | 0x0 | 0x40000 |
| uboota | 0x40000 | 0x100000 |
| ubootb | 0x140000 | 0x100000 |
| enva | 0x240000 | 0x40000 |
| envb | 0x280000 | 0x40000 |
| fac | 0x2c0000 | 0x200000 |
| bdinfo | 0x4c0000 | 0x40000 |
| cfga | 0x500000 | 0x200000 |
| cfgb | 0x700000 | 0x200000 |
| log | 0x900000 | 0x440000 |
| pstore | 0xd40000 | 0x40000 |
| kernela | 0xd80000 | 0x840000 |
| kernelb | 0x15c0000 | 0x840000 |
| rootfsa | 0x1e00000 | 0x1740000 |
| rootfsb | 0x3540000 | 0x1740000 |
| rootfs_data | 0x4c80000 | 0x1600000 |
| upgrade | 0x6280000 | 0x1d80000 |

17 partitions, total 0x8000000 = 128 MiB. `kernela`/`kernelb` are the two kernel
slots (0x840000 = **8,650,752 B** each - `docs/FLASH-PLAN.md` requires writing the
whole slice, uImage + FIT tail at `0x4185D0`). `rootfsa`/`rootfsb` are the two UBI
slots; the sysenv register `/sys/devices/platform/sysenv/boot_reg` selects A
(`0x10`) or B (`0x21`).

### BSP tr6560 (`target/linux/tr6560/files-5.10/arch/arm/boot/dts/triductor-tr6560.dtsi`)

| label | offset | size |
| --- | --- | --- |
| uboot | 0x0 | 0x80000 |
| kernelA | 0x80000 | 0x800000 |
| rootfsA | 0x880000 | 0x2000000 |
| firmwareA | 0x80000 | 0x2800000 |
| kernelB | 0x2880000 | 0x800000 |
| rootfsB | 0x3080000 | 0x2000000 |
| firmwareB | 0x2880000 | 0x2800000 |
| rootfs_data | 0x5080000 | 0x2e00000 |
| equip | 0x7e80000 | 0x80000 |
| wlanrf | 0x7f00000 | 0x80000 |
| upgflag | 0x7f80000 | 0x80000 |

11 named partitions; `firmwareA`/`firmwareB` are OpenWrt wrappers over their
`kernel*`+`rootfs*` pair (so the table double-counts). Same A/B idea, sized for a
different flash: 8 MiB kernel slots, 32 MiB rootfs slots, and a `wlanrf` blob that
the Cudy has no partition for.

A BSP image must not be flashed as-is: the kernel slots sit at different offsets
(ours `kernela@0xd80000` vs the BSP `kernelA@0x80000`) and sizes (0x840000 vs
0x800000), and the boot-header partition differs (ours `esbc` 0x40000 at 0x0 vs the
BSP `uboot` 0x80000 at 0x0).

## 4. Device tree source

- Single source of truth (tracked): `docs/soc/luofu-r116.dts`.
- dtscmp output (decompiled, not committed): `build/tmp/bsp-notes/dtscmp/cudy.dts`
  plus `delta.md`. Regenerate from the repo root with
  `pyenv/Scripts/python.exe build/tmp/bsp-notes/dtscmp/dtscmp.py`.

## 5. Milestones

1. **Baseline BSP build** (this task): dispatch
   `.github/workflows/bsp-tr6560-build.yml` and get `bin/targets/tr6560/generic/`
   images out. Proves the toolchain + feeds + config path end to end.
2. **Board layer**: add our device tree / target glue under `lab/cudy-wr3000v2/`
   (mirroring the BSP tree layout, so the workflow's overlay picks it up) and add a
   `Device/cudy-wr3000v2` to the tr6560 image Makefile carrying the section-3 Cudy
   layout.
3. **Kernel delta**: port the `hsan,` clock/reset/pcie/fmc bindings onto the BSP's
   5.10 tree (the `lab/luofu-*` lanes are the in-flight host-driver work).
4. **Image**: produce `sysupgrade.bin`/`fullimage.bin` for the Cudy slot layout;
   validate against `docs/FLASH-PLAN.md` before any flash write.

## 6. Guardrails

- Every flash write follows `docs/FLASH-PLAN.md`: full dumps first, one slot always
  stock, write only the inactive slot, verify the written hash before switching.
- No BSP image is flashed to the device until its layout is matched to section 3.
- Board material lands under `lab/cudy-wr3000v2/` only; the docs stay out of the
  overlay (`CUDY.md` / `README.md` are excluded by the workflow's `rsync` step).
