# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 5, 2026-11-20. Supersedes revision 4. New: the 037 reserve_low1m hardware
run (image 6cb3d908) refuted the "loader parks CPU1 in the low 1 MiB window" class;
the next single variable is the early probe (037 already supports it, no code change).
**Host-only: no device access by this lane; hardware results are the lead's attended
runs, cited from the saved console logs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037). Design/evidence doc: docs/SMP-RE.md sections 14-18 (this revision's
shared.patch appends section 18). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | .../034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | .../035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| patch 037 (requested as 036; 036 is the parallel PFE lane) | .../037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| shared documentation patch (SMP-RE.md section 18) | docs/incoming/smp-decode/shared.patch | 413df54299bc0c7c424ff67eb9d673d94174116a0ed746815047d07659545e9f |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

## 2. Hardware results so far

| run | image | result |
| --- | --- | --- |
| earlier gate/eps boots | up to 031 | vendor-faithful release, CPU1 offline |
| W7 | 14791958 (001-033) | 033 knobs rejected (built-in param prefix); probe never ran |
| 034 | 5d2c5476 | dumps worked; marker ioremap of RAM failed -> probe inconclusive |
| 035 warm | r3-armed | marker channel works: consumed=no, CPU1 offline |
| 035 cold (power-cycle) | 75868fe0 | identical to warm: consumed=no, CPU1 offline; rstinfo 0x02000000 vs 0x42000000 warm |
| **037 reserve_low1m** | **6cb3d908** | reserve effective (available RAM now starts 0x80600000 = vendor effective map); CPU1 still consumed=no, markers 0, CPU1 offline |

## 3. What the 037 run established [V]

* the low 1 MiB window 0x80500000-0x805fffff contains a DDR test pattern
  (0x66cc66cc with bit variations), not code; the decoded U-Boot park-loop signature
  is absent from the whole 0x80500000-0x80607fff range (scan hits=0);
* the reservation moved our available memory map to exactly the vendor kernel's
  effective map (available memory now starts at 0x80600000), yet CPU1 still does not
  consume the pen -> **the "loader parked CPU1 in the low 1 MiB and our kernel
  overwrote it" hypothesis is refuted**;
* the 16 KiB flashinfo window at 0x80600000 holds a few non-pattern words
  (00030003, 00000000, c0600008, c0600008, 0) then the test pattern; with the low
  1 MiB reserved it is the first available RAM for our kernel, so these may be our
  own early-allocation leftovers [U];
* the reserved ranges near the top of RAM are the kernel's own early reservations;
* cold and warm boots fail identically, so reset type/power state are not the cause.

## 4. Next single variable: luofu_smp_early_probe=1 (no code change needed)

Reserving the remaining flashinfo window is now low value (boot-data shaped content,
no park signature, and the early probe runs before any allocation). The next boot
should be **luofu_smp_early_probe=1** (mode 1: pen only; no CRG clear, no GIC write).

Special considerations:

* the early probe runs from an early_param handler inside setup_arch(), before
  memblock/paging init; it uses early_ioremap() and fixed SoC physical addresses, so
  no DT dependency;
* it prints "luofu-smp: EARLY probe mode 1 ... stub <pa> marker <pa>" (expected
  around 0x81193440 / 0x81193400 for this build) and "EARLY pen ... resume ...";
* the stub prints one raw 'E' on the UART (bounded LSR poll; UART already configured
  by the bootloader), records its MPIDR in a marker page, then WFE-spins; 'E' may
  interleave with earlycon output, so scan the raw log for a stray 'E';
* if CPU1 consumes the pen it is parked in our early stub: it will NOT come online
  and the normal +1ms/+21ms markers stay 0. The evidence is 'E' and/or
  "EARLY mode 1 marker0=<MPIDR> consumed=yes" in the prep line;
* if mode 1 is silent, try mode 2 (adds CRG resume-bit clear) then mode 3 (adds GIC
  wakeup IPI). Mode 3 is the only mode that touches the GIC before its driver is
  initialised; small hang risk, recovery door covers it;
* if all three modes are silent, CPU1 does not execute the loader's pen-poll loop
  even before our kernel does anything; then the next step is the stock read plan
  (SMP-RE 17.6) plus loader-side decode of how CPU1 is started.

If mode 1 is positive, the follow-up patch (038) will bisect when the consumer
disappears by repeating the same marker-stub pen write at later early-boot phases
(e.g. late_time_init), which localises the phase that destroys/diverts CPU1.

## 5. Stock read-only measurement plan (root telnet, no writes)

/devmem exists at /sbin/devmem; /dev/mem readable. On the stock 5.10 boot (2 CPUs):

1. SCU + SYSCTRL + CRG reference:
       for a in 0x10180000 0x10180004 0x10180008 0x10100c00 0x10100c04 \
                0x10100c08 0x10100c0c 0x10100c10 0x10100c14 0x14880038 0x1488003c; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done
2. Loader windows (preserved on stock because the vendor kernel does not use them):
       for a in $(seq $((0x80500000)) 4096 $((0x805ff000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done
       for a in $(seq $((0x80600000)) 4096 $((0x80603000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done
   and, for the park-loop signature:
       dd if=/dev/mem bs=4096 skip=$((0x80500000/4096)) count=256 2>/dev/null \
         | od -An -v -tx4 | grep -i -m1 'e5967000'
3. Repeat (1) after a known cold power-on and a known warm reboot for the rstinfo
   correlation (ours: 0x02000000 cold / 0x42000000 warm).

If /dev/mem refuses 0x805xxxxx or a read faults, stop and report; the 037 in-kernel
dump already covers that region from our side.

## 6. Validation status

Patch 037 is unchanged and was validated in revision 4: pristine apply of the current
repository series 001-037 (including the parallel 036-net patch, which requires 032
revision 3) RC=0, 117 files, no rejects; clean prepare RC=0 (1,250 patching lines) and
compile RC=0 (zImage sha256 de31f6b5..., vmlinux sha256 10d74fb6...), 0 warnings,
built platsmp.c = delivered (528ac6f8...), early stub encodings verified with the ARM
assembler. This revision only updates documentation (SMP-RE section 18) and the lane
record; no code change.

## 7. Remaining unknowns

* Whether CPU1 executes the loader's pen-poll loop at all (early probe answers it).
* Where/how the loader starts CPU1 on this boot path (loader-side decode).
* rstinfo cold/warm encoding (0x02000000 vs 0x42000000) [U]; SCU config bits above
  [1:0] [U]; physical semantics of CRG + 0x38 bits [U].
* Whether the 6.18 secondary entry works when CPU1 reaches it [U].

## 8. Integration notes

1. Patch 037 is installed in patches-6.18/; no DTS/config change needed for the
   diagnostic image.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 18 to docs/SMP-RE.md and changes nothing else.
3. Next bench token: replace luofu_smp_reserve_low1m=1 with luofu_smp_early_probe=1
   (keep the other tokens unchanged). Mode 2/3 later if mode 1 is silent.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply trees,
   clean prepare/compile logs, patch sources, System.map excerpts, early-stub
   assembler checks, EVIDENCE-SHA256SUMS.txt).
