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
3. **`NEW` marks our symbols.** The 12 driver-less `CONFIG_*_LUOFU` symbols
   have no Kconfig entry yet - each needs one, with a prompt and the matching
   `select` of rule 1. Until then the CI lane reports exactly those as
   "requested but not in final `.config`", which is the to-do list.
   `CONFIG_ARCH_LUOFU` has left that list: it is a real platform now
   (`mach-luofu/` + `kernel-tree.patch`, see "The image lane") and the fast CI
   job asserts it like any other symbol.
4. **`PSTORE_BLK` / `MTD_PSTORE` are deliberately absent.** In 5.10.201
   `PSTORE_BLK` `depends on BROKEN`, and `BROKEN` is promptless with nothing
   selecting it (`init/Kconfig:116`) - pstore-on-MTD is unreachable in a vanilla
   tree, so a fragment cannot enable it. The vendor does (hence their
   `pstore_blk`/`mtdpstore` modules); the luofu port must `select BROKEN` from
   `MTD_NAND_LUOFU` or carry the vendor Kconfig patch. `PSTORE`,
   `PSTORE_CONSOLE` and `PSTORE_RAM` are set normally.
5. **Baked in, not modules.** The vendor carries many of these as `.ko`; a
   self-contained OpenWrt target wants them `=y` (stage2.md sec.3 note).

## The image lane: `mach-luofu/` + `kernel-tree.patch`

The first own-kernel image is the fragment **plus a machine plus a DTB**, so the
lane carries the two things the fragment cannot express:

| file | what it is | where it lands in the tree |
| --- | --- | --- |
| `mach-luofu/luofu.c` | the machine descriptor: `.dt_compat = "hisilicon,luofu-r116" / "hisilicon,luofu"`, no `.init_machine`, no register access; `.l2c_aux_val = 0x430001` / `.l2c_aux_mask = ~0` replay the vendor PL310 AUX value (pinned:1043) and are also what makes `init_IRQ` call `l2x0_of_init()` at all (`arch/arm/kernel/irq.c:88`) | `arch/arm/mach-luofu/` (copied) |
| `mach-luofu/Makefile` | `obj-y += luofu.o` | `arch/arm/mach-luofu/` (copied) |
| `kernel-tree.patch` | `config ARCH_LUOFU` inside `arch/arm/mach-hisi/Kconfig`'s `if ARCH_HISI` menu (the Hisilicon platform-type menu), `machine-$(CONFIG_ARCH_LUOFU) += luofu` in `arch/arm/Makefile`, `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb` in `arch/arm/boot/dts/Makefile` | 3 in-tree edits, `patch -p1` |
| (no file) | the devicetree itself: `docs/soc/luofu-r116.dts`, copied in rather than patched so it stays the single source of truth | `arch/arm/boot/dts/luofu-r116.dts` (copied) |

What this buys, and what it does not:

- **Without the machine** the boot still happens, but implicitly: with
  `CONFIG_ARCH_MULTIPLATFORM` the DT match falls through to the generic machine
  in `arch/arm/kernel/devtree.c:216`, whose `.l2c_aux_val = 0 / .l2c_aux_mask = ~0`
  (plus `arch/arm/kernel/irq.c:88`) reprograms the PL310 AUX register to 0 and
  warns. Matching our own compatible keeps the vendor's AUX value and ties the
  DT to the platform.
- **SMP is not delivered.** The DT's `enable-method` is the vendor's
  `hisilicon,hsan_smp` (pen/smc) and has no mainline `smp_ops`; expect a UP boot
  with a warning (see stage 1). `maxcpus=1` is redundant but harmless.
- **Node coverage is not boot coverage.** `hisilicon,luofu-*` nodes are
  `status = "disabled"` and/or have no driver in this tree, so the NAND/FMC,
  PCIe, Ethernet and Wi-Fi blocks stay dark; the console is the free
  `snps,dw-apb-uart` binding (`CONFIG_SERIAL_8250_DW`) and `earlycon` comes in
  through `SERIAL_8250_CONSOLE select SERIAL_EARLYCON` +
  `OF_EARLYCON_DECLARE(uart, "snps,dw-apb-uart", ...)` (`8250_early.c:182`).
- **`CLK_IGNORE_UNUSED` is not a `CONFIG_`.** 5.10.201 has no such symbol (it is
  a *clock flag*, `include/linux/clk-provider.h`, plus the `clk_ignore_unused`
  kernel command line in `drivers/clk/clk.c:1298`). It is moot here anyway: no
  CRG driver is in the tree, so no gate clock is registered and
  `clk_disable_unused()` has nothing of ours to turn off.

## Build / verify

