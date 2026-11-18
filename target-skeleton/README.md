# Luofu / Cudy WR3000 v2.0 - OpenWrt target skeleton

> **STATUS: UNTESTED - SCHEMATIC - NOT A WORKING TARGET.**
>
> This directory contains a first-pass, reviewable **shape** of the OpenWrt target
> files described in [TARGET-DESIGN.md](../docs/TARGET-DESIGN.md) sections 4-6.
> It has been exercised against `openwrt main` (`27663a0`) at the target-metadata,
> kernel-prepare, kernel-config and kernel-compile stages: the draft patch series,
> including the 2026-11-18 early-boot `TEXT_OFFSET` fix (`003`), builds the full
> `linux-6.18.60` kernel with the tree's own OpenWrt GCC 14.4.0 toolchain and the real
> `0x80608000` load/entry address (2026-11-18 - see
> [SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md)). A first **FIP kernel slice** was then
> assembled from our own base DTB + loader-match overlay + uImage and structurally
> validated (2026-11-18, fourth session; `tools/build-kernel-slice.sh`). **It has never
> been booted; there is no rootfs, no sysupgrade container and no install image, and
> nothing here has ever run on a router.** The executable artifacts are
> `tools/fip-slice.py` (safe read-only paths: `check`, `check-trailer`) and
> `tools/build-kernel-slice.sh` (host-only, fail-closed, refuses vendor-derived inputs).
> On 2026-11-18 (fifth session) the first draft **driver series** was added: the board's
> CRG clock/reset controller (patch `004`) and watchdog (patch `005`) with their YAML
> bindings, device-tree clock-id header and DTS nodes. A sixth session added the network
> trio: an MDIO bus controller (`007`), the Genius PHY driver (`008`) and a **partial**
> GEMAC binding/resource driver (`009` - the TX/RX ring DMA data path is not implemented,
> so the GEMAC nodes stay disabled). The same session also added the board's **FMC
> SPI-NAND controller** (patch `006` - a SPI-MEM controller with the mainline SPI-NAND
> stack behind it, its YAML binding and the DTS conversion to a SPI bus + `spi-nand`
> child). A seventh session added the **machine-restart driver** (patch `010` -
> a restart handler over the SYSCTRL and CRG syscon nodes: boot-selector
> save, CRG software-reset command and a CRG watchdog fallback; the reset
> command is PROVISIONAL). Further sessions added pinctrl (`011`), thermal (`012`),
> the DWC PCIe host (`013`), the CRG reset-polarity fix (`014`) and the **PIE packet
> engine draft** (`015` - refuses to arm the engine until its register inventory is
> captured). A twelfth session added the **internal-switch D1a probe skeleton**
> (patch `016` - binding/resource/port-model validation only; the probe refuses to
> program the switch, the node stays disabled and the switch is modelled as
> switchdev over the GEMAC port netdevs, not DSA). All of them compile as built-ins
> into the same 6.18 kernel build (host-only) and have **never run on hardware**.
> The design these files
> encode is not accepted upstream: the rest of the driver series, image recipe and device
> page do not exist. **Do not read this directory as upstream progress.**
>
> **2026-11-18 (image-recipe session): the image chain is IMPLEMENTED and was
> built host-side.** `image/luofu.mk` now carries real `Build/` stages for the
> whole recipe (zImage -> legacy uImage with the derived load/entry -> generated
> FIP trailer -> validated kernel slice -> manifest -> squashfs in a static UBI
> volume -> sysupgrade container), `base-files/lib/upgrade/platform.sh`
> implements a structural `platform_check_image` and **refuses**
> `platform_do_upgrade` (A/B selector [U]), and `tools/luofu-image.py`
> builds/validates the rootfs UBI and the container. A full `make world` in an isolated `openwrt main` clone (commit `27663a0`, `linux-6.18.60`, patches `001`-`015`) then completed RC=0 and produced a 6,175,022-byte `...-squashfs-sysupgrade.bin` (sha256 `ea143b03...`) whose kernel slice, rootfs UBI and `fip-manifest.txt` were re-validated from the built bytes (see SKELETON-SMOKE.md, image-recipe session).
> Nothing has booted; there is still no install/factory path. Details and hashes:
> [SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (image-recipe session).
>
> **PUBLICATION: NOT READY.** The published `opensource/` half must not be cited as a
> submission-ready repository: ten vendor `.o` files remain in its history and the
> publish checklist is open ([PUBLISH-READINESS.md](../docs/PUBLISH-READINESS.md)).
> This skeleton contains **no** vendor binaries, firmware blobs, extracted DTBs, DTS
> dumps, calibration data or credentials. Values quoted or embedded here are
> structural facts documented in [TARGET-DESIGN.md](../docs/TARGET-DESIGN.md) or in
> the parent evidence index; the artifacts that carry them are local-only.

Everything here is marked one of:

| marker | meaning |
| --- | --- |
| **VERIFIED** | backed by a repo artifact or a dated ledger entry (citation given) |
| **SCHEMATIC** | file shape only; it does not build/run, and it is not a claim |
| **TODO** | implementation, binding or measurement that does not exist |
| **[U]** | unverified/open - a fact nobody has measured yet (TARGET-DESIGN.md legend) |

---

## 1. What is in here, exactly

| path | what it is | real OpenWrt tree location |
| --- | --- | --- |
| `README.md` | this file: status, facts, mapping, TODOs | nowhere (repo documentation) |
| `target/linux/hisilicon/Makefile` | target definition: `ARCH:=arm`, `BOARD:=hisilicon`, `SUBTARGETS:=luofu`, feature flags, kernel-version placeholder, default package sketch (**SCHEMATIC**) | `target/linux/hisilicon/Makefile` |
| `target/linux/hisilicon/Kconfig` | review stub: the kernel platform Kconfig bits the target needs (documented, not applied) plus two target build options (**SCHEMATIC**; OpenWrt targets normally have no such file - see the header) | `target/linux/hisilicon/` only after upstream review; the platform symbols belong in the kernel patch `arch/arm/mach-hisilicon/Kconfig` |
| `target/linux/hisilicon/luofu/target.mk` | subtarget: board name, `CPU_TYPE:=cortex-a9`, NAND feature, package sketch (**SCHEMATIC**) | `target/linux/hisilicon/luofu/target.mk` |
| `target/linux/hisilicon/patches-6.18/001-arm-mach-hisilicon-luofu.patch` | draft kernel machine support: new `arch/arm/mach-hisilicon/{Kconfig,Makefile,luofu.c}`; `ARCH_LUOFU` (under the existing upstream `ARCH_HISI`), clean UP-only DT machine descriptor for `hisilicon,luofu-r116`/`hisilicon,luofu`, documented TODO for the `cudy,wr3000-v2` compat, SMP, restart and the L2 AUX replay. **Applies cleanly in the 6.18 tree and the machine object compiles**; never upstreamed, never booted. | `target/linux/hisilicon/patches-<ver>/` |
| `target/linux/hisilicon/patches-6.18/002-arm-kconfig-textofs.patch` | draft core ARM integration: source the new mach Kconfig from `arch/arm/Kconfig`, `machine-$(CONFIG_ARCH_LUOFU) += hisilicon`, `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000` (load-address derivation, TARGET-DESIGN.md 3.2/5.2). Applies cleanly; compiles together with patch `003` below, which makes the non-immediate offset assemble. | same |
| `target/linux/hisilicon/patches-6.18/003-arm-textofs-nonimmediate.patch` | draft core ARM early-boot fix (2026-11-18): replaces the four `#TEXT_OFFSET` ARM immediates (`arch/arm/kernel/head.S` stext + `pgtbl` macro, `arch/arm/boot/compressed/head.S` two sites) with the kernel's portable `mov_l` constant macro, so the required `0x00608000` text offset assembles (ARM can only encode an 8-bit rotated immediate). Applies cleanly; **full kernel compile RC=0**; load/entry verified at `0x80608000` from the built artifacts. Not reviewed upstream. | same |
| `target/linux/hisilicon/patches-6.18/004-arm-clk-reset-luofu-crg.patch` | first real upstream-style driver (2026-11-18, fifth session): new `drivers/clk/hisilicon/crg-luofu.c` (16 gate clocks + 2-cell-offset reset controller over one **syscon regmap**; gate geometry is driver data, clock IDs are the DT ABI), `include/dt-bindings/clock/hisilicon,luofu-crg.h`, YAML `Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml`, Kconfig/Makefile (`CONFIG_LUOFU_CRG`). Applies cleanly to pristine 6.18.60; **host kernel compile RC=0, no warnings**; `dt_binding_check` pass; **never booted, never bench-run**. | `target/linux/hisilicon/patches-<ver>/` |
| `target/linux/hisilicon/patches-6.18/005-arm-watchdog-luofu-wdt.patch` | first real upstream-style driver (2026-11-18, fifth session): new `drivers/watchdog/luofu-wdt.c` (6.18 `devm` watchdog API; hwver-2 register values are driver data; child of the CRG syscon node, so **no second MMIO claim**; no restart handler/IRQ - documented TODOs), YAML `Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.yaml`, Kconfig/Makefile (`CONFIG_LUOFU_WDT`). Applies cleanly to pristine 6.18.60; **host kernel compile RC=0, no warnings**; `dt_binding_check` pass; **never booted, never bench-run**. | same |
| `target/linux/hisilicon/patches-6.18/006-arm-mtd-nand-luofu-fmc.patch` | FMC SPI-NAND controller (2026-11-18, sixth session): new `drivers/spi/spi-luofu-fmc.c`, a SPI-MEM controller for the 0x10a20000 FMC; the mainline SPI-NAND stack (`drivers/mtd/nand/spi`, ESMT `F50L1G41LC`) owns chip detection, on-die ECC, OOB layout, bad-block handling and the 17 `fixed-partitions`; YAML `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml`, Kconfig/Makefile (`CONFIG_LUOFU_FMC`) and the SPI/SPI-NAND config symbols. Applies cleanly to pristine 6.18.60; **host kernel compile RC=0, 0 warnings from the new files**; `dt_binding_check` pass; **never booted, never bench-run** - the FMC ECC, DMA-OOB and quad-enable semantics are unverified. NOTE: the ESMT F50L1G41LC chip entry is a v6.19 addition supplied here by OpenWrt's generic backport `434-...`. | `target/linux/hisilicon/patches-<ver>/` |
| `target/linux/hisilicon/patches-6.18/007-net-mdio-luofu.patch` | network trio 1/3 (2026-11-18, sixth session): new `drivers/net/mdio/mdio-luofu.c` (6.18 MDIO bus API; the frame/status/data register model and the polled handshake are the ones a previous out-of-tree 5.10 driver exercised on the lab board), Kconfig/Makefile (`CONFIG_LUOFU_MDIO` in `drivers/net/mdio/`), YAML `Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml`. Applies cleanly to pristine 6.18.60; **host kernel compile RC=0, no warnings**; `dt_binding_check` pass; **never booted, never bench-run**. | `target/linux/hisilicon/patches-<ver>/` |
| `target/linux/hisilicon/patches-6.18/008-net-phy-luofu.patch` | network trio 2/3: new `drivers/net/phy/luofu-phy.c` (PHYLIB driver for the Hisilicon "Genius" PHY, PHY ID `0x20669a00` / mask `0xffffff00`; generic clause 22 ops; the vendor per-PHY firmware patch is **not** loaded - documented TODO), Kconfig/Makefile (`CONFIG_LUOFU_PHY`). No YAML by design: PHY drivers match by PHY ID and the PHY children stay standard `ethernet-phy-ieee802.3-c22` nodes. **Host kernel compile RC=0, no warnings**; `checkpatch` clean; **never booted**. | same |
| `target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch` | network trio 3/3, **PARTIAL**: new `drivers/net/ethernet/hisilicon/luofu-gmac.c` (platform driver; claims the binding and validates reg/clocks/resets/phy-mode/phy-handle, then **refuses to probe** - the TX/RX ring DMA model is not implemented and has never been decoded, so no netdev is registered), Kconfig/Makefile (`CONFIG_LUOFU_GMAC`), YAML `Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml`. **Host kernel compile RC=0, no warnings**; `dt_binding_check` pass; the GEMAC DTS nodes stay `disabled`. **Not a data-path driver; never booted.** | same |
| `target/linux/hisilicon/patches-6.18/010-arm-luofu-restart.patch` | machine restart (2026-11-18, seventh session): new `drivers/power/reset/luofu-reboot.c` (restart handler over two syscon regmaps: saves the SYSCTRL A/B boot selector with a read-back write-same, issues the CRG software-reset command from the vendor `reboot-offset`/`softrst_val0/1` values and falls back to the CRG watchdog; probe touches no register), YAMLs `power/reset/hisilicon,luofu-reboot.yaml` and `soc/hisilicon/hisilicon,luofu-sysctrl.yaml`, Kconfig/Makefile (`CONFIG_LUOFU_REBOOT`). Applies cleanly to pristine 6.18.60; **host kernel compile RC=0, no warnings**; `dt_binding_check` pass; **the CRG reset command sequence is PROVISIONAL and has never run on hardware; never booted**. | `target/linux/hisilicon/patches-<ver>/` |
| `target/linux/hisilicon/luofu/config-5.10` | kernel-config skeleton derived from the only kernel the lab has evidence for (5.10.201) - **NOT the final upstream config**; lab symbol names marked. Superseded for 6.18 by the re-derived file below; kept as the 5.10 reference | `target/linux/hisilicon/luofu/config-<release>` after re-derivation |
| `target/linux/hisilicon/luofu/config-6.18` | **re-derived** 6.18 kernel-config delta (2026-11-18) emitted by the tree's canonical top-level `make kernel_oldconfig` with its own OpenWrt GCC 14.4.0 toolchain; provenance-marked (**[L]** lab value / **[P]** platform selection / **[N]** new driver symbols + their selects / unmarked oldconfig resolution); idempotent under a second run, and symbol-identical to a canonical re-derivation with the new driver Kconfig (fifth session, 2026-11-18). It enables `ARCH_HISI`+`ARCH_LUOFU`+`CONFIG_LUOFU_CRG/WDT/FMC`, the SPI/SPI-NAND stack (`CONFIG_SPI`, `CONFIG_SPI_MEM`, `CONFIG_MTD_SPI_NAND`), the network symbols `CONFIG_LUOFU_MDIO/PHY/GMAC` (GEMAC partial) and the pinctrl/thermal/PCIe/PIE/switch drafts (`CONFIG_LUOFU_PINCTRL`, `CONFIG_LUOFU_TVSENSOR`, `CONFIG_LUOFU_PCIE`, `CONFIG_LUOFU_PIE`, `CONFIG_LUOFU_SWITCH`); with patches `001`-`016` the **full kernel compiles** (RC=0) and nothing boots | same |
| `target/linux/hisilicon/image/Makefile` + `image/luofu.mk` + `image/luofu-ubinize.cfg` | board image Makefile (main idiom: `image.mk`, includes the subtarget file, `BuildImage`) plus the **real** target image chain: `kernel-bin | luofu-check-textofs | append-dtb | uImage none | luofu-fip-slice | luofu-fip-manifest`, then `luofu-rootfs-ubi | luofu-sysupgrade-container | append-metadata`. Every target-specific stage validates its own output and fails closed. **host-built 2026-11-18** (never booted) | `target/linux/hisilicon/image/` |
| `target/linux/hisilicon/luofu/profiles/cudy_wr3000-v2.mk.unused` | legacy profile sketch, kept only as history; main derives the device symbol from the image stanza | not copied (only if a targeted release uses profiles) |
| `target/linux/hisilicon/base-files/lib/upgrade/platform.sh` | sysupgrade hooks: `platform_check_image` re-derives the container structure from the received bytes (tar layout, `CONTROL`, uImage load/entry, FIP trailer/gate/descriptor constants, rootfs UBI volume shape, `fip-manifest.txt` cross-check, slot budgets) and returns 74 for a foreign/vendor image; `platform_do_upgrade` **REFUSES to write** because the A/B selector protocol and the install path are [U]. **`sh -n` clean; host-tested against the real built container and against corrupted/foreign variants; never run on hardware** | same path (board-level base-files) |
| `target/linux/hisilicon/base-files/etc/board.d/02_network` | board detection / network-defaults sketch at board level (main idiom); deliberately configures no interface because the switch has no driver (**SCHEMATIC**) | same path (mode 0755) |
| `target/linux/hisilicon/dts/luofu-r116.dts` | draft upstreamable DTS derived from the lab DTS; TODO-heavy. The CRG, watchdog and FMC nodes match their written bindings (CRG = one syscon system-controller node + watchdog child with clock IDs from the header; FMC = a SPI bus with one `spi-nand` child carrying the 17 fixed partitions and consuming the CRG `sfc_clk` gate and `sfc_rst` reset), the MDIO node matches patch `007` (seven CRG resets added) and the GEMAC nodes match patch `009` but stay `status = "disabled"` because the GEMAC driver has no data path; every other node is still a proposal (**UNTESTED**). The target `dts/` + `DEVICE_DTS_DIR := ../dts` mechanism is the chosen delivery, a kernel-patch copy is an alternative not to be hand-maintained in parallel | `target/linux/hisilicon/dts/luofu-r116.dts` |
| `dts/luofu-r116-overlay.dts` | minimal **loader-match stub** for our own base DTB: root `vendor_id="hsan"`, compatible `hsan-luofu`, `board_id=<3>`, one no-op fragment targeting `/`; mirrors the decoded overlay matcher and applies with libfdt. Not a vendor DTBO, no vendor bytes; it becomes a real board overlay only when there is a real board delta (and binding) to carry (**UNTESTED**, never booted) | `target/linux/hisilicon/dts/luofu-r116-overlay.dts` |
| `tools/fip-slice.py` | decoded trailer parser + fail-closed check/assemble/manifest + **`make-trailer`** (generates a structurally valid trailer from user-supplied base DTB + overlays; refuses to skip the gate/match/overlay checks; no vendor bytes embedded; one attended boot of a novel trailer still required) | `scripts/fip-slice.py` (or a target-local tools path) in the OpenWrt tree |
| `tools/luofu-image.py` | host assembly/validation helper for the image recipe: `rootfs-ubi` (squashfs -> one static UBI volume named `squashfs` with the measured geometry; re-parses EC/VID headers, their CRCs, the layout volume table, every static-LEB data CRC and the rootfs slot budget), `sysupgrade` (deterministic tar with `CONTROL`, `kernel`, `root`, `fip-manifest.txt`, `INSTALL-STATUS.txt`; cross-checks the slice against a manifest re-derived from the bytes) and `check-container` (independent read-back of a built container). Refuses vendor-derived input paths and unknown ubinize-cfg keys; never touches a device | `scripts/luofu-image.py` |
| `tools/build-kernel-slice.sh` | reproducible, fail-closed **host** script for the first own-artifact FIP kernel slice: cpp+dtc the base DTS and the match overlay, `mkimage` the 6.18 zImage, generate the trailer with `make-trailer`, assemble the slice, run `check`/`manifest`, and re-read the gate/descriptor/load address independently. Refuses missing inputs and any vendor-derived input path; never touches a device. It outputs a structurally validated slice that has **never been booted** | the standalone host reproduction that produced the first own-artifact slice (never booted); its steps are now also wired into `image/luofu.mk` |
| `../docs/TARGET-SKELETON.md` | one-page index/status of this skeleton, pointing back at TARGET-DESIGN.md | nowhere (repo documentation) |

Deliberately **absent**: a factory/install image stanza (the install path is an open
question, TARGET-DESIGN.md section 5.5) and a working `platform_do_upgrade` write path
(the A/B selector protocol is [U], section 3.4; `platform.sh` exists but refuses to
write), any Wi-Fi or switch driver, and any U-Boot source.

---

## 2. What is verified from the lab (citations)

These are the facts the skeleton is built on. They are evidence from the bench and
the parent repository; they are **not** evidence that any target builds or boots.

| fact | evidence |
| --- | --- |
| Board is the luofu v2 design (Hi5671Y, 2x Cortex-A9, 128 MB DDR3, 128 MB NAND, board R116), **not** the MediaTek v1; enclosure label may say v1 | TARGET-DESIGN.md section 2 (`HARDWARE-IDENTITY.md`; parent `BENCH-FACTS.md` section 1) |
| Stock loader boots with `bootcmd=mtd read kernel${bootflag} ${loadaddr}; bootfip ${loadaddr}`; `loadaddr=0x83200000`, `bootflag=a/b` | TARGET-DESIGN.md section 3.1; parent `BENCH-FACTS.md` section 5; local env dump (not published) |
| `bootfip` accepts only the vendor FIP package (uImage + trailer); a bare uImage is refused | TARGET-DESIGN.md section 3.1; `docs/phase42-decode/report.md`; ledger 2026-10-14 |
| The pre-entry gate is the overlay-container magic `0xd7b7ab1e` at `0x40 + ih_size + base-DTB totalsize`; entry is taken only if the applier returns 0 | TARGET-DESIGN.md section 3.2; `docs/phase42-decode/report.md` section 2; ledger PHASE 95/99 (2026-10-11) |
| The bench lane carries a fixed **40,732-byte** trailer copied verbatim into every slice. The format was decoded offline on 2026-11-18 (from the local U-Boot binary + artifacts); `make-trailer` now reproduces the vendor 2.4.15 container (40,828 B) and the 2.5.24 container byte-for-byte from user-supplied inputs. The bench lane itself still copies the fixed artifact until a generated trailer has had one attended boot | `docs/TRAILER-FORMAT.md`; ledger 2026-11-18 (trailer decode); parent `tools/fire-preflight.sh`; ledger 2026-11-17 08:05 |
| Trailer anatomy: base DTB 31,220 B + 96-byte descriptor + overlay 4,592 B (board_id 0) + overlay 4,920 B (board_id 3); container total 9,608 B; the fixed artifact is 96 B shorter than the vendor tail (missing bytes inside the last overlay's 1,024-byte FDT padding) | TARGET-DESIGN.md section 3.2; ledger PHASE 91; re-parsed locally on 2026-11-18 from the local-only artifact (the artifact is not in this repo) |
| uImage identity in the bench lane: `mkimage -A arm -O linux -T kernel -C none -a 0x80608000 -e 0x80608000 -n Linux-5.10.201`; `bootfip` reads it as `Image Name: Linux-5.10.201` | TARGET-DESIGN.md section 3.2; local build lane `build-3dl.sh`; ledger PHASE 100/125 |
| Load/entry address follows from the loader staging base and the kernel's `TEXT_OFFSET`: `(0x83200000 & 0xf8000000) + 0x00608000 = 0x80608000`; the lab hit this as a link-address defect | TARGET-DESIGN.md section 3.2; ledger PHASE 93-100 |
| NAND partition map (17 fixed partitions, total 0x8000000) and slot sizes: `kernela`/`kernelb` 8,650,752 B, `rootfsa`/`rootfsb` 24,313,856 B | TARGET-DESIGN.md section 3.3; `docs/soc/vendor-dt-notes.md` section 6; parent `BENCH-FACTS.md` section 5 |
| The lab DTS is wrong for upstream: it truncates `upgrade` to 0x1500000 and adds a bench `lab` partition; the stock size is 0x1d80000 | TARGET-DESIGN.md section 3.3; `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts` (parent) |
| rootfs slots hold squashfs in a static UBI volume named `squashfs`; geometry p=131072 m=2048 s=2048 O=2048, 117 LEBs of 126,976 B in the lab build | TARGET-DESIGN.md section 5.1; `docs/FLASH-PLAN.md` 0.1 |
| `rootfs_data` (mtd15, 22 MB) is the shared persistent overlay, vendor-format JFFS2; the design keeps JFFS2 for the first submission | TARGET-DESIGN.md section 5.3; `docs/HAZARDS.md` section 6 |
| A/B selector is the sysenv cell `0x10100C08`; loader prints `set 10` (A pair) / `set 11` (B pair); a manual B fire wrote `0x21`; the A-side handover value is **not** established. Environment blocks enva (mtd3)/envb (mtd4), format `crc32-LE(4) \| flags(1) \| data(131067)`; the bench writes both copies | TARGET-DESIGN.md sections 3.4; `docs/FLASH-PLAN.md` "Slot switch recipe"; parent `BENCH-FACTS.md` section 5 |
| Stock U-Boot checksum verifiers are off the boot path; the uImage hcrc/dcrc checks are not what refuses a slice | TARGET-DESIGN.md section 3.1; `docs/phase42-decode/report.md` section 2 |
| DTS node values (addresses, interrupts, clock/reset geometry, partitions, PHY mapping, thermal trips) | `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts` (parent tracked mirror); `docs/soc/vendor-dt-notes.md`; TARGET-DESIGN.md section 6 |

**Decoded on 2026-11-18 (offline, see `docs/TRAILER-FORMAT.md`):** the overlay
match rule (vendor_id env + compatible-family table + board_id/strap), the descriptor
shape (+8/+12/+20 are hard-checked `32`, +16 is the entry count, entries are 32-byte
totalsize/offset records), the 96-byte short-trailer explanation (zero FDT padding cut),
the loader validation order (uImage header only, no CRC, FDT header, applier, overlay
apply), and the fact that the vendor containers can be generated byte-for-byte.

**Still not verified:** the meaning of the +8/+12/+20 constants and the unvalidated
+24/+28/entry reserved words, the 628-byte slot-only scrambled region after the
container, whether the stock loader overrides `/chosen/bootargs`, how `root=`/UBI
arguments reach the kernel, the selector protocol for both pairs, the Linux read path
for the boot selector, the MAC-address source, the install path, and any driver or DTS
build. One attended boot of a novel generated trailer is still required.
See TARGET-DESIGN.md section 12.

---

## 3. The non-negotiable image facts

These come from the stock loader and were found the hard way on the bench (TARGET-DESIGN.md
section 3). Any future recipe that contradicts one of them is wrong. The image
`Makefile` repeats them inline.

### 3.1 Kernel slot = legacy uImage + FIP overlay-container trailer

    +------------------+-----------------------------+---------------------------+
    | uImage header    | kernel payload              | FIP trailer               |
    | 0x40 bytes       | zImage (+ appended DTB)     | base DTB + descriptor +   |
    |                  |                             | board DTBO overlays       |
    +------------------+-----------------------------+---------------------------+
    ^ slice offset 0                                 ^ 0x40 + ih_size

- The slot budget is **8,650,752 bytes**. Anything larger must fail the build.
- Trailer anatomy (the vendor 2.4.15 container, 40,828 B; values recovered from
  the local-only stock slot dump and the vendor sysupgrade image): base DTB at 0
  (FDT `d00dfeed`, totalsize 31,220 = 0x79f4); 96-byte overlay-container
  descriptor at 31,220 (32-byte header + 2 x 32-byte entries for this board);
  overlay 0 4,592 B at 31,316 (board_id 0); overlay 1 4,920 B at 35,908
  (board_id 3, this R116). The descriptor's container total is 9,608 B, so the
  nominal end is 40,828 B. A second independent vendor container in the 2.5.24
  firmware (base DTB 31,300; overlays 4,604/4,968; total 9,668) has the same
  descriptor shape. See `docs/TRAILER-FORMAT.md` for the byte tables.
- The fixed bench-lane trailer is **40,732 bytes and is byte-identical to the
  first 40,732 bytes of that 40,828-byte container**: the last 96 bytes of the
  last overlay's 1,024-byte all-zero FDT trailing padding are absent (its
  logical content still ends at 39,804 B). Both lengths have booted on the
  bench. The loader tolerates the short version because the applier never
  compares the declared container total with the bytes present, and the overlay
  is opened into a 128 KiB work buffer where the missing bytes fall in unused
  FDT padding. **Why the artifact was cut to 40,732 is not documented** [U];
  the arithmetic 31,220 + 4,592 + 4,920 = 40,732 (the old descriptor-less tail
  size) suggests the historical length was kept [I]. The 40,732 size is a lab
  convention enforced by the fire preflight, not a loader requirement.
- The 96-byte descriptor is now **fully decoded** (2026-11-18 disassembly of the
  on-device U-Boot 2022.07 build; big-endian words, relative to the descriptor):
  header `+0` magic `0xd7b7ab1e`, `+4` container total (the loader only
  checks it is <= 8 MiB), `+8`/`+12`/`+20` required constant **32** (their meanings
  are [U]; the loader accepts nothing else), `+16` entry count (<= 64, the
  applier's loop bound), `+24`/`+28` unread zero. Entries start at `+32`, stride
  32, count entries: entry `+0` = that overlay's FDT totalsize (the loader does
  not read it), entry `+4` = overlay offset relative to the descriptor (read; the
  target must be a valid FDT <= 128 KiB), entry `+8..+28` unread zero.
  Correcting the earlier reading: `+36` is entry 0's offset (96 = 32 + 32 x
  count), not a stored descriptor length; `+68` is entry 1's offset (4,688 =
  96 + 4,592). `tools/fip-slice.py` recomputes all of this from the file it is
  given; no vendor bytes are embedded.
