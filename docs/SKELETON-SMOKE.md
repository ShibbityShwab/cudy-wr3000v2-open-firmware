# Target-skeleton smoke test (2026-11-18)

**Status: structure accepted by OpenWrt main; the kernel patch series (including the
2026-11-18 early-boot `TEXT_OFFSET` fix, patch `003`) applies; the config re-derives
with the tree's own OpenWrt GCC 14.4 toolchain; and `make target/linux/compile` now
completes RC=0, producing `arch/arm/boot/zImage` with the correct `0x80608000`
load/entry address. A first FIP kernel slice was then assembled from our own DTB +
overlay + uImage and structurally validated (fourth session below); nothing has been
booted: no rootfs, no sysupgrade container, no install path, no boot, no driver
series.**

This page records the host-side validation of [`target-skeleton/`](../target-skeleton/README.md)
against a real OpenWrt tree (four sessions on 2026-11-18: the target-layout smoke test
below, the kernel patch/config session, the `TEXT_OFFSET` + full-kernel-compile
session, and the first own-artifact FIP kernel slice appended at the end). It is not a
target build result and must not be cited as one: the only build products it records
are the kernel compile product and a structurally validated kernel slice - no rootfs,
no sysupgrade container, no install image and no boot.

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

