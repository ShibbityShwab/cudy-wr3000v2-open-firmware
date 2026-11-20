# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 4, 2026-11-20. Supersedes revision 3. New in this revision: the cold-boot
result (CPU1 never consumes the pen on cold or warm boot), the loader-window
hypothesis (vendor kernel avoids 0x80500000-0x805fffff and reserves 0x80600000+16 KiB,
our kernel treats both as free RAM), patch 037 (early probe + window reservation +
loader-window diagnostics; requested as 036 but renumbered because the parallel PFE
lane filed 036-net-lsw-pfe-init.patch at the same time), and the precise stock
read-only measurement plan. **Host-only: no device access by this lane; hardware
results are the lead's attended runs, cited from the saved console logs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035 and
the new 037). Design/evidence doc: docs/SMP-RE.md sections 14-17 (this revision's
shared.patch appends section 17). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | target-skeleton/target/linux/hisilicon/patches-6.18/034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | target-skeleton/target/linux/hisilicon/patches-6.18/035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| **patch 037 (this revision; requested as 036)** | target-skeleton/target/linux/hisilicon/patches-6.18/037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| shared documentation patch (SMP-RE.md section 17) | docs/incoming/smp-decode/shared.patch | fa0b4ae0d8292e9f11e804e7fd6e6692267977211005b83065e6de2c68e9ad53 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

Numbering note: patch 036 is taken by 036-net-lsw-pfe-init.patch from the parallel
PFE lane; the SMP diagnostic is therefore 037 so the series stays unambiguous. It
changes only arch/arm/mach-hisilicon/platsmp.c (no DTS/config change). The parallel
036 patch requires 032 revision 3 (sha256 719c824c...); the validation window used
the current repository series.

## 2. Hardware results so far (lead's attended sessions)

| run | image | result |
| --- | --- | --- |
| earlier gate/eps boots | series up to 031 | vendor-faithful release ran, CPU1 offline |
| W7 run | 14791958 (001-033) | 033 knobs were rejected (built-in object params are prefixed "platsmp."), probe never ran, CPU1 offline |
| 034 run | 5d2c5476 (001-034) | dumps worked (loader-left pen 0, resume 0x02a00000, SCU ctrl 3 / config 0x511 / status 0x03030000, post-release resume 0x00200000); marker ioremap of RAM failed -> probe inconclusive |
| 035 run warm | r3-armed image | marker channel works: prep markers 0, +1ms/+21ms marker0/1 = 0, consumed=no, pen 0x81195000 unchanged, CPU1 offline |
| 035 run cold (power-cycle) | 75868fe0 | identical to warm: markers 0, consumed=no, pen unchanged, CPU1 offline. Only difference: rstinfo 0x10100c10 = 0x02000000 cold vs 0x42000000 warm |

**Conclusion [V]: CPU1 does not consume the pen on warm OR cold boot.** CPU1 is
powered (SCU status 03030000: CPU0/CPU1 normal, CPU2/CPU3 powered off) before and
after the release; the pen write is visible (read back); the CRG result matches the
vendor reference; the marker path is proven (it reads zeros through the same
cache-maintenance channel the stub writes with).

## 3. Hypotheses after the cold result

The cold result removes reset type and CPU1 power state as explanations. The
remaining question is whether CPU1 is executing the loader's pen-poll loop at all.
The leading new hypothesis [I] is that the loader parks CPU1 in RAM that our kernel
treats as free and overwrites before the release:

* the vendor 5.10 kernel's effective memory map starts at **0x80600000** ("Early
  memory node ranges: node 0: [mem 0x80600000-0x87ffffff]"), although the vendor DTB
  memory node starts at 0x80500000, while our 6.18 kernel uses **0x80500000** as its
  first RAM range [V];
* the vendor DTB reserves a 16 KiB "flashinfo" window at **0x80600000**
  (reserved-memory/flashinfo, atag-offset 0x1000); our DTB has no reserved-memory
  node, and our page tables sit just above it at 0x80604000 [V].

Our kernel's first memblock allocations are first-fit from the lowest RAM address
(page tables, memmap), so they can overwrite whatever the loader left in
0x80500000-0x805fffff; the vendor kernel never touches those bytes. That would
explain "CPU1 powered but never polls the pen" on both cold and warm boots while
stock works. Patch 037 tests this hypothesis directly.

