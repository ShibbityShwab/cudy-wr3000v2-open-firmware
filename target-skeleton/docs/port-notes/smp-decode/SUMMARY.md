# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 7, 2026-11-20. Supersedes revision 6. New: the stock read-only reference
(SCU config 0x531 vs our 0x511, GIC reference values, rstinfo third sample) and patch
042 - the SCU-config single-variable knob plus GIC reference dumps that match the
stock read. **Host-only: no device access by this lane; hardware results are the
lead's attended runs and stock telnet reads, cited from the saved logs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037, 039, 042). Design/evidence doc: docs/SMP-RE.md sections 14-20 (this revision's
shared.patch appends section 20). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | .../033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | .../034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | .../035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| patch 037 | .../037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| patch 039 | .../039-arm-hisilicon-smp-early-safe.patch | 37b7932b0c240d612582090ed17c5c91241f8bdfed074bdd0e7bafb13dd43a32 |
| **patch 042 (new)** | .../042-arm-hisilicon-smp-scu-gic.patch | dc8511edb95e4b788fc1c7d7934a23c6eb9e9160d4b538ef40333db550fa673a |
| shared documentation patch (SMP-RE.md section 20) | docs/incoming/smp-decode/shared.patch | 1979e04255b915f1184f5f561eb6cebfbc2c56ab29d308529078c91d19db0dc1 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

Numbering note: the SMP patch was requested in the "040" window; the parallel net lane
filed 040-net-gmac-w9-diag.patch and 041-net-gmac-macconf-nodoorbell.patch at the
same time, so the SMP patch is 042. As of this writing those two net patches do not
apply cleanly on a fresh 001-039 series (040 fails at hunks 1 and 3 of
drivers/net/ethernet/hisilicon/luofu-gmac.c), so the validation window of this
revision is 001-039 + 042 (net 040/041 skipped; net lane to fix).

## 2. Stock read-only reference [V]

Stock 5.10 boot, boot_id 72c87547-70e5-4882-8117-a128eac71ad5, two identical rounds
(build/evidence-bench-gates/2026-11-20-stock-reads.txt):

* SCU ctrl 3, config **0x531**, status 0x03030000;
* GICD_CTLR 1, GICD_TYPER 0xFC25, GICD_IIDR 0x0102043B, SGIR 0, GICC CPU0 CTLR 1,
  GICC CPU1 window read from CPU0 = 0 (banked/RAZ, no information);
* SYSCTRL c00 0x80609680 (vendor runtime pen), c04 0, c08 0x10, c0c 0, c10
  0x0A000000, c14 0xDFDF0781;
* CRG 0x38 0x00200000, 0x3c 0.

Interpretation:

* GICC CPU1 = 0 is **not meaningful**: the A9 GIC CPU interface is banked per CPU and
  a CPU can only read its own bank; stock and our kernel both read zero there. CPU1
  enables its own interface only when it starts, so the vendor release cannot depend
  on it.
* SCU config **0x531 vs our 0x511** is a real difference in bit 5. The semantics of
  the bit above the core-count field are not decoded [U]; the lead's "standby enable"
  reading is plausible but unverified. U-Boot is shared by both boot paths and our
  earliest read (prepare, 0.13 s) already shows 0x511, so [I] the vendor kernel sets
  bit 5 in its own early init; our port never writes the SCU Configuration Register
  (scu_enable() returns early because ctrl bit 0 is already set). It cannot gate a
  core that is already powered (SCU status NORMAL), but it is the first
  non-vendor-equivalent SCU state found, so patch 042 exposes it as a single variable.
* GICD_CTLR = 1 / SGIR = 0 / GICC CPU0 = 1 is a normal enabled distributor; patch 042
  prints the same fields from our kernel for a direct diff.
* The stock pen value is the vendor kernel's runtime write and stays intact - the
  consumer does not clear the pen - so it says nothing about consumption.
* rstinfo 0x0A000000 is a third sample (ours 0x42000000 warm, 0x02000000 cold); the
  field varies with the boot path and is not decoded [U].
* CRG resume 0x00200000 equals the value our release produces; the CRG side remains
  vendor-equivalent.

## 3. Patch 042

platsmp.c only, keeps all 039 behaviour:

* **luofu_smp_scu_cfg=<hex>**: OR the mask into the SCU Configuration Register
  (0x10180004) at prepare time, before the release, with before/after/readback
  logging. "luofu_smp_scu_cfg=0x20" is the single-variable test for the stock bit 5;
  the standard SCU dumps then show the configuration the release ran with.
