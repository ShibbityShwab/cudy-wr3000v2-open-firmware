# Upstream OpenWrt target for the luofu v2 board - REPORT

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Lane:** task-243 (owner `upstream2`). **Date:** 2026-11-27.
**Status:** the target, the board DTS and the image recipe are single-sourced,
assembled into an OpenWrt main tree and **built into a device image in this
lane** (host-only; the image was NOT flashed and NOT booted). The target is
**not upstream-accepted** and the image is deliberately **not flashable**
(install path never executed on a device).

## 1. What was asked, and what is delivered

The task: create the upstream OpenWrt target for the luofu v2 board -
(1) the target directory `target/linux/hisilicon`; (2) the board DTS carrying
the 2x Cortex-A9 CPU, the 128 MB DDR3, the SPI-NAND and the UART (the
extracted board facts); (3) the image recipe; (4) the assembled build.

| # | request | delivered (single source) | evidence in this lane |
| --- | --- | --- | --- |
| 1 | target directory | [`../../../target-skeleton/target/linux/hisilicon/`](../../../target-skeleton/target/linux/hisilicon/) - 85 installed files | [validation/assembly/](validation/assembly/) check/manifest/verify |
| 2 | board DTS | [`../../../target-skeleton/dts/luofu-r116.dts`](../../../target-skeleton/dts/luofu-r116.dts) (715 lines) + loader-match overlay (90 lines) | [validation/dts/](validation/dts/) compile log + 24/24 contract |
| 3 | image recipe | [`.../image/luofu.mk`](../../../target-skeleton/target/linux/hisilicon/image/luofu.mk) + [`image/Makefile`](../../../target-skeleton/target/linux/hisilicon/image/Makefile) + [`image/luofu-ubinize.cfg`](../../../target-skeleton/target/linux/hisilicon/image/luofu-ubinize.cfg) + the three host tools | [validation/image-build/](validation/image-build/) |
| 4 | assembled build | `make -j16 world` in an OpenWrt main tree with the target installed | [validation/image-build/](validation/image-build/), report section 5 |

The lane does **not** copy the target into this directory: the single-source
rule from the previous lane stands (no fifth DTS/patch copy). This lane adds
the missing **build evidence and report** for those sources, and re-runs the
checks that were not committed before.

## 2. The target directory

Installed by [`../../upstream-dts/tools/assemble-target.sh`](../../upstream-dts/tools/assemble-target.sh)
(mapping: target-skeleton/README.md section 4). Manifest breakdown
([validation/assembly/target-manifest.tsv](validation/assembly/target-manifest.tsv),
86 lines = header + 85 files):

| group | files | destination |
| --- | --- | --- |
| kernel patch series | 66 | `target/linux/hisilicon/patches-6.18/` (001-060, 062-067; 061 is unused, the former 013a endpoint pulse is folded into 013 in the v4 form) |
| target metadata | 5 | `Makefile`, `Kconfig` (review stub, see PR-READINESS 2.2), `luofu/target.mk`, `luofu/config-6.18` (402 lines, bench config), `luofu/config-5.10` (reference only) |
| image recipe | 3 | `image/{Makefile,luofu.mk,luofu-ubinize.cfg}` |
| board DTS | 2 | `dts/luofu-r116.dts`, `dts/luofu-r116-overlay.dtso` |
| base-files | 6 | `etc/board.d/02_network`, `etc/init.d/{heartbeat,luofu_record_pair}`, `lib/upgrade/platform.sh`, `lib/functions/luofu-{mac,netdev}.sh` |
| host tools | 3 | `scripts/{fip-slice,luofu-image,pad-dtb}.py` |

Key target choices (from the files):

- `Makefile`: `ARCH:=arm`, `BOARD:=hisilicon`, `SUBTARGETS:=luofu`,
  `KERNEL_PATCHVER:=6.18`, `KERNELNAME:=zImage`,
  `FEATURES:=squashfs nand ubifs`, `DEFAULT_PACKAGES += kmod-mtd-rw ubi-utils`
  (ubiformat is needed by the A/B write path).
