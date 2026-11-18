# Target skeleton (schematic, untested target) - index

**Status 2026-11-18: SCHEMATIC, NOT A WORKING TARGET - but its structure was accepted by
OpenWrt main's target metadata and kernel prepare, the target compiles a 6.18 kernel with the
correct load address, and an own-artifact FIP kernel slice has been assembled and validated
structurally (host-only; never booted). See [SKELETON-SMOKE.md](SKELETON-SMOKE.md).**
The concrete files live in [`../target-skeleton/`](../target-skeleton/README.md); this page is
only an index. The design source of truth remains [TARGET-DESIGN.md](TARGET-DESIGN.md) sections
4-6; if this page and TARGET-DESIGN.md disagree, TARGET-DESIGN.md wins.

**What it is.** The first-pass target files from the design, now in the layout that a real
`openwrt main` tree accepted: `target/linux/hisilicon` (target `Makefile`/`Kconfig`, the
`luofu` subtarget with `target.mk`/`config-5.10`/`config-6.18` placeholder, a board
`image/Makefile` plus `image/luofu.mk` device stanza, board-level `base-files`), a draft
upstreamable board DTS under the target `dts/` directory, and the fail-closed FIP-slice tool.

**What it is not.** Not flashed, not booted, not accepted upstream. The host sessions proved that
OpenWrt main's `make defconfig`/`prepare` accept the target, that the kernel patch series compiles
a 6.18 kernel with the real `TEXT_OFFSET`, and that the image recipe builds end-to-end: an
isolated `make world` produced a reproducible squashfs sysupgrade image with our own FIP kernel
slice, rootfs UBI and manifest (all structurally re-validated from the finished image; never
flashed or booted). The
**rest** of the driver/binding series (switch, PCIe, GEMAC data path), rootfs/UBI, sysupgrade
`platform.sh` metadata and the install path do not exist, and a novel trailer still needs one
attended boot. The drivers written so far exist as host-compiled
drafts only: CRG clock/reset (patch `004`) and watchdog (patch `005`) with their YAML
bindings (2026-11-18, fifth session), plus the network trio - MDIO bus controller (`007`),
Genius PHY (`008`) and a **partial** GEMAC binding/resource driver (`009`, no data path;
its DTS nodes stay disabled) - added in the sixth session the same day - and the FMC
SPI-NAND controller (`006`, a SPI-MEM controller with the mainline SPI-NAND stack behind
it; it consumes the CRG `sfc_clk` gate and `sfc_rst` reset and adds `CONFIG_SPI` /
`CONFIG_MTD_SPI_NAND`), the machine-restart driver (`010`: SYSCTRL boot-selector save, a
PROVISIONAL CRG software-reset command and a CRG watchdog fallback), pinctrl (`011`), thermal
(`012`), the DWC PCIe host controller (`013`, RC nodes disabled - the Wi-Fi endpoint is `[X]`)
the PIE packet-engine draft (`015`; resources + ring scaffolding, refuses to bring up the
unverified data path), the internal LSW switch D1a skeleton (`016`) and the A/B
boot-selector interface (`017`: `current_pair`/`next_pair` sysfs over the shared
SYSCTRL cell; probe performs no register access; `next_pair` is the loader-style low-byte
read-modify-write), with the CRG reset polarity corrected by `014`. The target image also
gains the boot-time pair recorder (patch `017` plus
`base-files/etc/init.d/luofu_record_pair`, corrected 2026-11-18 against
[`SELECTOR-0x21.md`](SELECTOR-0x21.md): the env `bootflag` selects the pair, the SYSCTRL
cell is the loader's record, and userspace must re-record the running pair after every boot).
None of them has run on hardware.

**What the smoke test did (2026-11-18).** OpenWrt `main` commit `27663a0`:
`make defconfig` PASS and generated `CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y`;
`make target/linux/prepare` RC=0 (kernel 6.18.60 fetched/unpacked). No feeds, toolchain,
compile or image build was attempted. Details and the exact layout adaptations:
[SKELETON-SMOKE.md](SKELETON-SMOKE.md).

**FIP trailer format (decoded AND generation implemented 2026-11-18).** The 40,732-byte trailer
is decoded from the local U-Boot binary and artifacts: magic `0xd7b7ab1e`, container size,
hard-checked `32` constants, an entry count and 32-byte entries holding overlay totalsize/offset,
an 11-row vendor/board match table, and the overlay applier chain in U-Boot. The vendor trailer
is 31,220 B base DTB + 96 B descriptor + 4,592 B and 4,920 B overlays = 40,828 B; the lab
artifact is the same bytes with the last 96 bytes of the final overlay's zero padding cut.
`tools/fip-slice.py make-trailer` takes user-supplied base DTB + overlays, fails closed on every
decoded loader check, and reproduces the vendor 2.4.15 and 2.5.24 containers byte-for-byte; the
tool never embeds vendor bytes. A novel generated trailer still needs one attended boot before
the image recipe relies on it. Full byte tables, checks and remaining unknowns:
[`../target-skeleton/docs/TRAILER-FORMAT.md`](../target-skeleton/docs/TRAILER-FORMAT.md).

**The facts it encodes (all from TARGET-DESIGN.md, which cites the bench ledger):**

- kernel slot = legacy uImage + FIP trailer (fixed 40,732 B accepted; structurally valid other
  lengths possible, see the trailer doc), `<= 8,650,752` B;
- gate magic `0xd7b7ab1e` at `0x40 + ih_size + base-DTB totalsize`;
- rootfs slot `<= 24,313,856` B, squashfs in a static UBI volume `squashfs`;
- `rootfs_data` stays **JFFS2** for the first submission;
- A/B sysupgrade writes only the inactive pair, then moves the SYSCTRL record; the bootloader's
  env mismatch path selects the new pair on the next boot and the target image's boot-time
  recorder re-records the running pair (see [`SELECTOR-0x21.md`](SELECTOR-0x21.md)); container
  metadata schema is proposed, not frozen;
- uImage load/entry must be derived from the loader staging base and the kernel's
  `TEXT_OFFSET`, not copied blind;
- stock partition map (17 partitions), no lab `lab` partition.

**What is still [U]/TODO** (full list in TARGET-DESIGN.md section 12 and the skeleton README):
one attended boot of a generated trailer; the sysupgrade container/install path
(host-side A/B write path implemented in `platform.sh` + patch `017`, never run on
hardware; the stock-to-image first-install path is designed and tooled host-side in
[FIRST-INSTALL.md](FIRST-INSTALL.md), also never executed) and the selector protocol (recorded A/B values measured; the env re-stamp, the cell's
upper 24 bits and one attended A/B validation still owed; the boot-time recorder now exists
host-side but has never run); a bench-verified reset path (the
patch-`010` CRG command pair is PROVISIONAL); MAC/efuse source; switch DSA-vs-switchdev; the
GEMAC TX/RX ring/DMA data path (the driver is a resource/binding half only); SMP; Wi-Fi scope;
board compatible vs OpenWrt device name.

**Publication.** Part of the `opensource/` half whose publication is **NOT READY** (history
purge pending, [PUBLISH-READINESS.md](PUBLISH-READINESS.md)). The skeleton contains no vendor
binaries, blobs, dumps or credentials.

**Reading order:** [TARGET-DESIGN.md](TARGET-DESIGN.md) -> [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md)
-> [SKELETON-SMOKE.md](SKELETON-SMOKE.md) & the trailer doc ->
[FIRST-INSTALL.md](FIRST-INSTALL.md) (stock-to-image first-install design) ->
[`../target-skeleton/README.md`](../target-skeleton/README.md).
