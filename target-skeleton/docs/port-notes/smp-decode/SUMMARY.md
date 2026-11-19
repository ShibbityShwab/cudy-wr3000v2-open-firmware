# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Date: 2026-11-19. **Host-only. No device, router, UART, TFTP, flash or bench tool
was touched; nothing was booted; no SMP claim is made.**

This directory is the incoming package for the follow-up of the SMP lane (task-32,
patch 018). The kernel-side implementation lives in
target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch,
the evidence/design document is docs/SMP-RE.md (section 14 is added by this package's
shared.patch), and the local-only evidence bundle is
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

This lane follows the 2026-11-19 bench result: patch 018's release ran on the board,
CPU1 stayed offline ("CPU1: failed to come online"), and patches 028/029 added the
diagnostics that pinned the pen write and the CRG resume-bit read-modify-write. The
task was to diagnose that failure offline and to deliver a corrected
diagnostic/release patch (033), compile-validated with the series.

## 1. What was delivered

| artifact | path | sha256 |
| --- | --- | --- |
| diagnostic/release patch 033 | target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| incoming shared patch (SMP-RE.md section 14) | docs/incoming/smp-decode/shared.patch (this directory) | 2db8251aeb4a53f9c3e7e18936db1693e5575b0c0842477d14b10b0fa5c3919c |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | see EVIDENCE-SHA256SUMS.txt there |

Patch 033 touches only arch/arm/mach-hisilicon/platsmp.c (the file created by 018 and
modified by 028/029). No devicetree change, no config change, no new binding.

## 2. Diagnosis (evidence-graded)

**The decoded release sequence is a faithful port and the pen value is the correct
physical address of secondary_startup for our image; the failure is downstream of the
register writes.** Details, all host-side:

| claim | grade | basis |
| --- | --- | --- |
| the release ran on hardware: cpu 1 / logical 1, pen written 0x8070a000, CRG resume word 0x02a00000 -> 0x00200000, IPI call reached, CPU1 never online | [V] | build/evidence-bench-gates/2026-11-19/uart-eps-run.log (patch 028 line) and the 1 s timeout gap; the same result with patch 029 clearing all four bits |
| the decompressed kernel base is 0x80608000, so secondary_startup physically is 0x8070a000 | [V] | boot log "Setting up static identity map for 0x80700000 - 0x8070003c" for __idmap_text_start 0xc0700000 => runtime PHYS_OFFSET 0x80000000 (16 MiB aligned, the head.S pv gate) and __pa(_text) = 0x80608000; System.map: _text = stext = 0xc0608000, secondary_startup = 0xc070a000 (= _text + 0x102000) |
| the +0x102000 offset is a layout property, not a relocation bug: CONFIG_STRICT_KERNEL_RWX aligns .text to a 1 MiB section boundary after the 0x290-byte .head.text, while the vendor 5.10 kernel has secondary_startup at _text + 0x1680 (its pen 0x80609680 with vendor PHYS_OFFSET 0x80600000) | [V] | built vmlinux section headers (.head.text 0xc0608000 size 0x290, .text 0xc0700000); vendor kallsyms (_text 0xc0008000, secondary_startup 0xc0009680); vendor hsan_boot_secondary disassembly (0xc0009680 - 0xc0000000 + 0x80600000 = 0x80609680) |
| the pen write sticks and is read back exactly as __pa_symbol(secondary_startup) | [V] | patch 028 diagnostic line |
| the CRG resume word starts at 0x02a00000 (CPU1 bits 0x17/0x19 set); the RMW clears the vendor's per-core pair (029 also tried all four bits) | [V] measured value; [I] that the vendor method would see the same bootloader state |
| the stock U-Boot carries the pen consumer: its secondary-core start path polls SYSCTRL + 0xc00 (ldr/cmp/beq) with MMU and caches off and on a non-zero value does "mov pc, r7"; the boot core path clears the pen, writes CRG + 0x50 (watchdog set word) and disables/invalidates/re-enables the SCU at 0x10180000 | [V] | host disassembly of dumps/router-20261108/uboota.bin (pen literal at file offset 0x5e0, referenced from the early start code) |
| the pen value itself is not the defect | [X] ruled out | [V] above: the value is the symbol's physical address for an image base independently confirmed by the idmap print |
| the vendor method contains no cache maintenance, no SMC/HVC and no register write beyond pen + resume bits + IPI | [V] | SMP-RE.md section 4 |
| the remaining failure is either (a) CPU1 never consumes the pen (not parked in the loader poll loop at release time / held in reset or power-down / wake-up not delivered) or (b) CPU1 consumes the pen and dies in the 6.18 secondary entry | [U] | no offline artifact can distinguish; patch 033's probe stub makes it a one-boot measurement |

The two 2026-11-19 bench variants (per-core bits from 028, then all four bits from 029)
both failed, which is why 033 returns the default release to the decoded vendor
per-core behaviour and keeps the all-four-bits variant only as an opt-in experiment.

## 3. What patch 033 does