- The overlay match rule is **decoded** as well: the loader compares the
  overlay's root `vendor_id` with the environment's `vendorid` (stock: `hsan`),
  then matches the first `compatible` string against an 11-row
  family/vendor table (our row is `luofu`/`hsan`; `hsan-luofu` contains the
  token), then selects by `board_id`: against an environment `boardid` if one
  is set, otherwise against the hardware board strap read from four pinctrl
  "cfg" registers (`0x14940000 + 4*{0x0a,0x0b,0x12,0x13}`, bit 6). The R116
  strap value is 3 (the board_id=3 overlay is the one the stock loader applied
  to this unit). The **first** matching overlay is applied to the base DTB with
  libfdt `fdt_overlay_apply`; if none matches, the applier returns non-zero,
  `bootfip` prints "boot apply overlay fail" and never jumps. Details and
  evidence: `docs/TRAILER-FORMAT.md` section 6.
- `bootfip` validates the uImage magic, the ARM architecture byte, a non-zero
  `ih_size`, the base DTB header and then the gate/descriptor/overlay checks
  above; it does **not** verify the uImage hcrc/dcrc or the load/entry address.
  A generated trailer must therefore satisfy the structural checks, and the
  build must still derive/assert the load address (section 3.4).
- The fixed artifact (`md5 7e5a83c0...`, sha256 `336b0792...`) is local-only and
  is **not** in this repository; the verified lane keeps copying it. From
  2026-11-18 `tools/fip-slice.py make-trailer` can generate a structurally
  validated trailer from a user-supplied base DTB + overlays (fail-closed,
  mirroring every decoded loader check, and reproducing both recorded vendor
  containers byte-for-byte in its regression test). That generation is **not
  hardware-proven**: one attended boot on a recovered unit is still required
  before the image recipe relies on it (TARGET-DESIGN.md section 5.2). A
  generated trailer is not 40,732 B; that size is only the bench-lane
  convention. **The first generated trailer was assembled into a complete slice
  on 2026-11-18 (fourth session): 8,984 B base DTB + 64 B descriptor + 230 B
  match overlay = 9,278 B trailer, 3,214,542 B slice, sha256 `3311a3e5...`;
  structurally validated only, never booted - see
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md).**

