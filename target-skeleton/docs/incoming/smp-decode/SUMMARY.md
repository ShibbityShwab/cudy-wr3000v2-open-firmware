# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 10, 2026-11-20. Supersedes revision 9. New: correction of the 060 handoff
result (the nonzero B marker is the loader's DDR test pattern, not consumption - CPU1
was never entered the stubs), and patch 061 - sentinel marker protocol plus the
re-polling handoff trampoline. **Host-only: no device access by this lane; hardware
results are the lead's attended runs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037, 039, 042, 059, 060 and the new 061). Design/evidence doc: docs/SMP-RE.md
sections 14-23 (this revision's shared.patch appends section 23). Local evidence
bundle: build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033-042 | (unchanged, see revision 9) | ... |
| patch 060 | .../060-arm-hisilicon-smp-handoff-probe.patch | e1d9dc5f6c1e64dcd473c26b6824e7c60822e18e424781388a03b82bcfa53938 |
| **patch 061 (new)** | .../061-arm-hisilicon-smp-trampoline.patch | 5e254fe4cea0b64d47aa0fc3c4ddf7c82f935b76d92fd8f311ba9b219b52ddd8 |
| shared documentation patch (SMP-RE.md section 23) | docs/incoming/smp-decode/shared.patch | 925ee41da250b410986f6594ef1f940e1dc776bbc8c76c86834a1e562c449666 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

## 2. Correction of the 060 handoff result [V]

Image 202832a2 reported handoff A=00000000 B=66cc66cc pen=80600080 scu_cfg=00000511
and was read as "B consumed during decompression". 0x66cc66cc is the loader's DDR
test pattern (visible across 0x80500000-0x805fffff and at 0x80600018/0x1c in the
flashinfo window), not a consumed marker: a consumed probe must read 0x80000001
(secondary MPIDR). So both probes were never entered and CPU1 is **not** polling the
pen at kernel handoff or stext. The "CPU1 alive during decompression" conclusion is
withdrawn; the earlier interpretation tree applies with A=B=0 (preset), i.e. the
loader never starts CPU1 into the pen-poll path for our image.

## 3. Patch 061

Two parts:

1. **Sentinel protocol** (removes the pattern ambiguity permanently): marker A at
   0x80600010 is initialised to 0x600D1A88 by the decompressor probe; marker words
   0x80600020/0x80600024/0x80600028 to 0x600D1A88/0x600D1A88/0 by stext. Sentinels
   read back as "not-entered"; 0x80000001 means entered; the jump magic 0x600D2B61 at
   0x80600024 means the trampoline jumped, with the new pen value at 0x80600028.
2. **Re-polling trampoline at stext** (PA 0x80600080, 17 words, position
   independent): on entry it stores MPIDR to 0x80600020, then polls the pen; when the
   pen differs from the trampoline address it stores 0x600D2B61 and the new value and
   branches there. With cmdline token **luofu_smp_trampoline=1** the page
   0x80600000-0x80600fff is memblock_reserved from an early_param handler (before any
   allocation) and the release path writes **pen = __pa_symbol(secondary_startup)**
   instead of the 035 probe stub, so a release hands CPU1 into the real secondary
   entry with secondary_data already prepared by __cpu_up(). Without the token 061
   behaves exactly like 060.

No relocation or cache maintenance is needed: the code uses movw/movt and
PC-relative branches only, is entered with MMU/caches off at its physical address,
is written once by stext with caches off (already in DRAM), the reserved page is
never allocated or written by the kernel, and marker reads use
__sync_cache_range_r() invalidation before reading.

## 4. Validation of 061

Applied after 060 in the isolated clone (SMP patch train), prepare RC=0, compile
RC=0, 0 warnings/errors; probe/trampoline opcodes verified in
arch/arm/kernel/head.o and arch/arm/boot/compressed/head.o (sentinel movw 0xe3015a88,
trampoline words incl. e5962000/e1520005/0afffffc/e3023b61/e12fff12); vmlinux sha256
17f9ef69..., zImage sha256 0cc32686..., platsmp.c 545b0257... = edited file; the
trampoline/handoff format strings are present in vmlinux. (The clone train excludes
the parallel net-lane patches 036+ because several do not apply in a fresh tree from
this side; unrelated to the three files 061 touches.)

## 5. Next

* Run 061 with tokens luofu_smp_handoff_probe=1 luofu_smp_trampoline=1: if all
  markers stay sentinel, CPU1 never enters the trampoline - the loader does not start
  CPU1 for our image, and the work moves to loader-side decode. If B0 becomes
  0x80000001, a later release should produce the jump magic and 2 CPUs.
* Loader-side evidence so far: stock kernel slot is 0x840000 (8,650,752 B), so the
  ~12 MiB uncompressed-Image variant cannot boot without a reduced config; the
  decoded U-Boot poll-loop address 0x800405e0 reads 0xE2066007 on stock and
  0x80040000 reads 0 - the U-Boot image is not at the assumed runtime base, so the
  loader decode must relocate it (scan candidate RAM bases for the U-Boot image
  signature) before any conclusion about CPU1 parking can be drawn.

## 6. Integration notes

1. Patch 061 is installed in patches-6.18/; apply after 060.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 23 to docs/SMP-RE.md and changes nothing else.
3. Bench tokens for the 061 image: luofu_smp_handoff_probe=1 luofu_smp_trampoline=1.
