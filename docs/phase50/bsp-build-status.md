# PHASE 50 - BSP build status: the Triductor TR6560 BSP as a firmware source for the Cudy WR3000 v2.0

Task `st_01a121ee`, 2026-10-09. Host-only: the device was not touched, no build was run, nothing was
committed. The BSP clone is local (`build/tmp/bsp`, git `daeb1eab`, 1.2 GB, `dl/` 441 MB / 204 files,
no `bin/`, `staging_dir/`, `build_dir/` or `tmp/` - it has **never been built**).

> **HEADLINE.** The BSP is a *half-source* vendor release with a **working OpenWrt 22.03 build
> skeleton and a fully pinned `tr6560` target (kernel 5.10.138, arm_cortex-a9 + musl, a committed
> 284,002-byte `.config`), but its device-specific half is shipped as binaries**: the SoC platform
> drivers are 21 prebuilt `.ko` (vermagic `5.10.138 SMP preempt mod_unload ARMv7`, no modversions,
> in `generic/base-files/lib/modules/5.10.138/`), the NAND/FMC controller is 10 prebuilt `.o` (no
> `.c` anywhere), the boot chain is a 69,625-byte bootram blob plus a 26,652-byte first-stage blob
> with **no U-Boot source at all**, and the Wi-Fi drivers are prebuilt blobs for *other* chips
> (tr5120/tr5220) than ours (Hi5622V100). Real source exists for exactly two files in `files-5.10`:
> `arch/arm/mach-triductor/platsmp.c` (SMP, `CPU_METHOD_OF_DECLARE(tri_smp_tr6560,
> "tri,tr6560-cpu-method")`) and `drivers/net/triductor/triams_skb_extend.c`. Everything the BSP
> binds is `tri,*`; our board's device tree - and our own stock firmware's modules - are `hsan,*`
> (our rootfs modules are `hi_*`), so the prebuilt BSP driver set **cannot attach to our DT as it
> stands**: of the SoC-identity nodes the two trees share the same MMIO addresses almost everywhere
> (GIC `0x10181000`/`0x10180100`, SCU `0x10180000`, L2 `0x7f000000`, TWD `0x10180600`, SP804
> `0x10104000`, UART0/1 `0x1010e000`/`0x1010f000`, FMC `0x10a20000`, PCIe DBI `0x10160000`/
> `0x10164000`, sysctrl `0x10100000`, CRG `0x14880000`, iomux `0x14900000`, flashinfo reserve
> `0x80600000`), and the compatible strings never do, so the port is a **binding/board-layer job,
> not a memory-map job** - but on the BSP's binary driver set that job is not possible at all (the
> blobs are hard-wired to `tri,*`), which is why the honest nearest-term deliverable from this lane
> is a *baseline* BSP image for the Banana Pi reference board, not for the WR3000.

---

## 1. STATUS of the BSP target components (source / objects / nothing)

Target root: `build/tmp/bsp/target/linux/tr6560/` (`Makefile`, `files-5.10/`, `patches-5.10/`,
`generic/`, `base-files/`, `image/`).

