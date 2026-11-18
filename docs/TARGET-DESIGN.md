# Target design: an upstream OpenWrt port for the Cudy WR3000 v2.0 (HiSilicon luofu / Hi5671Y)

**Status: 2026-11-18. Design proposal - not a submission, not a receipt, not progress.**
No OpenWrt submission exists today. The target files, board DTS and the first two drivers
(CRG clock/reset, watchdog) are drafts under [`../target-skeleton/`](../target-skeleton/README.md):
they are host-compile/binding-validated only (SKELETON-SMOKE.md, fifth session), never booted and
not reviewed upstream. That is a recorded fact, not a judgement: the parent
`EVIDENCE-INDEX.md` row *"Upstream submission is not started"* and the ledger entry
**2026-11-17 00:20** say it, no target PR exists, and the rest of the driver/image/sysupgrade
series is still missing.

This document is the concrete design that the roadmap phases point at. It exists so a reviewer
can see the target layout, the boot/image requirements (including the vendor loader's FIP
container), the DTS conversion, and the per-driver upstream distance **before** any code is
claimed as "target work".

**How it relates to the other documents**

| document | relationship |
| --- | --- |
| `UPSTREAM-PORT-PLAN.md` (2026-10-05 snapshot) | Its subsystem inventory is still useful; its **target/DTS/image sections are superseded by this file**. |
| `ROADMAP-OPENWRT.md` | Phase plan; written around the bench-hybrid era. Updated 2026-11-18 to point here and to mark its superseded claims. |
| `PORT-STATUS.md`, `PORT-PLAN.md`, `DEPLOYMENT-DECISION.md`, `HYBRID-IMAGE-PLAN.md` | History (marked SUPERSEDED/FALLBACK in their own headers). Not design inputs. |
| `PORT-SURFACE.md` | The measured Wi-Fi port cost. This file does not repeat its numbers; the Wi-Fi blocker below points into it. |
| `VENDOR-SKB-BRIDGE-PORT.md` | Separate multi-step port of the vendor HYFI/bridge userspace contract (the v17 ceiling); not part of the first upstream target. |
| `PUBLISH-READINESS.md`, `NOTICE.md`, `README.md` | This half is **NOT READY to publish** (vendor `.o` history purge pending). Nothing in this file changes that. |
| parent `STATUS.md`, `EVIDENCE-INDEX.md`, `mem-entries.md` | Live state and the only evidence of record. The newest dated ledger entry wins. |

**Reading legend used throughout**

- **[V] verified now** - backed by a repo artifact, a committed document, or a dated ledger entry.
- **[I] needs implementation** - the shape is known; the code/recipe does not exist yet.
- **[U] unverified / open** - the design depends on a fact nobody has measured yet; must be closed before it is claimed.
- **[X] blocked** - externally blocked or out of scope for the first submission.

Paths outside `opensource/` refer to the **parent (private) repository**, which is the current
evidence store. The published half must contain no vendor binaries, blobs, dumps or credentials
(HEAD is clean of the ten vendor `.o` files; the history purge is still pending, see
`PUBLISH-READINESS.md`).

---

## 1. Scope and non-goals

**In scope**

1. A new OpenWrt target/subtarget and board profile for the WR3000 v2.0 (board R116).
2. A board DTS converted from the lab tree to documented upstream bindings.
3. An image recipe that produces a kernel slot the **stock vendor U-Boot** accepts, and a
   rootfs the stock boot flow can mount from NAND.
4. A/B `sysupgrade` semantics for the two kernel/rootfs slot pairs, including the vendor
   environment/boot-selector mechanism.
5. The driver inventory, its upstream distance, and the required new drivers.
6. Acceptance criteria and a staged plan.

**Non-goals (explicitly)**

- Running the vendor userspace on our kernel (the bench reversal track; see parent `PLAN-FULL-REVERSAL.md` and `STATUS.md`).
- Replacing or rebuilding U-Boot. U-Boot source is not published (the Cudy GPL page returned 404; the TR6560 BSP ships only U-Boot blobs), so the target is designed *for the stock loader*. [V] `HARDWARE-IDENTITY.md` "U-Boot capture / static analysis"; `phase50/bsp-build-status.md` §1.
- A new Wi-Fi driver. The Hi5622V100 stack is vendor-binary and GPL-source-requested, not open; see §8.3. [X]
- Publishing vendor-derived content: the vendor DTB/DTBO bytes, vendor modules, calibration data, dumps. The upstream artifacts must be regenerated from our own sources. [V] `PUBLISH-READINESS.md`, `NOTICE.md`.
- A general HiSilicon SoC family port. One board first; family support only if a second board is evidenced.

---

## 2. Hardware summary

| item | value | evidence |
| --- | --- | --- |
| Board | Cudy WR3000 v2.0, HiSilicon luofu v2-class, board R116; enclosure label may say v1 | `HARDWARE-IDENTITY.md` "Identity"; parent `BENCH-FACTS.md` §1 |
| SoC | HiSilicon Hi5671Y, 2x Arm Cortex-A9 (ARMv7-A), platform `hsan luofu` | `HARDWARE-IDENTITY.md`; `DEPLOYMENT-DECISION.md` §1 |
| RAM | 128 MB DDR3; vendor DT: `memory@80500000` reg `<0x80500000 0x7b00000>` (123 MB above the SoC window) | `soc/vendor-dt-notes.md` §3 |
| NAND | 128 MB NAND, ESMT F50L1G41LC: 2048-byte pages, 64-byte OOB, 128 KiB erase blocks; FMC controller at `0x10a20000` + 1 MiB window at `0x1c000000` | parent `tools/lab-src/drivers/mtd/nand/luofu/luofu-fmc.c` header |
| Wi-Fi | Hi5622V100 over PCIe: two endpoints `59e7:0005`; vendor modules `hi5622v100_wifi.ko` / `hi5622v100_plat.ko` + a closed 928,920-byte `FIRMWARE.bin` | `HARDWARE-IDENTITY.md`; `PORT-SURFACE.md`; `UPSTREAM-PORT-PLAN.md` §1 |
| Stock software | OpenWrt 22.03.6 vendor target `hisilicon/luofu`, kernel 5.10.201, 58 vendor modules under `/lib/hisilicon/ko`, all declaring `license=GPL` | parent `STATUS.md` §1; `FLASH-PLAN.md`; `GPL-REQUEST.md` |
| Bootloader | U-Boot 2022.07 `for luofu`; only FIP packages are accepted; UART is the only console and the U-Boot prompt is unreachable on this unit | `HARDWARE-IDENTITY.md`; parent `BENCH-FACTS.md` §5 |
| Kernel slot budget | 8,650,752 B per kernel slot (`kernela`/`kernelb`) | parent `BENCH-FACTS.md` §5 |
| Package arch | `arm_cortex-a9` / musl; stock packages are upstream OpenWrt `arm_cortex-a9` packages | `phase26/how-current-userland.md`; `DEPLOYMENT-DECISION.md` §1 |

Sibling boards on the same SoC family (Jeton AX3000 Core, V-SOL HG5013-4G) exist and are named in
`HARDWARE-IDENTITY.md`; they are **not** targets of this design. Public documentation for
"WR3000 v2" is partly wrong (it describes the MediaTek v1); the identity warning in
`HARDWARE-IDENTITY.md` must be repeated on the device page. [V]

---

## 3. Boot and firmware layout facts

These are the constraints the image recipe must satisfy. The bench found them the hard way; they
are **not** negotiable without the U-Boot source.

### 3.1 The loader

- U-Boot command: `bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}`. [V]
  parent `BENCH-FACTS.md` §5; the same string is in the dumped environment
  `dumps/router-20261108/enva.bin` (local evidence).
- `loadaddr` in the stock environment is `0x83200000`; `bootflag` is `a`/`b`. [V] same env dump.
- `bootfip` accepts only the vendor's FIP container: a uImage followed by a **fixed trailer**
  carrying the vendor base DTB and a DTBO/overlay container. A bare uImage is refused before kernel
  entry. [V] `phase42-decode/report.md`; ledger 2026-10-14 "The boot guard fell"; ledger
  **2026-11-17 08:05**.
- The pre-entry gate is the overlay applier `FUN_800743ec`: it reads the 4 bytes at
  `0x40 + ih_size + dtb_totalsize` (the end of the main DTB) and requires the magic
  `0xd7b7ab1e`; the kernel entry is taken **only if the applier returns 0**. The uImage hcrc/dcrc
  checks are on the legacy loader path that `bootcmd` never calls. [V] `phase42-decode/report.md`
  §"the gate"; ledger PHASE 95/99 (2026-10-11).
- The overlays are matched by `vendor_id`/`board_id` against the loader's environment/board
  data (the overlays also carry `board_name`; whether it participates in the match is [U]); the
  R116 board overlay is `board_id = 3`, `vendor_id = "hsan"`. [V] ledger PHASE 95 (2026-10-11);
  `soc/vendor-dt-notes.md` §8; env `vendorid=hsan` in `dumps/router-20261108/enva.bin`.