- `opensource/.github/workflows/luofu-kernel-config.yml`, job **`config`**
  (every `omo/**` push, ~2 min): runs the kernel's own `merge_config.sh -m` +
  `olddefconfig` against the real 5.10.201 Kconfig and asserts every requested
  symbol survived. `CONFIG_ARCH_LUOFU` is asserted (it has a Kconfig entry now);
  the remaining driver-less `CONFIG_*_LUOFU` symbols are reported as the to-do
  list. `CONFIG_MTD_NAND_LUOFU` has left that list: `nand/luofu/Kconfig`
  defines it and both jobs wire it in, so it now resolves to `=y`.
  Green run of the pre-image version: **`37573150202`** on `c40637a` - 47/47
  non-`NEW` symbols resolved `=y`, the 13 `NEW` ones listed as the to-do (see
  `build/tmp/inta-spec/kcfg.md`).

### The FMC/NAND driver (`nand/luofu/`)

The flash controller at 0x10a20000 is what the rootfs lives behind, so the
lane carries its driver from here on. `nand/luofu/luofu-fmc.c` is **stage A**:
it maps the controller and its 1 MiB window, resets the die, reads the 5-byte
READ ID and reports each step through the sysctrl scratch pair 0x10100c18 /
0x10100c1c (the same registers the mach breadcrumbs use - this board has no
console). It compiles in **no write path at all** and registers no `mtd_info`,
so it cannot modify the flash. The register map, the command recipes and the
one place the BSP and the vendor disagree (which register the completion poll
reads) are documented at the top of that file; the full decoded spec is
`docs/phase50/nand-fmc-port-spec.md`. Stage B adds the page read and the MTD
integration.

Both jobs wire it in by copying the directory and **appending** two lines to
`drivers/mtd/nand/Kconfig` and `drivers/mtd/nand/Makefile`, rather than adding
patch hunks: an append has no upstream context to drift against.
- Job **`kernel`** (the image lane, ~25-40 min) runs either on a
  `workflow_dispatch` with `build_kernel: true` (dispatch from this branch works
  even though the file is not on the default branch yet - run `37579008899`
  proved it), or on a push whose head commit message carries the marker
  `[build-kernel]`, which keeps a 30-minute build out of every other push's way.
- Artifacts (flat, artifact name `luofu-kernel-image`): `zImage`,
  `luofu-r116.dtb`, `luofu-uImage` (mkimage, `-C none`, load=ep=`0x80608000`),
  `luofu-image.config` (the `.config`, renamed because `upload-artifact` skips
  dotfiles), `luofu-r116.ref.dtb` (the same DTS compiled separately by the
  distro dtc, as a second opinion), `luofu-config-delta.txt` (the base vs. the
  merged config), `luofu-slice.txt` (the `size(1)` audit + the kernelb
  arithmetic) and `MANIFEST.txt` (size + sha256 per file). The job pins
  `KBUILD_BUILD_TIMESTAMP`/`_USER`/`_HOST` (the commit date, `luofu-lane`,
  `github-actions`) because `scripts/mkcompile_h` otherwise embeds the build
  wall-clock, and it **fails** (exit 1) when the packed uImage exceeds the
  8,650,752-byte kernelb partition.

## The kernelb size budget (`luofu-shrink.fragment`)

The first image the lane built was 10,142,208 B of zImage / 10,142,272 B of
uImage - over the 8,650,752-byte `kernelb` partition by 1,502,327 B, which is
why the flash was aborted (`build/tmp/inta-spec/firstkboot.md`). The overflow is
the base config, not the partition: `multi_v7_defconfig` carries 30+ machines
and ~110 platform symbols for ~40 SoCs, while the vendor's own slice uses only
4,334,892 B of the same partition.

`luofu-shrink.fragment` is applied on top of `luofu.fragment` and switches off
only what it lists: every other SoC's mach/plat code, the non-luofu driver
surface (USB/MMC/DRM/FB/audio/media/input/hwmon/BT/SCSI/ATA/sensors), the
filesystems we do not mount, netfilter/tc, the other network drivers, the
on-chip peripheral drivers this DT has no binding for, and the optional
debug/perf/ftrace/EFI machinery - plus the size-oriented pair
`CC_OPTIMIZE_FOR_SIZE=y` and `BLK_DEV_INITRD=n` (no initramfs in the slice). It
also carries a small `=y` block of symbols the base only had because *other*
platforms selected them (`HAVE_ARM_ARCH_TIMER`, `VFP`, `NEON`, the Cortex-A9 and
PL310 errata, `TMPFS`, `STACKTRACE`, ...) - those would otherwise fall off
silently. The `kernel` job asserts both groups on every build: the load-bearing
list must stay `=y` (machine, GIC + architected timer, console, MTD/UBI +
squashfs/ubifs, pstore, PCIe, wifi, userspace basics) and the dropped families
must stay off.
- Download + hash a finished run with
  `gh run download <run-id> -R ShibbityShwab/cudy-wr3000v2-open-firmware -n luofu-kernel-image -D build/tmp/kboot`.
- The reasoning and the symbol-by-symbol classification live in
  `build/tmp/inta-spec/kcfg.md`; the image lane's own receipt (config deltas,
  artifact hashes, honest gaps) is `build/tmp/inta-spec/imgbuild.md`.
