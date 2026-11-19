# overlay lane (task-51) - persistent overlay + board_detect fix + final image rebuild

**STATUS: HOST-ONLY. The final image has NOT been flashed and NOT been booted.** The
2026-11-19 attended bring-up booted an earlier build of this image recipe (patches
`001`-`017`, UP, 1 CPU, no `rootfs_data` volume, stale board.d). The image described here
is the first one built from the **full series `001`-`023`** with the persistent-overlay and
board_detect fixes; the next attended boot is its first hardware test.

Lane: `overlay-lane`. Device contract: host-only (no router, UART, TFTP or flash tool was
touched; the WR3000 was never accessed).

## 1. Final artifact

| item | value |
| --- | --- |
| image | `bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin` |
| size / sha256 | **6,205,742 B** / `15dae772984669545f175806cff70216039684e494f6196e0a94b71530026b99` |
| kernel slice | 3,305,732 B, sha256 `6c99aa3d0c7b4e104c74fef5d14f355e483b90ff0fa91aa9fc2c4f4e10ea95ca` |
| kernel uImage | `Linux-6.18.60`, ih_size 3,293,803, load/ep `0x80608000`/`0x80608000` |
| FIP trailer | 11,865 B (base DTB 11,571 B + 64 B descriptor + 230 B overlay); gate `0xd7b7ab1e` at `0x326fde` |
| rootfs UBI | 2,883,584 B (22 PEBs), sha256 `81d049511cb1775a1b6efc9fab9182eda86ea8069f45a75580dada9ab8330028` |
| volume 0 | `squashfs` static, vol_id 0, used_ebs 20, reserved_pebs 20, flags 0 |
| volume 1 | `rootfs_data` dynamic, vol_id 1, **EMPTY** (0 image PEBs), reserved_pebs 9, flags `0x01` (autoresize) |
| reserve budget | 31 PEBs reserved (2 layout + 20 squashfs + 9 rootfs_data) of the 185-PEB slot; image 2,883,584 B <= 24,313,856 B |
| container marker | `STATUS: BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED` (full text below) |
| build tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img`, `openwrt main` `27663a0`, `linux-6.18.60`, OpenWrt GCC 14.4.0 |

Patch series **23/23 applied cleanly**: `make target/linux/prepare V=s` RC=0, 1,223
patching-file lines, 0 failed hunks, no `.rej` (the only `.orig` is the pre-existing
patch-`008` offset backup). Every series patch is named in the prepare log; the applied list
is saved as `applied-patches-001-023.txt` in the local evidence:
`001` machine support, `002`/textofs `003`, `004` CRG, `005` watchdog, `006` FMC,
`007`-`009` MDIO/PHY/GMAC, `010` restart, `011` pinctrl, `012` thermal, `013` PCIe,
`014` reset polarity, `015` PIE, `016` LSW, `017` selector, `018` SMP, `019` clock tree,
`020` PIE datapath, `021` LSW W1, `022` LSW gated qid-map write path, `023` GEMAC netdev
readiness.
Build: `make defconfig` RC=0, `make world -j16` **RC=0**, 0 `error` and 0 `warning:`
lines. Boot fixes preserved: `CONFIG_CMDLINE_FORCE=y` + the built-in `CONFIG_CMDLINE`,
`CONFIG_XZ_DEC_ARM=y`, `--pad-align 4096` in `image/luofu.mk`. `_text = stext =
0xc0608000`; the DTS carries `enable-method = "hisilicon,luofu-smp"` and enables 12 nodes
(the net-lane LSW/PIE/GEMAC set); all `CONFIG_LUOFU_*`, `CONFIG_UBIFS_FS`,
`CONFIG_OVERLAY_FS`, `CONFIG_MTD_UBI_BLOCK` and `CONFIG_SMP` are `y`.
`config-6.18` is unchanged against the repository
(`2982d66330ac5c9114340a6afa34da70964f4b92c474be874c7f3157fcebc2d2`).

## 2. What changed in the repository (target-skeleton)

| file | change |
| --- | --- |
| `target/linux/hisilicon/image/luofu-ubinize.cfg` | second UBI volume `rootfs_data` (dynamic, vol_id 1, `vol_size=1MiB`, `vol_flags=autoresize`, no image = empty; deliberately NOT pre-formatted, with the reason documented in the file) |
| `target/linux/hisilicon/image/luofu.mk` | rootfs-slot/UBI description updated; `LUOFU_ROOTFS_DATA_VOLUME_NAME`; a fail-closed **base-files guard** in `Build/luofu-rootfs-ubi` (every `. /lib/...` source in the installed `/etc/board.d/*` must exist in the image) |
| `tools/luofu-image.py` | v0.2-overlay: new two-volume cfg parser and UBI validator (volume-table flags/reserved PEBs, empty-`rootfs_data` enforcement, cfg-`vol_size`-vs-`reserved_pebs` check, reserve-vs-slot budget), updated prints/docstring, and the honest `INSTALL-STATUS.txt` marker below. Negative paths fail closed (old single-volume container/cfg, pre-filled `rootfs_data`, non-autoresize overlay, dynamic squashfs). |

Base-files needed no repository change: the corrected `02_network` (no
`/lib/functions/board.sh`; sources `uci-defaults.sh`, `system.sh`, `luofu-mac.sh`) and
`luofu-mac.sh` were already in the tree from the 2026-11-18 MAC session; they are now
actually installed in the rebuilt image (the booted image had the stale board.d).

### The container marker (final text)

The generated `INSTALL-STATUS.txt` header is:

```
hisilicon/luofu image container - STATUS: BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED
====================================================================================
board:  cudy_wr3000-v2
target: hisilicon/luofu
built:  by luofu-image.py 0.2-overlay (host-only)

BOOT EVIDENCE (2026-11-19 attended bring-up, this board)
  An earlier build of this image recipe booted to userspace on a WR3000 v2 ...
    - it was built from kernel patches 001-017 only (UP, 1 CPU); the SMP patch 018
      and patches 019-023 were not part of that build and have never run on hardware;
    - it had no rootfs_data volume: mount_root fell back to a tmpfs overlay ...
    - its board.d/02_network sourced a missing /lib/functions/board.sh ...
  THIS EXACT REBUILD (patches 001-023, rootfs_data overlay volume, corrected board.d)
  has NOT been flashed and NOT been booted.
...
NOT FLASHABLE-UNTIL-INSTALLED: do not flash this file as a stock sysupgrade ...
```

The literal substring `NOT FLASHABLE` is kept on purpose: `tools/first-install-stock.sh`
(line ~505) and the target `platform_check_image` (base-files `lib/upgrade/platform.sh`,
line ~316) grep for it, so both keep warning/working without edits. The parent tool's
warning sentence still says "(it has never booted)", which is now imprecise - a one-line
follow-up for the lead when that file is touched. The full text is in the evidence bundle
(`04-validation-final/install-status.txt`).

## 3. The persistent-overlay design and why mount_root will find it

Shape (standard OpenWrt squashfs+UBI, exactly what `scripts/ubinize-image.sh` emits):

- volume 0 `squashfs` - static, the read-only rootfs (unchanged);
- volume 1 `rootfs_data` - dynamic, `vol_size=1MiB`, `vol_flags=autoresize`, **empty** in
  the image.

Boot chain (source-verified in the tree's own kernel/fstools; **not yet observed on this
board**, [U]):

1. `ubi.mtd=rootfsb` attaches the rootfs slot; UBI's attach path finds the volume marked
   autoresize and grows `rootfs_data` to all free PEBs of the slot
   (`drivers/mtd/ubi/build.c:autoresize()`; flag read/validated in
   `drivers/mtd/ubi/vtbl.c`, one autoresize volume only).
2. fstools `mount_root` looks for a volume named `rootfs_data`; the UBI driver wins over
   the mtd/partition drivers by priority, matches the sysfs volume name and identifies it as
   UBIFS (`mount_root.c`, `libfstools/ubi.c`).
3. `mount_overlay()` mounts it read-write (`libfstools/overlay.c`). Because the volume is
   empty, UBIFS itself creates the default filesystem on that first read-write mount
   (`fs/ubifs/sb.c` `ubifs_read_superblock()` -> `create_default_filesystem()`;
   `fs/ubifs/super.c` refuses this only for a read-only mount). The overlay then becomes
   the persistent `/overlay` via overlayfs; `/etc/init.d/done` marks it ready.
4. The following boots mount the now-formatted UBIFS normally; the volume stays grown to the
   slot's free space.

Consequences: the overlay is per-slot (no shared JFFS2 partition to keep in sync), no
`mkfs.ubifs` step is needed in the image build, and the vendor mtd15 `rootfs_data` JFFS2
partition is left untouched (the UBI driver takes precedence when the in-slot volume exists).
The 2026-11-19 booted image had no `rootfs_data` volume, which is why mount_root printed
"no usable overlay filesystem found, using tmpfs overlay".

## 4. board_detect resolution (no `/lib/functions/board.sh` exists)

[V] The boot warning `/bin/board_detect: .: line 34: can't open /lib/functions/board.sh` came
from the stale target file `base-files/etc/board.d/02_network`, which sourced a file that
**does not exist anywhere in OpenWrt main**: upstream provides `board_name()` in
`/lib/functions.sh` (fed by `/tmp/sysinfo/board_name`, written by
`/lib/preinit/02_sysinfo`), and `/bin/board_detect` merely sources `/etc/board.d/*` in a
subshell. No file was added upstream or in the target; the corrected board.d (already in the
repository) sources only `/lib/functions/uci-defaults.sh`, `/lib/functions/system.sh` and
`/lib/functions/luofu-mac.sh`. The rebuilt image contains that corrected file
(byte-identical to the repository, sha256 `be1e4ca6...`) plus
`/lib/functions/luofu-mac.sh`; the MAC provisioning helper's test still passes 103/103.
New guard: the image recipe now fails the build if *any* installed `/etc/board.d/*` sources a
`/lib/...` helper that is missing from the image, so this failure class cannot reach a boot
log again. `sh -n` is clean on `02_network`, `luofu-mac.sh`, `platform.sh` and
`luofu_record_pair`.

## 5. Validation actually run (host-side; evidence in `04-validation-final/`)

- `scripts/luofu-image.py check-container` on the final image bytes: **PASS** (kernel slice +
  manifest cross-check, two-volume UBI, reserve budget, marker).
- Negative tests: old single-volume container, single-volume cfg, pre-filled
  `rootfs_data`, non-autoresize overlay, dynamic squashfs - **all refused** (RC 1/3); no
  positive path skipped.
- Installed rootfs: corrected `/etc/board.d/02_network` (no board.sh reference), MAC helper,
  `luofu_record_pair` + `S96` symlink, `platform.sh`; `sh -n` clean.
- `test-platform-abinstall.sh --container <final image>`: **100/100** (the real
  `platform_check_image` accepts the new container; the A/B write path is unchanged).
- `tests/first-install.test.sh --real-container <final image>`: **144/144** (the parent
  first-install tooling accepts the container; its device-side rootfs verification is
  unaffected - the empty `rootfs_data` volume has no PEBs, so `tools/ubi-volume-check.py`
  compares the same squashfs payload on both sides).
- `test-mac-provisioning.sh`: **103/103** (MAC helper intact; it sources
  `uci-defaults/system` and is not touched by the board_detect fix).
- `config-6.18` unchanged vs the repository; kernel `.config` carries the boot fixes, SMP and
  all `CONFIG_LUOFU_*`; 23/23 patches named in the prepare log with no rejects.
- NOT run: anything on hardware. The next attended boot is the first test of the overlay,
  SMP/CPU1 and the full `001`-`023` series as a set.

## 6. Risks / follow-ups (honest list)

- **The overlay path has never run on this board.** The empty-volume/autoresize/UBIFS
  autocreate behaviour is read from the kernel and fstools sources; the next boot must show
  the formatted overlay mount and persistence across a reboot. If it does not, the fallback
  is the same tmpfs overlay as before (no brick risk from this change).
- **SMP and the new driver series are untested as a set.** The booted image was `001`-`017`;
  patches `018`-`023` (SMP, clock tree, PIE, LSW, GEMAC) have never executed on hardware.
  The newly enabled LSW/PIE/GEMAC nodes should fail closed on a default boot (no
  `luofu_lsw.arm=1` / `luofu_lsw.qidmap=1` / `luofu_pie.arm=1` in `CONFIG_CMDLINE`), so
  the expected delta is refusal log lines - this expectation itself is [U] until booted.
- **Overlay vs A/B switch:** the overlay is per-slot; booting the other slot starts an empty
  overlay. A plain selector switch is not a configuration migration. Documented in
  TARGET-DESIGN.md §5.4 via `shared.patch`.
- **`platform_check_image` still validates only volume 0** of the rootfs UBI (the PEB-count
  invariant is unchanged, so the new image passes). A check for the `rootfs_data` record
  would be stronger; left as a follow-up because it touches the device-side upgrade script
  and its mocked test outside this lane's scope.
- **Parent tool warning text**: `tools/first-install-stock.sh:505` still prints "(it has
  never booted)" when it sees the marker; reword when that file is next touched (marker
  itself is compatible by design).
- **Doc inconsistency (pre-existing):** TARGET-DESIGN.md §3.3 says the rootfs partitions are
  `0x1740000` (= 24,379,392 B) but the text/budget use 24,313,856 B (0x1730000). This lane
  keeps the existing conservative budget constant; not changed here.
- **The image is not byte-reproducible** across kernel rebuilds (the UTS banner/build
  timestamp is embedded in the zImage - pre-existing caveat).
- **A reflinked copy of the build clone does not build as-is** (copied `staging_dir` keeps
  absolute paths to the source clone; the `apk` host build then fails on a shadowed
  `<endian.h>`). This lane built in the clone in place; the same copy artefact is already
  recorded in the 2026-11-18 image-recipe session.

## 7. Documentation patch

`shared.patch` applies to the `opensource/` root (git-apply-checked against the current
HEAD) and updates:

- `docs/TARGET-DESIGN.md` - §3.3 (vendor rootfs_data note), §5.1 rootfs-UBI row, §5.3
  (overlay design replaced: per-slot empty dynamic UBI volume, vendor JFFS2 partition left
  untouched, boot-chain citations, [V]/[U] grades), §5.4 step 6 (per-slot overlay upgrade
  semantics), §5.5 first-install note (the written rootfs slot now carries the empty overlay
  volume), open question 6 (resolved/attended-open).
- `docs/SKELETON-SMOKE.md` - the 2026-11-19 attended-boot section gets the follow-up note,
  and a new **"2026-11-19 (afternoon, host-only)"** session records the overlay/board_detect
  changes, the full-series build, the image hashes, the validation list, the `001`-`017` vs
  `001`-`023` distinction and the limits above.
- `target-skeleton/README.md` - top status block (boot milestone + afternoon rebuild), image
  recipe row, tool row, fact-table UBI/overlay rows, §3.3 slot/overlay text, DTS row (enabled
  nodes vs fail-closed drivers) and a new validated-build bullet with the final hashes.

Apply with `git -C opensource apply ../../target-skeleton/docs/incoming/overlay/shared.patch`
from the repository root (or `git apply` inside `opensource/`), then commit the submodule.

## 8. Local evidence (gitignored)

`build/linux-kbuild/waves/2026-11-19-overlay-image/`:

- `00-baseline/` - previous clean image `e893c388...` (6,185,262 B, patches `001`-`017`,
  UP, no overlay volume), its kernel/rootfs UBI, and the 001-017 target-tree tar;
- `01-build-intermediate-001-021/`, `02-validation-intermediate-001-021/` - the
  intermediate 001-021 build (image `97ce5eaf...`, 6,195,502 B), kept for comparison;
- `03-build-final-001-023/` - final image, kernel slice, rootfs UBI, trailer, manifest,
  prepare log with the 23 named patches, world log, kernel/OpenWrt configs, bin sha256sums;
- `04-validation-final/` - check-container, container members, install-status, kernel + UBI
  table dump, installed base-files, negative tests, host-test logs, source citations;
- `SHA256SUMS` over everything.

**Nothing in this lane was flashed, booted, or written to the device.**
