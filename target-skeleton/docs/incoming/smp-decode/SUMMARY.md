# SMP release diagnosis lane - integration record (task-62, agent smp-decode)

Revision 9, 2026-11-20. Supersedes revision 8. New: patch 060 - the handoff probes
that test whether the loader parks/started CPU1 *before* our decompressor runs -
plus the absorbed live results (late add_cpu attempts all consumed=no; the stock
CRG/GIC deltas are PCIe/WiFi activity; SCU CONFIG bit5 is a read-only CPU1-ran
monitor) and the uncompressed-Image variant recipe. **Host-only: no device access by
this lane; hardware results are the lead's live reads and attended runs.**

Kernel side: target-skeleton/target/linux/hisilicon/patches-6.18/ (033, 034, 035,
037, 039, 042, 059 - lead's - and the new 060). Design/evidence doc:
docs/SMP-RE.md sections 14-22 (this revision's shared.patch appends section 22).
Local evidence bundle: build/linux-kbuild/waves/2026-11-19-smp-fix/ (gitignored).

## 1. Deliverables

| artifact | path | sha256 |
| --- | --- | --- |
| patch 033 | .../033-arm-hisilicon-smp-release-diagnostics.patch | cf0df0ee29af01edd13b89935eba5326dc3a07ecead2efcf1390e8c3fe5725cc |
| patch 034 | .../034-arm-hisilicon-smp-release-probe.patch | cc27347f503b203c6f891373c589239e278dccd52b66551c9c291632f8953208 |
| patch 035 | .../035-arm-hisilicon-smp-release-marker.patch | 8724e926a7d621d50a89590e4907aa4e5912a0c69e35f092607564b8aa9e0997 |
| patch 037 | .../037-arm-hisilicon-smp-early-probe.patch | 2b073ec0905dcf48c56bc3e0fb6c16fe363ae030d67f6205e24fbda19391f237 |
| patch 039 | .../039-arm-hisilicon-smp-early-safe.patch | 37b7932b0c240d612582090ed17c5c91241f8bdfed074bdd0e7bafb13dd43a32 |
| patch 042 | .../042-arm-hisilicon-smp-scu-gic.patch | dc8511edb95e4b788fc1c7d7934a23c6eb9e9160d4b538ef40333db550fa673a |
| **patch 060 (new)** | .../060-arm-hisilicon-smp-handoff-probe.patch | e1d9dc5f6c1e64dcd473c26b6824e7c60822e18e424781388a03b82bcfa53938 |
| shared documentation patch (SMP-RE.md section 22) | docs/incoming/smp-decode/shared.patch | 0d83172a14ee2ad76c8e92360e7734ac1600ac66c314e85f3006584322a97cf8 |
| this record | docs/incoming/smp-decode/SUMMARY.md | - |
| local evidence bundle | build/linux-kbuild/waves/2026-11-19-smp-fix/ | EVIDENCE-SHA256SUMS.txt there |

## 2. Absorbed live results (lead, DEVMEM image, 001-059 + hotplug)

Every late-time attempt failed with consumed=no: add_cpu(1) x3 via the sysfs trigger
(real __cpu_up), targeted and broadcast SGI0 storms during the attempt, CRG resume
word cleared first (0x00000000 readback), and the stock CRG/GIC deltas applied
(+0x20/+0x28 |= 0x3000, +0x34 |= 0x000FF000, GICD ISENABLER1 |= 0x5) then add_cpu.
Registers restored, box healthy. SCU CONFIG bit5 write (0x531) acknowledged but read
back 0x511, i.e. read-only.

Interpretation: the stock CRG deltas are **PCIe/WiFi activity**, not CPU1: our clock
driver defines +0x20 as a clock-gate word (bits 12/13 = PCIe0/PCIe1) and our DTS
assigns +0x34 bits 0xc-0x13 to the PCIe0/PCIe1 reset lines; stock has PCIe up, our
image does not. The GIC differences (IRQ32/34 enabled, SGI-target ITARGETSR0=0x02
bytes) are post-boot device-IRQ/affinity state (ITARGETSR0-7 are read-only/banked for
SGIs; SGI routing is via SGIR target bits and was already exercised). SCU CONFIG
bit5 is a read-only **CPU1-ran monitor** (bits 4/5 = CPU0/CPU1 participation [I]):
0x531 means a CPU1 core executed some boot code; 0x511 means it never did.

## 3. Patch 060: handoff probes (the untested window)

Three files, guarded by CONFIG_ARCH_LUOFU && !CONFIG_THUMB2_KERNEL, no kernel config
token needed (only the early_param token luofu_smp_handoff_probe=1):

1. arch/arm/boot/compressed/head.S at the decompressor entry: 5-word marker-only stub
   at PA 0x80600040 (writes MPIDR to PA 0x80600010, spins), pen set to it. Runs
   before any decompression write/cache/MMU setup.
2. arch/arm/kernel/head.S at stext (MMU/caches off): same with stub B at
   PA 0x80600080 (marker 0x80600020), pen set to it.
3. platsmp.c: early_param luofu_smp_handoff_probe reads A/B through an
   early_ioremap window of 0x80600000 *before* arm_memblock_init/paging_init (no
   allocation can have overwritten them; uncached view of the RAM bytes the
   secondary wrote with caches off), prints A/B/pen/SCU config, and prints the same
   markers and the SCU CONFIG bit-5 monitor at prepare and after the release.

Expected lines (early_param, before the paging-init banner):

    luofu-smp: handoff A=00000000 B=00000000 pen=80600080 scu_cfg=00000511
    luofu-smp: handoff verdict decompressor-entry=no kernel-stext=no
    luofu-smp: handoff cpu1-ran monitor scu_cfg bit5=clear

A consumed probe is nonzero and equals 0x80000001 (CPU1 MPIDR). Interpretation:
A nonzero => the consumer was alive at the decompressor entry (loader does park
CPU1; failure is in our kernel's first-instruction/decompressor window); A=0,B!=0 =>
consumer appeared during decompression (probe between cache_on and decompress_kernel);
A=B=0 => the loader never started CPU1 on our image path. The probes are marker-only
spin stubs, so a taken pen parks CPU1 harmlessly; the image is diagnostic (writes the
pen at boot) - do not expect add_cpu to online CPU1 on it, read the lines and reboot.

## 4. Uncompressed-Image variant

build/linux-kbuild/build-3dl.sh already shows the recipe: build with the Image target,
make the payload "arch/arm/boot/Image + luofu-r116.dtb" instead of "zImage + dtb", wrap
with the same mkimage line (-a/-e 0x80608000, -n Linux-5.10.201) and the same 40,732-byte
trailer. Caveats: (1) the appended-DTB mechanism is decompressor-only, so an Image
boot receives the loader's DTB (vendor base + overlays, root compatible "hsan-luofu")
which our machine descriptor does not match - for the experiment add "hsan-luofu" to
luofu_dt_compat (one line) and accept a vendor-DT boot (enough to reach the handoff
print), or pass our DTB explicitly; (2) the loader reads a bounded kernel image
(8650752 bytes = 8.25 MiB in the failover log) while our Image is about 12 MiB - check
/proc/mtd on stock; if the kernel partition is smaller, the variant needs a
size-reduced config or is not bootable. An Image build has no decompressor, so only
probe B exists there: Image-B consumed but zImage-B not isolates the decompressor /
image-type question.

## 5. Validation of 060 (host-only, isolated clone)

The three-file patch applies after 059 and the full kernel compiles (see the lane
summary for the exact log names/hashes; the isolated clone's patch set is the SMP
train because the parallel net-lane patches in the current repository do not apply in
a fresh tree as of this writing). No runtime validation is possible from this lane.

## 6. Remaining unknowns / next

* Outcome of the 060 handoff probes (A/B) - decides decompressor-side vs loader-side.
* Stub B/A marker values and SCU CONFIG bit5 after the probe image boots.
* Loader-side decode: why/hw CPU1 is not started for our image (stock DT tar,
  flashinfo/ATAG window, loader-region reads, U-Boot/hboot code).
* rstinfo encoding (0x02/0x42/0x0A samples) [U].
* W9 net TX oops (net lane).

## 7. Integration notes

1. Patch 060 is installed in patches-6.18/; 033/034/035/037/039/042/059 unchanged.
2. Apply the shared documentation patch from the opensource/ root:
       cd opensource && git apply target-skeleton/docs/incoming/smp-decode/shared.patch
   It appends section 22 to docs/SMP-RE.md and changes nothing else.
3. Bench token for the 060 image: luofu_smp_handoff_probe=1 (and the usual tokens).
4. Evidence: build/linux-kbuild/waves/2026-11-19-smp-fix/ (patch sources, probe
   assembler check, clean build logs, EVIDENCE-SHA256SUMS.txt).