| Component | Verdict | Exact path checked | Counts / detail |
| --- | --- | --- | --- |
| Target/build skeleton | **real SOURCE** | `target/linux/tr6560/Makefile` (964 B), `image/Makefile` (4,122 B), `generic/target.mk` (309 B), `generic/config-5.10` (14,329 B) | `BOARD:=tr6560`, `ARCH:=arm`, `SUBTARGETS:=generic virgo`, `KERNEL_PATCHVER:=5.10`, `KERNELNAME:=bzImage`; `generic/target.mk` adds `CPU_TYPE:=cortex-a9`, `FEATURES+=nand`, `DEFAULT_PACKAGES += ubifs` |
| Mach / SMP | **real SOURCE (1 .c)** | `files-5.10/arch/arm/mach-triductor/` | 3 files: `platsmp.c`, `Makefile` (`obj-$(CONFIG_SMP) += platsmp.o`), `Kconfig` (`ARCH_TRIDUCTOR`, `MACH_TR6560`, `TRI_LSW`); watchdog.c is commented out. `platsmp.c` ends `CPU_METHOD_OF_DECLARE(tri_smp_tr6560, "tri,tr6560-cpu-method", &tri_smp_ops)` and looks up `of_find_compatible_node(NULL,NULL,"tri,crg")` |
| Board DTS | **real SOURCE (3 .dts/.dtsi)** | `files-5.10/arch/arm/boot/dts/` | `triductor-tr6560.dtsi` 7,573 B / `THG6500-TAX2.dts` 3,304 B / `THG6400-TAC2.dts` 2,999 B |
| Kernel delta | **real SOURCE (15 patches, 24 upstream files)** | `patches-5.10/` | 0001 printk.c; 0002 arch/arm/Kconfig; 0003 arch/arm/Makefile; 0004 compressed/head.S + Kconfig; 0005 kernel/bios32.c (`EXPORT_SYMBOL(pci_common_init_dev)`) + kernel/kallsyms.c (`EXPORT_SYMBOL_GPL(kallsyms_lookup_name)`); **0006 "tri_sdk" = 12 files** (asm/io.h, arch/arm/Kconfig, drivers/net/Makefile, `drivers/net/ethernet/marvell/sky2.c`, `net/ppp/ppp_generic.c`, include/linux/skbuff.h, net/bridge/br_input.c, net/core/skbuff.c, net/ipv4/ip_output.c, net/ipv6/ip6_input.c, net/ipv6/ip6_output.c, nf_conntrack_standalone.c); 0007+0008 mtd Makefile/Kconfig; 0009 ubifs budget.c; 0010 mkcompile_h; 0011 ofpart_core.c+parsers/Makefile; 0012 gpiolib/leds (6 files); 0013 ppp; 0014 skbuff.c; 0015 ubi/build.c + init/do_mounts.c |
| Vendor headers | **real SOURCE (32 headers)** | `files-5.10/include/tr6560/**` | 32 files under `include/tr6560` (33 `.h` in `files-5.10` overall): `kbasic/` (securec.h, tri_os.h, tri_symbol.h, ...), `hal/`, `chip/level_2/ecs/tri_eth.h`, `cfe/`, `net/triams_exskbuff.h`, `drivers/phy/tri_phy.h` |
| NAND / FMC controller | **objects only - NO source** | `files-5.10/drivers/mtd/` | 10 prebuilt `.o`: `triductor/tri_fmc.o` (30,592 B), `tri_mtd_parts.o`, `nfc_tr6560/{tri_nand.o 36,036, tri_nand_mtd.o 38,652, tri_nand_drv_yyxxxx.o 29,844, tri_spi_nand_drv.o 28,888, tri_nand_check.o 13,364, tri_hal_nand.o 13,892, tri_nand_bbt.o 6,740}`, `parsers/ofpart_triductor.o` (6,084 B). All `ELF 32-bit LSB relocatable, ARM, EABI5, with debug_info, not stripped`; zero `.c`, zero `.ko`. Driven by patches 0007/0008 (`obj-$(CONFIG_MTD_TRIDUCTOR_NAND) += triductor/`) with `generic/config-5.10:306 CONFIG_MTD_TRIDUCTOR_NAND=y`. The register spec was already recovered in `opensource/docs/phase50/nand-fmc-port-spec.md` (from these same `.o`, DWARF + `hi_flash.ko` + the U-Boot stage-2 blob) - not duplicated here |
| Ethernet / LSW | **NO native driver source; patched mainline + prebuilt .ko** | `files-5.10/drivers/net/triductor/` (1 `.c`: `triams_skb_extend.c`), `patches-5.10/0006` | The Ethernet/Wi-Fi data path is *mainline `sky2.c` + `ppp_generic.c` + `skbuff.c` patched with `CONFIG_ARCH_TRIDUCTOR` hooks* (`tri_wifi_dev_recv_reg`, `chip_sky2_xmit_frame`, `of_find_compatible_node(NULL,NULL,"tri,pcie")` gate) over the vendor headers; the switch/MAC/PHY runtime is the prebuilt `tri_kport.ko`, `tri_kphy.ko`, `tri_kphy_ext.ko`, `tri_sdk_l2.ko`. There is **no** driver for our `hsan,dp` / `hsan,pfe` / `hsan,mac` / `hsan,mdio` |
| PCIe | **objects/blobs only** | `generic/base-files/lib/modules/5.10.138/tri_pcie.ko` (17,904 B) | No `.c` anywhere for a tri PCIe host controller. Kernel-side hooks only: patch 0005 exports + patch 0006 `IO_SPACE_LIMIT 0xffffffff`. `tri_pcie.ko depends=tri_basic` |
| Wi-Fi | **blobs only (other chips)** | `package/triductor/tr5120/files/ko/` (10), `tr5220/files/ko/` (3) | tr5120: alg, dmac, frw, hal, hmac, oal, oam, plat, sdt, wal `.ko` + userspace `bin/{app_acs,app_nlc,app_sdt,create_ifname.sh,extpriv.sh}` + ini; tr5220: `peanut_plat.ko`, `peanut_wifi.ko`, `wifi_debug.ko`. **Our board's radio is Hi5622V100** (`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/hi5622v100_wifi.ko` 3,581,748 B + `hi5622v100_plat.ko` 371,708 B + `/lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini`) - same HCC family, different product |
| U-Boot / boot chain | **NOTHING but blobs** | `package/triductor/triboot/src/` + `package/triductor/start-boot_6560/`, `package/boot/*` | `TR6560-bootimage_nodtb.bin` 69,625 B (sha256 `db66b75f8e392f628c025d817994b235eeb8fd302748a1dd93e7b3cc9628d4d4`, byte-identical to the analysed copy in `build/tmp/bsp-notes/bootblob/bootram.bin`), `TR6560-startbootimage.bin` 26,652 B, `src/tools/gen_boot.sh` (pads bootram to 128 K with 0xFF, appends `boot.dtb`). `package/boot/` holds 32 upstream bootloader packages - **none** for tr6560/triductor |
| Image format | **real SOURCE** | `target/linux/tr6560/image/Makefile`, `scripts/triductor/ubinize-image-rootfs-data.sh`, `tools/triductor-image/{Makefile,addecc.c,gen_ecc.sh}` | `KERNEL := kernel-bin \| append-dtb \| uImage none`; `KERNEL_LOADADDR 0x80608000`; BOOT 384 K / KERNEL 8192 K / KERNEL_BOOT 8576 K / ROOTFS 40960 K / KERNEL_ROOTFS_BOOT 41344 K / FLASH_SIZE 131072; `UBINIZE_OPTS -s 2048 -e 1 -E 5`, `BLOCKSIZE 128k`, `PAGESIZE 2048`, `VID_HDR_OFFSET 2048`, `ROOTFS_DATA_TRI_SIZE 2048K`, `MKUBIFS_OPTS -m 2048 -e 124KiB -c 4096`; images `sysupgrade.bin` (= kernel + pad + ubi + `"fullimage=0"` + metadata) and `fullimage.bin` (= boot blob + pad + kernel + pad + ubi + `"fullimage=1"`), plus `BurningImage.bin` when `BURNING=1` (addecc/ECC NAND burn image via the `triductor-image` **host tool**, which does install `gen_ecc.sh`+`addecc` into `staging_dir/host/bin`) |
| Platform driver set (clk/reset/gpio/pinctrl/pie/tvsensor/usb/dfx) | **prebuilt .ko only** | `target/linux/tr6560/generic/base-files/lib/modules/5.10.138/` | 21 modules: `tri_basic.ko`, `tri_sdk_l0/l1/l2.ko`, `tri_sdk_hal.ko`, `tri_kphy.ko`, `tri_kphy_ext.ko`, `tri_kport.ko`, `tri_kspi.ko`, `tri_kcfe_{mc,res,srv_diagnose,wifi}.ko`, `tri_cpufreq.ko`, `tri_ktsensor.ko`, `tri_dfx.ko`, `tri_pcie.ko`, `tri_usb.ko`, `tri_wdg.ko`, `kgpio-tri6560.ko`, `mdb-notify-multi.ko`; loaded by `etc/modules.d/05-tri_bsp` (14 lines) + `09-kgpio_adapter` + `99-igmp_mdb_notify`; `strings` shows they bind **only `tri,*`** (`tri_pcie.ko`: tri,board/tri,crg/tri,gpio0/tri,iomux/tri,pcie/tri,sysctrl; `tri_sdk_l2.ko`: tri,board/tri,gpio0/tri,pie/tri,tvsensor; `tri_wdg.ko`: tri,crg) |
| Vendor packages named by the target | **~8 names have NO Makefile in the tree** | `package/**`, `feeds/**` | `grep -rl 'PKG_NAME:=<n>\|define Package/<n>' package feeds` finds **nothing** for `tri_bsp, cs_cli, dmc, easycwmp, kmod-kgpio_adapter, kmod-igmp_mdb_notify, prplmesh, meshinfo` - yet `.config` carries `CONFIG_DEFAULT_<n>=y` for all 8 (67 `CONFIG_DEFAULT_*` lines). Their payloads exist only as blobs (`cs_cli` is an ARM/musl ELF in `generic/base-files/bin/`, `kgpio-tri6560.ko`/`mdb-notify-multi.ko` are the prebuilt `.ko`). Present packages: `package/triductor/{triboot,start-boot_6560,tr5120,tr5220,trinft-qos,triqos-scripts,firewall4br,luci-app-triqos,luci-app-rate-limit,ambiorix/**}` plus the four feeds |

---

## 2. BUILDBILITY: what `make menuconfig` / `make` would actually do for `tr6560`

Read: `build/tmp/bsp/README.md`, `build/tmp/bsp/Makefile`, `feeds.conf.default`, `version`,
`target/linux/tr6560/{Makefile,generic/target.mk,generic/config-5.10,image/Makefile}`,
`include/{target.mk,kernel.mk,scan.mk,toplevel.mk}`, `include/kernel-5.10`,
`opensource/.github/workflows/bsp-tr6560-build.yml`, `opensource/lab/cudy-wr3000v2/{README.md,CUDY.md}`.