- `luofu/target.mk`: `CPU_TYPE:=cortex-a9`, `FEATURES+=nand`; the package
  list still carries TODO(packages) markers.
- device stanza (`image/luofu.mk`): `DEVICE_DTS := luofu-r116`,
  `DEVICE_VARIANT := v2.0 (HiSilicon luofu R116)`, `SUPPORTED_DEVICES` with the
  DT compatibles, `IMAGES += sysupgrade.bin`, and deliberately **no factory /
  install stanza**.

## 3. The board DTS - CPU, memory, NAND, UART

Source: `target-skeleton/dts/luofu-r116.dts`, 715 lines,
sha256 `6774221ef49b10888bfb277f2c7620858a798a3cf228b1660cb483df50e6b062`.
Anchor: `/ { model = "Cudy WR3000 v2.0 (HiSilicon luofu R116)"; compatible =
"cudy,wr3000-v2", "hisilicon,luofu-r116", "hisilicon,luofu"; }`.

**CPU (2x Cortex-A9).** `cpus { enable-method = "hisilicon,luofu-smp"; }` with
`cpu@0`/`cpu@1`, both `compatible = "arm,cortex-a9"`, `reg = <0>/<1>`. The
SMP release method is the decoded vendor one (SYSCTRL pen word + CRG resume
bits; patch 018), and 2-CPU operation is bench-verified on the 2026-11-21
baseline (RELEASE-NOTES.md).

**Memory (128 MB DDR3).** The board has 128 MiB at `0x80000000..0x88000000`;
the stock loader reserves the first 5 MiB for staging/FIP-ATAG, so Linux gets
`memory@80500000 { device_type = "memory"; reg = <0x80500000 0x7b00000>; }`
(123 MiB). `reserved-memory/flashinfo@80600000 { reg = <0x80600000 0x4000>;
atag-offset = <0x1000>; }` is the loader hand-off contract (compatibles
`hisilicon,luofu-flashinfo`, `hsan,flashinfo_reserved`): without it the stock
U-Boot skips the staging that CPU1 waits on.

**NAND (128 MB SPI-NAND via FMC).** `fmc: spi@10a20000` with
`compatible = "hisilicon,luofu-fmc"`, `reg = <0x10a20000 0x1000>, <0x1c000000
0x100000>` (register page + flash window), SFC clock/reset, and a
`flash@1 { compatible = "spi-nand"; spi-max-frequency = <50000000>;
spi-tx/rx-bus-width = <4>; }` child with **17 fixed partitions**:
esbc, uboota, ubootb, enva, envb, fac, bdinfo (read-only; factory MAC at
+0xde00), cfga, cfgb, log, pstore, kernela/kernelb (`0x840000` = 8,650,752 B
each - the kernel slot budget), rootfsa/rootfsb (`0x1740000` = 24,313,856 B
each - the rootfs slot budget), rootfs_data (`0x1600000`), upgrade.

**UART (console).** `uart1: serial@1010f000` (`snps,dw-apb-uart`, 32-bit
reg-shift, IRQ 0x2e, `apb_clk`) is `status = "okay"`; `aliases { serial0 =
&uart1; console = &uart1; }`; `chosen { stdout-path = "serial0:115200n8"; }`.
`uart0: serial@1010e000` is present and disabled (serial1 alias).

The rest of the board is modelled and contract-checked in the same DTB:
CRG syscon + watchdog, sysctrl/reboot/boot-selector (selector offset 0xc08),
GIC/SCU/PL310/TWD/SP804, two DW GPIO banks with keys/LEDs, both PCIe root
complexes (RC0 carries the v4 `reset-gpios` endpoint pulse, GPIO0 20/21/28
active low, 20/100 ms), MDIO + 4 PHYs + 5 GEMAC nodes, the LSW switch + PIE
engine, pinctrl, thermal zones and the clock tree.