- The loaded DT is the loader's, not the kernel's appended DTB; the lab machine had to match
  `hsan-luofu` for this reason. [V] `tools/lab-src/arch/arm/mach-luofu/luofu.c` header;
  `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts` header.

### 3.2 The kernel slot ("FIP slice")

```
+------------------+--------------------------------------------+-------------------------------+
| uImage header    | kernel payload                              | fixed trailer                 |
| 0x40 bytes       | zImage (+ appended DTB in the lab lane)     | vendor base DTB + DTBO cont.  |
+------------------+--------------------------------------------+-------------------------------+
^ 0x00                                                           ^ 0x40 + ih_size
```

- uImage identity used by the bench: `mkimage -A arm -O linux -T kernel -C none
  -a 0x80608000 -e 0x80608000 -n Linux-5.10.201`; `bootfip` reads it as
  `Image Name: Linux-5.10.201`. [V] parent `build/linux-kbuild/build-3dl.sh` (local build lane);
  ledger PHASE 100/125 (2026-10-11/12, `uImage load/ep 0x80608000`); parent `STATUS.md` §3.
- Load/entry address 0x80608000: the loader stages the slot at 0x83200000 and the decompressor
  computes `(pc & 0xf8000000) + TEXT_OFFSET`; with TEXT_OFFSET 0x00608000 this is 0x80608000.
  The lab hit this as a link-address defect; the upstream recipe must derive/assert it rather than
  copy it blindly. [V] ledger PHASE 93-100 (2026-10-11); [I] build-time assertion.
- The trailer in the working lab lane is **exactly 40,732 bytes** and is copied verbatim into
  every slice (it is not rebuilt). [V] `tools/fire-preflight.sh` fails closed unless
  `tail -c 40732` matches `build/linux-kbuild/slice-trailer-40732.bin` (local, md5
  `7e5a83c0dd424006f4b2f3f4235a01a6`, sha256 `336b0792...`); ledger **2026-11-17 08:05**
  records DTB+trailer identical across 3df/3dg/3dh/3dl.
- Measured trailer anatomy (parsing the local artifact; values reproduced from the ledger):

| offset | size | content |
| --- | --- | --- |
| 0 | 31,220 | vendor base DTB, FDT magic `d00dfeed`, totalsize `0x79f4`, sha256 `947ec62d...` in the stock image |
| 31,220 | 96 | overlay descriptor: BE magic `0xd7b7ab1e`, +4 = `0x2588` (9,608 = descriptor 96 + ov1 4,592 + ov2 4,920), +32 = 4,592 (ov1 size), +36 = 96 (own length), +64 = 4,920 (ov2 size), +68 = 4,688 |
| 31,316 | 4,592 | overlay DTBO `board_id=0` (reference board) |
| 35,908 | 4,920 | overlay DTBO `board_id=3` (this board, R116) |

  The vendor tail recorded in the ledger is **40,828 B**; the fixed lab artifact is 40,732 B, i.e.
  96 B shorter, and the missing bytes fall inside the last overlay's 1,024-byte FDT trailing
  padding (its logical content ends at 39,804 B). Both lengths have booted on this bench (40,828 in
  the 2026-10-11 fires, 40,732 in the 2026-11-15/18 slices), so the loader tolerates the shorter
  tail. **How 40,732 was derived is not documented in the repo** - the upstream recipe must
  regenerate a byte-identical or semantically equivalent trailer and prove it with one attended
  boot. [V] ledger PHASE 91 (vendor tail decomposition, 2026-10-11); [V] local artifact parse;
  [I][U] regeneration; [U] why the 96-byte padding is absent.

### 3.3 The NAND partition map

From the vendor base DTB (`soc/vendor-dt-notes.md` §6; the same table is in the lab DTS
`tools/lab-src/arch/arm/boot/dts/luofu-r116.dts:622-646`). Total NAND `0x8000000` = 128 MB.

| # | label | offset | size | # | label | offset | size |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | esbc | 0x0 | 0x40000 | 9 | log | 0x900000 | 0x440000 |
| 1 | uboota | 0x40000 | 0x100000 | 10 | pstore | 0xd40000 | 0x40000 |
| 2 | ubootb | 0x140000 | 0x100000 | 11 | kernela | 0xd80000 | 0x840000 |
| 3 | enva | 0x240000 | 0x40000 | 12 | kernelb | 0x15c0000 | 0x840000 |
| 4 | envb | 0x280000 | 0x40000 | 13 | rootfsa | 0x1e00000 | 0x1740000 |
| 5 | fac | 0x2c0000 | 0x200000 | 14 | rootfsb | 0x3540000 | 0x1740000 |
| 6 | bdinfo | 0x4c0000 | 0x40000 | 15 | rootfs_data | 0x4c80000 | 0x1600000 |
| 7 | cfga | 0x500000 | 0x200000 | 16 | upgrade | 0x6280000 | 0x1d80000 |
| 8 | cfgb | 0x700000 | 0x200000 | | | | |

- `kernela`/`kernelb` are 8,650,752 B (0x840000) each; `rootfsa`/`rootfsb` 24,313,856 B each. [V]
- The **current lab DTS is wrong for upstream**: it truncates `upgrade` to 0x1500000 and carves an
  `0x800000` `lab` partition out of it for bench scratch. That node must not exist in the
  submitted DTS. [V] `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts:638-646`; the second copy
  `soc/luofu-r116.dts:652` already carries the stock `upgrade` size.
- The vendor system formats `rootfs_data` as JFFS2 and uses it as the persistent overlay; the lab
  init mounts it as JFFS2 (`/vendor`), and OpenWrt can consume it as the overlay partition. [V]
  `HAZARDS.md` §6; `FLASH-PLAN.md` 0.3; [U] whether upstream should keep JFFS2 or reformat as
  UBI (design decision in §5.3).
- The lab lane also used **mtd16 `upgrade`** as a scratch UBI volume (`owrt`, `ubiblock0_0`)
  while leaving the vendor slots untouched. That was a bench convenience, **not** the upstream
  layout. [V] ledger PHASE 124 (2026-10-12); [V] `FLASH-PLAN.md`. Upstream should use the slot
  pair.

### 3.4 Slot selection and environment

- Selector cell: sysctrl/sysenv `0x10100C08`. U-Boot prints `bootreg init, set 10` (A pair) or
  `set 11` (B pair); a manual slot-B fire wrote `0x21`; `0x10` is the A pair and `0x11` the
  B pair in the loader's own print. [V] parent `BENCH-FACTS.md` §5. The exact write semantics
  for both slots are only partly measured - an A-side handover value is **not established**, and
  the old "plain `fw_setenv` works" assumption was wrong (the binary is a symlink to
  `fw_printenv`). [V] same section; **treat the selector protocol as [U] until one attended
  A/B validation**.
- A draft restart driver (patch `010`) saves the selector before a reset by
  reading the cell and writing the same value back. That preserves the pair without
  inventing the value the stock path writes, but it does not close the protocol: the
  Linux read path for `sysupgrade` and one attended A/B validation remain open. [I]
- Environment blocks: `enva` (mtd3) and `envb` (mtd4), 128 KiB each; the measured block layout is
  `crc32-little-endian(4) | flags(1) | data(131067)`; the bench writes **both** copies when
  switching. [V] `FLASH-PLAN.md` "Slot switch recipe"; [I] an upstream helper that reproduces
  the format and is validated.
- Stock U-Boot environment (from `dumps/router-20261108/enva.bin`, local evidence) also carries
  TFTP update commands: `fipupdate_eth` writes both kernel slots; `squashfsupdate_eth` /
  `rootfsupdate_eth` write both rootfs slots. This is an **installation/recovery** path that
  ignores A/B semantics; `sysupgrade` must not use it. [V] local env dump; [U] its use as the
  factory-install path (§5.5).
- Bootloader failover: a failed handover is followed by a failover read of the other slot, but the
  older claim *"a failed boot leaves a status word that makes bootfip reject the slot"* is **not
  supported** by the 2026-11-16 captures. Do not build a rollback plan on it. [V]
  `BENCH-FACTS.md` §5.

---

## 4. Proposed OpenWrt target

### 4.1 Recommendation

**`target/linux/hisilicon`** (new target) with subtarget **`luofu`**:

```
target/linux/hisilicon/
├── Makefile                 # BOARD:=hisilicon, SUBTARGETS:=luofu, ARCH:=arm
├── image/Makefile           # target-level image helpers (FIP slice, UBI rootfs)
└── luofu/
    ├── target.mk            # BOARDNAME, CPU_TYPE:=cortex-a9, FEATURES, DEFAULT_PACKAGES
    ├── config-6.12          # kernel config for the subtarget (name tracks the chosen kernel)
    └── image/               # (or keep the images in ../image/Makefile) Device/cudy_wr3000-v2
```

Why this path:

1. The vendor firmware itself calls the platform `hisilicon/luofu` (target/revision strings in
   `DEPLOYMENT-DECISION.md` §1), and the board DT root is `hsan-luofu`. [V]
2. A target/subtarget split lets a second luofu-family board (Jeton AX3000, V-SOL HG5013-4G) be
   added as another subtarget or device without renaming the target. [V] `HARDWARE-IDENTITY.md`
