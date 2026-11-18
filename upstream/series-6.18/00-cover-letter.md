# [PATCH 00/18] ARM: hisilicon: add Hi5671Y "luofu" (Cudy WR3000 v2) platform and driver support

> Submission form for review, not a release. **Nothing in this series has
> ever run on hardware.** Read "Test evidence" and "No hardware validation"
> before reading any driver as working silicon support.

## What this series is

This is an 18-patch kernel series for the HiSilicon Hi5671Y "luofu" SoC
(2x Cortex-A9, PL310 L2, 128 MiB DDR3) and the Cudy WR3000 v2.0 board
(board R116, 128 MiB NAND, Hi5622V100 Wi-Fi).  The unit ships OpenWrt
22.03.6 on kernel 5.10.201 and its stock U-Boot boots a FIP container from
NAND; this series is the kernel side of a project that is porting that
platform to current Linux and to an upstream OpenWrt target.

The series adds:

- the ARM machine and early-boot support needed to build for the platform
  (0001-0003);
- an RFC, attended SMP bring-up (0018) whose CPU1 release is decoded from
  the stock vendor kernel and has never run on our kernel.  It is staged
  separately and is not part of the UP-first mergeable set;
- the documented-minimum drivers and devicetree bindings for the blocks
  whose register/protocol model is currently understood well enough to
  describe (0004-0008, 0011-0014, 0017);
- RFC or partial drivers for the blocks where the hardware model is not
  complete (0006, 0009-0011, 0013, 0015-0017).  These are explicitly
  marked and several refuse to probe.

It deliberately does **not** contain: the board DTS, the SMP devicetree
enable-method (0018 is inert without it; it belongs to the attended board
DTS stage), the Wi-Fi driver, the internal-switch data path, the GEMAC
TX/RX data path, the PIE data path, or any OpenWrt target/image-recipe
files.  See "Scope split" and
"Dependencies".

## Target hardware

- SoC: HiSilicon Hi5671Y "luofu" (2x Cortex-A9), vendor bootloader U-Boot
  2022.07 which only boots its own FIP container format.
- Board: Cudy WR3000 v2.0, vendor board name R116.  Note that the retail
  enclosure can say v1: the PCB is the luofu v2 design, not the MediaTek
  MT7981 v1 board.  This identity warning belongs on any device page.
- NAND: ESMT F50L1G41LC-class SPI-NAND behind the FMC controller.
- Wi-Fi: Hi5622V100, vendor-only kernel modules with no open source; the
  Wi-Fi data path is **[X] out of scope** for this series.
- Kernel image contract: the stock loader stages the kernel slot at
  0x83200000 and the uImage carries load/entry 0x80608000; the kernel must
  be built with TEXT_OFFSET 0x00608000 (patches 0002/0003).  This is a
  bootloader dependency that cannot be changed from the kernel side.

## Scope split

**1. Core platform (RFC, UP-first)** - 0001-0003
New arch/arm/mach-hisilicon machine under the existing ARCH_HISI umbrella,
the ARCH_LUOFU Kconfig/text-offset wiring, and the ARM early-boot change
that makes the non-immediate 0x00608000 TEXT_OFFSET assemble.  These
patches are UP-first: the machine descriptor leaves `.smp` unset (patch
0001 describes that state).  SMP bring-up is the separate, attended patch
0018 below and is not part of this mergeable set.

**2. Peripheral drivers, ready for review** - 0004, 0005, 0007, 0008,
0012, 0014
CRG clock/reset, watchdog, MDIO bus, Genius PHY, thermal sensor, and the
CRG reset-polarity fix.  "Ready" means: the implemented subset is backed by
a citable artifact (measured register, decoded vendor devicetree/module, or
a documented software interface), probe does not write unverified
hardware, and known gaps are documented in the driver.  It does not mean
the driver has been validated on hardware - none has.

