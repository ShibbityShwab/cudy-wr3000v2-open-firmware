# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 3, 2026-11-19. Supersedes revision 2. New in this revision: the 034 hardware
result (register snapshots, SCU decode, pen-left-0 answer), the marker-channel fix
(patch 035) and the updated next-boot plan. **Host-only: no device, router, UART,
TFTP, flash or bench tool was touched while producing this package; nothing was
booted by this lane.** The hardware results quoted below were produced by the lead's
attended bench sessions and are cited from the saved console captures.

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034 and the
new 035). Design/evidence doc: docs/SMP-RE.md sections 14, 15 and 16 (this revision's
shared.patch appends section 16). Local evidence bundle:
build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | target-skeleton/target/linux/hisilicon/patches-6.18/033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | target-skeleton/target/linux/hisilicon/patches-6.18/034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| **patch 035 (this revision)** | target-skeleton/target/linux/hisilicon/patches-6.18/035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| shared documentation patch (SMP-RE.md section 16) | docs/incoming/smp-decode/shared.patch | 7d96d1ca939006fa5bed3d0e499c845e14e49fb1b3c51b5bca0f84eacc00ec42 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

Patch 035 touches only arch/arm/mach-hisilicon/platsmp.c. No devicetree, config or
binding change.

## 2. Hardware results so far (lead's attended sessions)

| run | image | result |
| --- | --- | --- |
| earlier gate/eps boots | series up to 031 | vendor-faithful release ran, CPU1 offline |
| W7 run | 14791958 (001-033) | 033 knobs were on the command line under bare names but were rejected (built-in object params need the "platsmp." prefix); probe never ran; SCU CPU status 0x00; CPU1 offline |
| 034 run | 5d2c5476 (001-034) | dumps worked: loader-left pen 0, resume 0x02a00000, SCU ctrl 3 / config 0x511 / status 0x03030000; after release pen 0x81195000 (unchanged), resume 0x00200000, SCU status unchanged, no 'P', CPU1 offline. The probe marker ioremap() of System RAM failed, so the marker was never read: **the probe result is inconclusive** |

## 3. What the 034 snapshot established [V] and what it did not

* SCU status 0x03030000 decodes byte-wise per core: CPU0 = 0x00 and CPU1 = 0x00
  (NORMAL/powered), CPU2 = 0x03 and CPU3 = 0x03 (powered off, cores do not exist).
  This validates the byte-wise A9 SCU power-mode decode and confirms CPU1 is
  powered, not dormant/power-down.
* The release produced CRG + 0x38 = 0x00200000, exactly the stock 2-CPU reference
  value, so the CRG operation matches the vendor's effect.
* The loader-left pen was 0 while the previous image had left 0x8070a000; that
  proves the decoded U-Boot boot-core path (clear pen, program CRG + 0x50, re-init
  the SCU) ran on this warm boot. The stock 0x80609680 sample is the vendor kernel's
  own runtime write, not loader state, so the two values are not comparable.
* The rstinfo record at SYSCTRL + 0xc10 read 0x42000000 versus 0x82000000 on the
  stock inventory - probably a reset-provenance or cold/warm marker [U].
* CRG words differ from the stock inventory only where our drivers deliberately
  write (0x20 gate, 0x30/0x34 resets) [I]; nothing CPU-power related differs.
* The probe result is inconclusive because the marker read path was broken (patch
  034 used ioremap() on System RAM, which 6.18 rejects). "pen unchanged, no 'P'" is
  consistent with both "CPU1 never consumed the pen" and "CPU1 consumed it and
  parked in the stub (for example in the unbounded UART poll)".

## 4. Patch 035: marker-fixed probe

* The marker page is read through the normal linear mapping after
  __sync_cache_range_r() (clean+invalidate inner cache, PL310 outer flush) instead
  of ioremap() of RAM.
* The stub writes marker word 0 = MPIDR on entry, prints 'P' on the console UART
  with a **bounded** LSR poll (1 << 20 reads, so a dead UART cannot park the core),
  then writes marker word 1 = 0x50454e21 ("PEN!") and branches to the real
  secondary_startup.
* +1 ms, +21 ms and retry lines print marker0/marker1 and "consumed=yes|no"; a
  read-only late_initcall() reads the markers once more in case the store arrives
  late.
* Everything else from 034 is unchanged: unconditional SYSCTRL/CRG/SCU snapshots at
  prepare/pre/+1ms/+21ms, probe default on, luofu_smp_probe=0 control, power,
  allbits, clear_mask, force_pen, retries knobs.
* Expected values for this build: marker page 0x81194000, stub 0x81195000, entry
  0x8070a000 (the log prints the real values).

## 5. Next single-variable boot plan