3. `arm_cortex-a9` is an upstream OpenWrt package architecture already used by the stock image,
   so no new binary feed architecture is implied. [V] `phase26/how-current-userland.md`;
   `DEPLOYMENT-DECISION.md` §1.

Alternative considered - **`target/linux/triductor`** with a `tr6560`/`luofu` subtarget: the only
public OpenWrt target for this silicon family is the Banana Pi TR6560 BSP's
`target/linux/tr6560`, which is a real OpenWrt 22.03 target skeleton. [V]
`phase50/bsp-build-status.md` §1. But that BSP is a *different board family* whose drivers bind
`tri,*`, while this board's firmware and DT are `hsan,*`; adopting the triductor target name
would import the wrong binding namespace and the wrong partition/radio assumptions. Naming is for
OpenWrt reviewers to confirm in the target PR; this document proposes `hisilicon/luofu`.

The target must add the platform Kconfig bits the lab tree already carries as a patch:
`ARCH_HISI` + `ARCH_LUOFU` (in `arch/arm/mach-hisi/Kconfig` style), `machine-$(CONFIG_ARCH_LUOFU) += luofu`, and
`dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb`. [V] `lab/luofu-kernel/README.md` "The image lane";
parent `tools/lab-src/arch/arm/mach-hisi/Kconfig`.

Kernel baseline: follow whatever kernel the OpenWrt release under development uses (`PORT-PLAN.md`
recorded 6.12 for 25.12.5 on 2026-10-01; re-verify at implementation). [V] `PORT-PLAN.md` §"three
facts"; [I] porting the 5.10.201 lab drivers forward is a work item, not a formality.

### 4.2 Target skeleton (schematic, not valid code yet)

```make
# target/linux/hisilicon/Makefile
include $(TOPDIR)/rules.mk
ARCH:=arm
BOARD:=hisilicon
BOARDNAME:=HiSilicon ARM SoCs
FEATURES:=squashfs nand ubifs
SUBTARGETS:=luofu
KERNEL_PATCHVER:=6.12          # follow the release; do not hardcode in code
KERNELNAME:=zImage
include $(INCLUDE_DIR)/target.mk
DEFAULT_PACKAGES += kmod-mtd-rw ...   # TODO: final package list
$(eval $(call BuildTarget))
```

```make
# target/linux/hisilicon/luofu/target.mk
BOARDNAME:=luofu (Hi5671Y)
CPU_TYPE:=cortex-a9
FEATURES+=nand
DEFAULT_PACKAGES += ubifs
```

```make
# target/linux/hisilicon/image/Makefile  (device stanza sketch)
define Device/cudy_wr3000-v2
  DEVICE_VENDOR := Cudy
  DEVICE_MODEL := WR3000
  DEVICE_VARIANT := v2.0 (HiSilicon luofu R116)
  DEVICE_DTS := luofu-r116
  KERNEL_SIZE := 8650752
  KERNEL_LOADADDR := 0x80608000
  SUPPORTED_DEVICES += cudy,wr3000-v2
  IMAGES := sysupgrade.bin
  # kernel slice = uImage + generated FIP trailer; rootfs = squashfs in a static UBI volume
  IMAGE/sysupgrade.bin := sysupgrade-tar | append-metadata
endef
TARGET_DEVICES += cudy_wr3000-v2
```

The snippets are **shape only**; they are not a claim that any of it compiles or is the accepted
OpenWrt idiom. [I]

**Target kernel config (`luofu/config-<ver>`), minimum content** [I]: `ARCH_HISI`,
`ARCH_LUOFU`, the ARMv7/A9 and PL310/L2 options the lab mirror carries; `MTD`, `MTD_NAND_CORE`,
`MTD_UBI`, `MTD_UBI_BLOCK`, `SQUASHFS`+`SQUASHFS_XZ`, `JFFS2`, `PSTORE`/`PSTORE_RAM` (and the
MTD pstore story - the 5.10.201 lab notes show `PSTORE_BLK`/`MTD_PSTORE` are unreachable in a
vanilla tree, so a decision is needed); `SERIAL_8250_DW`/`SERIAL_8250_CONSOLE`, `SERIAL_EARLYCON`;
`GPIO_DWAPB`, `I2C_DESIGNWARE_PLATFORM`; `WATCHDOG` plus the new watchdog driver symbol (lab name `LUOFU_WATCHDOG`); `PINCTRL`+
`PINCONF`+`GENERIC_PINCONF`; `MDIO_BUS`, `PHYLIB`, `PHYLINK`; `NET`, `VLAN_8021Q`,
`BRIDGE` and the netfilter/bridge options if the design copy needs them; `PCI`, `PCI_DW`/
`PCI_DW_HOST`, `PCI_DOMAINS_GENERIC`, `PCI_MSI_IRQ_DOMAIN`; `THERMAL`,
`THERMAL_OF`; and the OpenWrt userspace basics. The fragment/fragment-audit files in
`lab/luofu-kernel/` are the starting inventory, not the final config: they were written
against 5.10.201 and include bench-only symbols. [V] `lab/luofu-kernel/luofu.fragment`,
`luofu-shrink.fragment`, `fragment-audit.md`; [I] the actual target config.

---

## 5. Image recipe

### 5.1 Required build products

| product | requirement | source of truth |
| --- | --- | --- |
| kernel slice | uImage (legacy, uncompressed) + FIP trailer; <= 8,650,752 B; gate magic at `0x40+ih_size+dtb_totalsize` | §3.2 |
| rootfs UBI | squashfs image in a static UBI volume named `squashfs`; <= 24,313,856 B (rootfs slot) | `FLASH-PLAN.md` 0.1 (geometry `p=131072, m=2048, s=2048, O=2048`; 117 LEBs of 126,976 B in the lab build); §3.3 |
| sysupgrade container | OpenWrt `sysupgrade.tar` with `CONTROL`, the full kernel slice, the rootfs UBI, plus a FIP manifest (below) | [I] |
| factory/install image | **open question** (§5.5) | [U] |

### 5.2 Kernel slice generation

```
kernel-bin (zImage) [+ appended DTB]  -> mkimage uImage
our board DTB + our 2 overlays (dtbo) + 96-byte descriptor -> trailer.bin
uImage + trailer.bin = kernel slice
```

- Use the OpenWrt build's own compiled board DTB for **both** the appended DTB (standard
  `append-dtb`) and the trailer base DTB, so there is one source. Note the loader passes its own
  DT and ignores the appended one on this unit; the appended copy is standard hygiene, not the
  mechanism. [V] `tools/lab-src/arch/arm/mach-luofu/luofu.c` header; [I] confirm on hardware.
- Generate the trailer from source, not from a vendor dump: a build helper (working name
  `gen-fip-trailer.py`) that
  1. compiles the board DTS to a DTB and asserts its FDT `totalsize`;
  2. compiles the two board overlays (source DTS) to `.dtbo` with the `vendor_id="hsan"` and
     `board_id` properties, and the fixups/target paths the loader expects;
  3. emits the 96-byte descriptor with the measured field layout (`0xd7b7ab1e`, container total,
     overlay sizes, table length) - exact field-by-field shape is in §3.2;
  4. concatenates base DTB + descriptor + overlays and **fails the build** unless:
     - `u32 at (0x40 + ih_size + dtb_totalsize) == 0xd7b7ab1e`;
     - the descriptor's size fields equal the actual overlay lengths;
     - `uImage total + trailer <= 8,650,752`.
- The build must assert the uImage load/entry address against the kernel's computed
  `TEXT_OFFSET`/staging base rather than hardcoding 0x80608000 without a check (§3.2). [I]
- [V] The overlay match rule and descriptor are decoded (2026-11-18): vendor_id env vs overlay
  `vendor_id`, `compatible` strstr against an 11-row family table, `board_id` vs env or the 4-bit
  strap; the 96-byte descriptor is a 32-byte header + 32-byte entries, with hard-checked `32`
  constants, and the applier applies the first matching overlay with libfdt. The generator is
  implemented as `target-skeleton/tools/fip-slice.py make-trailer` and reproduces the vendor
  2.4.15 and 2.5.24 containers byte-for-byte; it is not yet wired into the image recipe. Full
  byte tables and checks: `target-skeleton/docs/TRAILER-FORMAT.md`. Remaining closure: **one
  attended boot** of a novel generated trailer, plus the meaning of the constant fields and the
  628-byte slot-only region. [U] for those unknowns only.

### 5.3 Rootfs and overlay

- `rootfsa`/`rootfsb` hold the static squashfs UBI volume `squashfs`; the stock vendor rootfs
  already uses that shape, so the installer/upgrade can reuse the partition map without resizing. [V]
  `FLASH-PLAN.md` 0.1; `soc/vendor-dt-notes.md` §6.
- `rootfs_data` (mtd15, 22 MB) is the shared persistent overlay. Vendor format: JFFS2. Design
  choice: **keep JFFS2** for the first submission (it matches the vendor layout and lets the
  stock system and the new one share the partition; OpenWrt `mount_root` supports JFFS2), and
  note the degraded-NAND risk measured on this unit. [V] `HAZARDS.md` §6; parent
  `BENCH-FACTS.md` §5. Alternative: reformat as UBI at install time - [U], costs erase/program
  cycles on degraded NAND and is not needed for a first port.
