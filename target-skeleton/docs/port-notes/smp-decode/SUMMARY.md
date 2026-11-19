# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 2, 2026-11-19. Supersedes revision 1 (which delivered patch 033); revision 2
adds the W7 hardware result, the SCU-status decode, the stock reference comparison,
patch 034 and the next single-variable boot plan. **Host-only: no device, router,
UART, TFTP, flash or bench tool was touched while producing this package; nothing was
booted by this lane.** The hardware results quoted below were produced by the lead's
attended bench sessions and are cited from the saved console captures.

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/
(033-arm-hisilicon-smp-release-diagnostics.patch, 034-arm-hisilicon-smp-release-probe.patch).
Design/evidence doc: docs/SMP-RE.md, sections 14 (delivered in revision 1, now
integrated) and 15 (this revision's shared.patch).
Local evidence bundle: build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 (032-033 window, still in the series) | target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| **patch 034 (this revision, next attended variant)** | target-skeleton/target/linux/hisilicon/patches-6.18/034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| shared documentation patch (SMP-RE.md section 15) | docs/incoming/smp-decode/shared.patch | 4ead0a0e7c55adb6903eac7567f7d50fe7630526d3d9587afd837d9eb29a3482 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

Patch 034 touches only arch/arm/mach-hisilicon/platsmp.c. No devicetree, config or
binding change.

## 2. Hardware results so far (lead's attended sessions, cited from saved logs)

| run | image | what happened |
| --- | --- | --- |
| 2026-11-19 gate/eps boots | series up to 031 | default 033 predecessor release: pen 0x8070a000, resume 0x02a00000 -> 0x00200000, CPU1 offline |
| 2026-11-19 W7 run | 14791958 (001-033) | command line had luofu_smp_diag=1 luofu_smp_probe=1; the kernel reported them **unknown** (built-in object parameters need the "platsmp." prefix), so diagnostics and the probe stub were OFF. Default release failed again: pen 0x8070a000, resume 0x02a00000 (mask 0x02800000), **SCU CPU status byte of CPU1 = 0x00**, CPU1 offline after the 1 s timeout. Warm reset ("Reset Info: Linux reboot cmd to reset"). |

**Consequence: the pen probe stub has not been run yet.** The W7 "no 1P" observation
carries no information about pen consumption; the run tested the plain default release
only.

## 3. Diagnosis up to this point (evidence-graded)

| claim | grade | basis |
| --- | --- | --- |
| the pen value is correct: it is the physical address of secondary_startup for our image (image base 0x80608000, symbol at _text + 0x102000) | [V] | boot log idmap print, System.map, section layout; see SMP-RE.md 14.2 |
| SCU CPU status 0x00 means NORMAL (powered, not dormant/power-down) | [V] | A9 SCU CPU-status encoding: 0 = normal, 2 = dormant, 3 = powered off; mainline SCU_PM_* defines and readb(scu+0x08+cpu) access (arch/arm/include/asm/smp_scu.h, arch/arm/kernel/smp_scu.c). "luofu_smp_power=1" is therefore a control, not a fix |
| the CRG release result matches the vendor: stock 2-CPU boot leaves CRG + 0x38 = 0x00200000, exactly the value our RMW produces | [V] | read-only stock inventory 2026-11-18 (bench-inventory .merged.tsv) vs our console capture |
| every our-kernel boot in the saved logs is a warm reset; no cold-boot SMP sample exists | [V] | log survey of build/uart-logs/ |
| warm resets do not stop the loader from making CPU1 releasable for stock: our 1-CPU kernel -> panic/watchdog warm reset -> U-Boot -> stock 5.10 with 2 CPUs | [V] | e.g. 20261119T044150Z-uart-boot2.log, 20261119T091941Z-uart-finalboot.log |
| the open question is whether CPU1 consumes the pen at all at our release time (loader did not park it / core held in reset / pen not routed) versus consumes it and dies in the 6.18 secondary entry | [U] | patch 034's un-disableable probe answers this in one boot |

## 4. Patch 034: what it changes

* Knobs are parsed with __setup(), so the **bare documented names** work on a built-in
  object: luofu_smp_probe=0|1, luofu_smp_power=0|1, luofu_smp_allbits=0|1,
  luofu_smp_clear_mask=<hex>, luofu_smp_force_pen=<pa>, luofu_smp_retries=<n>;
  luofu_smp_diag= is accepted as a compatibility no-op (dumps are always on).
* Unconditional register snapshots: at prepare time (including the pen word as left by
  the loader), immediately before the release, and at +1 ms / +21 ms: SYSCTRL
  0xc00..0xc1c, CRG 0x04..0x3c (includes resume 0x38; skips reboot cmd 0x00 and the
  watchdog words), SCU ctrl/config/status plus both CPU status bytes.
* The pen probe is **on by default**: the pen points at a stub that first stores the
  core MPIDR in a page-aligned scratch page (read back by the boot CPU through an
  uncached ioremap alias, so it survives a UART failure), prints the low MPIDR nibble
  and "P" on the console UART, then branches to the real secondary_startup.
  "1P" on the console or marker != 0 in the +1ms/+21ms lines proves the pen was
  consumed. luofu_smp_probe=0 restores the plain entry for the control run.
* Release order and default resume mask are unchanged from 033 (vendor per-core pair,
  two dsb() before the IPI).

Expected console lines for the next boot (addresses are from the validated build; the
log prints the real ones): a "luofu-smp: prep pen ... stub 81195000 marker 81194000"
line, a "luofu-smp: prep pen value ..." line (loader-left pen), three sysctrl lines,
three crg lines and three scu lines, then "release cpu=1 ..." and the +1ms/+21ms lines
with "consumed=yes|no".

## 5. Stock read plan (read-only, root telnet + /dev/mem)

/devmem is present at /sbin/devmem (health check devmem-capability PASS on 2026-11-18)
and /dev/mem is readable. **Read-only only, no writes.** Run twice and record
boot_id/uptime:

    for a in 0x10180000 0x10180004 0x10180008 \
             0x10100c00 0x10100c04 0x10100c08 0x10100c0c 0x10100c10 0x10100c14 \
             0x14880004 0x14880008 0x1488000c 0x14880010 0x14880018 0x1488001c \
             0x14880038 0x1488003c ; do
        printf '%s ' "$a"; /sbin/devmem "$a" 32; done

Fallback if devmem is missing:
dd if=/dev/mem bs=4 skip=$((0xADDR/4)) count=1 2>/dev/null | od -An -tx4

Already known from the 2026-11-18 stock inventory (do not need re-reading): SYSCTRL
0xc00 = 0x80609680, 0xc08 = 0x00000010, 0xc10 = 0x82000000; CRG 0x14 = 0x01360ef1,
0x20 = 0x02f8300b, 0x2c = 0x0d300e01, 0x30 = 0xfe7fe05b, 0x34 = 0x000ff309,
**0x38 = 0x00200000**, 0x90 = 0x6a010008, 0x100 = 0x00000000, 0x138 = 0x00018042.
The new stock sample is needed for the three SCU words and the SYSCTRL/CRG neighbours
the inventory did not cover, so it can be compared like-for-like with the 034 dump.
Stock /dev/mem can only show the post-boot state; the loader-time CPU1 state remains
[U] and is what the in-kernel dump targets.

## 6. Next single-variable boot plan

1. Build an **001-034** image from the bench clone (no command-line change required:
   dumps are unconditional and the probe defaults on; removing "luofu_smp_diag=1" and
   keeping "luofu_smp_probe=1" also works). Warm boot, capture UART read-only.
2. One boot decides the branch:
   * "1P" and/or marker != 0 -> CPU1 consumed the pen. If CPU1 is online, the next
     variable is the direct entry (luofu_smp_probe=0); if CPU1 is still offline, CPU1
     dies in the 6.18 secondary entry after the stub and the next patch instruments
     __secondary_switched.
   * no marker and +21ms pen still equals the written value -> CPU1 did not consume
     the pen. Then, one variable per boot:
     a. cold power-on of the same image (reset-type variable; no cold SMP sample exists);
     b. luofu_smp_power=1 (control; expected no-op, SCU status is normal);
     c. luofu_smp_clear_mask=0x02a00000 then 0x03c00000 (CRG bit experiments, only
        with the stock CRG read from section 5 in hand).
3. If the cold boot also shows no consumption: compare the 034 dumps with the stock
   reference, then move the pen write earlier in our boot (early_initcall probe with a
   spin-only stub and marker) to test whether our own early init stops CPU1 before the
   normal release point. That is patch 035 work.

## 7. Validation of patch 034 (host-only, isolated clone)

The validation window is the series 001-034 in build/linux-kbuild/waves/2026-11-19-smp-fix/,
a btrfs-reflink copy of /home/shibbityshwab/openwrt-lab-smp (openwrt main 27663a0,
linux-6.18.60, tree's own OpenWrt GCC 14.4.0). Shared clones were not modified; no
device was touched.

| check | result |
| --- | --- |
| pristine 6.18.60 series apply 001-034 | patch -p1 --no-backup-if-mismatch -F0 in a fresh pristine tree: RC=0, 112 files, no .rej/.orig; only the four pre-existing offset hunks (008 x2, 021, 028); 034 applies at its exact line numbers; resulting arch/arm/mach-hisilicon/platsmp.c sha256 2431ba4f... identical to the delivered after-file |
| OpenWrt kernel prepare 001-034 | make target/linux/prepare V=s: RC=0 (1,245 patching-file lines, no .rej/.orig) |
| full kernel compile 001-034 | make target/linux/compile V=s -j16: **RC=0**; zImage 3,286,680 B sha256 ab268d03505c1f40ca461464080959a3092757b76b14c9835b7943dbb1426f13; vmlinux 25e4807fb3dbd849943eb2ccefc10e6782627132afe7758c50f059c8788c16d3; 0 warnings attributable to new/changed files |
| built tree identity | arch/arm/mach-hisilicon/platsmp.c in the built tree sha256 2431ba4f... = delivered patch content |
| layout / entry pin | System.map: _text = stext = 0xc0608000, secondary_startup = 0xc070a000 (pen 0x8070a000 on the 0x80608000 image base); probe marker c1194000 (page aligned), stub c1195000 |
| parameter plumbing | vmlinux .init.setup carries the bare strings luofu_smp_probe=, luofu_smp_power=, luofu_smp_allbits=, luofu_smp_clear_mask=, luofu_smp_force_pen=, luofu_smp_retries=, luofu_smp_diag= (the W7 failure mode cannot recur) |
| probe stub encoding | generated instructions compared word-for-word with the project ARM assembler for a sample marker address: all 13 instructions + marker literal match, including the movw/movt split for the marker address (one bug was found and fixed during this lane: movt must use marker >> 16, 0xe3484119 for 0x8119) |
| checkpatch | patch-mode on 034: only the expected missing commit-description/Signed-off-by metadata plus pr_cont warnings (deliberate: compact register dumps); file-mode 0 errors / 0 warnings |
| shared.patch v2 | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root; it appends SMP-RE.md section 15 to the already-integrated section 14 |

The earlier 001-033 validation (revision 1) remains valid and is superseded only by
this window; 033 itself is unchanged.

## 8. What is NOT claimed / remaining unknowns

* Patch 034 has not run on hardware; it is compile-validated only.
* The probe may still be defeated if CPU1 reaches the stub but both the marker write
  and the UART write fail; the marker page was chosen to make that very unlikely.
* The loader-time CPU1 state cannot be observed from stock userspace [U]; the 034
  in-kernel dump is the first direct look at our boot.
* Physical semantics of CRG + 0x38 bits 0x16..0x19 remain [U] (vendor name "resume"
  only); the stock sample in section 5 plus the 034 dump narrow it.
* The physical meaning of the SCU config register bits beyond [1:0] (core count) is
  [U] in this document; the raw word is printed for the record.
* The review-form submission series (opensource/upstream/series-6.18/) is not updated
  by this lane.

## 9. Integration notes

1. Patch 034 is installed in the target tree patches-6.18/ directory (033 is
   unchanged). No DTS/config edit is needed; the bench image can be built from
   001-034 directly.
2. Apply the shared documentation patch from the opensource/ root:

       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch

   It appends section 15 to docs/SMP-RE.md and changes nothing else.
3. For the next bench image: keep the current baked command line (optionally drop
   "luofu_smp_diag=1 luofu_smp_probe=1" - the probe now defaults on and the old diag
   name is a compatibility no-op, so either way works).
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply tree, clean
   prepare/compile logs, patch sources, System.map excerpts, stub assembler checks,
   EVIDENCE-SHA256SUMS.txt).