* GIC reference dumps at prepare and after the release, covering the same registers
  as the stock read: GICD_CTLR, ISENABLER0/1, ISPENDR0, ISACTIVER0, ITARGETSR0,
  ICFGR0/1, IPRIORITYR0, GICC PMR/BPR (CPU0 bank) and the labelled CPU1 banked view.

## 4. Additional stock reads requested (while stock is reachable)

Priority 1 (safe): GICD 0x10181100 0x10181104 0x10181108 0x10181200 0x10181300
0x10181400 0x10181800 0x10181c00 0x10181c04 and GICC CPU0 0x10180104 0x10180108
(SGI/PPI enables, pending/active, SGI targets, SGI edge config, SGI priorities,
CPU0 PMR/BPR - the reference our GIC driver must match for the wakeup SGI 0);
the 16 KiB flashinfo window 0x80600000 via
"dd if=/dev/mem bs=4096 skip=$((0x80600000/4096)) count=4 | od -Ax -v -tx4"; one word
per 4 KiB page of 0x80500000-0x805fffff; /proc/interrupts and /proc/softirqs (SGI/IPI
delivery reference); SYSCTRL 0x10100c18 0x10100c1c and the full CRG window
0x14880000-0x1488003c.

Priority 2 (riskier, decisive if readable): the decoded U-Boot CPU1 poll-loop
candidate in the loader region - 0x80040000 and 0x800405c0-0x80040600. This address is
outside DT RAM; if /dev/mem refuses it or the read faults the stock kernel may oops,
but the loader recovers. If it reads, it proves where the loader park loop lives and
whether the decoded instructions are there at runtime.

Also: record how the stock boot was entered (fallback after our crash?) for the
rstinfo 0x0A000000 sample, and if a known cold power-on stock boot is ever available,
repeat the c10 / SCU-config reads.

## 5. Next single variables

1. 001-039 image: luofu_smp_early_probe=2 (plan unchanged), then 3, then 4.
2. 001-042 image, separate session/later image: luofu_smp_scu_cfg=0x20 as a single
   variable (do not combine with the early-probe modes).
3. If all silent: the stock reads in section 4 plus the loader decode of how CPU1 is
   started.

## 6. Validation of 042 (host-only, isolated clone)

| check | result |
| --- | --- |
| pristine 6.18.60 apply of 001-039 + 042 (net 040/041 skipped, see numbering note) | patch -p1 -F0: RC=0, 120 files, no .rej/.orig; only the four known offset hunks (008 x2, 021, 028); resulting platsmp.c sha256 0c728162... identical to the delivered file |
| kernel prepare | RC=0 (1,255 patching-file lines) |
| full kernel compile | RC=0 (real 147.8 s); zImage 3,290,208 B sha256 d8abb8dc56b912d0972c1d94c7d6ab0ce0f423fb415d20173a35056c5cb98cb5; vmlinux sha256 c30554b78525d5f36a44e9ad30971897f852ee45ca370c9bc2a5d17fe64eb5ce; 0 warnings |
| built tree identity | platsmp.c sha256 0c728162... = delivered patch content |
| symbols | _text = 0xc0608000, secondary_startup = 0xc070a000; early marker 0xc1293400, early stub 0xc1293440, probe stub 0xc1295000 |
| setup strings | luofu_smp_early_probe, luofu_smp_scu_cfg, luofu_smp_reserve_low1m present in vmlinux |
| checkpatch | patch-mode: only the expected missing commit-description/Signed-off-by metadata plus compact-dump pr_cont notes |
| shared.patch v8 | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root (appends section 20 on top of the integrated sections 14-19) |

## 7. Remaining unknowns

* Whether CPU1 is held in reset/dormant or not polling the pen at all (early modes
  2/3/4).
* Whether the SCU config bit 5 matters (luofu_smp_scu_cfg=0x20).
* rstinfo encoding (0x02/0x42/0x0A samples) [U]; SCU config bit 5 semantics [U];
  physical semantics of CRG + 0x38 bits [U].
* Whether the 6.18 secondary entry works when CPU1 reaches it [U].
* W9 net TX oops: faulting function not captured (net lane).

## 8. Integration notes

1. Patch 042 is installed in patches-6.18/; 033/034/035/037/039 unchanged. No
   DTS/config change needed for the diagnostic image.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 20 to docs/SMP-RE.md and changes nothing else.
3. Next bench tokens: early runs unchanged (luofu_smp_early_probe=2 on 001-039);
   the SCU-config test is luofu_smp_scu_cfg=0x20 on an 001-042 image, alone.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply, clean
   prepare/compile logs, patch sources, System.map excerpts, early-stub assembler
   checks, W9 capture copies, stock reads copy, EVIDENCE-SHA256SUMS.txt).
