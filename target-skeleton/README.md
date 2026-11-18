# Luofu / Cudy WR3000 v2.0 - OpenWrt target skeleton

> **STATUS: UNTESTED - SCHEMATIC - NOT A WORKING TARGET.**
>
> This directory contains a first-pass, reviewable **shape** of the OpenWrt target
> files described in [TARGET-DESIGN.md](../docs/TARGET-DESIGN.md) sections 4-6.
> Nothing here has been built inside an OpenWrt tree, and nothing here has ever run
> on a router. The only executable artifact is `tools/fip-slice.py`, and its safe path
> is the read-only `check` mode. The design these files encode is not accepted
> upstream, no OpenWrt target, board DTS, image recipe or driver series exists yet,
> and there is no device page. **Do not read this directory as upstream progress.**
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
| `target/linux/hisilicon/luofu/config-5.10` | kernel-config skeleton derived from the only kernel the lab has evidence for (5.10.201) - **NOT the final upstream config**; lab symbol names marked | `target/linux/hisilicon/luofu/config-<release>` after re-derivation |
| `target/linux/hisilicon/luofu/config-6.18` | placeholder copy of the 5.10 reference under the tree's kernel version; **not re-derived** - does not validate a 6.18 build | same, after a real `olddefconfig` re-derivation |
| `target/linux/hisilicon/image/Makefile` + `image/luofu.mk` | board image Makefile (includes `image.mk` and the subtarget file) plus the device stanza, kernel-slice assembly calling the FIP tool, UBI rootfs sketch and sysupgrade metadata sketch (**SCHEMATIC**) | `target/linux/hisilicon/image/Makefile` + `image/luofu.mk` |
| `target/linux/hisilicon/luofu/profiles/cudy_wr3000-v2.mk.unused` | legacy profile sketch, kept only as history; main derives the device symbol from the image stanza | not copied (only if a targeted release uses profiles) |
| `target/linux/hisilicon/base-files/etc/board.d/02_network` | board detection / network-defaults sketch at board level (main idiom); deliberately configures no interface because the switch has no driver (**SCHEMATIC**) | same path (mode 0755) |
| `target/linux/hisilicon/dts/luofu-r116.dts` | draft upstreamable DTS derived from the lab DTS; TODO-heavy; proposed compatibles are **not** bindings (**UNTESTED**); the target `dts/` + `DEVICE_DTS_DIR := ../dts` mechanism is the chosen delivery, a kernel-patch copy is an alternative not to be hand-maintained in parallel | `target/linux/hisilicon/dts/luofu-r116.dts` |
| `tools/fip-slice.py` | decoded trailer parser + fail-closed check/assemble/manifest + **`make-trailer`** (generates a structurally valid trailer from user-supplied base DTB + overlays; refuses to skip the gate/match/overlay checks; no vendor bytes embedded; one attended boot of a novel trailer still required) | `scripts/fip-slice.py` (or a target-local tools path) in the OpenWrt tree |
| `../docs/TARGET-SKELETON.md` | one-page index/status of this skeleton, pointing back at TARGET-DESIGN.md | nowhere (repo documentation) |

Deliberately **absent**: a factory/install image stanza (the install path is an open
question, TARGET-DESIGN.md section 5.5), a sysupgrade `platform.sh` (needs the A/B
selector protocol, section 3.4), any Wi-Fi or switch driver, and any U-Boot source.

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
  convention.

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
ledger PHASE 93-100). This skeleton's image Makefile names the derivation and
`tools/fip-slice.py assemble --loader-stage/--text-offset` asserts it; the real build
wiring is still TODO.

---

## 4. How these files map into a real OpenWrt tree

They are not a drop-in target. The mapping is:

    openwrt/                                    target-skeleton/
    ├── target/linux/hisilicon/                 ├── target/linux/hisilicon/
    │   ├── Makefile                            │   ├── Makefile
    │   ├── Kconfig                             │   ├── Kconfig
    │   └── luofu/                              │   └── luofu/
    │       ├── target.mk                       │       ├── target.mk
    │       ├── config-<release>                │       ├── config-5.10  (reference only)
    │       ├── image/Makefile                  │       ├── image/Makefile
    │       ├── profiles/                       │       ├── profiles/
    │       └── base-files/...                  │       └── base-files/...
    ├── target/linux/hisilicon/dts/luofu-r116.dts  <- dts/luofu-r116.dts
    └── scripts/fip-slice.py                    <- tools/fip-slice.py

plus a **kernel patch series that does not exist yet** (TARGET-DESIGN.md sections 4.1,
7). At minimum that series must add:

- `arch/arm/mach-hisilicon/Kconfig` with `ARCH_HISI` + `ARCH_LUOFU` (template is in
  `target/linux/hisilicon/Kconfig`);
- `arch/arm/mach-hisilicon/Makefile`: `machine-$(CONFIG_ARCH_LUOFU) += luofu`;
- `arch/arm/boot/dts/Makefile`: `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb`;
- `arch/arm/Makefile`: `textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000`;
- a clean `mach-luofu` machine descriptor (the lab one carries bench breadcrumbs and
  a `dt_compat` for the vendor tree - none of which may be submitted);