**3. Peripheral drivers, RFC** - 0006, 0011, 0013, 0017
FMC SPI-NAND (critical storage path; ECC/OOB/quad-enable semantics
unverified, and it depends on a v6.19 SPI-NAND chip entry), pinctrl
(consumer/default pin states unmeasured), PCIe host (never enumerated;
write-only misc-window rules only), and the A/B boot selector (new sysfs
ABI; the recorded-pair contract is reconstructed from local evidence - the
bootloader env selects the pair, the SYSCTRL cell is its opposite-pair/failed-
boot record, and a boot-time recorder in the target image is required for
stability - but the protocol is not hardware-validated).

**4. RFC / partial drafts, do not merge** - 0009, 0010, 0015, 0016
GEMAC (binding/resource half only; probe refuses, no netdev, no data
path), machine restart (provisional CRG reset command), PIE packet engine
(ring scaffolding only; probe refuses), and the internal LSW switch (D1a
resource/binding skeleton; probe refuses).  These are included so the
design can be reviewed as a whole; they are not submission candidates
until the hardware inventory/write set exists.

**5. SMP bring-up (RFC, attended; not in the UP-first set)** - 0018
The CPU1 release (pen word, CRG resume bits, wakeup IPI) is decoded from
the stock vendor kernel's private `hisilicon,hsan_smp` method
(opensource/docs/SMP-RE.md) and registered as the documented devicetree
method `hisilicon,luofu-smp` in a new
arch/arm/mach-hisilicon/platsmp.c.  The method is fail-safe: it stays
inert when the enable-method or either syscon node is absent, and without
the enable-method the platform boots UP.  This patch is RFC and attended:
it has never released a CPU on our kernel, and its acceptance gate is the
two-stage plan in SMP-RE.md section 8 (S1 UP-first, S2 attended bring-up
with recovery armed).  The devicetree enable-method is deliberately not
in this kernel series - the board DTS is delivered through the OpenWrt
target for now; an S1 submission drops it and the DTS ships a single
cpu@0 node.

**6. Not-for-submission items (not in this series)**
- the board DTS and loader-match overlay: draft, TODO-heavy, delivered
  through the OpenWrt target's dts/ directory today; an upstream kernel
  DTS patch comes only after the bindings settle and a full-draft
  dtbs_check is clean (see 03-rationale-tree-placement.md);
- the OpenWrt target, image recipe (FIP trailer, squashfs-in-UBI,
  sysupgrade container), base-files/sysupgrade hooks, target config and
  host tools: userspace/OpenWrt tree content, not kernel patches;
- the A/B sysupgrade path and installer: host-only and mocked so far;
- Wi-Fi: [X], blocked on a vendor source/licence answer (GPL request sent,
  no reply);
- the bench/lab kernel edits (module_alloc, JFFS2/UBI hooks, initramfs
  vendor-stack loader, etc.): research instrumentation that must never be
  submitted;
- vendor-derived binaries, blobs, extracted DTBs or calibration data: none
  are in this series; drivers are GPL-2.0 code derived from documented
  reverse engineering of the vendor's GPL binaries (provenance note below).

## Dependencies

- Baseline: pristine linux-6.18.60 (tarball sha256
  3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e).
- The series is ordered: 0001-0003 must precede everything that selects
  ARCH_LUOFU; 0004 (CRG) must precede 0005, 0006, 0007, 0009, 0013, 0015
  and 0016 at the devicetree/clock-reset level; 0014 fixes 0004 and should
  be folded into it before a real submission; 0010 introduces the SYSCTRL
  binding that 0017 also uses.  0018 is last: it compiles independently but
  maps the pen/resume pages through the CRG syscon node (0004) and the
  SYSCTRL syscon node (0010), and stays inert if either is missing.
- 0018's devicetree enable-method (`hisilicon,luofu-smp`) is not in this
  series: the board DTS is OpenWrt-target content and is excluded above.
  A DT without the property boots UP (unmatched CPU method) - that is the
  UP-first staging, not a kernel patch change.
- The board DTS is not in the series.  The DTS currently lives in the
  project's OpenWrt target skeleton and consumes the bindings added here;
  a kernel DTS submission is a later series (arch/arm/boot/dts/hisilicon/
  already exists in 6.18, so it is a file+Makefile addition, not a new
  directory).