## 4. Patch 037: what it adds

All 035 behaviour is kept (unconditional SYSCTRL/CRG/SCU dumps at prepare/pre/+1ms/
+21ms, marker-fixed probe default on, probe=0 control, power/allbits/clear_mask/
force_pen/retries). New knobs and diagnostics:

| knob / diagnostic | effect |
| --- | --- |
| luofu_smp_reserve_low1m=1 | memblock_reserve(0x80500000, 1 MiB) from an early_param handler, i.e. before arm_memblock_init() and paging_init() and therefore before the first allocation. Highest-value single variable: if the loader parked CPU1 there, preserving the window should let CPU1 consume the pen and come online. |
| luofu_smp_reserve_flashinfo=1 | memblock_reserve(0x80600000, 16 KiB), mirroring the vendor reserved-memory node. |
| luofu_smp_early_probe=1 (2, 3) | run the release inside setup_arch() (parse_early_param, before any allocation) using early_ioremap(): pen points at a stub that prints 'E', records its MPIDR in a marker and spins in WFE. Mode 1 = pen only, 2 = pen + CRG resume clear, 3 = pen + CRG clear + GIC wakeup IPI. 'E' and/or the EARLY marker proves CPU1 consumed the pen before our kernel did anything. |
| always on | read-only loader-window diagnostics at prepare: first words of 0x80500000, 0x805ff000, 0x80600000, 0x80604000; a scan of 0x80500000-0x80607fff for the decoded U-Boot park-loop signature (e5967000/e3570000/0afffffc/e1a0f007); and the memblock memory/reserved range dump (shows what our kernel allocated where). |

The early probe builds its stub at runtime, cleans it to the point of coherency and
uses early_ioremap() (available in setup_arch()). Nothing is written or reserved
unless the corresponding command-line knob is present.

## 5. Next single-variable boot plan

