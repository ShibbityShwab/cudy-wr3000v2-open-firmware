# Image recipe - host build session summary (2026-11-18)

**Status: the whole image chain is implemented and was built by a real
`make world` in an isolated OpenWrt main clone; every artifact validates from
its own bytes. It has NEVER BOOTED, and it is NOT installable: the A/B boot
selector protocol and the install path are [U] and `platform_do_upgrade`
refuses to write. Nothing here is a boot, flash or install claim.**

> UPDATE 2026-11-18 (abinstall session): the refusing `platform_do_upgrade`
> recorded below has since been replaced by a host-only A/B install path
> (patch `017` + `base-files/lib/upgrade/platform.sh` +
> `tools/test-platform-abinstall.sh`); see ../abinstall/SUMMARY.md. The image
> and its build evidence below are unchanged; nothing has been flashed.

This directory is the hand-off of the image-recipe session for task-25:
`shared.patch` (apply from the `opensource/` root) contains the README,
SKELETON-SMOKE and TARGET-DESIGN updates; this file records the commands,
artifacts and hashes. Directly edited repo files (not in this incoming dir):

- `target/linux/hisilicon/image/Makefile`, `image/luofu.mk` (rewritten),
  `image/luofu-ubinize.cfg` (new) - the real image chain.
- `target/linux/hisilicon/base-files/lib/upgrade/platform.sh` - new; real
  structural `platform_check_image`, fail-closed `platform_do_upgrade`.
- `target/linux/hisilicon/Makefile` - added `KERNELNAME:=zImage` (without it
  `Kernel/CopyImage` never places a zImage in $(KDIR) and the image chain has
  no kernel input).
- `tools/luofu-image.py` (new; target mapping `scripts/luofu-image.py`),
  `tools/fip-slice.py` (one stale manifest comment updated).
- `dts/luofu-r116-overlay.dts` is already in the repo; the target mapping must
  place it as `target/linux/hisilicon/dts/luofu-r116-overlay.dtso` (the
  `DEVICE_DTS_OVERLAY` rule only compiles `.dtso` sources) together with
  `dts/luofu-r116.dts` as `target/linux/hisilicon/dts/luofu-r116.dts`.

## Environment

- OpenWrt `main`, commit `27663a0`, `linux-6.18.60`, GCC 14.4.0 toolchain
  (already built in the source clone).
- Isolated clone: `cp -a --reflink=auto /home/shibbityshwab/openwrt-lab-verify
  /home/shibbityshwab/openwrt-lab-image` (8.7 GB, btrfs). The shared
  `/home/shibbityshwab/openwrt-lab` was not modified.
- Feeds were **not** installed and not updated (no network feed fetch): the
  rootfs contains the core `package/` set only (73 packages configured), which
  is enough to build a real rootfs but is not a release package set.
- No device, UART, TFTP, flash or bench tool was used.

## What was run

1. Installed the skeleton into the clone:
   `target/linux/hisilicon/` from `target-skeleton/target/linux/hisilicon/`;
   `dts/luofu-r116.dts` -> `target/linux/hisilicon/dts/luofu-r116.dts`;
   `dts/luofu-r116-overlay.dts` -> `target/linux/hisilicon/dts/luofu-r116-overlay.dtso`;
   `tools/fip-slice.py`, `tools/luofu-image.py` -> `scripts/` (+x).
2. `make defconfig` -> RC=0 (`CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y`).
3. `make world -j16` -> **RC=0** (kernel compiled, packages built/installed,
   target install/image stage ran). The shell command was
   `make world -j16 > build-world5.log 2>&1`.
4. Rebuilt once more with no changes: **byte-identical image** (reproducible).

Pre-existing clone-copy issue (not caused by the recipe): the copied
`staging_dir` contained 553 files with absolute paths to the source clone;
the 131 text ones (pkg-config `.pc`, autoconf/libtool wrappers, `*-config`)
were rewritten to the new clone path before the first successful world run.
Without that, the `apk` host build failed on a shadowed `<endian.h>` from the
duplicate staging include (an artifact of the copy, not of this target).