**Compile + contract evidence (host-only).** `cpp` + `dtc` (GCC 16.2.1 /
DTC v1.8.1): RC=0, zero warnings, DTB **12,848 B**
sha256 `1c200daf8046cb154e5e6dfa990cd827348ab960da9fce13596ebfa0bb4e04b8`
(-detail: [validation/dts/build-evidence.txt](validation/dts/build-evidence.txt)).
The 24-check board contract on the compiled DTB (not on source text):
**24/24 PASS, 0 FAIL**
([validation/dts/dts-check.log](validation/dts/dts-check.log)). The same DTB
sha256 is produced by the stock OpenWrt `Image/BuildDTB` rule in the image
build (cross-check: [validation/image-build/dtb-crosscheck.txt](validation/image-build/)).
The run reproduces the task-238 evidence byte-for-byte.

**"The extracted DTS".** No vendor DTS/DTB is committed anywhere: the vendor
base DT (local, gitignored `dumps/router-20261108/kernelb/kernelb.dtb`,
13,880 B, sha256 `ffe7bcc4...`) is an analysis input only. The extracted board
facts were transcribed into our own clean-room DTS (the one above) and into
[`../../TARGET-DESIGN.md`](../../TARGET-DESIGN.md)/HARDWARE-IDENTITY.md; the
vendor-vs-ours node/compatible diff and the remaining DTS review gaps are in
[`../../upstream-dts/DTS-COVERAGE.md`](../../upstream-dts/DTS-COVERAGE.md)
sections 4 and 6.

## 4. The image recipe

Files: `image/Makefile` (includes the subtarget `luofu.mk`, registers the
device stanza), `image/luofu.mk` (326 lines), `image/luofu-ubinize.cfg`
(56 lines), plus `tools/{fip-slice,luofu-image,pad-dtb}.py` installed as
`scripts/` in the tree.

- **Kernel chain** (every step is fail-closed):
  `kernel-bin | luofu-check-textofs | luofu-pad-dtb | append-dtb |
  uImage none | luofu-fip-slice | luofu-fip-manifest`.
  - `luofu-check-textofs` reads `_text` from the just-built vmlinux and
    refuses the build unless its physical address equals the derived
    TEXT_OFFSET `0x00608000` (patches 002/003);
  - load/entry are **derived, not copied**: `(loader stage 0x83200000 &
    0xf8000000) + 0x00608000 = 0x80608000`, and the recipe errors out if the
    derivation changes;
  - `luofu-pad-dtb` pads the base DTB to an 8-byte totalsize (the stock
    bootfip gate-word load is unaligned otherwise);
  - `luofu-fip-slice` generates the FIP trailer **from our own base DTB +
    our loader-match overlay** (board id 3, vendor id `hsan`, family
    `luofu`), applies the overlay through the kernel tree's `fdtoverlay`
    (required, never skipped) and assembles uImage+trailer; the slice is
    re-validated against the 8,650,752 B kernel slot and the gate magic.
- **Rootfs chain**: 4 KiB-padded squashfs -> UBI with two volumes:
  `squashfs` (static, vol_id 0) and `rootfs_data` (dynamic, empty,
  `vol_id=1`, `vol_size=1MiB`, `vol_flags=autoresize`); geometry
  p=131072 m=2048 s=2048 O=2048; `luofu-image.py` re-parses every EC/VID
  header, CRC, volume table and slot budget from the produced bytes.
- **Container**: deterministic tar with `CONTROL`, `INSTALL-STATUS.txt`,
  `fip-manifest.txt`, `kernel` (FIP slice) and `root` (rootfs UBI), then
  the stock `append-metadata`. The provenance marker is
  "BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED"; no factory image.

Measured geometry used by the recipe: kernel slot `8650752`, rootfs slot
`24313856`, page 2048, PEB 131072; load/entry `0x80608000`.

