# SMP-RE: the vendor enable-method "hisilicon,hsan_smp" - decode, design and staged plan

Status: **2026-11-18, host-only research + draft code. No boot of our kernel, no SMP
release attempted.** A read-only inventory of the stock vendor boot on the same day did read the
live SYSCTRL pen word back (0x10100c00 = 0x80609680) and the live `/proc/cmdline`; those two
measured data points are recorded in sections 2, 3.4/3.5 and 10, and in
[INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 5. They confirm the decode; they do not
run anything.
Scope: what the stock firmware private SMP enable-method actually does, what of it can
be recovered from local artifacts, the draft mainline-style implementation (patch
[018]), its dependencies and risks, and the staged plan from a mergeable UP-first
target to an attended SMP bring-up.

Evidence markers used everywhere in this document: **[V]** a value read from a local
artifact or a bench record that exists in this repository; **[I]** an interpretation
that follows from [V] facts but is not itself measured; **[U]** an open question that
needs an attended measurement or a document that does not exist locally; **[X]** a
claim that the local evidence contradicts. The extraction and hashes behind every [V]
in this document are in the local evidence bundle
build/linux-kbuild/waves/upstream-6.18-smp-20261118/ (gitignored); section 2 below lists
the exact commands and every value quoted.

---

## 1. The question, and the answer in one paragraph

The board devicetree says:

    cpus { enable-method = "hisilicon,hsan_smp"; ... };

and no mainline ARM kernel has a CPU method of that name, so the current draft target
boots UP and the repository has carried an explicit TODO for the stage-1 SMP port.
The vendor enable-method is a **pen-release**: the boot CPU writes the physical entry
address of the secondary kernel entry point into a SYSCTRL word (SYSCTRL + 0xc00),
clears two "resume" bits that belong to the target core in a CRG word (CRG + 0x38, bit
pairs from the vendor DT core-info property), and sends the ARM wakeup IPI. There is
**no SMC, no PSCI, no secure-monitor call in the Linux-side release path**; the vendor
kernel is a self-contained pen + reset-bit + IPI implementation. That much is decoded
[V] from the two stock vendor vmlinux images and their base devicetree. What remains
open [U] is exactly the hardware semantics around the write (which unit consumes the
pen word, what the CRG bits physically gate, and whether the bootloader has already
enabled the SCU) - i.e. the part that only an attended boot can settle. This document
therefore delivers the decoded spec plus a fail-safe platsmp port and keeps the actual
SMP claim behind the attended-bring-up gate.

---

## 2. How this was decoded (all host-side, reproducible)

The local source of truth is the **stock vendor kernel as it sits in the dumped kernel
slot**, plus the vendor base DTB that the stock loader hands to it:

| item | source | size / identity |
| --- | --- | --- |
| stock kernel slot A | dumps/router-20261108/full/mtd11-kernela.bin | 8,650,752 B, sha256 6c7c0784... |
| vendor kernel image | uImage inside the slot, payload at file offset 0x40 (zImage, XZ stream at payload offset 0x3cf7) | vmlinux 12,207,156 B, sha256 779f7b39... |
| vendor base DTB | follows the uImage at file offset 0x418610 | 31,220 B, sha256 947ec62d... |
| other stock release | firmware/WR3000V2-R116-2.5.24-...sysupgrade.bin, uImage at 0x80e5c | vmlinux also 12,207,156 B, sha256 27eda71b... |
| stock boot console | build/uart-logs/20261107T191003Z-poweron-uboot.log | vendor kernel, 2 CPUs (section 4.5) |

Independent verification points make this a strong basis [V]:

* The 2.5.24 kernel is a *different build* of the same design (same decompressed size
  and same 76,769 recovered symbols, different bytes and shifted function addresses),
  and its hsan_boot_secondary is instruction-for-instruction identical to the 2.4.15
  one after stripping absolute branch targets. The design is not a one-off.
* The base DTB hash matches the one an independent 2026-11-18 lane cited
  (MISC-SOC-BLOCKS.md section 12), i.e. two separate reads of the same dump agree.
* A read-only inventory of the *running* stock vendor system on 2026-11-18 read the live
  SYSCTRL+0xc00 pen word as **0x80609680** - exactly the value the decoded
  `__pa_symbol(secondary_startup)` expression computes for the vendor kernel - and read the live
  `/proc/cmdline` as `noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2
  additional_cpus=1`. Both are [V] measured on that boot (evidence
  `parent:build/bench-inventory/20261118T130512Z-inv1-run/`; analysis
  [INVENTORY-2026-11-18.md](INVENTORY-2026-11-18.md) section 5).

The vendor vmlinux is a raw (uncompressed) ARM kernel image; it has no ELF headers, so
the analysis needed the **kallsyms table** to be recovered from the image:

1. the uImage payload is a self-decompressing zImage; the XZ stream at payload offset
   0x3cf7 decompresses to the raw 12,207,156-byte vmlinux (xz --single-stream -dc);
2. the kallsyms anchor is the relative_base word 0xc0008000 followed by
   num_syms = 76769 at image offset 0x80e460; the 5.10 kallsyms layout follows it
   (offsets array, relative base, num_syms, compressed names, markers, token table,
   token index, all labels 4-byte aligned), and decoding it yields a complete symbol
   table (hi_arch_init_early, hsan_boot_secondary, hsan_smp_ops,
   __cpu_method_of_table_hsan_smp, the early-initcall array, ...);
3. the two kernel functions are then disassembled with the project own ARM toolchain
   and cross-checked against the vendor DT and the stock boot log.

The analysis scripts and the full symbol tables are kept with the local evidence bundle
(not in this repository): kallsyms3.py, dumpmem.py, scan_refs.py, kallsyms-vendor.txt,
vendor-full.dis, 00-raw-dumps.txt, 00-vendor-dtb-smp.txt.

**What the local modules add: nothing.** SMP bring-up is built-in machine code in
5.10.201; none of the 58 vendor hi_*.ko modules contains an SMP or platsmp path (they
are drivers). The useful artifacts are the two vmlinux images, the vendor base DTB and
the stock UART capture. The vendor U-Boot blobs are only a lead: uboota.bin contains
one early data table at file offset 0x5e0 that holds 0x10100c00 (the pen word),
0x14880050 and 0x10180000 (the SCU base), consistent with a bootloader/ROM-side role in
the release, but no instruction-level consumer has been decoded [U].

---

## 3. What the vendor method actually is

### 3.1 Registration

The vendor arch/arm/mach-hisi/platsmp.c (their file path is in the image strings)
registers a fourth entry in the standard CPU-method table, next to the three mainline
Hisilicon methods:

    __cpu_method_of_table (vendor vmlinux, VA 0xC0A1DBE8):
      hip01_smp   "hisilicon,hip01-smp"     -> hip01_smp_ops
      hix5hd2_smp "hisilicon,hix5hd2-smp"   -> hix5hd2_smp_ops
      hi3xxx_smp  "hisilicon,hi3620-smp"    -> hi3xxx_smp_ops
      hsan_smp    "hisilicon,hsan_smp"      -> hsan_smp_ops     <- this board

and the machine descriptor __mach_desc_HSAN has .smp = NULL, so the DT
/cpus/enable-method string is what selects it (the normal ARM DT CPU-method path). The
vendor machine dt_compat list is {hsan-xiling, hsan-luofu, hsan-baiyun, hsan-emei,
hsan,tiangong0/1/2} [V].

### 3.2 The operations table has only one operation

hsan_smp_ops is a 4-pointer table, i.e. the vendor kernel was built with
CONFIG_HOTPLUG_CPU=n (the hotplug fields do not exist in the struct):

| field | value |
| --- | --- |
| smp_init_cpus | NULL |
| smp_prepare_cpus | NULL |
| smp_secondary_init | NULL |
| smp_boot_secondary | 0xC075525C hsan_boot_secondary |

Consequences, all [V] from the table plus the kernel sources:

* the vendor method does **not** enable the Cortex-A9 SCU. Nothing in the method calls
  scu_enable(); the only scu_enable caller in that kernel is the mainline
  hisi_enable_scu_a9(), reachable only from hi3xxx_smp_prepare_cpus(), which this
  board never calls. Either the bootloader leaves the SCU enabled or the vendor relies
  on something else; that is [U].
* there is no per-core secondary init and no CPU hotplug/offline support. A port must
  not advertise .cpu_die/.cpu_kill, and CONFIG_HOTPLUG_CPU should stay off for this
  platform until CPU-offline is understood.
* the actual release is therefore one function, and it needs two register pages that
  the vendor machine early_initcall prepared before smp_init() runs.

### 3.3 Register preparation - hi_arch_init_early

The vendor registers hi_arch_init_early as an **early_initcall** (its entry is in the
pre-SMP early-initcall array, i.e. it runs before smp_init() releases CPU1). It parses
the vendor DT and fills one static array (VA 0xC0BAD420, symbol g_hsan_mach_addr) [V]:

| index | DT source | value | used by |
| --- | --- | --- | --- |
| [0] | hsan,sysctrl reg ioremap | SYSCTRL base 0x10100000 | - |
| [1] | hsan,crg reg ioremap | CRG base 0x14880000 | - |
| [2] | CRG + reboot-offset = 0x00 | restart command word | machine restart (patch 010 path) |
| [3] | CRG + resume-offset = **0x38** | **CPU resume register** | **SMP release** |
| [4] | SYSCTRL + smp-offset = **0xc00** | **CPU pen / jump word** | **SMP release** |
| [5],[6] | softrst_val0 = 0x51162100, softrst_val1 = 0xaee9deff | restart magic words | machine restart (patch 010 path) |
| [7] | hsan,rstinfo record_addr = 0x10100c10 (ioremap 4) | reset record | machine restart (patch 010 path) |
| [8..11] | CRG core-info = <0x16 0x18 0x17 0x19> | per-CPU resume bits | **SMP release** |

Vendor base-DTB nodes, verbatim [V]:

    cpus { enable-method = "hisilicon,hsan_smp"; ... };
    crg@14880000 { compatible = "hsan,crg"; reg = <0x14880000 0x1000>;
                   resume-offset = <0x38>; reboot-offset = <0x00>;
                   reboot-base-index = <0x00>;
                   core-info = <0x16 0x18 0x17 0x19>;
                   softrst_val0 = <0x51162100>; softrst_val1 = <0xaee9deff>; };
    sysctrl: system-controller@10100000 { compatible = "hsan,sysctrl", "syscon";
                   reg = <0x10100000 0x1000>; smp-offset = <0xc00>; };
    rstinfo@0x14880000 { compatible = "hsan,rstinfo"; reg = <0x14880000 0x1000>;
                   reg-offset = <0x90>; record_addr = <0x10100c10>; ... };

Note that resume-offset is the SMP resume word, not a "soft-reset magic word": the two
softrst_val* cells are written by the vendor machine restart into CRG + reboot-offset
(that is patch 010 path), while the SMP release only reads and modifies the word at
CRG + 0x38 [V].

### 3.4 The release - hsan_boot_secondary

Reconstructed from the disassembly (the hsan_smp_ops.smp_boot_secondary target;
identical body in both stock kernels) [V]:

    static int hsan_boot_secondary(unsigned int cpu, struct task_struct *idle)
    {
            u32 val, bits;
            unsigned int bit0, bit1;

            if (cpu_logical_map(cpu) != 0 && g_hsan_mach_addr[4])
                    writel(__pa_symbol(secondary_startup), g_hsan_mach_addr[4]);

            if (cpu <= 1) {
                    bit0 = g_hsan_mach_addr[8 + cpu * 2 + 0];  /* core-info[2c]   */
                    bit1 = g_hsan_mach_addr[8 + cpu * 2 + 1];  /* core-info[2c+1] */
                    val  = readl(g_hsan_mach_addr[3]);         /* CRG + 0x38      */
                    bits = (1U << bit0) | (1U << bit1);
                    writel(val & ~bits, g_hsan_mach_addr[3]);
            }

            arch_send_wakeup_ipi_mask(cpumask_of(cpu));  /* IPI_WAKEUP = GIC SGI 1 */
            return 0;
    }

Facts that matter for a port:

* The pen value is __pa_symbol(secondary_startup), i.e. a computed physical address,
  not a magic constant. In both stock kernels it resolves to 0x80609680 because the
  vendor kernel executes at physical 0x80608000 (PHYS_OFFSET 0x80600000, TEXT_OFFSET
  0x8000). **Measured 2026-11-18 [V]:** the live pen word on the stock boot is
  0x80609680, so the decoded expression is confirmed against the running vendor kernel.
  Our own kernel reaches the same physical base through the TEXT_OFFSET = 0x00608000 scheme, so
  the same expression should yield 0x80609680 there too - but only our first fire can read our
  kernel's value back and verify it [U].
* The release is **ordered**: pen write, then resume-bit clear, then wakeup IPI.
* Only bits 0x16/0x18 (CPU0) and 0x17/0x19 (CPU1) of the resume word are cleared; the
  rest of the word is preserved (read-modify-write, 32-bit accesses).
* Only CPUs 0 and 1 exist in this method; the SoC has exactly two Cortex-A9 cores.
* There is no SMC/HVC anywhere in the function, no cache maintenance and no barrier
  beyond what writel and the IPI send provide.

### 3.5 What the stock system proves

The stock vendor kernel really does bring up both cores on this board [V]
(build/uart-logs/20261107T191003Z-poweron-uboot.log):

    [    0.180432] CPU1: thread -1, cpu 1, socket 0, mpidr 80000001
    [    0.180441] CPU1: Spectre v2: using BPIALL workaround
    [    0.191232] smp: Brought up 1 node, 2 CPUs
    [    0.195339] SMP: Total of 2 processors activated (3987.86 BogoMIPS).

That is the target state for our own kernel, and it is also the evidence that the
sequence above is the working one and not a dead branch of the vendor machine code.

The 2026-11-18 read-only inventory adds two live confirmations on the same stock boot
(no boot of our kernel): the SYSCTRL pen word read **0x80609680** [V], and the live
`/proc/cmdline` carries `maxcpus=2 nr_cpus=2 additional_cpus=1` [V] - the loader does pass the
2-CPU bootargs through to the vendor kernel, and the box really is running a 2-CPU
configuration at the time of the read. Whether our kernel receives the same bootargs from the
stock loader is still an attended-boot question (U10).

---

## 4. Evidence ledger: [V] decoded, [I] inferred, [U] open, [X] contradicted

| claim | grade | basis |
| --- | --- | --- |
| the vendor method name is hisilicon,hsan_smp and it is selected from the vendor DT | [V] | vendor base DTB; __cpu_method_of_table entry; machine descriptor .smp = NULL |
| the method is implemented in the vendor arch/arm/mach-hisi/platsmp.c, appended to the mainline Hisilicon method table | [V] | image string arch/arm/mach-hisi/platsmp.c; four-entry method table; recovered symbols |
| the ops table contains only smp_boot_secondary; no prepare/init/hotplug | [V] | 16-byte hsan_smp_ops with three NULL slots; no hotplug symbols in kallsyms |
| release = pen write (SYSCTRL+0xc00) + resume-bit clear (CRG+0x38, bits 0x16/0x18/0x17/0x19) + IPI_WAKEUP | [V] | hsan_boot_secondary disassembly; vendor DT properties |
| the pen value is __pa_symbol(secondary_startup) | [V] | instruction sequence at 0xC0755288..0xC0755298 (computed 0x80609680) |
| the **live** pen word is 0x80609680 on the stock boot | [V] | 2026-11-18 read-only inventory, SYSCTRL+0xc00, repeat=2 identical; this matches the computed value - the decode is confirmed against hardware (the consumer/physical semantics remain [U]) |
| the stock loader passes `maxcpus=2 nr_cpus=2 additional_cpus=1` to the vendor kernel | [V] | 2026-11-18 read-only inventory, live /proc/cmdline; matches the stock boot log's 2-CPU bring-up. Our-kernel delivery still [U] |
| the vendor brings both cores up on this board | [V] | stock UART capture above |
| the same design is in both stock releases (2.4.15 and 2.5.24) | [V] | two extracted vmlinux images; identical function body after address stripping |
| ***no SMC in the release path*** | [V]/[X] | no SMC/HVC/secure-monitor instruction in hsan_boot_secondary; the repository earlier "pen/smc" phrasing is not supported - "pen" [V], "SMC" [X] |
| the SCU is enabled by the bootloader, not by Linux | [I] | the vendor method never calls scu_enable(); a working second core needs the SCU - either the loader enabled it or something not in this method did [U] |
| the pen word is consumed by the bootloader/ROM secondary stub | [I] | self-consistent with the pen+reset-bit+IPI order; the U-Boot blobs contain the word in an early data table; no instruction-level consumer decoded [U] |
| the CRG resume bits gate core clock/reset/power state | [I] | the vendor property is named "resume" and the method clears exactly two bits per core; the physical function of the bits is [U] |
| our platsmp port needs a prepare hook to enable the SCU | [I] | mainline precedent (hisi_enable_scu_a9); safe and testable on the first attended boot |
| SMP is a hard prerequisite for the first upstream submission | [I] | no: SoC platform, NAND, console and wired network can be submitted in a UP-first form; SMP is a quality step, not an acceptance requirement |

---

## 5. The draft implementation (patch 018)

### 5.1 Method name and registration

New file arch/arm/mach-hisilicon/platsmp.c, built as
obj-$(CONFIG_SMP) += platsmp.o, with:

    static const struct smp_operations luofu_smp_ops __initconst = {
            .smp_prepare_cpus   = luofu_smp_prepare_cpus,
            .smp_boot_secondary = luofu_smp_boot_secondary,
    };

    CPU_METHOD_OF_DECLARE(luofu_smp, "hisilicon,luofu-smp", &luofu_smp_ops);

The DT method name is **hisilicon,luofu-smp**, not the vendor string. Reasons: the
vendor string names the vendor internal platform ("hsan" is their board family, not a
SoC binding), it is vendor-private and undocumented, and this repository documented
naming convention for the SoC blocks is hisilicon,luofu-* (see the CRG, watchdog, FMC,
MDIO, GEMAC, PCIe, thermal, pinctrl, PIE, switch and selector patches). A DT that still
carries the vendor string simply does not match and keeps booting UP - fail-safe by
construction.

### 5.2 Operations

luofu_smp_prepare_cpus():

1. enable the Cortex-A9 SCU exactly like mainline mach-hisi does (scu_a9_has_base(),
   scu_a9_get_base(), ioremap(SZ_4K), scu_enable(), iounmap()); the vendor method does
   not do this, so the port adds it explicitly instead of relying on the loader [I];
2. locate and map the two pages by compatible: hisilicon,luofu-sysctrl (page
   0x10100000, already a syscon node from patch 010) and hisilicon,luofu-crg (page
   0x14880000, the syscon/simple-mfd node from patch 004), then store
   pen = sysctrl + 0xc00 and resume = crg + 0x38.

luofu_smp_boot_secondary():

1. if either page could not be mapped, return -ENODEV (the boot continues UP - the
   method never writes a guessed address);
2. reject a CPU index for which the port has no resume-bit pair (-EINVAL);
3. write __pa_symbol(secondary_startup) to the pen word (vendor condition: non-zero
   logical CPU);
4. read-modify-write the CRG resume word, clearing BIT(bit0) | BIT(bit1) from the
   driver-data table;
5. arch_send_wakeup_ipi_mask(cpumask_of(cpu)).

The port deliberately has **no** .smp_init_cpus, .smp_secondary_init (the 6.18 generic
secondary path covers what is needed) and no hotplug operations; there is no CPU
offline support, matching the vendor table (section 3.2).

### 5.3 Where the values live

* The register **offsets** (pen 0xc00, resume 0x38) and the **core-resume bit numbers**
  (0x16/0x18 for CPU0, 0x17/0x19 for CPU1) are SoC facts and are **driver data in
  platsmp.c**, each with the vendor-property citation:
  - #define LUOFU_SMP_PEN_OFFSET 0x0c00   (vendor DT smp-offset)
  - #define LUOFU_CRG_RESUME_OFFSET 0x38  (vendor DT resume-offset)
  - static const u8 luofu_resume_bits[][2] (vendor DT core-info)
* That is deliberate: the vendor numeric properties are not exposed as a new DT
  binding surface. The two MMIO pages are referenced by their existing syscon node
  compatibles, so no new phandle, no new YAML and no overlap with the syscon/driver
  regmaps.
* If an attended measurement ever shows board-to-board variance in those values, the
  correct evolution is to promote them into the existing sysctrl/CRG bindings (with a
  YAML update and the evidence attached), not to hardcode new numbers blindly.

### 5.4 DT shape

One property on the existing node:

    cpus {
            #address-cells = <1>;
            #size-cells = <0>;
            enable-method = "hisilicon,luofu-smp";

            cpu@0 { ... };
            cpu@1 { ... };
    };

and the two syscon nodes must exist (they do; patches 004 and 010):

    sysctrl: system-controller@10100000 {
            compatible = "hisilicon,luofu-sysctrl", "syscon";
            reg = <0x10100000 0x1000>;
    };

    crg: crg@14880000 { compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"; ... };

No other DT change is required by the implementation. The enable-method line is shipped
in the incoming integration patch (section 8) and is the single-line switch that turns
the release on; without it the board boots UP, which is the staged fallback.

### 5.5 Why not the vendor shape

* Reusing "hisilicon,hsan_smp" would put a vendor-private string into a documented
  target and freeze their internal board-family name into an ABI. [X] reject.
* Exposing vendor properties smp-offset/resume-offset/core-info on the new
  hisilicon,luofu-* bindings would add undocumented, unmeasured properties to bindings
  that are otherwise review-ready. The values are needed, so they become driver data
  (the same pattern the CRG gate table and the FMC register data already use).
* A PSCI description is impossible: the vendor kernel is not a PSCI client and has no
  psci node; inventing one would be a fiction. [X] reject.
* A dedicated new hisilicon,luofu-smp node with its own reg is unnecessary and would
  create a second claim on the SYSCTRL/CRG pages that already have syscon nodes.

---

## 6. Dependencies

| dependency | state | consequence for SMP |
| --- | --- | --- |
| SYSCTRL page (0x10100000) node | hisilicon,luofu-sysctrl syscon exists (patch 010) | pen word at +0xc00; if the node is absent the port stays inert |
| CRG page (0x14880000) node | hisilicon,luofu-crg syscon/simple-mfd exists (patch 004) | resume word at +0x38; same fail-safe |
| Cortex-A9 SCU | CONFIG_HAVE_ARM_SCU selected by ARCH_LUOFU when SMP (patch 001 Kconfig); the target kernel config already resolves CONFIG_SMP=y, HAVE_ARM_SCU=y, HAVE_ARM_TWD=y (validated, section 12) | the port enables the SCU explicitly; if the loader already did, scu_enable() is harmless |
| GIC | arm,cortex-a9-gic DT node + irqchip; 6.18 sets up the IPI range via set_smp_ipi_range() | IPI_WAKEUP must be routable to the secondary; a wrong GIC CPU-interface setup shows up as a hung CPU1 |
| TWD timer | arm,cortex-a9-twd-timer node with twd_clk (fixed-factor from the corrector clock) | a wrong TWD clock leaves CPU1 with a broken local timer (boot may still complete, then fail late) |
| PL310 L2 / AUX | machine descriptor l2c_aux_val/l2c_aux_mask, PL310 node | the cache must already be coherent for the released core; the vendor early hook replays the AUX value from DT, our machine does it statically |
| CPU clocks / OPP / PLL | vendor CPU nodes carry clocks/OPP/nvmem, but the release path does **not** touch them; the port does not model them either | the CPU frequency at boot is whatever the loader/CRG leaves; a later cpufreq/OPP driver is independent work |
| kernel physical load address | TEXT_OFFSET 0x00608000 (patches 002/003): kernel executes at 0x80608000, same physical base as the vendor kernel | __pa_symbol(secondary_startup) must resolve to the actual physical address; attended boot should read the pen word back and compare with the expectation derived from System.map |
| bootloader secondary stub | [U] the pen word consumer is not decoded | if the loader does not implement it, the release silently fails (CPU1 never comes up) - detect via failed-to-boot/boot timeout, not via a crash |
| secure world | [U] whether a secure monitor traps the SYSCTRL/CRG writes | the first attended run is the only way to tell |

---

## 7. Risks and mitigations

1. **Second-core hang (highest).** If the release sequence is subtly wrong for our
   kernel (wrong pen value, wrong resume bits, wrong order, secure-world trap), CPU1 may
   start executing garbage or never come up. Mitigations: the release is fail-safe when
   the DT pages are missing; for the attended run keep the recovery door armed and the
   UART capture read-only; first run with the release compiled in but the DT method
   absent (UP boot proves the machine/DT), then one single-variable fire with the method
   enabled. A wrong entry address is a CPU1 crash, not a CPU0 crash; CPU0 keeps
   running, which makes the failure observable.
2. **Cache/coherency.** Releasing CPU1 with the SCU not enabled would run CPU1
   non-coherent and corrupt memory. The port enables the SCU before the release; the
   first attended run must confirm the SCU control register state (read-only) and that
   CPU1 enters through the normal secondary_startup (not a secure-monitor trampoline).
3. **GIC/TWD setup.** CPU1 needs its GIC CPU interface and TWD local timer; both are
   generic 6.18 paths once the CPU is online. A wrong twd_clk or a missing TWD node
   produces a late failure, so the attended acceptance must include a soak test and
   /proc/interrupts per-CPU sanity.
4. **Register read-modify-write on a shared word.** CRG+0x38 may be touched by other
   consumers (the vendor restart path clears/sets unrelated CRG words, and the clock/
   reset drivers own different offsets). The port does a single RMW of the whole word in
   the boot_secondary path, which the generic boot path serialises per CPU; still, the
   first attended run should capture the word before/after (read-only) and confirm only
   the two expected bits changed.
5. **No CPU hotplug / offline.** CONFIG_HOTPLUG_CPU should stay off for this platform;
   with hotplug enabled the generic hotplug path would see NULL ops and refuse CPU
   offline, but the DT/CPU topology may still advertise offline-capable CPUs. Document
   that CPU offline is unsupported (vendor table has no fields for it).
6. **UP-first instability if the method is missing.** With two cpu@ nodes in the DT and
   no matching enable-method, 6.18 marks both CPUs possible and then cannot boot CPU1;
   this is exactly the situation the bench line hit on 5.10, where a kernel built
   without smp_ops and booted with nr_cpus=2 faulted in the scheduler some 14 s into
   userspace, while maxcpus=1 was stable [V] (parent ledger 2026-10-12, phases 81/82).
   Consequences for staging: an UP-first *submission* should either ship a single cpu@0
   node or carry maxcpus=1 in the bootargs until the release is verified; the current
   draft target instead ships the enable-method, so this failure mode is designed out.
7. **Vendor-property drift.** If a future vendor build changes the pen/resume values,
   the driver data would be wrong. The stock 2.4.15 and 2.5.24 builds agree today [V];
   the attended bring-up should capture the vendor DT values and the register readback
   so the constants can be confirmed against the board.
8. **Documentation/claim drift.** The repository previously described the path as
   "pen/smc" and the TR6560 BSP as the reference. The SMC half is contradicted by the
   decoded method [X]; the TR6560 BSP remains only a *code-shape* reference (it has a
   real CPU_METHOD_OF_DECLARE platsmp with different bindings; its source is not in this
   repository). Do not cite it as the register contract.

---

## 8. Staged plan (spec -> UP-first mergeable -> attended bring-up)

| stage | deliverable | what it proves | gates |
| --- | --- | --- | --- |
| **S0 - spec (this document + patch 018 draft + validation, host-only)** | decoded register/sequence spec; arch/arm/mach-hisilicon/platsmp.c; machine Kconfig/Makefile/descriptor refactor; DTS enable-method in the integration patch; full-series validation | that the port builds, the DT is schema-clean and the evidence is reproducible | SMP-RE.md reviewed; series 001-018 applies/compiles; no hardware claim |
| **S1 - UP-first mergeable refactor** | the same patch 018, shipped **without** the DTS enable-method hunk (and, for an upstream submission, with a single cpu@0 node); CONFIG_SMP=y with an unmatched method leaves the boot UP | that the machine/DT/platform code is reviewable and safe with no SMP claim | UP boot on the attended board (one fire) with the method absent; dmesg must not try to bring up CPU1 |
| **S2 - attended SMP bring-up** | the one-line DT enable-method + cpu@1; a gated fire sequence | that the decoded release actually works on our kernel | single-variable fires, recovery door armed, UART read-only capture; acceptance below |
| **S3 - upstream** | patch 018 + the DTS change in the submission series, with this evidence summary | reviewer-facing SMP support | S2 acceptance + review |

The current tree state: patch 018 is in the series and the DTS enable-method is in the
incoming shared patch, i.e. the tree describes the S2 end state. The hunk is a single
line and can be dropped for an S1 submission without touching the driver (the driver
stays inert without it).

**S2 acceptance (attended, all required):**

* boot log shows the release: no CPU1 timeout, and "smp: Brought up ... 2 CPUs";
* /proc/cpuinfo lists both processors; /sys/devices/system/cpu/cpu1/online exists;
* /proc/interrupts shows per-CPU timer/IPI activity on CPU1;
* a >= 10 minute soak with the box reachable (no scheduler/RCU stall, no oops);
* read-back of the pen word and the resume word before/after the release, captured to
  the evidence bundle;
* the same fire repeated once from a cold boot (a single sample is not evidence);
* recovery path re-verified (the box must end on the known-good slot).

**S2 failure modes and what they mean:** CPU1 never online + failed-to-boot = the pen
consumer or the resume bits are wrong for our kernel; CPU1 online then hang = TWD/GIC/
SCU ordering; CPU0 crash during the release = the write landed on a wrong address (the
port only writes the two mapped pages, so this would mean a DT/mapping error, and the
fire must be reverted to the UP image).

---

## 9. Effort

* Decode + spec + draft patch + host validation (this lane): **~1.5 engineer-days**
  (one day of binary/symbol RE plus half a day of drafting/validation) - done.
* S1 UP-first integration/review polish: **0.5-1 day** (mostly the same file plus review
  text; no new dependencies).
* S2 attended bring-up: **one bench session (0.5-1 day)** including the recovery
  contingency; risk of a second session if the release is wrong.
* S3 upstream review iterations: **1-2 review rounds**; the code is small, the main
  review question will be "does it work" and "why driver data instead of DT
  properties" - both answerable with this document.

Compare with the other open blocks (LSW switch driver and PCIe are multi-week efforts;
this is a small, self-contained platform port).

---

## 10. Open questions ([U]) and how to close each

| # | question | close with |
| --- | --- | --- |
| U1 | physical semantics of CRG+0x38 bits 0x16..0x19 (clock gate? reset? power?) | read-only register census before/after a stock vendor boot; then before/after one own-kernel release |
| U2 | who consumes the pen word (bootloader stub, ROM WFI loop, secure firmware) | vendor U-Boot/bootram disassembly (local blobs) and one UART capture of a failed release |
| U3 | is the SCU enabled by the loader? | read SCU control register (read-only) on a stock boot and on our first boot |
| U4 | does our TEXT_OFFSET scheme give the vendor pen value 0x80609680? | **half closed 2026-11-18:** the vendor kernel's live pen word was measured as 0x80609680 (SYSCTRL+0xc00, stock boot), so the decoded expression and the vendor value agree. Remaining: our first fire must read our kernel's pen word back and compare with the expected __pa_symbol value |
| U5 | does the release work without an explicit secure-world handshake/any SMC? | the S2 fire itself (the decoded method contains no SMC; if it fails, look at secure firmware) |
| U6 | TWD clock on CPU1 and per-CPU GIC interface after release | /proc/interrupts, dmesg timer lines, soak |
| U7 | CPU hotplug/offline support | out of scope; if ever needed, re-RE the vendor die path (none exists in the stock table) |
| U8 | board-to-board variance of pen/resume bits | one attended boot on a second unit if/when available |
| U9 | OPP/cpufreq interaction with SMP | independent cpufreq work; not a release-path dependency |
| U10 | whether the vendor bootargs (maxcpus=2 nr_cpus=2 additional_cpus=1) reach our kernel | **half closed 2026-11-18:** the live `/proc/cmdline` on the stock boot carries them, so the loader does hand them to the vendor kernel. Remaining: one attended boot of our kernel to see whether the stock loader passes the same args (target-design 5.6) |

---

## 11. What this document does NOT claim

* No boot of this port, no bench, no fire. A **read-only** inventory of the stock vendor system on
  2026-11-18 did read the live pen word and `/proc/cmdline` back (section 2); that is a stock-boot
  measurement used to confirm the decode, not a run of the patch-018 port.
* No SMP claim for our kernel yet: the release is decoded from vendor binaries and the
  port is host-compiled only; the vendor pen word is measured, our kernel's is not.
* No upstream acceptance claim: patch 018 is a draft; the submission series in
  opensource/upstream/ still ends at 017 and this document does not edit it.
* No invented register values: every number in section 3 is cited to the vendor
  vmlinux/DT/boot log; interpretations are [I] and unknowns are [U].

---

## 12. Validation of the draft (host-only, 2026-11-18)

Isolated validation in a btrfs-reflink copy of the shared clone
(a copy of the 001-017 lane's clone; openwrt main
27663a0, linux-6.18.60, the tree own OpenWrt GCC 14.4.0). The shared clone was not
modified. **No device, router, UART, TFTP, flash or bench tool was touched.** Logs,
hashes and the intermediate trees are in the local evidence bundle
build/linux-kbuild/waves/upstream-6.18-smp-20261118/.

| check | result |
| --- | --- |
| patch 018 as delivered | 6,955 B, sha256 5509ca59...; new platsmp.c (159 lines) + Makefile + Kconfig + luofu.c comment |
| pristine 6.18.60 series apply | fresh linux-6.18.60 tree; patches 001-018 applied in order with patch -p1: RC=0, no .rej/.orig; only pre-existing offset adjustments in patch 008 (Kconfig/Makefile hunks, expected because that patch was generated against the OpenWrt-prepared tree); the resulting mach-hisilicon files are byte-identical to the validated series tree |
| patch 018 dry-run + apply | on a pristine+001-017 tree: patch -p1 --dry-run RC=0; real apply RC=0; result byte-identical to the committed series tree; git apply --check RC=0 |
| OpenWrt prepare, series 001-018 | make target/linux/prepare V=s: RC=0 (1,202 patching-file lines, no .rej/.orig) |
| full kernel compile | make target/linux/compile V=s -j$(nproc): RC=0, 20m06s; zImage 3,264,472 B sha256 ae59d998...; **0 warnings attributable to any new/changed file** (the 38 modpost "missing MODULE_DESCRIPTION" warnings are the exact same pre-existing baseline as the 001-017 lane and the restart lane) |
| new objects in the build | arch/arm/mach-hisilicon/platsmp.o compiled; symbols in System.map: luofu_smp_boot_secondary, luofu_smp_prepare_cpus, luofu_smp_ops, __cpu_method_of_table_luofu_smp, luofu_smp_pen/resume |
| TEXT_OFFSET unchanged | arch/arm/Makefile: textofs-$(CONFIG_ARCH_LUOFU) := 0x00608000 (unchanged); System.map: _text = stext = 0xc0608000 (unchanged) |
| SMP-related config | CONFIG_ARCH_LUOFU=y, CONFIG_SMP=y, CONFIG_HAVE_ARM_SCU=y, CONFIG_HAVE_ARM_TWD=y, CONFIG_ARM_CPU_TOPOLOGY=y; no config change is needed by the patch |
| config derivation idempotency | make kernel_oldconfig three runs, RC=0 each; the resulting kernel .config is byte-identical to the 001-017 lane build (sha256 1b9b084e...); the canonically re-derived target config-6.18 has the same 300-symbol set as the delivered fragment and is byte-idempotent across runs (sha256 4106877f...) |
| dt_binding_check | RC=0 for all 13 series YAMLs (dtschema 2026.9; yamllint unavailable - that one lint step is skipped, same caveat as the earlier lanes) |
| board DTS | repo DTS with the enable-method hunk: cpp RC=0; patched-kernel dtc RC=0, **zero warnings**; DTB 10,710 B sha256 bc599a4e... |
| DTS schema validation | dt-validate -s processed-schema.json: RC=0, only the same six pre-existing findings as the 001-017 lane (arm,sp804 compatible too short; two UART clock-names findings per UART); no finding names /cpus, enable-method or any node introduced by this patch |
| incoming shared.patch | git apply --check RC=0 and git apply RC=0 on a fresh git archive of the opensource HEAD; the four edited files byte-identical to the delivered copies; patch -p1 --dry-run RC=0; sha256 081c5987... |
| checkpatch | platsmp.c 0 errors / 0 warnings; Kconfig, Makefile, luofu.c 0/0 each; the bare patch file only reports the expected "missing commit description/Signed-off-by" metadata that the working patch format does not carry (the review-form submission copy adds those separately; 018 is not yet in that copy) |
| static safety scan | platsmp.c performs no register access at probe/prepare beyond ioremap; the only writes are the two vendor-decoded words in smp_boot_secondary; the fail-safe path returns -ENODEV and never writes a guessed value; no lab/bench breadcrumbs, no vendor strings, no private paths |

---

## 13. References

* vendor evidence bundle (local, gitignored): the artifacts listed in section 2 and
  01-RE-evidence.md in the same directory;
* ../target-skeleton/target/linux/hisilicon/patches-6.18/018-arm-mach-hisilicon-smp.patch -
  the draft implementation;
* ../target-skeleton/docs/incoming/smp/SUMMARY.md - the integration record and the
  incoming shared.patch (DTS enable-method + documentation updates);
* TARGET-DESIGN.md sections 6.2/7.1/8.5 (SMP rows), README.md (status),
  SKELETON-SMOKE.md (build validation history);
* MISC-SOC-BLOCKS.md section 6.3 - the SYSCTRL page cell census (0x10100c00 SMP release
  word was recorded there as [U]; this document closes the decode and leaves the
  physical semantics open).

---

## 14. Bench update (2026-11-19): the release runs, CPU1 still fails; patch 033

Status: **the decoded release has now run on the hardware** in the 2026-11-19 bench
sessions (gate images built from the series with patches 018/028/029). CPU1 does not
come online. This section records what the console proved, what that rules out, and
the discriminating experiment that patch 033 adds. Host-only work: no device action in
this session either; the measurements are quoted from the saved console captures in
build/evidence-bench-gates/2026-11-19/ (local, gitignored).

### 14.1 What the console measured [V]

    [    0.135401] smp: Bringing up secondary CPUs ...
    [    0.140325] luofu-smp: boot cpu=1 logical=1 pen=8070a000 resume 02a00000 -> 00200000
    [    1.190342] CPU1: failed to come online
    [    1.194237] smp: Brought up 1 node, 1 CPU

(the one-second gap is __cpu_up()'s wait_for_completion_timeout(); the UART bytes
between 0.14 s and 1.19 s belong to the boot CPU's own init, so the "failed to come
online" line is the only SMP result). Patch 028's diagnostic therefore proves:

* smp_prepare_cpus() ran: the SYSCTRL and CRG pages were mapped and the SCU path was
  taken (scu_a9_has_base() is true for a Cortex-A9 and the CBAR-based mapping did not
  fail; a failure prints "failed to map the SCU"); the fail-safe -ENODEV path was not
  taken [V];
* smp_boot_secondary() ran for cpu 1, logical 1 [V];
* the pen word reads back 0x8070a000 and the CRG resume word reads 0x02a00000 before
  and 0x00200000 after the read-modify-write [V];
* the wakeup IPI call was reached (the release path does not branch around it) [V];
* clearing all four resume bits instead of the vendor's per-core pair (patch 029) did
  not change the result [V].

### 14.2 The pen value is correct for our image layout [V]

The value is __pa_symbol(secondary_startup) using the *runtime* phys-virt offset
(CONFIG_ARM_PATCH_PHYS_VIRT). Two independent facts pin the decompressed image base:

* the kernel prints "Setting up static identity map for 0x80700000 - 0x8070003c" for
  __idmap_text_start (0xc0700000 virtual), i.e. __pa() = virtual - 0x40000000, so
  PHYS_OFFSET = 0x80000000 and __pa(_text) = 0x80608000 (the runtime PHYS_OFFSET is
  recomputed by head.S from the actual load PC and must be 16 MiB aligned; 0x80000000
  is the only value consistent with the boot);
* System.map: _text = stext = 0xc0608000, secondary_startup = 0xc070a000, i.e. the
  symbol sits at _text + 0x102000. In the decompressed binary the image base is
  0x80608000 and secondary_startup is therefore at 0x8070a000 - exactly the pen value.

The 0x102000 offset is not a relocation bug: the 6.18 link places .head.text (0x290
bytes) first and then aligns .text to a 1 MiB section boundary because
CONFIG_STRICT_KERNEL_RWX is on; the vendor 5.10 kernel has .text immediately after
.head.text, which is why its secondary_startup is at _text + 0x1680 (pen 0x80609680 on
a vendor PHYS_OFFSET of 0x80600000). The pen value is the physical address of the
entry in both kernels; the difference is layout, not semantics [V].

### 14.3 What the stock loader side contributes [V]

A host-side disassembly of the stock U-Boot (mtd1-uboota.bin, offsets behind the
TRAILER-FORMAT decode) shows the secondary-core contract directly:

* the early start code reads MPIDR; the non-boot core invalidates its caches and then
  polls SYSCTRL + 0xc00 in a tight ldr/cmp/beq loop and, on a non-zero value, branches
  to it with MMU and caches off ("mov pc, r7", with the literal 0x10100c00 at
  file offset 0x5e0) [V];
* the boot core's path writes 0 to the same word (a stale pen cannot survive boot),
  writes 0x30003c00 to CRG + 0x50 (the watchdog "set" word, per the vendor DT) and
  disables/invalidates/re-enables the SCU at 0x10180000 with the SCU control bit and
  the invalidate-all register [V].

That changes the discrimination: the pen consumer we must satisfy is that poll loop,
which runs with the MMU and caches off, so the target address only has to be valid
memory (it is) and the write only has to be visible (a device write followed by dsb
is; patch 033 adds the explicit barrier the vendor's writel() already had) [I]. The
wakeup IPI cannot be what releases a core that is already polling memory, so a silent
CPU1 means one of: the core was not in that loop at release time (reset/power state,
or a loader path that did not reach it), the write did not reach the register the core
polls, or the core branched into the kernel entry and died before its first print [U].

### 14.4 Patch 033: the discriminating experiment

Patch 033 keeps the decoded release as the default (per-core resume-bit pair - the
patch-029 all-four-bits variant becomes opt-in - and writel()/dsb ordering) and adds
attended-only instrumentation, all off by default:

| parameter | effect |
| --- | --- |
| luofu_smp_diag=1 | prints the SCU CBAR/CBCTRL/CBCONFIG/CPU-status bytes at prepare time, the physical pen/resume addresses and the entry address, then re-reads pen/resume/SCU 1 ms and 21 ms after the IPI |
| luofu_smp_probe=1 | writes the pen as the address of a boot-built position-independent stub that polls the console UART, prints the low MPIDR nibble and "P", then branches to __pa_symbol(secondary_startup); "1P" on the console means CPU1 consumed the pen and reached the stub |
| luofu_smp_power=1 | sets the target core's SCU power status to NORMAL before the release (tests the A9 power-mode path) |
| luofu_smp_allbits=1 | the pre-033 behaviour: clear all four resume bits |
| luofu_smp_force_pen=<pa> | bench-only pen override, no rebuild needed for an address experiment |
| luofu_smp_retries=N | repeats pen + resume + IPI up to N times |

The probe stub is the decisive measurement: it separates "CPU1 never consumed the
pen" (no console output) from "CPU1 consumes the pen but the kernel entry fails"
("1P" printed, CPU1 still offline). Both outcomes are actionable in the next attended
session without another guess:

| observation | interpretation | next attended step |
| --- | --- | --- |
| "1P" printed, CPU1 online | the entry works through the stub; the direct entry needs a closer look (cache-cleaned stub vs decompressed text; instruction-fetch/coherency) | keep the stub, diff its state against the direct jump (SCTLR/cache state), fold a minimal cache clean into the release path if it reproduces |
| "1P" printed, CPU1 still offline | CPU1 consumes the pen and dies in the 6.18 secondary entry (page tables/idmap/secondary_data) | instrument __secondary_switched (a second stub that prints before/after __enable_mmu), compare against the vendor 5.10 entry; the layout difference at 0x102000 makes this the next suspect |
| no output, SCU CPU status changes after the release | the core left its old power state but did not run the stub (or the UART write failed) | inspect the CRG resume-bit semantics (which bits are reset vs clock/power), try power=1 and retries |
| no output, SCU CPU status unchanged (2 = dormant, 3 = power-down) | the core is not powered/released by the decoded pair of bits at all | measure the CRG page around 0x38 on the stock boot, look for a per-core power/clock gate the loader touches, and gated-write the exact vendor sequence only after that measurement |
| no output, status NORMAL | the core is powered but not polling the pen (loader did not park it there on this boot path) | check the loader path for this boot (cold vs warm reset) and whether the U-Boot secondary stub is entered at all; this is the case where the IPI/SEV and the CRG bits are the only remaining levers |

### 14.5 What patch 033 does NOT establish

* Nothing in this section ran on hardware: 033 is compiled and series-validated only.
* The SCU CPU-status and CBAR readings have never been taken on this board; the
  SCU-enable path is believed to be a no-op because the decoded U-Boot enables the SCU
  first, but that is an inference from the U-Boot disassembly [I].
* The physical function of CRG + 0x38 bits 0x16..0x19 is still [U]; the vendor
  property name "resume" is still the only description.
* The vendor's pen write is the same physical-address value; there is no evidence that
  any vendor-specific address encoding is missing [V negative: the value is a plain
  __pa_symbol()].


---

## 15. W7 run (2026-11-19): the probe was never enabled; SCU status decoded; patch 034

Status: second hardware run of this lane. Image 14791958 (series 001-033, bench
command line). **The run did not execute patch 033's probe stub**: the knobs were on
the command line under the wrong names. This section corrects the record, decodes the
one new measurement (SCU CPU status 0x00), lists the stock reference state, and
delivers the next attended variant (patch 034).

### 15.1 The probe stub was not enabled [V]

The boot log of image 14791958 contains:

    Kernel command line: ... luofu_smp_diag=1 luofu_smp_probe=1
    Unknown kernel command line parameters "... luofu_smp_diag=1 luofu_smp_probe=1",
    will be passed to user space.

    [    0.140670] luofu-smp: boot cpu=1 logical=1 pen=0x8070a000 resume 02a00000 (mask 02800000) scu_status=00
    [    1.190448] CPU1: failed to come online
    [    1.194319] smp: Brought up 1 node, 1 CPU

Patch 033 registered its knobs with module_param(). For an object that is linked into
the kernel image (not a module) include/linux/moduleparam.h sets
MODULE_PARAM_PREFIX = KBUILD_MODNAME "."; the built vmlinux therefore carries the
strings "platsmp.luofu_smp_diag" / "platsmp.luofu_smp_probe", and the bare names on
the W7 command line were reported unknown. Consequences:

* the extra diagnostics were off (no prepare-time register dump, no +1ms/+21ms lines);
* the pen was the plain secondary_startup address (0x8070a000), **not** the probe
  stub. The W7 run therefore says nothing about whether CPU1 consumes the pen; the
  probe experiment is still unrun [V].

The correction for future 033 command lines is
"platsmp.luofu_smp_diag=1 platsmp.luofu_smp_probe=1". Patch 034 removes the trap by
parsing the bare documented names with __setup() and by enabling the probe by default.

### 15.2 What the W7 run did establish [V]

* the default 033 release (vendor per-core resume bits + write barriers) still fails:
  pen 0x8070a000, resume word 0x02a00000 before the RMW, mask 0x02800000, IPI sent,
  CPU1 offline after the full 1 s timeout;
* the A9 SCU CPU-status byte of CPU1 reads 0x00.

### 15.3 SCU CPU status 0x00 means NORMAL (powered) [V]

The Cortex-A9 SCU CPU Status Register is at SCU + 0x08 with one byte per core; each
byte's bits [1:0] are the power mode: 0b00 normal, 0b10 dormant, 0b11 powered off
(0b01 reserved). Mainline ARM has the same encoding and access shape:

    arch/arm/include/asm/smp_scu.h: SCU_PM_NORMAL 0, SCU_PM_DORMANT 2, SCU_PM_POWEROFF 3
    arch/arm/kernel/smp_scu.c:      readb_relaxed(scu_base + SCU_CPU_STATUS + cpu) & GENMASK(1, 0)

So CPU1 is **powered and not dormant/power-down** at release time [V].
"luofu_smp_power=1" (set the SCU CPU power status to NORMAL) is a no-op in this state
and is only useful as a control. Caveat: the SCU status field describes power mode,
not reset state or execution; a powered core held in reset still reads 0b00 [I].

### 15.4 Stock 2-CPU reference state (read-only, 2026-11-18) [V]

The stock vendor boot with both CPUs online was measured read-only on 2026-11-18
(build/bench-inventory/20261118T130512Z-inv1-run/, .merged.tsv, repeats identical):

| address | word | note |
| --- | --- | --- |
| SYSCTRL 0x10100c00 | 0x80609680 | vendor pen value (leaves the word set) |
| SYSCTRL 0x10100c08 | 0x00000010 | boot selector pair |
| SYSCTRL 0x10100c10 | 0x82000000 | reset record |
| CRG 0x14880014 | 0x01360ef1 | gate word |
| CRG 0x14880020 | 0x02f8300b | gate word |
| CRG 0x1488002c | 0x0d300e01 | reset word |
| CRG 0x14880030 | 0xfe7fe05b | reset word |
| CRG 0x14880034 | 0x000ff309 | reset word |
| CRG **0x14880038** | **0x00200000** | resume word after the vendor release |
| CRG 0x14880090 | 0x6a010008 | PLL status |
| CRG 0x14880100 | 0x00000000 | watchdog status |
| CRG 0x14880138 | 0x00018042 | clock mux (bit3 CPU source) |

Key point: the vendor's release leaves CRG + 0x38 = 0x00200000, which is exactly the
value **our** read-modify-write produces (the 029 run and the 033 default both end at
0x00200000). The CRG release operation therefore matches the vendor's effect on this
board and cannot by itself explain the different boot outcome [V]. The W7 boot also
saw 0x02a00000 again before its RMW, i.e. the resume word is re-initialised by each
reset [V: two boots].

### 15.5 Boot context: every SMP test so far is a warm reset [V]

The W7 boot was a warm reset from the running image ("Reset Info: Linux reboot cmd to
reset", no "Hard Power-on reset" line). A log survey of build/uart-logs/ shows that
**no our-kernel boot in this repository has ever been a cold power-on**, and that warm
resets do not prevent a working CPU1 release for the stock kernel: e.g. a 1-CPU 6.18
boot followed by a panic/watchdog warm reset, then U-Boot, then stock 5.10 with
2 CPUs (20261119T044150Z-uart-boot2.log, 20261119T091941Z-uart-finalboot.log). The
loader path can therefore still make CPU1 releasable after a warm reset; what differs
is the state inside our boot or at the moment of our pen write [I].

### 15.6 Patch 034: the next attended variant

Patch 034 (arch/arm/mach-hisilicon/platsmp.c only, no config/DTS change) makes the
evidence immune to command-line mistakes and enables the decisive probe:

* __setup() parsing: bare "luofu_smp_probe=0|1", "luofu_smp_power=0|1",
  "luofu_smp_allbits=0|1", "luofu_smp_clear_mask=<hex>",
  "luofu_smp_force_pen=<pa>", "luofu_smp_retries=<n>"; the old
  "luofu_smp_diag=" name is accepted as a compatibility no-op;
* unconditional register snapshots at prepare time (including the pen word as left
  by the loader), before the release, and at +1 ms and +21 ms: SYSCTRL 0xc00..0xc1c,
  CRG 0x04..0x3c, SCU ctrl/config/status with both CPU status bytes;
* probe stub on by default: the pen points at a stub that first stores the core's
  MPIDR in a page-aligned scratch page (read back through an uncached ioremap alias,
  so it survives a UART failure), prints the low MPIDR nibble and "P" on the console
  UART, and then branches to the real secondary_startup. "1P" on the console or a
  non-zero marker in the +1ms/+21ms lines proves the pen was consumed;
  "luofu_smp_probe=0" restores the plain entry for the control run;
* the release order and the resume mask are unchanged from 033 (vendor pair by
  default; clear_mask/allbits for experiments).

The CRG dump intentionally stops at 0x3c and skips the reboot command word (0x00) and
the watchdog words (0x50/0x64/0x70/0x100).

### 15.7 What to read on the stock system (read-only, root telnet) [U to close]

The stock already has the read path: /sbin/devmem is present (health check
devmem-capability PASS on 2026-11-18) and /dev/mem is readable. Read-only only, no
writes. Run this on the stock boot, twice, and record boot_id/uptime for each sample:

    for a in 0x10180000 0x10180004 0x10180008 \
             0x10100c00 0x10100c04 0x10100c08 0x10100c0c 0x10100c10 0x10100c14 \
             0x14880004 0x14880008 0x1488000c 0x14880010 0x14880018 0x1488001c \
             0x14880038 0x1488003c ; do
        printf '%s ' "$a"; /sbin/devmem "$a" 32; done

Fallback without devmem: dd if=/dev/mem bs=4 skip=$((0xADDR/4)) count=1 2>/dev/null |
od -An -tx4. Values already known from the 2026-11-18 inventory (15.4) do not need to
be re-read; the missing ones are the three SCU words and the SYSCTRL/CRG neighbours
listed above. Purpose: complete a like-for-like comparison with the 034 dump (SCU
config/status of both CPUs and the words around the resume register). This is the
first stock sample that is directly comparable to our boot-side dump.

Note the limitation: /dev/mem only shows the post-boot state; it cannot capture the
loader-time CPU1/power state, which is why the in-kernel dump is the primary probe.

### 15.8 Next single-variable boot plan

1. Build an 001-034 image from the bench clone (no command-line change required:
   dumps are unconditional and the probe is on by default; leaving
   "luofu_smp_probe=1" is fine, dropping "luofu_smp_diag=1" avoids the unknown-param
   line). Warm boot, capture the UART read-only.
2. Read the single decision from that boot:
   * **"1P" and/or marker != 0**: the loader's pen consumer is alive and CPU1
     consumed the pen.
     - CPU1 online: the stub path works; the next variable is the direct entry
       (luofu_smp_probe=0) to see whether the direct jump still fails.
     - CPU1 still offline: CPU1 dies in the 6.18 secondary entry after the stub;
       the next patch instruments __secondary_switched.
   * **no marker, +21ms pen still equals the written value**: CPU1 did not consume
     the pen. Then, one variable per boot, in this order:
     a. cold power-on of the same image (reset-type variable) - tests whether the
        loader only parks CPU1 on a cold boot;
     b. luofu_smp_power=1 (control; expected no-op because SCU status is normal);
     c. luofu_smp_clear_mask=0x02a00000 then 0x03c00000 (CRG bit experiments; this
        is the point where the stock CRG read in 15.7 should be in hand).
3. If the cold boot also shows no consumption, compare the 034 pre/post dumps with
   the stock reference and then move the pen write earlier in our boot (an
   early_initcall probe that writes a spin-only stub and a marker) to test whether
   anything in the kernel's own early init stops CPU1 before the normal release
   point. That is patch 035 work, not part of this variant.

---

## 16. 034 hardware result (2026-11-19, image 5d2c5476) and patch 035

Status: third hardware run of this lane. Patch 034's diagnostics and probe ran (the
__setup() names worked), but the probe marker page could not be read, so the probe
result is inconclusive. This section records the first boot-side register state of
our kernel, answers the "pen read 0 before our write" question, and delivers the
marker-fixed variant (035). The early-write probe idea from section 15.8 is not part
of 035; it moves to a later patch if it is still needed.

### 16.1 First in-kernel register snapshot [V]

Boot context: warm reset from the previous image ("Reset Info: Linux reboot cmd to
reset"; no "Hard Power-on reset" line). Values measured at prepare (~0.25 s) and
again after the release (~0.37 s):

| word | prepare value | after release | note |
| --- | --- | --- | --- |
| SYSCTRL 0x10100c00 (pen) | 0x00000000 | 0x81195000 | loader left 0; after our write it holds the probe stub address and is never cleared |
| SYSCTRL 0x10100c08 | 0x00000010 | same | boot selector pair |
| SYSCTRL 0x10100c10 (rstinfo) | 0x42000000 | same | differs from the stock sample (0x82000000); likely reset provenance, [U] |
| SYSCTRL 0x10100c14 | 0xdfdf0781 | same | unknown, [U] |
| CRG 0x14880014 | 0x01360ef1 | same | same as the stock inventory |
| CRG 0x14880020 | 0x02f8000b | same | stock inventory 0x02f8300b (bit 12 differs; our gated clock writes) [I] |
| CRG 0x1488002c | 0x0d300e01 | same | same as stock |
| CRG 0x14880030 | 0xfe7fe040 | same | stock 0xfe7fe05b (low bits; our reset writes) [I] |
| CRG 0x14880034 | 0x00000309 | same | stock 0x000ff309 (our PCIe/reset writes) [I] |
| CRG 0x14880038 (resume) | 0x02a00000 | 0x00200000 | exactly the stock reference value after the vendor release |
| SCU ctrl | 0x00000003 | same | bit0 SCU enabled (loader); our scu_enable() early-returned [I] |
| SCU config | 0x00000511 | same | bits [1:0] = 1 -> 2 cores [V, mainline formula]; other bits raw [U] |
| SCU status | 0x03030000 | same | byte per core: CPU0 = 0x00, CPU1 = 0x00 -> NORMAL; CPU2 = 0x03, CPU3 = 0x03 -> powered off. Validates the byte-wise power-mode decode [V] |

Release attempt: pen = 0x81195000 (probe stub PA), resume 0x02a00000 -> 0x00200000
with mask 0x02800000; at +1 ms and +21 ms the pen still reads 0x81195000, the SCU
status is unchanged (CPU1 still normal), no 'P' appeared and CPU1 stayed offline
after the 1 s timeout.

### 16.2 The probe marker channel failed [V]

The boot log shows "luofu-smp: cannot map the probe marker" at 0.239 s. Patch 034
read the marker page through ioremap() of System RAM; on 6.18 ioremap() of
pfn_valid() RAM returns NULL, the read helper then returned 0 and the marker was
never observed. **The 034 probe result is inconclusive: "pen unchanged, no 'P'" is
consistent with both "CPU1 never consumed the pen" and "CPU1 consumed it and parked
in the stub" (for example on the unbounded UART LSR poll).**

### 16.3 The loader-left pen 0 is expected; the stock 0x80609680 is not comparable [V]/[I]

The only stock pen reading in the repository (0x80609680) was taken at runtime with
the vendor kernel running, i.e. it is the *vendor kernel's own write*, not the
loader state. The loader-left state was never measured on the stock system. On our
side, the previous image had left pen = 0x8070a000; 034 then read pen = 0 before its
own write, which proves that the decoded U-Boot boot-core path (clear pen, program
CRG + 0x50, re-init the SCU) ran on this warm boot [V]. The loader-left pen value
therefore carries no information about CPU1 by itself: it is 0 for every boot where
the loader's boot-core path runs, including boots where CPU1 does come up under the
stock kernel. What it does say is that the bootloader half of the hand-off is
healthy [I].

The rstinfo record at SYSCTRL + 0xc10 differs between the two samples
(0x42000000 here versus 0x82000000 on the stock inventory); the top bit is the
likely cold/warm or reset-source marker and is [U]. A stock read after a known cold
power-on and after a known warm reboot (same command set as 15.7 extended with
0x10100c10) would settle it.

### 16.4 Patch 035: marker-fixed probe

Patch 035 keeps 034's unconditional dumps and release knobs and replaces the probe
marker channel:

* the marker page is no longer ioremap()ed. The boot CPU reads it through the
  normal linear mapping after __sync_cache_range_r() has cleaned and invalidated
  its own cache lines (inner + PL310 outer flush); CPU1 writes with the caches off,
  so its stores are already in DRAM;
* the stub records two markers: word 0 = MPIDR as soon as it starts, word 1 =
  0x50454e21 ("PEN!") just before it branches to the real entry. It prints 'P' on
  the console UART with a bounded LSR poll (1 << 20 reads), so a dead UART cannot
  park the core forever;
* the +1 ms, +21 ms and retry lines print marker0/marker1 and a "consumed=" verdict;
  a read-only late_initcall() looks at the marker page once more in case the store
  arrives late;
* everything else (probe default on, probe=0 control, power, allbits, clear_mask,
  force_pen, retries, unconditional SYSCTRL/CRG/SCU snapshots) is unchanged.

Expected lines on the next boot: "prep markers 00000000 00000000 (must be zero)",
then either "P" from CPU1 and non-zero marker0/marker1 with "consumed=yes", or zeros
with "consumed=no" at +21 ms and at late_initcall().

### 16.5 Updated next single-variable boot plan

1. Build an 001-035 image from the bench clone (no command-line change needed) and
   warm-boot it. The marker decides:
   * consumed=yes -> CPU1 does consume the pen. Then the next variable is the plain
     entry (luofu_smp_probe=0): if the direct entry still fails, CPU1 dies in the
     6.18 secondary entry and the next patch instruments __secondary_switched.
   * consumed=no -> CPU1 never consumed the pen on this warm boot. Next, one
     variable per boot: (a) cold power-on of the same image (reset-type variable);
     (b) luofu_smp_probe=0 control; (c) luofu_smp_power=1 control (SCU says
     NORMAL, expected no-op); (d) clear_mask experiments only after the stock
     CRG/rstinfo reads are in hand.
2. Also read the stock SCU words and rstinfo (section 15.7 command set plus
   0x10100c10) after a known cold power-on and after a known warm reboot, to
   correlate 0x42000000/0x82000000 with the reset type [U].

### 16.6 Validation of 035 (host-only, isolated clone)

| check | result |
| --- | --- |
| pristine 6.18.60 series apply 001-035 | patch -p1 -F0: RC=0, 113 files, no .rej/.orig; only the four pre-existing offset hunks (008 x2, 021, 028); resulting platsmp.c sha256 cc3edadc... identical to the delivered file |
| OpenWrt prepare + compile 001-035 | make target/linux/prepare V=s: RC=0; make target/linux/compile V=s -j16: RC=0 (see the lane summary for the artifact hashes) |
| probe stub encoding | generated instruction words compared with the project ARM assembler for a sample marker address: all 22 instructions + literal match (the bne back into the poll loop was corrected from 0x1afffff9 to 0x1afffffa during this lane) |
| checkpatch | patch-mode: only the expected missing commit-description/Signed-off-by metadata; file-mode 0 errors / 0 warnings |

---

## 17. Cold-boot result (2026-11-19, image 75868fe0) and patch 037

Status: fourth hardware run of this lane. A normal power-cycle cold boot of the
marker-fixed 035 image gives the same result as every warm boot: CPU1 never consumes
the pen. This section records that result, explains which hypotheses it removes, and
delivers the next diagnostic (patch 037 - requested as "036" but renumbered because
the parallel PFE lane filed 036-net-lsw-pfe-init.patch at the same time).

### 17.1 The cold-boot test [V]

Normal power-cycle (no RESET button), image 75868fe0 (series 001-035 with the marker
fix). Console result, identical to the warm runs except for the reset record:

    prep markers 00000000 00000000 (must be zero)
    prep pen value 00000000 (loader state) resume 02a00000
    prep scu ctrl=00000003 config=00000511 status=03030000 cpu0=0 cpu1=0
    release cpu=1 ... pen=0x81195000 ...
    +1ms  marker0=00000000 marker1=00000000
    +21ms marker0=00000000 marker1=00000000 consumed=no
    late_initcall consumed=no
    CPU1: failed to come online

So CPU1 does not consume the pen on **warm or cold** boot. The SCU CPU status stays
03030000 (CPU0/CPU1 normal, CPU2/CPU3 powered off) before and after the release, and
the pen word stays 0x81195000. The only difference between the cold and warm runs is
the reset record at SYSCTRL + 0xc10: 0x02000000 on the cold boot versus 0x42000000 on
the warm one [V: reset-source encoding, exact bit meaning [U]].

### 17.2 What the cold result removes [V]/[I]

* reset type (warm vs cold) is not the differentiator: both fail identically;
* CPU1 power state is not the differentiator: the SCU reports normal in both cases;
* the pen write/register path is not the differentiator: the value is written and
  read back, the CRG result matches the vendor's reference, and the marker channel is
  proven working (the prep markers read as zero through the same path);
* what remains is that CPU1 is **not executing the loader's pen-poll loop** at our
  release time (or is held in reset / diverted), or that the consumer is not a CPU1
  poll loop at all and expects a different condition.

### 17.3 New lead: the loader may park CPU1 in RAM that our kernel treats as free [I]

Two measured facts make this the leading hypothesis:

1. The vendor 5.10 kernel's effective memory map starts at **0x80600000** ("Early
   memory node ranges: node 0: [mem 0x80600000-0x87ffffff]" in the stock boot logs),
   although its DTB memory node starts at 0x80500000. Our 6.18 kernel instead uses
   **0x80500000** as the first RAM range. The 1 MiB window 0x80500000-0x805fffff is
   therefore free RAM for our kernel and avoided by the vendor kernel [V].
2. The vendor DTB reserves a 16 KiB "flashinfo" window at **0x80600000**
   (reserved-memory/flashinfo, atag-offset 0x1000); our DTB has no reserved-memory
   node at all. Our kernel's page tables sit just above it at 0x80604000 [V].

If the loader leaves the CPU1 park loop/state in either window, our kernel's first
memblock allocations (first-fit from the lowest RAM address: page tables, memmap)
overwrite it before our release - while the vendor kernel never touches those bytes.
That would explain every observation: CPU1 powered (SCU normal) but not polling, on
both cold and warm boots, while stock works. It is an inference [I]; patch 037 tests
it directly.

### 17.4 Patch 037

Requested as 036; delivered as
target-skeleton/target/linux/hisilicon/patches-6.18/037-arm-hisilicon-smp-early-probe.patch
because the parallel PFE lane created 036-net-lsw-pfe-init.patch in the same window.
It keeps all 035 behaviour and adds:

| knob | effect |
| --- | --- |
| luofu_smp_reserve_low1m=1 | memblock_reserve(0x80500000, 1 MiB) from an early_param handler, i.e. before arm_memblock_init()/paging_init() and therefore before the first allocation. Highest-value single variable: if the loader parked CPU1 there, preserving the window should make CPU1 consume the pen. |
| luofu_smp_reserve_flashinfo=1 | memblock_reserve(0x80600000, 16 KiB), mirroring the vendor reserved-memory node. |
| luofu_smp_early_probe=1 | run the release inside setup_arch(), before any allocation: pen = address of a stub that prints 'E', records its MPIDR in a marker and spins in WFE. Mode 1 = pen only, mode 2 = + CRG resume clear, mode 3 = + GIC wakeup IPI. 'E' on the console and/or the EARLY marker proves CPU1 consumed the pen before our kernel did anything. |
| (always on) | read-only loader-window diagnostics at prepare: first words of 0x80500000, 0x805ff000, 0x80600000, 0x80604000; a scan of 0x80500000-0x80607fff for the decoded U-Boot park-loop instruction signature (e5967000/e3570000/0afffffc/e1a0f007); and the memblock memory/reserved range dump, which shows what our kernel allocated where. |

The early probe uses early_ioremap(), which is available in setup_arch() before
paging_init(); the stub is built at runtime and cleaned to the point of coherency.
Nothing is reserved or written unless the corresponding command-line knob is present.

### 17.5 Next single-variable boot plan

1. **luofu_smp_reserve_low1m=1** on an 001-037 image (highest-value test). If CPU1
   consumes the pen / comes online, the loader parked CPU1 in that window and the real
   fix is a memory-map correction (reserve the window early, or correct the DTS memory
   map to the vendor's effective map). The 037 diagnostics also print whether the
   park-loop signature is visible there.
2. If not: **luofu_smp_reserve_flashinfo=1**.
3. If not: **luofu_smp_early_probe=1**, then 2, then 3. An 'E'/early marker at mode 1
   means CPU1 was parked and polled the pen before our kernel allocated anything, so
   something between setup_arch and the normal release destroys or diverts it; a hit
   only at mode 3 means the CRG clear + wakeup IPI are required.
4. If all silent: the loader does not park CPU1 on our boot path at all; then decode
   the loader's CPU1 start (hboot/U-Boot) and use the stock measurements in 17.6.

### 17.6 Stock read-only measurement plan (root telnet, no writes)

/devmem exists at /sbin/devmem (health check 2026-11-18) and /dev/mem is readable.
Run on the stock 5.10 boot (2 CPUs) after recovery:

1. SCU and SYSCTRL/CRG reference (read twice, note boot_id/uptime):

       for a in 0x10180000 0x10180004 0x10180008 \
                0x10100c00 0x10100c04 0x10100c08 0x10100c0c 0x10100c10 0x10100c14 \
                0x14880038 0x1488003c ; do
           printf '%s ' "$a"; /sbin/devmem "$a" 32; done

   Expected from earlier inventory: pen 0x80609680, resume 0x00200000, c08 0x10;
   SCU status is expected to read 0x03030000 if the same decode holds.

2. **The loader windows (the important new measurement).** On stock these bytes are
   preserved because the vendor kernel does not use them:

       for a in $(seq $((0x80500000)) 4096 $((0x805ff000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done
       for a in $(seq $((0x80600000)) 4096 $((0x80603000))); do
           printf '%x ' $a; /sbin/devmem $a 32; done

   To look for the U-Boot park loop (busybox od):

       dd if=/dev/mem bs=4096 skip=$((0x80500000/4096)) count=256 2>/dev/null \
         | od -An -v -tx4 | grep -i -m1 'e5967000'
       dd if=/dev/mem bs=4096 skip=$((0x80600000/4096)) count=4 2>/dev/null \
         | od -Ax -v -tx4

   If /dev/mem refuses 0x805xxxxx (STRICT_DEVMEM) or the read faults, stop there and
   say so - the 037 in-kernel dump reads the same region from our side.

3. Cold vs warm correlation for the reset record: repeat (1) after a known power-on
   and after a known warm reboot; on our side the cold record was 0x02000000 and the
   warm record 0x42000000.

### 17.7 Validation of 037 (host-only, isolated clone)

* pristine 6.18.60 apply of the current series 001-037 (including the parallel
  036-net patch): RC=0, 117 files, no .rej/.orig; only the four pre-existing offset
  hunks (008 x2, 021, 028); resulting platsmp.c sha256 528ac6f8... identical to the
  delivered file;
* clean make target/linux/prepare + make target/linux/compile in the isolated clone
  (see the lane summary for the artifact hashes);
* early stub encodings verified word-for-word against the project ARM assembler
  (18 instructions, including the bounded UART poll and the WFE spin);
* checkpatch: only the expected missing commit-description/Signed-off-by metadata
  plus compact-dump pr_cont style notes.

---

## 18. 037 reserve_low1m run (2026-11-20, image 6cb3d908): window hypothesis refuted

Status: fifth hardware run of this lane. The first 037 single-variable boot enabled
luofu_smp_reserve_low1m=1. The reservation worked, but CPU1 still does not consume
the pen, and the diagnostics show what is actually in the low memory window.

### 18.1 Result [V]

Boot context: warm reset ("Reset Info: Linux reboot cmd to reset"), token present in
the baked command line. Console:

    luofu-smp: reserved 0x80500000-0x805fffff before allocation
    prep pen value 00000000 (loader state) resume 02a00000
    prep markers 00000000 00000000 (must be zero)
    mem 80500000: 66cc66cc 66c466cc 66cc66cc ...
    mem 805ff000: 66cc66cc ...
    mem 80600000: 00030003 00000000 c0600008 c0600008 00000000 66cc668c ...
    mem 80604000: 00000000 ...
    loader park-loop scan 0x80500000-0x80607fff hits=0
    memblock memory ranges (available): mem [80600000-87ffffff]
    memblock reserved ranges: rsv [80500000-805fffff] ...
    release ... pen=0x81195000 ... +1ms/+21ms marker0=0 marker1=0 consumed=no
    early=00000000 (early probe not enabled)
    late marker0=0 marker1=0 consumed=no
    CPU1: failed to come online

The reservation is effective: the available-memory dump starts at 0x80600000, i.e.
exactly the vendor kernel's effective map, while the low 1 MiB is reserved. CPU1 is
still not released, so **the "loader parked CPU1 in the low 1 MiB window and our
kernel overwrote it" hypothesis is refuted [V]**.

### 18.2 What the windows contain [V]/[U]

* 0x80500000 and 0x805ff000 are filled with the DDR test pattern 0x66cc66cc (with
  occasional 0x66c4/0x66ec/0x67cc/0x668c bit variations): a memory-test fill, not
  code; no park-loop signature anywhere in 0x80500000-0x80607fff (scan hits=0).
* 0x80600000 (the vendor "flashinfo" window, not reserved by our DTB) holds a few
  non-pattern words (00030003, 00000000, c0600008, c0600008, 0) followed by the same
  test pattern. With the low 1 MiB reserved, 0x80600000 becomes the first available
  RAM for our kernel, so these words may already be our own early-allocation
  leftovers; what the loader leaves there is [U] until the stock read in section 17.6
  is taken.
* 0x80604000 (swapper_pg_dir area) read as zero at prepare time; recorded raw.
* The reserved ranges near the top of RAM (87ffe...-87ffffff) are the kernel's own
  early reservations and are expected.

### 18.3 Next single variable: early probe, not reserve_flashinfo

Reserving the remaining 16 KiB flashinfo window is now low value: the window content
is boot-data/test-pattern shaped, the park-loop signature is absent from both
windows, and - decisively - the early probe runs *before any allocation*, so a
positive early probe would tell us the loader does park CPU1 and the later
reservations/timing matter, while a negative one rules out the whole window class.
The next boot should therefore be **luofu_smp_early_probe=1** (mode 1: pen only, no
CRG clear, no GIC access - the safest mode). If silent, repeat with mode 2 (adds the
CRG resume-bit clear) and mode 3 (adds the GIC wakeup IPI; mode 3 writes the GIC
distributor before the kernel's GIC driver initialises it, so it is the only mode
with a small hang risk - the recovery door covers it).

Special considerations for the early probe:

* it runs from an early_param handler inside setup_arch(), before memblock/paging
  init, and uses early_ioremap() and fixed SoC physical addresses (no DT needed);
* it prints "luofu-smp: EARLY probe mode 1 ... stub <pa> marker <pa>" (expected
  around 0x81193440/0x81193400 for this build) and "EARLY pen ... resume ...";
* the stub prints a single raw 'E' to the UART THR (bounded LSR poll; the bootloader
  already configured the UART), records its MPIDR in a marker page and WFE-spins.
  The 'E' may interleave with earlycon output - scan the raw log for a stray 'E'
  rather than a whole line;
* if CPU1 consumes the pen it is parked in our early stub, so it will NOT come online
  and the normal +1ms/+21ms markers stay 0 - the evidence is 'E' and/or
  "EARLY mode 1 marker0=<MPIDR> consumed=yes" in the prep line;
* if mode 1 is silent, mode 2 tells whether the core needs its CRG resume bits
  cleared, and mode 3 whether it additionally needs the wakeup IPI;
* if all three modes are silent, CPU1 does not execute the loader's pen-poll loop
  even before our kernel does anything; the next step is then the stock read plan
  (17.6) plus loader-side decode of how CPU1 is started.

### 18.4 Follow-up if the early probe is positive

If 'E' appears at mode 1, the remaining question is what between setup_arch
(~0.02 s) and smp_prepare_cpus (~0.12 s) makes the pen consumer disappear (candidate
phases: paging_init's page-table/cache setup, L2C init, console/timer init, the
arch memblock reserve work). The follow-up patch (038) would repeat the same
marker-stub pen write from a late_time_init hook and from a couple of later
early-boot points to bisect the phase; only then would the fix direction become
clear.

---

## 19. W9 window (2026-11-20): the "2 CPUs" sighting is the stock fallback, the crash is traffic/W9, and patch 039 is the safe early probe

Status: sixth hardware run window. Two W9 images were booted: a6b57361 (001-038
with luofu_smp_early_probe=1) and 45d248ff (001-038, no early probe). Both eventually
reset after traffic was sent, and the loader fell back to the stock 5.10 image. This
section corrects the log reading, states what mode 1 did and did not test, records
the crash evidence, and delivers patch 039.

### 19.1 The "2 CPUs" lines are the vendor 5.10 fallback, not our kernel [V]

Evidence from the raw capture build/uart-logs/20261120T033451Z-uart-w9-run.log:

* the 6.18.60 boot with the early probe logs
  "luofu-smp: EARLY probe mode 1 in setup_arch: stub 81293440 marker 81293400",
  then "[1.530358] CPU1: failed to come online", "[1.534218] smp: Brought up 1 node,
  1 CPU", "[1.538264] SMP: Total of 1 processors activated (1993.93 BogoMIPS)";
* every "Brought up 1 node, 2 CPUs" / "CPU1: thread -1 ... mpidr 80000001" sighting in
  the W9 captures carries the vendor timeline (CPU1 thread at 0.1803-0.1805 s,
  brought-up at 0.1912-0.1913 s, 3987.86 BogoMIPS) and belongs to a stock 5.10.201
  boot segment (the capture interleaves our 6.18 console with the previous/fallback
  stock boot, which starts with its own "Linux version 5.10.201 (jenkins@cudyrd1)");
* our kernel's SMP release itself only starts at 0.36-0.37 s because the 037 dumps
  run first, so a 0.19 s CPU1 line cannot be ours;
* the same signature appears in the older stock captures (0.180494/0.191330 ...).

Therefore no 6.18 boot of this lane has ever brought up CPU1, and consumed=no with
1 CPU is internally consistent. The earlier reading "the early probe enabled the
normal release" is withdrawn.

### 19.2 What mode 1 actually tested [V]/[I]

Mode 1 writes only the pen (address of the early stub). It does not clear the CRG
resume bits and does not send the wakeup IPI. If CPU1 is held in reset or dormant -
i.e. not executing the loader's poll loop - a pen write alone cannot release it, so
marker=0 is expected and does **not** prove the pen consumer is absent. The decoded
order is pen -> resume bits -> wakeup IPI; only modes 2/3/4 program that order before
the first allocation. Mode 1 result: the pen alone is not enough, nothing more.

### 19.3 Crash evidence [V]

Both W9 boots (early-probe and clean) reset after traffic; the clean image has no
early probe, so the reset is not an early-probe effect. In the early-probe boot the
kernel ran to userspace as 1 CPU and oopsed at 7.4336 s:

    [7.403493] pie-trace GMAC eth2 xmit port=11 len=90
    [7.428223] pie-trace GMAC eth2 xmit port=11 len=1043
    ------------[ cut here ]------------
    [7.433699] Internal error: Oops: 17 [#2] SMP ARM
    [7.433709] CPU: 0 UID: 0 PID: 497 Comm: netmsg Not tainted 6.18.60 #0 NONE
    [7.433729] Unable to handle kernel NULL pointer dereference at virtual address 00000008 when read

The capture then cuts to the loader fallback ("enter esbc main"), so the register
dump and call trace are not in the log. The oops is real (6.18.60 kernel) but the
faulting function is not captured. The context - immediately after a 1043-byte GEMAC
eth2 transmit from the vendor netmsg userspace process, with the same reset happening
on the clean 1-CPU image - points at the W9 net TX path, not SMP. The net lane should
reproduce with the pie-trace lines and capture the full oops (widen console coverage
around the crash / delay the failover long enough).

### 19.4 Patch 039: safe early-probe variant

target-skeleton/target/linux/hisilicon/patches-6.18/039-arm-hisilicon-smp-early-safe.patch
(platsmp.c only; keeps the 037 behaviour used by earlier runs):

| mode | behaviour |
| --- | --- |
| 1 | pen only (same as 037) |
| 2 | pen + CRG resume-bit clear |
| 3 | pen + CRG clear + wakeup IPI; the raw GICD_SGIR value is now 0x00020000 (target list = CPU1, SGI 0). 037 wrote 0x02000000, which the GIC reads as target-list-filter "only the requesting CPU", i.e. a CPU0 self-IPI. The early IPI stays best effort because the GIC driver has not run yet |
| 4 | pen + CRG clear, a bounded window (about 200k cpu_relax iterations), then restore the pen and resume words if nothing was consumed, so the normal boot_secondary hands over cleanly; if the window consumed the pen the normal release is skipped |
| 5 | read-only early snapshot, no hardware writes at all (safe control) |

Other changes: the early stub now writes a second marker word (0x4545) after the UART
'E' print, so "never ran" and "ran past the UART poll" are distinguishable; the early
markers are printed at the end of the early handler, at prepare, at +1ms/+21ms and at
late_initcall; if a mode 1-4 marker shows consumption, boot_secondary skips the normal
release (no double release) and returns -EIO; the GIC distributor and CPU-interface
words (GICD_CTLR, GICD_SGIR, GICC_CTLR of CPU0 and CPU1) are dumped at prepare and
after the release, because the wakeup IPI can only reach CPU1 if the distributor and
the target CPU interface are enabled.

### 19.5 Next single variables

1. **luofu_smp_early_probe=2** on an 001-039 image: early pen + CRG clear, the first
   mode that tests the decoded release order before any allocation (mode 1 only tested
   the pen). If this consumes the pen, CPU1 was being held and the early release can
   work; if not, continue with 3 (corrected IPI) and 4 (hand-back window).
2. If all modes are silent, the loader/ROM is not polling or not releasing CPU1 on
   this boot path at all; then the stock reads (17.6 plus the GIC words in 19.6) and a
   loader-side decode of how CPU1 is started become the next step.
3. Optional control: mode 5 (read-only snapshot) confirms the early handler itself
   does not perturb anything.

### 19.6 Stock read-only additions

Add to 17.6 (stock 5.10 boot, 2 CPUs):

    for a in 0x10181000 0x10181004 0x10181008 0x10181f00 0x10180100 0x10180200 \
             0x10180000 0x10180004 0x10180008; do
        printf '%s ' "$a"; /sbin/devmem "$a" 32; done

Expected on a healthy stock boot: GICD_CTLR bit0 = 1 (enabled), GICD_SGIR reads 0,
GICC_CTLR (CPU0 and CPU1) bit0 = 1. These are the values our kernel's GIC driver must
reproduce for the wakeup IPI to be deliverable; they also give the stock reference for
the SCU words.

### 19.7 Validation of 039 (host-only, isolated clone)

* pristine 6.18.60 apply of the current series 001-039: RC=0, 121 files, no
  .rej/.orig; only the four known offset hunks (008 x2, 021, 028); resulting
  arch/arm/mach-hisilicon/platsmp.c sha256 99343778... identical to the delivered file;
* early stub re-verified word-for-word against the project ARM assembler (20
  instructions, including the marker word 1 progress store);
* clean make target/linux/prepare + make target/linux/compile in the isolated clone
  (artifact hashes in the lane summary).

---

## 20. Stock read-only reference (2026-11-20) and patch 042: SCU config bit and GIC reference

Status: the lead took a clean read-only reference on a stock 5.10 boot
(boot_id 72c87547-70e5-4882-8117-a128eac71ad5, two identical rounds,
build/evidence-bench-gates/2026-11-20-stock-reads.txt). This section interprets it,
records the new single-variable knob, and lists the additional stock reads that are
worth taking while the stock image is still reachable.

### 20.1 Stock reference values [V]

| register | stock value | our 6.18 value | note |
| --- | --- | --- | --- |
| SCU 0x10180000 ctrl | 0x00000003 | 0x00000003 | same |
| SCU 0x10180004 config | **0x00000531** | **0x00000511** | bit 5 set only on stock |
| SCU 0x10180008 status | 0x03030000 | 0x03030000 | CPU0/1 NORMAL, CPU2/3 off |
| GICD 0x10181000 CTLR | 0x00000001 | (039 dumps) | distributor enabled |
| GICD 0x10181004 TYPER | 0x0000FC25 | - | 44 IRQ lines (vendor config) |
| GICD 0x10181008 IIDR | 0x0102043B | - | ARM GIC v1 r2p4 |
| GICD 0x10181f00 SGIR | 0x00000000 | - | no IPI pending/left in SGIR |
| GICC 0x10180100 CTLR (CPU0) | 0x00000001 | (039 dumps) | CPU0 interface enabled |
| GICC 0x10180200 CTLR (CPU1 view) | 0x00000000 | (039 dumps) | **not meaningful**: banked/RAZ from CPU0 |
| SYSCTRL 0x10100c00 pen | 0x80609680 | loader-left 0, then our values | vendor runtime pen, not cleared |
| SYSCTRL 0x10100c04/c0c | 0 | 0 | same |
| SYSCTRL 0x10100c08 | 0x00000010 | 0x00000010 | same |
| SYSCTRL 0x10100c10 rstinfo | 0x0A000000 | 0x42000000 warm / 0x02000000 cold | third sample, encoding [U] |
| SYSCTRL 0x10100c14 | 0xDFDF0781 | 0xDFDF0781 | same |
| CRG 0x14880038 resume | 0x00200000 | our post-release 0x00200000 | release value vendor-equivalent |
| CRG 0x1488003c | 0 | 0 | same |

### 20.2 Interpretation

* **GICC CTLR CPU1 = 0 carries no information [V].** The A9 GIC CPU interface is
  banked per CPU: a CPU can only read its own bank. CPU0 reading the CPU1 window
  (0x10180200) returns zero on stock and on our kernel alike; it does not mean CPU1's
  interface is disabled. CPU1 only enables its own interface when it starts running
  its GIC init, so the vendor's release cannot depend on this bit. (Patch 039/042
  dumps it only as a labelled reference value.)
* **SCU config 0x531 vs 0x511 is a real state difference, semantics [U].** Bit 5
  (0x20) is set on stock and not in our kernel. U-Boot is shared by both boot paths
  and our first read of the register (prepare time, 0.13 s) already sees 0x511, so
  [I] the vendor kernel sets bit 5 in its own early init; our port never writes the
  SCU Configuration Register at all (scu_enable() returns early because ctrl bit 0 is
  already set by the loader). With the A9 SCU Configuration Register's low bits
  decoded (bits [1:0] = cores - 1), bit 5 is above the documented core-count field;
  the lead's "standby enable" reading is plausible but unverified [U]. It cannot gate
  a core that is already powered (SCU status NORMAL) and polling memory, but it is the
  first non-vendor-equivalent SCU state we have found, so it deserves one
  single-variable run.
* GICD_CTLR = 1, SGIR = 0, GICC CPU0 CTLR = 1 show a normal enabled distributor and
  CPU0 interface on stock; patch 042 prints the same fields from our kernel so the
  comparison is direct.
* The pen word 0x80609680 is the vendor kernel's runtime write and stays intact -
  the consumer does not clear the pen - so the stock value says nothing about
  consumption.
* rstinfo 0x0A000000 is a third sample (ours 0x42000000 warm, 0x02000000 cold); the
  field varies with the boot path and is not decoded [U].

### 20.3 Patch 042: SCU config bit and GIC reference dumps

Requested as part of the patch 040 window; renumbered to 042 because the parallel net
lane filed 040-net-gmac-w9-diag.patch and 041-net-gmac-macconf-nodoorbell.patch at
the same time (those two do not apply cleanly on a fresh 001-039 series as of this
writing - flagged for the net lane, not part of this post's validation window).

target-skeleton/target/linux/hisilicon/patches-6.18/042-arm-hisilicon-smp-scu-gic.patch
(platsmp.c only; keeps all 039 behaviour):

* new bare-name knob **luofu_smp_scu_cfg=<hex>**: ORs the mask into the SCU
  Configuration Register (0x10180004) at prepare time, before the release, and prints
  the value before, after and the readback. "luofu_smp_scu_cfg=0x20" is the
  single-variable test for the stock bit 5. The standard prep/+1ms/+21ms SCU dumps
  then show the configuration the release ran with.
* the GIC dump now covers the same registers as the stock read: GICD_CTLR,
  ISENABLER0/1, ISPENDR0, ISACTIVER0, ITARGETSR0, ICFGR0/1, IPRIORITYR0, plus GICC
  PMR/BPR of the CPU0 bank and the labelled CPU1 banked view, at prepare and after
  the release.

### 20.4 Additional stock reads (while the stock image is reachable)

Priority 1 (safe, clearly useful):

    for a in 0x10181100 0x10181104 0x10181108 0x10181200 0x10181300 \
             0x10181400 0x10181800 0x10181c00 0x10181c04 \
             0x10180104 0x10180108 ; do
        printf "%s " "$a"; /sbin/devmem "$a" 32; done

  This covers SGI/PPI enables, pending/active SGIs, SGI targets, SGI config edges,
  SGI priorities and the CPU0 interface PMR/BPR. It is the reference our kernel's GIC
  driver must match for the wakeup IPI (SGI 0) to be deliverable to CPU1 on stock.

    dd if=/dev/mem bs=4096 skip=$((0x80600000/4096)) count=4 2>/dev/null | od -Ax -v -tx4

  The 16 KiB flashinfo window as the loader/vendor kernel left it (our kernel's view
  of the same window is in the 037/039 dumps).

    for a in $(seq $((0x80500000)) 4096 $((0x805ff000))); do
        printf "%x " $a; /sbin/devmem $a 32; done

  One word per page of the low 1 MiB window: on our side it is only the
  0x66cc66cc DDR test pattern; the stock value tells whether the loader left anything
  else there.

    cat /proc/interrupts ; cat /proc/softirqs

  Harmless, shows that SGI/IPI delivery to CPU1 works on stock (the reference for a
  CPU1 that may be woken by IPI rather than polling).

    for a in 0x10100c18 0x10100c1c 0x14880000 0x14880004 0x14880008 0x1488000c \
             0x14880010 0x14880014 0x14880018 0x1488001c 0x14880020 0x14880024 \
             0x14880028 0x1488002c 0x14880030 0x14880034 ; do
        printf "%s " "$a"; /sbin/devmem "$a" 32; done

  Remaining SYSCTRL words next to the reset record and the full CRG window for the
  baseline comparison.

Priority 2 (riskier, decisive if readable): the decoded U-Boot CPU1 poll-loop
candidate in the loaders own region:

    for a in 0x80040000 0x800405c0 0x800405d0 0x800405e0 0x800405f0 0x80040600 ; do
        printf "%s " "$a"; /sbin/devmem "$a" 32; done

  0x8004xxxx is outside the DT RAM window; if /dev/mem refuses it or the read faults the
  stock kernel may oops, but the loader recovers. If it reads, it proves where the
  loader park loop lives and whether the decoded instructions are really there at
  runtime.

Also useful: note how the stock boot was entered for this sample (fallback after our
crash?) for the rstinfo 0x0A000000 entry, and if a known cold power-on stock boot is
ever available, repeat the c10/SCU-config reads.

### 20.5 Next single variables

1. 001-039 image: luofu_smp_early_probe=2 (plan unchanged), then 3, then 4.
2. 001-042 image (separate session or a later image): luofu_smp_scu_cfg=0x20 as a
   single variable (do not combine with the early-probe modes in the same run).
3. If all silent: the stock reads in 20.4 plus the loader decode of how CPU1 is
   started.

---

## 21. Live MMIO capability (2026-11-20): the CPU1 bring-up ladder without rebuilds

Status: the bench now runs a DEVMEM diagnostic image (c6ede30c) of our 6.18 kernel
with SSH and a static devmem tool, so MMIO can be read and written live. This section
records the live state, the correct interpretation of it, and the experiment ladder -
including the one thing that must not be tried and why.

### 21.1 Live read-only state [V]

    SCU 0x10180000 CTRL   = 0x00000003
    SCU 0x10180004 CONFIG = 0x00000511
    SCU 0x10180008 status = 0x03030000   (CPU0/1 NORMAL, CPU2/3 off)
    GICD 0x10181000 CTLR  = 0x00000001
    GICD 0x10181004 TYPER = 0x0000fc25
    GICD 0x10181008 IIDR  = 0x0102043b
    GICC 0x10180100 CTLR  = 0x00000001, PMR 0x000000f8
    GICC 0x10180200/04    = 0 (CPU1 bank; RAZ from CPU0, no information)
    SYSCTRL 0x10100c00 pen = 0x81295000   (kernel probe stub)
    CRG 0x14880038 resume  = 0x00200000   (CPU1 bits already clear)
    late marker0/1 = 0/0, consumed=no, early_mode=0
    System.map: secondary_startup = 0xc070b000 (PA 0x8070b000)

### 21.2 Two facts that shape the live plan [V]

1. **The current pen value is the kernel's probe stub, not the entry.** The probe
   stub prints 'P', writes its marker words and then branches to
   secondary_startup. That stub is safe until the branch.
2. **secondary_data is zeroed after the failed boot bring-up.** arch/arm/kernel/smp.c
   ends __cpu_up() with memset of secondary_data after the "CPU1: failed to come
   online" timeout, and secondary_startup reads pgdir/swapper_pg_dir/stack/task out of
   that struct with MMU off. A userspace-initiated jump into secondary_startup
   therefore cannot bring CPU1 online - it would enable the MMU with a zero
   page-table pointer and wedge CPU1. **Do not write pen=0x8070b000**, and treat a
   raw release into the current probe stub as "CPU1 will be wedged after printing
   'P'". A live release can prove consumption; it cannot online CPU1.

### 21.3 Primary ladder: the kernel's own hotplug path, live and repeatable

The clean live experiment is not a raw devmem release at all - it is writing 1 to
/sys/devices/system/cpu/cpu1/online. That runs the full kernel __cpu_up() path: it
sets secondary_data correctly, calls our luofu_smp_boot_secondary() (pen = probe stub
by default, CRG read-modify-write, wakeup IPI, all diagnostics), waits one second, and
on failure zeroes secondary_data again. It is repeatable, it needs no writes from
userspace, and it is exactly the path that failed at boot - but now at an arbitrary
later time with a fully initialised system.

Per attempt, dmesg shows the full 039/042 diagnostic set (release, pre, +1ms, +21ms,
consumed) followed by "CPU1: failed to come online" on failure. Success signature:
"CPU1: thread ... mpidr ..." from arch/arm/kernel/topology.c, /proc/cpuinfo with two
processors, /sys/devices/system/cpu/cpu1/online = 1, and 'P' on the console from the
probe stub. The probe markers also persist, so any later attempt's dmesg reprints a
consumption from an earlier attempt.

Live variables to interleave (one at a time):

    # 1. plain attempts, three times
    for i in 1 2 3; do echo 1 > /sys/devices/system/cpu/cpu1/online; sleep 2; done
    dmesg | tail -60

    # 2. the stock SCU configuration bit (bit 5), then attempts
    devmem 0x10180004 32 0x00000531; devmem 0x10180004 32
    echo 1 > /sys/devices/system/cpu/cpu1/online; sleep 2; dmesg | tail -40
    devmem 0x10180004 32 0x00000511        # restore when done

    # 3. wakeup-IPI storm during an attempt (tests IPI delivery/retry sensitivity)
    ( while true; do devmem 0x10181f00 32 0x00020000; done ) &
    echo 1 > /sys/devices/system/cpu/cpu1/online; sleep 2; kill %1
    # and the broadcast form: devmem 0x10181f00 32 0x01000000

### 21.4 Secondary ladder: raw devmem release into a safe stub (only if needed)

The raw sequence only makes sense with a pen target that neither branches nor
requires secondary_data. The one safe stub we already know how to build in-kernel is
the early-probe stub (patch 039/042): it writes marker word 0 = MPIDR, prints 'E',
writes marker word 1 = 0x4545 and spins in WFE. It is only built at boot when the
luofu_smp_early_probe token is present, so **rebuild the DEVMEM image once with
luofu_smp_early_probe=5** (mode 5 is the read-only snapshot - it performs no MMIO
writes but does build and cache-clean the stub). Then, with the stub PA S and marker
PA M taken from System.map (VA - 0x40000000):

    devmem 0x10100c00 32 S           # pen -> safe stub (must happen first)
    cur=$(devmem 0x14880038 32); new=$(( cur & ~0x02800000 ))
    devmem 0x14880038 32 $new        # CRG RMW: clear the CPU1 resume bits
    devmem 0x10181f00 32 0x00020000  # wakeup IPI: SGI0, target list CPU1

Expected on success: a single 'E' character on the UART (raw console write, not
dmesg), the box stays up, CPU1 spins in WFE inside the kernel stub, /proc/cpuinfo
stays at 1 (the kernel never onlined it). Read the early marker page with an
uncached read only: the normal /dev/mem read/write path uses the cacheable linear map
(__va, no cache maintenance), so a plain devmem read of RAM can return a stale cached
line; the UART 'E' is the reliable signal. A devmem variant that mmaps /dev/mem with
O_SYNC gets a write-combine (uncached) mapping and is suitable for the marker page.

Do not run this ladder against the current probe stub: it branches into
secondary_startup and wedges CPU1 (see 21.2).

### 21.5 Rollback, and which writes can hurt the box

Rollback after a failed raw attempt (self-cleaning, but explicit is better):

    devmem 0x10100c00 32 0x81295000   # pen back to the kernel probe stub
    devmem 0x14880038 32 0x00200000   # CRG resume back to the boot value
    devmem 0x10180004 32 0x00000511   # only if the SCU config bit was changed

If CPU1 consumed the pen ('E' seen, or markers set): reboot. The reset releases CPU1
from its parked state and the next boot starts from the known 1-CPU baseline; no
other cleanup is possible or needed.

Hang-risk ranking:

| write | risk | note |
| --- | --- | --- |
| pen = secondary_startup PA (0x8070b000), or raw release into the probe stub | medium | CPU1 branches into secondary_startup with a zeroed secondary_data: MMU enable with TTBR=0, almost certainly an abort loop/wedge for CPU1; small chance of a bus watchdog reset. UART capture armed, recovery door ready |
| SCU CONFIG (0x10180004) write with unknown higher bits | low-medium | bit 5 semantics undecoded; values written are the stock-known 0x531 and our 0x511, low bits (core count) unchanged |
| CRG 0x14880038 | low | only ever write the read-modify-write result of the known mask 0x02800000 |
| GICD_SGIR (0x10181f00) | none | write-only, GIC ignores targets that are not present/enabled |
| GICC CPU1 bank (0x10180200/04) | forbidden | banked per CPU; writes from CPU0 are undefined - never touch |

The sysfs-hotplug ladder (21.3) carries none of these risks: on failure it is exactly
the boot path that fails today, and on success CPU1 boots through the normal entry
with a valid stack and page tables.

### 21.6 Interpretation path

* If any live attempt reports consumed=yes/'P': CPU1 does poll/consume the pen in
  our 6.18 kernel context. Whatever single variable made the difference (timestamp,
  SCU config bit, IPI storm) becomes the kernel-side fix (for example a retry loop in
  __cpu_up with real delays, and/or the SCU configuration bit in the release path).
* If every live attempt stays consumed=no: CPU1 is not executing the loader's
  pen-poll loop on our boot path at all; the loader decode and the deeper stock reads
  (section 20.4) are the next step, not more release variants.

---

## 22. Handoff probes (patch 060) and the uncompressed-Image variant

Status: the live late-time attempts (add_cpu(1), IPI storms, CRG/GIC stock deltas)
all stayed consumed=no, and the CRG/GIC differences are now explained as PCIe/WiFi
activity on stock (section 20): +0x20/+0x28 bits 12/13 are the PCIe0/PCIe1 clock
gates and +0x34 bits 12-19 are the PCIe0/PCIe1 reset lines, i.e. the stock kernel
has PCIe up and our image does not. The SCU CONFIG bit 5 is read-only (a live write
is ignored) and is used from now on as a CPU1-ran monitor, not a lever. That leaves
one untested window: between the loader handoff and our kernel's first allocations.
Patch 060 probes that window.

### 22.1 Patch 060: two marker-only handoff probes

target-skeleton/target/linux/hisilicon/patches-6.18/060-arm-hisilicon-smp-handoff-probe.patch
touches three files; both probe blocks are guarded by CONFIG_ARCH_LUOFU and
!CONFIG_THUMB2_KERNEL, and need no kernel config token:

1. arch/arm/boot/compressed/head.S, immediately after the decompressor's entry
   (the 1: label block, after AR_CLASS(mrs r9, cpsr)): install a 5-word
   marker-only stub at PA 0x80600040 and point the SYSCTRL pen at it. The stub
   writes its MPIDR to PA 0x80600010 and spins - it never branches, so a secondary
   core that takes the pen parks safely without secondary_data. This runs before
   any decompression write, cache or MMU setup.
2. arch/arm/kernel/head.S, at stext right after safe_svcmode_maskall (MMU and
   caches still off): the same with stub B at PA 0x80600080, marker PA 0x80600020,
   pen = 0x80600080. stext runs after the decompressor has finished and before the
   kernel has touched a page.
3. arch/arm/mach-hisilicon/platsmp.c: an early_param handler
   luofu_smp_handoff_probe (token value required) reads both markers through an
   early_ioremap() window of 0x80600000 - before arm_memblock_init()/paging_init(),
   so no allocation can have overwritten them and the uncached view sees what the
   secondary core wrote to RAM with its caches off. It prints A/B/pen/SCU config;
   the same marker monitor is printed at prepare and after the release, and the
   SCU CONFIG bit 5 is printed as the CPU1-ran monitor.

### 22.2 Expected output format

At early_param time (before the paging-init banner lines):

    luofu-smp: handoff A=00000000 B=00000000 pen=80600080 scu_cfg=00000511
    luofu-smp: handoff verdict decompressor-entry=no kernel-stext=no
    luofu-smp: handoff cpu1-ran monitor scu_cfg bit5=clear

Later, at prepare and after the release:

    luofu-smp: handoff markers at prep A=<hex> B=<hex>
    luofu-smp: handoff cpu1-ran monitor at prep scu_cfg=<hex> bit5=<set|clear>
    luofu-smp: handoff cpu1-ran monitor post-release scu_cfg=<hex> bit5=<set|clear>

A consumed probe is nonzero and equals the secondary core's MPIDR
(0x80000001 for CPU1); "pen" at early_param is 0x80600080 because stext wrote probe
B last. Everything else in the boot log is the existing 039/042/059 instrumentation.

### 22.3 Interpretation tree

| A | B | meaning | next step |
| --- | --- | --- | --- |
| nonzero | zero | the consumer was alive at the decompressor's entry and was parked by probe A (B is zero because the core is already parked in stub A) | the loader does start/park CPU1, and something at/around our kernel's first instructions still lets it see the pen; the failure must be inside the decompressor/head.S window - bisect by moving probe B earlier and adding a probe after the decompressor's cache/MMU setup |
| zero | nonzero | consumer became active only during decompression (loader starts it late, or a decompressor side effect woke it) | bisect inside the decompressor: probe after cache_on, after decompress_kernel, before the branch to stext |
| zero | zero | the consumer never ran between loader handoff and stext | the loader does not start CPU1 on our image's boot path at all (or starts it only for the vendor image type); loader-side decode + stock comparison, and the uncompressed-Image variant below |
| nonzero | nonzero | consumer alive through the decompressor but parked by A; B is then redundant | as for A-only |

The SCU CONFIG bit 5 monitor adds a second, independent channel: if it reads 0x531
(set) at early_param or in the post-release line, a CPU1 core has executed
something (its U-Boot/ROM setup code); if it stays 0x511 with A/B zero, CPU1 never
executed any code on this boot.

Safety: the probes are marker-only spin stubs (no branch, no secondary_data), so a
taken pen parks the secondary core harmlessly; the image is a diagnostic image (it
writes the pen at boot), so do not expect add_cpu(1) to bring CPU1 online on it -
read the handoff lines and reboot.

### 22.4 Parallel variant: booting an uncompressed Image

Stock boots an uncompressed ARM Image; our image is a zImage that decompresses 11.5
MB over the same area. The build recipe exists in build/linux-kbuild/build-3dl.sh:
build the kernel with the Image target, make the payload "arch/arm/boot/Image +
luofu-r116.dtb" instead of "zImage + dtb", wrap it with the same mkimage command
(-A arm -O linux -T kernel -C none -a 0x80608000 -e 0x80608000 -n Linux-5.10.201)
and the same 40,732-byte trailer. Two caveats decide whether it is worth a build:

1. The appended-DTB mechanism lives in the decompressor. An uncompressed Image gets
   the DTB the loader passes, which is the vendor base DTB plus its overlays with
   root compatible "hsan-luofu" - our machine descriptor matches only
   cudy,wr3000-v2 / hisilicon,luofu-r116 / hisilicon,luofu. Either add "hsan-luofu"
   to luofu_dt_compat for the experiment (one line, and the boot then uses the
   vendor DT; enough to reach the handoff print, though later drivers will not
   bind), or pass our DTB explicitly with a small head.S/build patch.
2. Kernel slot size: the loader reads a bounded kernel image (8650752 bytes = 8.25
   MiB in the failover log) while our uncompressed Image is of the order of 12 MiB.
   Check /proc/mtd on stock before building; if the kernel partition is smaller than
   the Image the variant is not bootable without a size-reduced config.

Because an Image build has no decompressor, probe A does not exist there - only
probe B. So the useful comparison is zImage (A/B) versus Image (B): if B is
consumed on the Image but not on the zImage, either the decompressor killed the
consumer or the loader treats the two image types differently. That is exactly the
open question, so the variant is worth one build if the slot size allows it.

### 22.5 Validation of 060 (host-only, isolated clone)

(see the lane summary for the final numbers)

---

## 23. Patch 061: sentinel marker protocol and the re-polling handoff trampoline

Status: the 060 handoff run (image 202832a2, token luofu_smp_handoff_probe=1)
reported A=00000000, B=66cc66cc. The B value is **not** consumption: 0x66cc66cc is
the loader's DDR test pattern (our own dumps show 0x80500000-0x805fffff filled with
it and 0x80600018/0x1c = 66cc66cc in the flashinfo window). A consumed probe must
read the secondary MPIDR 0x80000001, so both probes were **never entered** - CPU1 is
not polling the pen even at the decompressor entry. Patch 061 removes that ambiguity
with an explicit sentinel protocol and turns stext stub B into a re-polling handoff
trampoline.

### 23.1 Protocol [V]

* marker A (0x80600010): initialised by the decompressor probe code to the sentinel
  0x600D1A88; a marker-only spin stub at 0x80600040 overwrites it with 0x80000001
  when entered.
* marker words at 0x80600020/0x80600024/0x80600028: sentinel 0x600D1A88, 0x600D1A88,
  0; the stext trampoline at 0x80600080 writes 0x80000001 to 0x80600020 when
  entered, and on a pen change writes 0x600D2B61 to 0x80600024 and the new pen value
  to 0x80600028 before branching there.
* the trampoline page 0x80600000-0x80600fff is memblock_reserved from an early_param
  handler (before arm_memblock_init/paging_init), so the kernel can never allocate or
  reuse the stub/markers.

Reading rules: sentinel = never entered; 0x80000001 = entered; 0x600D2B61 = the
trampoline saw the pen change and jumped to the recorded target.

### 23.2 Trampoline mode (token luofu_smp_trampoline=1)

With the token, the release path writes **pen = __pa_symbol(secondary_startup)**
instead of the 035 probe stub. The stext trampoline is already polling the pen, so
when __cpu_up() has secondary_data prepared and the release runs, CPU1 sees the pen
change, records the jump, and branches into the real secondary entry - the normal
2-CPU path. Without the token, 061 behaves exactly like 060 (marker-only stubs).

No relocation or cache maintenance is required for the trampoline:

* the code is position independent (movw/movt immediates and PC-relative branches
  only) and is entered with the MMU and caches off, at its physical address;
* it is written once by stext code that also runs with caches off, so the bytes are
  already in DRAM (point of coherency) - no clean/invalidate is needed for the code;
* because the page is reserved, CPU0 never allocates, writes or re-reads the code
  afterwards; marker reads from the kernel use __sync_cache_range_r() invalidation
  first, so a stale clean line cannot hide an update.

### 23.3 Expected output

At early_param time:

    luofu-smp: trampoline page 0x80600000-0x80600fff reserved; release pen = secondary_startup
    luofu-smp: handoff A=600d1a88 B0=600d1a88 B1=600d1a88 B2=00000000 pen=80600080 scu_cfg=00000511
    luofu-smp: handoff verdict A=not-entered B(trampoline)=not-entered jump=no target=00000000
    luofu-smp: handoff cpu1-ran monitor scu_cfg bit5=clear

At prepare, after the release and in late_initcall:

    luofu-smp: handoff prep A=600d1a88 B0=600d1a88 B1=600d1a88 B2=00000000 A=not-entered B=not-entered jump=no target=00000000
    luofu-smp: handoff prep cpu1-ran monitor scu_cfg=00000511 bit5=clear
    luofu-smp: release cpu=1 logical=1 pen=0x8070b000 entry=8070b000 probe=1 tramp=1 force=0
    luofu-smp: handoff post-release A=... B0=... B1=... B2=... A=... B=... jump=yes/no target=...

(pen=0x8070b000 is whichever __pa_symbol(secondary_startup) is in the built
System.map.)

### 23.4 Interpretation

| A | B0 | B1 | meaning | next step |
| --- | --- | --- | --- | --- |
| sentinel | sentinel | sentinel | CPU1 never entered either stub: it is not polling the pen before the release, on the loader path of our image | loader-side cause: the loader does not start CPU1 for our boot; decode the loader and test image-type sensitivity |
| 0x80000001 | sentinel | sentinel | CPU1 entered probe A (decompressor entry): it was polling very early, then the decompressor/window killed it | bisect inside the decompressor |
| sentinel | 0x80000001 | sentinel | CPU1 entered the stext trampoline and is still polling; no handoff yet (pen unchanged) | if an add_cpu(1) then writes the entry pen, the trampoline should jump and the jump marker proves it |
| sentinel | 0x80000001 | 0x600D2B61 | the trampoline jumped to the recorded target: a real handoff happened | check /proc/cpuinfo and the online line; if target == secondary_startup and CPU1 is not online, the failure is inside the secondary entry |

The trampoline only helps if CPU1 actually reaches it; the 060 result says it does
not on our current loader path. So 061 is both the clean re-probe (sentinel protocol
removes the pattern ambiguity permanently) and the handoff channel for any future
run in which CPU1 does start polling.

---

## 24. FINAL: SMP resolved - the root cause was the loader handoff DT contract (2026-11-21)

Status: closed. CPU1 comes up on the 6.18 kernel; the failure was in the
bootloader handoff staging, not in any kernel SMP code. This section consolidates
the measured root cause, the fix, the disposition of the diagnostic patches, and the
corrections to earlier conclusions.

### 24.1 Root cause [V]

U-Boot's bootfip handoff staging aborts when the board/kernel devicetree lacks two
nodes that the stock vendor base devicetree carries:

1. /aliases/console - resolved to the console UART node. The stock base DTB has
   aliases { console = "/uart1@0x1010f000"; serial0 = ...; serial1 = ...; }.
2. /reserved-memory/flashinfo@80600000 - compatible "hsan,flashinfo_reserved",
   reg = <0x80600000 0x4000>, atag-offset = <0x1000>. bootfip reads the node, reads
   atag-offset and computes atag_addr = 0x80601000.

Without them the staging path bails early: our UART showed the vendor warning
"alias console not found" and the staging lines (alias console=..., serialnr->high=
0x8250, serialnr->low=0x1010f000, atag_addr=0x80601000) never appeared. CPU1 was
therefore never running the U-Boot start.S pen-poll loop when the kernel ran, so
every correctly-decoded kernel release (pen write -> CRG resume clear -> wakeup IPI)
had no consumer - which is exactly what patches 033..042 measured repeatedly, and
what the 060/061 sentinel probes proved (CPU1 did not enter the stubs at the
decompressor entry, at stext, or at late time).

With the two DT nodes added to our board DTS the staging completes (the stock
staging lines printed), CPU1 is released into its poll loop, consumes the real
secondary_startup pen at the kernel release and boots:

    smp: Brought up 1 node, 2 CPUs
    CPU1: thread -1, cpu 1, socket 0, mpidr 80000001
    /proc/cpuinfo = 2, online = 0-1, CPU0/CPU1 interrupts present,
    SCU CONFIG bit 5 = set (the CPU1-ran monitor from section 20),

and the 060/061 sentinels remained untouched because CPU1 consumed the real
secondary_startup pen instead of a diagnostic stub. That contrast is the clean proof
that the loader handoff staging, not the kernel release, was the missing piece.

[I] The internal mechanism inside bootfip (FIP staging / warm-reset / firmware BL2/
BL31 path) is consistent with the strings and code decoded during the loader work but
is not instruction-level proven; it is no longer needed for the fix.

### 24.2 The fix: DTS-only

target-skeleton/dts/luofu-r116.dts (and the same nodes in the upstream submission
DTS):

    aliases {
        serial0 = &uart1;
        serial1 = &uart0;
        console = &uart1;      /* vendor base DTB contract; U-Boot bootfip staging */
    };

    reserved-memory {
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        flashinfo@80600000 {
            compatible = "hsan,flashinfo_reserved";
            reg = <0x80600000 0x4000>;
            atag-offset = <0x1000>;
        };
    };

No kernel code change is required for SMP functionality: the vendor-faithful release
and the hisilicon,luofu-smp enable-method implementation already in the series are
correct. The missing piece was the loader handoff devicetree contract that the stock
base devicetree provides.

### 24.3 Disposition of the diagnostic patches [V]/[I]

* patches 060 (handoff probes) and 061 (sentinel protocol + re-poll trampoline) are
  bench-only diagnostics. They should not be part of a release/upstream series; keep
  them in the bench set or drop them. They were designed to answer whether CPU1 ever
  entered the loader poll loop, and they did exactly that.
* patches 033/034/035/037/039/042/059 are likewise bench instrumentation (dumps,
  probe stubs, late add_cpu trigger, bookkeeping). A release series should carry the
  minimal functional SMP patch set plus the DTS fix; the diagnostics can stay compiled
  in default-off on bench images but are not needed for the fix.
* The canonical bench image 974e6ca6 (documented by the lead) verifies the fix: 2
  CPUs, CPU1 thread mpidr 80000001, ping 20/20 0% loss, SSH, rw overlay, soak
  running.

### 24.4 Corrections to earlier conclusions

* The "2-CPU image hung after 23 minutes" was a HOST bench-link outage (USB Ethernet
  lost carrier/IPs overnight). UART showed the box alive with no Oops/panic/BUG/
  WARNING (only the literal panic=0 command line), and after power-cycle/link restore
  the same image is stable. There is no SMP stability bug. Bench lesson: check host
  link carrier before attributing a hang to the kernel; the UART is the authority.
* The W9 traffic crash (section 19) remains a net-lane item; it was not SMP.
* The loader-side open items from sections 22/23 - U-Boot staging copy content at
  0x80000000, hboot being overwritten in DRAM, the unreadable 0x1017 block - are
  normal handoff artifacts and are not needed for SMP.
* The "2 CPUs" sighting in the W9 log was correctly re-attributed to the stock 5.10
  fallback (section 19); the sentinel protocol then proved definitively that our
  kernel never had CPU1 in the poll loop before the DT fix.

### 24.5 Upstream / repository actions

1. Commit the two DT nodes into the target DTS (and mirror in the upstream submission
   DTS); reference this section as the measured loader handoff contract.
2. Keep the DTS nodes minimal and documented: console alias + flashinfo
   reserved-memory (reg/atag-offset); nothing else was needed for CPU1.
3. Release series: functional SMP driver + DTS fix; bench diagnostics out (or gated,
   default off). Verify a clean image with no luofu_smp_* command-line tokens and no
   probe stubs boots to 2 CPUs (canonical 974e6ca6 evidence).
4. Ledger/bench notes: record the host bench-link outage that caused the false hang
   attribution.