Default behaviour (no parameters) is the decoded vendor release, now with explicit
ordering: pen write -> dsb -> CRG read-modify-write of the target core's two resume
bits -> dsb -> IPI_WAKEUP -> sev. The write barriers are the one mechanical difference
from the 028/029 code (writel_relaxed -> writel + explicit dsb); the vendor's writel()
already implied that ordering.

Attended-only instrumentation, all off by default, all documented in the file header:

| parameter | effect |
| --- | --- |
| luofu_smp_diag=1 | prints SCU CBAR/CBCtrl/CBcpu-status at prepare time, the physical pen/resume/entry addresses, then re-reads pen/resume/SCU 1 ms and 21 ms after the IPI |
| luofu_smp_probe=1 | pen points at a boot-built position-independent stub that prints the low MPIDR nibble and "P" on the console UART and then branches to the real secondary_startup; "1P" proves CPU1 consumed the pen |
| luofu_smp_power=1 | sets the target core's SCU power status to NORMAL before the release |
| luofu_smp_allbits=1 | the pre-033 all-four-bits resume-bit variant |
| luofu_smp_force_pen=<pa> | bench-only pen override, no rebuild needed for an address experiment |
| luofu_smp_retries=N | repeat pen + resume + IPI up to N times (1 ms apart) |

The probe stub is verified instruction-by-instruction against the project's ARM
assembler (see the lane logs); it uses only ARMv7 movw/movt and the CP15 MPIDR read,
and it clobbers only r0-r3.

## 4. Validation (host-only, isolated clone)

The validation window is the series as it stands at 033 (032 is the queue-manager
patch from the parallel lane, included in the same window). All of this ran in
build/linux-kbuild/waves/2026-11-19-smp-fix/, a btrfs-reflink copy of
/home/shibbityshwab/openwrt-lab-smp (openwrt main 27663a0, linux-6.18.60, the tree's
own OpenWrt GCC 14.4.0). The shared clones were not modified; no device was touched.