- The FMC controller (0006) needs the ESMT F50L1G41LC SPI-NAND chip entry,
  which is a v6.19 addition; it is not in 6.18.60 and is currently supplied
  by OpenWrt's generic backport 434.  Do not post 0006 before that entry
  exists upstream, or carry the chip entry in the same series.
- A MAINTAINERS entry is proposed in 02-maintainers-proposal.md.  The
  identity in the Signed-off-by lines and in the binding maintainers:
  fields is the project's git configuration identity
  (ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com>); DCO
  requires a real legal name before this is sent.
- Provenance: the register models are recovered from the vendor's
  GPL-2.0 kernel modules and devicetree by reverse engineering; no vendor
  code or binary is copied.  A GPL source request to the vendor was sent,
  no reply is held.  Reviewers must be told this explicitly; "clean-room"
  would be inaccurate.

## Test evidence (host-only; already recorded)

| # | check | result | evidence |
| --- | --- | --- | --- |
| 1 | sequential apply of the 18 series files to pristine 6.18.60 | patch(1) 18/18 RC=0; git am 18/18 RC=0; resulting trees identical; 0 fuzz, 0 rejects | 04-validation.md, validation/ |
| 2 | offset note | only 0008 has offsets (-75, -12 in drivers/net/phy/{Kconfig,Makefile}) because the source patch was generated against an OpenWrt-patched kernel tree; content applies cleanly | 04-validation.md |
| 3 | checkpatch.pl --no-tree (series patch mode, default and --strict) | 0 errors; 47 warnings, all explained (15 MAINTAINERS, 14 binding split, 16 long lines, 2 help paragraphs; plus 4 strict-only CHECKs) | 04-validation.md, validation/checkpatch-*.log |
| 4 | full kernel compile of 0001-0018 (SMP lane, 2026-11-18) | make target/linux/prepare RC=0 (1,202 patching lines, 0 rej/orig); make target/linux/compile RC=0, no compiler warning attributable to a new or changed file (the 38 modpost MODULE_DESCRIPTION messages are the pre-existing baseline), zImage 3,264,472 B sha256 ae59d998...; platsmp.o built with the luofu_smp_* symbols; CONFIG_LUOFU_SELECTOR=y; TEXT_OFFSET contract 0x80608000 unchanged; config re-derivation byte-idempotent | SKELETON-SMOKE.md (SMP session), target-skeleton/docs/port-notes/smp/SUMMARY.md |
| 5 | dt_binding_check per new YAML | RC=0 for every binding (dtschema 2026.9; yamllint unavailable, that one lint step skipped); the corrected boot-selector YAML re-checked 2026-11-18; 0018 adds no binding | SKELETON-SMOKE.md sessions 5-12, A/B + selector-fix + SMP sessions |
| 6 | board DTS compile (not part of this series) | patched-kernel dtc RC=0, zero warnings; 10,664 B sha256 5726f7cd... without the SMP line, 10,710 B sha256 bc599a4e... with the enable-method line (SMP lane) | SKELETON-SMOKE.md (A/B install + selector-fix sessions), port-notes/smp/SUMMARY.md |
| 7 | kernel config re-derivation | two kernel_oldconfig runs byte-identical (4106877f...), 309 symbols identical to the delivered delta, idempotent (re-run 2026-11-18) | SKELETON-SMOKE.md (A/B install + selector-fix sessions) |
| 8 | OpenWrt image chain (not kernel submission scope) | make world RC=0 in an isolated clone, reproducible container | SKELETON-SMOKE.md (image-recipe session) |
| 9 | hardware | none. no boot, no probe-on-silicon, no register I/O, no CPU1 release, no UART/TFTP/flash, no device | - |

Evidence tags used in the project: [V] verified from an artifact/measurement,
[I] implementation inference, [U] unmeasured, [X] blocked.  The only [V]
claims above are host-side build/patch/binding results and recorded vendor
register values; every "works on the board" claim remains [U].

## No hardware validation