- Keep one slot bootable at all times. `sysupgrade` writes only the inactive slot pair (§5.4).
- The lab's mtd16 `upgrade` scratch volume is **not** used; leave the vendor `upgrade` partition
  untouched. [V] §3.3.

### 5.4 A/B sysupgrade semantics

**Container metadata (proposed schema; [I])** - the OpenWrt tar plus one small manifest the
target's `platform_check_image` reads without touching hardware:

```
sysupgrade-hisilicon_luofu_cudy_wr3000-v2/
├── CONTROL                    # board=..., target=..., version=...
├── kernel                     # full FIP slice (§3.2)
├── root                       # rootfs UBI (§5.3)
└── fip-manifest.txt           # ih_size, dtb_totalsize, gate offset+magic, container sha256,
                                # overlay sizes, slice sha256, slice length
```

**`platform_check_image`** [I]: board match; slice length <= 8,650,752; recompute the gate magic
and container fields from `kernel`; verify `fip-manifest.txt` against the bytes; root UBI
volume name/geometry and length <= 0x1740000; refuse anything else (including a vendor
`recovery.bin`).

**`platform_do_upgrade`** [I], using `/proc/mtd` **labels** (never a bare number; the venue
lesson is in parent `LESSONS.md` 21):

1. Determine the active pair from the boot selector (read the sysenv cell and/or the loader's
   recorded value; exact read path [U], §3.4).
2. Target = the other pair: `kernela`+`rootfsa` or `kernelb`+`rootfsb`.
3. Erase/write the kernel slot with the slice; `ubiformat` the target rootfs slot with the UBI
   image; verify by reading back a hash.
4. Update the environment block(s) and the boot selector for the target pair - **only** via the
   measured recipe, both env copies, format `crc32-LE | flags | data` (§3.4).
5. Reboot; the previous pair stays bootable for rollback.
6. Keep `rootfs_data` (overlay) unless `-n`; the two slots share one overlay partition, so the
   overlay format must be identical between versions (it is the same filesystem, but package/kernel
   changes in it are not atomic - document this).
7. Never use U-Boot's TFTP `fipupdate_eth`/`rootfsupdate_eth` commands for `sysupgrade`
   (they overwrite both slots) [V] env dump; they remain a recovery/install option only.

**What not to rely on**: the vendor "slot failed" status word (§3.4). Rollback is by keeping the
other pair bootable and switching back, not by assuming the loader rejects a bad slot.

### 5.5 Installation path - open question

The stock loader only accepts the FIP container (§3.1), and this unit's UART can only be read
(no input, parent `BENCH-FACTS.md` §3), so "boot a TFTP rescue image and install" as commonly
written is not available on this specific bench. Opening the design questions:

1. Can the stock **TFTP recovery** (`recovery.bin`, RESET-counter entry) write an arbitrary
   image, or does it verify/expect a vendor container? The recovery file is a 20,975,775-byte
   Cudy image; nothing in the repo characterizes its write path. [U] parent `BENCH-FACTS.md` §4.
2. Can a first install be done from the stock vendor userspace (root shell over LAN) with
   `mtd write`/`ubiformat` to the inactive slot, exactly as the project's 0.1-0.3 builds did?
   That path is **measured** for rootfs `ubiformat` and kernel slot writes and is the safest
   documented installer, even though it requires temporary root on the vendor firmware. [V]
   `FLASH-PLAN.md` §0.1-0.3; [I] package it as a documented installer with hashes and a
   recovery card.
3. Only if both fail: a U-Boot-compatible full-image container for TFTP. [U]

The device page must not be written, and the target must not claim installability, until one of
these is executed end to end on a recovered unit.

### 5.6 Boot arguments

The stock loader passes the DT `/chosen/bootargs` from its own tree:
`noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2 additional_cpus=1` (vendor
base DTB, `soc/vendor-dt-notes.md` §2). Where the root filesystem selector (`root=`, `ubi.mtd=`,
`ubi.block=`) comes from is **not established**: the lab boots used a built-in cmdline/extend;
the vendor chosen bootargs carry no UBI argument. If the trailer's base DTB is ours (as designed in
§5.2), the cmdline can be carried there - [U] whether the stock U-Boot overrides `/chosen/bootargs`
from its environment. Close it with one attended boot before the recipe depends on it.

---

## 6. DTS plan

### 6.1 Starting point and duplicates

- Working source: `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts` (parent repo; 942 lines) - the
  tree that is actually compiled into the lab slices. [V]
- Second copy: `docs/soc/luofu-r116.dts` (943 lines) - same tree with two differences
  that matter (stock `upgrade` partition instead of the lab carve-out; WDT node body expanded)
  and one that does not (a comment about a reverted PCIe experiment). [V] measured diff.
- Reference only: `docs/soc/luofu-r116-pinned.dts` (1,544 lines, machine-extracted
  vendor static tree) and `soc/vendor-dt-notes.md` (the extracted DT read, overlay shape,
  compatible census). These are **evidence**, not the upstream source; the upstream DTS must be
  our own file. [V] `PUBLISH-READINESS.md` "single-source upstream-style DTS" checklist item.
- Work: adopt one source (the lab tree), restore the stock partition map (§3.3), keep the second
  copy as generated/checked against it or delete it; do not ship two hand-maintained copies. [I]

The lab DTS header already states the conversion rule: free bindings are written in mainline form;
vendor `hsan,*` blocks become `hisilicon,luofu-*` placeholders with `status="disabled"`, each
with a TODO and its binding question. [V] `tools/lab-src/arch/arm/boot/dts/luofu-r116.dts` header.
The implementation is to replace the placeholders with documented bindings and drivers, not to
keep them.

### 6.2 Node-by-node conversion

Already mainline (reuse, no new binding): `arm,cortex-a9` CPUs, `arm,cortex-a9-gic` GIC,
`arm,cortex-a9-scu`, `arm,pl310-cache` L2, `arm,cortex-a9-twd-timer`, `arm,sp804` timer,
`snps,dw-apb-uart` UARTs, `snps,dw-apb-gpio`/`-port`, `snps,designware-i2c`,
`ethernet-phy-ieee802.3-c22`, `fixed-clock`, `fixed-factor-clock`, `fixed-partitions`,
`gpio-leds`, `gpio-keys-polled`, `operating-points-v2`, `pwm-regulator`. [V]
`soc/vendor-dt-notes.md` §9 ("already free in mainline").

New/derived bindings (propose one YAML per driver; all [I]):