### 3.2 The gate magic is checked before kernel entry

The stock loader's overlay applier reads the 32-bit big-endian word at

    slice offset 0x40 + ih_size + base-DTB totalsize     (== the descriptor's magic)

and requires **`0xd7b7ab1e`**. It is position-sensitive: any change to `ih_size`,
the base DTB size, or the container bytes moves the gate and the loader prints a
failure and never jumps. The uImage hcrc/dcrc verifiers are on a loader path that
`bootcmd` never calls, so they are **not** the guard (TARGET-DESIGN.md section 3.1;
`docs/phase42-decode/report.md` section 2). A build recipe must fail closed if the
gate does not verify.

### 3.3 Slot sizes, A/B sysupgrade metadata

- Kernel slots `kernela` (mtd11) / `kernelb` (mtd12): **8,650,752 B** each.
- Rootfs slots `rootfsa` (mtd13) / `rootfsb` (mtd14): **24,313,856 B** each; each
  holds a squashfs image inside a static UBI volume named `squashfs`.
- `rootfs_data` (mtd15, 22 MB) is the **shared** persistent overlay; keep it as
  **JFFS2** for the first submission (it matches the vendor layout and OpenWrt
  `mount_root` supports JFFS2; degraded-NAND risk is recorded in `docs/HAZARDS.md`
  section 6). It survives an upgrade unless `-n`/explicit wipe is chosen. Because the
  two slots share one overlay, an upgrade is not atomic across the overlay - document
  that.
- Proposed sysupgrade container (schema **not frozen**, TARGET-DESIGN.md section 5.4):

      CONTROL            board=/target=/version=
      kernel             full FIP slice
      root               rootfs UBI image
      fip-manifest.txt   ih_size, base-DTB totalsize, gate offset+magic,
                         container sha256, overlay sizes, slice sha256/length

  `platform_check_image` (not written) must: match the board; reject a slice over the
  slot budget; recompute the gate/container fields from the received bytes; verify the
  manifest against those bytes; validate the root UBI volume name/geometry/length; and
  refuse anything else - including a vendor `recovery.bin`. `platform_do_upgrade`
  (not written) must write only the **inactive** pair via `/proc/mtd` labels, update
  **both** environment copies with the measured `crc32-LE | flags | data` format and the
  boot selector, verify by reading back a hash, and leave the previous pair bootable.
  It must **not** use U-Boot's TFTP `fipupdate_eth`/`rootfsupdate_eth` commands, which
  overwrite both slots. Rollback is by switching back, **not** by relying on a
  "slot failed" status word - that older claim is not supported by the 2026-11-16
  captures (TARGET-DESIGN.md section 3.4).
- Selector facts: sysenv cell `0x10100C08`; observed `0x10` = A pair, `0x11` = B
  pair (loader print), manual B write used `0x21`; the exact A-side handover value and
  the Linux read path are **[U]**. Treat the selector protocol as unproven until one
  attended A/B validation.

### 3.4 Load/entry must be derived, not copied

The loader stages the slot at `0x83200000`; the ARM decompressor computes
`(pc & 0xf8000000) + TEXT_OFFSET`. With `TEXT_OFFSET = 0x00608000` the address is
`0x80608000`. `TEXT_OFFSET` is **not** a `.config` symbol: it comes from `textofs-y`
in `arch/arm/Makefile`, so the platform patch must set
`textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`, and the image build must assert

    uImage ih_load == ih_ep == (loader_stage & 0xf8000000) + TEXT_OFFSET

The lab hit a link-address defect by not doing this (TARGET-DESIGN.md section 3.2;
ledger PHASE 93-100). Patch `003` (section 4) makes the kernel boot code materialize
this non-immediate `TEXT_OFFSET` (`mov_l`) so the linker `textofs` contract and the
`0x80608000` derivation hold in a real build; with `001`-`003` the full kernel compiles
and `System.map` gives `_text = 0xc0608000`. This skeleton's image Makefile names the
derivation and `tools/fip-slice.py assemble --loader-stage/--text-offset` asserts it;
`tools/build-kernel-slice.sh` now performs the derivation and assertion end-to-end on
the host (2026-11-18), but the real OpenWrt build wiring is still TODO.

---

## 4. How these files map into a real OpenWrt tree

They are not a drop-in target. The mapping is:

    openwrt/                                    target-skeleton/
    ├── target/linux/hisilicon/                 ├── target/linux/hisilicon/
    │   ├── Makefile                            │   ├── Makefile
    │   ├── Kconfig                             │   ├── Kconfig
    │   ├── patches-6.18/                       │   ├── patches-6.18/
    │   │   ├── 001-arm-mach-hisilicon-luofu.patch
    │   │   ├── 002-arm-kconfig-textofs.patch
    │   │   └── 003-arm-textofs-nonimmediate.patch
    │   └── luofu/                              │   └── luofu/
    │       ├── target.mk                       │       ├── target.mk
    │       ├── config-<release>                │       ├── config-5.10  (5.10 reference)
    │       │                                   │       └── config-6.18  (re-derived)
    │       ├── image/Makefile                  │       ├── image/Makefile
    │       │                                   │       ├── image/luofu.mk
    │       │                                   │       ├── image/luofu-ubinize.cfg
    │       ├── profiles/                       │       ├── profiles/
    │       └── base-files/...                  │       └── base-files/...
    │                                           │           (lib/upgrade/platform.sh)
    ├── target/linux/hisilicon/dts/luofu-r116.dts         <- dts/luofu-r116.dts
    ├── target/linux/hisilicon/dts/luofu-r116-overlay.dtso
    │                                           <- dts/luofu-r116-overlay.dts
    │                                              RENAMED: the DEVICE_DTS_OVERLAY rule
    │                                              only compiles .dtso sources
    ├── target/linux/hisilicon/Makefile         must set KERNELNAME:=zImage
    ├── scripts/fip-slice.py                    <- tools/fip-slice.py
    └── scripts/luofu-image.py                  <- tools/luofu-image.py

The remaining data path and switch blocks have evidence-graded specifications: the PIE
packet-engine data path in [../docs/PIE-DATAPATH.md](../docs/PIE-DATAPATH.md) (a refusing draft
of the driver is patch `015`) and the internal switch (switchdev, register/table/port model,
D0-D2 plan) in [../docs/LSW-SPEC.md](../docs/LSW-SPEC.md); the overall plan is
[../docs/REMAINING-DRIVERS.md](../docs/REMAINING-DRIVERS.md). The internal switch driver remains
unwritten.

The **draft kernel patch series** (written 2026-11-18; TARGET-DESIGN.md sections 4.1, 7)
is `patches-6.18/`:

- `001` adds `arch/arm/mach-hisilicon/Kconfig` with `ARCH_LUOFU` as a platform under
  the already-existing upstream `ARCH_HISI` (the upstream tree defines `ARCH_HISI` in
  `arch/arm/mach-hisi/Kconfig`; it must not be redefined), the machine Makefile
  (`obj-y += luofu.o`) and the cleaned `luofu.c` machine descriptor. The descriptor
  sets only `dt_compat` and the L2 AUX replay; `map_io`/`init_early`/`init_time`/
  `init_machine` are deliberately left at their DT-platform defaults, `.smp` is TODO
  (UP-only first submission, no mainline `smp_ops` for the vendor enable-method) and
  `.restart` is TODO (no reboot driver yet). No bench breadcrumbs, no vendor symbol
  stubs, no vendor-facing `dt_compat`.
- `002` wires it into core ARM: `source "arch/arm/mach-hisilicon/Kconfig"` in
  `arch/arm/Kconfig`, `machine-$(CONFIG_ARCH_LUOFU) += hisilicon` and
  `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000` in `arch/arm/Makefile`. The textofs
  value is required by the load-address derivation (section 3.4); on its own it does
  not compile in stock 6.18 (the value is not an ARM immediate) - see `003`.
- `003` makes that non-immediate text offset assemble in the early boot code: it
  replaces the four `#TEXT_OFFSET` immediates with the kernel's portable `mov_l`
  constant macro (`movw`/`movt` on ARMv7; literal load on pre-ARMv7), keeping the
  linker `textofs` contract and the load address unchanged. With `001`-`003` the full
  kernel compiles; details and evidence in SKELETON-SMOKE.md (third session).
- `004` adds the first real driver, upstream-style: `drivers/clk/hisilicon/crg-luofu.c`
  registers the 16 CRG gate clocks and the reset controller from one node. The node is
  modelled as `"hisilicon,luofu-crg", "syscon", "simple-mfd"`, so the driver (and the
  watchdog child in `005`) use a single syscon regmap over the shared `0x14880000`
  page: no `request_mem_region()`, no second mapping, no overlapping resource. Gate
  geometry is a driver table indexed by `dt-bindings/clock/hisilicon,luofu-crg.h`
  (clock IDs 0..15 preserve the lab ordering contract); resets keep the documented
  2-cell `(CRG page offset, bit)` form, range-checked to the three reset words. The
  YAML is `Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml`.
- `005` adds the watchdog: `drivers/watchdog/luofu-wdt.c`, 6.18 watchdog API,
  hwver-2 register values and offsets as driver data, no restart handler and no IRQ
  (both documented TODOs), YAML
  `Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.yaml`. It has no
  `reg`: it is the child of the CRG syscon node, which is what removes the lab-lane
  `-EBUSY`/`devm_ioremap()` workaround and keeps probe register-free.
- `006` adds the FMC SPI-NAND controller: `drivers/spi/spi-luofu-fmc.c` is a SPI-MEM
  controller for the 0x10a20000 FMC (the command engine for command/address/short-data
  phases, the measured DMA engine for page + OOB transfers), and the mainline SPI-NAND
  stack supplies chip detection, the on-die ECC engine, the ESMT OOB layout, bad-block
  handling and the 17 `fixed-partitions`. The lab driver's custom `mtd_info`, sysctrl
  breadcrumbs, JFFS2/UBI edits and "read ECC off" flag are absent. The vendor DT props
  become standard SPI properties (`spi_cs` -> the child's `reg`, `enable-quad-mode` ->
  `spi-{tx,rx}-bus-width`), the DTS node becomes a SPI bus with a `spi-nand` child, and the
  controller consumes the CRG clock cell (`LUOFU_CLK_SFC`) and the 2-cell reset
  (`<&crg 0x2c 0x0>`). The YAML is
  `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml`. NOTE: the ESMT
  F50L1G41LC chip entry is a v6.19 upstream addition and reaches this 6.18 target through
  OpenWrt's generic backport
  `434-v6.19-mtd-spinand-esmt-add-support-for-F50L1G41LC.patch` - not vanilla 6.18.60.
- `007` adds the MDIO bus controller: `drivers/net/mdio/mdio-luofu.c` (platform
  driver + `devm_mdiobus_alloc_size`/`of_mdiobus_register`, gate clock via
  `devm_clk_get_enabled`, the seven CRG resets as a reset array). The transfer is the
  decoded MMIO handshake (`0x20` done/ack, `0x2c` busy, `0x30` frame, `0x38` data) that a
  previous out-of-tree 5.10 driver exercised on the lab board; this port has only been
  host-compiled. Clock IDs/resets come from the CRG provider. YAML
  `Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml`.
- `008` adds the Genius PHY driver: `drivers/net/phy/luofu-phy.c`, a PHYLIB driver matched
  by PHY ID (`0x20669a00`/mask `0xffffff00`) with the generic clause 22 ops, so the PHY
  children stay standard `ethernet-phy-ieee802.3-c22` nodes and no YAML is added (PHY
  drivers are ID-matched upstream). The vendor per-PHY firmware patch is a documented TODO.
- `009` adds the GEMAC **partial** port: `drivers/net/ethernet/hisilicon/luofu-gmac.c`
  claims the binding and validates the resources (one clock, two reset lines, phy-mode,
  phy-handle) and then refuses to probe, because the TX/RX ring DMA programming model has
  never been decoded and a guessed model could hang the bus. The driver registers no
  netdev, and the DTS GEMAC nodes are `disabled`; YAML
  `Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml`. The data path is the
  remaining blocker before Ethernet can be claimed.
- `010` adds the provisional machine-restart driver: `drivers/power/reset/luofu-reboot.c`
  plus YAMLs for the reboot node and a `hisilicon,luofu-sysctrl` syscon node. It saves the
  boot selector with a read-back/write-same (only the measured A/B encodings), writes the
  vendor softrst pair to the CRG reboot word (PROVISIONAL - the exact order/width was never
  measured) and falls back to a bounded watchdog arm rather than parking the box. Probe
  performs no register access; nothing has run on hardware.
- `011` adds the peripheral pinctrl driver: `drivers/pinctrl/hisilicon/pinctrl-luofu.c`
  (6.18 pinctrl framework, 37 pins / 24 groups / 24 functions re-verified from the vendor
  module, pinconf bits decoded from the module's config ops) + YAML; probe writes no mux or
  cfg register, and consumer pin states remain open. YAML
  `Documentation/devicetree/bindings/pinctrl/hisilicon,luofu-pinctrl.yaml`.
- `012` adds the thermal sensor: `drivers/thermal/luofu_tvsensor.c` (read-only, vendor v4
  conversion on `+0x24`, 6.18 of-thermal API) + YAML, with the DTS `cls0-thermal` zone using
  the vendor trips (115 C passive / 125 C critical, 3 C hysteresis, 1 s polling) and no
  cooling map; threshold/IRQ/enable/trim registers are undecoded and unwritten. YAML
  `Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml`.
- `013` adds the DWC PCIe host controller draft: `drivers/pci/controller/dwc/pcie-luofu.c`
  (+ YAML) programs the two root complexes from the DT `ranges`/iATU data, with clocks,
  resets, RC mode and link handling; the hard rules are encoded (never read the misc
  window, aligned dwords only). The Wi-Fi PCIe endpoint is [X], so the endpoint nodes are
  documented but not enabled. YAML
  `Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml`.
- `014` fixes the CRG reset polarity discovered by the PCIe lane: the vendor reset class is
  active-low (assert clears the bit, deassert sets it; measured `0x14880034 = 0x000ff309`
  with both RCs up), so patch `004`'s original assert=set/deassert=clear was inverted.
  Patch `014` swaps the two operations and inverts `reset_control_status`; all reset
  consumers (FMC, MDIO/GEMAC, PCIe) now release their lines through the standard API
  correctly. Host-compiled only.
- `015` adds the PIE packet-engine **draft**: `drivers/net/ethernet/hisilicon/luofu-pie.c`
  (+ YAML) validates the 64 KiB window, the CRG clock/reset, the four named IRQs and
  `dma-coherent`, allocates the coherent RX/TX ring bookkeeping (4 x 1024 x 32 B RX, 4 x 4096 x
  32 B TX), and then **refuses to probe** (`-EOPNOTSUPP`) because the register/channel inventory
  has not been captured on hardware: no register access, clock enable, reset toggle, IRQ request
  or NAPI registration happens. The recovered v250 offsets/descriptor accessors/IRQ schedule are
  compiled scaffolding with the unrecovered fields left zero and marked TODO. The DTS node is
  disabled; the data path remains unverified. YAML
  `Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml`.
- `016` adds the internal LSW switch **D1a** probe skeleton: `drivers/net/ethernet/
  hisilicon/luofu-switch.c` (+ YAML) validates the 15 switch MMIO windows, the two
  CRG gate clocks, the four CRG reset lines and the switch port model
  (`ethernet-ports` children naming the GEMAC node that owns each port's netdev),
  then refuses to probe (`-EOPNOTSUPP`) because the register/queue/port-id model has
  never been measured on hardware. The switch is modelled as switchdev over the
  per-port GEMAC netdevs, not as DSA (no CPU-port tag exists in the recovered model;
  LSW-SPEC.md 9.3). No register is read or written, no clock/reset is driven, no
  netdev or switchdev notifier is registered; the node stays `disabled`. The vendor
  `queue`/`flow` DDR regions and the WoE block are deliberately not modelled. The
  forwarding milestone (D1b-D1d) still needs the attended register inventory and the
  reviewed write set. YAML
  `Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml`.
- No separate arm-dts Makefile patch: the board DTS is delivered through the target's
  `dts/` + `DEVICE_DTS_DIR := ../dts` mechanism, so a second `arch/arm/boot/dts`
  copy would be a hand-maintained duplicate (TARGET-DESIGN.md section 6.1).

Still missing from the submission:

- the rest of the driver + dt-binding series (switch data path, GEMAC data path -
  TARGET-DESIGN.md section 7); pinctrl (`011`), thermal (`012`), the PCIe host (`013`,
  endpoint [X]) and the PIE packet engine (`015`, draft) are now drafted too. The
  CRG/WDT (`004`/`005`), FMC SPI-NAND (`006`), network trio (`007`-`009`), restart (`010`),
  pinctrl (`011`), thermal (`012`), PCIe host (`013`), CRG reset-polarity fix (`014`),
  PIE (`015`) and internal-switch D1a skeleton (`016`) drafts exist with their YAMLs (the
  PHY driver needs none), but they are host-compile-validated only: no bench run, no boot,
  no upstream review. The GEMAC driver is a **resource/binding half only** - the TX/RX
  ring DMA data path is not implemented, so there is no Ethernet data path yet (MDIO +
  PHY are complete ports); the switch driver (`016`) is a **probe skeleton only** - it
  validates resources and refuses to program the switch, so there is no forwarding either;
- a bench-verified reset path: patch `010` compiles and binds, but which of its
  write sequences actually resets the SoC is unmeasured; the CRG command
  pair is PROVISIONAL and the selector protocol is still [U]
  (TARGET-DESIGN.md section 3.4);
- the A/B install path: `platform.sh` now implements a real structural
  `platform_check_image`, but `platform_do_upgrade` **refuses to write** because
  the selector protocol and the install path are still [U] (TARGET-DESIGN.md 3.4,
  5.4, 5.5);
- the sysupgrade container schema is implemented in the recipe and every field is
  cross-checked at build time, but it is **not frozen** and no container has been
  written to a device;
- a trailer generator (TARGET-DESIGN.md section 5.2) - **written and structurally validated** (`tools/fip-slice.py make-trailer`, byte-exact against both vendor containers); a novel generated trailer still needs one attended boot.

The DTS delivery is an open question: OpenWrt targets commonly keep board DTS files
under `target/linux/<board>/dts/`, while a new SoC DTS may be cleaner as a kernel patch
under `arch/arm/boot/dts/`. Pick one, never both hand-maintained (TARGET-DESIGN.md
section 6.1).

---

## 5. What has actually been checked (and what has not)

Checked on the author's host on 2026-11-18, nothing more:

- `tools/fip-slice.py`: `python3 -m py_compile` passes. `check`/`assemble`/`manifest`
  were exercised against a synthetic uImage + the local-only 40,732-byte bench trailer
  and against corrupted variants (tampered gate magic, truncated input, bad uImage
  magic, wrong load address, overwrite without `--force`): expected pass/fail outcomes.
  **`make-trailer` is implemented and was validated offline**: from local-only vendor
  inputs it reproduces the 2.4.15 container (40,828 B) and the 2.5.24 container
  byte-for-byte, and the generator refuses mismatched vendor/board/family, oversized
  overlays, non-FDT inputs and failed libfdt overlay application. No vendor bytes were
  copied into the repository; one attended boot of a novel trailer remains required.
- **OpenWrt main smoke test (2026-11-18)**: copied into `openwrt main` (commit
  `27663a0`), `make defconfig` PASS (generated
  `CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y`) and
  `make target/linux/prepare` RC=0 (kernel 6.18.60). No image/boot at that stage; the
  full kernel compile came later the same day (third bullet below). Details:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md).
- **Kernel patch series + config re-derivation (2026-11-18, host-only)**: both draft
  patches apply cleanly to `linux-6.18.60` in the same `openwrt main` clone
  (`patch -p1 --dry-run` PASS, `make target/linux/prepare` RC=0, no `.rej`); the
  patched tree has `arch/arm/mach-hisilicon/` and the `textofs`/machine lines. The
  canonical top-level `make kernel_oldconfig` was run to completion: it built the
  tree's own OpenWrt GCC 14.4.0 toolchain from source (~13.5 minutes) and emitted a new
  `luofu/config-6.18` delta - `ARCH_HISI=y`, `ARCH_LUOFU=y`, ARMv7/LE/VFP, 281
  symbols - and a second run was byte-identical. The machine object
  `arch/arm/mach-hisilicon/luofu.o` **compiles** with that toolchain and its
  `DT_MACHINE_START` entry is in the object. At that point `make target/linux/compile`
  stopped on ONE blocker: stock `arch/arm/kernel/head.S` cannot encode
  `#TEXT_OFFSET = 0x00608000` as an ARM immediate. An earlier pass with the bench GCC
  11.3 also hit 94 `retain`-attribute errors under `CONFIG_WERROR`; that was the
  unsupported compiler and is gone with the tree's GCC 14.4. A local, **non-delivered**
  workaround (kernel-`.config` `CONFIG_WERROR` off + `TEXT_OFFSET=0x00008000`) built a
  3,205,816-byte zImage; that proved the rest of the patch/config builds but was **not**
  a bootable image and **not** evidence that the target works. Exact commands, errors
  and logs: [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md).
- **`TEXT_OFFSET` fix + full kernel compile (2026-11-18, third host-only session)**:
  patch `003` (section 4) makes the non-encodable `0x00608000` assemble via `mov_l`.
  In the same clone, with the kernel build tree removed so it is a clean build,
  `make target/linux/prepare` RC=0 and `make target/linux/compile V=s -j16` **RC=0**
  with the tree's own OpenWrt GCC 14.4.0 toolchain (`CONFIG_WERROR=y`; no make
  override): `arch/arm/boot/zImage`, 3,205,200 B, sha256
  `45ad00d502ed7b2386f83db794e9015267f4f9d66de8d0deaab8aca24cf18d10`. `System.map`
  gives `_text = 0xc0608000` / `swapper_pg_dir = 0xc0604000`, and the linked early
  boot code contains the `movw/movt` `0x00608000` materialization at all four sites;
  the load/entry derivation is `(0x83200000 & 0xf8000000) + 0x00608000 = 0x80608000`,
  i.e. the image recipe's `mkimage -a 0x80608000 -e 0x80608000` is unchanged. **This
  is a compile product, not a bootable image**: it had no FIP trailer, no rootfs and
  was never booted (the fourth-session bullet below adds the structurally validated
  slice). Evidence is local-only under
  `build/linux-kbuild/waves/upstream-6.18-textoffset-20261118/`; exact disassembly,
  hashes and derivation: [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md).
- **First own-artifact FIP kernel slice (2026-11-18, fourth host-only session)**:
  `tools/build-kernel-slice.sh` compiled the draft DTS (no source change needed; one
  expected `unique_unit_address` warning), compiled the new loader-match overlay,
  wrapped the 3,205,200 B zImage in a legacy uImage (`Linux-6.18.60`, load/entry
  `0x80608000`), generated a 9,278 B trailer with `fip-slice.py make-trailer`
  (`--overlay-apply require`), assembled a **3,214,542 B** slice (5,436,210 B under the
  slot budget) and validated it with `check` + `manifest` plus independent gate/
  descriptor and system-dtc re-reads. Two clean runs are byte-identical
  (`SOURCE_DATE_EPOCH=0`). Slice sha256 `3311a3e5...`; gate `0xd7b7ab1e` at
  `0x310ba8`. **Never booted; not flashable through the current bench fire lane
  (fixed 40,732-byte trailer preflight).** Details:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (fourth session).
- **First real driver series - CRG clock/reset + watchdog (2026-11-18, fifth host-only
  session)**: patches `004` and `005` add the two drivers, their YAML bindings, the
  `dt-bindings/clock/hisilicon,luofu-crg.h` clock-ID ABI and the DTS nodes. Results:
  both patches apply to pristine `linux-6.18.60` (`patch -p1 --dry-run` RC=0) and in
  the clone (`make target/linux/prepare` RC=0); the full kernel compiles
  (`make target/linux/compile V=s -j16` **RC=0**, 1190 s, tree's own OpenWrt GCC 14.4.0,
  `CONFIG_WERROR=y`, **0 warnings from the new files**), with `CONFIG_LUOFU_CRG=y` +
  `CONFIG_LUOFU_WDT=y` built-in and both probes in `System.map`; `zImage` 3,222,416 B
  sha256 `50cd07a1...`. The board DTS compiles with the patched kernel's own dtc
  (1.7.2) to an 8,944 B DTB (sha256 `86396521...`) with **zero warnings** - the former
  `unique_unit_address` collision is gone. `dt_binding_check` passes for both YAMLs
  (dtschema 2026.9; `yamllint` unavailable, that one lint step skipped) and the DTS CRG
  subtree validates RC=0 against the real board DTB. An independent cross-check confirms
  all 16 clock IDs match the driver table and the dt-bindings header and all 15 existing
  consumers use the expected IDs. The hand-added config delta is symbol-identical to a
  canonical `make kernel_oldconfig` re-derivation. `checkpatch.pl`: 0 errors / 0
  warnings on both drivers and the header; neither driver claims MMIO
  (`request_mem_region`/`ioremap` absent, one shared syscon regmap) and neither probe
  touches a register. **Nothing was booted or bench-tested; the binding maintainer entry
  and the upstream review are open.** Exact commands/results:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (fifth session).
- **Network trio - MDIO bus, Genius PHY, partial GEMAC (2026-11-18, sixth host-only
  session)**: patches `007` (MDIO bus controller on the 6.18 MDIO API), `008`
  (Genius PHY driver, ID-matched, no binding) and `009` (GEMAC binding + resource
  driver, **partial: no TX/RX ring DMA data path**) plus the DTS/config edits. Results:
  all three apply to pristine `linux-6.18.60` (`patch -p1 --dry-run` RC=0) and in
  the clone (`make target/linux/prepare` RC=0); the full kernel compiles
  (`make target/linux/compile V=s -j16` **RC=0**, 1284.97 s, tree's own
  OpenWrt GCC 14.4.0, `CONFIG_WERROR=y`, **0 warnings**), with
  `CONFIG_LUOFU_MDIO=y` + `CONFIG_LUOFU_PHY=y` + `CONFIG_LUOFU_GMAC=y`
  built-in and all three probes in `System.map`; `zImage` 3,223,728 B sha256
  `2749c294fe95b6fe3ece2ecc26357dbb44d0aa5e5fc765699e8865f0a6e5b6f5` (the `TEXT_OFFSET` contract is unchanged:
  `_text = 0xc0608000`). The board DTS compiles with the patched kernel's own dtc
  to a 9,459 B DTB (sha256 `f6781920eedc8421ffbe5d72aa6adecc9f64509ee2316f62263aabd5f4f9c887`) with **zero warnings**.
  `dt_binding_check` passes for both new YAMLs (dtschema 2026.9; `yamllint`
  unavailable, that lint step skipped); `checkpatch.pl` reports 0 errors / 0 warnings
  / 0 checks on all three drivers; there are no lab/bench breadcrumbs or vendor-only
  properties. **The GEMAC driver registers no netdev and refuses to probe** (the ring/DMA
  model is not decoded) and its DTS nodes are `disabled`; MDIO and PHY are complete
  ports. Nothing was booted or bench-tested. Exact commands/results:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (sixth session).
- **FMC SPI-NAND controller (2026-11-18, sixth host-only session)**: patch `006` adds the
  FMC as a SPI-MEM controller with the mainline SPI-NAND stack behind it, its YAML binding
  and the DTS conversion (SPI bus + `spi-nand` child with the 17 fixed partitions).
  Results: the patch applies to pristine `linux-6.18.60` (`patch -p1 --dry-run` RC=0) and
  in an isolated clone copy (`make target/linux/prepare` RC=0); the full kernel compiles
  (`make target/linux/compile V=s -j16` **RC=0**, 109 s wall clock, tree's own OpenWrt GCC
  14.4.0, `CONFIG_WERROR=y`, **0 warnings from the new files**), with `CONFIG_LUOFU_FMC=y`
  built-in and `luofu_fmc_probe` / `luofu_fmc_exec_op` in `System.map`; `zImage`
  3,248,984 B sha256 `51f340ad...`. The board DTS compiles with the patched kernel's own
  dtc (1.7.2) to a 9,163 B DTB (sha256 `3541b267...`) with **zero warnings**, and its FMC
  subtree validates against the binding schema (`dt-validate` RC=0). `dt_binding_check`
  passes for the YAML (dtschema 2026.9; `yamllint` unavailable, that lint step skipped).
  The hand-added config delta is symbol-identical to a canonical `make kernel_oldconfig`
  re-derivation (285/285, idempotent, merged config identical). `checkpatch.pl`: 0 errors /
  0 warnings on the driver and the YAML. **Nothing was booted or bench-tested; the ECC,
  DMA-OOB and quad-enable semantics and the v6.19 ESMT chip-entry dependency are open.**
  Exact commands/results: [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (sixth
  session, FMC/NAND port).
- **Internal LSW switch D1a probe skeleton (2026-11-18, twelfth host-only session)**:
  patch `016` adds `drivers/net/ethernet/hisilicon/luofu-switch.c`, its YAML binding,
  `CONFIG_LUOFU_SWITCH` and the board DTS switch node (disabled). Results: the patch
  applies to pristine `linux-6.18.60` (`patch -p1 --dry-run` RC=0) and in the isolated
  clone (`make target/linux/prepare` RC=0); the full kernel compiles
  (`make target/linux/compile V=s -j16` **RC=0**, tree's own OpenWrt GCC 14.4.0,
  `CONFIG_WERROR=y`, **0 warnings from the new files**), with `CONFIG_LUOFU_SWITCH=y`
  built-in and `luofu_lsw_probe` in `System.map`; zImage 3263624 B sha256
  `201ba86863d8ee6c14d865702ee2339cd3e373c30fbb4b94983afcc2d72df83f`; the board DTS compiles with the patched kernel's own dtc to a
  10538 B DTB (sha256 `ff34bc7f7e744b61bbc0ca7bd0934317e46d837c87d024592fc08335ed5ce4a0`) with **zero warnings**; `dt_binding_check`
  passes for the new YAML; `checkpatch.pl` reports 0 errors / 0 warnings / 0 checks;
  the probe path has no MMIO access at all and refuses with `-EOPNOTSUPP`.
  **Nothing was booted or bench-tested; the switch register/queue/port-id model remains
  unmeasured.** Exact commands/results:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md) (twelfth session).
- `dts/luofu-r116.dts`: parses after `cpp` + `dtc` to a DTB on the host with the patched
  kernel's own dtc (1.7.2) and **zero warnings** (the former deliberate
  `unique_unit_address` collision between the CRG and watchdog nodes is resolved by the
  syscon-parent design - see the driver-session bullet above). This is a kernel-dtc
  compile of the target DTS plus a subtree schema validation; a full-draft-DTS
  `dtbs_check` is still NOT clean because every other node still has a TODO binding.