1. **luofu_smp_reserve_low1m=1** on an 001-037 image (highest value). If CPU1 consumes
   the pen or comes online, the loader parked CPU1 in that window and the real fix is
   a memory-map correction (reserve the window very early, or correct the DTS memory
   map to the vendor's effective map). The 037 diagnostic also prints whether the
   park-loop signature is visible there.
2. If not: **luofu_smp_reserve_flashinfo=1**.
3. If not: **luofu_smp_early_probe=1**, then 2, then 3. An 'E' / early marker at mode
   1 means CPU1 was parked and polling before our kernel allocated anything, so
   something between setup_arch and the normal release destroys or diverts it; a hit
   only at mode 3 means the CRG clear + wakeup IPI are required for a parked core.
4. If all silent: the loader does not park CPU1 on our boot path; then decode the
   loader's CPU1 start (hboot/U-Boot) and use the stock measurements in section 6.

## 6. Stock read-only measurement plan (root telnet, no writes)

/devmem is present at /sbin/devmem (health check 2026-11-18) and /dev/mem is readable.
Run on the stock 5.10 boot (2 CPUs) after recovery:

1. SCU and SYSCTRL/CRG reference (read twice, record boot_id/uptime):

       for a in 0x10180000 0x10180004 0x10180008 \
                0x10100c00 0x10100c04 0x10100c08 0x10100c0c 0x10100c10 0x10100c14 \
                0x14880038 0x1488003c ; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done

   Expected from the 2026-11-18 inventory: pen 0x80609680, resume 0x00200000,
   c08 0x10; SCU status should read 0x03030000 if the same decode holds.

2. **Loader windows (the important new measurement).** On stock these bytes are
   preserved because the vendor kernel does not use them:

       for a in $(seq $((0x80500000)) 4096 $((0x805ff000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done
       for a in $(seq $((0x80600000)) 4096 $((0x80603000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done

   Look for the U-Boot park loop with a dump-and-grep (busybox od):

       dd if=/dev/mem bs=4096 skip=$((0x80500000/4096)) count=256 2>/dev/null \
         | od -An -v -tx4 | grep -i -m1 'e5967000'
       dd if=/dev/mem bs=4096 skip=$((0x80600000/4096)) count=4 2>/dev/null \
         | od -Ax -v -tx4

   If /dev/mem refuses 0x805xxxxx (STRICT_DEVMEM) or a read faults, stop there and
   say so; the 037 in-kernel dump reads the same region from our side.

3. Reset-record correlation: repeat (1) after a known power-on and after a known warm
   reboot. On our side the cold record was 0x02000000 and the warm record 0x42000000.

## 7. Validation (host-only, isolated clone)

Validation window: the current repository series 001-037 (including the parallel
036-net-lsw-pfe-init.patch, which requires 032 revision 3). Environment:
build/linux-kbuild/waves/2026-11-19-smp-fix/, a btrfs-reflink copy of
/home/shibbityshwab/openwrt-lab-smp (openwrt main 27663a0, linux-6.18.60, tree's own
OpenWrt GCC 14.4.0). Shared clones were not modified; no device was touched.

| check | result |
| --- | --- |
| pristine 6.18.60 series apply 001-037 | patch -p1 --no-backup-if-mismatch -F0: RC=0, 117 files, no .rej/.orig; only the four pre-existing offset hunks (008 x2, 021, 028); resulting arch/arm/mach-hisilicon/platsmp.c sha256 528ac6f8... identical to the delivered file |
| OpenWrt kernel prepare 001-037 | make target/linux/prepare V=s: RC=0 (1,250 patching-file lines, no .rej/.orig) |
| full kernel compile 001-037 | make target/linux/compile V=s -j16: **RC=0** (real 120.4 s); zImage 3,289,376 B sha256 de31f6b526194319b95a9d050e1130ebf2144f02f362f8f8431eeeccabd3eb1e; vmlinux sha256 10d74fb67fa5ed2674868aee0c3ef1ae5a731d71ae6db7584d0c1fb542b3e6af; 0 warnings attributable to new/changed files |
| built tree identity | platsmp.c sha256 528ac6f8... = delivered patch content |
| symbols | _text = stext = 0xc0608000, secondary_startup = 0xc070a000; luofu_smp_early_marker c1193400, luofu_smp_early_stub c1193440, luofu_smp_probe_marker c1194000, luofu_smp_probe_stub c1195000 |
| early knobs present | vmlinux carries the early_param strings luofu_smp_early_probe, luofu_smp_reserve_low1m, luofu_smp_reserve_flashinfo |
| early stub encoding | all 18 instructions verified word-for-word against the project ARM assembler (bounded UART poll, 'E' print, WFE spin) |
| checkpatch | patch-mode: only the expected missing commit-description/Signed-off-by metadata plus compact-dump pr_cont notes; file-mode 0 errors / 0 warnings |
| shared.patch v4 | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root (appends section 17 on top of the integrated sections 14-16) |

Note: an earlier validation attempt failed at 036-net-lsw-pfe-init.patch because the
repository's 032 had been revised (revision 3) after the clone was synced; after
re-syncing the whole target tree the series applies and builds cleanly. This is a
numbering/baseline note for the lead, not an SMP patch problem.

## 8. Remaining unknowns

* Whether CPU1 is executing the loader's pen-poll loop at our release time (the 037
  window reservation and early probe answer this).
* Where the loader leaves CPU1's park state (1 MiB window / flashinfo window / loader
  image region); the stock reads in section 6 and the 037 in-kernel scan target it.
* Physical semantics of CRG + 0x38 bits 0x16..0x19 [U]; rstinfo cold/warm encoding
  (0x02000000 vs 0x42000000) [U]; SCU config bits above [1:0] [U].
* Whether the 6.18 secondary entry path works when CPU1 reaches it [U].

## 9. Integration notes

1. Patch 037 is installed in the target tree patches-6.18/ directory; 033/034/035 are
   unchanged. No DTS/config edit is needed for the diagnostic image.
2. Apply the shared documentation patch from the opensource/ root:

       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch

   It appends section 17 to docs/SMP-RE.md and changes nothing else.
3. Bench command lines for the next runs (keep the previous baked line, add one of):
   luofu_smp_reserve_low1m=1  ->  then luofu_smp_reserve_flashinfo=1  ->
   then luofu_smp_early_probe=1 / 2 / 3.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply trees,
   clean prepare/compile logs, patch sources, System.map excerpts, early-stub
   assembler checks, EVIDENCE-SHA256SUMS.txt).
