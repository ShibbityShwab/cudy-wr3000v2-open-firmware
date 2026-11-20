# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 8, 2026-11-20. Supersedes revision 7. New: the live MMIO capability (DEVMEM
image c6ede30c with SSH + devmem) and the CPU1 experiment ladder that replaces
patch/build cycles - including the decode that a userspace release cannot bring CPU1
online (secondary_data is zeroed after the failed boot bring-up), and the safe
sysfs-hotplug path that can. No kernel code change in this revision. **Host-only: no
device access by this lane; hardware state is the lead's live reads and attended
runs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037, 039, 042). Design/evidence doc: docs/SMP-RE.md sections 14-21 (this revision's
shared.patch appends section 21). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | .../033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | .../034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | .../035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| patch 037 | .../037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| patch 039 | .../039-arm-hisilicon-smp-early-safe.patch | 37b7932b0c240d612582090ed17c5c91241f8bdfed074bdd0e7bafb13dd43a32 |
| patch 042 | .../042-arm-hisilicon-smp-scu-gic.patch | dc8511edb95e4b788fc1c7d7934a23c6eb9e9160d4b538ef40333db550fa673a |
| shared documentation patch (SMP-RE.md section 21) | docs/incoming/smp-decode/shared.patch | e25afefadf335985300fa4e2606caf7fd4333073e41abde536d908f31759e8b5 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

## 2. Live state on the DEVMEM image (c6ede30c, our 6.18, 1 CPU) [V]

    SCU CTRL 0x3, CONFIG 0x511, CPU status 0x03030000
    GICD CTLR 1, TYPER 0xfc25, IIDR 0x0102043b
    GICC CPU0 CTLR 1, PMR 0xf8; GICC CPU1 bank reads 0 from CPU0 (RAZ, no information)
    SYSCTRL pen 0x81295000 (kernel probe stub), CRG 0x14880038 = 0x00200000
    late marker0/1 = 0/0, consumed=no, early_mode=0
    System.map secondary_startup = 0xc070b000 (PA 0x8070b000)

## 3. Two decisive facts for live work [V]

1. The current pen value is the kernel probe stub, not the entry. It prints 'P',
   writes marker words and branches to secondary_startup.
2. secondary_data is memset to zero at the end of __cpu_up() after the failed boot
   bring-up (arch/arm/kernel/smp.c), and secondary_startup consumes it with MMU off.
   A userspace release into secondary_startup therefore cannot online CPU1; it would
   enable the MMU with a zero page-table pointer and wedge CPU1. Never write
   pen=0x8070b000, and treat a raw release into the current probe stub as "CPU1
   wedges after printing 'P'".

## 4. Primary ladder (no raw writes, fully repeatable): sysfs hotplug

Writing 1 to /sys/devices/system/cpu/cpu1/online runs the real __cpu_up() path with
secondary_data set correctly, our release diagnostics, the probe stub and a 1 s
timeout. It is exactly the boot path, but at a late, fully initialised time, and it is
repeatable. Success = "CPU1: thread ... mpidr ..." in dmesg, /proc/cpuinfo with two
processors, cpu1/online = 1, and 'P' on the console. Failure = the usual
"CPU1: failed to come online" with the 039/042 diagnostic set in dmesg.

Interleave single variables: plain attempts x3; SCU CONFIG bit 5 set to the stock
0x00000531 and attempts; a wakeup-IPI storm (SGIR 0x00020000, or broadcast
0x01000000) running during an attempt.

## 5. Secondary ladder (only if needed): raw devmem release into a safe stub

Rebuild the DEVMEM image once with luofu_smp_early_probe=5: mode 5 writes no MMIO but
builds and cache-cleans the safe early stub (marker word 0 = MPIDR, prints 'E', marker
word 1 = 0x4545, WFE spin). Then: pen = early-stub PA (System.map VA - 0x40000000),
CRG RMW clearing 0x02800000, SGIR 0x00020000. Success = a raw 'E' on the UART (not
dmesg) while the box stays up; /proc/cpuinfo stays at 1 because the kernel never
onlines it. Marker page reads must use an uncached (/dev/mem mmap O_SYNC) path; plain
devmem read/write of RAM goes through the cacheable linear map without cache
maintenance. Do not use this ladder against the current probe stub.

## 6. Rollback and risk

Failed raw attempts are self-cleaning but explicitly restore pen 0x81295000, CRG
0x00200000 and (if changed) SCU config 0x00000511. If CPU1 consumed the pen ('E' or
markers set): reboot - the reset releases CPU1 from its parked state and the next
boot starts from the 1-CPU baseline.

Hang-risk: medium for pen=secondary_startup / raw release into the probe stub (CPU1
abort loop, small watchdog-reset chance; UART capture and recovery door ready);
low-medium for SCU CONFIG bit writes; low for the CRG RMW; none for GICD_SGIR; never
touch the CPU1 GICC bank. The sysfs ladder carries none of these risks.

## 7. Interpretation path

* consumed=yes/'P' at any live attempt: CPU1 does consume the pen in our 6.18
  context; the differing single variable (late time, SCU config bit, IPI storm)
  becomes the kernel-side fix (retry loop in __cpu_up with real delays and/or the SCU
  configuration bit in the release path).
* all attempts consumed=no: CPU1 does not execute the loader pen-poll loop on our
  boot path; the loader decode and the deeper stock reads (SMP-RE 20.4) are next.

## 8. Remaining unknowns

* Whether CPU1 consumes the pen at any late time (live ladder).
* Whether SCU CONFIG bit 5 matters (stock 0x531 vs our 0x511).
* rstinfo encoding (0x02/0x42/0x0A samples) [U]; SCU config bit 5 semantics [U].
* W9 net TX oops: faulting function not captured (net lane).

## 9. Integration notes

1. No kernel patch in this revision; patches 033-042 stand as delivered.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 21 to docs/SMP-RE.md and changes nothing else.
3. Live experiment order: sysfs hotplug attempts (plain, then SCU config bit, then
   IPI storm); if silent, one rebuild with luofu_smp_early_probe=5 for the raw
   safe-stub ladder; if still silent, loader decode + stock deeper reads.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply, clean
   prepare/compile logs, patch sources, System.map excerpts, capture copies,
   EVIDENCE-SHA256SUMS.txt).