There is no boot, no hardware validation and no `Tested-by` from this
series.  The platform has never booted a mainline kernel; none of these
drivers has ever probed on this board, and the SMP release (0018) has
never brought up CPU1 on our kernel.  The kernel built from this series
therefore proves compilation and interface conformance only.  Known open
hardware questions include: whether the CRG gate geometry is correct, the
FMC ECC/OOB/quad-enable semantics, the pinctrl default/consumer states,
the PCIe wrapper/link bring-up, the GEMAC ring/DMA model, the PIE channel
model, the LSW register/queue/port-id model, the restart command sequence
and selector protocol, and the SMP pen-word consumer, CRG resume-bit
semantics and SCU pre-state (SMP-RE.md sections 7-10).  Reviewers should
treat every driver as an RFC until the corresponding hardware step in
TARGET-DESIGN.md section 10 is done.

## Draft / refusing status (explicit)

| patch | status |
| --- | --- |
| 0009 GEMAC | **PARTIAL**: claims binding, validates resources, then refuses to probe. No netdev, no TX/RX data path, DT nodes disabled. Do not merge as a network driver. |
| 0010 restart | **RFC / PROVISIONAL**: selector save is measured, but the CRG software-reset command pair and its write order/width were never measured. Must not merge without recovery-armed hardware validation. |
| 0015 PIE | **DRAFT**: scaffolding only; probe allocates nothing permanent and returns -EOPNOTSUPP before touching a register. Not mergeable. |
| 0016 LSW | **DRAFT / D1a skeleton**: validates 15 windows, clocks, resets and the switchdev port model, then refuses with -EOPNOTSUPP before any register access. Not mergeable. |
| 0013 PCIe | **RFC**: encodes the measured hard access rules (never read the misc window, aligned dwords only) but has never enumerated an endpoint. |
| 0017 selector | **RFC**: new sysfs ABI. The recorded-pair contract is reconstructed from local evidence (docs/SELECTOR-0x21.md: env bootflag selects, the cell records, 0x21 is the project's bench value and never persists) and the store now does the loader-style low-byte read-modify-write; post-boot stability depends on a boot-time recorder in the target image (not this series), and the env re-stamp and the cell's upper 24 bits remain unmeasured. ABI must be reviewed before freezing. |
| 0006 FMC | **RFC**: storage path is critical and its ECC/OOB/quad-enable semantics are unverified. |
| 0011 pinctrl | **RFC**: probe writes nothing and consumer states are unmeasured; applying guessed muxes could break console/NAND. |
| 0018 SMP | **RFC / ATTENDED**: decoded vendor pen/resume/IPI CPU1 release in a new platsmp.c; has never released a CPU on our kernel. Not in the UP-first set; S1 drops the DTS enable-method hunk (UP boot), S2 is a single-variable attended bring-up with recovery armed. Register values are driver data, no new binding. |

## Recommended submission sequence

Do not post this consolidated 18-patch file set as one series.  It is
consolidated here so the whole design can be reviewed.  A real submission
splits by subsystem and by readiness:

1. **RFC first, for direction (arm-soc + Russell King):** 0001-0003.
   Ask whether a new arch/arm/mach-hisilicon directory under ARCH_HISI,
   UP-only, is acceptable, and whether the TEXT_OFFSET change (0003)
   should be a separate generic ARM improvement.  No DTS yet.
2. **Ready-for-review series, small and independent:** 0004 (with 0014
   folded in) -> clk/reset maintainers; 0005 -> watchdog; 0007 -> netdev
   MDIO; 0008 -> netdev PHY; 0012 -> thermal.  Each with its binding
   split out and its Kconfig/Makefile patch.  Send the CRG provider before
   its consumers.
3. **RFC series with an explicit open-work statement:** 0011 pinctrl,
   0006 FMC (only after the v6.19 ESMT chip entry lands), 0013 PCIe,
   0010 restart + 0017 selector (needs the sysupgrade protocol story and the
   target's boot-time pair recorder).
4. **Do not send yet:** 0009 GEMAC, 0015 PIE, 0016 LSW.  They are
   refusing/partial drafts; post as RFC only when asking for design
   direction, clearly marked `[RFC PATCH]`, and never as a working
   Ethernet/switch port.
5. **SMP only after the attended bring-up (0018):** do not include it in
   the UP-first set.  The S1 submission is 0001-0003 without the
   devicetree enable-method (and, upstream, with a single cpu@0 node); the
   patch is RFC until S2 has run on hardware with recovery armed and a
   soak, then post it with the SMP-RE evidence summary.  The DT
   enable-method belongs to the DTS series, not this kernel series.
6. **After the bindings are accepted:** the board DTS to
   arch/arm/boot/dts/hisilicon/ (with the stock partition map and the
   identity warning; the SMP enable-method only after S2), then the
   OpenWrt target PR (target/subtarget, image recipe, sysupgrade metadata,
   board files, device page).

Submission order must be dependency order: platform -> CRG -> consumers.
Patch 0014 should disappear as a separate patch (fold into 0004) because
it fixes code first added in this series; keep it only while the series is
carried as an OpenWrt target patch stack.

## Known review items to fix before sending

1. **Split bindings out of driver patches.** checkpatch warns 14 times
   that DT binding docs/includes should be a separate patch.  One binding
   patch per driver, before its driver patch; the dt-bindings clock header
   of 0004 also goes with the binding.
2. **Add the MAINTAINERS entry** (02-maintainers-proposal.md); checkpatch
   warns 15 times about new files without it (0018 adds platsmp.c).
3. **Mechanical checkpatch cleanups:** wrap the 16 over-long CRG clock
   table lines (112 > 100 columns), add comments to the two spinlock_t
   declarations, fix two continuation-line alignments in luofu-switch.c,
   and expand the LUOFU_MDIO / LUOFU_PHY Kconfig help text to at least
   four lines.
4. **Fold 0014 into 0004** and **regenerate 0008 against mainline** (it
   currently applies with -75/-12 offsets because it was generated against
   an OpenWrt-patched kernel tree).
5. **Naming consistency:** LUOFU_FMC sits in drivers/spi and would be
   expected to be SPI_LUOFU_FMC; decide before the spi maintainers see it.
   The thermal driver file is luofu_tvsensor.c (underscore) while most new
   drivers use hyphens; follow the subsystem preference.
6. **DCO/provenance:** real legal name in Signed-off-by; a short
   provenance paragraph in the cover letter of the first driver series
   (GPL-2.0, derived from reverse engineering of the vendor's GPL
   binaries; vendor GPL source request unanswered).
7. **State the Wi-Fi scope** in every platform submission: vendor-only,
   blocked, not part of the port.
8. **State the loader dependency** (FIP container, 0x80608000,
   TEXT_OFFSET 0x00608000) and that installation is out of scope until a
   first-install path is validated.
9. **SMP (0018) review answers:** why the pen offset (0xc00), the resume
   offset (0x38) and the resume bit numbers are driver data rather than DT
   properties (vendor-private values, reuse of the existing syscon nodes,
   no new binding surface - SMP-RE.md section 5.5); no CPU hotplug
   (`CONFIG_HOTPLUG_CPU` stays off, matching the vendor ops table); and
   the method string is `hisilicon,luofu-smp`, not the vendor's
   `hisilicon,hsan_smp`.

## Where the evidence lives

- This directory: 00-cover-letter.md (this file), 01-commit-messages.md
  (per-patch messages + mapping table), 02-maintainers-proposal.md,
  03-rationale-tree-placement.md, 04-validation.md, README.md,
  commit-messages/, series/, tools/, validation/.
- Recorded validation sessions: opensource/docs/SKELETON-SMOKE.md
  (third to twelfth sessions, image-recipe, A/B, selector-fix and SMP
  sessions), opensource/docs/TARGET-DESIGN.md sections 4-7 and 9-12,
  opensource/docs/SMP-RE.md (SMP decode and staging),
  opensource/target-skeleton/docs/port-notes/*/SUMMARY.md.
- Current live state and the publication boundary:
  opensource/docs/PUBLISH-READINESS.md (NOT READY: vendor .o history
  purge, GPL reply, DCO/licence files still open).  This series does not
  change that status.