- `base-files/etc/board.d/02_network` and `lib/upgrade/platform.sh`: `sh -n` passes.
- **Image recipe build (2026-11-18, image-recipe host-only session)**: the real
  target image chain in `image/luofu.mk` (plus `image/luofu-ubinize.cfg`,
  `base-files/lib/upgrade/platform.sh` and `tools/luofu-image.py`) was installed
  in an isolated `openwrt main` clone (`27663a0`, `linux-6.18.60`, patches
  `001`-`015`) and the whole tree was built: `make defconfig` RC=0 and
  `make world -j16` RC=0. The build produced
  `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`
  (6,175,022 B, sha256 `ea143b03d9d248b3e2b21723ef639e42817e66da847285496ebbc46e12756881`)
  from a 3,283,008-byte FIP kernel slice (`23c3286b...`) and a 2,883,584-byte
  rootfs UBI (`d3c5611c...`, static volume `squashfs`, 22 PEBs = 2 layout + 20
  data). The slice carries a uImage (`Linux-6.18.60`, ih_size 3,272,457,
  load/entry `0x80608000`) with the DTB appended, a trailer generated from OUR
  board DTB + loader-match overlay (10,487 B) and the gate magic `0xd7b7ab1e`
  at `0x32171a`; `image/luofu.mk` recomputes and asserts the load/entry address
  from the loader staging base and `TEXT_OFFSET`, and reads `_text` back from
  the built `vmlinux` with the target `nm`. The container carries
  `CONTROL`, `kernel`, `root`, `fip-manifest.txt` and `INSTALL-STATUS.txt`
  (NOT FLASHABLE); `tools/luofu-image.py check-container` re-validated every
  field from the final bytes, `platform_check_image` (sourced from
  `base-files/lib/upgrade/platform.sh`) accepted the real artifact and refused
  corrupted/foreign variants, and two consecutive `make world` runs produced a
  byte-identical image (REPRODUCIBLE). **Nothing was booted, and
  `platform_do_upgrade` still refuses to write.** Details, commands and exact
  sizes/hashes: [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md)
  (image-recipe session).