| check | result |
| --- | --- |
| pristine 6.18.60 series apply 001-033 | patch -p1 -F0 in a fresh pristine tree: RC=0, 111 files, no .rej; only the four pre-existing offset hunks (008 x2, 021, 028); 033 applies at its exact line numbers; the resulting arch/arm/mach-hisilicon/platsmp.c is byte-identical to the delivered after-file (sha256 d8fedb72...) |
| OpenWrt kernel prepare, series 001-033 | make target/linux/prepare V=s in the isolated clone: RC=0 (1,244+ patching-file lines including the target's generic patches; no .rej/.orig) |
| full kernel compile | make target/linux/compile V=s -j16: RC=0, zImage and vmlinux hashes in section 5 |
| patch 033 object compile | arch/arm/mach-hisilicon/platsmp.o compiles clean with the tree's GCC 14.4.0; zero warnings attributable to the file in the full build |
| probe stub encoding | assembled with the project ARM toolchain and compared word-for-word with the runtime stub array: all 13 instructions + the pen literal match (the one correction made during this lane was the movw imm4/imm12 split for movw r0, #0xf000: 0xe30f0000, not 0xe3000f00) |
| checkpatch | patch-mode on 033: 0 style errors and 0 style warnings, 1 CHECK (udelay in the diag-only path); the only ERROR/WARNING are the expected "Missing Signed-off-by"/"Missing commit description" metadata of the working patch format (same as every other patch in this series); platsmp.c file-mode: 0 errors / 0 warnings |
| shared.patch | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root |
| no shared doc edited in place | opensource/docs/SMP-RE.md is unchanged in the repo; section 14 ships only as shared.patch |

## 5. Build result

## 5. Build result (final clean run, patch 033 as delivered)

The clean run re-extracted the kernel, applied the series (prepare RC=0, 1,244
patching-file lines, no .rej/.orig) and compiled it to completion:

| item | value |
| --- | --- |
| make target/linux/compile V=s -j16 | **RC=0**, compile step real 125.6 s (user 1,306.8 s) |
| zImage | 3,286,776 B, sha256 e9ca1b2c381a432e0c2e022d3fee405a67eec011f4f8e4cfc36bcd2f095ed803 |
| vmlinux | 57,103,060 B, sha256 33e4d35f32f892e98ce185cf86ded422c05b51aa7df269e54a6566323cb7fc09 |
| compiler warnings attributable to new/changed files | 0 |
| built tree identity | arch/arm/mach-hisilicon/platsmp.c sha256 d8fedb722d93f39adb31a33e4c31ec06a8f4ee236cb2ab300dd631962a2f7f67 - byte-identical to the delivered after-file, i.e. the build is of the exact patch 033 content |
| layout pin | vmlinux sections: .head.text 0xc0608000 size 0x290, .text 0xc0700000 (1 MiB aligned; CONFIG_STRICT_KERNEL_RWX=y); System.map: _text = stext = 0xc0608000, secondary_startup = 0xc070a000 => pen value 0x8070a000 on the 0x80608000 image base |
| TEXT_OFFSET / SMP config | arch/arm/Makefile textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000 unchanged; CONFIG_SMP=y, CONFIG_HAVE_ARM_SCU=y, CONFIG_HAVE_ARM_TWD=y, CONFIG_ARM_PATCH_PHYS_VIRT=y, CONFIG_AUTO_ZRELADDR=y, CONFIG_STRICT_KERNEL_RWX=y, CONFIG_THUMB2_KERNEL not set; no config change needed |
| runtime stub words | the built platsmp.o contains the expected stub words e30f0000 / e3410010 / e5d01014 / 0afffffc (disassembly check), matching the assembler verification |
| bench parameters wired | System.map carries __param_str_luofu_smp_{diag,probe,power,allbits,force_pen,retries}, so the built-in kernel parameters really exist; the runtime stub buffer is luofu_smp_probe_stub in .bss (c1193400) |

The earlier run of the same window (before the one-instruction movw fix in the probe
stub) also built RC=0 with zImage sha256 0e323bea1eb47da7fd50c45241c9fb28c7df3b9c66078f26f132d32c083739dc;
the clean run above is the final evidence and supersedes it.

## 6. Attended bench sequence (next session, human at the bench)

The box currently runs the EPS gate image (console-only). After a verified install of
a 033 image, boot with the diagnostic command line, capture the UART read-only, and
read the result:

1. baseline with diagnostics only: luofu_smp_diag=1
   expected extra lines: prepare-time SCU CBAR/CTRL/CONFIG/CPU-status, the physical
   pen/resume addresses, then "+1ms"/"+21ms" SCU status after the release. This
   records the SCU power state of CPU1 for the first time.
2. probe run: luofu_smp_diag=1 luofu_smp_probe=1 - decide with the table below.
3. only if step 2 shows CPU1 consuming the pen but not coming online: add
   luofu_smp_retries=3 for a longer window and then treat the 6.18 entry path as the
   suspect (instrument __secondary_switched next).
4. only if step 2 shows no CPU1 activity and the SCU status stays dormant/power-down:
   luofu_smp_power=1, then stop and go back to the vendor evidence for the CRG
   power/clock gate (measure the CRG page on the stock boot before writing anything
   else).

Decision table:

| observation | conclusion | next action |
| --- | --- | --- |
| "1P" and CPU1 online | the entry works via the stub | compare the stub's cache/state with the direct entry, fold the minimal difference into the release |
| "1P" and CPU1 offline | CPU1 consumes the pen and dies in the secondary entry | instrument __secondary_switched (second stub) and compare with the vendor 5.10 entry; the +0x102000 layout is the next suspect |
| no output, SCU status changes | the core left its old power state but did not run the stub | inspect CRG resume-bit semantics (reset vs clock/power) and retry with power/retries |
| no output, SCU status dormant/power-down | the decoded bits do not power/release the core | measure the CRG page on a stock boot, find the per-core power/clock gate, then gated-write only that |
| no output, SCU status normal | the core is powered but not polling the pen on this boot path | check whether the loader's secondary stub is entered (cold vs warm reset) and whether the IPI/SEV is the only lever left |

Recovery: the launch is a normal A/B pair install with the verified B-pair path; the
recovery door must be armed before the boot (no kernel-side change affects recovery).

## 7. What is NOT claimed / remaining unknowns

* No hardware behaviour of patch 033 and no SMP claim: it is compile-validated only.
* SCU CPU-status and CBAR have never been read on this board [U]; the SCU-enable path
  is believed to be a no-op because the decoded stock U-Boot enables the SCU first [I].
* The physical semantics of CRG + 0x38 bits 0x16..0x19 are still [U] (vendor name
  "resume" only); whether they gate reset, clock or power is what the SCU status read
  will narrow.
* Whether our kernel's secondary_startup runs correctly when CPU1 reaches it is [U]
  for this board until the probe stub reports; the entry itself is mainline ARM and
  the same page tables are exercised by the boot CPU.
* The all-four-bits variant (patch 029) is kept only as a parameter; it did not fix
  the bench failure.
* The review-form submission series (opensource/upstream/series-6.18/) is not updated
  by this lane.

## 8. Integration notes

1. Patch 033 is already installed in the target tree's patches-6.18/ directory (it is
   the only source-tree change of this lane). No DTS/config edit is needed.
2. Apply the shared documentation patch from the opensource/ root:

       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch

   It appends section 14 ("Bench update (2026-11-19): the release runs, CPU1 still
   fails; patch 033") to docs/SMP-RE.md and changes nothing else.
3. The evidence bundle is local-only and gitignored; it contains the pristine apply
   tree and logs, the isolated OpenWrt clone's prepare/compile logs, the patch and
   doc sources, and EVIDENCE-SHA256SUMS.txt.
4. For the bench image, add luofu_smp_diag=1 (and luofu_smp_probe=1 for the
   discriminating run) to the clone's baked command line; the repository default
   stays parameter-free and therefore vendor-faithful.