Patch series note: the clone was built with `patches-6.18/001-015`, the state
stated for this task. Patch `016-net-lsw-luofu.patch` and its
`CONFIG_LUOFU_SWITCH=y` `config-6.18` line were completed by the parallel
task-26 lane **after** this build started (an earlier snapshot of 016 without
the config line stopped non-interactive `syncconfig` at a prompt, which is why
it is not part of the artifacts here). Re-run `make world -j16` after
integration; the kernel/slice/UBI/image hashes in this file will change.

Note on shared docs: `README.md`, `docs/SKELETON-SMOKE.md` and
`docs/TARGET-DESIGN.md` already carried uncommitted edits from the completed
task-26 lane when `shared.patch` was generated (2026-11-18 15:07). The patch is
a diff against that on-disk state and was verified with `git apply --check`;
re-check before applying if those files change again.

## Artifacts (all from the final RC=0 build)

| artifact | size (B) | sha256 |
| --- | --- | --- |
| `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin` | 6175022 | `ea143b03d9d248b3e2b21723ef639e42817e66da847285496ebbc46e12756881` |
| `build_dir/.../linux-hisilicon_luofu/cudy_wr3000-v2-kernel.bin` (FIP slice) | 3283008 | `23c3286b631a20d1a8fa0d7671d0bea1d7dfbd7f8fdcf7c85a084fba3c5ca636` |
| `build_dir/.../cudy_wr3000-v2-rootfs.ubi` (rootfs UBI) | 2883584 | `d3c5611cf49395a7a99c8703331b6545a5b543ae9781041e2571e80a9faff2e7` |
| `build_dir/.../cudy_wr3000-v2-fip-manifest.txt` | 945 | `bd976870c2f7c0331eb57ba42930c26eda4d34038004e33d433c1d98e1b77422` |
| `build_dir/.../luofu-fip-trailer.bin` | 10487 | `d9616d26a05ab29732a2da99f7b5d3627330aa1bd196b8443b012a9915de1df5` |
| `build_dir/.../image-luofu-r116.dtb` | 10193 | `4e4a03043e7c01448c5a559bc9fb055df0cd7f5d71703a4a0905a4600b8e37a0` |
| `build_dir/.../image-luofu-r116-overlay.dtbo` | 230 | `8e4ab83b575dc14d9f3131dc01d1cbdccc63b38f8f8c4fa774da0d1f200b23b6` |
| `build_dir/.../root.squashfs` | 2481002 | `c079a1d6c4a78ed0e93603b77bf388ff00df432a351f69693c7a9232483f53a4` |
| `linux-6.18.60/arch/arm/boot/zImage` | 3262264 | `b3227ac3d0ab5265c73d1d25c773767d6c72fc974d85209521620af41664c060` |

Derived values (recomputed from the bytes):

- kernel slice: 3,283,008 B of the 8,650,752 B slot (5,367,744 B
  headroom); uImage `Linux-6.18.60`, `ih_size` 3,272,457 B (= zImage + appended
  DTB), load/entry `0x80608000`; FIP trailer 10,487 B, base DTB
  10,193 B, descriptor 64 B (1 overlay), container total 294 B,
  overlay 230 B; gate magic `0xd7b7ab1e` at `0x32171a`.
- rootfs UBI: 2,883,584 B of the 24,313,856 B slot (21,430,272 B
  headroom); 22 PEBs = 2 layout + 20 data LEBs (LEB 126,976 B),
  static volume named `squashfs`, all EC/VID/data CRCs verified by
  `tools/luofu-image.py`.
- sysupgrade container: 6,175,022 B, members
  `sysupgrade-cudy_wr3000-v2/{CONTROL,INSTALL-STATUS.txt,fip-manifest.txt,kernel,root}`
  plus the standard appended fwtool metadata
  (`supported_devices` = `cudy,wr3000-v2`, `hisilicon,luofu-r116`,
  `hisilicon,luofu`; `board` cudy_wr3000-v2; revision r0-27663a0).
- Reproducibility: two consecutive `make world -j16` runs with no changes
  produced the same image sha256 (`ea143b03d9d248b3...`).