- the driver + dt-binding series (watchdog, CRG, thermal, pinctrl, FMC, MDIO/PHY/GE-
  MAC, switch, PCIe - TARGET-DESIGN.md section 7), each with its YAML;
- a `platform.sh` implementing `platform_check_image`/`platform_do_upgrade`
  (TARGET-DESIGN.md section 5.4) - **not written**;
- the sysupgrade container wrapper that adds the kernel slice, rootfs UBI and
  `fip-manifest.txt` to `CONTROL` - **not written**;
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
  `make target/linux/prepare` RC=0 (kernel 6.18.60). No compile/image/boot. Details:
  [../docs/SKELETON-SMOKE.md](../docs/SKELETON-SMOKE.md).
- `dts/luofu-r116.dts`: parses after `cpp` + `dtc` to a DTB on the host. dtc emits
  exactly one expected warning: `unique_unit_address: /clock-reset-controller@14880000
  duplicate unit-address (also used in node /watchdog@14880000)` - that is the
  documented open question that the vendor watchdog shares the CRG page; a submission
  cannot keep both nodes. This is a **syntax** result only: no bindings, no
  `make dtbs_check`, no kernel build.
- `base-files/etc/board.d/02_network`: `sh -n` passes.
- The target Makefiles were **parsed by OpenWrt main's make** in the smoke test above
  (`make defconfig` and `make target/linux/prepare`); they have still never been
  compiled, imaged or booted, and the Kconfig stub/profile file are not used by main.

**Not done, and not claimed:** no OpenWrt build, no kernel build, no image build, no
boot, no device interaction, no UART, no bench tool run, no driver/binding review.

---

## 6. Unknowns and TODOs

Cross-cutting (all [U]/[I], TARGET-DESIGN.md section 12):

1. **Trailer regeneration** - decoded and implemented (`docs/TRAILER-FORMAT.md`,
   `fip-slice.py make-trailer`, byte-exact against both vendor containers); what
   remains is one attended boot of a novel generated trailer before the image recipe
   relies on it, plus the unknowns listed in TRAILER-FORMAT.md (constant meanings,
   628-byte slot region).
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
7. **Switch driver model** - DSA vs switchdev; blocks the board `02_network` port map
   and the DTS switch nodes.
8. **SMP** - no `platsmp` port; the draft DTS is UP-only.
9. **Wi-Fi scope statement** - out of the first submission; the vendor stack is not
   mac80211 and the firmware is closed (TARGET-DESIGN.md section 8.3).
10. **Publication sequence** - history purge before any of this can point at a public
    tree.

Skeleton-specific findings recorded while writing the files:

- **Board compatible vs device name**: `dts/luofu-r116.dts` keeps the lab root
  compatible `"hisilicon,luofu-r116", "hisilicon,luofu"`, while the image recipe sketch
  (following TARGET-DESIGN.md section 4.2) uses `SUPPORTED_DEVICES += cudy,wr3000-v2`.
  OpenWrt's `board_name`/sysupgrade match and the DTS compatible order must be made to
  agree; the decision is open (an upstream board DTS would normally carry a
  `cudy,wr3000-v2` compatible first).
- **Kernel config filename**: `config-5.10` is committed because 5.10.201 is the only
  kernel with lab evidence. TARGET-DESIGN.md section 4.1 says follow the release under
  development (6.12 was recorded on 2026-10-01; re-verify). The Makefile carries a
  `KERNEL_PATCHVER` placeholder with that caveat; the config must be re-derived, not
  renamed.
- **Duplicate unit address at 0x14880000**: the vendor watchdog and CRG share the
  page. The draft keeps both nodes with a TODO; dtc warns, deliberately. The upstream
  shape must resolve this (child of a syscon, or a narrower reg window).
- **Vendor 2-cell reset form**: kept for readability; the submission must convert to a
  documented reset-id table.
- **SP804 interrupt** `<1 0x2f 4>` is not a valid mainline PPI cell value (PPIs are
  0..15 in the binding). Marked NEEDS-VERIFICATION in the DTS; it only matters if the
  SP804 is used (the board uses the TWD).
- **Kconfig placement**: a target directory normally has no `Kconfig`; the required
  `ARCH_HISI`/`ARCH_LUOFU` symbols belong in the kernel patch. The file here is a
  review stub and says so.

---

## 7. Safety and publication constraints honored

- No device was touched: no bench tools, no UART, no SSH, no flash. This is a
  host-side documentation/skeleton deliverable.
- No vendor binaries, `.o/.ko`, firmware blobs, calibration data, extracted DTBs,
  U-Boot images, credentials or dumps are present. Named values (`0xd7b7ab1e`,
  40,732, slot sizes, addresses) are structural facts already documented in the
  published `TARGET-DESIGN.md`/`phase42-decode` analysis.
- No false buildability: every file says SCHEMATIC/UNTESTED, and this README states
  plainly that nothing here builds or boots.
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