| block | current compatible in lab DTS | proposed upstream binding / action |
| --- | --- | --- |
| board root | `hisilicon,luofu-r116`, `hisilicon,luofu` | keep; add the loader-match properties (`vendor_id`, `board_id`, `board_name`) in the trailer base DTB/overlays only if the match rule requires it (§5.2) |
| CRG clocks+reset | `hsan,crg` + 16 `hsan,clk-gate` + 2 mux + 2 PLL children | **written** as patch `004`: one `hisilicon,luofu-crg` syscon + simple-mfd node with YAML `clock/hisilicon,luofu-crg.yaml` and header `dt-bindings/clock/hisilicon,luofu-crg.h`; the 16 gates are a driver-side table indexed by the header's IDs (no fake child nodes), and `#reset-cells=<2>` remains the lab `(offset, bit)` form documented and range-checked in the binding/YAML. Still TODO: named reset-ID table, per-gate parent/PLL/mux model, bench-confirm the provisional 0x20-group offsets |
| watchdog | `hisilicon,luofu-wdt` | **written** as patch `005`: child of the CRG syscon node (no `reg`, no overlapping MMIO), YAML `watchdog/hisilicon,luofu-wdt.yaml`; the vendor magic values/offsets are hwver-2 driver data, no vendor DT properties, no IRQ and no restart handler (both documented TODOs; the restart policy now lives in the patch-`010` `hisilicon,luofu-reboot` driver, which uses the watchdog only as a fallback) |
| peripheral pinctrl | `hsan,luofu-peri-pinctrl` (the measured driver string) | **written** as patch `011`: `hisilicon,luofu-pinctrl` with the 37 pins / 24 groups / 24 functions tables re-verified from the vendor module, mainline pinmux/pinconf drivers, YAML `pinctrl/hisilicon,luofu-pinctrl.yaml` (no separate dt-bindings header needed); probe writes no register and consumer pin states ([U]) are not attached yet. Host compile only; never booted. |
| FMC / SPI-NAND | `hisilicon,luofu-fmc` with `bus_id`/`spi_cs`/`enable-quad-mode` vendor props and 17 `fixed-partitions` children | **written** as patch `006`: the controller is a SPI-MEM controller (`drivers/spi/spi-luofu-fmc.c`) and the mainline SPI-NAND stack behind it (`drivers/mtd/nand/spi`, ESMT `F50L1G41LC`) owns chip detection, on-die ECC, OOB layout, bad-block handling and `fixed-partitions`; YAML `Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml` (flat `mtd/` - 6.18 has no `mtd/nand/` binding directory). The vendor props are gone: `spi_cs` -> the child's `reg`, `enable-quad-mode` -> `spi-{tx,rx}-bus-width = <4>`, `bus_id` -> nothing; the DTS node becomes a SPI bus with one `spi-nand` child, and its `sfc_rst` reset consumes the CRG 2-cell form (`<&crg 0x2c 0x0>`) and its clock the `LUOFU_CLK_SFC` ID. Still TODO: `u-boot,env` for `enva`/`envb` ([U] format compatibility), read-only flags, the unverified ECC/DMA/partition semantics, and the ESMT F50L1G41LC chip entry is a v6.19 addition that this 6.18 target gets from OpenWrt's generic backport `434-...F50L1G41LC.patch`. Host compile only; never booted. |
| GEMAC | `hisilicon,luofu-gmac` | **written** as patch `009`: YAML `net/hisilicon,luofu-gmac.yaml`; `phy-mode`, `phy-handle`, one clock and the two CRG reset lines are described; the driver is **PARTIAL** (binding/resource validation only; probe refuses because the TX/RX ring DMA model is not decoded), so the DTS nodes stay `disabled`; `gmac0` is not used by the vendor (mapping: 14380000->phy2, 14400000->phy3, 14480000->phy4 (wired), 14500000->phy5) |
| MDIO | `hisilicon,luofu-mdio` | **written** as patch `007`: YAML `net/hisilicon,luofu-mdio.yaml`; the four PHY children are standard `ethernet-phy-ieee802.3-c22`; the seven CRG resets are described and released by the driver before the bus scan |
| PHY | no node driver; standard PHY child nodes | **written** as patch `008`: a PHYLIB driver for the Hisilicon "Genius" PHY (ID `0x20669a00`, mask `0xffffff00`), bound via PHY ID, not DT compatible, so no YAML is added; the vendor firmware patch load is a TODO |
| internal switch | `hisilicon,luofu-dp`/`-pfe`/`-lsw-woe`, `hisilicon,luofu-pie` | **specified** in [LSW-SPEC.md](LSW-SPEC.md) (2026-11-18): switchdev over per-port netdevs (not DSA), synthetic module-id register map, port/queue/qid model, table formats, D0-D2 plan; driver and binding still unwritten. The vendor switchdev handlers are stubs in the shipped build and its rootif write is a userspace compatibility layer, not a hardware API. |
| PCIe host | `hisilicon,luofu-pcie` (two domains), vendor `iatu_rc`/`iatu_ep`/`acp-gate` props | **written** as patch `013`: `drivers/pci/controller/dwc/pcie-luofu.c` (+ YAML `pci/hisilicon,luofu-pcie.yaml`) programs the two RCs from DT `ranges`/iATU data with clocks, resets, RC mode and link handling; driver init data carries the iATU tables, and the hard rules are encoded (never read the `misc` window, aligned dwords only). The Wi-Fi endpoint is [X] and its nodes are not enabled. Host compile only; never booted. Reset release now uses the fixed polarity from patch `014`. |
| thermal | `hisilicon,luofu-tvsensor` + `thermal-zones` | **written** as patch `012`: `drivers/thermal/luofu_tvsensor.c` (read-only, vendor v4 conversion, 6.18 of-thermal API) + YAML `thermal/hisilicon,luofu-tvsensor.yaml`; the DTS zone `cls0-thermal` uses the vendor trips (115/125 C, 3 C hysteresis, 1 s polling) with no cooling map; threshold/IRQ/enable/trim registers are [U] and not written. Host compile only; never booted. |
| sysctrl/sysenv | vendor blocks `hsan,sysctrl`, `hsan,sysenv`, `hsan,rstinfo` are **not** in the lab DTS | **partly written** as patch `010`: a `hisilicon,luofu-sysctrl` syscon node for page `0x10100000` (boot selector at `0xc08`) plus the `hisilicon,luofu-reboot` node that consumes it; YAMLs `soc/hisilicon/hisilicon,luofu-sysctrl.yaml` and `power/reset/hisilicon,luofu-reboot.yaml`. Still TODO: the nvmem/sysupgrade read path and the selector protocol itself (§3.4) |
| SMP | `enable-method = "hisilicon,hsan_smp"` | a documented method, e.g. `hisilicon,luofu-smp`, plus `platsmp` port; the TR6560 BSP has a real `platsmp.c` as a reference shape (`CPU_METHOD_OF_DECLARE`) but different bindings, so it is reference only |
| CPU OPP/regulators | absent (TODO in lab DTS) | optional for a first submission: fixed-rate CPU is acceptable if documented; otherwise OPP table + `pwm-regulator` + efuse/nvmem cells; YAMLs as needed |
| efuse/RNG/LED PWM/PMU PWM/clk-corrector/net-cdev/woe_dev/woe_wifi | absent from the lab DTS; `hsan,*` in the pinned tree | decide per block: needed (efuse/nvmem for MAC? - [U], PWM for LEDs, RNG as hwrng) or out of scope for v1; do not invent bindings for unused blocks |

Additional must-do cleanup to be upstream-reviewable [I]:
- remove the `lab` partition and restore the stock `upgrade` size (§3.3);
- remove bench-only comments/instrumentation and `status="okay"` for blocks with no driver;
- `make dtbs_check` clean against the new YAMLs; no `unit_address_vs_reg` warnings;
- `Documentation/devicetree/bindings/` entries for every new compatible, plus the target's
  `MAINTAINERS` entries;
- no extracted vendor DTB props (e.g. `iatu_rc`) left in the DT;
- a device-page statement of the identity warning (the enclosure says v1).

---

## 7. Drivers: what exists and what is missing

### 7.1 Existing lab drivers (parent `tools/lab-src`, tracked mirror of the live 5.10.201 tree)

For the blocks that still have no driver in this tree (internal LSW switch and the GEMAC data
path), the evidence-graded port plan - what the repo knows, what is missing, the framework,
dependencies and effort - is [REMAINING-DRIVERS.md](REMAINING-DRIVERS.md), with the two deep
dives [LSW-SPEC.md](LSW-SPEC.md) and [PIE-DATAPATH.md](PIE-DATAPATH.md).

The tracked mirror is the source of record for the bench kernel; the published snapshots under
`lab/` are older and some of their READMEs are stale (the pinctrl README still says
`set_mux` is a stub, which commit `6decd10` changed - see
`lab/luofu-kernel/pinctrl/README.md` §2). Use the tracked mirror for state, not the
snapshot READMEs. [V] `tools/lab-src/FILES`; `tools/check-lab-src.sh`.