## Validations actually run

- `tools/luofu-image.py check-container` on the final `.bin`: **PASS** -
  independent re-read of CONTROL, the kernel slice (uImage identity +
  load/entry + FIP trailer/gate/descriptor), the UBI volume shape and the
  manifest-vs-bytes cross-check.
- `base-files/lib/upgrade/platform.sh` `platform_check_image`, sourced from the
  real tree and run on the real image: **PASS** (exit code `0`), plus
  corrupted-image, wrong-board and foreign-image variants all refused
  (RC=1, RC=1/74); `platform_do_upgrade` returns 1 with the [U] refusal text.
- Negative tests on the tools: fabricated uImage load/entry, slot-budget
  overflow, non-squashfs rootfs, tampered/unknown-key ubinize cfg, corrupted
  UBI data CRC, manifest/sha mismatch - all refused with the expected exit
  codes (3 = refusal, 1 = validation failure).
- `sh -n`/`bash -n` on `platform.sh`; `image/luofu.mk` parsed by the real
  OpenWrt make (the build executed every stage).

## What is proven

- The image recipe is **real OpenWrt idiom**: the stock image machinery
  (`make -C target/linux/hisilicon/image compile install`, target `install`)
  builds it in a normal `make world`; the device stanza, DTS delivery,
  `DEVICE_DTS_OVERLAY`, `append-dtb`, `uImage`, the custom Build stages,
  `append-metadata` and the bin/install flow all work.
- A complete host-built sysupgrade container exists, produced from our own
  kernel + our own generated trailer + our own rootfs; every field the data
  path will later rely on is validated from the bytes at build time and again
  independently from the finished image.
- The load/entry derivation `(0x83200000 & 0xf8000000) + 0x00608000 =
  0x80608000` is asserted against the built kernel's `_text` and against the
  assembled uImage header.
- The build is reproducible byte-for-byte.
- The rootfs UBI is the measured shape (single static `squashfs` volume,
  p=131072 m=2048 s=2048 O=2048).

## What is NOT proven / remaining blockers

1. **No boot of anything.** The stock loader's acceptance of a *novel
   generated* trailer, the kernel's boot, the rootfs mount and the
   `rootfs_data` overlay are all unproven; one attended boot is required
   (TARGET-DESIGN.md 5.2, 9).
2. **A/B selector protocol [U]** and therefore no `platform_do_upgrade`
   implementation; it refuses to write. `sysenv 0x10100C08`, both env-block
   copies and the Linux read path remain unmeasured (TARGET-DESIGN.md 3.4).
3. **Install/factory path is an open question** (TARGET-DESIGN.md 5.5); the
   factory/install stanza is deliberately absent.
4. **Patch 016 not covered**: the parallel task-26 lane added the LSW patch and
   its `CONFIG_LUOFU_SWITCH=y` config line after this build; these artifacts
   describe `001-015` only. The image recipe is independent of it, but the
   hashes here are for the `001-015` kernel.
5. **Package/rootfs scope**: feeds were not installed, so this is the core
   package set, not a release rootfs; `DEFAULT_PACKAGES += ubifs` /
   `kmod-mtd-rw` from the target sketches do not exist without feeds and were
   not built.
6. **Boot arguments**: how the rootfs selector (`root=`/UBI) reaches the
   kernel, and whether the stock loader overrides `/chosen/bootargs`, is
   still [U] (TARGET-DESIGN.md 5.6) - the container/UBI design assumes this can
   be carried, unproven.
7. The sysupgrade manifest schema is not frozen; the A/B container layout is a
   documented artifact, not a compatibility promise.
8. No Wi-Fi, no switch, no GEMAC data path (unrelated to the image chain but
   means the result cannot be a usable router image).

## Evidence

Local-only evidence lives in the isolated clone
`/home/shibbityshwab/openwrt-lab-image` (`build-world5.log`,
`build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/`, `bin/`),
which is disposable and gitignored by design. The durable record is this
summary plus the SKELETON-SMOKE section in `shared.patch`.