* **Tree identity**: OpenWrt 22.03-era, `version` = `r19685-512e76967f`; `README.md` is the stock
  OpenWrt README with a THG6500-TAX2 title line ("compile the BSP on ubuntu-22.04 host"), i.e. a
  fork of upstream 22.03 with `target/linux/tr6560` grafted in.
* **Selection**: the BSP commits a full `.config` at the tree root
  (`CONFIG_TARGET_tr6560=y`, `CONFIG_TARGET_tr6560_generic=y`,
  `CONFIG_TARGET_tr6560_generic_DEVICE_THG6500-TAX2=y`, `CONFIG_TARGET_ARCH_PACKAGES="arm_cortex-a9"`,
  `CONFIG_CPU_TYPE="cortex-a9"`, `CONFIG_LINUX_5_10=y`, `CONFIG_ARCH="arm"`,
  `CONFIG_TARGET_ROOTFS_SQUASHFS=y`). `make menuconfig` is therefore optional: `make` consumes
  `.config`; regenerating it (`config`/`oldconfig`/`defconfig`) is the only thing that would rewrite
  it. Menu path: *Target System -> triductor TR6560*, *Subtarget -> generic*, *Target Profile ->
  THG6500-TAX2*.
* **Kernel**: `KERNEL_PATCHVER:=5.10` and `include/kernel-5.10` pins
  `LINUX_VERSION-5.10 = .138` with `LINUX_KERNEL_HASH-5.10.138 = 29a003bb…`, so the build downloads
  **linux-5.10.138** exactly (it is *not* in `dl/`; `dl/` carries no `linux-5.10.*` tarball) and
  applies `target/linux/generic/{pending,hack,backport}-5.10` + these 15 patches. Note this is
  **5.10.138**, while our device's own vendor kernel is **5.10.201** (`tmp_manifest.txt` is a
  manifest of `/lib/modules/5.10.201/*.ko`).
* **Toolchain**: OpenWrt builds its own - `dl/` already holds `binutils-2.37.tar.xz` (22.9 MB),
  `gcc-11.2.0.tar.xz` (80.9 MB), `musl-1.2.3.tar.gz`, `gmp-6.2.1`, `mpfr-4.1.0`, `m4-1.4.19`,
  `zlib-1.2.11`. Host needs the SDK manual's package list (the CI installs
  `build-essential libncurses-dev unzip bzip2 gawk file python3 rsync subversion wget gettext git
  automake libc6-dev-i386 lib32stdc++6` after `dpkg --add-architecture i386`). No external
  cross-toolchain and no `CONFIG_EXTERNAL_TOOLCHAIN` are involved.
* **Feeds**: `feeds.conf.default` pins four feeds to *commit hashes* (`packages^426ccd2e`,
  `luci^487e58a0`, `routing^88723590`, `telephony^1d2031a5`) - `./scripts/feeds update -a` needs
  network (git.openwrt.org) and `./scripts/feeds install -a` creates `package/feeds/*`. In the local
  clone **both have already been done**: `feeds/{luci,packages,routing,telephony}.index` exist and
  `package/feeds/{luci,packages,routing,telephony}` symlink dirs exist (4).
* **What still must be downloaded**: `linux-5.10.138`, the selected package sources (~204 files /
  441 MB are pre-seeded; a full 22.03 userland with luci + wpad + `prplmesh`-class packages
  typically pulls another ~0.5-1.5 GB), plus the feeds git objects on a fresh clone.
* **Subtarget quirk**: `SUBTARGETS:=generic virgo` but only `generic/` exists (no `virgo/`
  directory, no `target/linux/tr6560/config-5.10` at target level - the kernel config comes from
  `generic/config-5.10`). `include/target.mk:72` derives the *effective* subtarget from
  `$(wildcard $(PLATFORM_DIR)/*/target.mk)`, so building works, but the target-metadata DUMP walks
  `$(SUBTARGETS)` (`include/target.mk:350`, `SUBMake -C image SUBTARGET=$(SUBTARGET)`) and will
  emit a `virgo` sub-target with no devices. Expected harmless; not executed (see section 5).
* **Patches that must be in place before a board swap**: none *structurally* - all 15 patches apply
  to the vanilla 5.10.138 tree and are board-independent except that they *create* the `tri,*`
  binding surface the blobs need. Swapping in our board's `hsan,*` DTS is a files-only change
  (`files-5.10/arch/arm/boot/dts/`, plus a `Device/…` stanza and `TARGET_DEVICES` in
  `image/Makefile`) - and that is exactly the change that breaks the prebuilt drivers (section 3/5).
* **CI lane that can host it**: `opensource/.github/workflows/bsp-tr6560-build.yml` - `workflow_dispatch`
  only (no push/PR triggers), `ubuntu-22.04`, `timeout-minutes: 360`, 9 steps: deps -> `git clone
  --depth 1` the BSP -> `rsync -a --exclude=README.md --exclude=CUDY.md lab/cudy-wr3000v2/ bsp/` ->
  `feeds update -a` -> `feeds install -a` -> `test -f .config && make -j$(nproc)` -> upload
  `bsp/bin/targets`. Its overlay contract is already wired: board material mirrors the BSP tree
  layout under `opensource/lab/cudy-wr3000v2/`, which today contains **only** `CUDY.md` +
  `README.md`, so a dispatch right now builds an **unmodified** THG6500-TAX2 baseline. The sibling
  lane `luofu-kernel-config.yml` builds vanilla 5.10.201 + `mach-luofu` and is **not** a BSP host.
* **Honest cost**: local (16-core desktop, Ubuntu/WSL): first build ≈ **45-70 min** wall
  (toolchain ~20 min + kernel/packages ~30-50 min), ≈ **6-9 GB** of `build_dir`+`staging_dir`+`bin`
  on top of the 1.2 GB clone; GitHub `ubuntu-22.04` (4 vCPU): ≈ **2.5-4 h** (the workflow caps at 6 h).
  All second builds are much faster (toolchain cached in `staging_dir`).

---

## 3. GAP ANALYSIS: BSP `triductor-tr6560.dtsi` + `THG6500-TAX2.dts` vs our `luofu-r116-pinned.dts`

Method: both files parsed with a brace-stack parser; nodes compared by `compatible` string and by
register address/size. 53 BSP nodes (with `compatible`/`reg`) vs 141 ours. Buckets below are
grouped by *function* (the address-set view alone double-counts multi-window `reg` properties).

### (a) Nodes that MATCH (same binding family and same address)