| block | live source (parent repo) | verified now | what is needed for upstream |
| --- | --- | --- | --- |
| FMC/NAND | `tools/lab-src/drivers/mtd/nand/luofu/luofu-fmc.c` (~8,000 lines) | [V] read path G1 green; write path byte-exact G2 (phase53-write-path-proof.md); MTD registration, partitions, UBI/JFFS2; 18 partitions on the lab kernel | [I] **draft in-tree driver written** (2026-11-18, patch `006`): `drivers/spi/spi-luofu-fmc.c` is a SPI-MEM controller, so the mainline SPI-NAND stack replaces the lab driver's own `mtd_info` - ESMT `F50L1G41LC` chip data, the on-die ECC engine, the ESMT OOB layout and real bad-block handling come from `drivers/mtd/nand/spi`. The lab breadcrumbs/sysctrl writes, the JFFS2/UBI in-tree patches, the `compat=5` acceptance and the "read ECC off" flag are absent. The two page-data interface packs and the ECC-type values are the measured register values, and the command-engine OP encoding reproduces every observed vendor byte. Still open: the FMC ECC semantics (host ECC engine vs on-die pass-through), the OOB bytes the DMA engine returns, whether the die reset the SPI-NAND core performs clears the quad-enable bit (the board runs quad and the mainline ESMT entry has no QE quirk), the 15 undecoded vendor init words (not written), IRQ/restart (absent by design), and the v6.19 ESMT chip-entry dependency (OpenWrt generic backport `434` in this tree). **Host compile only; never booted.** |
| CRG clocks/reset | `tools/lab-src/arch/arm/mach-luofu/crg.c` (~200 lines) and the more complete `drivers/clk/luofu/luofu-clk.c` snapshot (`lab/luofu-clk`, 1,461 lines) | [V] gate clocks + reset provider implemented from the pinned tree (the clock-provider side is the published 1,461-line snapshot); probe-time behaviour on a fired kernel is not re-verified here; the 0x20-group offsets are reconstructed and PROVISIONAL (binding doc says so) | [I] **draft in-tree driver written** (2026-11-18, patch `004`): `drivers/clk/hisilicon/crg-luofu.c` + `dt-bindings/clock/hisilicon,luofu-crg.h` + YAML binding; gates are a driver-side table indexed by the documented clock IDs (no fake child nodes), resets keep the documented 2-cell form, and the reset provider now sets `of_reset_n_cells = 2` (the lab `-EINVAL` from `devm_reset_control_array_get()` is explained). Still open: bench-confirm the 0x20-group gate offsets, per-gate parent/PLL/mux model (rates are not modelled), named reset-ID table, upstream review. **Reset polarity is fixed by patch `014`**: the vendor class is active-low (assert clears, deassert sets; measured `0x14880034 = 0x000ff309` with both RCs up), so consumers now release lines correctly through the standard API. **Host compile only; never booted.** |
| watchdog | `tools/lab-src/drivers/watchdog/luofu-wdt.c` (234 lines) | [V] decompiled fully; timeout encoding for hwver 0/1/2; probe reads only; the board's hwver=2 path matches the live set-offset | [I] **draft in-tree driver written** (2026-11-18, patch `005`): `drivers/watchdog/luofu-wdt.c` + YAML binding, 6.18 watchdog API (`devm`, `.set_timeout/.start/.stop/.ping`, DT `timeout-sec`), the magic values and offsets are hwver-2 driver data, and the watchdog is a child of the CRG syscon node so there is no overlapping MMIO claim (the lab `-EBUSY`/`devm_ioremap()` workaround is gone). No restart handler (reset path unverified) and no IRQ (status semantics unverified) - both documented TODOs. Still open: bench-confirm clear/enable ordering and boot-time running state, upstream review. **Host compile only; never booted.** |
| pinctrl | `lab/luofu-pinctrl/luofu-pinctrl.c` (published snapshot; 640 lines body) | [V] 37 pins / 24 groups / 24 functions transcribed; `set_mux` implemented from the vendor algorithm; pinconf bits decoded from the vendor module's config ops | [I] **draft in-tree driver written** (2026-11-18, patch `011`): `drivers/pinctrl/hisilicon/pinctrl-luofu.c` + YAML, mainline framework, six generic pinconf params decoded from the module; probe writes nothing, no consumer states yet (vendor DTS attaches none), reset absent in the vendor node. Still open: drive-strength mA mapping, probe/default pin state, MDIO two-state question, bench confirmation. **Host compile only; never booted.** |
| MDIO | `tools/lab-src/drivers/net/ethernet/luofu/luofu-mdio.c` (508 lines) | [V] MMIO transfer layout recovered from `hi_kmdio.ko`; live vendor topology measured (PHY 2..5 on `mdio0`); the lab MDIO/GEMAC carry a link, but with the vendor `hi_kphy` bound (M2 not met) | [I] **draft in-tree driver written** (2026-11-18, patch `007`): `drivers/net/mdio/mdio-luofu.c` on the 6.18 MDIO bus API (`devm_mdiobus_alloc_size` + `of_mdiobus_register`), gate clock + the seven CRG resets, YAML `net/hisilicon,luofu-mdio.yaml`; the register model/polled handshake is the one the previous out-of-tree 5.10 driver exercised on the lab board (a real C22 read of PHY ID `0x20669a09`), but **this port has only been host-compiled** - never booted, no bench run. Upstream review is open (the reset-array ordering assumption is documented). |
| Genius PHY | `tools/lab-src/drivers/net/ethernet/luofu/luofu-phy.c` (44 lines) | [V] PHY ID/mask recovered; genphy ops only; link/autoneg expected to work without the vendor patch | [I] **draft in-tree driver written** (2026-11-18, patch `008`): `drivers/net/phy/luofu-phy.c`, PHYLIB driver matched by PHY ID (`0x20669a00`/mask `0xffffff00`) with the generic clause 22 ops; no binding is added because the PHY is ID-matched and the nodes stay standard C22. The per-PHY firmware patch (`lsw/hi_ephy_%xv%x.bin`) load path is still a TODO, and the driver has only been host-compiled - never booted. |
| GEMAC | `tools/lab-src/drivers/net/ethernet/luofu/luofu-gmac.c` (717 lines) | [V] phylink driver frame; netdev; the vendor MAC is a phylink driver, so the shape is right; an earlier bring-up got a link via the lab MAC (`ETHERNET-BRINGUP-FRAMING.md`), and the final hybrid stack ran on vendor modules; no lab-MAC data path is proven | [I] **partial in-tree port written** (2026-11-18, patch `009`): `drivers/net/ethernet/hisilicon/luofu-gmac.c` claims the binding (`net/hisilicon,luofu-gmac.yaml`) and validates reg/clocks/resets/phy-mode/phy-handle, then **refuses to probe**: the TX/RX ring DMA model is not implemented and has never been decoded (the lab header's "rings are staged" was a comment without code, and the vendor's transmit helper is exported by another vendor module). No netdev is registered and the DTS nodes are `disabled`. Still needed: the ring/DMA model, NAPI TX/RX, DMA mapping, the MAC port-mode programming, then enable the nodes; the data path is the PIE engine and is specified in [PIE-DATAPATH.md](PIE-DATAPATH.md) (2026-11-18). M2 ("no vendor network modules") remains **NOT MET**. |
| internal LSW switch | **none** - only reverse-engineered vendor modules (`hi_lsw_pub/dp/pfe`, `hi_swa`) | [X] no driver source in either tree; the register blocks/port bitmap are in the DTS; the RE specification is [LSW-SPEC.md](LSW-SPEC.md) (module layering, synthetic module-id register map, port/queue/qid model, table formats, vendor switchdev stubs and rootif correction) | [I] the largest new SoC-side driver: **switchdev over per-port netdevs** (not DSA - no tag/CPU-port evidence), table/VLAN/learning/port config; driver/binding still unwritten, D1/D2 plan and bench campaign in LSW-SPEC.md. The PIE packet-engine draft is patch `015` (resources + ring scaffolding, refuses to bring up the data path); it must be integrated with this switch's egress qid/port map before Ethernet can work. |
| PCIe host | `lab/luofu-pcie/luofu-pcie.c` (1,664 lines) | [V] read-only inventory frame; DWC link state; a compile-gated bring-up exists but **was never validated**; the driver does not call `pci_host_probe()`; the misc window is write-only/forbidden to read; sub-word/unaligned accesses fault (panic class) | [I] **draft in-tree host driver written** (2026-11-18, patch `013`): `drivers/pci/controller/dwc/pcie-luofu.c` + YAML, DT `ranges`/iATU data, clocks/resets (fixed polarity via `014`), RC mode, link handling, `pci_host_probe`; the endpoint power GPIOs and real enumeration remain unverified, and the Wi-Fi endpoint driver is [X]. **Host compile only; never booted.** |
| thermal | `lab/luofu-thermal/luofu-thermal.c` | [V] temperature conversion decoded and numerically verified; threshold writes staged | [I] **draft in-tree driver written** (2026-11-18, patch `012`): `drivers/thermal/luofu_tvsensor.c` (read-only, vendor v4 formula) + YAML, DTS zone `cls0-thermal` with the vendor trips and polling delay; threshold/IRQ/enable/trim registers remain [U] and are not written; no cooling map (net cooling depends on the LSW block). **Host compile only; never booted.** |
| Wi-Fi | `lab/luofu-wifi/`, `lab/wifidrv0`, `lab/wifidrv1` | [V] a cfg80211 wiphy+netdev was registered in an earlier phase; the takeover reaches the firmware dialogue; **no data path**; see §8.3 | [X] blocked; not part of the first submission |
| machine | `tools/lab-src/arch/arm/mach-luofu/luofu.c` (~1,040 lines) | [V] machine descriptor, L2 AUX replay, bench breadcrumbs into sysctrl, no `smp_ops`, boot-time hacks | [I] split it: clean `mach-luofu` + drivers; delete breadcrumbs and bench-only writes; add SMP or document UP; upstream machine/board match. The lab `.restart` hook is replaced by the patch-`010` `hisilicon,luofu-reboot` driver (no machine hook; the bench-only crumb writes are gone) |
| sysenv / boot selector | **none** | [V] the vendor mechanism is only observed (cell + env copies); no driver exists (only the lab machine wrote the cell) | [I] **draft written** as patch `010`: `hisilicon,luofu-sysctrl` syscon node + `drivers/power/reset/luofu-reboot.c`, which saves the selector with a read-back write-same before a reset and then issues the PROVISIONAL CRG reset command; the read path a `sysupgrade` helper needs and the selector protocol remain [U]. **Host compile only; never booted.** |
| SMP | **none** - lab machine has no `smp_ops` | [V] `luofu.c` header says a UP boot with a warning is expected; the vendor DT method has no mainline match | [I] `platsmp` port (pen/smc release address from the vendor DT/sysctrl); reference shape in the TR6560 BSP `arch/arm/mach-triductor/platsmp.c` (different bindings) |
| efuse/nvmem, RNG, LED PWM, PMU PWM, clk-corrector, net cooling | fragment requests `CONFIG_LUOFU_HWRNG`/`EFUSE` but **no source exists**; the lab DTS has no nodes | [V] `lab/luofu-kernel/fragment-audit.md` ("7 requested LUOFU symbols never reached .config; of those 3 have neither a Kconfig entry nor a source file: HWRNG, EFUSE, LSW") | [I] decide per block; efuse may be needed for MAC address handling ([U] where the MAC address actually comes from - not established in the repo); LED PWM needed only if LEDs are modelled on it |

