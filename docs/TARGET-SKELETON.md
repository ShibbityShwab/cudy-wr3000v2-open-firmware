# Target skeleton (schematic, untested target) - index

**Status 2026-11-18: SCHEMATIC, NOT A WORKING TARGET - but its structure was accepted by
OpenWrt main's target metadata and kernel prepare (see [SKELETON-SMOKE.md](SKELETON-SMOKE.md)).**
The concrete files live in [`../target-skeleton/`](../target-skeleton/README.md); this page is
only an index. The design source of truth remains [TARGET-DESIGN.md](TARGET-DESIGN.md) sections
4-6; if this page and TARGET-DESIGN.md disagree, TARGET-DESIGN.md wins.

**What it is.** The first-pass target files from the design, now in the layout that a real
`openwrt main` tree accepted: `target/linux/hisilicon` (target `Makefile`/`Kconfig`, the
`luofu` subtarget with `target.mk`/`config-5.10`/`config-6.18` placeholder, a board
`image/Makefile` plus `image/luofu.mk` device stanza, board-level `base-files`), a draft
upstreamable board DTS under the target `dts/` directory, and the fail-closed FIP-slice tool.

**What it is not.** Not compiled, not imaged, not booted, not accepted upstream. The smoke test
proved only that OpenWrt main's `make defconfig` accepts the target/device and that
`make target/linux/prepare` returns 0 with this target present. The kernel platform patch,
driver/binding series, image-chain wiring, `platform.sh`/sysupgrade metadata and install path
do not exist.

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
- A/B sysupgrade writes only the inactive pair and switches the measured selector/environment
  mechanism; container metadata schema is proposed, not frozen;
- uImage load/entry must be derived from the loader staging base and the kernel's
  `TEXT_OFFSET`, not copied blind;
- stock partition map (17 partitions), no lab `lab` partition.

**What is still [U]/TODO** (full list in TARGET-DESIGN.md section 12 and the skeleton README):
one attended boot of a generated trailer; kernel platform patch + machine descriptor/DTS
Makefile entry; kernel-config re-derivation for the shipping release; wiring the custom image
chain; sysupgrade container/install path and selector protocol; MAC/efuse source; switch
DSA-vs-switchdev; SMP; Wi-Fi scope; board compatible vs OpenWrt device name.

**Publication.** Part of the `opensource/` half whose publication is **NOT READY** (history
purge pending, [PUBLISH-READINESS.md](PUBLISH-READINESS.md)). The skeleton contains no vendor
binaries, blobs, dumps or credentials.

**Reading order:** [TARGET-DESIGN.md](TARGET-DESIGN.md) -> [ROADMAP-OPENWRT.md](ROADMAP-OPENWRT.md)
-> [SKELETON-SMOKE.md](SKELETON-SMOKE.md) & the trailer doc -> [`../target-skeleton/README.md`](../target-skeleton/README.md).