**Not done, and not claimed:** the built sysupgrade container has never been
booted or written to a device (one attended boot of a generated FIP trailer is still
required), no install/factory path exists (`platform_do_upgrade` refuses because the
A/B selector protocol is [U]), no device interaction, no UART, no bench tool run, and
no network data path (the GEMAC driver is a resource/binding half only and its DTS
nodes are disabled). The image is a host build product with a real OpenWrt rootfs,
not a release. The four driver bindings (CRG, watchdog, MDIO, GEMAC) pass
`dt_binding_check` (the PHY driver needs none) but have had no upstream review, and
a full-draft-DTS `dtbs_check` is still not clean (every other node has a TODO binding). The
zImage produced by the compile stage and the uImage + FIP slice assembled by
`tools/build-kernel-slice.sh` are host build products that satisfy the *decoded* loader
structure; they are not deliverable images and have never been loaded on hardware. The bench
fire lane still copies the fixed 40,732-byte trailer and does not accept a generated one yet.

---

## 6. Unknowns and TODOs

Cross-cutting (all [U]/[I], TARGET-DESIGN.md section 12):

1. **Trailer regeneration** - decoded and implemented (`docs/TRAILER-FORMAT.md`,
   `fip-slice.py make-trailer`, byte-exact against both vendor containers), and the
   first generated trailer has now been assembled into a complete slice by
   `tools/build-kernel-slice.sh` (structurally validated, never booted). What remains:
   one attended boot of a novel generated trailer; the host steps are now wired into
   `image/luofu.mk` and the whole image chain is host-built (SKELETON-SMOKE.md,
   image-recipe session), and the unknowns listed in TRAILER-FORMAT.md (constant
   meanings, 628-byte slot region).