Reusable without a driver: UART (8250_dw), GPIO (gpio-dwapb), I2C (designware), GIC/SCU/L2/TWD/SP804.
[V] `soc/vendor-dt-notes.md` §9.

### 7.2 Lab code that must not be submitted as-is

The lab kernel is an instrument, not a submission. The tracked mirror contains bench-specific
kernel edits that must be dropped or reworked before any patch series: near-region
`module_alloc` (`arch/arm/kernel/module.c`), boot breadcrumbs/TEXT_OFFSET edits
(`arch/arm/kernel/head.S`, `compressed/head.S`), `vermagic.h` tag removal, the JFFS2 edits
(`fs/jffs2/*`), the slab exports (`mm/slab*.c`, `include/linux/slab.h`), the network ABI
edits (`include/linux/netdevice.h`, `include/linux/skbuff.h`, `net/core/*`, `net/bridge/br_fdb.c`),
`net/wireless/Kconfig` (`CONFIG_WIRELESS_EXT` under COMPILE_TEST), the UBI `compat=5` acceptance
(`drivers/mtd/ubi/io.c`), the lab config (`config/kernel.config`) and the initramfs vendor-stack
loader (`kbuild/init-vendorroot.c`). [V] `tools/lab-src/FILES`; `lab/luofu-kernel/README.md`.
They exist to keep the vendor modules loadable on 5.10.201 - the opposite of an upstream goal.

---

## 8. Required new work and blockers

### 8.1 Internal LSW switch

The wired Ethernet path is `gemac -> internal LSW -> PHY -> RJ45`; the wired jack is a GMII port
into the switch (`0x14480000` = `eth2` on the vendor). [V] `ETHERNET-BRINGUP-FRAMING.md` §2;
`tools/lab-src/arch/arm/boot/dts/luofu-r116.dts:774-837`. The switch blocks are described in the
DT (`lsw_dp` 7 register windows, `lsw_pfe` 10 windows, `lsw_woe`, `pie`), and the vendor
implementations are `hi_lsw_*`/`hi_swa` modules. **No driver source exists in any tree.** This is
the largest SoC-side work item. Driver design question: DSA (idiomatic for a multi-port internal
switch) vs a switchdev/netdev-per-port driver; the vendor stack uses its own switchdev/HYFI glue
(the multi-step port in `VENDOR-SKB-BRIDGE-PORT.md`), which is an argument for a clean DSA design
with our own port model rather than reproducing the vendor ABI. [I]

### 8.2 PCIe host

Two DWC-based root complexes; the Wi-Fi endpoints sit behind them, and the vendor driver stack
claims them. [V] `soc/vendor-dt-notes.md` §5; `UPSTREAM-PORT-PLAN.md` §1. The lab PCIe driver is
a **read-only frame**, not a host controller (§7.1). A first class submission can defer PCIe:
a wired-only board is still a useful OpenWrt device. The blockers are the same as the RE record's:
clock/reset sequencing, iATU windows, LTSSM, and the two endpoint functions. [I]

### 8.3 Wi-Fi (Hi5622V100) - blocked for upstream until open source exists [X]

The measured port surface is in `PORT-SURFACE.md`; the essential numbers (do not paraphrase them
as estimates):

- the Wi-Fi module is **not a mac80211 driver**: **0** mac80211 symbols referenced, a vendor-owned
  MAC inside the module, ~4,330 vendor namespace strings, 211 symbols imported through the vendor
  `HW_RTOS_NS` namespace - none of which exist in an upstream kernel;
- the firmware is a closed 928,920-byte blob; the radio firmware/licence question is unresolved;
- the vendor modules declare `license=GPL`; the GPLv2 §3 source request was sent 2026-10-01 and
  **no reply is held** (`GPL-REQUEST.md`; the parent ledger records it as sent with no reply;
  `JETON-GPL-EMAIL.txt` is a separate draft).

OpenWrt does not carry a closed kernel driver, so the Wi-Fi stack cannot be part of a submission
that claims to be open. The design consequence is a **wired-first target**: submission scope is the
SoC platform + NAND + Ethernet + standard userspace; Wi-Fi stays out until one of:

1. the GPL source arrives (`PORT-SURFACE.md` path A) - then port/upstream the parts that can be
   separated from the closed firmware, and document the blob dependency that remains;
2. a documented-blob exception is judged acceptable - only after the driver source/licence
   question is answered, and it would still not be a mac80211 driver;
3. a from-scratch open stack is funded - years, not months (`PORT-PLAN.md`, `PORT-SURFACE.md`).

Do not present the bench wifidrv/takeover line as an upstream driver. [V] `PORT-SURFACE.md`;
`phase49/README.md`; parent `EVIDENCE-INDEX.md` "G5: Wi-Fi is vendor-only"; `STATUS.md` §2 G5.

### 8.4 Bootloader and container (external dependency)

- U-Boot source is unpublished; only blobs exist in the BSP and the stock image. The target is
  **designed for the stock U-Boot**: FIP trailer shape, load address, envelope. This is a real
  upstream review risk, not a code problem: the image format is vendor-specific and must be
  documented in the target's image recipe and device page. [V] `HARDWARE-IDENTITY.md`;
  `phase50/bsp-build-status.md` §1.
- The trailer is currently copied from a vendor-derived local artifact and **cannot be regenerated
  from published material yet**; §5.2 is the plan to close that. [V] `PUBLISH-READINESS.md`
  checklist item "Reproducible artifact"; ledger 2026-11-17 00:20.
- Install path (§5.5) is open; no device page can be written before it is executed.

### 8.5 Environment/selector and SMP

Partially measured (§3.4) / missing (§7.1). Both are small but block `sysupgrade` and "a router
that behaves like a router"; the A/B protocol must be closed with attended bench validation.

### 8.6 GPL source dependency

The vendor's 58 modules declare GPL and the source request is unanswered. This is not only a
legal note: if the source arrives, several "from-scratch" rows above (CRG, FMC, GEMAC, LSW, PCIe,
Wi-Fi host glue) become ports of a known implementation and the schedule changes. Until then the
upstream path must remain clean-room/reverse-engineering based, with the provenance statement
required by `PUBLISH-READINESS.md` (GPL-compatible reverse engineering, not "clean-room"). [V]
`GPL-REQUEST.md`; `NOTICE.md`; `PUBLISH-READINESS.md` item 5.

### 8.7 Publication boundary

The upstream contribution cannot be made from a repository whose history contains vendor objects.
The current checklist is in `PUBLISH-READINESS.md`: purge the 10 tracked vendor `.o` files from
history, add GPL-2.0 text and SPDX/copyright, state provenance, publish the kernel delta series and
the trailer regeneration recipe. [V] `PUBLISH-READINESS.md`; `NOTICE.md`.

---

## 9. Acceptance criteria for an OpenWrt submission

A submission is ready when **all** of the following hold; each row is a reviewable artifact or a
device fact, not a percentage.

**Build and review [I]**

1. `target/linux/hisilicon/luofu` builds in CI: kernel, board DTB, two overlays, FIP trailer,
   squashfs+UBI rootfs, sysupgrade tar. Login/console = UART.
2. Build-time assertions: kernel slice <= 8,650,752 B; gate magic at
   `0x40+ih_size+dtb_totalsize`; descriptor size fields match the overlays; load/entry address
   matches the kernel's own `TEXT_OFFSET` computation.
3. `make dtbs_check` clean with one YAML per new compatible; `scripts/checkpatch.pl` clean on
   every driver patch; no lab instrumentation, no vendor `hsan,*` compatibles, no vendor-derived
   binary props in the DT.
4. One DTS source in the repository (no hand-maintained duplicates), stock partition map, no lab
   partition.
5. Device page states the hardware identity warning (enclosure says v1; PCB is luofu v2).

**Hardware (attended bench, recovery door armed) [I]**

6. A current-kernel image built from this target boots to OpenWrt userspace on NAND with **no
   vendor modules loaded**: console, procd, LAN.
7. The wired ports forward traffic; M2-equivalent: no vendor network module bound (the 2026-11-14
   caveat - vendor `hi_kphy` bound - must be gone).
8. `sysupgrade` from OpenWrt slot to the other slot works, the version changes, and switching
   back restores the previous system; `rootfs_data` survives or is explicitly wiped as chosen.
9. One installation path is executed end to end on a recovered unit and documented with hashes
   (vendor-root install or a characterized bootloader recovery, §5.5).
10. Recovery is documented and still physical (RESET counter + TFTP, parent `BENCH-FACTS.md` §4)
    for the installed system; no bricked-state path.

**Upstream process [I]**

11. Drivers are submitted as separate series with bindings, in dependency order (watchdog, CRG,
    thermal, pinctrl, FMC, MDIO/PHY/GEMAC, switch, PCIe), reviewed/merged before the target PR.
12. Target PR contains target/subtarget, board profile, image recipe, sysupgrade metadata, board
    files and device page; it states the vendor loader dependency, the install path, and the
    Wi-Fi status.
13. Wi-Fi is either absent from the submission scope with a documented reason, or backed by open
    source with a stated firmware/licence position.
14. Publication preconditions met (history purge, licences, provenance, reproducible
    kernel/trailer artifacts).

