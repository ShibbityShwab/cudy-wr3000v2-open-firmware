# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 6, 2026-11-20. Supersedes revision 5. New: the W9 log reconciliation (the
"2 CPUs" sighting is the vendor 5.10 fallback, not our 6.18 kernel; no 6.18 boot of
this lane has ever brought up CPU1), the traffic-correlated crash evidence, and patch
039 - the safe early-probe variant (modes 1-5, corrected wakeup-IPI target, early
marker word 1, skip-release guard, GIC state dumps). **Host-only: no device access by
this lane; hardware results are the lead's attended runs, cited from the saved
console logs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037, 039). Design/evidence doc: docs/SMP-RE.md sections 14-19 (this revision's
shared.patch appends section 19). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | .../033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | .../034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | .../035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| patch 037 | .../037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| **patch 039 (new)** | .../039-arm-hisilicon-smp-early-safe.patch | 37b7932b0c240d612582090ed17c5c91241f8bdfed074bdd0e7bafb13dd43a32 |
| shared documentation patch (SMP-RE.md section 19) | docs/incoming/smp-decode/shared.patch | 3dd9bb0120f7b70a6a54c10c6a6fc2c8eccba707b50e93e844dd08077c4c9ae9 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

## 2. W9 window reconciliation [V]

Two W9 images were booted: a6b57361 (001-038 + luofu_smp_early_probe=1) and 45d248ff
(001-038, no early probe); both reset after traffic and the loader fell back to the
stock 5.10 image.

* **No 6.18 boot of this lane has ever brought up CPU1.** The early-probe boot's own
  lines in the raw capture are
  "[1.530358] CPU1: failed to come online", "[1.534218] smp: Brought up 1 node, 1 CPU",
  "[1.538264] SMP: Total of 1 processors activated (1993.93 BogoMIPS)".
* Every "Brought up 1 node, 2 CPUs" / "CPU1: thread -1 ... mpidr 80000001" sighting in
  the W9 captures belongs to a **stock 5.10.201** boot segment (vendor timeline:
  CPU1 thread 0.1803-0.1805 s, brought-up 0.1912-0.1913 s, 3987.86 BogoMIPS; the
  vendor segment also carries its own "Linux version 5.10.201 (jenkins@cudyrd1)").
  Our kernel's SMP release starts at 0.36-0.37 s because the 037 dumps run first, so a
  0.19 s CPU1 line cannot be ours.
* Therefore consumed=no together with "2 CPUs" is not a contradiction - it was the
  wrong boot segment. The earlier reading "the early probe enabled the normal release"
  is withdrawn.

## 3. What early_probe=1 actually tested (and the next single variables)