2. **Boot arguments** - how the rootfs selector reaches the kernel, and whether the
   stock U-Boot overrides `/chosen/bootargs`.
3. **Install path** - stock TFTP recovery vs a documented vendor-root install
   (TARGET-DESIGN.md section 5.5). No factory image is defined until this is executed.
4. **Selector protocol** - exact env-block/bootreg write sequence for both pairs and a
   Linux read path. Blocks `platform.sh`.
5. **MAC address / efuse** - source not established; blocks `nvmem` cells and the
   device page.
6. **Overlay filesystem** - JFFS2 kept for the first submission (decision in
   TARGET-DESIGN.md section 5.3); conversion to UBI is a later option only.
7. **Switch driver model** - **RESOLVED 2026-11-18: switchdev over per-port netdevs, not
   DSA** (no CPU-port tag in the recovered model; each wired port already has a GEMAC
   netdev; the vendor integration is switchdev; LSW-SPEC.md 9.3). The binding's
   `ethernet-ports` children are the port model and the first slice is the patch-`016`
   D1a probe skeleton. Still open: the switch data path itself (D1b-D1d need the attended
   register inventory/write set) and the board `02_network` port map.
8. **SMP** - no `platsmp` port; the draft DTS is UP-only.
9. **Wi-Fi scope statement** - out of the first submission; the vendor stack is not
   mac80211 and the firmware is closed (TARGET-DESIGN.md section 8.3).