**Explicitly not met today** (2026-11-18): every row above except the identity warning. There
is no target PR, no DTS submission, no image recipe, no install path and no WiFi answer; the
target/DTS drafts and the driver drafts under `../target-skeleton/` (CRG, watchdog, FMC/SPI-NAND,
MDIO bus, Genius PHY, partial GEMAC, provisional restart, pinctrl, thermal, PCIe host) are
host-only skeleton work (compile/binding-validated, never booted, not reviewed upstream); the
GEMAC data path does not exist (its PIE specification is [PIE-DATAPATH.md](PIE-DATAPATH.md)), and
the internal switch driver is still unwritten (its specification and D1/D2 plan are in
[LSW-SPEC.md](LSW-SPEC.md)).

---

## 10. Staged plan

Stages are gated; each lists what can start **host-side** (no device risk) and what needs an
attended bench session. Bench fires remain gated by the parent rules (recovery armed, read-only
UART capture, human at the bench) - see parent `RUNBOOK.md`/`BENCH-FACTS.md`. [V]

| stage | deliverable | method | gate |
| --- | --- | --- | --- |
| **S0 design freeze** (host only) | this document; one DTS source with stock partition map; binding/YAML drafts; image/trailer generator with offline gate check; sysupgrade metadata schema | pure repo/kernel-tree work; no device | generator output passes the offline gate assertions; DTS compiles with `dtbs_check`; reviewed |
| **S1 kernel to console** | current-kernel image boots the board to UART with our DTS, no vendor modules | needs CRG (clocks/resets), watchdog, pinctrl (early), UART; one single-variable fire per change with recovery armed | our kernel's own probes/console appear; no silent reset; verified from capture |
| **S2 storage + userspace** | FMC/NAND driver upstream-quality; MTD partitions; squashfs+UBI rootfs mounts; OpenWrt userspace starts | FMC read/write already proven on 5.10 (G1/G2); port forward, strip lab hacks, add bad-block handling; bench | rootfs mounts from NAND, procd runs, no vendor modules |
| **S3 wired network** | MDIO+PHY+GEMAC, then internal LSW; LAN ports forward | measure MAC config values from the vendor (already partly done); implement TX/RX; LSW driver is the long pole; interim: one MAC + fixed-link to prove the path | M2-equivalent: all wired ports forward with no vendor module bound |
| **S4 image + sysupgrade + install** | full target image; A/B `sysupgrade`; documented install path | image recipe (§5), selector protocol validated, install path executed (§5.5) | upgrade/downgrade/rollback on the unit; recovery card verified |
| **S5 PCIe + Wi-Fi decision** | PCIe host controller; Wi-Fi decision executed | DWC host port; GPL follow-up; WiFi only if source/licence allows | endpoint enumerates; decision recorded (source received / blob documented / rewrite scoped) |
| **S6 upstream** | driver series, bindings, target PR, device page | submit in dependency order; address review; keep bench evidence separate | drivers merged; target PR merged; device page live |

Notes on ordering: S0 can start today and does not depend on the box. The small drivers
(watchdog, thermal, CRG once bench-confirmed) are the cheapest first upstream series. The LSW and
PCIe are the two large efforts; Wi-Fi is external. The bench-reversal line has stopped at its
documented ceiling (parent `STATUS.md` §3; ledger **2026-11-18 06:50**) and is not a prerequisite
for S0; S1-S4 do need bench sessions and a recovery plan.

---

## 11. Evidence map (claim -> artifact)

| claim | evidence |
| --- | --- |
| Board identity, SoC, Wi-Fi, stock firmware | `HARDWARE-IDENTITY.md`; parent `BENCH-FACTS.md` §1; parent `STATUS.md` §1 |
| Stock U-Boot 2022.07, FIP-only boot, fixed 40,732-byte trailer, kernel slots | parent `BENCH-FACTS.md` §5; `HARDWARE-IDENTITY.md` "U-Boot"; parent `EVIDENCE-INDEX.md` (loader/blocker rows); ledger **2026-11-17 08:05** |
| Gate = DTBO container, magic `0xd7b7ab1e` at `0x40+ih_size+dtb_totalsize` | `phase42-decode/report.md` §"the gate"; ledger PHASE 95/99 (2026-10-11) and 2026-10-14 entry "The boot guard fell" |
| Trailer anatomy (base DTB 31,220 + 96-byte descriptor + overlays 4,592/4,920) | ledger PHASE 91 (2026-10-11); local `build/linux-kbuild/slice-trailer-40732.bin` (md5 `7e5a83c0...`, sha256 `336b0792...`); parent `tools/fire-preflight.sh:101-107` |
| Partition map, kernel/rootfs sizes, env blocks | `soc/vendor-dt-notes.md` §6; parent `BENCH-FACTS.md` §5; `FLASH-PLAN.md` "Slot switch recipe"; local env dump `dumps/router-20261108/enva.bin` |
| Selector cell + measured values; "bootfip rejects slot" not supported | parent `BENCH-FACTS.md` §5 |
| FMC read/write proof | parent `EVIDENCE-INDEX.md` G1, G2 rows; `phase53-write-path-proof.md` |
| G4 (Ethernet on our kernel) is vendor-module-backed; M2 NOT MET | parent `STATUS.md` §2 gate table; `EVIDENCE-INDEX.md` G4 row; ledger 2026-11-14 23:15 |
| Every vendor module declares GPL; request sent 2026-10-01, no reply | `GPL-REQUEST.md`; `EVIDENCE-INDEX.md` "Vendor GPL obligation" row; the parent ledger records the request as sent 2026-10-01 with no reply |
| Wi-Fi port surface (211 namespaced symbols, 0 mac80211, closed 928,920-byte blob) | `PORT-SURFACE.md`; `EVIDENCE-INDEX.md` G5 row; ledger phase 22f/25 entries (2026-10-02/03) |
| No OpenWrt submission (target PR / DTS / image recipe / full driver series) exists; the `target-skeleton/` drafts and the first two drivers (CRG, watchdog) are host-only, never booted, never reviewed | `EVIDENCE-INDEX.md` "Upstream submission is not started" row; ledger **2026-11-17 00:20**; `target-skeleton/README.md` section 5; SKELETON-SMOKE.md fifth session |
| Publication not ready (history purge) | `PUBLISH-READINESS.md`; `NOTICE.md`; `README.md` snapshot note |
| Bench lab-kernel ceiling (v17) | parent `STATUS.md` §3; ledger **2026-11-18 06:50**; `EVIDENCE-INDEX.md` "Bench kernel line 2026-11-17/18" section |
| BSP target skeleton as prior art (different board/bindings) | `phase50/bsp-build-status.md` §1-3 |
| Lab driver inventory and source paths | parent `tools/lab-src/FILES`; `luofu-kernel/README.md`; `fragment-audit.md`; per-driver headers |
| ETHERNET-BRINGUP-FRAMING (species of bring-up bugs, PHY mapping) | `ETHERNET-BRINGUP-FRAMING.md` §2 |

Ledger entry dates used above are the parent `mem-entries.md` headings; the newest entry
(**2026-11-18 06:50**) wins over every older claim in this file.

---

## 12. Open questions / TODO list

1. **Trailer regeneration** - DECODED AND IMPLEMENTED 2026-11-18: descriptor layout, gate rule,
   overlay match table and validation order are documented in
   [../target-skeleton/docs/TRAILER-FORMAT.md](../target-skeleton/docs/TRAILER-FORMAT.md);
   `fip-slice.py make-trailer` reproduces the vendor 2.4.15 (40,828 B) and 2.5.24 containers
   byte-for-byte from user-supplied base DTB + overlays. Remaining: one attended boot of a novel
   generated trailer, the meaning of the hard-checked `32` constants and the 628-byte slot-only
   region, and why the bench artifact was cut to 40,732 B. (§3.2, §5.2)
2. **Boot arguments** - how `root=`/`ubi.*` reaches the kernel; whether the loader overrides
   `/chosen/bootargs` from its environment. (§5.6)
3. **Install path** - characterization of the stock TFTP recovery write path vs the documented
   vendor-root install. (§5.5)
4. **Selector protocol** - exact env-block/bootreg write sequence and value for both pairs; a
   read path usable from Linux. Patch `010` only writes back the value it has just read
   (a write-same), so the protocol itself is still open. (§3.4)
5. **MAC address / efuse** - where the unit's MACs come from (bdinfo/fac/efuse); needed for
   `nvmem` cells and the device page. [U]
6. **Overlay filesystem** - keep JFFS2 `rootfs_data` or convert to UBI at install. (§5.3)
7. **Switch driver model** - DSA vs switchdev; port numbering; whether PIE/WoC is in v1 scope. (§8.1)
8. **SMP** - platsmp port for the vendor's pen/smc release; is UP acceptable for a first
   submission? (§7.1)
9. **Wi-Fi scope statement** - the exact wording for the device page/target PR that does not
   claim an open radio. (§8.3)
10. **Publication sequence** - the history purge must happen before the target/driver series can be
    pointed at a public tree. (§8.7)

All open questions are [U]/[I] by construction; none may be presented as verified until the
artifact or the attended measurement exists.