| function | BSP | ours | addresses |
| --- | --- | --- | --- |
| CPUs | `cpu@0`, `cpu@1` `arm,cortex-a9` | same | reg 0x0 / 0x1 |
| GIC | `interrupt-controller` `arm,cortex-a9-gic` | same | 0x10181000 0x1000 + 0x10180100 0x100 |
| SCU | `scu@0x10180000` `arm,cortex-a9-scu` | `scu@0x10180000` | 0x10180000 0x1000 |
| L2 | `L2: l2-cache` `arm,pl310-cache` | `l2-cache` | 0x7f000000 0x1000 |
| TWD | `local_timer@10180600` | same | 0x10180600 0x20 |
| SP804 | `timer0: timer@10104000` | `timer@10104000` | 0x10104000 0x1000 |
| UART | `uart@0x1010e000`, `uart@0x1010f000` `snps,dw-apb-uart` | `uart0@0x1010e000`, `uart1@0x1010f000` | both 0x1000 |
| PHYs (binding only) | 5x `ethernet-phy-ieee802.3-c22` reg 1..5 | 5x same, reg 1..5 | mdio parent differs (below) |
| flash-info reserve | `flashinfo_reserved` `tri,flashinfo_reserved` reg `0x80600000 0x4000` | `flashinfo@0x80800000` `hsan,flashinfo_reserved` reg `0x80600000 0x4000` | **same window** (the BSP node *label* says `0x80c00000`, its `reg` says `0x80600000` - vendor label drift) |
| partitions container | `fixed-partitions` | `fixed-partitions` | contents differ, see (b) |

### (b) Nodes that DIFFER - same function, different binding and/or address/size