Mode 1 writes only the pen (early-stub address); it does not clear the CRG resume bits
and does not send the wakeup IPI. If CPU1 is held in reset or dormant (not executing
the loader's poll loop), a pen write alone cannot release it - so marker=0 is the
expected result and does not prove the consumer is gone. The decoded order is
pen -> resume bits -> wakeup IPI.

Patch 039 therefore provides the complete single-variable ladder before the first
allocation:

| mode | behaviour |
| --- | --- |
| 1 | pen only (same as 037) |
| 2 | pen + CRG resume-bit clear (**next run: luofu_smp_early_probe=2**) |
| 3 | pen + CRG clear + wakeup IPI; GICD_SGIR value fixed to 0x00020000 (target list CPU1, SGI 0). 037 wrote 0x02000000 = target-list-filter "only the requesting CPU" = CPU0 self-IPI |
| 4 | pen + CRG clear, bounded window, then restore pen and resume words for the normal release (hand-back mode); if consumed, the normal release is skipped |
| 5 | read-only early snapshot, no writes (safe control) |

Order to run: **2**, then 3, then 4; mode 5 as the control. If all are silent, the
loader/ROM is not polling or not releasing CPU1 on this boot path; then the stock
reads (SMP-RE 17.6 + 19.6) and a loader-side decode of how CPU1 is started are next.

## 4. Crash evidence [V]

The two W9 resets correlate with traffic, not with the early probe: the clean 1-CPU
image (no early probe) also reset after traffic. In the early-probe boot the kernel ran
to userspace as 1 CPU and oopsed at 7.4336 s, immediately after two pie-trace GEMAC
eth2 transmits (len=90, then len=1043):

    Internal error: Oops: 17 [#2] SMP ARM
    CPU: 0 UID: 0 PID: 497 Comm: netmsg Not tainted 6.18.60 #0 NONE
    Unable to handle kernel NULL pointer dereference at virtual address 00000008 when read

The capture cuts to the loader fallback right at the oops, so the register dump and
call trace are missing; the faulting function is not captured. The oops is real
(6.18.60) but the context is the W9 net TX path, not SMP (one CPU, no early-stub
consumption, clean image resets the same way). Net lane should reproduce with the
pie-trace lines and capture the full oops.

## 5. Patch 039 details

* modes 1-5 as in the table above, all from the same early_param handler inside
  setup_arch(), before memblock/paging init, using early_ioremap() and fixed SoC
  physical addresses (no DT dependency);
* the early stub now writes marker word 1 = 0x4545 after the UART 'E' print, so
  "never ran" and "ran past the UART poll" are distinguishable; the early markers are
  printed at the end of the early handler, at prepare, at +1ms/+21ms and at
  late_initcall;
* if a mode 1-4 marker shows consumption, boot_secondary skips the normal release and
  returns -EIO (no double release, no conflicting vendor sequence);
* GIC distributor and CPU-interface words are dumped at prepare and after the release
  (GICD_CTLR, GICD_SGIR, GICC_CTLR of CPU0 and CPU1): the wakeup IPI can only reach
  CPU1 if the distributor and the target CPU interface are enabled;
* everything from 037 is kept (loader-window dumps, park-loop scan, memblock dumps,
  the 035 probe stubs and knobs).

## 6. Stock read-only measurement plan (root telnet, no writes)

/devmem exists at /sbin/devmem; /dev/mem readable. On the stock 5.10 boot (2 CPUs):

1. SCU + SYSCTRL + CRG reference:
       for a in 0x10180000 0x10180004 0x10180008 0x10100c00 0x10100c04 \
                0x10100c08 0x10100c0c 0x10100c10 0x10100c14 0x14880038 0x1488003c; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done
2. GIC reference (new):
       for a in 0x10181000 0x10181004 0x10181008 0x10181f00 0x10180100 0x10180200; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done
   Expected: GICD_CTLR bit0 = 1, GICD_SGIR = 0, GICC_CTLR (CPU0 and CPU1) bit0 = 1.
3. Loader windows (preserved on stock): 0x80500000-0x805fffff page sampling and the
   0x80600000-0x80604000 dump as in SMP-RE 17.6; the 0x805xxxxx range is only a
   memory-test pattern on our side, and the low 1 MiB reservation did not revive CPU1.
4. Repeat (1) after a known cold power-on and a known warm reboot for the rstinfo
   correlation (ours: 0x02000000 cold / 0x42000000 warm).

## 7. Validation of 039 (host-only, isolated clone)

| check | result |
| --- | --- |
| pristine 6.18.60 apply of the current series 001-039 | patch -p1 -F0: RC=0, 121 files, no .rej/.orig; only the four known offset hunks (008 x2, 021, 028); resulting platsmp.c sha256 99343778... identical to the delivered file |
| kernel prepare 001-039 | RC=0 (1,254 patching-file lines) |
| full kernel compile 001-039 | RC=0 (real 123.7 s); zImage 3,289,344 B sha256 fe1846ffcfe7bec5eeb961c508814eb090cfffdab72252b31be1aa405aeb5a4b; vmlinux sha256 4720ce13e0afac7bc6312615bf0ea224382b5ee3cd76bd839cdf96fdb5bfe1c7; 0 warnings |
| built tree identity | platsmp.c sha256 99343778... = delivered patch content |
| symbols | _text = stext = 0xc0608000, secondary_startup = 0xc070a000; early marker 0xc1293400, early stub 0xc1293440 (expected PAs 0x81293400/0x81293440, matching the W9 log lines), probe stub 0xc1295000 |
| early stub encoding | all 20 instructions verified word-for-word against the project ARM assembler (marker 0 = MPIDR, marker 1 = 0x4545 progress store, bounded UART poll, WFE spin) |
| checkpatch | patch-mode: only the expected missing commit-description/Signed-off-by metadata plus compact-dump pr_cont notes |
| shared.patch v7 | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root (appends section 19 on top of the integrated sections 14-18) |

## 8. Remaining unknowns

* Whether CPU1 is held in reset/dormant or not polling the pen at all (modes 2/3/4).
* What the vendor kernel does differently before its release (GIC state is the next
  measurable difference; the stock GIC reads are in section 6).
* rstinfo cold/warm encoding (0x02000000 vs 0x42000000) [U]; SCU config bits above
  [1:0] [U]; physical semantics of CRG + 0x38 bits [U].
* Whether the 6.18 secondary entry works when CPU1 reaches it [U].
* W9 net TX oops: faulting function not captured; net lane.

## 9. Integration notes

1. Patch 039 is installed in patches-6.18/; 033/034/035/037 unchanged. No DTS/config
   change needed for the diagnostic image.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 19 to docs/SMP-RE.md and changes nothing else.
3. Next bench token: replace luofu_smp_early_probe=1 with luofu_smp_early_probe=2 on
   an 001-039 image; then 3 and 4 if silent; mode 5 as the read-only control.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply, clean
   prepare/compile logs, patch sources, System.map excerpts, early-stub assembler
   checks, W9 capture copies, EVIDENCE-SHA256SUMS.txt).
