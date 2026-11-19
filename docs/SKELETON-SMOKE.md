# Target-skeleton smoke test (2026-11-18)

**Status: structure accepted by OpenWrt main; the kernel patch series (including the
2026-11-18 early-boot `TEXT_OFFSET` fix, patch `003`) applies; the config re-derives
with the tree's own OpenWrt GCC 14.4 toolchain; and `make target/linux/compile` now
completes RC=0, producing `arch/arm/boot/zImage` with the correct `0x80608000`
load/entry address. A first FIP kernel slice was then assembled from our own DTB +
overlay + uImage and structurally validated (fourth session below). The **real drivers**
added so far - CRG clock/reset controller (`004`), watchdog (`005`), and the
network trio MDIO bus (`007`), Genius PHY (`008`) and a **partial** GEMAC
(`009`), with device-tree bindings - compile into the same kernel (fifth and sixth
sessions below); they have **never run on hardware**, and the GEMAC has no data path
(its DTS nodes stay disabled). Nothing has been booted: no rootfs, no sysupgrade
container, no install path, no boot, and the rest of the driver series does not
exist.**

This page records the host-side validation of [`target-skeleton/`](../target-skeleton/README.md)
against a real OpenWrt tree (six sessions on 2026-11-18: the target-layout smoke test
below, the kernel patch/config session, the `TEXT_OFFSET` + full-kernel-compile
session, the first own-artifact FIP kernel slice, the first driver series, and the
network trio appended at the end). It is not a target build result and must not be cited as one: the build
products it records are the kernel compile product, a structurally validated kernel
slice and the two driver objects linked into that kernel - no rootfs, no sysupgrade
container, no install image and no boot.

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

(Items 1-2 above were addressed at draft level in the later 2026-11-18 session below;
items 3-5 remain.)

---

## 2026-11-18 (later session): kernel patch series + config re-derivation + compile stage

**Status: the series applies cleanly in the real tree; the config re-derives with the
tree's own OpenWrt GCC 14.4.0 toolchain (idempotent); the new machine code compiles with
that toolchain; the full kernel build is blocked by ONE platform-patch defect (the
non-immediate `TEXT_OFFSET`). No image, no boot. Publication NOT READY.**

> **Update (third session, same day, below):** the compile blocker described in this
> section (the non-immediate `TEXT_OFFSET`) is fixed by patch `003`; the full kernel
> compile now reaches RC=0 and produces `zImage` with load/entry `0x80608000`. Every
> other statement in this section (config derivation, patch application, no image, no
> boot) still stands. See the third-session section at the end.

Second host-side session against the same clone: `openwrt main` `27663a0`,
`linux-6.18.60`, no feeds updated, no device. The first pass derived the config with a
cross-compiler override before the tree's toolchain existed; the canonical top-level
`make kernel_oldconfig` was then run to completion (toolchain built from source) and
the compile stage was repeated with the real compiler. Logs and artifacts are local-only
under `build/linux-kbuild/waves/upstream-6.18-patches-20261118/` (gitignored; sha256
list inside). None of this is a build result for the target: the target's image recipe,
drivers and userspace do not exist.

### What was added to the skeleton

| artifact | sha256 |
| --- | --- |
| `target/linux/hisilicon/patches-6.18/001-arm-mach-hisilicon-luofu.patch` | `996bf896e6191d24e6b1e8e4456112e7ce985963e1d40cf37cb5ff8f10688d1d` |
| `target/linux/hisilicon/patches-6.18/002-arm-kconfig-textofs.patch` | `278bb1a01427b31dc79d02b534e4cc4fae9e418698dad16afeea26a40af95796` |
| `target/linux/hisilicon/luofu/config-6.18` (re-derived with the tree's GCC 14.4 toolchain, annotated) | `2fdf4aea7a8074137d3e949684f17fc24155c9a7d10765cae8fd3b5d74057731` |

- `001`: new `arch/arm/mach-hisilicon/{Kconfig,Makefile,luofu.c}`. `ARCH_LUOFU`
  depends on the existing upstream `ARCH_HISI` (it is not redefined); the machine
  descriptor sets only `dt_compat` (`cudy,wr3000-v2`, `hisilicon,luofu-r116`,
  `hisilicon,luofu`) and the L2 AUX replay (`0x430001`, mask `~0`, with a TODO to
  verify the vendor mask `0xffb0fffe` and decode the latency fields on hardware).
  `map_io`/`init_early`/`init_time`/`init_machine` are intentionally not set (DT
  platform defaults; `drivers/of/platform.c` populates the DT platform devices);
  `.smp` and `.restart` are documented TODOs (UP-only first submission; no reboot
  driver). No bench breadcrumbs / vendor symbol stubs / vendor `dt_compat` from the
  lab `mach-luofu/luofu.c`.
- `002`: `source "arch/arm/mach-hisilicon/Kconfig"` in `arch/arm/Kconfig`,
  `machine-$(CONFIG_ARCH_LUOFU) += hisilicon` and
  `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000` in `arch/arm/Makefile`.
- `003-arm-dts-makefile.patch` was **not** created on purpose: the board DTS is
  delivered by the target's `dts/` + `DEVICE_DTS_DIR := ../dts` mechanism; adding a
  second copy under `arch/arm/boot/dts` would be the hand-maintained duplicate the
  design forbids (TARGET-DESIGN.md section 6.1).
- `luofu/config-6.18`: the 5.10 copy was replaced by the re-derived 6.18 delta
  (derivation and provenance below). `config-5.10` stays as the 5.10 reference.

### Environment shims needed (host-only, outside the repository, reversible)

The clone has no OpenWrt toolchain and no feeds; the earlier `make defconfig` run had
not built host tools. Three local shims were required. They are **not** part of the
deliverable, but without them the tree's own stages cannot start:

1. `staging_dir/host/bin/{m4,flex,bison}` symlinked to the system tools. Without them
   the kernel kconfig build dies with
   `flex: fatal internal error, exec of .../staging_dir/host/bin/m4 failed`.
2. `staging_dir/host/bin/bison` replaced with a wrapper that runs the system
   `/usr/bin/bison` with `BISON_PKGDATADIR=/usr/share/bison`; the plain symlink fails
   with `bison: .../share/bison/m4sugar/m4sugar.m4: cannot open: No such file or
   directory` because bison resolves its data dir from its own path.
3. cross-compiler override on the make command line,
   `KERNEL_CROSS=<repo>/build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-`,
   `KERNEL_CC=...-gcc` (Arm GNU Toolchain 11.3). Without it:
   `scripts/Kconfig.include:40: C compiler 'arm-openwrt-linux-muslgnueabi-gcc' not
   found`. The tree's own toolchain is gcc-14.4.0 (visible in the `KCFLAGS` of every
   OpenWrt log line) and was not built in the first pass; the canonical top-level
   `make kernel_oldconfig` starts by building the host tools and the toolchain, and the
   first bounded attempt was cut off at the host-tools stage (RC=124, evidence log
   `13-kernel_oldconfig-toplevel.log`). That attempt was later completed: see the
   canonical re-derivation in Stage 2 and the canonical compile rerun in Stage 3.

### Stage 1 - patch application: PASS

Copied the repository `target/linux/hisilicon/` to the clone, added the target DTS as
`target/linux/hisilicon/dts/luofu-r116.dts` (the existing smoke-test layout), and ran:

```
$ cd /home/shibbityshwab/openwrt-lab
$ make target/linux/prepare
...
time: target/linux/prereq#0.04#0.03#0.07
RC=0
```

Result: `RC=0`; the tree was re-extracted and patched
(`build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60`), no
`.rej`/`.orig` files, and the patched tree contains:

```
arch/arm/mach-hisilicon/Kconfig   (ARCH_LUOFU)
arch/arm/mach-hisilicon/Makefile  (obj-y += luofu.o)
arch/arm/mach-hisilicon/luofu.c   (DT_MACHINE_START LUOFU)
arch/arm/Kconfig:396              source "arch/arm/mach-hisilicon/Kconfig"
arch/arm/Makefile:175             textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000
arch/arm/Makefile:200             machine-$(CONFIG_ARCH_LUOFU) += hisilicon
```

Both patches also pass `patch -p1 --dry-run` against the pristine 6.18.60 source
(`checking file ...`, exit 0). Caveat: the first prepare invocation that added the
patch directory printed only the outer make lines; the resulting tree was verified
file-by-file instead (the `.prepared` stamp name changed from `5a37066a...` to
`a3192102...`, i.e. the kernel file-digest dependency forced the re-extract).

### Stage 2 - kernel config derivation: PASS, after fixing the placeholder