| function | BSP | ours | the difference |
| --- | --- | --- | --- |
| **FMC / SPI-NAND** | `fmc@10a20000` `compatible = "tri,fmc"`, `bus_id="fmc"`, `spi_cs=<0x1>`, `reg = <0x10a20000 0x1000>, <0x1c000000 0x100000>` | `fmc@10a20000` `hsan,fmc` + `clocks=<sfc_clk>`, `reset-names="sfc_rst"`, `reg = <0x10a20000 0x1000 0x1c000000 0x100000>` | **addresses identical**, compatibles differ (`tri,fmc` vs `hsan,fmc`), and ours carries clock/reset handles the BSP node has no model for |
| partitions | 11 nodes: `uboot`(0x0,0x80000), `kernelA`(0x80000,0x800000), `rootfsA`(0x880000,0x2000000), `firmwareA`(0x80000,0x2800000), `kernelB`(0x2880000,…), `rootfsB`(0x3080000,…), `firmwareB`(0x2880000,…), `rootfs_data`(0x5080000,0x2e00000), `equip`(0x7e80000), `wlanrf`(0x7f00000), `upgflag`(0x7f80000) | 17 nodes: `esbc`(0x0,0x40000), `uboota`(0x40000,0x100000), `ubootb`(0x140000,…), `enva`, `envb`, `fac`(0x2c0000,0x200000), `bdinfo`(0x4c0000,0x40000), `cfga`(0x500000,0x200000), `cfgb`(0x700000,…), `log`(0x900000,0x440000), `pstore`(0xd40000,0x40000), `kernela`(0xd80000,0x840000), `kernelb`(0x15c0000,0x840000), `rootfsa`(0x1e00000,0x1740000), `rootfsb`(0x3540000,…), `rootfs_data`(0x4c80000,0x1600000), `upgrade`(0x6280000,0x1d80000) | **completely different flash map** (different boot-slot offset 0x0 size, kernel slot offset+size 0x840000 vs 0x800000, rootfs slot 0x1740000 vs 0x2000000, and 6 partitions only ours has) |
| **PCIe host** | ONE node `pcie@0x10145000` `tri,pcie`, `pcie_num=<0x2>`, `interrupts=<0 59 4>,<0 63 4>`, `iatu0`/`iatu1` tables, `reg = <0x10160000 0x1000>, <0x10164000 0x1000>, <0x10161000 0x3000>, <0x10165000 0x3000>, <0x40000000 0x4000000>, <0x58000000 0x4000000>, <0x41000000 0x2000000>, <0x59000000 0x2000000>, <0x50000000 0x4000>, <0x68000000 0x4000>` | TWO nodes `pcie@0x10160000` (`hsan,pcie`+`hsan,acp-pcie0`, `reg = <0x10160000 0x1000 0x10161000 0x3000 0x50000000 0x1000 0x40000000 0x2000000 0x48000000 0x800000>`) and `pcie@0x10164000` (`hsan,acp-pcie1`, `reg = <0x10164000 0x1000 0x10165000 0x3000 0x68000000 0x1000 0x58000000 0x2000000 0x60000000 0x800000>`) | same DBI/APB/cfg/mem bases, **different window sizes and IO base**: BSP MEM 0x40000000 **64 MB** vs ours **32 MB**; BSP IO at 0x41000000/0x59000000 size 0x2000000 vs ours at 0x48000000/0x60000000 size 0x800000; BSP cfg 0x4000 vs ours 0x1000; BSP one RC + outbound tables `iatu0/1` vs ours two RCs with `iatu_rc`/`iatu_ep`. (Both declare the RC `misc` window `0x10161000 0x3000` - the window the project's hard rule forbids *reading* on the live box; it is a DT fact in both trees, not a licence to touch it) |
| **GPIO** | ONE node `gpio0: gpio@0x10106000` `tri,gpio0`, `interrupts=<0 48 4>,<0 49 4>`, `reg = <0x10106000 0x2000>` (two banks in one 8 KB window) | TWO nodes `gpio0@0x10106000` and `gpio1@0x10107000`, both `snps,dw-apb-gpio` 0x1000 + `snps,dw-apb-gpio-port` children | ours uses the **mainline** gpio-dwapb binding (verified present in the device's own module manifest: `gpio-dwapb.ko`), BSP uses its own driver over a single 0x2000 window |
| Clocks | `crg@14880000` `tri,crg` with `resume/reboot/clr-wdg/set-wdg/switch-wdg` offsets + two `fixed-clock`s (`arm_timer_clk` 250 MHz, `clk_uart` 100 MHz) | `crg@14880000` `hsan,crg`, plus a separate `clk@14880000` `hsan,clk`+`simple-mfd` tree with **16 `hsan,clk-gate` + 2 `hsan,clk-mux` + 2 `hsan,clk-pll`**, `hsan,hsan-clk-corrector`, `fixed_clk0/1`, `fixed_factor_clk`, `opp_table0` (3 OPPs) | same base address; ours has a real gate/mux/pll model the BSP collapses, **including `sfc_clk` = `clk_gate@001400`** which the FMC node depends on |
| sysctrl / SMP | `sysctrl: system-controller@10100000` `tri,sysctrl`, `smp-offset = <0xc08>`, cpu `enable-method = "tri,tr6560-cpu-method"` | `system-controller@10100000` `hsan,sysctrl`+`syscon`, `smp-offset = <0xc00>`, `enable-method = "hisilicon,hsan_smp"` | same base; **different SMP offset and method name** (BSP's method has source: `platsmp.c`) |
| IOMUX / pinctrl | `iomux@14900000` `tri,iomux`, `jtag-sel-offset=<0x108>` | `iomux` `hsan,iomux` 0x14900000 0x1000 + `pinctrl@0x14900000` `hsan,luofu-peri-pinctrl` `reg = <0x14900100 0x3c 0x14940000 0x100>` with 5 pin-state groups | same base; ours splits mux+pad windows into a pinctrl provider |
| **Ethernet / switch** | `pie { compatible = "tri,pie" }` **with no `reg`**, holding `mdio-bus` (`tri,tr6560-mdio`, no reg) with phy1..phy5, and `halport0` (`tri,tr6560-halport`, no reg) whose `wan`/`lan1..lan4` ports are filled by the board file | `mdio0` `hsan,mdio` @0x14000000, five `gemac@0x14300000…0x14500000` `hsan,mac` (each 0x10000), `lsw_dp` `hsan,dp`, `lsw_pfe` `hsan,pfe`, `lsw_woe` `hsan,lsw_woe`, `woe_dev`, `woe_wifi` | **structurally different Ethernet architecture**: the BSP describes no MAC/switch MMIO at all (its data path is the patched `sky2` + prebuilt `*_kport/kphy/sdk_l2`), ours names the LSW/gemac register blocks explicitly |
| Memory | **no `memory` node**; `chosen bootargs = "noinitrd mem=110M@0x80000000 console=ttyS1,115200 mtdparts=10a20000.fmc:512K(boot),8M(kernela),16M(rootfsa)"` | `memory { reg = <0x80500000 0x7b00000> }` (123 MB); `bootargs = "noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2 additional_cpus=1"` | BSP relies on U-Boot-supplied `mem=` from 0x80000000 (110 M); ours declares 0x80500000 + 0x7b00000. Console naming differs (`ttyS1` vs `ttyS0`, same two UARTs) |
| UART dt props | `uart@0x1010e000` `bus_id="uart0"`, `tty_name="ttyAMA"`; `uart@0x1010f000` `bus_id="uart1"`, `tty_name="ttyS"`, `clocks = <0x2>` (phandle-numbered, non-standard `clk_uart` node with `linux,phandle = <0x2>`) | `uart0@…` / `uart1@…` with `clocks`/`clock-names = "apb_pclk"` and `clk_uart`/`apb_clk`/`ahb_clk` fixed-clocks | same addresses, ours uses proper `#clock-cells` providers |
| Board-level | BSP board adds `leds{gpio-leds}`, `keys{gpio-keys-polled}`, `board{compatible="tri,board"}` (usb_power, pcie_rstn/vol_en/wl_en/pmu_pwron, rgmii/extphy delays, i2c, jtag_sel), and 5 `halport0` ports | ours has **no** board node: board identity (LEDs `oem:green:*`, `gpio-keys-polled` reset/wps gpio 0x16/0x17, `board_id=3`, `model="hsan luofu/R116"`) lives in the **U-Boot DT overlay** `kernela_overlay_board3.dtbo`, not in the base blob | board layer must be re-materialised in the BSP tree |

### (c) Nodes OUR board has and the BSP does not (functional list)

`hsan,efuse` family (4 nodes: `chip_key`/`ns`/`online`/`sec_ctrl` with ~30 sub-registers), `gpio1@0x10107000`,
`i2c0@0x10111000` (`snps,designware-i2c`), `jent-rng` (`hsan,jent-rng`), `ledpwm@14900280` (`hsan,ledpwm`,
4 windows), `pwm@1` (`hsan,pmu-pwm`) + `reg@0` (`pwm-regulator`), `reset0` (`hsan,hsan-reset`), `rstinfo@0x14880000`
(`hsan,rstinfo`), `sysenv` (`hsan,sysenv`), `watchdog` (`hsan,hsan-watchdog`, reg 0x14880000 0x1000),
`tvsensor@14900500` (`hsan,tvsensor`, 0x14), `clk_corrector@0` + `corrector_fixed_clk`, `cooling_dev0`
(`hsan,net-cdev`) + `thermal-zones`/`trips` + `opp_table0`, `lsw_dp`/`lsw_pfe`/`lsw_woe`/`woe_dev`/`woe_wifi`,
`hsan,acp-pie` `pie@0x10a70000`, the 5 `gemac` MACs + `mdio0`, and the 17-partition table.
Also ours has `ethernet-phy@0..4` **node names with reg 1..5** (decompile artefact) where the BSP names
`phy1..phy5` with the matching reg.

### (d) Nodes the BSP has and our board lacks

`usb_phy@0x1016c000` (`tri,usb_phy`), `usb_ehci@0x10a40000` (`generic-ehci`), `usb_ohci@0x10a50000`
(`generic-ohci`), `spi@0x10114000` (`tri,spi`), `dmac@0x15200000` (`tri,dmac`, chan_num 16),
`lsw_napt_reserved` reserved-memory @0x80000000 + 0x600000 (`tri,lsw_napt_reserved`), and the board
nodes `leds`/`keys`/`board{tri,board}`, plus the `uboot`/`firmwareA|B`/`equip`/`wlanrf`/`upgflag`
partitions. (Our DT is the *superset* for the SoC; the BSP adds a USB block and a
`lsw_napt_reserved` carve-out ours does not declare.)

### Spotlights

* **`fmc@10a20000`** - the one peripheral where the two trees agree *exactly* on addresses
  (`0x10a20000 0x1000` + `0x1c000000 0x100000`) and differ only in binding (`tri,fmc` vs `hsan,fmc`)
  and in the partition table. The controller itself is the same IP: the phase-50 spec
  (`opensource/docs/phase50/nand-fmc-port-spec.md`) decodes it from these very `tri_*.o` and from our
  live box's `hi_flash.ko` ("HSAN FMC Controller Device Driver, Version 100"), i.e. `tri_fmc.o` and
  `hi_flash.ko` are two builds of one driver. Keeping the BSP's `tri,fmc` node and the BSP's NAND
  `.o` (and its 11-partition table) is the cheapest path on the BSP lane; our 17-partition table and
  `hsan,fmc` binding stay for the upstream lane.
* **PCIe / Wi-Fi** - both trees place the Wi-Fi behind the internal PCIe root complexes (ours:
  `hsan,pcie` domains 0/1; BSP: `tri,pcie` with `pcie_num=2`), and neither base DT describes a radio.
  The BSP attaches `tr5220-iFEM` (Wi-Fi 6) or `tr5120` (Wi-Fi 5) via the `board` node and the
  prebuilt blobs; our unit's endpoint is `Hi5622V100`. So the *transport* is the same shape, the
  *silicon and driver* are not.

---

## 4. FIRST-BUILD RECIPE (our board's DTS swapped in)

Nothing below was executed (see section 5). Steps 1-3 are the BSP's own documented path; step 4 is
the board drop-in the CI overlay is already wired for.

```bash
# --- 0. Locally the BSP already exists; do NOT re-clone:
#     build/tmp/bsp  (git daeb1eab, 1.2 GB, dl/ 441 MB, feeds installed, never built)
#     On a fresh host: git clone --depth 1 https://github.com/BPI-SINOVOIP/THG6500-TAX2-OPENWRT-BSP.git bsp

# --- 1. Host deps (Ubuntu 22.04; the SDK-manual list the CI uses)
sudo dpkg --add-architecture i386
sudo apt-get update
sudo apt-get install -y build-essential libncurses-dev unzip bzip2 gawk file python3 rsync \
  subversion wget gettext git automake libc6-dev-i386 lib32stdc++6

# --- 2. Feeds (already done in the local clone; needed on a fresh one)
cd bsp
./scripts/feeds update -a          # network: 4 pinned git feeds
./scripts/feeds install -a         # creates package/feeds/* symlinks

# --- 3. Toolchain config: the BSP commits its full .config at the tree root
#     (CONFIG_TARGET_tr6560_generic_DEVICE_THG6500-TAX2=y). Nothing to expand:
ls -l .config                      # 284002 bytes, must exist
#     Optional regen (menuconfig): Target System -> triductor TR6560 /
#     Subtarget -> generic / Target Profile -> THG6500-TAX2. Do NOT `make defconfig`
#     blindly: it drops the CONFIG_DEFAULT_* lines for the 8 withheld packages.

# --- 4. OUR BOARD DROP-IN (the only non-upstream step). Mirror the BSP tree under
#     opensource/lab/cudy-wr3000v2/ so the CI's `rsync -a ... bsp/` overlay picks it up:
#       4a. our device tree, RE-EXPRESSED in the BSP's binding namespace (see below):
#           lab/cudy-wr3000v2/target/linux/tr6560/files-5.10/arch/arm/boot/dts/cudy-wr3000v2.dts
#           (source material: opensource/docs/soc/luofu-r116-pinned.dts, or the decompiled
#            build/tmp/bsp-notes/dtscmp/cudy.dts; it must NOT be dropped in as-is - it is hsan,*)
#       4b. a device stanza + registration in the target's image Makefile:
#           define Device/cudy-wr3000v2
#             DEVICE_MODEL := cudy-wr3000v2
#             DEVICE_DTS := cudy-wr3000v2
#             DEVICE_PACKAGES :=            # tr5220-iFEM is the BPI Wi-Fi chip, not ours
#           endef
#           TARGET_DEVICES += cudy-wr3000v2        # inside the SUBTARGET=generic block
#       4c. a 17-partition A/B table matching our flash (kernela@0xd80000 0x840000,
#           rootfsa@0x1e00000 0x1740000, esbc@0x0 0x40000, ...) in place of the BSP's 11;
#       4d. a config fragment (e.g. lab/cudy-wr3000v2/cudy-wr3000v2.config) applied before make:
#             CONFIG_TARGET_tr6560_generic_DEVICE_cudy-wr3000v2=y
#             # CONFIG_TARGET_tr6560_generic_DEVICE_THG6500-TAX2 is not set
#           (applied as: cp .config .config.bak; cat fragment >> .config; make oldconfig)

# --- 5. Build
make -j"$(nproc)"                  # add V=s on the first failure

# --- 6. Artifacts
ls -la bin/targets/tr6560/generic/
#     sysupgrade.bin  fullimage.bin   (BurningImage.bin only when BURNING=1)
#     fullimage.bin = boot blob(384k) | kernel | pad | ubi rootfs | "fullimage=1" | metadata
```

* **CI host**: dispatch `bsp-tr6560-build.yml` (Actions tab; `workflow_dispatch` only). It clones the
  BSP, overlays `lab/cudy-wr3000v2/`, runs feeds + `make -j$(nproc)`, and uploads
  `bsp/bin/targets` -> `bsp-tr6560-images`. Two honest caveats: the workflow **has no run record we
  can see from here** (dispatch-only, nothing pushed it), and its first successful dispatch will
  prove the *baseline*, not the board swap.
* **Honest estimates** (not measured here): local 16-core first build 45-70 min, ~6-9 GB
  `build_dir`/`staging_dir`/`bin`; downloads ≈ `linux-5.10.138` (≈106 MB) + package sources
  (~0.5-1.5 GB) on top of the 441 MB already in `dl/`; GitHub 4-vCPU runner 2.5-4 h against a 6 h cap.
* **What I could NOT verify now**: that the .config's 8 missing `CONFIG_DEFAULT_*` packages do not
  hard-fail the metadata step; that a `virgo` subtarget with no directory is tolerated by
  `prepare-tmpinfo`; that the 21 prebuilt `5.10.138` `.ko` actually load against a kernel built from
  this committed `.config` (only vermagic and the absence of modversions are checked - symbol-set
  drift is a runtime question); that the BSP's tc/build system runs on this Windows host at all (it
  is not a supported build host - Linux required, case-sensitive FS); and anything about the device's
  boot acceptance of the produced image (section 5).

---

## 5. HONEST BLOCKERS (ranked; each with the observation that would settle it)

1. **The prebuilt platform driver set binds `tri,*` only; our board's DT is `hsan,*`.**
   All 21 vendored `.ko` were string-checked: `tri_pcie.ko` -> `tri,board/+crg/gpio0/iomux/pcie/sysctrl`,
   `tri_sdk_l2.ko` -> `tri,board/gpio0/pie/tvsensor`, `tri_wdg.ko` -> `tri,crg`; and `grep -rl hsan`
   over the whole tree (excluding `dl/`) finds **6** files - the four Wi-Fi blobs, one netdata patch
   and a wdk config, **no platform driver**. So dropping our `luofu-r116-pinned.dts` into the BSP is
   guaranteed to bind nothing the BSP ships.
   *Settles it*: build the baseline image, then `strings` the produced kernel `.ko`/`of_match_table`
   against a boot log on the bench (or simply confirm by inspection that no `hsan,`-matching blob
   exists - already done statically: the 6-file `grep`).
2. **The image/flash layout is our board's, not the BSP's, and the BSP's own layout would be wrong
   to flash.** BSP `uboot@0x0 0x80000`, `kernelA@0x80000 0x800000`, `rootfsA@0x880000 0x2000000`;
   ours `esbc@0x0 0x40000`, `kernela@0xd80000 0x840000`, `rootfsa@0x1e00000 0x1740000`. A BSP image
   written to our slots is misaligned; writing it into our `upgrade` region is not a supported path.
   *Settles it*: the two partition tables (both read; already settled statically) plus one
   `image/Makefile` `Device/cudy-wr3000v2` stanza whose `IMAGES` sizes equal 0x840000/0x1740000 and
   `docs/FLASH-PLAN.md`'s pre-flash checks.
3. **The device's boot path refuses anything that is not in the vendor container shape.** The
   phase-42 decode already established that U-Boot's pre-entry gate is a DTBO/overlay container
   check requiring magic `0xd7b7ab1e` at `fdt_end` (report
   `opensource/docs/phase42-decode/report.md`, verdict `build/tmp/bsp-notes/verify/VERDICT.md`) -
   and the BSP's `fullimage.bin` is `bootblob | kernel(uImage) | ubi | "fullimage=1" | metadata`,
   which is *not* that container. So a straight BSP image is expected to be refused before kernel
   entry even if items 1 and 2 were fixed.
   *Settles it*: build once, then read the container/magic at the kernel tail of the produced
   `fullimage.bin` (and re-read `verify/VERDICT.md` + `bsp-notes/ubootguard/hunt.md` for the gate's
   exact offsets).
4. **The Wi-Fi silicon differs, so no booted BSP image gives our board Wi-Fi.** The BSP ships
   `tr5120`/`tr5220` blobs; our unit's radio is `Hi5622V100` (`rootfs-2.4.15/…/lib/hisilicon/ko/
   hi5622v100_wifi.ko` 3,581,748 B, `hi5622v100_plat.ko` 371,708 B, `lib/firmware/hi_wifi/
   cfg_hi5622v100_hisi.ini`). The sibling driver-diff lane already mapped them as one HCC family
   with different kernel identity (`vermagic 5.10.201` vs `5.10.138`; `build/tmp/bsp-notes/drivdiff/map.md`).
   *Settles it*: `strings`/`modinfo` diff of our `hi5622v100_*.ko` vs the BSP's `peanut_*.ko` and the
   endpoint's PCI IDs on the bench (map.md is the existing artifact).
5. **Our clock/reset/OPP model does not exist in the BSP tree** - so even a booting image has
   unmanaged clocks. Our DT has 16 `hsan,clk-gate` + 2 mux + 2 pll + `hsan,hsan-reset` + `opp_table0`;
   the BSP has two `fixed-clock`s and a bare `crg@14880000`. Notably our FMC node depends on
   `sfc_clk` (`clk_gate@001400`, `luofu-r116-pinned.dts:239`) and the BSP's `fmc@10a20000` has no
   clock handle at all: storage bring-up on our silicon is unproven on this lane.
   *Settles it*: the BSP kernel's clk-summary on the bench (or the phase-50 spec's clock section) -
   the question is whether the bootrom/U-Boot leaves `sfc_clk` open when `tri_fmc.o` probes.
6. **Build hygiene on the BSP's own tree (lower rank: expected, not proven fatal).** Eight
   `CONFIG_DEFAULT_*` packages named by the target Makefile have **no Makefile anywhere** in
   `package/`+`feeds/` (`tri_bsp, cs_cli, dmc, easycwmp, kmod-kgpio_adapter, kmod-igmp_mdb_notify,
   prplmesh, meshinfo`; their payloads exist only as prebuilt binaries/`.ko`), and `SUBTARGETS`
   declares `virgo` with no directory.
   *Settles it*: one `make -j1` run (or `make prepare-tmpinfo`) with `V=s` on a Linux host - the
   metadata step's warnings/errors close it in minutes without touching the device.

**Net:** the *baseline* BSP image (Banana Pi THG6500-TAX2) is plausibly reachable today in one CI
dispatch; a **bootable, current OpenWrt image for OUR board from this BSP** is not, and the reason is
not the build system - it is that the BSP's device half is binaries bound to `tri,*`, our board is
`hsan,*` with a different flash layout, a different radio and an unmodelled clock tree. The BSP's
real value is as a *reference and a source of decoded structure* (its NAND `.o` and boot blob are the
same objects our board's drivers derive from, per `phase50/nand-fmc-port-spec.md`), and as the
toolchain/skeleton host for the upstream port in `UPSTREAM-PORT-PLAN.md` - not as a drop-in firmware
source for the WR3000.

---

## Evidence

Everything below was run in this session, host-side only (`bash -lc` from
`C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2`; `G` = that path, `B` = `$G/build/tmp/bsp`,
`T` = `$B/target/linux/tr6560`). No device access, no `tools/exp.sh`, no ssh, no register writes, no
build, no commit.

### Commands (verbatim)

```bash
# tree / clone state
ls -la build/tmp/bsp ; du -sh build/tmp/bsp build ; ls -la build/tmp/bsp/target/linux/tr6560/
cd build/tmp/bsp && git log -1 --format='%H %ci %s' ; git remote -v ; git status --porcelain
ls -d $B/bin $B/staging_dir $B/build_dir $B/tmp          # all absent -> never built
du -sh $B/dl ; ls $B/dl | wc -l                          # 441M / 204 files

# source-vs-blob inventory
cd $T/files-5.10 && find . -type f | sort
cd $T/files-5.10 && echo "C: $(find . -name '*.c' | wc -l)  H: $(find . -name '*.h' | wc -l)  O: $(find . -name '*.o' | wc -l)  KO: $(find . -name '*.ko' | wc -l)  ALL: $(find . -type f | wc -l)"
cd $T/files-5.10 && file drivers/mtd/triductor/*.o drivers/mtd/triductor/nfc_tr6560/*.o drivers/mtd/parsers/*.o
cd $T/files-5.10 && ls -la drivers/mtd/triductor/tri_fmc.o drivers/mtd/triductor/nfc_tr6560/*.o
cat $T/files-5.10/drivers/mtd/triductor/Makefile $T/files-5.10/drivers/mtd/triductor/nfc_tr6560/Makefile $T/files-5.10/drivers/net/triductor/Makefile $T/files-5.10/arch/arm/mach-triductor/Makefile $T/files-5.10/arch/arm/mach-triductor/Kconfig
ls -la $T/files-5.10/arch/arm/boot/dts/ $T/patches-5.10/
cd $B && find . -path ./dl -prune -o -name '*.ko' -print | wc -l ; find . -path ./dl -prune -o -name '*.o' -print
find $T/files-5.10/include/tr6560 -type f | wc -l
cd $B/target/linux/tr6560/generic/base-files/lib/modules/5.10.138 && ls -la
for k in tri_basic.ko tri_pcie.ko tri_sdk_l0.ko; do printf "%s: " "$k"; strings -a "$k" | grep -m1 'vermagic='; done
for k in tri_pcie.ko tri_sdk_l2.ko tri_basic.ko tri_kport.ko tri_wdg.ko; do echo "=== $k"; strings -a "$k" | grep -iE '^(tri|hsan)[,.]' | sort -u; done
for k in tri_sdk_l0.ko tri_sdk_l1.ko tri_sdk_l2.ko tri_kport.ko tri_kphy.ko tri_pcie.ko; do printf '%-18s ' "$k"; strings -a "$k" | grep -m1 '^depends='; done
readelf -S tri_pcie.ko | head -30                     # no __versions section
cd $B && grep -rl 'hsan' --include='*' . | grep -v '^./dl/'   # 6 files, all Wi-Fi blobs / netdata patch
cd $B && cat $T/generic/base-files/etc/modules.d/* ; file $T/generic/base-files/bin/cs_cli

# patches
cd $T/patches-5.10 && for f in *.patch; do printf '%-52s' "$f"; grep -c '^+++ ' "$f"; grep '^+++ ' "$f" | sed 's|^+++ [ab]/||;s|\t.*||' | tr '\n' ' '; echo; done
cat $T/patches-5.10/0002*.patch $T/patches-5.10/0003*.patch $T/patches-5.10/0004*.patch $T/patches-5.10/0005*.patch $T/patches-5.10/0007*.patch $T/patches-5.10/0008*.patch $T/patches-5.10/0015*.patch
head -260 $T/patches-5.10/0006-tgp-support_tri_sdk.patch

# build machinery
cat $B/README.md $B/Makefile $B/feeds.conf.default $B/version $T/Makefile $T/image/Makefile $T/generic/target.mk
cd $B && grep -E '^CONFIG_(TARGET|ARCH|LINUX_5_10|CPU_TYPE|MODULE)' .config | head -80
cat $B/include/kernel-5.10 ; grep -nE 'SUBTARGET|config-\$\(KERNEL_PATCHVER\)|PLATFORM_SUBDIR' $B/include/target.mk
sed -n '1,80p' $B/include/scan.mk ; grep -n 'prepare-tmpinfo|menuconfig|defconfig' $B/include/toplevel.mk
cd $B && for n in tri_bsp cs_cli kmod-kgpio_adapter kmod-igmp_mdb_notify dmc easycwmp udhcpsnoop libamxb mod-amxb-ubus prplmesh meshinfo trinft-qos firewall4br; do printf '%-24s' "$n"; grep -rl -E "PKG_NAME:=[[:space:]]*$n\b|define Package/$n\b" package feeds | tr '\n' ' '; echo; done
cd $B && grep -nE '^CONFIG_DEFAULT_(tri_bsp|cs_cli|dmc|easycwmp|prplmesh|meshinfo|kmod-kgpio_adapter|kmod-igmp_mdb_notify)=y' .config
ls $B/package/feeds ; ls -d $B/package/feeds/* ; ls $B/feeds
ls $B/package/boot/ ; grep -rl 'tr6560\|triductor' $B/package/boot
ls -la $B/tools/triductor-image/ ; cat $B/tools/triductor-image/Makefile ; head -20 $B/tools/triductor-image/gen_ecc.sh
ls -la $B/scripts/triductor/ ; find $B -name 'gen_ecc*' -not -path '*/dl/*'

# boot chain / blobs
sha256sum $B/package/triductor/triboot/src/TR6560-bootimage_nodtb.bin $G/build/tmp/bsp-notes/bootblob/bootram.bin
ls -la $B/package/triductor/start-boot_6560/TR6560-startbootimage.bin ; cat $B/package/triductor/triboot/src/tools/gen_boot.sh
cat $B/package/triductor/triboot/Makefile ; head -40 $B/package/triductor/start-boot_6560/Makefile
find $B/package/triductor/tr5120 $B/package/triductor/tr5220 -type f

# our board's data
ls -la $G/opensource/docs/soc/
grep -nE 'compatible|reg[[:space:]]*=' $G/opensource/docs/soc/luofu-r116-pinned.dts     # (parser input, see below)
grep -n 'smp-offset\|sfc' $G/opensource/docs/soc/luofu-r116-pinned.dts
grep -c '' $G/build/tmp/bsp-notes/dtscmp/cudy.dts ; head -40 $G/build/tmp/bsp-notes/dtscmp/cudy.dts
ls $G/rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/ | grep -iE 'wifi|plat|pcie'
find $G/rootfs-2.4.15/squashfs-root -iname '*5622*'
grep -oE '[^ /]*\.ko' $G/tmp_manifest.txt | sort | head -80 ; head -3 $G/tmp_manifest.txt
sed -n '1,40p' $G/opensource/docs/phase50/nand-fmc-port-spec.md
grep -nE '^#{1,4} |BSP' $G/opensource/docs/UPSTREAM-PORT-PLAN.md | head -80 ; sed -n '46,100p' $G/opensource/docs/UPSTREAM-PORT-PLAN.md
ls $G/opensource/docs/phase49/ ; head -60 $G/opensource/docs/phase49/README.md
head -70 $G/CUSTOM-FIRMWARE-PLAN.md ; cat $G/opensource/lab/cudy-wr3000v2/README.md $G/opensource/lab/cudy-wr3000v2/CUDY.md
ls -la $G/opensource/.github/workflows/ ; cat $G/opensource/.github/workflows/bsp-tr6560-build.yml ; head -30 $G/opensource/.github/workflows/luofu-kernel-config.yml
find $G/opensource/lab/luofu-{clk,kernel,pcie,pinctrl,wifi} -type f
grep -n 'BSP' $G/mem-entries.md | tail -25
```

**DTS diff** - both trees parsed with a brace-stack parser (node name + `compatible` + `reg` +
`interrupts`, label prefixes handled), then compared by register address and by function:

```python
# inputs:
#   BSP: $T/files-5.10/arch/arm/boot/dts/triductor-tr6560.dtsi + THG6500-TAX2.dts
#   ours: $G/opensource/docs/soc/luofu-r116-pinned.dts
# output: 53 BSP nodes vs 141 our nodes; compatible census; address-set (a)/(b)/(c)/(d) buckets
# parsed node count / census / bucket tables are the tables in section 3.
```

### Files read (verbatim list)

* `build/tmp/bsp/README.md`, `Makefile`, `feeds.conf.default`, `version`, `.config` (284,002 B)
* `build/tmp/bsp/include/{target.mk, kernel.mk, scan.mk, toplevel.mk, kernel-5.10}`
* `build/tmp/bsp/target/linux/tr6560/{Makefile, image/Makefile, generic/target.mk, generic/config-5.10}`
* `build/tmp/bsp/target/linux/tr6560/files-5.10/**` (53 files: 2 `.c`, 33 `.h`, 10 `.o`, 5 Mk/Kconfig)
* `build/tmp/bsp/target/linux/tr6560/files-5.10/arch/arm/boot/dts/{THG6500-TAX2.dts, THG6400-TAC2.dts, triductor-tr6560.dtsi}`
* `build/tmp/bsp/target/linux/tr6560/patches-5.10/*.patch` (all 15 header lines + 0002/0003/0004/0005/0006/0007/0008/0015 in full)
* `build/tmp/bsp/target/linux/tr6560/generic/base-files/etc/modules.d/{05-tri_bsp, 09-kgpio_adapter, 99-igmp_mdb_notify}`
* `build/tmp/bsp/target/linux/tr6560/generic/base-files/lib/modules/5.10.138/*` (21 `.ko`, listed + strings)
* `build/tmp/bsp/package/triductor/{triboot/Makefile, triboot/src/tools/gen_boot.sh, start-boot_6560/Makefile}`
* `build/tmp/bsp/tools/triductor-image/{Makefile, gen_ecc.sh}`, `build/tmp/bsp/scripts/triductor/ubinize-image-rootfs-data.sh`
* `opensource/docs/soc/{luofu-r116-pinned.dts, luofu-r116.dts, vendor-dt-notes.md}`
* `opensource/docs/{UPSTREAM-PORT-PLAN.md, phase49/README.md, phase50/nand-fmc-port-spec.md}`
* `opensource/lab/cudy-wr3000v2/{README.md, CUDY.md}`, `opensource/.github/workflows/{bsp-tr6560-build.yml, luofu-kernel-config.yml}`
* `opensource/lab/luofu-{clk,kernel,pcie,pinctrl,wifi}/**` (file listing + `luofu-kernel/README.md`)
* `build/tmp/bsp-notes/bootblob/analysis.md`, `build/tmp/bsp-notes/dtscmp/cudy.dts`, `build/tmp/dt-spec/dt.md`
* `CUSTOM-FIRMWARE-PLAN.md`, `mem-entries.md` (BSP-related entries), `tmp_manifest.txt`
* `rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/**` (listing + `hi5622v100_*` sizes)

**Cross-referenced, not re-derived here**: `opensource/docs/phase50/nand-fmc-port-spec.md`
(NAND/FMC register spec), `build/tmp/bsp-notes/drivdiff/map.md` (Wi-Fi/platform `.ko` diff),
`build/tmp/bsp-notes/verify/VERDICT.md` + `opensource/docs/phase42-decode/report.md` (boot container
gate), `build/tmp/bsp-notes/dtscmp/delta.md` (prior DTS diff; this report's section 3 was produced
independently from the same two files and agrees).