10. **Publication sequence** - history purge before any of this can point at a public
    tree.
11. **TEXT_OFFSET is not an ARM immediate** - **RESOLVED 2026-11-18 (third host-only
    session)** by `patches-6.18/003-arm-textofs-nonimmediate.patch`: the four
    `#TEXT_OFFSET` immediates (`arch/arm/kernel/head.S` stext + `pgtbl` macro,
    `arch/arm/boot/compressed/head.S` two sites) now materialize the constant with
    `mov_l`. The full kernel compiles (RC=0) with `0x00608000` and the load/entry
    derivation still resolves to `0x80608000` (section 5; SKELETON-SMOKE.md third
    session). The patch is not reviewed upstream; no offset change was made.
12. **OpenWrt toolchain** - RESOLVED: the tree's own GCC 14.4.0 toolchain was built
    from source by `make kernel_oldconfig` (~13.5 minutes) and used for the committed
    `config-6.18`, the failed pre-fix compile and the successful post-fix full kernel
    compile; the earlier GCC 11.3 `retain`/`CONFIG_WERROR` failure is gone. The
    toolchain is no longer a blocker for the kernel build.

13. **GEMAC data path** - the TX/RX ring and DMA programming model is not decoded, so
    patch `009` is a resource/binding half only (no netdev; its DTS nodes are
    disabled). This blocks the wired Ethernet data path; MDIO + PHY are complete ports.
    The vendor GPL source (request open) or a citable reverse-engineering record is the
    prerequisite.