1. Build an **001-035** image from the bench clone (no command-line change needed)
   and warm-boot it; capture the UART read-only. The marker decides:
   * **consumed=yes** (or 'P' on the console): CPU1 consumes the pen. Next variable:
     luofu_smp_probe=0 (plain entry). If that still fails, CPU1 dies in the 6.18
     secondary entry and the next patch instruments __secondary_switched.
   * **consumed=no**: CPU1 never consumed the pen on this warm boot. Then, one
     variable per boot: (a) cold power-on of the same image (reset type); (b)
     luofu_smp_probe=0 control; (c) luofu_smp_power=1 control (SCU already normal,
     expected no-op); (d) clear_mask experiments only after the stock CRG/rstinfo
     reads are in hand.
2. Stock read (root telnet + /dev/mem, read-only) to correlate rstinfo and complete
   the SCU picture: after a known cold power-on and after a known warm reboot:

       for a in 0x10180000 0x10180004 0x10180008 \
                0x10100c00 0x10100c04 0x10100c08 0x10100c0c 0x10100c10 0x10100c14 \
                0x14880038 0x1488003c ; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done

   (devmem exists at /sbin/devmem; fallback dd if=/dev/mem bs=4 skip=$((0xADDR/4))
   count=1 | od -An -tx4). Known values from the 2026-11-18 inventory: pen
   0x80609680, resume 0x00200000, c08 0x10, c10 0x82000000, SCU not yet read.

## 6. Validation of patch 035 (host-only, isolated clone)

The validation window is series 001-035 in build/linux-kbuild/waves/2026-11-19-smp-fix/
(btrfs-reflink copy of /home/shibbityshwab/openwrt-lab-smp; openwrt main 27663a0,
linux-6.18.60, tree's own OpenWrt GCC 14.4.0). Shared clones were not modified; no
device was touched.

| check | result |
| --- | --- |
| pristine 6.18.60 series apply 001-035 | patch -p1 --no-backup-if-mismatch -F0: RC=0, 113 files, no .rej/.orig; only the four pre-existing offset hunks (008 x2, 021, 028); resulting arch/arm/mach-hisilicon/platsmp.c sha256 cc3edadc... identical to the delivered file |
| OpenWrt kernel prepare 001-035 | make target/linux/prepare V=s: RC=0 (1,246 patching-file lines, no .rej/.orig) |
| full kernel compile 001-035 | make target/linux/compile V=s -j16: **RC=0** (real 119.4 s); zImage 3,287,240 B sha256 c0a42e5ace13cd52be22a316676766ab8b0d143281729d2a9c5cee66c3a91c60; vmlinux sha256 e74c9178a100c5e59e0bd188ab7efea865fc2909152e352dae12f0224fba8f94; 0 warnings attributable to new/changed files |
| built tree identity | platsmp.c in the built tree sha256 cc3edadc... = delivered patch content |
| layout / symbols | System.map: _text = stext = 0xc0608000, secondary_startup = 0xc070a000, luofu_smp_probe_marker = 0xc1194000 (page aligned), luofu_smp_probe_stub = 0xc1195000; .init.setup carries the bare-name strings luofu_smp_probe= .. luofu_smp_diag= |
| probe stub encoding | generated words compared with the project ARM assembler for a sample marker address: all 22 instructions + literal match; the bne back into the poll loop was corrected from 0x1afffff9 to 0x1afffffa during this lane |
| checkpatch | patch-mode: only the expected missing commit-description/Signed-off-by metadata; file-mode 0 errors / 0 warnings |
| shared.patch v3 | git apply --check RC=0 and patch -p1 --dry-run RC=0 from the opensource/ root (appends section 16 on top of the integrated sections 14 and 15) |

## 7. Remaining unknowns

* Whether CPU1 consumes the pen at all on a warm boot (patch 035's marker answers it).
* Loader-time CPU1 state cannot be observed from stock userspace [U].
* Physical semantics of CRG + 0x38 bits 0x16..0x19 [U]; rstinfo 0x42000000 vs
  0x82000000 provenance [U]; SCU config bits above [1:0] [U] (raw word recorded).
* Whether the 6.18 secondary entry path works when CPU1 reaches it [U]; depends on
  the 035 marker result.

## 8. Integration notes

1. Patch 035 is installed in the target tree patches-6.18/ directory (033 and 034 are
   unchanged). No DTS/config edit is needed; build the bench image from 001-035.
2. Apply the shared documentation patch from the opensource/ root:

       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch

   It appends section 16 to docs/SMP-RE.md and changes nothing else.
3. The bench command line needs no change for 035 (probe default on, dumps always
   on). Leaving "luofu_smp_probe=1" is fine; "luofu_smp_diag=1" is accepted as a
   compatibility no-op.
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (pristine apply trees,
   clean prepare/compile logs, patch sources, System.map excerpts, stub assembler
   checks, EVIDENCE-SHA256SUMS.txt).