## 5. Build result (this lane, host-only)

The assembled tree was built in an OpenWrt main checkout at revision
`27663a017ad8ac6883e11a81015db0c0fc341a9e` with the OpenWrt GCC 14.4.0
toolchain and `linux-6.18.60`. A clean target rebuild
(`make target/linux/clean` then `make -j16 world`) completed **RC=0**
and produced the device image.

### 5.1 Artifacts (this lane, host-only - NOT flashed, NOT booted)

Final image: `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`
**6,267,199 B**, sha256 `b991f4c4c4b8aefacfd8abf6de075887ee16d04a4c24fc041c1028ada1438c48`.
The clean rebuild and the earlier warm build produced the **same image sha256**
(deterministic with the recipe's `SOURCE_DATE_EPOCH`); all hashes in
[validation/image-build/artifacts.sha256](validation/image-build/artifacts.sha256).

| artifact | size (B) | sha256 | what it proves |
| --- | --- | --- | --- |
| sysupgrade image | 6,267,199 | `b991f4c4...` | the container the recipe produces |
| kernel slice (`cudy_wr3000-v2-kernel.bin`) | 3,370,590 | `4f6c157d...` | within the 8,650,752 B kernel slot |
| FIP manifest | 945 | `18c142ca...` | gate magic `0xd7b7ab1e` at offset 0x336d38; load/ep `0x80608000`; base DTB 12,848 B; trailer 13,142 B; 1 overlay (230 B) |
| rootfs UBI (`cudy_wr3000-v2-rootfs.ubi`) | 2,883,584 | `cb27f649...` | within the 24,313,856 B rootfs slot; volumes `squashfs` (static, used_ebs 20) + `rootfs_data` (dynamic, empty, autoresize, reserved_pebs 9) |
| board DTB (`image-luofu-r116.dtb`) | 12,848 | `1c200daf...` | identical to the host cpp+dtc compile of the single-source DTS |
| loader-match overlay (`.dtbo`) | 230 | `8e4ab83b...` | the overlay embedded in the FIP trailer |

Image build logs: [validation/image-build/clean.log](validation/image-build/clean.log),
[validation/image-build/clean-world.log](validation/image-build/clean-world.log),
plus the first successful run [validation/image-build/warm-world.log](validation/image-build/warm-world.log).
Full provenance: [validation/image-build/build-config.txt](validation/image-build/build-config.txt).

### 5.2 Validation matrix (all host-only)

| check | command (scratch) | result | log |
| --- | --- | --- | --- |
| sources -> tree assembly | `assemble-target.sh --check/--manifest/--verify` | PASS; 85 files; manifest byte-identical to task-238 | [validation/assembly/](validation/assembly/) |
| board DTS compile | `cpp` + `dtc` (GCC 16.2.1, DTC v1.8.1) | RC=0, 0 warnings, DTB 12,848 B `1c200daf...` | [validation/dts/build-evidence.txt](validation/dts/build-evidence.txt) |
| board contract (24 checks) | `check-board-dts.py` on the compiled DTB | 24/24 PASS, 0 FAIL | [validation/dts/dts-check.log](validation/dts/dts-check.log) |
| clean target rebuild | `make target/linux/clean` | CLEAN-RC=0 | [validation/image-build/clean.log](validation/image-build/clean.log) |
| full image build | `make -j16 world` | WORLD-CLEAN-RC=0 | [validation/image-build/clean-world.log](validation/image-build/clean-world.log) |
| kernel slice structure | `fip-slice.py check --max-size 8650752` | PASS: uImage magic/load/ep/CRCs, trailer, base DTB, gate magic, slot budget | [validation/image-build/fip-slice-check.log](validation/image-build/fip-slice-check.log) |
| container read-back | `luofu-image.py check-container` | PASS: CONTROL, kernel, UBI volumes, slot budgets, NOT-FLASHABLE marker | [validation/image-build/container-check.log](validation/image-build/container-check.log) |
| container payload identity | `tar -xOf` + `cmp` against the build products | kernel and root are byte-identical to the build products | [validation/image-build/container-payload.txt](validation/image-build/container-payload.txt) |
| image DTB == single-source DTB | sha256 comparison | identical (12,848 B `1c200daf...`) | [validation/image-build/dtb-crosscheck.txt](validation/image-build/dtb-crosscheck.txt) |

**Image/install status carried by the container itself:**
"BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED" - an earlier build of
this recipe booted on the board (2026-11-19/21/24), but this exact rebuild has
not been flashed or booted; `platform_do_upgrade` has never run on a device,
and the baked command line pins `ubi.mtd=rootfsb` (pair B only), so no factory
image is generated.


## 6. What this proves, and what it does not

**Proves (host-only):** the target's single sources assemble into a complete
OpenWrt target tree (85 files, byte-identical to their sources); the target
builds an image from a clean target rebuild, not just a schematic; the image
carries a structurally validated kernel slice (gate magic, load/entry, slot
budget), a two-volume UBI rootfs and an internally consistent sysupgrade
container; the board DTB compiles clean and passes the 24-check board
contract; the image build reproduces the identical board DTB.

**Does not prove:** that this exact image boots (no boot of this rebuild;
the v4 active-DTS change is still un-boot-tested); that the install path
works (`platform_do_upgrade` has never run on a device; no factory image);
Wi-Fi (vendor-only, GPL-blocked) or native switch L2/VLAN forwarding
(separate project); upstream acceptance (no bindings review, no MAINTAINERS/
DCO identity, no PR); a pristine-clone build (the checkout is a long-lived lab
tree - build-config.txt records its local state).

**Honest build notes.** The first `make world` attempt failed in the copied
lab staging tree: 172 files under `staging_dir/host` carried absolute include
paths from another checkout, which broke the host `apk` build
(`endian.h` `include_next` chain). That is a tree-copy artifact, not a
target defect; retargeting those paths to this tree fixed it and the build then
completed. The clean rebuild runs the target kernel from source; package
staging caches survive from earlier lab builds (build-config.txt).

## 7. Remaining gaps

The target PR checklist, hardware gates and ordering are consolidated in
[PR-READINESS.md](PR-READINESS.md); the definitive per-item submission
checklist is [`../../upstream/SUBMISSION-STATUS.md`](../../upstream/SUBMISSION-STATUS.md)
(27 items: 5 DONE / 17 PARTIAL / 5 BLOCKED; the target PR split in section 8).

## 8. Reproduce

```sh
# 0. from this lane directory (opensource/docs/incoming/upstream2)

# 1. assemble the target into an OpenWrt main tree (single sources -> tree;
#    the script resolves the single source itself; --verify checks an existing tree)
../../upstream-dts/tools/assemble-target.sh --verify /path/to/openwrt
../../upstream-dts/tools/assemble-target.sh --apply  /path/to/openwrt

# 2. select the device and build (host-only; no device contact)
cd /path/to/openwrt
make defconfig            # CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2
make target/linux/clean
make -j16 world

# 3. capture the validation evidence into this lane
T=/path/to/openwrt /path/to/this/lane/validation/image-build/capture.sh

# 4. board DTS contract (host cpp+dtc compile + fdtget checks)
#    full command and inputs: validation/dts/build-evidence.txt
```

## 9. Relation to the previous lane (task-238)

`upstream-dts` (task-238) landed the assembler, the target manifest and the
DTS coverage document, but stopped before landing its own REPORT/SUMMARY, its
build evidence (`validation/image-build/`), or completing the build of the assembled
target (the files are still open in its README). This lane completes exactly
that hand-off and leaves the task-238 files untouched (write scope:
`opensource/docs/incoming/upstream2/`). All shared artefacts are referenced,
not duplicated.