Command (the tree's own path, non-interactive preset on stdin):

```
$ cd openwrt-lab
$ yes '' | make -C target/linux TOPDIR=$PWD oldconfig V=s \
      KERNEL_CROSS=<repo>/build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf- \
      KERNEL_CC=<...>-gcc
...
/home/shibbityshwab/openwrt-lab/scripts/kconfig.pl - '>' \
    target/linux/generic/config-6.18 \
    build_dir/.../linux-6.18.60/.config target/linux/generic/config-filter \
    > target/linux/hisilicon/luofu/config-6.18
RC=0
```

That is the OpenWrt path exactly: `scripts/kconfig.pl + generic target` merge, kernel
`make oldconfig`, then `scripts/kconfig.pl - '>'` diff against the generic config
(with `generic/config-filter`) written back to the subtarget config.

**Placeholder bug found first (why the old `config-6.18` was wrong):** with the
5.10-derived reference as input, oldconfig *succeeds* but silently loses the platform:
`# CONFIG_ARCH_MULTI_V7 is not set` (the reference only carried lab values, not the
ARM platform selection), so `ARCH_MULTI_CPU_AUTO` falls back to ARMv5 and the delta
contains `CONFIG_CPU_ARM926T=y`, big-endian, no `ARCH_HISI`/`ARCH_LUOFU`. Evidence:
`05-baseline-derived-config-6.18` in the evidence directory. This is the proof the
previous placeholder did **not** validate a 6.18 build.

**Fix + re-derivation:** the user-selectable symbols the local lab 5.10.201 `.config`
has were restored before the derivation (`ARCH_MULTIPLATFORM=y`, `ARCH_MULTI_V7=y`,
`ARM_THUMB=y`, `VFP=y`, `VFPv3=y`; `CPU_LITTLE_ENDIAN` resolved from the choice).
Re-running the same command gave `RC=0` with:

```
CONFIG_ARCH_HISI=y
CONFIG_ARCH_LUOFU=y
CONFIG_ARCH_MULTI_V7=y
CONFIG_CPU_V7=y
CONFIG_CPU_LITTLE_ENDIAN=y
CONFIG_SMP=y
CONFIG_NR_CPUS=2
CONFIG_VFP=y
CONFIG_VFPv3=y
```

A second run with the derived file as input reproduced **byte-identical** output and a
byte-identical kernel `.config` (`CONFIG_DERIVATION_IDEMPOTENT`,
`KCONFIG_IDEMPOTENT`). The final committed file is the machine-derived delta plus
comments; `scripts/kconfig.pl` merges it to exactly the same map as the un-annotated
delta with no warnings (`ANNOTATED_CONFIG_MERGES_IDENTICALLY`).

**Provenance of the delta** (marked inline in the file; the first-pass version had 282
symbols, the committed canonical one has 281 - see above):

- 6 user-selectable symbols restored before derivation (**[P]**); 46 symbols whose value
  is carried from the lab 5.10.201 reference (**[L]**); the rest are what 6.18
  oldconfig/select-default resolution produced (not a design choice).
- 22 symbols in the 5.10 reference are **not** in the delta because they equal the
  generic config (`CONFIG_MTD`, `CONFIG_NET`, `CONFIG_SERIAL_8250`,
  `CONFIG_JFFS2_FS`, `CONFIG_SQUASHFS`, ... - the delta format omits generic values).
- 13 reference symbols were **dropped** by 6.18: 9 do not exist in the upstream tree at
  all (`CONFIG_LUOFU_WATCHDOG`, `CONFIG_LUOFU_GMAC`, `CONFIG_LUOFU_MDIO`,
  `CONFIG_LUOFU_PHY`, `CONFIG_MTD_NAND_LUOFU`, `CONFIG_MDIO_DEVICE`,
  `CONFIG_MDIO_DEVRES`, `CONFIG_MTD_SPI_NAND`, `CONFIG_PCI_MSI_IRQ_DOMAIN` - the
  drivers still have to be written), and 4 exist but are hidden/select-only in 6.18
  (`CONFIG_I2C_DESIGNWARE_PLATFORM`, `CONFIG_MTD_NAND_CORE`, `CONFIG_PCIE_DW`,
  `CONFIG_PCIE_DW_HOST`), so they are only enabled by a driver that selects them.
- `CONFIG_PHYLINK` 5.10 y -> 6.18 m: it is a promptless tristate selected by its users
  (here `NET_DSA=m`); y was never user-settable.
- 12 `[D-default]` symbols (other Hisilicon SoCs' clock/reset/thermal drivers) are on
  only because `ARCH_HISI=y` has `default ARCH_HISI` in their Kconfig entries. They
  should be pruned or the defaults tightened before submission.

**Canonical re-derivation (same session, after the toolchain build):** the
top-level, tree-native command was then run to completion -

```
$ cd /home/shibbityshwab/openwrt-lab
$ yes '' | timeout 1800 make kernel_oldconfig -j16
...
RC=0
```

It first built the OpenWrt host tools and the tree's own toolchain from source
(`arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0 toolchain-c0e63334e4) 14.4.0`,
~13.5 minutes wall clock), then ran the same `target/linux oldconfig` path with that
toolchain and wrote `luofu/config-6.18`. That canonical delta has **281 symbols** and
contains the same platform selection (the 282-symbol first-pass delta differs only by
`CONFIG_GCC_ASM_GOTO_OUTPUT_BROKEN`, a compiler-feature symbol that GCC 14.4 does not
set). A second canonical run reproduced `luofu/config-6.18` **byte-identically**
(`CANONICAL_DERIVATION_IDEMPOTENT`), and the committed annotated file merges through
`scripts/kconfig.pl` to exactly the same config as the un-annotated delta
(`ANNOTATED_CONFIG_MERGES_IDENTICALLY`, no warnings). **The committed
`config-6.18` is this canonical delta.** Evidence: logs `14-`, `15-`, `16-`, `17-`
in the evidence directory.

**Not proven by this stage:** the config boots nothing, and the compile stage below
still fails on one platform-patch defect.

### Stage 3 - compile stage: FAIL, on one platform-patch blocker

**First attempt (before the OpenWrt toolchain existed; bench GCC 11.3 override):**

```
$ cd openwrt-lab
$ make target/linux/compile V=s \
      KERNEL_CROSS=<repo>/build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf- \
      KERNEL_CC=<...>-gcc -j16
...
RC=2
```

The OpenWrt configure step inside compile completed (headers_install, .config from
`kconfig.pl`, vermagic) and the kernel build started. Two independent failures were
seen in this first attempt:

**Blocker 1 - the platform patch's `TEXT_OFFSET` value is not an ARM immediate
(patch-specific, must be fixed before the series can build a kernel):**

```
AS      arch/arm/kernel/head.o
arch/arm/kernel/head.S:123: Error: invalid constant (608000) after fixup
arch/arm/kernel/head.S:188: Error: invalid constant (608000) after fixup
make[8]: *** [scripts/Makefile.build:430: arch/arm/kernel/head.o] Error 1
```

`0x00608000` needs bits 15 and 21/22 set, which cannot be encoded in the rotated 8-bit
immediate of `sub r8, r8, #TEXT_OFFSET` (head.S:123) or of the `pgtbl` macro's
`add \rd, \phys, #TEXT_OFFSET` (head.S:68, expanded at :188). The decompressor has the
same problem at `arch/arm/boot/compressed/head.S:312` (`add r4, r0, #TEXT_OFFSET`) and
`:410` (`sub r0, r4, #TEXT_OFFSET`). The value is fixed by the load-address
derivation (the stock loader stages the slot at `0x83200000` and the kernel must land
at `0x80608000`; TARGET-DESIGN.md sections 3.2/5.2), so it cannot simply be changed to
an encodable value. The lab tree patched these files as bench edits (TARGET-DESIGN.md
section 7.2 lists them as edits that must be reworked). At the time of this session the
clean core patch had not been written; it was written later the same day as patch `003`
(third session below), using `mov_l` (portable constant materialization) at all four
sites. Without it the failure blocks the whole kernel build, not just the luofu machine.

**Blocker 2 - GNU GCC 11.3 vs `retain` + `CONFIG_WERROR` (environment, not the
patch):**

```
net/bpf/test_run.c:511:1: error: 'retain' attribute ignored [-Werror=attributes]
net/core/filter.c:12084:1: error: 'retain' attribute ignored [-Werror=attributes]
kernel/bpf/helpers.c:2207:1: error: 'retain' attribute ignored [-Werror=attributes]
mm/slab_common.c:1310:1: error: 'retain' attribute ignored [-Werror=attributes]
kernel/cgroup/rstat.c:130:1: error: 'retain' attribute ignored [-Werror=attributes]
... 94 occurrences
RC=2
```

GCC 11.3 answers `__has_attribute(__retain__)` with true but ignores the attribute at
codegen. 6.18 defines `__retain` when `CONFIG_LD_DEAD_CODE_DATA_ELIMINATION` or
`CONFIG_LTO_CLANG` is set; the OpenWrt generic config sets
`CONFIG_LD_DEAD_CODE_DATA_ELIMINATION=y` and `CONFIG_WERROR=y`, so the warning becomes
an error. This was an environment mismatch, not a repository problem.

**Canonical rerun with the tree's own OpenWrt GCC 14.4.0 toolchain:**

```
$ cd openwrt-lab
$ make target/linux/compile V=s -j16
...
  AS      arch/arm/kernel/head.o
arch/arm/kernel/head.S:123: Error: invalid constant (608000) after fixup
arch/arm/kernel/head.S:188: Error: invalid constant (608000) after fixup
make[8]: *** [scripts/Makefile.build:430: arch/arm/kernel/head.o] Error 1
...
RC=2
```

Result: **0** `retain` errors (blocker 2 is gone with the supported compiler, exactly
as expected); the **only** remaining compile failure is blocker 1. The machine object
`arch/arm/mach-hisilicon/luofu.o` was **compiled with GCC 14.4**
(`6028 bytes`, `__mach_desc_LUOFU` + `"HiSilicon Hi5671Y (luofu)"` + the three
`dt_compat` strings, no warning/error attributed to the new files), so kbuild accepts
the machine descriptor under the canonical config; `head.S` stops the image.
Evidence: `18-compile-stage-canonical-toolchain.log`.

**Evidence-only build (local workarounds, NOT delivered):** to separate blocker 1 from
the rest of the patch/config, the build was rerun with (a) `CONFIG_WERROR` turned off
**in the build tree's kernel `.config` only`** (needed only for the bench GCC 11.3
pass) and (b) a make-command-line override `TEXT_OFFSET=0x00008000` (encodable, wrong
load address) instead of the platform value:

```
$ make -C build_dir/.../linux-6.18.60 ARCH=arm CROSS_COMPILE=<...>- \
      TEXT_OFFSET=0x00008000 -j16 zImage
...
  OBJCOPY arch/arm/boot/zImage
  Kernel: arch/arm/boot/zImage is ready
RC=0
```

Result: `arch/arm/boot/zImage`, 3,205,816 bytes, sha256
`9e79d5914573beaf7878bdb1c25ece36dfa8afbf1388264956b9c7810d50461d`. **This is not a
deliverable image and proves no boot**: it has the wrong TEXT_OFFSET (the real one must
be `0x00608000`), no FIP trailer, no rootfs, it was built with the bench GCC 11.3
rather than OpenWrt's toolchain, and nothing was written to the router. It only shows
that the patch/config do not break the rest of the kernel build.

### What this proves, and what it does not

Proven (host-only, no device): the patch series applies cleanly in the OpenWrt 6.18
kernel tree; the Kconfig integration selects `ARCH_LUOFU`; the config derivation
through the tree's own top-level `make kernel_oldconfig` with the tree's own OpenWrt
GCC 14.4.0 toolchain is reproducible and idempotent; the cleaned machine descriptor
compiles with that toolchain; and the kernel compiles up to `arch/arm/kernel/head.o`
with only the `TEXT_OFFSET` defect stopping it (an evidence-only zImage was also built
with local workarounds, not a deliverable).

Not proven: a kernel image with the correct text offset/load address, no image, no FIP
trailer in a slice, no rootfs, no `dtbs_check`/binding review, no
`platform.sh`/sysupgrade, no A/B selector protocol, no SMP (UP-only), no restart
driver, no boot, no device interaction, no driver series. Publication is still NOT
READY.

### Remaining blockers

1. `TEXT_OFFSET=0x00608000` non-immediate core patch (4 assembly sites; see Stage 3) -
   the only compile blocker for the kernel with this platform textofs. **FIXED in the
   third session below** by `patches-6.18/003-arm-textofs-nonimmediate.patch` (mov_l);
   upstream review of that patch is still pending.
2. Driver + dt-binding series (watchdog, CRG, thermal, pinctrl, FMC, MDIO/PHY/GEMAC,
   switch, PCIe) - nothing exists; hidden 6.18 symbols must be selected by the new
   drivers.
3. Prune/tighten the 12 `[D-default]` other-SoC Hisilicon drivers (or decide to keep
   them).
4. Image recipe/trailer/sysupgrade/install path and the A/B selector (TARGET-DESIGN.md
   5.2-5.5), `dtbs_check`, and then one attended boot - none attempted here.

---

## 2026-11-18 (third session): early-boot `TEXT_OFFSET` fix + full kernel compile

**Status: the kernel compiles end-to-end. With the new core patch `003`, the canonical
OpenWrt stages (`make target/linux/prepare`, `make target/linux/compile -j16`) reach
RC=0 using the tree's own OpenWrt GCC 14.4.0 toolchain, `arch/arm/boot/zImage` is
produced (3,205,200 B), and the built early boot code carries the real
`TEXT_OFFSET = 0x00608000` / load address `0x80608000` with no make-command-line
override. **No image, no FIP trailer, no rootfs, no boot, no driver series. Publication
NOT READY.** Host-only, same clone (`openwrt main` `27663a0`, `linux-6.18.60`);
evidence logs are local-only under
`build/linux-kbuild/waves/upstream-6.18-textoffset-20261118/` and this repository
contains none of them.

### The defect (recap from Stage 3 / blocker 1)

`#TEXT_OFFSET` is used as an ARM immediate in four places:
`arch/arm/kernel/head.S:123` (`sub r8, r8, #TEXT_OFFSET`, PHYS_OFFSET recovery),
`arch/arm/kernel/head.S:68` (the `pgtbl` macro's `add \rd, \phys, #TEXT_OFFSET`,
expanded at `:188`) and `arch/arm/boot/compressed/head.S:312` / `:410` (the
decompressor's final-kernel-address computation and the ATAG-to-DTB fallback). ARM
immediates are an 8-bit value rotated by an even number of bits; `0x00608000` (bits 22,
21 and 15 set) is not encodable, so the assembler reports
`invalid constant (608000) after fixup`. The value itself is required by the image
design: the stock loader stages the slot at `0x83200000`, and
`(0x83200000 & 0xf8000000) + 0x00608000 = 0x80608000`, the bench-proven kernel
load/entry address (TARGET-DESIGN.md 3.2; skeleton README 3.4).

### Chosen approach (option (a)): make the offset work in the boot code

New patch `target/linux/hisilicon/patches-6.18/003-arm-textofs-nonimmediate.patch`,
sha256 `407352b8aced76da6c6f1fcef53c77bca0221473515c144126cbe3358e707f5c`. It replaces
each immediate with the kernel's own portable constant-move macro `mov_l`
(`arch/arm/include/asm/assembler.h`): `movw`+`movt` on ARMv7 (no literal pool, no
relocation), `ldr dst, =imm` on older CPUs. `mov_l` is already used in
`arch/arm/kernel/head.S` itself (`secondary_startup`), so no new abstraction is
introduced. The alternative (option (b)) - changing `textofs` to an encodable value
such as `0x00800000` (load `0x80800000`) - was rejected on evidence: it would
invalidate the only bench-derived kernel-slot address identity and require a new
attended boot to re-establish the address story, while option (a) keeps the proven
`0x80608000` contract and changes nothing loader-facing. Patch `002` and
`image/luofu.mk` are therefore unchanged.

### Exact commands and pass/fail

Baseline (before the fix) reproduced in the same clone:

```
$ cd /home/shibbityshwab/openwrt-lab
$ make target/linux/compile V=s -j16
  AS      arch/arm/kernel/head.o
arch/arm/kernel/head.S:123: Error: invalid constant (608000) after fixup
arch/arm/kernel/head.S:188: Error: invalid constant (608000) after fixup
...
RC=2
```

After copying the three patches and the config into the clone target directory, the
kernel build tree was removed (clean build, no stale workaround objects) and:

```
$ make target/linux/prepare
RC=0                                    # no .rej/.orig; patch 003 applied
$ make target/linux/compile V=s -j16
...
  AS      arch/arm/kernel/head.o
  AS      arch/arm/boot/compressed/head.o
  LD      arch/arm/boot/compressed/vmlinux
  OBJCOPY arch/arm/boot/zImage
  Kernel: arch/arm/boot/zImage is ready
time: target/linux/compile#1179.62#72.37#104.27
RC=0
```

- toolchain used: `arm-openwrt-linux-muslgnueabi-gcc (OpenWrt GCC 14.4.0
  toolchain-c0e63334e4) 14.4.0` (the tree's own).
- `CONFIG_WERROR=y` in the kernel `.config` - no WERROR workaround.
- both `.head.o.cmd` carry `-DTEXT_OFFSET=0x00608000` from patch `002`; no
  `TEXT_OFFSET=` or `KERNEL_CROSS=` make override was used.
- `arch/arm/boot/zImage`: **3,205,200 bytes**, sha256
  `45ad00d502ed7b2386f83db794e9015267f4f9d66de8d0deaab8aca24cf18d10`.
- an incremental relink of the same tree reproduced the same zImage byte-for-byte.

### Address story verified from the built artifacts

- `System.map`: `_text = stext = 0xc0608000`, `swapper_pg_dir = 0xc0604000`,
  `_stext = 0xc0700000`; `CONFIG_PAGE_OFFSET=0xC0000000`.
- the four patched sites in the linked code are `movw/movt` pairs:
  - `vmlinux 0xc0608050`: `movw r5,#0x8000; movt r5,#0x60; sub r8,r8,r5`, preceded
    by the PC-relative `adr_l r8,_text`, so at run time r8 is the physical `_text`
    and the subtraction yields PHYS_OFFSET;
  - `vmlinux 0xc060808c` (`__create_page_tables`): `movw r4,#0x8000;
    movt r4,#0x60; add r4,r4,r8; sub r4,r4,#0x4000`, so the initial page tables land
    0x4000 below the final kernel address;
  - decompressor `0xd4`: `movw r4,#0x8000; movt r4,#0x60; add r4,r0,r4` with
    `r0 = pc & 0xf8000000` - precisely the `(pc & 0xf8000000) + TEXT_OFFSET`
    computation;
  - decompressor `0x18c`: `movw r0,#0x8000; movt r0,#0x60; sub r0,r4,r0`.
- the built zImage's own `.table` (file offset 0x69ec) carries `0x00608000` as its
  TEXT_OFFSET field.
- derivation check: `0x83200000 & 0xf8000000 = 0x80000000`; `+ 0x00608000` =
  **0x80608000** = physical `_text` (`0xc0608000 - 0xc0000000 + 0x80000000`).
- therefore the image recipe's values are unchanged:
  `mkimage -A arm -O linux -T kernel -C none -a 0x80608000 -e 0x80608000`, with
  `ih_load == ih_ep == (loader_stage & 0xf8000000) + TEXT_OFFSET == 0x80608000`.
- portability check: re-assembling the same patched sources with
  `-D__LINUX_ARM_ARCH__=6` (the `ldr` literal branch of `mov_l`) also assembles and
  pools the new `0x00608000` literal in the existing literal pools. The target config
  (`CONFIG_CPU_V7`) takes the `movw/movt` branch.

### What this proves and what it does not

Proven (host-only): the patch series plus the canonical config build the whole 6.18
kernel with OpenWrt's own GCC 14.4.0 toolchain and with the real, non-encodable
`TEXT_OFFSET`; the produced boot code materializes `0x00608000` correctly and the
load/entry derivation is `0x80608000`; there is no locally applied build workaround.

Still not proven (and not attempted): no target image, no FIP trailer, no rootfs, no
sysupgrade container, no `platform.sh`, no `dtbs_check`, no SMP, no restart driver,
no boot, no device interaction, no driver series. The zImage is an intermediate
`target/linux/compile` product, not a deliverable image, and it has never been loaded
on hardware. Publication remains NOT READY; patch `003` has not been reviewed upstream.

### Remaining blockers (updated)

1. Driver + dt-binding series (watchdog, CRG, thermal, pinctrl, FMC, MDIO/PHY/GEMAC,
   switch, PCIe) - nothing exists.
2. Image recipe wiring: FIP trailer generation into the slice, UBI rootfs, sysupgrade
   wrapper/manifest, `platform.sh` + A/B selector (TARGET-DESIGN.md 5.2-5.5).
3. `dtbs_check`/binding review of the draft DTS, and prune/tighten the 12
   `[D-default]` other-SoC Hisilicon drivers.
4. Upstream review/submission of patches `001`-`003`, then one attended boot of a
   full image.

Evidence: local-only `build/linux-kbuild/waves/upstream-6.18-textoffset-20261118/`
(`00-README.md` indexes the baseline/fix logs, disassembly, hashes and the derivation);
it is gitignored and none of it is in this repository.

---

## 2026-11-18 (fourth session): first own-artifact FIP kernel slice (host-only)

**Status: for the first time a complete FIP kernel slice exists as a host artifact, built
from our own sources: the 6.18.60 zImage wrapped in the legacy uImage, followed by a trailer
generated from our draft board DTB and a minimal loader-match overlay. It satisfies every
structural check the decoded stock `bootfip` performs and it is byte-reproducible. It has
NEVER been booted; one attended boot on a recovered unit is still required before the image
recipe may rely on a novel trailer. No rootfs, no sysupgrade container, no install path, no
appended DTB, no driver series. Publication NOT READY.**

No router, UART, TFTP, flash or bench tool was used. No vendor dump, trailer, DTB, DTBO,
U-Boot image or vendor byte is an input; every input is either a repository DTS source
(`target-skeleton/dts/luofu-r116.dts`, `.../luofu-r116-overlay.dts`), the kernel build
product (`arch/arm/boot/zImage`) or a host tool from the OpenWrt build tree. The repeated
strings and numbers below (`hsan`, `luofu`, `board_id 3`, `0xd7b7ab1e`, the load
address) are decoded structural facts documented in
[`../target-skeleton/docs/TRAILER-FORMAT.md`](../target-skeleton/docs/TRAILER-FORMAT.md)
and TARGET-DESIGN.md 3.2/5.2, not copied vendor payload.

### What was built

Produced in one scripted run (the script is `target-skeleton/tools/build-kernel-slice.sh`,
section "Exact commands" below). Hashes are the run-1 values; a second clean run in a
different directory reproduced every byte (section "Reproducibility").

| artifact | size | sha256 | role |
| --- | --- | --- | --- |
| `luofu-r116.dtb` | 8,984 B | `2f97313a03a73ad64d40314f6fb1484a2d6db967aaa7cb5a50c96a531c883bb7` | our draft base DTB; first member of the trailer |
| `luofu-r116-overlay.dtbo` | 230 B | `8e4ab83b575dc14d9f3131dc01d1cbdccc63b38f8f8c4fa774da0d1f200b23b6` | minimal loader-match overlay (`vendor_id=hsan`, compatible `hsan-luofu`, `board_id=3`, one no-op fragment targeting `/`) |
| `uImage` | 3,205,264 B | `77406f1de3e11379942aaaf8d056d9c0aa70c0ac857f179266b8c5a32ec0124e` | legacy uImage: 0x40 header + 3,205,200 B zImage; `-A arm -O linux -T kernel -C none -a 0x80608000 -e 0x80608000 -n "Linux-6.18.60"` |
| `fip-trailer.bin` | 9,278 B | `aaf45c10a138bfb83c72b5234d6365781e6dcd56143cbadc491190de5c328c2f` | base DTB 8,984 B + 64 B descriptor (32-byte header + one 32-byte entry) + 230 B overlay; container total 294 B |
| `luofu-kernel-slice.bin` | 3,214,542 B | `3311a3e560c0b3c3af8b68c206a0e3b2e943ed0f754185afc1e82993b120d143` | uImage + trailer; gate `0xd7b7ab1e` at slice `0x310ba8`; 5,436,210 B under the 8,650,752 B slot budget |
| `fip-manifest.txt` | 901 B | `b244088c13eb8fed606d8955d7892d05632b0993025b471d2f67388314980fb7` | `fip-slice.py manifest` output (schema not frozen) |

The generated trailer is **not** the fixed 40,732-byte bench-lane artifact: 8,984 + 64 + 230 =
9,278 B. The loader does not require the fixed length (TRAILER-FORMAT.md sections 7 and 9);
40,732 is a fire-preflight convention, so this slice cannot go through the current bench fire
lane unchanged (open question 4 below).

### DTS compilation: no source change was required

The draft base DTS compiled unchanged - no TODO placeholder had to be commented out or
removed, no binding was invented. Exactly one dtc warning is emitted, the already-documented
`unique_unit_address` collision between `/clock-reset-controller@14880000` and
`/watchdog@14880000` (the vendor watchdog shares the CRG page; README section 6):

```
cpp -nostdinc -I<kernel>/include -I<kernel>/arch/arm/boot/dts -undef -D__DTS__ \
    -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
<kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
opensource/target-skeleton/dts/luofu-r116.dts:248.39-260.4: Warning (unique_unit_address):
    /clock-reset-controller@14880000: duplicate unit-address (also used in node /watchdog@14880000)
```

Tools: the build tree's own `scripts/dtc/dtc` (DTC 1.7.2-g52f07dcc) and the host
`cpp` (GCC 16.2.1); `cpp`'s include paths are the clone's kernel tree. The script asserts
the FDT header (magic, totalsize == file size), the root compatible
(`["hisilicon,luofu-r116", "hisilicon,luofu"]`) and the root model. The same assertions pass
under the independent system dtc 1.8.1/`fdtget` (evidence 03).

### The overlay is a loader-match stub for our own base DTB

`dts/luofu-r116-overlay.dts` is **not** a vendor DTBO and embeds no vendor bytes. It exists
only because the decoded applier refuses to hand over to the kernel unless the first matching
overlay applies successfully. The decoded rule (TRAILER-FORMAT.md section 6) is mirrored
exactly:

- root `vendor_id = "hsan"` - the applier does `strcmp` against the stock environment's
  `vendorid` (stock: `hsan`);
- root `compatible = "hsan-luofu"` - the match helper does
  `strstr(compatible, family_token)` against its 11-row family table; this board's row is
  `luofu`/`hsan`, and `hsan-luofu` is the decoded vendor family string for that row;
- root `board_id = <3>` - matched against the environment `boardid` if set, otherwise
  against the 4-bit board strap. **Evidence for 3:** the vendor trailer carries a
  board_id=0 reference overlay and a board_id=3 R116 overlay, and the stock loader selected
  the board_id=3 overlay on this unit (the live DT carries the R116 model; TRAILER-FORMAT.md
  section 6). The physical strap-bit reading is an inference [I] from that selection; the
  attended boot doubles as its confirmation, because if the strap were not 3 the applier would
  find no match and `bootfip` would refuse to boot the slot;
- one `fragment@0` with `target-path = "/"` and an intentionally empty `__overlay__`.
  Our base DTB is already the complete board tree for our kernel; the empty fragment adds
  nothing and therefore needs no growth of the base DTB, which is the least demanding case for
  the applier's libfdt buffers. Host libfdt applies it and the applied tree is byte-identical
  to the base (evidence 03).

The overlay DTS is a draft for the same in-tree location as the base DTS
(`target/linux/hisilicon/dts/`); it becomes a real overlay only when an overlay has a real
board delta (and binding) to carry.

### Exact commands

Delivered script, run from a clean output directory (it refuses a non-empty directory without
`--force`, refuses missing inputs and any vendor-derived input path, and defaults to the lab
clone unless `--openwrt-dir`/`OPENWRT_DIR` is given):

```
cd /home/shibbityshwab/Documents/GitHub/cudy-wr3000v2
opensource/target-skeleton/tools/build-kernel-slice.sh \
    --out build/linux-kbuild/waves/upstream-6.18-slice-20261118/run1
```

Internally (per-step logs in the output's `logs/`):

```
cpp ... luofu-r116.dts                       -> luofu-r116.dts.preprocessed
<kernel>/scripts/dtc/dtc                     -> luofu-r116.dtb   (8,984 B)
<kernel>/scripts/dtc/dtc                     -> luofu-r116-overlay.dtbo (230 B)
<openwrt>/staging_dir/host/bin/mkimage -A arm -O linux -T kernel -C none \
    -a 0x80608000 -e 0x80608000 -n "Linux-6.18.60" -d zImage uImage
fip-slice.py make-trailer --base-dtb luofu-r116.dtb \
    --overlay luofu-r116-overlay.dtbo --expect-board-id 3 \
    --expect-vendor-id hsan --match-family luofu --overlay-apply require \
    --output fip-trailer.bin
fip-slice.py assemble --uimage uImage --trailer fip-trailer.bin \
    --loader-stage 0x83200000 --text-offset 0x00608000 \
    --max-size 8650752 --output luofu-kernel-slice.bin
fip-slice.py check luofu-kernel-slice.bin --max-size 8650752
fip-slice.py manifest --slice luofu-kernel-slice.bin --output fip-manifest.txt
```

`fip-slice.py check` output on the result:

```
FIP slice check: PASS (measurable constraints only)
  slice size:      3214542 B (kernel slot limit 8650752 B)
  uImage:          magic ok, name='Linux-6.18.60', ih_size=3205200 (0x30e850)
  uImage load/ep:  0x80608000 / 0x80608000
  trailer:         9278 B
  base DTB:        totalsize 8984 B
  descriptor:      64 B (32-byte header + 1 x 32-byte entries), container total 294 B
    overlay 0      size 230 B, offset 64 B (trailer+0x2358)
  gate:            magic 0xd7b7ab1e at 0x310ba8
```

Gate arithmetic, re-read independently of the tool's own validation:
`0x40 + ih_size 0x30e850 + base-DTB totalsize 0x2318 = 0x310ba8`, and the 32-bit
big-endian word there is byte-identically `d7 b7 ab 1e`. The descriptor totals
`32 + 32*count + sum(overlay sizes) = 32 + 32 + 230 = 294` B equal its header word +4.
A second, independent Python re-read (no `fip-slice` code) and the system dtc 1.8.1
`fdtoverlay`/`fdtget` checks agree (evidence 02/03).

### Reproducibility

The two clean runs (different output directories) are byte-identical for every artifact:
same SHA256SUMS, same slice `3311a3e5...`. This works because `mkimage` honours
`SOURCE_DATE_EPOCH`, which the script pins to 0 by default (the decoded loader never reads
`ih_time`; override with `--source-date-epoch` to embed a real build time). Full listing:
evidence `01-reproducibility.txt`.

### What this proves and what it does not

Proven (host-only, structural): a trailer generated from our own base DTB + overlay satisfies
every check the decoded stock applier performs (gate word at the computed offset, required
descriptor constants, entry count/stride/totals, overlay FDT header/size, libfdt overlay
apply); the uImage carries the correct ARM/Linux/kernel/none identity, the derived
`ih_load == ih_ep == 0x80608000`, and the full slice fits the kernel slot by 5,436,210 B;
the result is byte-reproducible from the same inputs and tools.

Not proven: that the stock loader accepts a novel slice or that any of this boots. That needs
one attended boot on a recovered unit (TARGET-DESIGN.md 5.2 and 9; TRAILER-FORMAT.md section
8). Also absent: rootfs, UBI, sysupgrade container and manifest wiring into one image,
`platform.sh`, the A/B selector path, appended DTB, install path, driver series,
`dtbs_check`. Publication remains NOT READY.

### Open questions

1. **Does a no-op overlay suffice?** Structurally yes for the decoded applier: the entry
   validates, the match properties pass, host libfdt applies an empty fragment, and no base-DTB
   growth is needed. But the stock U-Boot 2022.07's own libfdt version and the applier's work
   buffers are not fully decoded, and no novel trailer has ever been booted on the stock
   loader. One attended boot is the only closure. If our base DTB ever depends on
   overlay-provided properties, the stub must become a real overlay.
2. **board_id 3** is inferred from the overlay the vendor loader applied and the live R116
   model, not from a physical strap read (TRAILER-FORMAT.md section 6 [I]); the attended boot
   confirms it (a wrong value means no match and no boot).
3. **Boot arguments / rootfs**: this slice carries no rootfs and no `root=`/`UBI`
   arguments; where the stock loader's bootargs come from is still [U] (TARGET-DESIGN.md 5.6).
   The uImage also has no appended DTB; the decoded loader hands over the trailer's base DTB,
   so the appended copy is hygiene for later recipes (TARGET-DESIGN.md 5.2), not required for
   this structural step.
4. **Delivery lane**: the slice is 3,214,542 B with a 9,278 B generated trailer, whereas the
   existing bench lane copies the fixed 40,732-byte trailer and
   `tools/fire-preflight.sh` fails closed unless the last 40,732 bytes match that artifact.
   This slice is therefore not flashable through the current tools; adopting a generated
   trailer needs the attended boot first, then the preflight/image-recipe update.
5. **Image recipe wiring**: `target/linux/hisilicon/image/luofu.mk` still treats the trailer
   as a fixed input and does not call `make-trailer`; this host script is not yet integrated
   into the OpenWrt build.

### Evidence

Local-only, gitignored:
`build/linux-kbuild/waves/upstream-6.18-slice-20261118/` - `00-README.md` indexes the two
run directories (full script stdout, per-step logs, dtb/dtbo/uImage/trailer/slice/manifest,
SHA256SUMS/SIZES), the reproducibility comparison, xxd byte structure, independent-tool
checks, exact commands and git state. None of it is committed; the published tree contains
only the sources and the script.

---

## 2026-11-18 (fifth session): first real drivers - CRG clock/reset + watchdog

**Status: the first two upstream-style drivers for this board are written, with device-tree
bindings in the shape upstream review expects, and they compile as built-ins into the same
`linux-6.18.60` kernel the `TEXT_OFFSET` series builds (`make target/linux/compile -j16`
RC=0, 1190 s, no warning from the new files). The delivered patches apply cleanly to pristine
6.18.60 and in the OpenWrt clone; `dt_binding_check` passes for both YAMLs; the draft board DTS
compiles to a DTB with the patched kernel's own dtc with ZERO warnings (the old deliberate
`unique_unit_address` collision is gone); the hand-added kernel-config delta is
symbol-identical to a canonical `make kernel_oldconfig` re-derivation with the new Kconfig;
and an independent cross-check confirms the DTS clock IDs, the driver gate table and the
dt-bindings header agree. Nothing has been booted or run on hardware: no bench, no device, no
boot claims; publication NOT READY.**

Host-only, same clone (`openwrt main` `27663a0`, `linux-6.18.60`, the tree's own OpenWrt
GCC 14.4.0 toolchain). No router, UART, TFTP, flash or bench tool was used.

### What was added

| artifact | sha256 |
| --- | --- |
| `target/linux/hisilicon/patches-6.18/004-arm-clk-reset-luofu-crg.patch` | `bb92a7cbd8f54addff628fb6ea409c8cf70b096c301669676bd21a03d4c050b0` |
| `target/linux/hisilicon/patches-6.18/005-arm-watchdog-luofu-wdt.patch` | `b8a780f5e65ed092247e1a693252c0033a9e7cf376a02c988890fd518963e2be` |
| `target/linux/hisilicon/luofu/config-6.18` (two `[N]` driver symbols plus the syscon/regmap selects; now symbol-identical to a canonical re-derivation) | `03ac74d1a167b8e05716d9409b19251a5bd2056a08545380d8d58ea28a9bb5aa` |
| `dts/luofu-r116.dts` (one CRG syscon node + watchdog child, no gate children, clock IDs from the header) | `2b395a292221174213a2e0e6532f1a78d5f3ffcf03dcdc9e18a92b3c67ed5b57` |

`004` adds:
- `drivers/clk/hisilicon/crg-luofu.c`: the 16 CRG gate clocks and the reset controller from
  one node.
- `include/dt-bindings/clock/hisilicon,luofu-crg.h`: the clock-ID ABI (IDs 0..15 keep the
  existing ordering contract; `LUOFU_CLK_NR` = 16).
- `Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml`.
- `drivers/clk/hisilicon/{Kconfig,Makefile}`: `CONFIG_LUOFU_CRG=y`.

`005` adds:
- `drivers/watchdog/luofu-wdt.c`: the watchdog on the 6.18 watchdog API, hwver-2 register
  data.
- `Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.yaml`.
- `drivers/watchdog/{Kconfig,Makefile}`: `CONFIG_LUOFU_WDT=y`.

### Design decisions (and why)

1. **One shared 0x14880000 page -> one `syscon` + `simple-mfd` node, one regmap, two
   drivers.** The CRG gate bits (0x14/0x20), reset bits (0x2c/0x30/0x34) and watchdog registers
   (0x50/0x64/0x70/0x100) are interleaved in one 4 KiB page, so no driver may claim it
   exclusively. The node is `compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"`; the
   CRG driver and the watchdog child both obtain the page with
   `syscon_node_to_regmap()` and the syscon core maps it once. Neither driver contains a
   `request_mem_region()` or `ioremap()` call, so there is no overlapping resource and no
   second mapping. The lab lane had exactly the failure this removes: the CRG claimed the page
   and the watchdog probe returned `-EBUSY` until it was worked around with a bare
   `devm_ioremap()` (ledger 2026-11-13 16:00/16:29). `simple-mfd` populates the watchdog
   child; `syscon` is the documented regmap contract, so the watchdog still works if
   `CONFIG_LUOFU_CRG` is disabled. Rejected alternative: two nodes with overlapping `reg`
   and one of them using bare `devm_ioremap()` (not upstream acceptable).
2. **Compatibles:** `hisilicon,luofu-crg` (controller) and `hisilicon,luofu-wdt` (child),
   each with a YAML binding. The watchdog node carries no vendor-only properties: the hwver,
   register offsets and magic values are hwver-2 driver data attached to the compatible's
   match entry.
3. **Clock gates: a driver-side table indexed by the documented clock IDs, not 16 child
   nodes.** The gates share two registers, and per-gate child nodes would have no `reg` (they
   are not devices) and would require an invented binding for the bit position. The DT ABI is
   the one-cell index from the header; the driver's designated-initializer table is indexed by
   the same IDs and the probe rejects a missing entry. This is the table+ID shape used by
   modern SoC clock drivers (sunxi-ng, meson, renesas CPG); child-node-per-gate is the
   deprecated TI shape and would describe no hardware. The clock IDs, ordering and the
   gate/(offset,bit) geometry are unchanged from the lab driver, so every existing consumer
   specifier keeps its meaning.
4. **Reset cells: two cells, `(CRG page register offset, bit)`.** This preserves the lab
   driver's specifier (documented in the YAML and the DTS). The xlate rejects any offset
   outside `0x2c/0x30/0x34`, so a reset specifier can never write the watchdog, soft-reset,
   mux or PLL words. The driver also sets `rcdev.of_reset_n_cells = 2`: the lab driver did not,
   which is why `devm_reset_control_array_get()` returned `-EINVAL` for every two-cell
   consumer (ledger 2026-11-13 21:41) - that bug is now fixed and explained. A named reset-ID
   table remains a submission TODO (it changes every consumer's cells and needs an agreed
   inventory).
5. **Watchdog:** 6.18 watchdog API (`devm_watchdog_register_device`, `.start/.stop/.ping/
   .set_timeout`, DT `timeout-sec`, min 1 s / max 512 s from driver data). The vendor write
   pairs/order are preserved exactly: enable val1 then val0 to 0x64, stop val0 then val1 to
   0x64, ping reset-val0 then reset-val1 to 0x70, and the hwver-2 timeout field `(secs - 1)`
   at bits [18:10] of 0x50 (clear mask `0xfff803ff`). Probe performs **no register access at
   all**, so loading the driver cannot arm the timer.
6. **Interrupt SPI 2 and restart are deliberately absent, not fake.** The block has a
   status/interrupt register at 0x100 (SPI 2 in the vendor tree), but its semantics are
   unverified, so the driver is polled and the node claims no `interrupts` property. There is
   no restart handler: the vendor reset path writes sysctrl/boot-selector registers that are not
   understood, and a wrong reset sequence would strand the board - a documented TODO. The
   watchdog therefore cannot reboot the chip by itself; that belongs to a future reset driver.

### Verification (exact commands and results)

All logs and artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/upstream-6.18-drivers-20261118/`; a `00-README.md` there indexes
them with the git state. The repository contains only the sources and these docs.

**1. Patches apply to pristine `linux-6.18.60`** (unpacked from the clone's
`dl/linux-6.18.60.tar.xz`):

```
$ cd .../upstream-6.18-drivers-20261118/a          # pristine tree
$ patch -p1 --dry-run -i 004-arm-clk-reset-luofu-crg.patch
checking file drivers/clk/hisilicon/Kconfig
checking file drivers/clk/hisilicon/Makefile
checking file drivers/clk/hisilicon/crg-luofu.c
checking file include/dt-bindings/clock/hisilicon,luofu-crg.h
checking file Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml
RC=0
$ patch -p1 --dry-run -i 005-arm-watchdog-luofu-wdt.patch
checking file drivers/watchdog/Kconfig
checking file drivers/watchdog/Makefile
checking file drivers/watchdog/luofu-wdt.c
checking file Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.yaml
RC=0
```

**2. OpenWrt prepare** (patches/config/DTS copied into the clone's target):

```
$ cd /home/shibbityshwab/openwrt-lab && make target/linux/prepare
RC=0
```

No `.rej`/`.orig` files; the patched tree contains
`drivers/clk/hisilicon/crg-luofu.c`, `drivers/watchdog/luofu-wdt.c`,
`include/dt-bindings/clock/hisilicon,luofu-crg.h` and the Kconfig/Makefile hooks.

**3. Full kernel compile** with the tree's own OpenWrt GCC 14.4.0 (`CONFIG_WERROR=y`, no
make-command-line override):

```
$ make target/linux/compile V=s -j16
  CC      drivers/clk/hisilicon/crg-luofu.o
  CC      drivers/watchdog/luofu-wdt.o
...
  Kernel: arch/arm/boot/zImage is ready
time: target/linux/compile#1190.22#74.07#104.94
RC=0
```

- `arch/arm/boot/zImage`: **3,222,416 B**, sha256
  `50cd07a1025e19e747e281ac3fb89e96e2b82f42594ddbeada2528a150dc528b`.
- kernel `.config`: `CONFIG_LUOFU_CRG=y`, `CONFIG_LUOFU_WDT=y`, `CONFIG_MFD_SYSCON=y`,
  `CONFIG_REGMAP=y`, `CONFIG_RESET_CONTROLLER=y`, `CONFIG_WATCHDOG_CORE=y`.
- both drivers are built-in and linked: `System.map` has `luofu_crg_probe` (`c0b25d08`) and
  `luofu_wdt_probe` (`c0ba5ce4`).
- **0 warnings from the new files.** The build emits 38 pre-existing
  `WARNING: modpost: missing MODULE_DESCRIPTION()` lines for unrelated generic modules
  (netfilter, phylink, ppp, ...); none mentions luofu/crg/wdt.
- the load-address contract is unchanged: both `.head.o.cmd` files carry
  `-DTEXT_OFFSET=0x00608000` and `System.map` has `_text = stext = 0xc0608000`
  (load/entry 0x80608000 via the loader staging base).

**4. Board DTS -> DTB with the patched kernel's own dtc:**

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
DTC 1.7.2-g52f07dcc, RC=0, stderr EMPTY (zero warnings)
```

- DTB: **8,944 B**, sha256
  `86396521d9db8f4abccfdb155e83b0ac99dc677f283a2a82015f391d96126df8`.
- node checks (system `fdtget`): `/system-controller@14880000` has compatible
  `["hisilicon,luofu-crg", "syscon", "simple-mfd"]`, `#clock-cells = <1>`,
  `#reset-cells = <2>`, and its only child is `watchdog`. The watchdog child has exactly
  `compatible = "hisilicon,luofu-wdt"`, `timeout-sec = <30>`, `status = "okay"` - no
  `reg`, no `interrupts`, no vendor-only properties.
- the same DTS also compiles through the delivered host slice script
  (`tools/build-kernel-slice.sh --out .../slice-check`): PASS, DTB 8,944 B (same sha256),
  trailer 9,238 B, FIP slice 3,231,718 B sha256
  `0c28771dadfffeb25e5329800ebcdeb0a36f06fadb930896a1271d7a40706fe1` with gate at
  `0x314ec0` and uImage load/ep `0x80608000`. Structural only: that slice has never been
  booted and is still not flashable through the bench lane (fixed 40,732-byte trailer).

**5. Clock-ID contract cross-check** (independent Python re-read of the DTB and the patch
text, not the tools' own asserts):

- all 16 dt-bindings IDs have a gate-table entry with the expected name and the designated
  initializer index equals the ID (`driver_table_matches_header_order: True`);
- all 15 existing clock consumers use the expected index (`mismatches: []`): fmc/sfc=0,
  gpio0=1, gpio1=2, i2c0=3, pcie0=4, pcie1=5, gmac0=6, lsw_dp=8, lsw_pfe=9, mdio=10,
  gmac1=11, gmac2=12, gmac3=13, gmac4=14, pie=15; led_pwm=7 has no consumer yet;
- no `clk-gate` child nodes remain under the controller node.

**6. Device-tree binding validation:**

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES=".../clock/hisilicon,luofu-crg.yaml:.../watchdog/hisilicon,luofu-wdt.yaml"
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   ./Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.example.dts
  DTC [C] Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.example.dtb
  DTEX    Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.example.dts
  DTC [C] Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.example.dtb
RC=0
$ dt-doc-validate -u Documentation/devicetree/bindings <both yamls>   -> RC=0
$ dt-validate -s processed-schema.json ...crg.example.dtb             -> RC=0
$ dt-validate -s processed-schema.json ...wdt.example.dtb             -> RC=0
```

dtschema **2026.9** was installed in a throwaway venv for this check (`pip install dtschema`;
the tree requires >= 2023.9). `yamllint` is not installed, so the tree's YAML-lint step is
skipped with its own warning - that is the only step not exercised. The real board DTB's CRG
subtree was also validated against the same processed schema: RC=0, no errors for the
controller or the watchdog child (only the synthetic wrapper root lacked
`compatible`/`model`). A **full**-draft-DTS `dtbs_check` is NOT clean and is not claimed:
every other node still has a TODO binding (TARGET-DESIGN.md 6.2).

**7. Kernel-config provenance:** with the new Kconfig in the tree, the canonical top-level

```
$ yes '' | make kernel_oldconfig
RC=0
```

target emitted 286 config-symbol lines, **identical** to the annotated
`luofu/config-6.18` after stripping comments (`CONFIG_SYMBOLS_IDENTICAL`; only the emitted
line order differs), and the kernel `.config` was byte-identical before and after the re-run
(`KCONFIG_UNCHANGED`). The two hand-added `[N]` driver symbols and the syscon/regmap selects
they pull in are therefore exactly what the tree derives; the committed file stays annotated.

**8. Static sanity of the submitted code:**

- no `request_mem_region()`/`ioremap()`/`platform_get_resource()` call in either driver;
  both obtain the page only via `syscon_node_to_regmap()`;
- neither probe body contains any `regmap_read`/`regmap_write`/`regmap_update_bits`
  (probe cannot arm the timer or touch a gate);
- `scripts/checkpatch.pl --no-tree -f`: **0 errors, 0 warnings** for both drivers and the
  dt-bindings header (346 / 247 / 33 lines);
- the delivered driver code contains no lab/bench breadcrumbs or private-repository references
  (0 matches for `TARGET-DESIGN`, `opensource`, `target-skeleton`, `bench`, `lab`).

### What this proves and what it does not

Proven (host-only): the two drivers are real code with bindings that pass `dt_binding_check`;
they apply to pristine 6.18.60 and build into the board kernel with zero warnings; the DTS uses
them with zero dtc warnings and the clock-ID/geometry contract is internally consistent; the
shared-page design removes the overlapping MMIO claim without any special-case mapping (there
is no mapping in the drivers at all); and the target kernel config is canonically derived.

Not proven / not claimed - any hardware behaviour:

- the 0x20-group gate offsets are reconstructed from the vendor encoding (PROVISIONAL);
- reset polarity (the driver preserves the lab behaviour; the vendor description says the
  opposite; no in-tree consumer uses the resets yet) and the per-gate clock parents/PLLs/muxes;
- watchdog enable/stop magic-value ordering at run time, whether the bootloader leaves the
  watchdog running (no `WDOG_HW_RUNNING` detection), and the 0x100 status/interrupt
  semantics;
- no restart handler exists at all (documented TODO - the vendor reset path is not understood);
- no boot, no bench, no device, no image: the FIP slice above is structural only and never
  flashable through the bench lane. **Publication remains NOT READY.**

### Remaining TODOs for the driver series

1. Attended-bench confirmation of the gate register/bit map, reset polarity, and watchdog
   clear/enable ordering and boot-time state; then decide `CLK_IGNORE_UNUSED`/rate modelling
   for boot-enabled gates and a `WDOG_HW_RUNNING` read.
2. Upstream review items: named reset-ID table, per-gate parent clock model (PLLs/muxes at
   0x198/0x1e0/0x138, read-only until measured), maintainer identity in the two YAMLs
   (currently the repository's git identity as a placeholder), and whether the CRG should be
   split across `drivers/clk/hisilicon` + `drivers/reset` (it is one MFD block, so this draft
   registers both providers from the clock driver).
3. The rest of the driver/binding series (thermal, pinctrl, FMC, MDIO/PHY/GEMAC, switch,
   PCIe), the image recipe/trailer wiring, rootfs/sysupgrade, and one attended boot of a
   generated trailer - unchanged from the earlier sessions.

Evidence: local-only
`build/linux-kbuild/waves/upstream-6.18-drivers-20261118/` (gitignored); its `00-README.md`
indexes the pristine/patched trees, the dry-run/prepare/compile logs, the dtc and
`dt_binding_check` logs, the cross-check script/output, the config re-derivation logs and the
slice re-export. None of it is committed.

## 2026-11-18 (sixth session): network trio - MDIO bus, Genius PHY, partial GEMAC

**Status: the network trio is written and compiles as built-ins into the same
`linux-6.18.60` kernel (`make target/linux/compile -j16` RC=0, 1284.97 s,
**zero warnings** with `CONFIG_WERROR=y`). The MDIO bus controller (`007`) and the
Genius PHY (`008`) are complete ports of the lab drivers; the GEMAC (`009`) is a
**partial** port - binding + platform resource validation only, deliberately **no data
path** (no TX/RX rings, no DMA, no netdev), with its DTS nodes disabled. Both new YAML
bindings pass `dt_binding_check`; the board DTS compiles with the patched kernel's own
dtc to a 9,459 B DTB with **zero warnings**; `checkpatch.pl` reports 0 errors /
0 warnings / 0 checks on all three files. **Nothing has been booted or run on hardware:
no bench, no device, no boot claims; publication NOT READY.**

Host-only, same clone (`openwrt main` `27663a0`, `linux-6.18.60`, the tree's own OpenWrt
GCC 14.4.0 toolchain). No router, UART, TFTP, flash or bench tool was used.

### What was added

| artifact | sha256 |
| --- | --- |
| `target/linux/hisilicon/patches-6.18/007-net-mdio-luofu.patch` | `dcbb2821b99f2f5b265596999b0d88fc8605561c4b2453d8857ca54d0d512ccb` |
| `target/linux/hisilicon/patches-6.18/008-net-phy-luofu.patch` | `676e332e5072608b7193a57213605d5af5cda26e11586d7a9c26fe6b14365912` |
| `target/linux/hisilicon/patches-6.18/009-net-gmac-luofu.patch` | `96022d6a61c7ef878397dbe2a28b56c00fbf68b6b3ea1bbc3df7ae5e0a2aecee` |
| `target/linux/hisilicon/luofu/config-6.18` (three `[N]` network symbols; `CONFIG_LUOFU_GMAC/MDIO/PHY=y`) | `6910879e7ef9ebfdb6e4eb3de82522bb26a05c3d86368af6794d7774ba276118` |
| `dts/luofu-r116.dts` (MDIO resets, GEMAC resets/bindings, GEMAC nodes disabled) | `989da320e753d9f7cd4f0d51563d55b3c784d713d3ef0d73bc71c85a1ef2fda6` |

`007` adds:
- `drivers/net/mdio/mdio-luofu.c`: the MDIO bus controller on the 6.18 MDIO bus API
  (`devm_mdiobus_alloc_size` + `of_mdiobus_register`), gate clock via
  `devm_clk_get_enabled()`, the seven CRG reset lines as a reset array, and the polled
  MMIO frame handshake.
- `drivers/net/mdio/{Kconfig,Makefile}`: `CONFIG_LUOFU_MDIO` (inside the existing
  `if PHYLIB` block, `depends on HAS_IOMEM && OF_MDIO`; it selects nothing new because
  the devm MDIO helpers are unconditional in 6.18).
- `Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml`.

`008` adds:
- `drivers/net/phy/luofu-phy.c`: a PHYLIB driver matched by PHY ID (`0x20669a00` /
  mask `0xffffff00`) with the generic clause 22 operations.
- `drivers/net/phy/{Kconfig,Makefile}`: `CONFIG_LUOFU_PHY`.
- **No YAML on purpose**: PHY drivers are matched by PHY ID, not by a devicetree
  compatible, and the PHY children stay standard `ethernet-phy-ieee802.3-c22` nodes.
  Adding a binding here would be wrong; the MDIO binding validates the bus and its
  children.

`009` adds:
- `drivers/net/ethernet/hisilicon/luofu-gmac.c`: the **resource/binding half** of the
  GEMAC port - it validates reg/clocks/resets/`phy-mode`/`phy-handle` and then refuses
  to probe (see below).
- `drivers/net/ethernet/hisilicon/{Kconfig,Makefile}`: `CONFIG_LUOFU_GMAC`.
- `Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml`.

### Design decisions (and why)

1. **MDIO goes in `drivers/net/mdio/`, not next to the MAC.** The controller is a
   standalone five-MAC-shared bus, and 6.18 keeps standalone MDIO controllers there
   (`mdio-xgene`, `mdio-hisi-femac`, ...). The driver uses only the standard bus API:
   no vendor glue, no vendor compatible string (`hsan,mdio` is not bound), and none of
   the vendor's optional DT properties (`pre-addr`/`pre-val`/`vendor-addr`/`pinctrl-sel`)
   are modelled - the board's DT does not carry them.
2. **The transfer sequence is the measured one, not the first decoded guess.** The lab
   source documents two register layouts: an early `0x00/0x60/0x6c/0x70/0x74` group
   recovered from the vendor's DFX path, and the `0x20/0x2c/0x30/0x38` group that the
   working lab driver actually used. The working group is the one with hardware
   evidence: the out-of-tree 5.10 driver read real clause 22 PHY IDs on the SoC (the
   Genius PHY ID came back as `0x20669a09`, ledger 2026-11-14 23:15). The port keeps
   that sequence: wait `0x2c[18:16]` clear, write the frame in the high half of `0x30`,
   poll `0x20` bit 0/1 for done, acknowledge by writing the observed value back, wait
   for busy to clear, read `0x38[15:0]`. A write puts the payload in the low half of the
   frame word. Caveat stated plainly: **that hardware evidence belongs to the previous
   out-of-tree driver; this 6.18 port has only been host-compiled.**
3. **The poll is bounded, not a spin.** The lab loop used jiffies + `cpu_relax`; the port
   uses `readl_poll_timeout_atomic()` with a 10 us interval and the same 100 ms bound,
   which is legal in both process and atomic context (the MDIO core serialises with
   `bus->mdio_lock`).
4. **PHY driver: ID-matched, generic C22 ops, no binding.** `PHY_GBIT_FEATURES` no longer
   exists in 6.18; the core reads the abilities with `genphy_read_abilities()`, and the
   driver only names itself, `config_aneg`/`read_status` and the suspend/resume helpers.
   This is the complete port of the 44-line lab driver plus the 6.18 API shape. The
   vendor per-PHY firmware patch (`lsw/hi_ephy_%xv%x.bin`) is a documented TODO: it is
   not loaded, and that is the first suspect if link training differs from the vendor
   stack.
5. **GEMAC is delivered as an honest partial, not a stub.** The previous out-of-tree
   driver registered a netdev whose `ndo_start_xmit` dropped every frame; the TX/RX ring
   and DMA programming model has never been decoded (the lab header's "rings are staged"
   was a comment without code, and the vendor's transmit helper `hi_net_dev_xmit` lives
   in another vendor module), and a wrong access to this block before it is ready has
   hung the board's AHB bus. Registering a net_device that cannot carry a packet would
   present a working-looking interface to userspace. Instead the driver claims the
   binding, validates the resources, and returns `-EOPNOTSUPP` from probe without
   registering anything; the DTS GEMAC nodes are `status = "disabled"` so a normal boot
   never reaches it. What is known is kept for the next step: the five instances are
   ports 8..12 of the internal switch, instance 0 is RGMII and unused by the vendor,
   instances 1..4 are GMII into the switch, and the wired jack is instance 3
   (0x14480000, port 11); a few words (PORT_MODE 0x0040, MODE_CHANGE_EN 0x01b4) were
   observed read-only on the vendor's running kernel. None of that is written here.
6. **DTS/config follow the existing provenance discipline.** The MDIO node gains its
   seven CRG resets with clean reset names (`mdio`, `gephyall`, `gephy0`..`gephy4`)
   instead of the vendor's `*_reset` strings; the GEMAC nodes gain the two CRG reset
   lines and are switched from `okay` (no driver) to `disabled` (partial driver).
   `config-6.18` gains the three `[N]` symbols =y. They reuse symbols already in the
   delta (`CONFIG_OF_MDIO`, `CONFIG_MDIO_BUS`, `CONFIG_PHYLIB`); `CONFIG_PHYLINK`
   stays `m` because the partial GEMAC links no phylink code - a future full GMAC port
   will need `PHYLINK=y` and its MAC ops.

### Verification (exact commands and results)

All logs and artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/upstream-6.18-net-20261118/`. The repository contains only the
sources and these docs.

**1. Patches apply to pristine `linux-6.18.60`** (unpacked from the clone's
`dl/linux-6.18.60.tar.xz`):

```
$ cd .../netport-pristine/linux-6.18.60
$ patch -p1 --dry-run < 007-net-mdio-luofu.patch
checking file drivers/net/mdio/Kconfig
checking file drivers/net/mdio/Makefile
checking file drivers/net/mdio/mdio-luofu.c
checking file Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml
RC=0
$ patch -p1 --dry-run < 008-net-phy-luofu.patch
checking file drivers/net/phy/Kconfig
Hunk #1 succeeded at 253 (offset -75 lines).
checking file drivers/net/phy/Makefile
Hunk #1 succeeded at 63 (offset -12 lines).
checking file drivers/net/phy/luofu-phy.c
RC=0
$ patch -p1 --dry-run < 009-net-gmac-luofu.patch
...
RC=0
```

The `-75`/`-12` offsets in `008` exist because the patch was generated against the
OpenWrt clone's `drivers/net/phy/*` files, which the OpenWrt generic patch set has
already extended (extra PHY drivers); the hunks still land on the right context in
pristine 6.18.60 and in the clone. This is cosmetic, not a data-path difference.

**2. OpenWrt prepare** (the three patches and the DTS/config edits were copied into the
clone's target):

```
$ cd /home/shibbityshwab/openwrt-lab && make target/linux/prepare V=s
Applying .../patches-6.18/007-net-mdio-luofu.patch using plaintext:
Applying .../patches-6.18/008-net-phy-luofu.patch using plaintext:
Applying .../patches-6.18/009-net-gmac-luofu.patch using plaintext:
RC=0
```

No `.rej`/`.orig` files; the tree contains `drivers/net/mdio/mdio-luofu.c`,
`drivers/net/phy/luofu-phy.c`, `drivers/net/ethernet/hisilicon/luofu-gmac.c` and both
YAMLs.

**3. Full kernel compile** with the tree's own OpenWrt GCC 14.4.0 (`CONFIG_WERROR=y`, no
make-command-line override):

```
$ make target/linux/compile V=s -j16
  CC      drivers/net/mdio/mdio-luofu.o
  CC      drivers/net/phy/luofu-phy.o
  CC      drivers/net/ethernet/hisilicon/luofu-gmac.o
...
  Kernel: arch/arm/boot/zImage is ready
time: target/linux/compile#1284.97#...
RC=0
```

- `arch/arm/boot/zImage`: **3,223,728 B**, sha256 `2749c294fe95b6fe3ece2ecc26357dbb44d0aa5e5fc765699e8865f0a6e5b6f5`.
- kernel `.config`: `CONFIG_LUOFU_MDIO=y`, `CONFIG_LUOFU_PHY=y`, `CONFIG_LUOFU_GMAC=y`
  (plus the already-enabled `CONFIG_LUOFU_CRG/WDT=y`).
- all three objects are built-in and linked: `System.map` has `luofu_mdio_probe`,
  `luofu_gmac_probe` and `luofu_genius_phy_drivers`.
- **0 warnings in the whole compile log** (`grep -c 'warning:'` = 0), and none of the
  new files appears in any warning (`grep` limited to the new names = none).
- the load-address contract is unchanged: both `.head.o.cmd` files carry
  `-DTEXT_OFFSET=0x00608000` and `System.map` has `_text = stext = 0xc0608000` /
  `swapper_pg_dir = 0xc0604000`, i.e. load/entry `0x80608000` via the loader staging base
  - the `TEXT_OFFSET` mechanism is untouched by this session.

**4. Board DTS -> DTB with the patched kernel's own dtc:**

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
DTC RC=0, stderr EMPTY (zero warnings)
```

- DTB: **9,459 B**, sha256 `f6781920eedc8421ffbe5d72aa6adecc9f64509ee2316f62263aabd5f4f9c887` (the fifth session's DTB was 8,944 B; the
  growth is the MDIO/GEMAC reset arrays and binding comments).
- `fdtget` node checks: `/mdio@14000000` compatible `"hisilicon,luofu-mdio"`, seven
  `resets` entries (`0x30` word, bits `0x6`, `0x1a..0x1f`) and reset-names
  `"mdio gephyall gephy0 gephy1 gephy2 gephy3 gephy4"`; `/ethernet@14480000` compatible
  `"hisilicon,luofu-gmac"`, `status = "disabled"`, two `resets` entries (bits
  `0x13`/`0x14`).
- 17 reset specifiers exist in the DTS; all are `(0x30, bit <= 31)`, which the CRG reset
  provider's xlate accepts (it only allows the `0x2c/0x30/0x34` words and bit 0..31) -
  checked independently with a Python re-read of the DTS, `invalid: []`.

**5. Device-tree binding validation:**

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES=".../net/hisilicon,luofu-mdio.yaml:.../net/hisilicon,luofu-gmac.yaml"
  DTEX    Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.example.dts
  DTC [C] .../hisilicon,luofu-mdio.example.dtb
  DTEX    Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.example.dts
  DTC [C] .../hisilicon,luofu-gmac.example.dtb
RC=0
```

dtschema **2026.9** in a throwaway venv (`/tmp/dtsv`); `yamllint` is not installed, so the
tree's YAML-lint step is skipped with its own warning - that is the only validation step
not exercised (same limitation as the fifth session). A **full**-draft-DTS `dtbs_check` is
NOT clean and is not claimed: every other node still has a TODO binding.

**6. Static sanity of the submitted code:**

- `scripts/checkpatch.pl --no-tree -f`: **0 errors, 0 warnings, 0 checks** -
  `mdio-luofu.c` 211 lines, `luofu-phy.c` 52 lines, `luofu-gmac.c` 137 lines.
- no `request_mem_region()`/`ioremap()` in any of the three drivers (MMIO only through
  `devm_platform_ioremap_resource()`/the MDIO bus API);
- no lab/bench breadcrumbs or private-repository references: `grep -i 'lab|bench'` on the
  three drivers and both YAMLs = **no matches**; no diagnostic-cell writes, no
  `0xc0de*`/`0x10100*` addresses, no vendor `hsan,*` strings;
- the GMAC driver registers no `net_device` and has no `ndo_start_xmit`; probe returns
  `-EOPNOTSUPP` before any register access.
- 281 lines of the committed config delta were compared against the compiled kernel's
  `.config`: all `LUOFU`/network symbols match. The only two differences are a
  **pre-existing, unrelated** `CONFIG_DEVTMPFS`/`CONFIG_DEVTMPFS_MOUNT` pair (the delta
  says `=y` while the OpenWrt generic config and the built `.config` say not-set); this
  session neither introduced nor fixed that and it does not touch the network symbols.

### What this proves and what it does not

Proven (host-only): the three drivers are real code with two bindings that pass
`dt_binding_check`; they apply to pristine 6.18.60, apply in the OpenWrt clone, and build
into the board kernel with **zero warnings under `CONFIG_WERROR=y`**; the board DTS uses
the new nodes with zero dtc warnings; the GEMAC driver cannot create a fake interface (it
refuses to probe and registers no netdev); and no vendor/lab instrumentation or private
artifact is embedded.

Not proven / not claimed - any hardware behaviour:

- the 6.18 MDIO driver's transfer timing/rate and the reset-polarity assumption (the
  register sequence has hardware evidence only through the earlier out-of-tree 5.10
  driver; the CRG reset polarity itself is still unverified);
- PHY autonegotiation/link/suspend behaviour on this silicon, and whether the vendor
  per-PHY firmware patch is required for correct link training;
- the GEMAC data path: it does not exist in this port. No rings, no DMA, no NAPI, no
  netdev; the DTS nodes are disabled;
- no boot, no bench, no device, no image. **Publication remains NOT READY.**

### Remaining TODOs for the network port

1. Bench: boot the 6.18 kernel (needs the image/install path that does not exist yet),
   confirm the MDIO bus comes out of reset, read the PHY IDs (expect `0x20669a09`-class
   values on addresses 2..5), and check autonegotiation/link on the wired port.
2. Decode and implement the GEMAC TX/RX ring + DMA model from a citable source
   (vendor GPL source or further RE), add NAPI and DMA mapping, program the MAC port
   mode from the phylink link-up callback, then enable the GEMAC DTS nodes and attach
   through `phylink` - that is when `CONFIG_PHYLINK=y` and the MAC ops enter the
   picture.
3. Decode the vendor per-PHY firmware patch (`hi_ephy_%xv%x.bin`) if hardware tests show
   the generic C22 path is insufficient.
4. Upstream review items: maintainer identity in the YAMLs (currently the repository's
   git identity as a placeholder), whether the seven-reset array belongs on the MDIO bus
   node, the MDIO controller file location, and whether a GEMAC binding should land
   before its data path exists (the draft keeps the binding because the DT nodes are
   already in the board tree and the partial driver documents the contract).

Evidence: local-only `build/linux-kbuild/waves/upstream-6.18-net-20261118/` (gitignored).
None of it is committed.


## 2026-11-18 (sixth session, FMC/NAND port): FMC SPI-NAND controller

**Status: the board's flash controller is ported as a SPI-MEM controller with a YAML
binding, and the draft board DTS is converted to a SPI bus + `spi-nand` child. The
delivered patch applies to pristine `linux-6.18.60` and in the clone, the full kernel
compiles as a built-in with zero warnings from the new files (`make target/linux/prepare`
and `make target/linux/compile` RC=0), the board DTS compiles with the kernel's own dtc
with zero warnings, `dt_binding_check` passes for the new YAML, and the hand-added config
delta is symbol-identical to a canonical `make kernel_oldconfig` re-derivation. Nothing has
been booted or run on hardware: the FMC ECC engine semantics, the OOB bytes the DMA engine
returns, the die's quad-enable state after the SPI-NAND core's reset and the partition
semantics are unverified. Publication NOT READY.**

Host-only, in an isolated reflink copy of the same clone (`openwrt main` `27663a0`,
`linux-6.18.60`, the tree's own OpenWrt GCC 14.4.0 toolchain) so a parallel driver port
could keep using the original clone. No router, UART, TFTP, flash or bench tool was used.

### What was added

| artifact | sha256 |
| --- | --- |
| `target/linux/hisilicon/patches-6.18/006-arm-mtd-nand-luofu-fmc.patch` | 1f8809e98be649fb915c33d0ceaadb0c7e98509068347e72557839d3c7f5865e |
| `target/linux/hisilicon/luofu/config-6.18` (three `[N]` groups: the driver symbol, the SPI-NAND stack, the SPI core; symbol-identical to a canonical re-derivation) | 2b5493d2797d12b5b77eb13027daee7e07b331d63b590ba66c305231fd67c58a |
| `dts/luofu-r116.dts` (the FMC node becomes a SPI bus with a `spi-nand` child; `resets` added) | d1cca5f9b730f5b01dcd2cab010680013ac6b46b02713f8650dd8f2ccfc4ed0d |

`006` adds:

- `drivers/spi/spi-luofu-fmc.c`: a SPI-MEM controller for the FMC at `0x10a20000` (command
  engine for command/address/short-data phases; the measured DMA engine for page + OOB
  transfers). The mainline SPI-NAND stack is the NAND layer.
- `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml`.
- `drivers/spi/{Kconfig,Makefile}`: `CONFIG_LUOFU_FMC`.

### Design decisions (and why)

1. **Not the raw-NAND framework: a SPI-MEM controller plus the mainline SPI-NAND stack.**
   The die is an SPI-NAND part (ESMT F50L1G41LC: page load `0x13`, read cache `0x6b`,
   program load `0x32`, program execute `0x10`, block erase `0xd8`, READ ID `0x9f`). The
   raw-NAND core drives `NAND_CMD_*` sequences and a `0x90` READ ID, which this die does not
   speak, and the controller is in SPI mode (FMC_CFG bit 2 = 0; the lab driver refuses
   raw-NAND mode for exactly this reason). Reusing `drivers/mtd/nand/spi` provides chip
   detection, the on-die ECC engine, the ESMT OOB layout, bad-block handling and partition
   parsing from mainline instead of re-implementing them, which is what the repo's port spec
   recommends (parent `phase50/nand-fmc-port-spec.md` section (f)).
2. **The controller driver is a SPI-MEM controller** (`spi_controller` + `mem_ops`), the
   upstream shape already used for dedicated SPI-NAND controller hardware
   (`drivers/spi/spi-qpic-snand.c`, `spi-amlogic-spifc-a4.c`). Page data uses the
   controller DMA engine with the two measured interface packs; the command engine handles
   commands, addresses, dummy cycles and the short feature/ID data phases through the
   memory-mapped window.
3. **No dirmap implementation is needed**: an SPI controller without `->dirmap_create` gets
   the spi-mem core's exec_op fallback, and every page-cache operation the SPI-NAND core
   issues is one of the shapes the driver accepts (checked in `supports_op`).
4. **Measured register values are driver data, not DT properties.** The vendor DT props
   are gone: `spi_cs` becomes the child's `reg` (chip select 1), `enable-quad-mode` becomes
   `spi-{tx,rx}-bus-width = <4>` (the SPI-NAND core then selects the quad op variants), and
   `bus_id` disappears. The page/OOB geometry (2048/64) is driver data matching the die the
   controller is wired to; every op is validated against it.
5. **Clock and reset are consumed from the CRG binding (patch `004`)**: `clocks = <&crg
   LUOFU_CLK_SFC>` and `resets = <&crg 0x2c 0x0>` (`sfc_rst`). The driver deasserts the
   reset rather than pulsing it: the bootloader has already released the block, and the
   block's timing configuration is bootloader data - pulsing is a measurement, not a guess.
6. **Deliberately absent**: no IRQ (all known implementations poll and no devicetree wires
   one), no restart, no die reset in the controller probe (the SPI-NAND core does its own),
   no writes to the fifteen undecoded vendor init words (their meaning is unknown; only the
   two documented mode bits are set), and no lab breadcrumbs, sysctrl writes, JFFS2/UBI
   edits or "read ECC off" flag - those were never in the driver slice.
7. **The ESMT F50L1G41LC chip entry is not in vanilla 6.18.60.** It is a v6.19 upstream
   addition that this target receives through OpenWrt's generic backport
   `target/linux/generic/backport-6.18/434-v6.19-mtd-spinand-esmt-add-support-for-F50L1G41LC.patch`.
   The driver therefore depends on that backport in this tree; an upstream submission needs
   the chip entry (or a target kernel bump).

### Verification (exact commands and results)

All logs/artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/upstream-6.18-fmc-20261118/`; nothing there is committed.

**1. Patch applies to pristine `linux-6.18.60`** (unpacked from the clone's
`dl/linux-6.18.60.tar.xz`):

```
$ cd .../upstream-6.18-fmc-20261118/a            # pristine tree
$ patch -p1 --dry-run -i 006-arm-mtd-nand-luofu-fmc.patch
checking file drivers/spi/Kconfig
checking file drivers/spi/Makefile
checking file drivers/spi/spi-luofu-fmc.c
checking file Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml
RC=0
```

**2. OpenWrt prepare** (target `001`-`006` + config + DTS copied into the isolated clone
copy):

```
$ cd /home/shibbityshwab/openwrt-lab-fmc && make target/linux/prepare
RC=0   (the log shows 004/005/006 applied; 0 .rej/.orig files)
```

**3. Full kernel compile** with the tree's own OpenWrt GCC 14.4.0 and `CONFIG_WERROR=y`:

```
$ make target/linux/compile V=s -j16
  CC      drivers/spi/spi-luofu-fmc.o
...
  Kernel: arch/arm/boot/zImage is ready
RC=0   (START 2026-11-18T03:27:22Z, END 03:29:11Z = 109 s wall clock; the copied tree's OpenWrt time.pl counter is an accumulated total (1202.37 s), not this run)
```

- `arch/arm/boot/zImage`: **3,248,984 B**, sha256 51f340adc9d4e83c6b4c55be93d43453d65947126c9c805309a5d08e99d859ba.
- kernel `.config`: CONFIG_LUOFU_FMC=y, CONFIG_MTD_SPI_NAND=y, CONFIG_MTD_NAND_CORE=y, CONFIG_MTD_NAND_ECC=y, CONFIG_SPI=y, CONFIG_SPI_MASTER=y, CONFIG_SPI_MEM=y (plus CONFIG_LUOFU_CRG=y / CONFIG_LUOFU_WDT=y from patches 004/005).
- the driver is built-in and linked: `System.map` has `luofu_fmc_probe`, `luofu_fmc_exec_op`,
  `luofu_fmc_dma_xfer`, the match table and the `device_initcall`.
- **0 compiler warnings/errors from the new files**: the only `spi-luofu-fmc` line in the
  build log is the `CC` line. The remaining warning strings in the log are pre-existing
  `modpost: missing MODULE_DESCRIPTION()` lines for unrelated generic modules (netfilter,
  phylink, ppp, ...) plus one kernel kconfig-sync notice printed before the build.
- the load-address contract is unchanged: both `.head.o.cmd` files carry
  `-DTEXT_OFFSET=0x00608000` and `System.map` has `_text = stext = 0xc0608000`.

**4. Board DTS -> DTB with the patched kernel's own dtc:**

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
DTC 1.7.2-g52f07dcc, RC=0, stderr EMPTY (zero warnings)
```

- DTB: **9,163 B**, sha256 3541b267ca666e90d921d48ccea2d3394d657e68805aa5fff1b1e4009de154df.
- the `spi@10a20000` node has compatible `hisilicon,luofu-fmc`, `reg-names = "regs",
  "window"`, `clocks = <&crg 0>` (`LUOFU_CLK_SFC`), `resets = <&crg 0x2c 0>`, and one child
  `flash@1` (`compatible = "spi-nand"`, `reg = <1>`, quad bus widths) carrying the 17 fixed
  partitions.

**5. Device-tree binding validation:**

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES=mtd/hisilicon,luofu-fmc.yaml
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   ./Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.example.dts
  DTC [C] .../hisilicon,luofu-fmc.example.dtb
RC=0
```

dtschema **2026.9** in a throwaway venv; `yamllint` is not installed, so the tree's
YAML-lint step is skipped with its own warning. The board DTB's FMC subtree (plus the CRG
provider it points at) validates against the same processed schema: `dt-validate` RC=0,
stderr empty.

**6. Kernel-config provenance:** with the new Kconfig in the tree:

```
$ yes '' | make kernel_oldconfig
RC=0
```

target emitted 285 config-symbol lines, **identical** to the annotated
`luofu/config-6.18` after stripping comments (285/285 symbols identical by set (CONFIG_SYMBOLS_IDENTICAL); only the [N] annotation lines move symbols out of emitted sort order), and the kernel `.config` was
byte-identical before and after (CONFIG_DERIVATION_IDEMPOTENT (two consecutive canonical runs byte-identical) and ANNOTATED_CONFIG_MERGES_IDENTICALLY (the annotated delta and the canonical delta merge with generic/config-6.18 to the identical kernel config); the derivation make returns rc=0 (the yes feeder exits on SIGPIPE afterwards)).

**7. Static sanity:**

- `scripts/checkpatch.pl --no-tree -f`: **0 errors, 0 warnings** for the driver (636
  lines) and the YAML (106 lines);
- the delivered driver/binding text contains no `lab`, `bench`, `opensource`,
  `target-skeleton`, sysctrl/breadcrumb or `0xC0DE` references (0 matches in the final
  scan);
- the driver consumes the CRG clock/reset cells instead of hard-coding them: the gate is
  `clocks = <&crg LUOFU_CLK_SFC>` and the reset is the documented 2-cell form
  `<&crg 0x2c 0x0>`.

### What this proves and what it does not

Proven (host-only): the controller driver is real code with a binding that passes
`dt_binding_check`; it applies to pristine 6.18.60 and builds into the board kernel as a
built-in with zero warnings; the board DTS uses it with zero dtc warnings and its FMC
subtree validates against the schema; the config delta is canonically derived; the load
contract is unchanged.

Not proven / not claimed - any hardware behaviour:

- the FMC ECC engine semantics: whether ECC type 1 is a host ECC engine or an on-die
  pass-through, and whether the on-die ECC engine's OOB/status expectations are met;
- the OOB bytes the DMA engine returns at `SADDR_OOB` (the receive side of the lab proof
  only ever consumed page data, and bad-block/ECC status handling depends on OOB);
- the page-load command (`0x13`) through the command engine: the measured page read let the
  DMA engine perform the whole page operation and never issued it;
- whether a Linux die reset (issued by the SPI-NAND core at probe) clears the die's
  quad-enable bit; the board's bootloader runs quad and the mainline ESMT entry has no QE
  quirk;
- the fifteen undecoded vendor init words are not written; whether the die needs them is
  unknown;
- partition read-only semantics and the `u-boot,env` representation;
- no boot, no bench, no device, no image: **publication remains NOT READY**.

### Remaining TODOs for the driver series

1. Attended bring-up of one generated kernel on the board: confirm the command-engine
   encodings and address byte order, the DMA completion protocol, the quad-enable state,
   the OOB path and the ECC status reporting; then decide whether any of the fifteen vendor
   init words is required.
2. Upstream: the ESMT F50L1G41LC chip entry must land (v6.19) or the target must move to a
   kernel that has it; `MAINTAINERS` entries; review the driver-data page/OOB geometry
   approach; decide whether the reset should be pulsed.
3. The rest of the driver series (thermal, pinctrl, MDIO/PHY/GEMAC, switch, PCIe) and one
   attended boot - unchanged.

Evidence: local-only `build/linux-kbuild/waves/upstream-6.18-fmc-20261118/`.

---

## 2026-11-18 (seventh session): machine restart (patch 010)

**Status: the first upstream-style machine-restart path for this SoC is written as a platform
driver with two YAML bindings, the board DTS gets a SYSCTRL syscon node and the reboot node,
and the driver/rootfs config gains `CONFIG_LUOFU_REBOOT=y`. The patch applies cleanly to
pristine `linux-6.18.60` and in the OpenWrt clone, the full kernel compiles as a built-in
with zero warnings, both bindings pass `dt_binding_check`, the board DTS compiles with the
patched kernel's own dtc to a DTB with zero warnings and the two new nodes validate against
their schemas, and `checkpatch` is clean. The CRG software-reset command itself is
**PROVISIONAL** (the vendor devicetree values and the vendor reboot-register offset are
used, but the exact write order/width has never been measured), and the selector protocol
stays [U]. NOTHING HAS BEEN BOOTED OR RUN ON HARDWARE AND THE RESET PATH IS NOT CLAIMED TO
WORK; publication remains NOT READY.**

Host-only, in an isolated btrfs-reflink copy of the clone
(`/home/shibbityshwab/openwrt-lab-restart`; `openwrt main` `27663a0`, `linux-6.18.60`,
the tree's own OpenWrt GCC 14.4.0 toolchain). The shared `openwrt-lab` clone was not
modified by this lane; no router, UART, TFTP, flash or bench tool was touched.

### What was added

| artifact | sha256 |
| --- | --- |
| `target/linux/hisilicon/patches-6.18/010-arm-luofu-restart.patch` | `3fbfaaeed7f3038eeb0c56701e4cc52ccdc082e693a04b990336b188c2d25095` |
| `target/linux/hisilicon/dts/luofu-r116.dts` (adds the SYSCTRL syscon node and the reboot node) | `42c6ffa30b74f1108d2045ae1814e1b3da709752f1e327aabc26b8871752b9b4` |
| `target/linux/hisilicon/luofu/config-6.18` (`CONFIG_LUOFU_REBOOT=y`) | `25c27473b3bc52ae2d0a207acb4126080578e8c28d59b2656ff0960c9fc7a9c8` |

`010` adds:
- `drivers/power/reset/luofu-reboot.c`: the restart handler. Three steps: save the SYSCTRL
  A/B boot selector with a read-back write-same; write the vendor `softrst_val0`/`val1`
  pair to the CRG reboot register at page offset `0x00`; if that does not reset the SoC,
  set the CRG watchdog to a 1 s expiry and arm its vendor enable pair, then wait a bounded
  time. Probe only obtains the two syscon regmaps and registers the handler - it performs
  no register access at all.
- `drivers/power/reset/{Kconfig,Makefile}`: `CONFIG_LUOFU_REBOOT` (bool,
  `depends on ARCH_LUOFU || COMPILE_TEST`, `select MFD_SYSCON`).
- `Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml` and
  `Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml`.

### Design decisions (and why)

1. **A standalone `drivers/power/reset/` platform driver, not a machine `.restart` hook.**
   This is the upstream shape for SoC restart implementations (the directory already holds
   `hisi-reboot.c`, `syscon-reboot.c`, `gpio-restart.c`), it keeps the mach file clean,
   and it matches the target design's "future reset driver" note (TARGET-DESIGN.md 7.1).
   The driver shares the two syscon pages instead of claiming them: SYSCTRL is a new
   `hisilicon,luofu-sysctrl` syscon node and CRG is the existing
   `hisilicon,luofu-crg` syscon node. There is no `ioremap()` and no
   `request_mem_region()` in the driver, so the lab lane's overlapping-MMIO failure mode
   cannot recur.
2. **The boot-selector write is a write-same, never the lab's hard-coded `0x10`.** The
   measured facts are the cell offset (`0xc08`), the A/B encodings (`0x10`/`0x11`) and
   that the cell is sticky across a warm reset. The value the stock restart path writes is
   not measured, so the driver reads the cell, validates the encoding and writes the same
   value back - it preserves whichever pair booted and cannot move the board to another
   slot. An unrecognised value (including the observed `0x21` handover) is left untouched
   with a warning.
3. **The CRG reset command is PROVISIONAL and is labelled so in the driver, the binding and
   the DTS.** The values (`0x51162100`/`0xaee9deff`, the second the bitwise complement of
   the first) and the register offset (`reboot-offset = <0x0>`) are the vendor devicetree's
   own; the recovered access rules name `0x00` the reboot register and mark it as a store
   the read-only lab instruments must not perform (i.e. writing it is expected to reset the
   SoC). What is *not* measured is whether the pair goes to one register or two adjacent
   words and in which order - the driver writes both to `0x00` (the vendor watchdog pair
   shape) and nothing is invented. The write is confined to the reboot register: no
   soft-reset (`0x84`), resume (`0x38`), mux (`0x138`) or PLL (`0x198`/`0x1e0`)
   word is ever touched, so a wrong command cannot reprogram clocks.
4. **A failed reset must not strand the board, so the watchdog is the bounded fallback.**
   PHASE 85 in the parent ledger is the evidence: the old restart path wrote its reset
   sequence, parked forever, and the SoC watchdog had already been disarmed - only a hand on
   the power returned the box. The fix was to arm the watchdog before parking. This driver
   keeps that backstop with the watchdog driver's own vendor hwver-2 values (1 s expiry,
   enable val1-then-val0) and **never parks**: after arming it waits 1.5 s and returns, so
   `machine_restart()` still gets to print its "Reboot failed" message.
5. **Why not extend the watchdog driver or `POWER_RESET_HISI`:** patch `005` deliberately
   has no restart handler (arming the watchdog from the watchdog driver is the wrong layer,
   and the reset path was unverified); `POWER_RESET_HISI` binds `"hisilicon,sysctrl"` and
   writes `0xdeadbeef` at a `reboot-offset` prop this SoC's vendor node does not have, and
   matching that compatible would give it an MMIO claim next to the syscon; `syscon-reboot`
   cannot express selector-save + two-stage reset. The watchdog enable constants therefore
   appear in both `005` and `010` - a known review smell, named in the port summary rather
   than hidden.
6. **Probe is read-only, and `checkpatch`/static checks are part of the gate.** The first
   MMIO access happens in the restart handler only; loading the driver cannot arm the timer,
   write the selector or reset anything. The delivered drivers/YAMLs contain no lab/bench
   breadcrumbs and no private-repository references.

### Verification (exact commands and results)

All logs/artifacts are local-only (gitignored) under
`build/linux-kbuild/waves/upstream-6.18-restart-20261118/`; nothing there is committed.

**1. Patch applies to pristine `linux-6.18.60`** (unpacked from the clone's
`dl/linux-6.18.60.tar.xz`):

```
$ cd .../upstream-6.18-restart-20261118/a            # pristine tree
$ patch -p1 --dry-run -i 010-arm-luofu-restart.patch
checking file drivers/power/reset/Kconfig
checking file drivers/power/reset/Makefile
checking file drivers/power/reset/luofu-reboot.c
checking file Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml
checking file Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml
RC=0
```

**2. OpenWrt prepare** (the isolated clone copy, target patches `001`-`010` plus the DTS
and config edits; the prep step re-extracts the kernel and applies every generic backport):

```
$ cd /home/shibbityshwab/openwrt-lab-restart && make target/linux/prepare V=s
Applying .../patches-6.18/010-arm-luofu-restart.patch using plaintext:
patching file drivers/power/reset/Kconfig
...
RC=0   (1,169 "patching file" lines over the generic + target patches; 0 .rej/.orig files)
```

**3. Full kernel compile** with the tree's own OpenWrt GCC 14.4.0 and `CONFIG_WERROR=y`
(no make-command-line override):

```
$ make target/linux/compile V=s -j16
  CC      drivers/power/reset/luofu-reboot.o
...
  Kernel: arch/arm/boot/zImage is ready
RC=0
```

- `arch/arm/boot/zImage`: **3,251,336 B**, sha256 `e548bc7b4c54160da2b482fcb2111d5569227c1132ba4c0299ba2e665d26aeb4`.
- kernel `.config`: `CONFIG_LUOFU_REBOOT=y`, plus the existing `CONFIG_POWER_RESET=y`,
  `CONFIG_MFD_SYSCON=y`, `CONFIG_REGMAP=y` and the rest of the series symbols.
- the driver is built-in and linked: System.map has `luofu_reboot_probe` and
  `luofu_restart_handler` (luofu_reboot_probe (c0ba94a8), luofu_restart_handler (c07082cc)).
- **0 warning lines** in the whole compile log.
- the load-address contract is unchanged: `_text = stext = 0xc0608000` in System.map and
  `-DTEXT_OFFSET=0x00608000` in both early-boot `.cmd` files.

**4. Board DTS -> DTB with the patched kernel's own dtc:**

```
$ cpp -nostdinc -I <kernel>/include -I <kernel>/arch/arm/boot/dts -undef \
      -D__DTS__ -x assembler-with-cpp -o luofu-r116.dts.preprocessed dts/luofu-r116.dts
$ <kernel>/scripts/dtc/dtc -I dts -O dtb -o luofu-r116.dtb luofu-r116.dts.preprocessed
RC=0, stderr EMPTY (zero warnings)
```

- DTB: **9,891 B**, sha256 `f23fc9319c3f0d9fc3cad6aa0a6578fde9a5f4f182ba27a5ff00f5cea44383cb`.
- node checks: `/reboot` has compatible `"hisilicon,luofu-reboot"` and the
  `hisilicon,sysctrl`/`hisilicon,crg` phandles; `/system-controller@10100000` has
  compatible `"hisilicon,luofu-sysctrl", "syscon"` and `reg = <0x10100000 0x1000>`.

**5. Device-tree binding validation:**

```
$ PATH=<dtschema-venv>/bin:$PATH make ARCH=arm dt_binding_check \
      DT_SCHEMA_FILES="Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml:Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml"
  DTEX    .../hisilicon,luofu-reboot.example.dts
  DTC [C] .../hisilicon,luofu-reboot.example.dtb
  DTEX    .../hisilicon,luofu-sysctrl.example.dts
  DTC [C] .../hisilicon,luofu-sysctrl.example.dtb
RC=0
```

dtschema **2026.9**; `yamllint` is not installed, so the tree's YAML-lint step is skipped
with its own warning - the only validation step not exercised (same limitation as the
earlier sessions). The real board DTB was also run through `dt-validate`: RC=0 and **no
message names the reboot or SYSCTRL node** (the remaining messages are the pre-existing TODO
nodes: sp804 compatible, UART clock-names, thermal zone naming). A full-draft-DTS
`dtbs_check` is still not clean and is not claimed.

**6. Static checks:**

```
$ scripts/checkpatch.pl --no-tree -f drivers/power/reset/luofu-reboot.c
  total: 0 errors, 0 warnings, 258 lines checked
$ scripts/checkpatch.pl --no-tree -f Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml
  total: 0 errors, 0 warnings, 69 lines checked
$ scripts/checkpatch.pl --no-tree -f Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml
  total: 0 errors, 0 warnings, 44 lines checked
```

- no `ioremap()`/`request_mem_region()`/`platform_get_resource()` in the driver; both pages
  are obtained through `syscon_regmap_lookup_by_phandle()`;
- the probe body contains no `regmap_read`/`regmap_write` call (no register touch before a
  reboot is requested);
- no lab/bench breadcrumb or private-repository reference in the driver or the two YAMLs
  (`grep -i 'lab|bench'` -> no matches); no `0xc0de*`/vendor `hsan,*` strings.

**7. Config provenance:** canonically re-derived with the new Kconfig in the tree: `yes '' | make kernel_oldconfig` RC=0 emitted 297 config-symbol lines that are **identical** to the annotated `luofu/config-6.18` after stripping comments (`CONFIG_SYMBOLS_IDENTICAL`), including `CONFIG_LUOFU_REBOOT=y`; a second run was byte-identical (`IDEMPOTENT_BYTE_IDENTICAL`) and the clone's target config was restored afterwards. The driver's `select MFD_SYSCON` adds no resolved line because MFD_SYSCON was already in the delta.

### What this proves and what it does not

Proven (host-only): the restart driver and its two bindings are real code that applies to
pristine 6.18.60, applies in the OpenWrt platform-patch order and builds into the board
kernel as a built-in with zero warnings under `CONFIG_WERROR=y`; the two YAMLs pass
`dt_binding_check` and the board DTS nodes validate against them; the board DTS compiles
with zero dtc warnings; `checkpatch` is clean; probe cannot write a register; and the load
contract/TEXT_OFFSET is unchanged.

Not proven / not claimed - any hardware behaviour:

- whether writing the vendor complementary pair to CRG offset `0x00` actually resets the
  SoC; the write order and whether the pair belongs in one register or two adjacent words
  are unmeasured (PROVISIONAL);
- whether the 1 s watchdog arming sequence from our own kernel takes: the values are the
  vendor driver's hwver-2 values and the watchdog has been observed resetting this board
  through the bootloader's own arming, but this exact sequence from Linux has not been run
  here;
- what `0x21` means as a selector value (the driver leaves it untouched);
- the selector protocol itself: the Linux read path for `sysupgrade` and one attended A/B
  validation remain open (TARGET-DESIGN.md 3.4);
- no boot, no bench, no device, no image: **publication remains NOT READY**.

### Remaining TODOs for the restart port

1. One attended bench cycle in the order given in `docs/incoming/restart/SUMMARY.md`: the
   watchdog fallback alone, then the CRG command variants as separate measured runs, then
   the selector save + one A/B validation.
2. Upstream review items: split the two binding YAMLs into their own patches (checkpatch's
   "DT binding docs and includes should be a separate patch"; this series keeps them with
   the driver like `004`/`005`), maintainer identity, phandle property naming, whether an
   `nvmem` cell provider should expose the selector instead of a driver-private write, and
   sharing the watchdog constants with patch `005`.

Evidence: local-only `build/linux-kbuild/waves/upstream-6.18-restart-20261118/`.


## 2026-11-18 (eighth session): peripheral pinctrl (patch 011)

Patch 011-pinctrl-luofu.patch (sha256 700bba855492352afc88dfaeca618c5f3a351bfec1734cdc294f54427fad7442)
adds drivers/pinctrl/hisilicon/pinctrl-luofu.c (848 lines; mainline pinctrl platform driver with
pinctrl/pinmux/pinconf ops and pinconf_generic_dt_node_to_map_all), the new
drivers/pinctrl/hisilicon Kconfig/Makefile wiring (CONFIG_PINCTRL_LUOFU bool), and
Documentation/devicetree/bindings/pinctrl/hisilicon,luofu-pinctrl.yaml (two register windows:
mux 0x3c, cfg 0x100; 37 pin names, 24 group names, 24 function names; generic state form).

Design decisions and findings:
- The vendor tables were re-verified directly from the local hi_kpinctrl.ko fixture (read-only
  pyelftools/capstone): 37 pins, 24 groups with real pin lists, 24 functions, the per-pin 5-hit
  mux table, group-level mux RMW fields, and cfg_off = 4*pin for every pin - the port is checked
  against the binary, not only the older transcription.
- Pinconf was decoded from the module's config ops: pull-up [0], pull-down [1],
  drive-strength [4:2] (raw 3-bit), schmitt [5], input-enable [7], output level [10] (5.10
  PIN_CONFIG_OUTPUT renamed to 6.18 PIN_CONFIG_LEVEL). Unsupported params fail loudly instead of
  being stubbed. The vendor probe writes no mux/cfg register, so our probe performs no register
  access and calls no reset.
- No consumer pin states are attached: the vendor DTS attaches none to MDIO/GEMAC/FMC/UART, and
  which states the wired path needs is [U]. Adding guessed states could remux the console/NAND.

Verified in the isolated clone /home/shibbityshwab/openwrt-lab-pinctrl (openwrt main 27663a0,
linux-6.18.60, OpenWrt GCC 14.4.0, CONFIG_WERROR=y, series 001-010 + 011; thermal 012 excluded):
pristine dry-run RC=0; make target/linux/prepare RC=0; full make target/linux/compile -j16 RC=0
(0 warnings, CC drivers/pinctrl/hisilicon/pinctrl-luofu.o 52,336 B); zImage 3,252,544 B sha256
a4686a41...; _text=stext=0xc0608000 and -DTEXT_OFFSET=0x00608000 unchanged (0x80608000 contract);
CONFIG_PINCTRL_LUOFU=y linked (System.map luofu_pinctrl_probe); dt_binding_check RC=0 (yamllint
unavailable, only that lint skipped); board DTS cpp+kernel-dtc RC=0 with zero stderr (DTB 9,887 B
sha256 eddb76f5...); config canonical re-derivation RC=0 twice, byte-identical and
symbol-identical (290 symbols); checkpatch 0 errors / 0 warnings / 0 checks; MMIO only through
devm_platform_ioremap_resource_byname; no lab/bench/vendor breadcrumbs.

Not proven: never probed or applied on hardware; probe-time/default pin state, consumer state
requirements, drive-strength mA mapping and the MDIO two-state question remain [U]. No boot/image/
bench claims.

## 2026-11-18 (ninth session): thermal sensor + zone (patch 012)

Patch 012-arm-thermal-luofu-tvsensor.patch (sha256
3c42f8e6a08ea159b5c3a074890ff64d8c1476f3cbc16bc7c92780b89c7edb5f) adds
drivers/thermal/luofu_tvsensor.c (read-only sensor registered with
devm_thermal_of_zone_register(), thermal_zone_device_ops.get_temp), its Kconfig/Makefile entry
(CONFIG_LUOFU_TVSENSOR), and Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml.
The board DTS placeholder becomes a real sensor node (reg widened from the vendor 0x14 to 0x100,
vendor IRQ SPI 0 not claimed) plus the cls0-thermal zone with the vendor trips (115 C passive /
125 C critical, 3 C hysteresis, polling-delay = 1000) and no cooling map.

Design decisions:
- The conversion is the vendor v4 path reproduced operation for operation: read +0x24 & 0xffff,
  compute ((165*raw - 32835)/100)*1000 - 40000 millidegrees (raw 240 -> 27.0 C), so values match
  the vendor kernel bit for bit.
- Registry through the 6.18 of-thermal API (the old thermal_zone_of_device_ops interface is gone).
- The reg window is widened to the containing 256-byte block; the vendor's page-granular ioremap
  masked the too-small DT window. Nothing beyond +0x24 is touched.
- Polled, no interrupt, no writes: threshold/IRQ/enable/trim registers are undecoded and no store
  to the block was measured. No cooling map (the vendor cooling device is a network rate limiter
  that depends on the LSW block); sustainable-power is dropped (units [U]).

Verified in the isolated clone /home/shibbityshwab/openwrt-lab-thermal (series 001-010 + 012):
pristine dry-run RC=0; prepare RC=0; full compile RC=0 with 0 warnings (zImage 3,250,848 B sha256
34ed6116...; a supplementary CONFIG_WERROR=y rebuild was also RC=0 and produced the identical
zImage); dt_binding_check RC=0; board DTS cpp+dtc RC=0, zero warnings (DTB 9,799 B sha256
2677acd7...); config canonical re-derivation byte-identical (298 symbols); checkpatch 0/0/0.
Caught in the lane: thermal-zones.yaml requires child names matching *-thermal, so the old
placeholder zone cls0 became cls0-thermal (a pre-existing dt-validate error is gone).

Not proven: threshold/enable/trim/IRQ register model is [U] and deliberately not implemented;
the reg width and the trip policy are [I]/vendor policy rather than our measurement; no clock or
reset is modelled; hardware behaviour is unverified - no boot/image/bench claim.


## 2026-11-18 (tenth session): DWC PCIe host controller (patch 013)

Patch 013-arm-pci-luofu-host.patch (sha256
73c413dda1463842aa4bfda63879147894930c64ef6e6909b7914110c9d01202) adds
drivers/pci/controller/dwc/pcie-luofu.c (467 lines, DWC host glue: the CRG clock plus the four
named apb/pcs/phy/ctrl resets through the reset API, the write-only misc RC-mode/iATU-enable/
LTSSM-enable full-word stores, aligned-dword read_dbi/write_dbi, a config-window pci_ops using
pci_generic_config_read32/write32, CFG0 iATU viewport programming, mem/IO iATU viewports derived
from the DT ranges through the framework, link wait/report and pci_host_probe), the Kconfig/
Makefile CONFIG_PCIE_LUOFU_HOST entry and Documentation/devicetree/bindings/pci/
hisilicon,luofu-pcie.yaml (built on pci-host-bridge.yaml). The shared DTS edits rewrite both PCIe
nodes (reg-names dbi/misc/config, corrected ranges, pcie clock, the four resets, num-lanes=1 and
documented-but-disabled pci59e7,0005 endpoint children); the RC nodes stay disabled because the
write path is unmeasured and the Wi-Fi endpoint driver is [X].

Design rules encoded: never read the RC misc window 0x10161000 (write-only stores only), only
4-byte-aligned dword MMIO accesses, no forbidden CA writes; the iATU tables are driver data, not
vendor DT props.

Validated in the isolated clone /home/shibbityshwab/openwrt-lab-pcie (series 001-013): pristine
dry-run RC=0; prepare RC=0; full compile RC=0 in 1m56s with 0 warnings and CONFIG_WERROR=y;
zImage 3,262,016 B sha256 392d5861...; driver built-in (luofu_pcie_probe in System.map);
_TEXT/TEXT_OFFSET contract unchanged; dt_binding_check RC=0; board DTS compiles RC=0 with zero
warnings (DTB 10,133 B sha256 46fac7ad...); dt-validate RC=0 with no PCIe diagnostic; config
canonical re-derivation byte-identical (306 symbols incl. CONFIG_PCIE_LUOFU_HOST=y); checkpatch
0/0/0; static scans clean (no misc read, aligned readl only, no vendor-only DT props, no lab/bench
breadcrumbs).

Not proven: misc wrapper store semantics, iATU-enable/LTSSM bit interaction, CFG0 per-access
programming, DWC version word 0x8f8, endpoint power GPIOs, radm/linkdown handling, MSI/INTx,
dma-ranges, ACP, ASPM/L1SS. The Wi-Fi PCIe endpoint remains [X]; no boot/enumeration claim.

## 2026-11-18 (cross-lane fix): CRG reset polarity (patch 014)

The PCIe lane found a real defect in patch 004: the vendor CRG reset class is ACTIVE-LOW
(hi_kreset: assert = clear the bit, deassert = set it), while patch 004 implemented the opposite.
The measured evidence is the live census read of 0x14880034 = 0x000ff309 with both PCIe root
complexes up: the four RC0 reset bits are SET in the released state (UPSTREAM-PORT-PLAN arm B/C;
REMAINING-DRIVERS block C). As shipped before 014, reset_control_deassert() would have CLEARED
those bits and driven the RC into reset.

Patch 014-arm-crg-reset-polarity.patch (sha256
739700c0ee0a85f969acbfde33d462fc0d501c1a1df3bff5ef223cdf53949227) swaps the two operations in
luofu_crg_reset_set (assert clears, deassert sets), inverts luofu_crg_reset_status (asserted =
bit clear) and documents the measured polarity in the driver comment and the CRG YAML. Every
consumer (FMC, MDIO/GEMAC, PCIe, watchdog fallback) uses the standard reset API, so releasing a
line now sets its bit as the hardware expects; nothing else needs to change.

Validation: patch 014 applies cleanly to the 004-created files (patch -p1 dry-run and real apply
in a scratch tree); in the combined verification tree (series 001-013 already prepared and
compiled) the patched crg-luofu.o compiles with the tree's OpenWrt GCC 14.4.0 (driver object
27,460 B) and the series 001-013 full compile itself was RC=0 with zero warnings (zImage
3,262,144 B sha256 8654b99a...; all CONFIG_LUOFU_* symbols present; DTS 10,133 B zero warnings;
0 error/warning lines in the compile log). Drive strength: the polarity fix is host-compiled
only and has never run on hardware - the reset class is documented, not hardware-proven by this
series.


## 2026-11-18 (eleventh session): PIE packet-engine draft (patch 015)

Patch 015-net-pie-luofu.patch (sha256
fac5ab382964c4696ba1a305ec4401068edded8372980f0af25bd62ed01446d2) adds
drivers/net/ethernet/hisilicon/luofu-pie.c, its Kconfig/Makefile entry
(CONFIG_LUOFU_PIE) and Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml. This is the
first draft of the PIE half of the wired data path specified in PIE-DATAPATH.md; it is a refusing
draft, not a working network driver.

What the probe does, in order: matches the variant (hisilicon,luofu-pie -> v250 generation; an
unknown generation fails closed), validates the 64 KiB register window (devm_ioremap only - no
MMIO access), gets the unnamed CRG gate clock and one CRG reset line, looks up the four interrupts
by name (ch0-7, ch1-6, ch2-5, ch3-4), requires dma-coherent and a 32-bit DMA mask, allocates the
coherent ring bookkeeping (4 RX rings x 1024 x 32 B, 4 TX rings x 4096 x 32 B) with
dma_alloc_attrs(..., 0), logs and frees the rings, then returns -EOPNOTSUPP with
"data path not started: PIE register/channel inventory is not captured; refusing to arm the
engine". It does not read/write any register, enable the clock, toggle the reset, request an IRQ
or register NAPI; the DTS node is disabled. Compiled scaffolding: the recovered v250 ring/doorbell/
completion/IRQ-mask/enable offsets, 32-byte descriptor accessors (only the recovered RX address+
owner and TX length+address+owner fields; everything unrecovered stays zero and TODO) and a hard
IRQ handler encoding the recorded schedule/mask/ack order and channel pairing. No poll callback:
the RX completion decode is not recovered.

Deliberate decisions: ring geometry is driver data (documented board constants) because the vendor
rx_ring/tx_ring properties are not an ABI and their pair-word decoding is inferred; the inventory
settles it by read-back. Clock/reset are acquired but not driven in probe (bring-up is a hardware
step). No netdev, no WoC, no exported API until the data path exists. Coherent DMA only; the ACP
attribute question stays open.

Validated in the isolated clone /home/shibbityshwab/openwrt-lab-pie (series 001-015, clean
re-extract): pristine dry-run/chained apply RC=0; prepare RC=0 (1,189 patching lines, no .rej);
full compile RC=0 in 1m50s with 0 warnings under CONFIG_WERROR=y; zImage 3,262,336 B sha256
5231214b...; luofu_pie_probe/driver/v250/irq_names linked; TEXT_OFFSET contract unchanged;
CONFIG_LUOFU_PIE=y; dt_binding_check RC=0; board DTS zero warnings pre/post shared.patch (DTB
10,193 B sha256 4e4a0304...); dt-validate RC=0; config re-derivation byte-identical (307 symbols);
checkpatch 0/0/0 (driver 597 lines, YAML 96); static checks show no MMIO/DMA/IRQ/netdev/export in
the probe path and no vendor-only DT properties.

Not proven: any hardware behaviour. Every register offset is from the vendor module, not measured
on this board; ring-pair decoding, descriptor decode, per-channel init, IRQ semantics and ACP
coherency remain [U]; the driver has never been instantiated on ARM or mapped the real window; no
boot/image/install/bench. The attended P3 read-only inventory is the next hardware step (bench is
currently blocked); until then the node must stay disabled.

Combined whole-series verification (in the scratch tree, patches 001-015 applied from the repo): make target/linux/prepare RC=0 and make target/linux/compile RC=0 with 0 warnings/errors; zImage 3,262,792 B sha256 c82d0ffe...; all CONFIG_LUOFU_* drivers built in (including the polarity-fixed CRG); combined DTS compiles with zero warnings (DTB 10,193 B).


## 2026-11-18 (twelfth session): internal LSW switch D1a probe skeleton (patch 016)

Patch 016-net-lsw-luofu.patch (sha256
c15aba7c52b671211334423b919d5ca209906a53b339ff0ccef5259f22a0b68a) adds
drivers/net/ethernet/hisilicon/luofu-switch.c, its Kconfig/Makefile entry
(CONFIG_LUOFU_SWITCH), Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml and
the board DTS node (disabled). This is the D1a gate of the internal-switch plan (LSW-SPEC.md
sections 10.4/11): a resource/binding/probe skeleton, not a switch driver.

Design decisions: the vendor tree describes the switch as two nodes (lsw_dp: 7 windows,
lsw_pfe: 10 windows), but it is one switch, so the binding is one hisilicon,luofu-switch node
with the 15 MMIO windows (five queue-manager + ten classifier/forwarding, 512 KiB each). The
vendor queue (4 MiB) and flow (1 MiB) regions are DDR memory the engine consumes, not MMIO
windows, and their live base is unestablished ([U]), so they are deliberately not modelled.
The two CRG gate clocks and four CRG reset lines are acquired by name but never enabled or
driven. The framework question is settled as switchdev, not DSA (no CPU-port tag; per-port
netdevs exist; LSW-SPEC.md 9.3), so the port model is an ethernet-ports child list where each
port has a reg ordering index (explicitly NOT the recovered hardware port id) and an ethernet
phandle to the GEMAC node that owns that port's netdev. The probe validates all of it, logs,
frees the port references and returns -EOPNOTSUPP; a file-wide grep for readl/writel finds none
(the windows are only ioremapped), no clock is enabled, no reset is toggled, no interrupt is
requested, no netdev is registered and no switchdev notifier is installed (there is no port
netdev to attach to; an empty notifier would reproduce the vendor's stub behaviour). The DTS
node stays disabled, so a normal boot never reaches the refusal.

Validation (isolated clone /home/shibbityshwab/openwrt-lab-lsw, target tree re-synced to the
committed 001-015 state plus patch 016 and the delivered DTS/config edits; the stale kernel
build directory was removed so make target/linux/prepare re-extracted 6.18.60 and re-applied
the whole series from scratch): pristine 6.18.60 dry-run of 016 RC=0; chained apply of
001-016 RC=0 (60 patching lines, 0 rej/orig); prepare RC=0 (1,193 patching lines, 0 rej/orig,
the 016 hunks applied without offsets); full compile RC=0 with 0 warnings under
CONFIG_WERROR=y; zImage 3263624 B sha256 201ba86863d8ee6c14d865702ee2339cd3e373c30fbb4b94983afcc2d72df83f; luofu_lsw_probe and
luofu_lsw_driver in System.map; TEXT_OFFSET contract unchanged;
CONFIG_LUOFU_SWITCH=y; dt_binding_check RC=0; board DTS zero warnings (DTB 10538 B
sha256 ff34bc7f7e744b61bbc0ca7bd0934317e46d837c87d024592fc08335ed5ce4a0); dt-validate reports no diagnostic for the switch node; config canonical
re-derivation byte-identical and symbol-identical to the delivered delta; checkpatch 0/0/0 on
the driver (447 lines) and the YAML (179 lines); shared.patch dry-run/apply/byte-compare OK;
static checks show no MMIO access in the probe, no netdev/switchdev registration, no exported
ABI and no lab/bench breadcrumbs or forbidden addresses.

Not proven: any hardware behaviour. The register/queue/port-id model is recovered offline from
the vendor modules and has never been measured on this board; the port_bitmap decoding, the
LSW-vs-MAC port-id mapping, the EQM command word, the qid-map high-port classes and the
classifier/VLAN table formats all remain [U]. The driver has never run on ARM, never mapped the
real windows at runtime and never registered a port; no boot, no image, no install path and no
bench run. The next hardware step is the attended, read-only register inventory plus a reviewed
write set (LSW-SPEC.md 12.3 W1/W2), after which D1b-D1d (queue maps, PIE host binding, bridge
forwarding) can start. Publication remains NOT READY.

## 2026-11-18 (image-recipe session): the image chain is wired and built

**Status: the target image recipe is implemented and produced a full, validated
sysupgrade container in an isolated `openwrt main` clone (`make world -j16`
RC=0). The artifact has NEVER BOOTED and is NOT installable: the A/B selector
protocol and the install path are [U], `platform_do_upgrade` refuses to write,
and no factory/install image exists. Nothing here is a boot or flash claim.**

### What changed

- `target/linux/hisilicon/image/luofu.mk` is no longer a sketch. The real
  chain is `kernel-bin | luofu-check-textofs | append-dtb | uImage none |
  luofu-fip-slice | luofu-fip-manifest` for the kernel and
  `luofu-rootfs-ubi | luofu-sysupgrade-container | append-metadata` for the
  image; `image/luofu.mk` derives the load/entry address from the loader
  staging base and `TEXT_OFFSET`, regenerates the FIP trailer from our own
  board DTB + loader-match overlay (`fip-slice.py make-trailer`, libfdt apply
  required), assembles/validates the slice with the slot budget and
  load/entry assertions (`fip-slice.py assemble`), writes a manifest, builds
  the squashfs-in-static-UBI rootfs from the measured geometry (checked-in
  `image/luofu-ubinize.cfg`) and packages the container. Every stage fails the
  build closed.
- `target/linux/hisilicon/image/luofu-ubinize.cfg` (new): one static volume
  named `squashfs`, vol_id=0, and a placeholder for the rootfs image path.
- `target/linux/hisilicon/base-files/lib/upgrade/platform.sh` (new): real
  structural `platform_check_image` (tar/`CONTROL`, uImage load/entry, FIP
  trailer/gate/descriptor constants, UBI volume shape, manifest-vs-bytes,
  slot budgets) and a **refusing** `platform_do_upgrade` with the full TODO
  list that must be closed before any write is allowed.
- `target/linux/hisilicon/Makefile`: `KERNELNAME:=zImage` added - without it
  `Kernel/CopyImage` never copies a zImage into $(KDIR) and the image chain
  has no kernel input.
- `tools/luofu-image.py` (new; OpenWrt-tree mapping `scripts/luofu-image.py`):
  `rootfs-ubi`, `sysupgrade` and `check-container`, all fail-closed; it
  re-parses EC/VID/data CRCs, the layout volume table and the static `squashfs`
  volume, refuses vendor input paths and unknown ubinize-cfg keys.
- `tools/fip-slice.py`: the generated manifest comment no longer says that
  `platform_check_image` is unwritten.
- The target mapping must place `dts/luofu-r116.dts` as
  `target/linux/hisilicon/dts/luofu-r116.dts` and `dts/luofu-r116-overlay.dts`
  as `.../luofu-r116-overlay.dtso` (the `DEVICE_DTS_OVERLAY` rule only
  compiles `.dtso` sources), and the two tools as `scripts/fip-slice.py` and
  `scripts/luofu-image.py`.

### How it was run (isolated clone, no shared tree modified)

    cp -a --reflink=auto /home/shibbityshwab/openwrt-lab-verify /home/shibbityshwab/openwrt-lab-image
    # install target/linux/hisilicon/, the two dts files and the two scripts as above
    make defconfig                       # RC=0
    make world -j16                      # RC=0

The clone was built from patches `001`-`015` (`linux-6.18.60`), the state
stated for this task. Patch `016` (task-26, LSW) and its
`CONFIG_LUOFU_SWITCH=y` `config-6.18` line were completed after this build;
they are not part of these artifacts (re-run `make world -j16` after
integration; the hashes will change). A second identical `make world -j16`
produced a byte-identical image.

One pre-existing clone-copy artefact had to be repaired before the build
succeeded: the copied `staging_dir` carried absolute paths to the source clone
(553 files; 131 text files such as `.pc` wrappers and autoconf/libtool
scripts) which produced duplicate staging include paths and broke the `apk`
host build on a shadowed `<endian.h>`. Rewriting the text files to the new
clone path fixed it; this is a reused-clone issue, not a target issue.

### Results

- Image: `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`
  - **6,175,022 B**, sha256 `ea143b03d9d248b3e2b21723ef639e42817e66da847285496ebbc46e12756881`
  - members `sysupgrade-cudy_wr3000-v2/{CONTROL,INSTALL-STATUS.txt,fip-manifest.txt,kernel,root}`
    plus standard appended fwtool metadata (`supported_devices` =
    `cudy,wr3000-v2 hisilicon,luofu-r116 hisilicon,luofu`; revision
    `r0-27663a0`).
- Kernel slice: `cudy_wr3000-v2-kernel.bin`, **3,283,008 B**, sha256
  `23c3286b631a20d1a8fa0d7671d0bea1d7dfbd7f8fdcf7c85a084fba3c5ca636`;
  uImage `Linux-6.18.60` with the DTB appended (`ih_size` 3,272,457 B),
  load/entry `0x80608000`; trailer 10,487 B generated from our DTB + overlay;
  gate `0xd7b7ab1e` at `0x32171a`; 5,367,744 B under the slot budget.
- Rootfs UBI: `cudy_wr3000-v2-rootfs.ubi`, **2,883,584 B**, sha256
  `d3c5611cf49395a7a99c8703331b6545a5b543ae9781041e2571e80a9faff2e7`;
  22 PEBs = 2 layout + 20 data, single static volume `squashfs` (LEB
  126,976 B), all header/data CRCs verified; 21,430,272 B under the slot
  budget. Its source `root.squashfs` is 2,481,002 B (sha256 `c079a1d6...`).
- zImage: 3,262,264 B sha256 `b3227ac3d0ab5265c73d1d25c773767d6c72fc974d85209521620af41664c060`;
  System.map is deleted by an incremental OpenWrt modules rebuild, so the
  recipe reads `_text` from `vmlinux` with the target `nm` (0xc0608000; the
  fallback path was exercised by the rebuild).
- Base DTB 10,193 B sha256 `4e4a0304...`, loader-match overlay DTBO 230 B
  sha256 `8e4ab83b...`, manifest 945 B sha256 `bd976870...`.

### Checks actually run

- `tools/luofu-image.py check-container` on the final `.bin`: **PASS**
  (independent re-read of CONTROL, kernel slice load/entry/trailer/gate/
  descriptor, UBI volume shape, manifest-vs-bytes, slot budgets).
- `platform_check_image` from `base-files/lib/upgrade/platform.sh`, run
  host-side against the real image: **PASS**; corrupted-image, wrong-board and
  foreign-image variants refused (RC=1 / RC=1 / RC=74);
  `platform_do_upgrade` returns 1 with the [U] refusal text.
- Negative tests: fabricated uImage load/entry, slot-budget overflow,
  non-squashfs rootfs, tampered/unknown-key ubinize cfg, corrupted UBI data
  CRC, manifest/sha mismatch - all refused.
- `sh -n`/`bash -n` clean on `platform.sh`; the recipe was parsed and executed
  by the real OpenWrt make.

### What this proves and what it does not

Proven: the image chain is valid OpenWrt idiom and builds end to end from our
own DTB/overlay/kernel/rootfs; the derived load/entry and the FIP gate are
asserted at build time; the rootfs UBI has the measured shape; the container
is structurally self-consistent under independent re-validation; the build is
reproducible byte-for-byte.

Not proven: anything about booting. The stock loader accepting a novel
generated trailer, kernel execution, rootfs mount and overlay, the A/B
selector protocol, the install path and the boot-argument hand-off are all
still [U]. The image is a host build product, not a release and not a
flashable upgrade.

### Next blockers

1. One attended boot of a generated trailer / kernel slice (and then of the
   rootfs UBI) on a recovered unit.
2. Measure the A/B selector read/write path (both env copies, `0x10100C08`)
   and only then implement `platform_do_upgrade`.
3. Execute one install path end to end (vendor-root documented installer or a
   characterized TFTP recovery) before adding any factory/install stanza.
4. Integrate patch `016` with its `config-6.18` symbol and re-run the full
   build.
5. Decide the boot-argument hand-off (`/chosen/bootargs` override question),
   then prove the rootfs mounts.


## 2026-11-18 (A/B install session): selector interface + sysupgrade write path

**Status: the A/B selector read/write interface and `platform_do_upgrade` are
IMPLEMENTED and validated host-side; NOTHING HAS BOOTED OR BEEN FLASHED.** The
container still says NOT FLASHABLE (it has never booted) and one attended A/B
validation remains owed.

### What changed

- patch `017` adds `drivers/soc/hisilicon/luofu-selector.c`, its YAML binding,
  Kconfig/Makefile and an ABI doc: `current_pair` (read-only; 0x10 = a,
  0x11 = b, 0x21 = manual-b, 0x00 = uninitialised, anything else unknown) and
  `next_pair` (write-only; accepts only "a" -> 0x10 and "b" -> 0x11, verifies
  the read-back, -EINVAL otherwise) over the shared SYSCTRL syscon cell at
  0xc08. Probe performs no register access; the page stays shared with the
  machine-restart driver (patch `010`), so there is no second mapping.
- the board DTS gains the enabled `boot-selector` node (sysctrl phandle +
  `hisilicon,selector-offset = <0x0c08>`), `CONFIG_LUOFU_SELECTOR=y` joins the
  config delta and `ubi-utils` joins `DEFAULT_PACKAGES` (the write path calls
  `ubiformat`, which `switch_to_ramfs()` copies only if it is installed).
- `base-files/lib/upgrade/platform.sh`: `platform_check_image` is unchanged in
  scope; `platform_do_upgrade` is implemented. The active pair is the
  `luofu-selector` `current_pair` AND the booted rootfs slot
  (`/sys/class/ubi/ubi0/mtd_num` + its `/proc/mtd` label), which must agree.
  The target is the other pair, located by label (`kernelb`+`rootfsb` after an
  A boot, `kernela`+`rootfsa` after a B boot). Kernel first (`mtd write`,
  erase+write), then the rootfs (`ubiformat`), each read back (kernel md5
  equals the member; the rootfs re-reads the UBI#/UBI! headers). Only then
  `sync` and the selector store, read back. Any failure leaves or restores the
  selector on the active pair, so the next boot still comes up on the
  untouched pair; the function never reboots (the framework does), never
  erases `rootfs_data` (mtd15, a separate shared partition) and never writes
  the env blocks. `/proc/mtd` is the only partition source; the measured
  kernel slot (8,650,752 B), the rootfs partition (24,379,392 B) and the image
  budget (24,313,856 B) are all checked.
- `tools/test-platform-abinstall.sh` (new): host-side mocked harness that
  sources the real `platform.sh` and runs `platform_do_upgrade` against fake
  `/proc/mtd`, fake `ubi0/mtd_num`, fake selector sysfs and
  `mtd`/`ubiformat`/`sync`/`reboot` shims. 52/52 assertions pass against the synthetic fixture containers; with
  `--container` the same suite runs the real built sysupgrade.bin
  (6,185,262 B, sha256 `0303d76a...`) through the real
  `platform_check_image` and all 58 assertions pass.

### Validation (isolated clone `/home/shibbityshwab/openwrt-lab-abinstall`)

- OpenWrt main commit `27663a0`, `linux-6.18.60`; the target was installed
  from the repository skeleton with patch `017` and the shared edits, the
  kernel build dir deleted and re-created by `prepare`.
- `patch -p1 --dry-run` of `017` against pristine `linux-6.18.60`: RC=0;
  serial apply `001`-`017`: RC=0, 0 `.rej`/`.orig`.
- `make target/linux/prepare V=s`: RC=0 (1,198 patching lines, 0 rej/orig).
- `make target/linux/compile V=s -j16`: RC=0, **0 warnings** under
  `CONFIG_WERROR=y`; `CONFIG_LUOFU_SELECTOR=y` built-in
  (`luofu_selector_probe` etc. in `System.map`); zImage 3,262,472 B sha256
  `a4e4f6c0612317dd1b4ea98f86408de811f6af8635c419b5d9fc7113c1ed5b2e`;
  `_text = stext = 0xc0608000` and `DTEXT_OFFSET=0x00608000` (TEXT_OFFSET
  contract unchanged).
- `make ARCH=arm dt_binding_check
  DT_SCHEMA_FILES=Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-boot-selector.yaml`:
  RC=0 (`yamllint` unavailable, that lint step skipped as in earlier lanes).
- stock DTS compile (cpp + kernel dtc): RC=0, **zero warnings**, DTB 10,664 B
  sha256 `5726f7cd3798113a0881c86a9e2778860b5171ef2ee29fbb93970a6bb8a6b857`;
  `dt-validate` RC=0 with no `boot-selector` diagnostic (the pre-existing
  draft-DTS uart diagnostics remain, unrelated to this node).
- config canonical re-derivation, two runs: RC=0/RC=0, byte-identical
  (`4106877fdb6dad7ee9d5d2895ed6eb878da0c6a70ff4067d664be3da380a2a1f`) and
  symbol-identical to the delivered delta (309/309 symbols).
- `checkpatch.pl`: 0 errors / 0 warnings on the driver (251 lines), the YAML
  (59 lines) and the ABI doc (37 lines).
- `sh -n` on the installed `platform.sh`: RC=0; `bash -n` on the harness: RC=0.
- `tools/test-platform-abinstall.sh`: 52/52 fixture assertions; 58/58 with
  `--container` on the real built sysupgrade.bin (6,185,262 B sha256
  `0303d76a987fd16aeee7833db0d8188d6390129c62e1fc048560a971b922dce2`,
  the artifact recorded by the image-recipe lane).

### What this proves and what it does not

Proven (host-only): patch `017` applies to pristine `linux-6.18.60` and after
`001`-`016`; the whole series prepares and compiles with no warnings; the
selector YAML passes `dt_binding_check`; the board DTS compiles with zero
warnings; the config re-derivation is idempotent; `checkpatch` is clean; the
mocked install path selects the right inactive pair, writes in the right
order, moves the selector last and refuses every bad input tested.

Not proven: any hardware behaviour. The post-boot cell value, the loader's
env re-stamp, NAND read-back under ECC, bad-block behaviour, the first boot of
the written pair and rollback are all still [U]. The A/B protocol is closed
only by one attended validation.

## 2026-11-18 (selector-fix session): corrected selector contract + boot pair recorder

**Status: patch 017 and the target's base-files were corrected against the local
evidence reconstruction (docs/SELECTOR-0x21.md), the target image gained the
boot-time running-pair recorder, and everything was re-validated host-side.
NOTHING HAS BOOTED OR BEEN FLASHED and no hardware behaviour is claimed.**

### What changed

- **Patch 017 corrected** (task-31): the comments, Kconfig help, YAML binding and
  ABI doc no longer claim a vendor 0x21 provenance. They now state the
  evidence: the bootloader selects the pair from its env bootflag, the SYSCTRL
  cell is its opposite-pair/failed-boot record (0x10 = A recorded, 0x11 = B
  recorded, 0x00 = power-on cleared, 0x21 = the project's bench handover
  value), and vendor userspace writes only 0x10/0x11. The next_pair
  store is now the loader-style **low-byte read-modify-write** (upper 24 bits
  preserved, whole-cell read-back, -EIO if it does not stick); probe still performs
  no register access and the page stays a shared syscon.
- **Boot-time running-pair recorder** (new
  base-files/etc/init.d/luofu_record_pair): START=96, after
  /etc/init.d/done; derives the booted rootfs pair from
  /sys/class/ubi/ubi0/mtd_num + its /proc/mtd label (optional
  /proc/cmdline ubi.mtd= cross-check), waits bounded for the selector
  interface, logs, and records the running pair only when the cell holds one of the
  two established pairs and differs (idempotent). It never writes
  manual/unknown values, never blocks the boot for long and never touches the env
  blocks. This is the target's equivalent of the vendor boot_done step and is
  what keeps the two-witness installer check valid.
- **platform.sh contract comments corrected**: the env is the selector (not an
  output re-stamped from the cell); the handover is the loader's mismatch path;
  the two-witness check is documented as valid only because the recorder runs on
  every boot; a warning (not a refusal - refusals unchanged) is emitted if the
  running system has no recorder.
- **Incoming shared edits** (target-skeleton/docs/incoming/selectorfix/shared.patch):
  TARGET-DESIGN 3.4/5.4, SKELETON-SMOKE (this session), TARGET-SKELETON,
  target-skeleton/README, the DTS boot-selector comment, and correction notes in
  FLASH-PLAN, HYBRID-IMAGE-PLAN, DEPLOYMENT-DECISION and the abinstall port-notes.
- **Review form regenerated**: commit message 0017 rewritten, series/0017 and
  01-commit-messages.md regenerated (gen-series.py --check clean), validation
  logs/hashes replaced, cover letter/README/04-validation updated. This session
  record is itself part of the shared patch.

### Validation (isolated clones; no device)

- **Upstream series** (tools/run-validation.sh on the pristine 6.18.60 tarball):
  patch(1) 17/17 RC=0 (0 fuzz, the two known 0008 offsets), git am 17/17 RC=0,
  trees identical, checkpatch default/strict 0 errors / 46 warnings (0017: 3
  review items, 436 checked lines). Logs/hashes replaced under
  upstream/series-6.18/validation/.
- **Kernel clone** (/home/shibbityshwab/openwrt-lab-selectorfix-k, a reflink copy
  of the A/B-install lane clone, target reinstalled): make target/linux/prepare V=s
  RC=0 (1,198 patching lines, no .rej/.orig in the kernel tree);
  make target/linux/compile V=s -j16 RC=0, **0 warnings / 0 errors** under
  CONFIG_WERROR=y; zImage 3,265,056 B sha256
  5b417ce976d8bc7fb90f34056e3a3da02f271f1613fb7945ec54d99639839bb1;
  CONFIG_LUOFU_SELECTOR=y built in; _text=stext=0xc0608000,
  DTEXT_OFFSET=0x00608000.
- **Binding/DTS/config**: boot-selector YAML dt_binding_check RC=0; board DTS
  cpp+kernel dtc RC=0 zero warnings (10,664 B DTB sha256 5726f7cd...,
  unchanged - only comments change in the DTS source, delivered via the shared
  patch); dt-validate RC=0, no boot-selector diagnostic; config re-derivation
  RC=0 twice, byte-identical (4106877f...), 309/309 symbols identical;
  checkpatch file-mode 0 on driver/YAML/ABI.
- **Mocked harness** (tools/test-platform-abinstall.sh): 94/94 fixture
  assertions and 100/100 with the real 6,185,262 B container
  (e869f105... from the previous lane). Covered: install path (unchanged
  refusals + the recorder warning), recorder recording/idempotence/fail-safe paths
  and the corrected record semantics.
- **Image rebuild** (/home/shibbityshwab/openwrt-lab-selectorfix-img, a reflink
  copy of the image clone, target reinstalled): make world -j16 RC=0 with
  **0 warnings / 0 errors**; the rebuilt container is 6,185,262 B sha256
  362ca4ece4df2b77b0e527ad5d948bbd997201ccb3bf6e2518d5ddaccbb59899; its rootfs
  carries /etc/init.d/luofu_record_pair (mode 0755, byte-identical to the
  repository file) and the enabled /etc/rc.d/S96luofu_record_pair symlink, and
  tools/luofu-image.py check-container PASSes; the harness passes 100/100
  against this new container.
- sh -n clean on the installed recorder and platform.sh; bash -n clean on
  the harness.

### What this proves and what it does not

Proven host-only: the corrected patch applies to pristine 6.18.60 and through
the series; the series prepares and compiles with no warnings; the binding
passes dt_binding_check; the board DTS compiles cleanly; the config delta is
idempotent; checkpatch is clean; the driver's low-byte RMW store builds and is
file-checkpatch-clean; the recorder is installed and enabled in the built
rootfs and behaves correctly against mocked sysfs/proc; the install path keeps
its ordering and refusals and now documents/depends on the recorder.

Not proven: any hardware behaviour. The in-boot cell value versus the running
pair (including after our recorder), the POR clearing chain, the meaning of the
cell's upper 24 bits, the env re-stamp, the first boot of a written pair and the
full A/B install are all still [U]; the recorder has never run on hardware.

---

## 2026-11-18 (SMP-RE session): platsmp port 018, host validation only

The vendor SMP enable-method was decoded and a platsmp port drafted (task-32). The
release sequence (SYSCTRL pen word at `0xc00`, CRG resume-bit read-modify-write at
`0x38` with the bit numbers from the vendor `core-info`, then the wakeup IPI; no
SMC/PSCI) comes from the two stock vendor vmlinux images and the vendor base DTB;
[SMP-RE.md](SMP-RE.md) is the evidence + design + staged-plan document and
`patches-6.18/018-arm-mach-hisilicon-smp.patch` is the implementation (new
`arch/arm/mach-hisilicon/platsmp.c`, `CPU_METHOD_OF_DECLARE` for the documented
method `hisilicon,luofu-smp`, machine Kconfig/Makefile/descriptor refactor). The DTS
`enable-method` line and the doc updates are in the incoming integration patch
(`docs/incoming/smp/`), not edited here.

Validated host-only in an isolated btrfs-reflink copy of the shared clone
(`openwrt-lab-smp`, `openwrt main` `27663a0`, `linux-6.18.60`, the tree's own
OpenWrt GCC 14.4.0; the shared clone was not modified):

- patches `001`-`018` apply to a fresh pristine `linux-6.18.60` tree in order,
  RC=0, no `.rej`/`.orig`; patch `018` dry-run on the `001`-`017` tree RC=0 and
  byte-identical after apply;
- `make target/linux/prepare` RC=0 (1,202 patching-file lines), then
  `make target/linux/compile` **RC=0** (20m06s), zImage 3,264,472 B sha256
  `ae59d998...`, **zero warnings attributable to any new/changed file** (the 38
  modpost `missing MODULE_DESCRIPTION` warnings are the exact same pre-existing
  baseline as the 001-017 and restart lanes);
- `arch/arm/mach-hisilicon/platsmp.o` is built and its symbols are in System.map
  (`luofu_smp_boot_secondary`, `luofu_smp_prepare_cpus`, `luofu_smp_ops`,
  `__cpu_method_of_table_luofu_smp`);
- `TEXT_OFFSET` unchanged: `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000` and
  `_text = stext = 0xc0608000`; SMP symbols already resolved in the config
  (`CONFIG_SMP=y`, `CONFIG_HAVE_ARM_SCU=y`, `CONFIG_HAVE_ARM_TWD=y`) - the patch
  needs no config change;
- `make kernel_oldconfig` three runs RC=0; the resulting kernel `.config` is
  byte-identical to the 001-017 build and the re-derived target `config-6.18`
  fragment has the same 300-symbol set and is byte-idempotent;
- `dt_binding_check` RC=0 for all 13 series YAMLs; the board DTS (with the
  enable-method hunk) compiles with **zero dtc warnings** (DTB 10,710 B sha256
  `bc599a4e...`); `dt-validate` has only the same six pre-existing findings as the
  001-017 lane and none naming the SMP nodes;
- checkpatch: `platsmp.c` 0/0 and the three edited files 0/0.

Not proven: any hardware behaviour. The release has never run on our kernel; the
second-core hang/cache/coherency/GIC/TWD risks and the attended acceptance gates are
in [SMP-RE.md](SMP-RE.md). No device, router, UART, TFTP or flash tool was touched.

## 2026-11-18 (bdinfo MAC provisioning session): factory LAN/WAN MACs from bdinfo

**Status: the target image now provisions its LAN/WAN MAC addresses from the factory
`bdinfo` partition, and the whole logic is host-validated; NOTHING HAS BOOTED and no MAC
has been read from a real unit or applied to a netdev.**

### What changed

- `target/linux/hisilicon/base-files/lib/functions/luofu-mac.sh` (new): reads the
  plaintext 6-byte factory MAC at `bdinfo+0xde00` through `mtd_get_mac_binary` (direct
  `/dev/mtdN` character-device fallback, found by partition label), validates it,
  derives LAN = base MAC and WAN = base+1 with the vendor `macaddr_add` arithmetic, and
  falls back to a clearly logged locally-administered random pair when the partition is
  missing/corrupt; the bdinfo partition is only ever read.
- `target/linux/hisilicon/base-files/etc/board.d/02_network`: sources the helper and
  records both addresses through `ucidef_set_interface_macaddr` (the normal board.d ->
  board.json -> `/bin/config_generate` -> netifd path). The interface placeholder is
  deliberately untouched (the switch port map is still missing, TARGET-DESIGN.md 8.1), so
  until that map exists the addresses are recorded in board.json only. The file also stops
  sourcing the non-existent `/lib/functions/board.sh`; this release defines
  `board_config_update`/`board_config_flush` in `/lib/functions/uci-defaults.sh` and
  the read/MAC helpers in `/lib/functions/system.sh`.
- `tools/test-mac-provisioning.sh` (new): host-only mocked harness for the helper (fake
  `/proc/mtd`, fake mtd devices, mocked `mtd_get_mac_binary`/`/dev/urandom`/
  `ucidef_set_interface_macaddr` seams): 103/103 assertions pass.
- no kernel patch, config delta or DTS change is part of this lane.

### Rules implemented (evidence: MISC-SOC-BLOCKS.md section 1)

- LAN = the factory base MAC itself; WAN = LAN+1 under the vendor/OpenWrt `macaddr_add`
  rule (OUI preserved, low 24 bits incremented modulo 2^24, so `...:ff:ff:ff` wraps to
  `...:00:00:00`). Sources: vendor 2.5.24 `99_fixwan:41` (LAN from
  `mtd_get_mac_binary bdinfo 0xde00`), vendor 2.4.15 `board.d/02_network:25-29`
  (canonicalized `bdinfo mac` for LAN, +1 for WAN, `ucidef_set_interface_macaddr`),
  vendor 2.4.15 `99_fixwan:17-52` (per-port application).
- validation rejects non-MAC input, all-zero, all-`0xff` (erased), multicast,
  locally-administered, OUI `00:00:00` and the `...:ff:ff:ff` WAN-wrap case. The last
  two are conservative project policy [I], not vendor rules; a rejected copy is logged and
  replaced with a random pair, never "repaired".
- the U-Boot env `ethaddr` is not read or written (it is a locally-administered loader
  value, MISC-SOC-BLOCKS 1.2), and no factory bytes are printed or embedded anywhere.

### Validation (isolated clone; shared clone untouched)

    cp -a --reflink=auto /home/shibbityshwab/openwrt-lab /home/shibbityshwab/openwrt-lab-mac
    make defconfig
    make world -j16

- `make defconfig`: RC=0, `CONFIG_TARGET_PROFILE="DEVICE_cudy_wr3000-v2"`.
- `make world -j16`: **RC=0** (started 18:34, image written 18:44 = about 10 minutes on 16 cores in the cache-reused clone; an explicit incremental \`make world -j16\` re-run also returned RC=0), built container `openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`,
  **6,185,262 B**, sha256 `54652e56241e26eba0e95bc7fa663950c6e69ddb834dba77beb5b1afbeea2cac`; `tools/luofu-image.py check-container`
  PASS on the built bytes.
- built rootfs: `/lib/functions/luofu-mac.sh` (mode 0644) and `/etc/board.d/02_network`
  (mode 0755) are present and byte-identical to the repository files; `sh -n` clean on
  the installed copies and `tools/test-mac-provisioning.sh` passes 103/103 when pointed
  at them.
- the target tree for the build came from `opensource` HEAD (`5989404`, series
  `001`-`018`) plus this lane's two base-files files; the concurrently in-flight patch
  `019` (CRG clock tree) was excluded on purpose.

### What this proves / does not prove

Proven host-only: the board.d path reads the right six bytes of the right partition,
validates and derives the vendor LAN/WAN addresses, records them through the standard
board.json path, logs and falls back on every bad input tested, never writes the factory
partition, and the image installs the files. Not proven: any hardware behaviour. The
bdinfo read has never run on a booted target; preinit `/dev/mtdN`/`/dev/mtdblockN`
availability is argued from `CONFIG_DEVTMPFS_MOUNT=y` + the MTD core (not measured); and
the addresses cannot reach a netdev until the LSW switch port map exists
(TARGET-DESIGN.md 8.1). No device, router, UART, TFTP or flash tool was touched.

## 2026-11-19 (attended hardware bring-up): our kernel + our rootfs reached userspace on the WR3000 v2

**Result: SUCCESS.** U-Boot's failover path booted the written pair: `Image Name: Linux-6.18.60`
and our kernel banner on real hardware (**1 CPU**: this image was built from patches 001-017; the SMP release patch 018 was not part of the build, so CPU1 bring-up caught by that build is untested), UBI attached `mtd14`
(`rootfsb`), `ubiblock0_0` was created from the static squashfs volume, the kernel mounted the
squashfs root read-only, and `/sbin/init` -> preinit -> procd (early/watchdog/ubus/init) ->
kmodloader -> urngd -> overlayfs (tmpfs fallback) completed; the console reached
"Please press Enter to activate this console." Evidence: `build/evidence-firstboot/`
(`uart-boot8-SUCCESS.log`, `SUCCESS.md`); the failure logs show the earlier empty-cmdline
VFS panic, the U-Boot data abort from a DTB bootargs property, the 512-byte ubiblock
truncation, and the XZ ARM-BCJ decompression failures that led to the fixes.

**Fixes that made it boot (all in the target tree now):** built-in `CONFIG_CMDLINE_FORCE`
(loader supplies no cmdline; DTB bootargs crashed U-Boot), 4 KiB squashfs payload padding
before ubinize (ubiblock rounds volumes to 512 B), `CONFIG_XZ_DEC_ARM=y` (OpenWrt squashfs uses
`Filters selected: arm`), semantic UBI-volume read-back verification in the first-install tool
(`ubiformat` rewrites EC image_seq, so a raw-partition hash can never match), and the
single-write sysfs selector recipe.

**Not there yet (observed during the same boot):** no network (LSW/PIE/GEMAC netdevs are not
wired), no persistent overlay (the image has no rootfs_data volume; mount_root falls back to
tmpfs), and `/lib/functions/board.sh` missing for `board_detect`. The running image is the
debug-CRC build used to localize the XZ failure; a clean production rebuild exists but has not
been re-flashed (re-flashing needs the stock userspace or physical TFTP recovery, because the
running image has no network).

**Follow-up (same day, afternoon, host-only):** both build-side gaps were closed and the
production image was rebuilt with the **full series `001`-`023`** - see the
"2026-11-19 (afternoon, host-only)" section below. The rootfs slot now carries an empty
dynamic `rootfs_data` UBI volume (autoresize; the kernel formats it on the first read-write
mount, which gives a persistent overlay instead of the tmpfs fallback), and
`board.d/02_network` no longer sources the nonexistent `/lib/functions/board.sh` (the build
now fails if any installed board.d sources a helper that is not in the image). **The image
booted above was built from patches `001`-`017` only (UP, 1 CPU); this rebuild is the first
image carrying `018`-`023` (SMP + the new LSW/PIE/GEMAC drivers) and the next attended boot
is its first hardware test.** The rebuild has **not been flashed**; the running box still has
the 2026-11-19 debug-CRC image.

## 2026-11-19 (afternoon, host-only): persistent overlay + board_detect fix + production image rebuild

**Status: HOST-ONLY BUILD RESULT. The image below has NOT been flashed; the box still runs
the 2026-11-19 debug-CRC image (UP, 1 CPU). The overlay layout, the SMP release and the
`018`-`023` drivers as a set have never run on hardware.** This closes the two build-side
gaps observed in the attended bring-up above and rebuilds the production image from the
full kernel series `001`-`023`. **The booted 2026-11-19 image was built from patches
`001`-`017`; this rebuild is the first image that carries SMP (patch `018`), the clock tree
(`019`), PIE (`015`/`020`), LSW W1 (`021`), the gated LSW qid-map write path (`022`) and the
GEMAC netdev readiness path (`023`) together, and the next attended boot is its first
hardware test.**

### What changed in the target tree

- `target/linux/hisilicon/image/luofu-ubinize.cfg` now describes the **standard OpenWrt
  two-volume rootfs UBI**: the static squashfs volume `squashfs` (unchanged) plus the
  persistent overlay as an **empty dynamic volume `rootfs_data`** (vol_id 1,
  `vol_size=1MiB`, `vol_flags=autoresize`). It is deliberately not pre-formatted: the
  kernel's first read-write UBIFS mount formats an empty UBI volume (`fs/ubifs/sb.c`
  `create_default_filesystem()`; `fs/ubifs/super.c` only refuses that for a read-only
  mount), fstools `mount_root` finds the volume by name (fstools `libfstools/ubi.c`; the
  UBI driver has a higher priority than the mtd/partition drivers) and UBI autoresize grows
  it to the free PEBs of the slot at attach (`drivers/mtd/ubi/build.c:autoresize()`, with
  the flag read in `drivers/mtd/ubi/vtbl.c`). Those facts are [V] read from the tree's own
  kernel and fstools sources; the combined behaviour on the board is [U] until the next
  attended boot.
- `tools/luofu-image.py` validates the new shape from the bytes it built: exactly two
  volume-table records (`squashfs` static + `rootfs_data` dynamic/autoresize/empty), the
  cfg-`vol_size`-vs-`reserved_pebs` cross-check, the reserved-PEB-vs-slot budget (31 of
  185 PEBs) and the existing per-LEB data CRCs. It refuses a pre-filled `rootfs_data`
  volume, a non-autoresize overlay, a single-volume cfg, a non-static squashfs volume and
  the old single-volume containers.
- `image/luofu.mk` gains a fail-closed **base-files guard**: every `. /lib/...` source in
  the installed `/etc/board.d/*` must exist in the image, otherwise the build fails. That
  is the 2026-11-19 `board_detect` failure class: the stale skeleton `02_network` sourced
  `/lib/functions/board.sh`, which **does not exist anywhere in OpenWrt main** (`board_name`
  is a function in `/lib/functions.sh`, fed by `/tmp/sysinfo/board_name` written by
  `/lib/preinit/02_sysinfo`). The image now ships the corrected board.d - it sources only
  `uci-defaults.sh`, `system.sh` and our `luofu-mac.sh` - and the MAC provisioning
  helper stays wired to it (103/103 mocked assertions still pass).
- The container marker changed from `NOT FLASHABLE` to
  **`BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED`**: it records the 2026-11-19
  userspace boot of an earlier build of this recipe, states that THIS exact rebuild has not
  been flashed/booted, and keeps the literal `NOT FLASHABLE` substring that
  `first-install-stock.sh` and the target `platform_check_image` grep for. It also records
  that the booted build was UP (patches `001`-`017`), had no `rootfs_data` volume and
  carried the stale board.d.

### Build (isolated clone; no device, no flash)

- Clone: `/home/shibbityshwab/openwrt-lab-selectorfix-img`, target tree synced from the
  repository `opensource/target-skeleton` HEAD: **patches `001`-`023`** - `018` SMP, `019`
  clock tree, `020` PIE datapath, `021` LSW read-only probe, **`022` LSW gated qid-map write
  path and `023` GEMAC netdev readiness** - the full board-level base-files, the 48,026-byte
  board DTS (the net-lane DTS that enables the LSW/PIE/GEMAC nodes), the unchanged
  `config-6.18` (`2982d66330ac5c9114340a6afa34da70964f4b92c474be874c7f3157fcebc2d2`), the
  new image recipe and the updated tool.
- **Patch application: 23/23 applied cleanly.** `make target/linux/prepare V=s` RC=0,
  1,223 patching-file lines, 0 failed hunks and no `.rej`; the only `.orig` is the
  pre-existing patch-`008` offset backup. Every series patch is named in the prepare log
  (`03-build-final-001-023/applied-patches-001-023.txt` in the local evidence).
- `arch/arm/mach-hisilicon/platsmp.c` (159 lines), `luofu-switch.c` (1,494 lines after
  `022`), `luofu-gmac.c` (423 lines after `023`) and `luofu-pie.c` (1,346 lines) are in the
  built tree; `luofu_smp_prepare_cpus`, `luofu_switch_probed`, `luofu_gmac_probe` and
  `luofu_pie_armed` are in `System.map`.
- `make defconfig` RC=0; `make world -j16` **RC=0** with no `error` and no `warning:`
  line in the log. (A reflinked *copy* of the clone cannot build as-is: the copied
  `staging_dir` keeps absolute paths to the source clone and the `apk` host build then
  fails on a shadowed `<endian.h>` - the same copy-artefact repair the 2026-11-18
  image-recipe session recorded. Building in the clone in place avoids it.)
- Boot fixes preserved: `CONFIG_CMDLINE_FORCE=y` plus the built-in `CONFIG_CMDLINE`
  string, `CONFIG_XZ_DEC_ARM=y`, and `--pad-align 4096` in `image/luofu.mk` (the built
  rootfs squashfs is 2,492,638 B and is padded to a 4 KiB multiple before ubinize).
- Kernel: `_text = stext = 0xc0608000` (physical `0x80608000` = the uImage load/entry);
  `arch/arm/mach-hisilicon/platsmp.c` is compiled (`luofu_smp_prepare_cpus` /
  `luofu_smp_boot_secondary` in `System.map`) and the DTS carries
  `enable-method = "hisilicon,luofu-smp"`; all `CONFIG_LUOFU_*` symbols plus
  `CONFIG_UBIFS_FS`/`CONFIG_OVERLAY_FS`/`CONFIG_MTD_UBI_BLOCK=y` are present. **No patch
  in 018-023 has ever executed on hardware.**
- Boot safety of the newly enabled nodes: the DTS now enables the LSW/PIE/GEMAC nodes
  (12 `status = "okay"` nodes in the board DTS), but the drivers still fail closed on a
  default boot - LSW probe refuses (`-EOPNOTSUPP`) unless `luofu_lsw.arm=1` (and writes the
  W2 qid maps only with `luofu_lsw.qidmap=1`), PIE stays unarmed without `luofu_pie.arm=1`,
  and GEMAC refuses/defer until PIE is armed and the LSW qid map is ready, before any
  ioremap/clk/reset. The default `CONFIG_CMDLINE` carries none of those parameters, so the
  next boot should only log refusals - [U] until it is booted.

### Result

| item | value |
| --- | --- |
| image | `openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`, **6,205,742 B**, sha256 `15dae772984669545f175806cff70216039684e494f6196e0a94b71530026b99` |
| kernel slice | 3,305,732 B, sha256 `6c99aa3d0c7b4e104c74fef5d14f355e483b90ff0fa91aa9fc2c4f4e10ea95ca`; uImage `Linux-6.18.60`, ih_size 3,293,803, load/ep `0x80608000`; trailer 11,865 B (base DTB 11,571 B), gate `0xd7b7ab1e` at `0x326fde` |
| rootfs UBI | 2,883,584 B / 22 PEBs, sha256 `81d049511cb1775a1b6efc9fab9182eda86ea8069f45a75580dada9ab8330028`; volume 0 `squashfs` static (used/reserved 20 PEBs), volume 1 `rootfs_data` dynamic, EMPTY in the image, reserved 9 PEBs, flags `0x01` (autoresize); 31 PEBs reserved of the 185-PEB slot, 21,430,272 B under the 24,313,856 B image budget |
| installed marker | `BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED` (full text in the container's `INSTALL-STATUS.txt`) |

### Checks actually run (all host-side)

- `tools/luofu-image.py check-container` on the final bytes: **PASS** (kernel slice +
  manifest cross-check, two-volume UBI, budgets, marker).
- Negative tests: old single-volume container, single-volume cfg, pre-filled
  `rootfs_data` image, non-autoresize overlay and non-static squashfs volume are all
  refused (RC 1/3); no positive path was skipped.
- Installed rootfs: `/etc/board.d/02_network` is byte-identical to the repository file and
  contains no `board.sh` reference; `/lib/functions/luofu-mac.sh`,
  `/etc/init.d/luofu_record_pair` (plus `S96` symlink) and `/lib/upgrade/platform.sh` are
  installed; `sh -n` is clean on all four.
- `target-skeleton/tools/test-platform-abinstall.sh --container <new image>`: **100/100**
  (the real `platform_check_image` accepts the new container; the A/B write path is
  unchanged).
- `tools/tests/first-install.test.sh --real-container <new image>`: **144/144** (the parent
  first-install tooling accepts the container and its real check passes).
- `target-skeleton/tools/test-mac-provisioning.sh`: **103/103** (MAC helper intact).
- Patch application: 23/23 series patches in the prepare log, RC=0, 0 failed hunks, 0
  `.rej` (the tree's own target patches are all named in
  `03-build-final-001-023/applied-patches-001-023.txt` in the local evidence).
- `config-6.18` is unchanged vs the repository (`2982d663...`); the resolved kernel
  `.config` carries exactly the symbols the `001`-`023` series needs, all already in the
  committed delta, plus the boot fixes and SMP from the same file.

### What this proves and what it does not

Proven (host-only): the image chain builds RC=0 from the full `001`-`023` series with the
boot fixes intact (23/23 target patches named, no rejects); the rootfs slot carries exactly the
two intended UBI volumes with the
expected flags/reserved PEBs and fits the slot budget; the overlay is the standard OpenWrt
shape implemented by fstools `mount_root` and the kernel UBIFS autocreate path; the board.d
that produced the boot warning is gone and a build guard prevents its return; the MAC helper,
the A/B install checker and the first-install tooling all still accept the artifact.

Not proven - and not claimed: any hardware behaviour of this rebuild. The overlay volume has
never been mounted on the board (the booted image had none), the SMP release has never run,
patches `018`-`023` have never executed as a set, and the image has not been flashed. **The
2026-11-19 booted image was built from patches `001`-`017` only; this rebuild is the first
image carrying the full series, so the next attended boot is the first test of SMP plus the
new LSW/PIE/GEMAC code paths.** The image is also not byte-reproducible across kernel rebuilds (the UTS banner/build timestamp is
embedded in the zImage - the pre-existing caveat). Follow-ups: `platform_check_image` still
validates only volume 0 of the rootfs UBI (the PEB-count invariant is unchanged, but a check
for the `rootfs_data` record would be stronger); the parent
`tools/first-install-stock.sh` NOT-FLASHABLE warning text (`tools/first-install-stock.sh:505`)
still says "it has never booted" and should be reworded when that file is touched. Evidence is
local-only under `build/linux-kbuild/waves/2026-11-19-overlay-image/`.


## 2026-11-18 (CRG clock-tree session): gate parents, fixed clocks, read-only PLLs/muxes (patch 019)

The CRG clock tree was decoded from the vendor module and the patch-`004` driver was
extended with the clock-parent, PLL and mux model (task-35). The decode and the
remaining [U] items are in [MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) section 5; the patch
header carries the exact register/field notes. **Host-only: no device, router, UART,
TFTP, flash or bench tool was touched; nothing was booted.**

### What was added

- `drivers/clk/hisilicon/crg-luofu.c`: the 16 gate clocks are parented on their real
  input clocks (apb/ahb/gemacN/lsw_dp/lsw_pfe, decoded from the vendor devicetree)
  instead of the previous single `apb_clk` parent; the two PLLs (control words at
  `0x198`/`0x1e0`, status word `0x90` bits 30/27) and the two muxes (`0x138` bit 3 CPU,
  bit 1 efuse) are registered as read-only clock outputs of the same provider. The
  provider resolves its seven input clocks from the node `clocks`/`clock-names` with
  `of_property_match_string` + `of_clk_get_parent_name`, so no global clock name is
  guessed. The probe remains register-free; all clocks are `CLK_IGNORE_UNUSED`.
- `include/dt-bindings/clock/hisilicon,luofu-crg.h`: four new indices appended
  (`LUOFU_CLK_PLL_CPU` 16, `LUOFU_CLK_PLL_LSW` 17, `LUOFU_CLK_CPU_MUX` 18,
  `LUOFU_CLK_EFUSE_MUX` 19; `LUOFU_CLK_NR` 20). The existing 16 gate IDs are not
  renumbered.
- the YAML binding gains the required `clocks`/`clock-names` input-clock model and its
  example; the PLL/mux description states they are read-only. No new Kconfig symbol or
  config line: the driver is still `CONFIG_LUOFU_CRG`.
- the target DTS (in the incoming integration patch, not edited here) gains the four
  missing fixed clocks (`gemacN_clk` 125 MHz, `lsw_dp_clk` 166 MHz, `lsw_pfe_clk`
  333 MHz, `efuse_ring_clk` 20 MHz) and the `clocks`/`clock-names` on the CRG node.

### The decode (why the values are what they are)

The vendor `hi_clk.ko` (GPL, not stripped, 15,556 B, md5
`3b331b9058e13bc409dd8f0040af3b11`, identical in the 2.4.15 and 2.5.24 rootfs) was
disassembled host-side. `hi_pll_recalc_rate` decodes a fractional PLL:
`rate = parent * (int + frac/2^24) / fbdiv`, followed by two divider-select fields
(`word0[10:8]` and `word0[6:4]`, each `+ div-factor`, skipped when zero; bit 3 of word0
byte 0 selects the integer-only path). The fields are [V] and the arithmetic order is
taken from the module; the on-board value is **not** measured - the boot-programmed
words have never been read back, so the derived rate is [U] until a `clk_summary`/
control-word readback (attended). The writer (`0x4c4`) busy/re-lock protocol was
decoded but is **not** implemented: its feedback-word encoding is not verified and a
wrong store can hang the core. The muxes are read-only (`set_parent` returns
`-EOPNOTSUPP`): `0x138` bit 3 selects the CPU clock source and must never be writable,
and the efuse mux is only switched by an efuse programming path that this tree does not
have.

### Verification (exact commands and results)

Validated in an isolated btrfs-reflink copy of the shared clone (`openwrt-lab-clk`,
`openwrt main` `27663a0`, `linux-6.18.60`, the OpenWrt GCC 14.4.0 of that tree; the
shared clone was not modified):

- patches `001`-`019` apply to a fresh pristine `linux-6.18.60` tree in order, RC=0,
  no `.rej`/`.orig`; patch `019` dry-run on the `001`-`018` tree RC=0, apply RC=0,
  `git apply --check` RC=0, and the applied files are byte-identical to the series
  tree;
- `make target/linux/prepare` RC=0 (patch `019` applied: 3 patching-file lines), then
  `make target/linux/compile` **RC=0**, **0 warnings** in the whole log and 0 errors;
  `arch/arm/boot/zImage` 3,264,560 B sha256 `4456b0ec...`,
  `drivers/clk/hisilicon/crg-luofu.o` 43,900 B;
- `TEXT_OFFSET` unchanged: `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`; both
  `arch/arm/kernel/.head.o.cmd` and `arch/arm/boot/compressed/.head.o.cmd` carry
  `-DTEXT_OFFSET=0x00608000`; `System.map` gives `_text = stext = 0xc0608000`;
  the CRG objects are linked (`luofu_crg_probe`, `luofu_crg_pll_recalc_rate`,
  `luofu_crg_mux_get_parent` in `System.map`);
- config: no patch-introduced symbols; `make kernel_oldconfig` five runs RC=0, the
  re-derived target `config-6.18` is byte-idempotent (`4106877f...`, 300 symbols, the
  same set as the `001`-`018` lane) and the kernel `.config` is byte-identical to that
  lane (`1b9b084e...`); `CONFIG_LUOFU_CRG=y` and `CONFIG_COMMON_CLK=y` unchanged;
- `dt_binding_check` for the extended YAML: RC=0, the example preprocesses and compiles
  (`DTEX`/`DTC [C]`), no diagnostic mentions the file; the only non-yamllint caveat is
  the same pre-existing `realtek,rtl8152.yaml` schema note and the "yamllint not
  installed" skip as the earlier lanes;
- board DTS (with the four fixed clocks + CRG `clocks`/`clock-names`): cpp RC=0;
  patched-kernel `dtc` RC=0 with **zero warnings (stderr 0 bytes)**; DTB 11,346 B
  sha256 `a31ab53f...`; `dt-validate` RC=0 with only the same six pre-existing findings
  as the 001-018 lane and **none** naming the CRG or a new clock node;
- checkpatch: `crg-luofu.c` 0/0, the dt-bindings header 0/0 and the YAML 0/0; the bare
  patch file only reports the expected working-format metadata ("Missing commit
  description") plus the series-wide "DT binding docs should be a separate patch" note
  (the whole 001-019 series keeps binding+driver in one patch per block).

Not proven: any hardware behaviour, and specifically the PLL **rate values** (the
equation is decoded from the module, not measured), the PLL status-bit meaning and the
mux parent read-back. No device, router, UART, TFTP or flash tool was touched; the
kernel has never been booted. The attended checks remaining: one boot with a read-only
probe to print `clk_summary` (CPU/LSW PLL rates, `cpu_mux`/`efuse_mux` parents, gate
parents) and read the two control words at `0x198`/`0x1e0`; and a read-only census of
the `0x90` status bits. No write experiment is needed for this read-only stage.

---

## 2026-11-18 (first-install session): stock-to-image path designed + fail-closed tooling

The first-install question of TARGET-DESIGN.md §5.5 is now answered as a design, host-side
only. The chosen path is the measured vendor-root install (option 2): from a root shell on
the stock firmware, write the FIP kernel slice with mtd (by label) and ubiformat the rootfs
UBI into the inactive pair, read both regions back with full-region sha256, then hand over
the boot selector by writing the written pair's recorded low byte (0x10 = A pair, 0x11 = B
pair) to the SYSCTRL cell - never the env blocks, never the active pair, never
rootfs_data/upgrade/vendor data.

Deliverables (parent private repository unless noted):

- [FIRST-INSTALL.md](FIRST-INSTALL.md) (this published half) - design: preconditions, exact
  sequence, expected loader/UART output, rollback/recovery cost, risks, attended
  checklist, [U] list;
- parent `tools/first-install-stock.sh` - read-only by default; writes only with
  `--write`/`--handover`/`--reboot` plus `--attended` and `--recovery-armed` and a
  validated `--image`; drives the box through the project rsh/rscp helpers, feeds the
  device side over ssh stdin, re-derives partitions by label, validates the container with
  `luofu-image.py check-container` plus independent byte checks, verifies full-region
  sha256 read-backs, moves the selector last, and prints the recovery plan on every
  failure path;
- parent `tools/tests/first-install.test.sh` - mocked harness (fake `/proc/mtd`,
  `/sys`, fake mtd devices, mtd/ubiformat/sync/reboot/fw_printenv/dmesg/df shims, fake
  rsh/rscp, synthetic container): 144/144 assertions pass.

Validation was host-only: `bash -n` clean on the tool, `sh -n` clean on the embedded
device script, the real built container (6,185,262 B sha256 `362ca4ec...`) passes the
real `luofu-image.py check-container` in a dry run, and the harness covers pair selection
both ways, kernel-before-rootfs write order, both full-region read-backs, every refusal
path, read-back and bad-block aborts, env-never-written, and the reboot classification.
Nothing was written, flashed, booted or recovered; the first attended execution, the loader
env re-stamp, the first boot of the written pair and the recovery cost of a
successful-but-unwanted boot remain [U] (FIRST-INSTALL.md sections 8-11).

---

## 2026-11-18 (PIE datapath session): PIE ring engine + GEMAC glue (patch 020)

The refusing PIE draft was extended into a **draft ring engine** built from the measured
2026-11-18 baseline, and the partial GEMAC was extended into a phylink/netdev front end
glued to it (task-46, lane `pie-datapath`). The register/channel model is the one in
[PIE-DATAPATH.md](PIE-DATAPATH.md) sections 4-5; the measured values are in
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 2. **Host-only: the shared
clone was not modified; no device, router, UART, TFTP, flash or bench tool was touched. No
PIE register has ever been written and nothing has been booted.**

### What was added

- `drivers/net/ethernet/hisilicon/luofu-pie.c`: the patch-`015` scaffolding becomes a
  ring engine: coherent 32-byte descriptor rings for the measured **two active RX and two
  TX channels** (RX log2 8 descriptors + 1536-byte buffers, TX log2 12), RX buffer
  allocation/DMA mapping, the recovered v250 global and per-channel register initialization
  (0x1000, 0x0180/0x0200/0x0380, 0x3210/0x3290/0x3310, 0x3204/0x3208, 0x3404/0x3408,
  0x3200 bits 0-7), per-channel TX/RX ring programming (base/config, RX size word at
  0x3504 = buffer size >> 7), mask-all then unmask-the-active-channels, the global RX
  enable at 0x3200 bit 0, four requested IRQ vectors with the recovered schedule/mask/ack
  order, per-RX-channel NAPI poll (consumes completions by read pointer and re-arms the
  descriptors; **does not decode** the still-[U] completion status/length words, so
  completions are counted as drops), a TX producer (linearize, coherent map, recovered
  length/address/owner descriptor fields only, free-running write pointer doorbell) with
  poll-only completion (the vendor has no TX IRQ), and a **read-back verification** of
  every programmed register that tears the engine down on any mismatch. The data path is
  **arm-gated**: without the `luofu_pie.arm=1` kernel command-line parameter the probe
  validates all resources and ring tables and refuses with `-EOPNOTSUPP` **before the
  first register write**, clock enable, reset toggle or IRQ request.
- `drivers/net/ethernet/hisilicon/luofu-pie.h` (new): the internal backend interface
  between the front end and the engine (singleton `luofu_pie_armed`/`luofu_pie_probed`/
  `luofu_pie_xmit`, exported GPL).
- `drivers/net/ethernet/hisilicon/luofu-gmac.c`: the patch-`009` resource driver becomes
  a 6.18 phylink/netdev front end ("the MAC owns the net_device and the engine owns the
  rings", the vendor split). It registers a netdev **only when the PIE backend is armed**
  (otherwise it refuses loudly, or defers if PIE has not probed yet), forwards
  `ndo_start_xmit` to the engine, starts/stops phylink in open/stop, and its only MAC
  register writes are the measured 1 Gbps link-up words (PORT_MODE `0x010c0008` bracketed
  by MODE_CHANGE_EN `0x01b4` bit 0) and link-down. `mac_config()` is a deliberate no-op:
  the lab driver measured that touching the block from phylink's config path hangs the AHB
  bus. For speeds other than 1 Gbps there is no measured PORT_MODE encoding, so the driver
  warns and leaves the port unconfigured instead of guessing.
- `Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml`: new required
  `hisilicon,rx-ring` / `hisilicon,tx-ring` ring tables (two words per RX channel:
  log2 descriptor count + buffer size; TX: log2 descriptor count + channel count); the
  YAML description now states the draft/arm-gated status. The GEMAC YAML description is
  updated to the front-end/engine split.
- Kconfig: `CONFIG_LUOFU_PIE` help rewritten; `CONFIG_LUOFU_GMAC` now `depends on
  LUOFU_PIE` and `select PHYLINK`, so the 6.18 config delta resolves `CONFIG_PHYLINK`
  from `m` to `y` (the only config change of this patch; re-derived canonically below).
- the target DTS (incoming integration patch, not edited here): the PIE node carries the
  measured ring tables `<0x8 0x600 0x8 0x600>` / `<0xc 0x2>`; both the PIE and the GEMAC
  nodes stay `disabled`.

### Which writes are evidence-backed, which are inferred

Every MMIO write in the arm path is annotated in the source. Summary:

| write | class |
| --- | --- |
| 0x1000 clear bit 0 / set bits 6-8 | [V] recovered v250 reg_init code + [V] measured post-boot 0x000a01c8 |
| 0x0180/0x0200/0x0380 + ch*4 = 1; 0x3210/0x3290/0x3310 + ch*4 = 3/2/8 | [V] recovered reg_init values + [V] measured on all eight slots; field meanings [U] |
| 0x3204 = 0; 0x3208 = 0x7d; 0x3404 = 0; 0x3408 = 0 | [V] recovered reg_init values + [V] measured (0x3404 measured nonzero after per-channel init - our driver only writes the documented reg_init 0 and does not guess the per-channel field) |
| 0x0400 + ch*4 = 1 (mask all eight slots), then 0 for the active RX channels | [V] recovered mask model; measured ch0/1=0 and ch2/3=1; the ch4-7 measured 0 is [U] (masking all is the safe reading and keeps the WoC half from raising unhandled interrupts) |
| 0x3000/0x2000 + ch*0x10 ring base = coherent ring DMA address | [V] register/field recovered; the *values* are our own allocations by design (the vendor's DDR addresses are runtime state, not a model fact) |
| 0x3004/0x2004 + ch*0x10 = log2 descriptor count | [V] recovered write; [V] measured TX 0xc / RX 0x8 equal the live DT first words |
| 0x3504 + ch*0x10 = RX buffer size / 128 | [I] decode (the measured 0xc equals live size word 0x600 >> 7, not the descriptor-ring bytes) |
| 0x3008/0x2008 write pointers (doorbells) | [V] recovered offsets/width; [I] free-running-counter semantics and the initial full-ring push |
| 0x3200 bit 0 (global RX enable) | [V] recovered function + [V] measured post-boot set |
| TX descriptor words: length/address/owner only | [V] recovered normal non-TSO branch; every offload/type word stays zero |
| **not written at all** | RX completion status/length decode path [U]; 0x3404 per-channel field [U]; 0x3700 GRO size (no GRO ring on this board, left alone); 0x2200 DFX; WoC rings/channels; ACP attributes; no MAC writes beyond the measured link-up/down words |

The GEMAC link-up writes are the only MAC writes: they are [V] measured on the live vendor
system (PORT_MODE 0x010c0008 at 1 Gbps, 0x000c0408 link-down) and were executed by the
out-of-tree lab driver on this board (`lab/luofu-gmac/luofu-gmac.c`);
`ETHERNET-BRINGUP-FRAMING.md` records that path running. Nothing else in the MAC is
programmed (no MAC filter/address register, no static port configuration).

### Verification (exact commands and results)

Validated in an isolated btrfs-reflink copy of the shared clone
(`openwrt-lab-pie-datapath`, `openwrt main` `27663a0`, `linux-6.18.60`, the tree's own
OpenWrt GCC 14.4.0; the shared clone was not modified):

- patches `001`-`020` apply to a fresh pristine `linux-6.18.60` tree in order
  (`patch -p1 --batch`): **RC=0, 0 `.rej`**; the two `.orig` files are patch `008`'s
  pre-existing pristine-tree hunk-offset backups (present before this patch, none from
  `020`); patch `020` dry-run on the `001`-`019` tree RC=0 and the applied driver files
  are byte-identical to the generated sources;
- `make target/linux/prepare` RC=0 (1,211 patching-file lines, patches `001`-`020`),
  no `.rej`/`.orig` in the extracted tree;
- `make target/linux/compile` **RC=0**, **0 warnings / 0 errors in the whole log**
  under `CONFIG_WERROR=y`; `CC drivers/net/ethernet/hisilicon/luofu-pie.o` and
  `luofu-gmac.o` built; `arch/arm/boot/zImage` **3,276,656 B**, sha256
  `dfc7b753d34ad443a76ed3ffe60e77d7e63a28b771546ba0dccc4a40a9515a5a` (the build
  timestamp/UTS banner is embedded, so the hash is build-time dependent - the same
  pre-existing caveat as the earlier sessions);
- `TEXT_OFFSET` unchanged: both `arch/arm/kernel/.head.o.cmd` and
  `arch/arm/boot/compressed/.head.o.cmd` carry `-DTEXT_OFFSET=0x00608000` and
  `System.map` gives `_text = stext = 0xc0608000` (load/entry `0x80608000`);
  PIE/GEMAC symbols linked: `luofu_pie_probe`, `luofu_pie_driver`, `luofu_pie_xmit`,
  `luofu_pie_armed`, `luofu_pie_probed`, `luofu_pie_rx_poll`, `luofu_gmac_probe`,
  `luofu_gmac_driver`, `phylink_create` and `phylink_of_phy_connect` are in
  `System.map`; the objects are `luofu-pie.o` 76,740 B and `luofu-gmac.o` 45,492 B;
- resolved config: `CONFIG_LUOFU_PIE=y`, `CONFIG_LUOFU_GMAC=y`, `CONFIG_PHYLINK=y`
  (was `m`), `CONFIG_NET_VENDOR_HISILICON=y`, `CONFIG_WERROR=y`;
- config canonical re-derivation (`yes '' | make kernel_oldconfig`): two runs RC=0, the
  emitted target `config-6.18` is byte-idempotent (`76aaafc8...`, 300 symbols); the
  committed annotated delta has the identical 300-symbol set (`SYMBOL_SETS_IDENTICAL`)
  and merges through `scripts/kconfig.pl` to exactly the derived config
  (`ANNOTATED_CONFIG_MERGES_IDENTICALLY`, no warnings); the only symbol change against
  the `001`-`019` delta is `CONFIG_PHYLINK` `m` -> `y` (the GEMAC select);
- `dt_binding_check` for the two changed YAMLs: **RC=0**; both examples preprocess and
  compile (`DTEX`/`DTC [C]`); `yamllint` is not installed, so the tree skips that one lint
  step with its own warning (same pre-existing limitation as the earlier lanes);
- board DTS (with the ring properties): cpp RC=0; patched-kernel `dtc` RC=0 with
  **zero warnings (stderr 0 bytes)**; DTB **11,430 B**, sha256
  `d3a9dd86ab5e82919da2f9e7645b401534c01173869c207e447cf7977ae5e44e`; `fdtget` shows
  pie `status = "disabled"`, `hisilicon,rx-ring = <0x8 0x600 0x8 0x600>`,
  `hisilicon,tx-ring = <0xc 0x2>`, gmac3 `disabled`; `dt-validate` RC=0 with only the
  same five pre-existing findings as the earlier lanes (the `realtek,rtl8152.yaml`
  schema note, `arm,sp804` compatible and the two `snps,dw-apb-uart` clock-name findings)
  and **none naming the PIE or GEMAC nodes**;
- checkpatch (`--no-tree -f`): pie driver 0/0/0 (1,346 lines), internal header 0/0/0
  (22 lines), gmac driver 0/0/0 (372 lines), PIE YAML 0/0/0 (128 lines), GEMAC YAML
  0/0/0 (100 lines);
- static checks: no RC-misc window (`0x10161000`) and no forbidden CA
  (`0x400392f0`) access anywhere in the new code; no vendor-only DT properties
  (`rx_ring`/`tx_ring`/`hwver`/`acp-*`/`hsan,*`); no lab/bench breadcrumbs; the arm
  path is the only path that writes PIE MMIO and it is unreachable without the
  module-parameter opt-in.

Evidence is local-only under
`parent:build/linux-kbuild/waves/2026-11-18-pie-datapath-020/` (gitignored): prepare and
compile logs, the canonical config re-derivation, the DTS/DTB artefacts, checkpatch logs,
patch copies and `SHA256SUMS`.

### Proven vs unverified

**Proven, host-only:** the driver files are mainline-API 6.18 code (platform driver, DMA
API, reset/clk frameworks, NAPI, phylink) that applies as patch `020` to pristine
6.18.60, applies/prepares/compiles in the OpenWrt clone with zero warnings, keeps the
TEXT_OFFSET/load-address contract, produces a dt_binding_check-clean binding and a
zero-warning board DTS, and reproduces its config delta canonically. The probe cannot
touch the engine on a normal boot: the arm gate is closed, the DTS node is disabled, and
the non-arm path has no MMIO, clock, reset, IRQ or NAPI operation.

**Not proven - any hardware behaviour:** every register offset/value is recovered from
the vendor module or measured on one vendor boot, not written by our code; the engine has
never been armed by this driver; the RX completion decode, the TX descriptor offload words
(kept zero), the free-running doorbell/write-pointer semantics, the 0x3504 decode, the
ch4-7 mask question, the ACP coherency question and behaviour under traffic are all open.
The GEMAC netdev has **no RX delivery** (PIE decode [U]) and no frame can be claimed to
reach a wire (internal-switch egress map is missing); the link-up words have been executed
by the out-of-tree lab driver, not by this in-tree driver. Nothing has booted.

### Attended checks needed (before any traffic claim)

1. Enabling the nodes plus `luofu_pie.arm=1` on a recovery-armed bench boot: the first
   PIE MMIO write this project makes. Capture the arm log, the read-back verification
   result and the register dump; confirm no AHB hang and that the pointer words move.
2. Reconcile the RX completion decode with known-frame traffic (the poll currently
   recycles buffers and counts drops); then re-enable a delivery path.
3. The internal-switch egress port/qid map is the long pole: until it is programmed, TX
   descriptors may be produced but no frame is guaranteed to leave a MAC port.
4. Confirm the ACP coherency question for 0x250 under traffic (PIE-DATAPATH section 8).


## 2026-11-18 (LSW init session): internal-switch read-only D1b baseline probe (patch 021)

**Status: HOST-ONLY, READ-ONLY. Nothing has run on the board; no boot, image, flash or
bench claim. Publication remains NOT READY.**

Patch `021-net-lsw-init-luofu.patch` (sha256
`de69296d80f2419b574d3af4ec4a8b83c877a711d278fc71350d14a231fe307e`) extends the
patch-`016` internal-switch D1a skeleton with the measured 2026-11-18 "W1" baseline
([INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3) and turns it into a
**read-only D1b baseline probe**. It adds a measured register-map header
(`drivers/net/ethernet/hisilicon/luofu-switch-regs.h`), extends the binding/board DTS to
the two engine DDR windows plus the CRG syscon phandle and `port-bitmap`, and extends
`luofu-switch.c` with: the 17-window resource model (15 MMIO windows mapped, the two DDR
regions validated but never mapped), a powered-state gate (CRG gate word `0x20` bits 0/1
read-only through the CRG syscon, four reset lines through the reset API - the reset class
is active-low per patch `014`), a 30-word measured register baseline, a word-for-word
read-only validation of both qid maps against a derived contiguous allocation model, and
per-port owner-device resolution for the switchdev port model. **The driver contains no
`writel()`**: no register is written, no clock is enabled, no reset is driven, no netdev
or switchdev notifier is registered, and probe always fails closed with `-EOPNOTSUPP`.
The DTS node stays `disabled`.

### Design decisions

- **17 windows, two kinds.** The vendor `lsw_dp` node has 7 windows, two of which
  (`queue`, `flow`) are DDR the engine consumes, and `lsw_pfe` has 10 MMIO windows.
  The binding models one switch with 17 windows in address order: the measured live DDR
  bases `queue = 0x80000000/4 MiB` and `flow = 0x80400000/1 MiB` (post-loader-fixup
  values, [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 3.1) plus the 15
  x 512 KiB register pages.  The driver validates base+size of the DDR windows against the
  measured values and never ioremaps them; the layout inside the regions stays [U].
- **No hardware ID register is invented.** The read-only inventory found no ID register on
  this block; identity is the devicetree `hwver` (0x250, match data) plus the measured
  register/table baseline.  The baseline exact-match checks are deliberately strict: they
  encode "this is the one measured boot's state", not a semantic claim, and a mismatch is a
  refusal to proceed rather than a hardware fault.
- **Powered-state gate before any read.**  The inventory was taken on a clocked,
  out-of-reset block.  The 6.18 clock consumer API has no "is enabled" query
  (`clk_is_enabled()` was removed), so the probe reads the CRG gate word `0x20` bits
  0/1 through the CRG **syscon** (a read of the shared page; the driver never writes it)
  and checks the four reset lines through `reset_control_status()`.  If the block is
  unclocked or held in reset the probe refuses without a single block access.
- **W2 model validated without writing.**  The vendor qid-map write shape is decoded
  ([LSW-SPEC.md](LSW-SPEC.md) 7.3) and the measured tables are [V].  The driver re-derives
  the allocation ("one contiguous qid space from 1, 8 qids per port, populated rows in
  order") and compares every word: 128 table-A words + 256 table-B words (four identical
  u32 per 16-byte entry) = 384 words.  That is the D1b W2 exit criterion applied read-only;
  the derived write set is tabulated in [LSW-SPEC.md](LSW-SPEC.md) 7.7 and is **not
  implemented**.
- **No netdev/switchdev registration.**  The port netdevs belong to the GEMAC side, whose
  data path does not exist (patch `009` is a resource/binding half; patch `020` adds a
  draft netdev front end only behind its attended arm gate).  The probe resolves each port's
  `ethernet` phandle to its owning platform device and records it, but a switchdev
  notifier without real port netdevs would reproduce the vendor's stub behaviour; it is
  therefore not registered.

### Validation (isolated clone + pristine tree; shared clone untouched)

Series validation in an isolated clone copy (`/home/shibbityshwab/openwrt-lab-lswinit`,
copied from the clock-tree clone; no shared clone was modified) and in a fresh pristine
`linux-6.18.60` extraction:

- the full series **001-021** applies to pristine `linux-6.18.60` in order: 21 patches,
  82 files patched, **0 failed hunks, 0 `.rej`/`.orig`, RC=0** (patch `020`, the
  parallel PIE lane, is included at its snapshot sha256
  `806a113c9dc581ccb3d9248f1298f4cf8a48e1645228b525b1a4853f70bfa6e8`; patch `021`
  applies after it with a 6-line offset in `drivers/net/ethernet/hisilicon/Kconfig`);
- `make target/linux/prepare` RC=0 (1,215 patching lines, no rejects) and
  `make target/linux/compile V=s -j10` **RC=0 under `CONFIG_WERROR=y` with 0 errors and
  0 warnings** (121 s wall clock; zImage 3,279,096 B sha256
  `96b927d35e2e6c6be9980c7683de20c20a288a5df58ddf08ed7fbbe9bb1e92d3`;
  `CONFIG_LUOFU_SWITCH=y`, `CONFIG_PHYLINK=y`, `luofu_switch_probe` in
  `System.map`);
- `_text = 0xc0608000` and `swapper_pg_dir = 0xc0604000`; the compressed early-boot
  code materializes `movw #0x8000` / `movt #0x60` (0x00608000) at both head.S sites,
  so the `TEXT_OFFSET` contract is unchanged;
- the kernel config delta (including patch `020`'s `CONFIG_PHYLINK=y`
  resolution) is **symbol-identical** to the committed delta after a canonical
  `make kernel_oldconfig` (0 symbol diff; the regenerated file body differs only in the
  hand-maintained provenance header) and two consecutive `kernel_oldconfig` runs emit a
  byte-identical file (sha256 `76aaafc8b51fe94f7989c99572da583467b1ec922e56a82f357c12b83737bc7f`);
  patch `021` adds no Kconfig symbol, so it cannot change the delta;
- `dt_binding_check` **RC=0** for `Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml`
  (dtschema 2026.9; the tree's optional `yamllint` step is skipped - the package is not
  installed on the host, same limitation as previous sessions);
- the board DTS compiles with the patched kernel's own dtc to **11,516 B**
  (sha256 `43c1e94bef3e3e35a639523aab57b7d2618f0f02ef26c60747be89e674c24638`) with
  **zero warnings**; `dt-validate` against the generated
  processed schema produces **no diagnostic for the switch node** (10 pre-existing
  diagnostics remain on unrelated TODO nodes, e.g. the SP804 timer and the UARTs - a
  full-draft `dtbs_check` is still not claimed clean);
- `checkpatch.pl --no-tree -f`: **0 errors / 0 warnings / 0 checks** on the modified
  Kconfig, the 931-line driver, the 214-line register header and the 223-line YAML;
- `shared.patch` applies to the `opensource/` root (dry-run, apply and byte-compare).

Static checks on the driver: no `writel()`/`writew()`/`writeb()` outside comments, no
`regmap_write`/`regmap_update_bits`, no `clk_enable`/`clk_prepare_enable`, no
`reset_control_assert`/`reset_control_deassert`, no `register_netdev`/switchdev
registration, and the two DDR windows are explicitly `continue`d before the ioremap call.
The read set contains only offsets the 2026-11-18 inventory actually read.

### What this proves and what it does not

Proven (host-only): the patch is clean upstream-style code, applies and compiles in the
series, the binding passes `dt_binding_check`, the board DTS is warning-free and the switch
node validates, and the compiled driver would refuse safely (the refusal path is the only
exit from probe).  The model is checked against measured state - but only by inspection of
code paths and by the numbers in this document: **the probe has never run, not even on a
stock vendor boot**, so the gate/table comparison logic and the powered-state read have
never executed on ARM.

Not proven: any hardware behaviour.  The write set has never been executed (W3-W6), the
EQM indirect command word/queue-table layout and the classifier/L2/VLAN formats remain
unknown, the LSW-vs-MAC port-id mapping and the 40..47 class meaning remain unknown, and
whether the block can be read safely in a *state* other than the measured vendor one is
untested (the gate only proves CRG clock/reset state).  No boot, no bench run, no image and
no upstream review.  The next hardware step is the attended W3 (gates/resets only) followed
by W4/W5 qid-map writes.

Evidence (local-only, gitignored):
`build/linux-kbuild/waves/2026-11-18-lsw-init/` - 21-patch snapshot + sha256,
`final-series-validation.log`, `prepare-final-0021.log`, `compile-001-021.log`,
`board-dts-final.dtb`, `dt-validate-final.out`, `apply-series.log`,
`static-checks.log` and the delivered patch bytes.

---

## 2026-11-19 (connectivity session): LSW W2 qid-map write path + GEMAC netdev readiness (patches 022/023)

**Task:** the image boots to userspace but has no network. This session implements the minimum
connectivity path host-side: the internal-switch port model and the first (reviewed) switch write
set, plus the GEMAC/PIE netdev readiness contract, so an attended boot can reach a LAN interface.
**Host-only: no device access, no flash, no boot.** Publication remains NOT READY.

### What changed

- **Patch 022** (`022-net-lsw-qidmap-write-luofu.patch`) - turns the patch-021 read-only LSW probe
  into a two-gate staged write path and wires the port model:
  - default (no parameters) is unchanged read-only behaviour: validate the 17 windows, the CRG
    clocks/resets, the port model, the 30-word measured baseline and the two qid maps, then refuse
    with `-EOPNOTSUPP` without writing;
  - `luofu_lsw.arm=1` = the attended **W3** step: enable the two CRG gate clocks, release the four
    CRG reset lines (documented C1/C2 rows of LSW-SPEC 7.7), re-check the powered state read-only
    through the CRG syscon and the reset API, then snapshot the 30 baseline words and both qid
    tables *read-only* and refuse - **no LSW register is written in this step**;
  - `luofu_lsw.arm=1 luofu_lsw.qidmap=1` = the attended **W4/W5** step: first a pre-write
    conformance gate (every qid-map word outside the derived allocation model must read zero,
    i.e. the block must be in the measured state the model was derived against), then program the
    W2 write set Q1/Q2 - table A ports 4-12 and table B ports 40-43, 13 rows x 8 slots, value
    `0x100 | qid` - then read back every programmed word and restore the found values and fail
    closed on the first mismatch;
  - new `luofu-switch.h` publishes `luofu_switch_probed()`/`luofu_switch_ready()`; the egress map
    only reports ready after the full readback verification;
  - the devicetree port model now carries the measured hardware port number
    (`hisilicon,port` = 9..12 for gmac1..4, from the vendor DT's GEMAC `port` cells, live set
    9-12); the driver validates each against the populated qid rows and rejects a port without a
    row. Binding updated accordingly.
- **Patch 023** (`023-net-gmac-netdev-connect-luofu.patch`) - makes the GEMAC netdev usable:
  - netdev registration now requires **both** `luofu_pie_armed()` and `luofu_switch_ready()`
    (Kconfig `LUOFU_GMAC` now `depends on LUOFU_SWITCH`); a boot that cannot carry a frame
    registers nothing;
  - `phylink_config.type = PHYLINK_NETDEV` with `config.dev = &ndev->dev` - the patch-020 draft
    used `PHYLINK_DEV`, which does not drive the carrier from the phylink resolve work, so a real
    link would have looked dead to userspace (ETHERNET-BRINGUP-FRAMING.md section 2 species B);
  - netdev address from `of_get_mac_address()` (standard `mac-address`/`local-mac-address`/nvmem
    properties) with a logged random locally-administered fallback; the bdinfo factory MAC has no
    nvmem representation yet at this point - **superseded later the same day by the
    bdinfo-MAC-nvmem session, which wires it as an nvmem cell; see the last session record** - so
    a DHCP test sets it from userspace (attended step);
  - link-up log line with interface/speed/duplex; the only MAC register writes remain the measured
    1 Gbps link-up/link-down words.
- **shared.patch** (this directory): board DTS enables the `lsw`, `pie` and `gmac1`-`gmac4` nodes
  and adds `hisilicon,port` to the four switch ports; `config-6.18` provenance annotations updated
  (no symbol changes); `README`, `TARGET-DESIGN`, `LSW-SPEC` and `PIE-DATAPATH` status updated.

### Gate model (fail closed, unchanged PIE gate)

| boot parameter | LSW driver | PIE driver | GEMAC netdevs |
| --- | --- | --- | --- |
| none (normal boot) | read-only validate + refuse | gate closed, validate + refuse, no register write | PIE refused -> no netdev |
| `luofu_lsw.arm=1` | W3 power/snapshot, no LSW register write, still refuses | unchanged | no netdev (no egress map) |
| `luofu_lsw.arm=1 luofu_lsw.qidmap=1` | W3 + W4/W5 qid-map write + readback, READY | unchanged | still refuses until PIE is armed |
| `luofu_pie.arm=1 luofu_lsw.arm=1 luofu_lsw.qidmap=1` | READY | arms rings/interrupts | netdevs register (eth0..eth3 in DT order); carrier driven by phylink |

The existing `luofu_pie.arm=1` semantics are untouched.

### Validation (isolated clone `/home/shibbityshwab/openwrt-lab-net`; shared tree untouched)

- **Series apply, pristine tree:** all **23 patches apply in order** to pristine `linux-6.18.60`
  with `patch -p1 -N`: **23 patches, 90 files patched, 0 failed hunks, 0 `.rej`/`.orig`, RC=0**
  (`09-pristine-apply.log`); the series is also applied by the OpenWrt pipeline.
- **Prepare** (`make target/linux/prepare V=s`): **RC=0**, 1,223 `patching file` lines, no failed
  hunks. One `Kconfig.orig` file is GNU patch's default `--backup-if-mismatch` backup created when
  patch 021 applies its Kconfig hunk at offset 6 (a pre-existing series behaviour, not a failure;
  no `.rej`).
- **Full kernel compile** (`make target/linux/compile V=s -j16`): **RC=0**, **0 errors, 0
  compiler warnings** in 1,254 s. `CONFIG_WERROR=y` is resolved in the build tree from the
  OpenWrt `CONFIG_KERNEL_WERROR=y`; the three new/changed driver objects compile under `W=1`
  with no warnings. zImage **3,281,376 B**, sha256
  `5903db04928e58214a816a5809f186d579857c8e1c5c0e695cc9035bd68e91ca`.
- **Load contract unchanged:** `_text = 0xc0608000`, `swapper_pg_dir = 0xc0604000`,
  `DTEXT_OFFSET=0x00608000` in both `arch/arm/kernel/.head.o.cmd` and
  `arch/arm/boot/compressed/.head.o.cmd`; `System.map` carries
  `luofu_switch_probed`/`luofu_switch_ready` (exported) and the switch/gmac probe symbols.
- **`dt_binding_check` RC=0** for both changed bindings (dtschema 2026.9; the optional
  `yamllint` step is skipped because the package is not installed - same limitation as every
  previous session).
- **Board DTS** (with the shared.patch node statuses and `hisilicon,port` cells) compiles with the
  patched kernel's own dtc to **11,571 B**, sha256
  `7fd33522c89ee06369538fa40f8790d200c68d92fdf2e85995c3ec1fe37a9e05`, **zero warnings** (empty
  dtc stderr); `dt-validate` against the generated processed schema produces **no diagnostic for
  the switch/PIE/GEMAC nodes** (5 pre-existing diagnostics remain on the SP804 timer and the two
  UART nodes).
- **Config:** two consecutive canonical `make kernel_oldconfig` runs are **byte-identical**
  (sha256 `d1af1b2a9411b5529ddef6d5ab1852059aca594eacb5de7f24adb0e35e87665b`, RC=0 both);
  the delivered `config-6.18` symbol set is **identical to the committed one** (patches 022/023
  add no Kconfig symbol; `CONFIG_LUOFU_GMAC`/`CONFIG_LUOFU_SWITCH` stay `y`, and the new
  `depends on LUOFU_SWITCH` resolves identically). The canonical regeneration differs from the
  hand-maintained file only in two pre-existing boot-fix artifacts
  (`# CONFIG_INITRAMFS_FORCE is not set`, `CONFIG_XZ_DEC_BCJ=y`), not caused by this series.
- **`checkpatch.pl --no-tree -f`: 0 errors / 0 warnings / 0 checks** on the Kconfig hunk, the
  1,494-line switch driver, the 23-line header, the 221-line register header, the 423-line GEMAC
  driver and both YAMLs.
- **Static write-site checks:** the only LSW register writes in the driver are the two W2 qid-map
  helpers; the only CRG writes are `clk_prepare_enable`/`reset_control_deassert` (and their
  rollbacks) inside the gated power sequence; the GEMAC writes are the measured link-up/down words
  only; the default probe path contains no write.

### What this proves and what it does not

Proven (host-only): the series applies in order on a pristine tree and in the OpenWrt pipeline;
the full kernel compiles with WERROR and no warnings; the load contract/TEXT_OFFSET are unchanged;
the bindings validate; the board DTS is warning-free; the config is idempotent and unchanged in
symbols; checkpatch is clean; every write is behind an explicit parameter and limited to the
reviewed W2 set. **No driver code in this session has executed on the board**: no LSW register has
been written, no CRG gate/reset has been driven by our kernel, no PIE ring has been armed on this
image, no netdev has appeared.

Not proven / unattended-untested:
- that powering the LSW block (C1/C2) is safe and that the block can be read in a state other than
  the measured vendor one (the W3 snapshot is the first observation);
- that the qid-map write has the intended effect (W4/W5), that the pre-write conformance gate
  passes on our boot's block state (it may refuse if the tables are not zero - by design);
- that the GEMAC/PHY link comes up on our kernel (MDIO/PHY/GEMAC probes have never run; the
  pinctrl consumer states for MDIO/GEMAC are still TODO);
- **RX delivery** (PIE completion decode [U]) and **TX egress selection** (PIE TX descriptor ->
  LSW queue/port [U]): a registered netdev with carrier is *not* yet a reachable LAN interface;
- that the DTS node-enable + module params survive the boot (the image's cmdline is
  `CONFIG_CMDLINE_FORCE`; the attended boot must pass the parameters through the loader or a
  temporary cmdline change).

### Attended boot sequence (first hardware execution; recovery door armed)

1. **W3 only:** boot with `luofu_lsw.arm=1` (PIE gate left off). Expect:
   `luofu-switch: luofu_lsw.arm=1: ATTENDED WRITE PATH ...`, powered-state line, 30
   `W3 snapshot:` lines, the two qid-table non-zero counts, then
   `W3 power/read step done; ... refusing to arm the data path`. Record the snapshot; the block is
   powered down on the failure path. This is the first own-kernel CRG write on the switch block.
2. **W4/W5:** boot with `luofu_lsw.arm=1 luofu_lsw.qidmap=1`. Expect the conformance gate to pass
   (all unpopulated words zero), then
   `W2 qid map programmed and verified: 72 table-A words ... + 128 table-B words ...` and
   `connectivity path armed ...`. If it refuses, the pre-write mismatch is the evidence and no
   write was made.
3. **Data path:** add `luofu_pie.arm=1` to the same boot. Expect PIE arm logs, then four
   `luofu-gmac: <ethN> registered ...` lines (eth0..eth3 in DT order; the bench cable is on
   **eth2**/gmac3 by the measured MAC<->PHY mapping). Then `ip link set eth2 up` and check
   `/sys/class/net/eth2/carrier`, `ethtool eth2` (PHY/link/speed) and the MAC address.
4. **DHCP test:** set the MAC from the bdinfo factory bytes (or accept the random fallback), assign
   a static address on the host side of the bench link, then `udhcpc -i eth2` (or a static
   `ip addr add`) and ping/ARP. Expected to fail at the ping step until the RX decode and the PIE
   TX egress binding are implemented; the test records where it fails (netdev up? carrier? PHY?
   TX doorbell? RX IRQ? no packets?).
5. After any step, the only recovery is the physical RESET+TFTP card; no driver adds a reset path.

### Evidence (local-only, gitignored)

`build/linux-kbuild/waves/2026-11-19-net-connect/`: `02-compile-baseline.log`,
`03-prepare-final.log`, `04-compile-final.log`, `05-dt-binding-check.log`, `06-board-dts.dtb`,
`07-dt-validate.out`, `08-config-idempotence.log` + regenerated configs/symbol lists,
`09-pristine-apply.log` + patch hashes, `10-static-checks.log`, `11-checkpatch-final.log`,
`base-shared/` + `base-kernel/` snapshots (sha256), `patches-new/` and `shared-work/`.


---

## 2026-11-19 (LSW egress session): R1/R3/R5 probe-configuration write stage (patch 025)

Host-only session (no SSH/telnet/TFTP/UART/flash; the router was not touched). It decodes the
switch egress path needed for TX frames one level further and implements the R1/R3/R5
probe-configuration write set as a third attended gate. **Nothing in this section has executed on
the board; the image has not been rebuilt from 025 and no register has been written.**

### What was added

- `luofu-switch.c` / `luofu-switch-regs.h` (patch 025): the attended W4 stage,
  `luofu_lsw.egress=1` (requires `luofu_lsw.arm=1`). It snapshots the R1/R3/R5 target words,
  refuses before the first write unless the EQM indirect interface is idle, every target word
  reads stable across two samples, and `fam+0x504` low14 equals the computed cell count
  `(queue_size / cell_size) & ~7` (= 0x2000 on this board); it then programs the nine dp/ips/eqm
  words in the vendor order, reads every one back against a value computed from the pre-write
  snapshot, restores the found state on the first mismatch (also in `remove()`), and leaves
  `luofu_switch_ready()` false until **both** this stage and the qid map are verified. R4/`fam`
  is read-only; the EQM queue tables remain excluded (contents [U]).
- The decoding added to `LSW-SPEC.md`: the R1/R3/R5 derivations (7.7), the EQM indirect
  command word (7.5), the `port_bitmap` u64 cell-order decode (6.3; the queue-allocation port
  bitmap {4..12, 32, 40..43}, which reproduces the measured qid map) and the TX-frame egress
  split (7.8): per frame the vendor writes no LSW register; the 32-byte PIE descriptor carries
  the port/vport word pair plus a per-port queue handle that is zero in this build; the
  qid-to-egress EQM tables are programmed at init and their contents are still [U].

### Gate matrix after patch 025 (all paths fail closed)

| boot parameters | LSW driver (021/022/025) | GEMAC netdevs (023) |
| --- | --- | --- |
| none (normal boot) | read-only baseline validate + refuse, no write | no netdev |
| `luofu_lsw.arm=1` | W3: power, verify powered state, snapshot, refuse; no LSW register write | no netdev |
| `luofu_lsw.arm=1 luofu_lsw.egress=1` | W3 + W4: conformance, R1/R3/R5 write + readback; then qid map absent -> restore found state, power down, refuse | no netdev |
| `luofu_lsw.arm=1 luofu_lsw.qidmap=1` | W3 + W5 qid map; then the probe config is missing -> restore, power down, refuse (new in 025) | no netdev |
| `luofu_lsw.arm=1 luofu_lsw.egress=1 luofu_lsw.qidmap=1` | W3 + W4 + W5, both verified -> `luofu_switch_ready()` true | requires `luofu_pie.arm=1` too |
| `luofu_lsw.egress=1` or `qidmap=1` without `arm=1` | explicit `-EINVAL` refusal (new in 025) | no netdev |

### Attended test sequence (recovery card armed; one variable per boot)

1. **W3 only (unchanged from the connectivity session):** boot with `luofu_lsw.arm=1`.
   Expect the ATTENDED WRITE PATH warning, the powered-state line, 30 `W3 snapshot:` lines,
   the qid-table non-zero counts, then the refusal line. No LSW register is written.
2. **W4 only:** boot with `luofu_lsw.arm=1 luofu_lsw.egress=1` (PIE and qidmap gates off).
   Expect first the W3 output, then
   `luofu_lsw.egress=1: programming the R1/R3/R5 probe configuration (W4) with pre-write
   conformance and readback verification`, then either
   `W4 probe configuration programmed and verified: dp+0x1c=0x18000 dp+0x64=0x1 eqm+0x18=0x80001cc0
   eqm+0x38=0x7f0 eqm+0x40=0x80000000 eqm+0xa4=0x1fc0 ips+0x08=0x2a ips+0x10=0x... (fam+0x504=0x2000
   read-only)` followed by the qid-map-absent refusal and the power-down, **or** a
   `fam+0x504 low14 reads ... but ... derived against the computed cell count 0x2000` refusal.
   The second case means the loader did not leave the block in the measured state: that is the
   useful evidence of the boot - stop there, record the value, do not force the gate.
   Also capture: `dmesg | grep luofu-switch`, the W3 snapshot lines, and that the console/CPU
   survived the write. Power-cycle between W4 variants.
3. **W4 + W5:** boot with `luofu_lsw.arm=1 luofu_lsw.egress=1 luofu_lsw.qidmap=1`. Expect the
   W4 success line, then `luofu_lsw.qidmap=1: ...`, the qid-map conformance pass, the
   `W2 qid map programmed and verified ...` line and
   `connectivity path armed: R1/R3/R5 probe configuration and the W2 qid maps are programmed and
   verified; the GEMAC netdev front end may register`. Only this combination reaches READY.
4. **Data path attempt:** add `luofu_pie.arm=1`. Expect the PIE arm logs and four GEMAC netdev
   registrations (eth0..eth3 in DT order; the bench cable is eth2 by the measured MAC<->PHY
   mapping). Bring the link up, then test `/sys/class/net/eth2/carrier`, `ethtool eth2`, static
   address + ARP/ping between two wired ports. **Expected open failure:** no frame forwards until
   the EQM queue tables (row E1) and the per-frame TX egress selection are solved; the test
   records where it stops (netdev up? carrier? TX doorbell? no RX?). Do not treat a registered
   netdev as proof of forwarding.
5. Rollback is the physical RESET+TFTP card; the driver also restores the found words and powers
   the block down on every refusal path, but that is not a substitute for the recovery card.

### What this proves and what it does not

Proven (host-side): the 001-025 chain (25 patches; `024` from the PIE lane, sha256 recorded at
validation time) applies to pristine 6.18.60 (25/25, 0 rejects), the full kernel compiles with
WERROR and no warnings from the changed files, `W=1` is clean for them, and patch 025 passes
checkpatch with 0 errors (one commit-message style note, as every pure-diff patch in this
series). Patch 025 is file-disjoint from 024. The pre-write conformance/readback/restore logic and the gate composition are code
and can be reviewed; the R1/R3/R5 expectations are the measured 2026-11-18 values.

Not proven: that the write set does what the vendor expects (no hardware run), that the block
survives it, that a TX frame reaches a MAC port, and what the per-frame egress selection is.
The EQM queue tables that the vendor programs at init (tables 16/32/34/35/37/38, table 19) are
still [U] and are not written by any patch; a registered netdev after the attended run must not
be read as a working link.

### Evidence (local-only, gitignored)

`build/linux-kbuild/waves/2026-11-19-lsw-egress/`: `work/` (patch sources and base snapshots),
`validation/series-tree-final` (pristine 6.18.60 + 001-025), `validation/compile-tree` (isolated
copy of the compiled OpenWrt kernel build dir + patch 025), and `validation/logs/` with the
series apply log, `40-checkpatch-025.log` and `61-compile-vmlinux-final.log`. The annotated
vendor disassembly and the decode notes are in `hi_lsw_dp.annotated.dis`, `hi_kpie.annotated.dis`
and `notes/`.
## 2026-11-19 (bdinfo-MAC-nvmem session): factory MAC exposed to the kernel netdev layer

**Status: HOST-ONLY. The board DTS now exposes the plaintext `bdinfo+0xde00` factory MAC as an
MTD nvmem cell and the GEMAC driver consumes it; the LAN/WAN interface map is implemented behind
a netdev-presence guard. NOTHING HAS BOOTED and no MTD nvmem read has run on hardware.**

### What changed

- **Board DTS** (`target-skeleton/dts/luofu-r116.dts`, a direct lane write; it is not in
  `shared.patch`): the `bdinfo` partition gains `read-only;` and a `nvmem-layout` child
  (`compatible = "fixed-layout"`) holding one `mac-base` cell `mac@de00 { reg = <0xde00 0x6>;
  #nvmem-cell-cells = <1>; }` - the plaintext factory MAC from MISC-SOC-BLOCKS.md section 1.1.
  The four live GEMAC nodes consume it with `nvmem-cells = <&bdinfo_mac N>` +
  `nvmem-cell-names = "mac-address"`: gmac1..gmac3 (LAN) use N=0, gmac4 (WAN) uses N=1 - the
  vendor userspace rule LAN = base, WAN = base+1 (rootfs-2.4.15/etc/board.d/02_network:25-29).
  A long comment in the DTS records the per-port-offset plan and the LSW port correlation
  (gmac1..4 = switch MAC ports 9..12; LSW-SPEC.md 6.1).
- **Patch 026** (`026-net-gmac-bdinfo-mac-nvmem.patch`, new): the MAC source is read before the
  netdev is allocated; `-EPROBE_DEFER` from a not-yet-registered nvmem provider now defers the
  GEMAC probe (fail-closed, no random identity invented), every other error keeps the logged
  random locally-administered fallback, and the driver installs the standard
  `ndo_set_mac_address = eth_mac_addr` / `ndo_validate_addr = eth_validate_addr` callbacks so
  the existing userspace provisioner can still apply addresses through netifd. No register
  write, no MTD write, no new write path.
- **Base-files**: new `lib/functions/luofu-netdev.sh` records the vendor LAN/WAN interface map
  (WAN = eth3, LAN = eth2 eth1 eth0) **only when the four expected GEMAC netdevs exist**, with a
  tested placeholder path that logs the missing devices and invents no netifd config; it carries
  the `TODO(netdevs)` for making the map unconditional once the netdevs stop being
  attended-gated. `etc/board.d/02_network` calls it before the unchanged factory-MAC
  provisioning (`luofu-mac.sh` still sourced, still called).
- **Host test** `tools/test-mac-nvmem.sh` (new, 96 assertions): board.d/helper/patch wiring
  statics, a synthetic `bdinfo` image read exactly like `mtd_nvmem_reg_read()` (decoys outside
  `+0xde00`, erased/all-zero fail-safe cases, kernel 48-bit vs userspace 24-bit add wrap,
  per-node offsets parsed from the DTS), cpp+dtc compile + decompiled-DTB phandle checks
  (`--kernel <tree>`), and mocked `/sys/class/net` interface-map scenarios; it also re-runs the
  pre-existing 103-assertion provisioner test.

### Why no target-local nvmem driver

- Vanilla 6.18.60 already registers an nvmem provider for **every** MTD device/partition
  (`mtd_nvmem_add()` in `drivers/mtd/mtdcore.c`: `config.reg_read = mtd_nvmem_reg_read` ->
  `mtd_read()`, `config.read_only = true`, `config.root_only = true`), parses the
  `nvmem-layout` container and resolves `#nvmem-cell-cells` arguments
  (`of_nvmem_cell_get()` in `drivers/nvmem/core.c`). The MTD partition gets its OF node from
  the partition parser (`mtdpart.c: child->dev.of_node = part->of_node`); `read-only` on the
  partition clears `MTD_WRITEABLE` (`partitions/ofpart_core.c`).
- The `mac-base` cell post-processing (`nvmem_layout_parse_mac_base()`, raw read ->
  `eth_addr_add()`) is **not** in vanilla 6.18.60: it arrives through OpenWrt's generic carried
  backports of upstream commits, `target/linux/generic/pending-6.18/
  804-nvmem-core-support-mac-base-fixed-layout-cells.patch` and
  `809-01-nvmem-core-generalize-mac-base-cells-handling.patch`. Without them the cell would
  read raw bytes and the WAN +1 offset would not be applied - a stated dependency.
- Consumer path: `of_get_mac_address()` (`net/core/of_net.c`) -> `of_get_mac_address_nvmem()`
  -> `of_nvmem_cell_get(np, "mac-address")`; `nvmem_get_mac_address()`
  (`net/ethernet/eth.c`) requires exactly 6 bytes and `is_valid_ether_addr()`. Kernel
  `eth_addr_add()` is a 48-bit add; the userspace `macaddr_add()` masks to the low 24 bits.
  The two agree for every base the provisioner accepts (it rejects `...:ff:ff:ff`, the only
  divergent case). If the nvmem provider is not registered yet, nvmem core returns
  `-EPROBE_DEFER` (`drivers/nvmem/core.c`), which patch 026 now honours.
- Read-only is therefore enforced twice (mtdcore's nvmem `read_only`/`root_only` config plus the
  partition flag) and the failure modes are fail-safe: DTB without the partition -> phandle
  unresolved -> `-ENOENT` -> GEMAC random fallback; provider late -> `-EPROBE_DEFER` -> probe
  deferred (no invented identity); erased/zero/invalid bytes -> `is_valid_ether_addr()` rejects
  -> random fallback; userspace bdinfo read failure -> the existing logged random pair.

### Validation (isolated clone `/home/shibbityshwab/openwrt-lab-macnvmem`; no device)

- **Pristine apply order:** the **final series 001-026** (024/025 landed from their lanes
  during this session) applies in numeric order to pristine `linux-6.18.60` with
  `patch -p1 -N`: **26 patches, 100 files patched, 0 failed hunks, 0 `.rej`/`.orig`,
  RC=0**. (An earlier run of 001-023+026 and one of 001-023+025+026 were also clean; 026's
  context survives 024's edits to `luofu-gmac.c`/its YAML.)
- **OpenWrt pipeline (this lane's series, 001-023+026):** `make target/linux/prepare`
  **RC=0** (1,225 `patching file` lines, patch 026 applied; the one `Kconfig.orig` is GNU
  patch's default backup for patch 021's known 6-line-offset hunk, not a reject);
  `make target/linux/compile V=s -j16` **RC=0, 0 errors, 0 compiler warnings under
  `CONFIG_WERROR=y`** (1,418 s wall, full rebuild from the pristine tarball); zImage
  **3,281,744 B**, sha256
  `8443c8c0c41a31a335188e1e547d6f82f5d33b61d945b4bd770d315eeac83f81`; the built `.config`
  has `CONFIG_WERROR=y`, `CONFIG_MTD=y`, `CONFIG_NVMEM=y`, `CONFIG_NVMEM_LAYOUTS=y`.
- **Full final series 001-026:** `make target/linux/prepare` **RC=0** (1,233 `patching file`
  lines, 0 rejected hunks, 0 `.rej`); the compile then **fails in patch 024** at
  `drivers/net/ethernet/hisilicon/luofu-pie.c:1060`: `error: 'GRO_DROP' undeclared` (that
  identifier does not exist in 6.18) - a pie-decode-lane defect, not this lane's. Everything
  else up to that point compiled cleanly, including `luofu-gmac.o` (024+026 combined) and
  `luofu-switch.o` (025); a targeted rebuild of `luofu-gmac.o` in the final prepared tree is
  RC=0. **The 001-026 image build must wait for the pie-decode lane to fix `luofu-pie.c`.**
- **Patch 026 context refresh:** after 024 landed, 026's original hunks applied with offsets and
  one fuzz-2 hunk. 026 was therefore re-diffed against the post-024 tree: applying 001-025 then
  the refreshed 026 is a **clean apply with no fuzz/offset**, and the resulting
  `luofu-gmac.c`/`hisilicon,luofu-gmac.yaml` are **byte-identical** to the content the
  001-023+026 full build compiled (`cmp`).
- **Binding:** `dt_binding_check` RC=0 for the changed
  `hisilicon,luofu-gmac.yaml` (example compiled; optional `yamllint` unavailable - same
  limitation as every previous session; one pre-existing unrelated realtek YAML warning).
- **Board DTS:** the patched kernel's own dtc compiles it with **zero warnings**
  (**11,996 B**, sha256 `e9c1649f...`; baseline before this session was 11,571 B /
  `7fd33522...`); `dt-validate` returns RC=0 with the same **5 pre-existing diagnostics**
  (two SP804 timer properties, two UART clock-names) and **no diagnostic** for the bdinfo,
  nvmem-layout, mac-base or GEMAC nodes.
- **checkpatch:** `--no-tree -f` **0 errors / 0 warnings / 0 checks** on the modified GEMAC
  driver and its YAML.
- **Host tests:** `tools/test-mac-nvmem.sh` **96/96 pass** (with the compiled-DTS checks);
  the pre-existing `tools/test-mac-provisioning.sh` still **103/103 pass**.
- **Config:** no `config-6.18` change and no new symbol; `CONFIG_NVMEM=y` /
  `CONFIG_NVMEM_LAYOUTS=y` were already present (the MTD nvmem provider and the fixed-layout
  container are stock), and the mac-base post-processing is a generic Makefile-carried
  backport, not a symbol.
- **Write-set audit:** patch 026 adds no writable nvmem provider, no `mtd_write`/`mtd_erase`
  path and no MAC register write; the nvmem read path is mainline `mtd_read()` through
  `mtd_nvmem_reg_read()`.

### What is proven / what is not

**Proven host-only:** the series applies and the full kernel compiles warning-free with the new
code; the binding and the board DTS validate; the wiring is exactly the documented one
(read-only bdinfo partition, one 6-byte mac-base cell at +0xde00, LAN offsets 0 and WAN offset
1), checked statically, on the raw DTS, on the compiled DTB and through a read-path mock; the
userspace map is guarded and backward compatible; every failure mode of the new path is either
a logged random fallback or a deferral, never an invented identity.

**Not proven / needs the board [U]:** the MTD nvmem read itself (no boot has ever reached this
code); whether the FMC/MTD partition appears with the expected `bdinfo` label and size under our
kernel; the provider-vs-GEMAC probe ordering and therefore whether the `-EPROBE_DEFER` branch is
taken; that the read bytes are the unit's factory MAC (the 0xde00 layout is from the local dump,
not from this boot path); the netdev naming order (eth0..eth3); the userspace `macaddr`
application through `ndo_set_mac_address`; and whether the guarded interface map activates on an
attended boot - `board.d` runs once per board.json, so a first boot without the gated netdevs
needs `/etc/board.json` regenerated (and the gated netdevs present) for the map to be recorded.

### Attended boot must test (recovery door armed; physical RESET+TFTP is the only recovery)

1. **Kernel nvmem read:** boot with `luofu_pie.arm=1 luofu_lsw.arm=1 luofu_lsw.qidmap=1` (the
   four GEMAC netdevs then register). Expect no `no usable MAC address in the devicetree`
   warning for eth0..eth3; if the warning appears, record its `%pe` reason - `EINVAL` means the
   cell read 6 bytes but `is_valid_ether_addr()` rejected them (erased/corrupt bdinfo or the
   mac-base backport missing), `ENOENT` means the phandle/wiring is absent, `EPROBE_DEFER`
   would have deferred probe instead. Compare `ip link` MACs with the userspace `lan_mac` /
   `wan_mac` from board.json: LAN must equal the base copy, WAN = base+1.
2. **Provider ordering:** check dmesg for the deferral message
   `MAC-address nvmem provider is not ready`; its presence (and later netdev registration)
   proves the deferred path, its absence proves the provider won the probe race.
3. **Interface map:** if the board.json from a previous boot is kept, the guarded map is absent;
   with the gated netdevs present, remove `/etc/board.json` (or force board_detect) and reboot
   to re-run board.d, then check `/etc/config/network` has the vendor `wan` device `eth3` and
   the `lan` device list `eth2 eth1 eth0`, and that the lan/wan `macaddr` values are the
   factory base / base+1.
4. **Fail-safe:** if any step misbehaves, do not "repair" factory bytes; the driver falls back
   to a logged random MAC and the userspace helper to a random pair. Record the logs; the
   bdinfo partition must never be written.

### Evidence (local-only, gitignored)

`build/linux-kbuild/waves/2026-11-19-mac-nvmem/`: `01-dt-binding-check.log`,
`02-board-dts-baseline.dtb` + `02-board-dts-new.dtb` + dtc stderr, `03-dt-validate-*.out`,
`04-pristine-apply.log`, `05-checkpatch.log`, `06-test-mac-nvmem.log`,
`07-test-mac-provisioning.log`, the patch 026 copy, the OpenWrt prepare/compile logs, and
`EVIDENCE-SHA256SUMS.txt`.
