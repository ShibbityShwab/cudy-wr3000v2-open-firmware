# Target-skeleton smoke test (2026-11-18)

**Status: structure accepted by OpenWrt main; nothing compiled, imaged, or booted.**

This page records the first host-side validation of [`target-skeleton/`](../target-skeleton/README.md)
against a real OpenWrt tree. It is not a build result and must not be cited as one.

## Environment

- OpenWrt `main`, shallow clone, commit `27663a0` (2026-11-18).
- Host: Linux x86_64, 16 cores, 820 GB free. No feeds were updated; no toolchain was built.
- Kernel sources downloaded by `make target/linux/prepare`: `linux-6.18.60.tar.xz` (148 MB).

## What was run

1. `git clone --depth 1 --single-branch --branch main https://github.com/openwrt/openwrt.git`
2. Copied the skeleton target directory to `target/linux/hisilicon/`, applying the layout
   adaptations listed below.
3. Seeded `.config` with:

   ```
   CONFIG_TARGET_hisilicon=y
   CONFIG_TARGET_hisilicon_luofu=y
   ```

4. `make defconfig` → **PASS** ("configuration written to .config", no target/image errors).
   It generated, among others:

   ```
   CONFIG_TARGET_hisilicon=y
   CONFIG_TARGET_hisilicon_luofu=y
   CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y
   CONFIG_TARGET_BOARD="hisilicon"
   CONFIG_TARGET_SUBTARGET="luofu"
   CONFIG_TARGET_PROFILE="DEVICE_cudy_wr3000-v2"
   ```

   (The package "has a dependency on ... which does not exist" warnings are feed-related and
   expected on a clone without `./scripts/feeds update`; no target/image warning was emitted.)

5. `make target/linux/prepare` → **RC=0**. The 6.18.60 kernel source was fetched, unpacked into
   `build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/`, and the
   target prepare step completed without error.

## Layout adaptations applied during the smoke (now reflected in the skeleton)

Main's image idiom differs from the first skeleton draft, so the published skeleton was
restructured to match the tree that accepted it:

| change | why |
| --- | --- |
| board-level `image/Makefile` (includes `image.mk`, `include $(SUBTARGET).mk`, `BuildImage`) | main includes the subtarget image file from the board image Makefile |
| subtarget image definitions moved to `image/luofu.mk` | same idiom as `target/linux/mediatek/image/*.mk` |
| `DEVICE_DTS_DIR := ../dts` + `dts/luofu-r116.dts` in the target | main's supported external-DTS mechanism |
| `base-files/etc/board.d/02_network` at board level | board-level base-files is the main idiom |
| `KERNEL_PATCHVER:=6.18` and a `luofu/config-6.18` placeholder | the tree's kernel version at the time of the test |
| profile stub renamed `.mk.unused` | main derives TARGET/DEVICE symbols from the image device stanza, not `profiles/` |

## What this proves

- The target directory **shape** is accepted by OpenWrt main's target/image metadata generation.
- The device stanza parses and produces the expected `CONFIG_TARGET_hisilicon_luofu_DEVICE_*`
  symbol; target/board/subtarget/profile selection works.
- `target/linux/prepare` completes for the target, so the tree can fetch/unpack the kernel with
  this target present.

## What this does **not** prove

- **No kernel compiled.** The kernel platform patch does not exist
  (`arch/arm/mach-hisilicon`, `ARCH_HISI`/`ARCH_LUOFU`, DTS Makefile entry, `TEXT_OFFSET`), so a
  kernel build would fail at the platform stage.
- `luofu/config-6.18` is a placeholder derived from the lab 5.10.201 tree, **not** a re-derived
  6.18 config; `make defconfig`/prepare do not validate it.
- **No image built.** The custom image steps (`luofu-uimage`, `fip-slice`, `fip-manifest`,
  `luofu-rootfs-ubi`, the sysupgrade wrapper) are schematics and are not referenced by the
  `IMAGE/` chain yet; no factory/install stanza exists.
- **No `dtbs_check`** on the draft DTS, and no boot on hardware.
- Trailer generation was **decoded and implemented separately on 2026-11-18**
  (`target-skeleton/docs/TRAILER-FORMAT.md`; `make-trailer` reproduces both vendor containers
  byte-for-byte), but this smoke test did not exercise the trailer path, and a novel generated
  trailer still needs one attended boot.
- No feeds, packages, or toolchain were built.

## Next blockers to expect when a real build is attempted

1. Kernel platform patch + machine descriptor + DTS Makefile entries (TARGET-DESIGN.md 4.1, 7.1).
2. Re-derived kernel config against 6.18 (TARGET-DESIGN.md 4.2).
3. Wiring the custom image chain to `fip-slice.py` and the UBI step; the trailer must come from a
   legitimate input, not fabrication (TARGET-DESIGN.md 5.2, 5.3).
4. `platform.sh`/sysupgrade metadata and the A/B selector/install path (TARGET-DESIGN.md 5.4, 5.5).
5. Switch driver (DSA vs switchdev) before `board.d` can assign any interface (TARGET-DESIGN.md 8.1).