14. **Genius PHY firmware patch** - the vendor driver downloads a per-PHY firmware patch
    (`lsw/hi_ephy_%xv%x.bin`) before link training; its format and necessity are not
    decoded. The 6.18 PHY driver uses the generic clause 22 path and loads nothing; if
    hardware tests show a link-training difference from the vendor stack, that patch is
    the first suspect (a hypothesis to test, not a claim).

Skeleton-specific findings recorded while writing the files:

- **Board compatible vs device name**: `dts/luofu-r116.dts` keeps the lab root
  compatible `"hisilicon,luofu-r116", "hisilicon,luofu"`, while the image recipe sketch
  (following TARGET-DESIGN.md section 4.2) uses `SUPPORTED_DEVICES += cudy,wr3000-v2`.
  OpenWrt's `board_name`/sysupgrade match and the DTS compatible order must be made to
  agree; the decision is open (an upstream board DTS would normally carry a
  `cudy,wr3000-v2` compatible first).
- **Kernel config**: `config-5.10` is committed because 5.10.201 is the only kernel
  with lab evidence; `config-6.18` is now **re-derived** through the tree's own
  `oldconfig` path (2026-11-18, see section 5). Re-deriving it with the previous
  5.10-derived reference as input silently dropped the whole platform (no
  `ARCH_MULTI_V7` -> ARMv5/ARM926T fallback); fixing the input by restoring the
  user-selectable symbols the lab config has (`ARCH_MULTIPLATFORM`, `ARCH_MULTI_V7`,
  `ARM_THUMB`, `VFP`, `VFPv3`) is what makes it select `ARCH_HISI`/`ARCH_LUOFU`.
  Regenerating with the derived file is idempotent.
- **Duplicate unit address at 0x14880000: RESOLVED (2026-11-18, fifth session).** The
  CRG page is now one `system-controller@14880000` node (`"hisilicon,luofu-crg",
  "syscon", "simple-mfd"`) with the watchdog as a child that has no `reg`; the two
  drivers share one syscon regmap. The former deliberate dtc
  `unique_unit_address` warning is gone (the DTB now compiles with zero warnings) and
  there is no overlapping MMIO claim.
- **Vendor 2-cell reset form**: kept, documented in the CRG binding and range-checked
  by the driver to the three reset words (`0x2c/0x30/0x34`) so a specifier can never
  reach the watchdog/soft-reset/mux/PLL words. Converting it to a named reset-ID table
  is still a TODO for the submission (it changes every consumer's cells and needs an
  agreed reset-ID inventory).
- **SP804 interrupt** `<1 0x2f 4>` is not a valid mainline PPI cell value (PPIs are
  0..15 in the binding). Marked NEEDS-VERIFICATION in the DTS; it only matters if the
  SP804 is used (the board uses the TWD).
- **Kconfig placement**: a target directory normally has no `Kconfig`; the required
  platform symbols belong in the kernel patch. The file here is a review stub and says
  so. The patch series now defines `ARCH_LUOFU` in `arch/arm/mach-hisilicon/Kconfig`
  **depending on** the upstream `ARCH_HISI` rather than redefining `ARCH_HISI`
  (redefining it would be a Kconfig duplicate); an alternative upstream shape would be
  to add the platform to the existing `arch/arm/mach-hisi/Kconfig`, which review would
  decide.
- **ARCH_HISI defaults**: enabling `ARCH_HISI` default-enables the clock/reset/thermal
  drivers of other Hisilicon SoCs (`default ARCH_HISI` in their Kconfig). The
  re-derived config marks them `[D-default]`; they should be pruned or the defaults
  tightened before submission.
- **No arm-dts Makefile patch needed**: the target delivers the board DTS through
  `dts/` + `DEVICE_DTS_DIR := ../dts`; a parallel `arch/arm/boot/dts` entry would be
  the hand-maintained duplicate the design forbids. (The `003` slot in
  `patches-6.18/` is the TEXT_OFFSET boot-code fix, not a DTS patch.)

---

## 7. Safety and publication constraints honored

- No device was touched: no bench tools, no UART, no SSH, no flash. This is a
  host-side documentation/skeleton deliverable; the 2026-11-18 patch/config sessions
  (including the sixth-session network port) ran only OpenWrt make targets and a kernel
  compile in the local clone.
- No vendor binaries, `.o/.ko`, firmware blobs, calibration data, extracted DTBs,
  U-Boot images, credentials or dumps are present. Named values (`0xd7b7ab1e`,
  40,732, slot sizes, addresses) are structural facts already documented in the
  published `TARGET-DESIGN.md`/`phase42-decode` analysis.
- No false buildability: every file says SCHEMATIC/UNTESTED, and this README states
  plainly what did and did not build. What exists: the patches apply, the config
  re-derives, and the full kernel compiles with the tree's own OpenWrt GCC 14.4.0
  toolchain and the correct `0x80608000` load-address derivation (2026-11-18, section 5;
  third-session evidence in SKELETON-SMOKE.md). What does not: any image (FIP slice,
  trailer, rootfs) and any boot. The zImage from that compile is an intermediate build
  product, not a target image; the earlier evidence-only workaround zImage is superseded
  and nothing is shipped.
- The publication caveat stands: the `opensource/` submodule is **NOT READY** until
  the history purge in [PUBLISH-READINESS.md](../docs/PUBLISH-READINESS.md) is done.

---

## 8. Reading order

1. [TARGET-DESIGN.md](../docs/TARGET-DESIGN.md) - the source of truth for this design.
2. [ROADMAP-OPENWRT.md](../docs/ROADMAP-OPENWRT.md) - phased plan and current status.
3. [TARGET-SKELETON.md](../docs/TARGET-SKELETON.md) - short index of this directory.
4. `docs/phase42-decode/report.md` - the loader gate decode.
5. `docs/soc/vendor-dt-notes.md` - the vendor DT read (partitions, overlays, compatibles).
6. `docs/FLASH-PLAN.md` - measured slot switch / UBI geometry.
7. [PUBLISH-READINESS.md](../docs/PUBLISH-READINESS.md) - why this half is not public yet.
