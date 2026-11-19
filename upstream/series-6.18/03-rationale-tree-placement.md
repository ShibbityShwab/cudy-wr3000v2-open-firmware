# Rationale - tree placement, series order and conflicts

This note records why the luofu 6.18 series is placed, ordered and
numbered the way it is, and which conflicts a real submission has to
expect.  It is repo-facing: a subsystem cover letter only needs the short
version (dependency order, loader dependency, no hardware validation).

## 1. Tree placement summary

All paths verified against pristine linux-6.18.60 [V]; none of the new
symbols, files or directories exist in that tree (grep for luofu returns
nothing in c/h/yaml/Kconfig/Makefile content; no mach-hisilicon and no
drivers/pinctrl/hisilicon directory).

| block | series patches | placement | why / alternative |
| --- | --- | --- | --- |
| machine | 0001, 0002 | new arch/arm/mach-hisilicon/, ARCH_LUOFU under existing ARCH_HISI | new SoC family, not part of mach-hisi; see section 2 |
| SMP bring-up | 0018 | new arch/arm/mach-hisilicon/platsmp.c | CPU_METHOD_OF_DECLARE("hisilicon,luofu-smp"); no new binding, reuses the CRG/SYSCTRL syscon nodes (0004/0010); staged separately, see section 9 |
| early boot / TEXT_OFFSET | 0003 | arch/arm/kernel/head.S, arch/arm/boot/compressed/head.S | general ARM change; see section 3 |
| CRG clocks/resets | 0004, 0014, 0019 | drivers/clk/hisilicon/crg-luofu.c | existing hisilicon clk directory |
| CRG dt-bindings header | 0004, 0019 | include/dt-bindings/clock/hisilicon,luofu-crg.h | clock-ID ABI, next to the binding |
| CRG binding | 0004, 0014, 0019 | Documentation/devicetree/bindings/clock/ | one flat binding file |
| watchdog | 0005 | drivers/watchdog/luofu-wdt.c | top-level watchdog menu; no subsystem subdirectory |
| FMC SPI-NAND | 0006 | drivers/spi/spi-luofu-fmc.c | it is a SPI-MEM controller, not a raw-NAND controller; binding under mtd/ (6.18 has no mtd/nand binding dir) |
| MDIO | 0007 | drivers/net/mdio/mdio-luofu.c | existing mdio-hisi-femac.c precedent |
| PHY | 0008 | drivers/net/phy/luofu-phy.c | PHYLIB driver matched by PHY ID; no binding on purpose |
| GEMAC | 0009, 0020, 0023 | drivers/net/ethernet/hisilicon/luofu-gmac.c | existing hisilicon net directory; 0020 adds the phylink/netdev front end, 0023 the readiness-gated registration, carrier fix and MAC source |
| restart | 0010 | drivers/power/reset/luofu-reboot.c | machine restart policy as a platform driver, next to hisi-reboot.c |
| sysctrl binding | 0010 | Documentation/devicetree/bindings/soc/hisilicon/ | syscon page binding |
| pinctrl | 0011 | new drivers/pinctrl/hisilicon/ | no hisilicon pinctrl directory exists in 6.18; new dir needs Kconfig source + obj-y entries |
| thermal | 0012 | drivers/thermal/luofu_tvsensor.c | existing hisi_thermal.c precedent (underscore naming matches that file) |
| PCIe host | 0013 | drivers/pci/controller/dwc/pcie-luofu.c | DWC host glue belongs with the other DWC host drivers |
| PIE | 0015, 0020 | drivers/net/ethernet/hisilicon/luofu-pie.c | Ethernet data path, next to GEMAC; 0020 is the arm-gated ring engine |
| PIE/GEMAC interface | 0020 | drivers/net/ethernet/hisilicon/luofu-pie.h | private front-end/engine interface shared by luofu-gmac.c and luofu-pie.c; not a stable ABI |
| LSW switch | 0016, 0021, 0022 | drivers/net/ethernet/hisilicon/luofu-switch.c | switchdev over per-port netdevs; no DSA tag exists; 0021 is the read-only D1b baseline probe and 0022 the gated W3/W4/W5 write path |
| LSW register map | 0021, 0022 | drivers/net/ethernet/hisilicon/luofu-switch-regs.h | measured register/qid-map map with per-item evidence grade; 0022 adds the write-set documentation |
| LSW/GEMAC readiness interface | 0022 | drivers/net/ethernet/hisilicon/luofu-switch.h | private hidden-engine readiness contract (probed/ready) consumed by the GEMAC front end; not a stable ABI |
| boot selector | 0017 | drivers/soc/hisilicon/luofu-selector.c | SoC sysfs interface, next to kunpeng_hccs.c |
| board DTS | not in series | future arch/arm/boot/dts/hisilicon/luofu-r116.dts + Makefile | section 4 |

## 2. mach-hisilicon vs mach-hisi

arch/arm/mach-hisi/ exists in 6.18 and defines ARCH_HISI (a menuconfig
gating the hi3xxx/hiP0x boards: hi3519, hi3620, hip01, hip04, x5hd2).
The luofu SoC is a different family with a different boot story and a
different external bus; it is not one more hi3xxx board.  Two placements
were possible:

1. new arch/arm/mach-hisilicon/ with ARCH_LUOFU `depends on ARCH_HISI`
   (what 0001/0002 do).  ARCH_HISI stays the common umbrella, the machine
   directories stay separate, and `machine-$(CONFIG_ARCH_LUOFU) +=
   hisilicon` compiles both directories when both symbols are enabled.
2. a board file inside mach-hisi/.  That would put a Cortex-A9 DT-only
   platform into the hi3xxx machine directory and make mach-hisi own two
   unrelated boot paths; it also mixes the machine Kconfig menus.

Option 1 is proposed, but this is exactly the kind of naming decision the
arm-soc maintainers should make in review: a new top-level machine
directory needs a user (driver series/DTS) and a reason.  The RFC cover
letter asks for the decision before more platform code is written.  If
arm-soc prefers `arch/arm/mach-hisi/luofu/` or a different name, 0001-0003
are small and self-contained enough to redo.

ARCH_HISI must not be redefined: 0001's Kconfig lives in a new file that
is sourced from arch/arm/Kconfig after the existing mach-hisi source line,
and its symbol depends on ARCH_HISI.  The existing
`machine-$(CONFIG_ARCH_HISI) += hisi` line is untouched.

## 3. ARM core changes (0002/0003) and why the text offset forces them

The series needs TEXT_OFFSET 0x00608000 because the stock loader stages the
kernel at 0x83200000 and the decompressor computes
`(pc & 0xf8000000) + TEXT_OFFSET`; the uImage load/entry address is
0x80608000.  This is a bootloader contract, not a design choice.

003 changes four `#TEXT_OFFSET` use sites to the kernel's portable `mov_l`
macro because 0x00608000 cannot be encoded as an ARM immediate.  This is
the only patch in the series that touches generic ARM code, and it is
therefore the one with the widest review surface: it changes early boot
for every ARM platform (no functional change for existing offsets, but the
generated instructions differ).  Recommended handling:

1. post 0003 as its own small patch ("ARM: head: materialize TEXT_OFFSET
   with mov_l") to Russell King's tree, explaining the new user;
2. keep it ahead of the luofu platform patches in the series so that
   bisection never has an unbuildable platform.

Alternative considered and rejected: special-casing the luofu value in
head.S (worse), or a per-platform relocate-in-C path (much larger change).

## 4. DTS delivery

Three options, one source of truth rule.

1. **OpenWrt target dts/** (current implementation): the board DTS is
   `target/linux/hisilicon/dts/luofu-r116.dts` and is compiled by the
   target's `DEVICE_DTS_DIR`; the overlay is renamed `-overlay.dtso`
   because the OpenWrt overlay rule only compiles .dtso.  This is the
   right vehicle for the OpenWrt target PR and needs no kernel patch.
2. **Kernel DTS patch**: `arch/arm/boot/dts/hisilicon/luofu-r116.dts` plus
   a `dtb-$(CONFIG_ARCH_LUOFU) += luofu-r116.dtb` line.  The directory
   already exists in 6.18 (hi3xxx DTS files), so this is a file+Makefile
   addition.  This is the only upstream proper form for the kernel and it
   requires a clean full-draft `dtbs_check` and bindings for every node.
3. Both hand-maintained: rejected (TARGET-DESIGN.md 6.1).  Two copies of a
   1,000-line DTS will diverge.

Recommendation: the DTS is deliberately **not** in this series.  The draft
DTS is TODO-heavy (only the nodes with bindings above are real; the rest
are proposal nodes) and a full-draft `dtbs_check` is not clean, so it
cannot be submitted yet.  Keep option 1 as the delivery for the OpenWrt
target, treat `opensource/target-skeleton/dts/luofu-r116.dts` as the single
source, and generate/copy the kernel file only when a DTS series is posted
after the bindings are accepted.  Do not maintain two copies in parallel.

The loader-match overlay is a separate artifact: it exists because the
stock loader matches FIP overlays by vendor_id/board_id.  It is not a
kernel DT overlay and must not be posted as one; the kernel DTS is the
base board tree.

## 5. Kconfig symbol placement and naming

Kconfig is where most review nits will come from.  Current placement
(verified by applying the series; files and menus as in 6.18.60):

| symbol | file | dependencies / selects | note |
| --- | --- | --- | --- |
| ARCH_LUOFU | arch/arm/mach-hisilicon/Kconfig (sourced by arch/arm/Kconfig) | depends on ARCH_HISI; select CACHE_L2X0; select HAVE_ARM_SCU/TWD if SMP | UP-only posture |
| LUOFU_CRG | drivers/clk/hisilicon/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST; COMMON_CLK; select MFD_SYSCON, RESET_CONTROLLER | bool; 0019 adds no symbol |
| LUOFU_WDT | drivers/watchdog/Kconfig (global menu) | depends on ARCH_LUOFU \|\| COMPILE_TEST; select MFD_SYSCON, WATCHDOG_CORE | tristate |
| LUOFU_FMC | drivers/spi/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST; MTD_SPI_NAND | name should probably be SPI_LUOFU_FMC (subsystem prefix) |
| LUOFU_MDIO | drivers/net/mdio/Kconfig | depends on HAS_IOMEM && OF_MDIO | no ARCH_LUOFU dependency (bus driver) |
| LUOFU_PHY | drivers/net/phy/Kconfig | none | PHY-ID matched; tristate |
| LUOFU_GMAC | drivers/net/ethernet/hisilicon/Kconfig, inside `if NET_VENDOR_HISILICON` + `if ARM \|\| ARM64 \|\| COMPILE_TEST` | depends on OF, depends on LUOFU_PIE; selects PHYLINK (0020) | net vendor menu convention; 0020 changes CONFIG_PHYLINK m -> y in the resolved config |
| LUOFU_PIE | same file, before `endif #HNS3` (inside both ifs) | depends on OF | draft arm-gated ring engine (0020); tristate |
| LUOFU_SWITCH | same file, immediately before `endif #NET_VENDOR_HISILICON` | depends on OF | outside the ARM if - inconsistent with PIE/GMAC, should move inside for consistency; read-only D1b probe (0021) |
| PCIE_LUOFU_HOST | drivers/pci/controller/dwc/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST, OF, PCI_MSI; select PCIE_DW_HOST | bool |
| LUOFU_REBOOT | drivers/power/reset/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST; select MFD_SYSCON | bool |
| PINCTRL_LUOFU | new drivers/pinctrl/hisilicon/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST, OF; select PINMUX, PINCONF, GENERIC_PINCONF | bool |
| LUOFU_TVSENSOR | drivers/thermal/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST, HAS_IOMEM, OF | tristate |
| LUOFU_SELECTOR | drivers/soc/hisilicon/Kconfig | depends on ARCH_LUOFU \|\| COMPILE_TEST, MFD_SYSCON | tristate |

Review points:

1. **Gating is inconsistent.** CRG/WDT/PCIe/REBOOT/PINCTRL/TVSENSOR/
   SELECTOR use `ARCH_LUOFU || COMPILE_TEST`; the net drivers use the
   NET_VENDOR_HISILICON menu convention and MDIO/PHY have no SoC
   dependency.  This is defensible (it keeps COMPILE_TEST coverage and
   matches each subsystem's neighbours) but it should be a deliberate,
   stated policy rather than an accident.  Decide before posting: either
   document the per-subsystem convention in the cover letter, or add
   `ARCH_LUOFU || COMPILE_TEST` everywhere for consistency.
2. **LUOFU_FMC** is in drivers/spi but lacks the SPI_ prefix used by every
   neighbour.  Rename to SPI_LUOFU_FMC (or argue why not) before the spi
   maintainers see it.
3. **LUOFU_SWITCH vs LUOFU_PIE** placement inside the file differs (one in
   the ARM if, one outside).  Move LUOFU_SWITCH inside `if ARM || ARM64 ||
   COMPILE_TEST` with PIE/GMAC; no functional bug, but reviewers notice
   (0009/0015/0020 are inside, 0016/0021 outside).
4. ~~0017 adds one extra blank line before `config LUOFU_SELECTOR`~~ -
   **fixed 2026-11-18** in the corrected patch (now one blank line); keep it
   clean when editing the patch again.
5. The new drivers/pinctrl/hisilicon/ directory needs both the
   `source`/`obj-y` lines in drivers/pinctrl/{Kconfig,Makefile} (0011 has
   them) and its own Kconfig/Makefile; add this directory to MAINTAINERS
   (see 02-maintainers-proposal.md).
6. The CRG, watchdog and reboot Kconfig entries all select MFD_SYSCON; the
   CRG node must stay the syscon/simple-mfd parent (0004) for the watchdog
   child (0005) and the sysctrl consumers (0010, 0017) to share one
   regmap.  If review asks for a different syscon layout, 0005/0010/0017
   all need a matching change.
7. 0018 adds no Kconfig symbol: it adds `obj-$(CONFIG_SMP) += platsmp.o`
   to the new machine Makefile and updates the ARCH_LUOFU help text.  No
   config delta is needed (CONFIG_SMP=y and the HAVE_ARM_SCU/TWD selects
   already come from 0001/0002); see SMP-RE.md section 3.
8. 0019 adds no Kconfig symbol and no config line either: it extends the
   existing CONFIG_LUOFU_CRG driver, its clock-ID header and its YAML
   binding only.  The recorded 001-0019 target build needed no config
   delta; the kernel_oldconfig re-derivation stayed byte-idempotent.  The
   new inputs (the seven fixed clocks and the gate parents) are board-DTS
   content, not Kconfig content, and they are not part of this kernel
   series.
9. 0020 changes one resolved kernel config symbol: LUOFU_GMAC now depends
   on LUOFU_PIE and selects PHYLINK, so CONFIG_PHYLINK resolves `y`
   instead of `m`; the PIE-datapath lane verified the config delta as
   symbol-identical through a canonical `make kernel_oldconfig`.  0021
   adds no Kconfig symbol (CONFIG_LUOFU_SWITCH already exists).  Both
   patches add only private headers, which the MAINTAINERS entry lists
   explicitly (see 02-maintainers-proposal.md).

## 6. Series order, dependency matrix and bisectability

Consolidated order is 0001 -> 0021.  Compile-time and DT-time
dependencies:

| patch | needs (compile) | needs (DT behaviour) |
| --- | --- | --- |
| 0001 | - | - |
| 0002 | 0001 (ARCH_LUOFU symbol) | - |
| 0003 | 0002 (new textofs value) | - |
| 0004 | 0001/0002 | - |
| 0005 | - (Kconfig ARCH_LUOFU if gated) | 0004 (CRG syscon parent) |
| 0006 | 0004 (clock/reset IDs) | 0004, 0014 (reset polarity) |
| 0007 | 0004 (clock/reset IDs) | 0004, 0014 |
| 0008 | - | 0007 (MDIO bus) |
| 0009 | 0004 | 0004, 0014, 0007, 0008 |
| 0010 | 0004 header only at DT level | 0004, 0010's own sysctrl binding |
| 0011 | - | - |
| 0012 | - | - |
| 0013 | 0004 (clock IDs) | 0004, 0014 |
| 0014 | 0004 | - |
| 0015 | 0004 | 0004, 0014 |
| 0016 | 0004 | 0004, 0014 |
| 0017 | - | 0010 (sysctrl binding, shared node) |
| 0018 | 0001 (it edits files 0001 creates), 0002 (ARCH_LUOFU/SMP posture) | 0004 (CRG syscon node), 0010 (SYSCTRL node); the DT enable-method is board-DTS content, not in this series |
| 0019 | 0004 (it edits the CRG driver, clock-ID header and YAML binding 0004 adds) | 0004 (CRG syscon node); the seven input clocks, the gate-parent clocks and the two mux parents are board-DTS content, not in this series |
| 0020 | 0009, 0015 (it rewrites the GEMAC and PIE drivers and bindings they add); 0004/0014 headers at compile time | 0004, 0014 (CRG clock/reset ABI); the ring tables and node enablement are board-DTS content, and the measured inventory is outside the series |
| 0021 | 0016 (it rewrites the switch driver and binding 0016 adds), 0020 (same ethernet Kconfig file, applied before it), 0004 (CRG state) | 0004, 0014 (CRG gate/reset state, read-only); the 17-window DTS model, CRG phandle and port bitmap are board-DTS content, and the measured inventory is outside the series |

**Bisectability problem: 0014 is too late.**  0014 fixes the CRG reset
polarity (active-low), and it is placed after 0006, 0007 and 0013, which
all consume resets through the CRG provider.  At those commits the CRG
driver would assert instead of release.  The final tree is correct, but
the series is not bisect-clean.  Two fixes:

1. **preferred:** fold 0014 into 0004 (it fixes code introduced by 0004 in
   the same submission, so a separate "fix" patch is noise upstream), or
2. if the patch stack is kept for the OpenWrt target, move 0014 to
   position 0005 (before any consumer) and note it.

The consolidated series keeps the discovery-order numbering so the mapping
table stays 1:1 with the target-skeleton patches; the real submission is
split by subsystem anyway.

Other ordering constraints:

- 0001-0003 must be the first series: 0002/0003 make the platform build.
- 0004 (CRG provider) must precede 0005/0006/0007/0009/0013/0015/0016 and
  the CRG binding must be accepted before its consumers, otherwise the
  consumers' DT nodes cannot be validated.
- 0010 and 0017 share the SYSCTRL page; 0010 introduces the
  `hisilicon,luofu-sysctrl` binding and 0017 adds the selector interface.
  Either order works for compilation; the restart patch should come first
  because the binding is there.
- 0011/0012 are independent of the rest and can be reviewed in parallel.
- 0018 edits files created by 0001 and maps the syscon nodes from
  0004/0010.  It is not part of the UP-first submission; the staging
  (S1 UP-first, S2 attended) is section 9.
- 0019 is the last of the clock/peripheral block: it extends the CRG
  provider added by 0004 and builds on the reset-polarity fix 0014.  A
  real submission either folds it into the CRG series after 0004/0014 or
  posts it as a follow-up clock-tree patch; it must not precede 0004.
- 0020 must follow 0009 and 0015 (it rewrites the drivers and bindings
  those patches add) and 0004/0014 (the CRG clock/reset ABI it uses).  It
  is the last of the two net data-path patches.
- 0021 must follow 0016 (same driver and binding) and 0020 (same
  `drivers/net/ethernet/hisilicon/Kconfig` file; 0020's insertion comes
  first, so 0021 applies with a 6-line offset).  It is the last patch of
  the consolidated series.
- Both 0020 and 0021 depend on the 2026-11-18 measured inventory
  (`../../docs/INVENTORY-2026-11-18.md`), which is a bench artifact and not a
  patch; section 10 records the staging.

## 7. Conflicts and merge-order hazards

Files touched by more than one patch (a real conflict surface when the
series is split across subsystem trees):

| file(s) | patches | hazard |
| --- | --- | --- |
| arch/arm/Kconfig, arch/arm/Makefile | 0002 (0003 separate tree) | arm-soc/ARM tree churn; rebase before sending |
| arch/arm/mach-hisilicon/{Kconfig,Makefile,luofu.c} | 0001, 0018 | 0018 edits files introduced by 0001 and is last in the series; no in-series conflict, but a split submission must rebase it on the platform series |
| arch/arm/kernel/head.S, arch/arm/boot/compressed/head.S | 0003 | ARM core tree; conflicts with any early-boot change |
| drivers/clk/hisilicon/crg-luofu.c, binding yaml, clock-ID header | 0004, 0014, 0019 | fold 0014 and 0019 into 0004, or post 0019 as the follow-up clock-tree patch after the CRG provider series |
| drivers/net/ethernet/hisilicon/{Kconfig,Makefile} | 0009, 0015, 0016, 0020, 0021 | five insertions in the same two files; order is 0009 -> 0015 -> 0016 -> 0020 -> 0021 in the consolidated tree.  0020 makes LUOFU_GMAC depend on LUOFU_PIE, and 0021's Kconfig hunk applies with a 6-line offset because 0020 edits the same file first.  If 0009 lands alone, 0015/0016/0020/0021 need a rebase (they are drafts, so this should not matter soon) |
| drivers/net/ethernet/hisilicon/Kconfig | 0009/0015/0020 inside `if ARM || ARM64 || COMPILE_TEST`, 0016/0021 outside it | move both outside entries inside (see section 5) |
| drivers/net/ethernet/hisilicon/luofu-{gmac,pie,switch}.c and their bindings | 0009, 0015, 0016, 0020, 0021 | 0020 and 0021 rewrite files and bindings created by the earlier draft patches; keep the draft block together when rebasing |
| Documentation/devicetree/bindings/soc/hisilicon/ | 0010 (sysctrl yaml), 0017 (boot-selector yaml) | different files, no textual conflict; both reference the same syscon node in the DTS, so the DTS patch must wait for both |
| drivers/soc/hisilicon/{Kconfig,Makefile} | 0017 | no conflict, but the Kconfig file is small; rebase against kunpeng changes |
| drivers/pinctrl/{Kconfig,Makefile} | 0011 | new directory + 2 top-level insertion lines; low risk |
| drivers/spi, drivers/net/mdio, drivers/net/phy, drivers/thermal, drivers/pci/controller/dwc, drivers/power/reset, drivers/watchdog | one patch each | low risk; subsystem trees move fast, rebase before sending |

The consolidated series applies cleanly in order to pristine 6.18.60
(validated; only 0008 and 0021 have offsets - see 04-validation.md).  That
does not remove the split-submission conflict risk: rebase each subsystem
series on the receiving tree before posting, especially 0002/0003 (ARM
core) and the net/ethernet/hisilicon net series, whose draft block
(0009/0015/0016/0020/0021) is rewritten in place by its own follow-ups.

## 8. Why the series files are numbered 0001-0021

The four-digit filenames under `series/` are a consolidated review form and
are not the numbering a maintainer will see.  A real submission renumbers
per subsystem series and always puts bindings before drivers:

    [PATCH 0/4] clk/reset: ... luofu CRG        (binding, header, driver, Kconfig)
    [PATCH 0/3] watchdog: ... luofu watchdog    (binding, driver, Kconfig)
    ...

The `series/NNNN-*.patch` files are format-patch style with
`[PATCH NN/21]` subjects so that `git am` reproduces the commit subjects
and messages exactly; the mapping table in 01-commit-messages.md maps each
file back to its source patch in
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`.

## 9. SMP staging (patch 0018) - UP-first vs attended

Patch 0018 is the only patch in the series that can bring up a second
core, and it is deliberately staged outside the UP-first mergeable set
(00-cover-letter.md scope item 5, 01-commit-messages.md group 5).  The
decision is made in the devicetree, not by the kernel patch: without a
matching `/cpus/enable-method` the method cannot bind, the code stays
inert and the platform boots UP.

**S1 - UP-first (no SMP claim).**  Keep 0018 in the series but do not
enable it: drop the one-line `/cpus/enable-method = "hisilicon,luofu-smp"`
hunk from the board DTS (the hunk lives in the target's DTS, not in this
kernel series), and for an upstream DTS submission ship a single
`cpu@0` node.  The failure mode to avoid is known from the bench: on
5.10, two DT CPUs with no matching method left both CPUs marked possible,
CPU1 never came up, and the box faulted in the scheduler about 14 s into
userspace, while `maxcpus=1` was stable (parent ledger 2026-10-12,
phases 81/82) [V].  The same shape on 6.18 (two CPU nodes, no matching
method) cannot boot CPU1 either [I] - so S1 needs the hunk dropped and,
upstream, a single cpu@0 node.  The current consolidated form describes
the S2 end state, not S1.

**S2 - attended bring-up (the acceptance gate).**  Add the
enable-method (and `cpu@1`) and run single-variable fires with the
recovery door armed and the UART capture read-only.  Acceptance is the
SMP-RE.md section 8 list: "smp: Brought up ... 2 CPUs" in the boot log,
both processors in /proc/cpuinfo, per-CPU timer/IPI activity in
/proc/interrupts, a >=10 minute soak, the pen/resume registers read back
before and after the release, one repeat cold boot and the recovery path
re-verified.  The physical semantics of the release remain [U]: the
pen-word consumer, the CRG resume-bit function and the SCU pre-state have
not been measured.

Why the values are driver data and not DT properties: pen offset 0xc00,
resume offset 0x38 and the per-core resume bit numbers are
vendor-private, undocumented values.  Exposing them would add unmeasured
properties to the otherwise reviewable luofu bindings, while the
existing SYSCTRL/CRG syscon nodes already provide the two pages
(SMP-RE.md section 5.5).  No CPU hotplug is advertised:
`CONFIG_HOTPLUG_CPU` stays off, matching the vendor operations table
(SMP-RE.md sections 3.2 and 7.5).

## 10. PIE and LSW staging (patches 0020 and 0021)

Both patches are follow-ups to drafts already in the series and both
encode the 2026-11-18 measured inventory.  They are staged as untrusted
drafts, not as working data paths, and both are grouped with the
RFC/partial drafts in 00-cover-letter.md and 01-commit-messages.md.

**Measured-inventory dependency.**  0020 and 0021 embed values measured on
the stock vendor system by the read-only inventory run of 2026-11-18
(`../../docs/INVENTORY-2026-11-18.md`): the PIE channel/ring/IRQ
configuration for 0020, and the LSW 30-word baseline plus the 384 qid-map
words for 0021.  That measurement is the dependency: it is not in any
patch and cannot be re-derived from the tree, and both drivers are
fail-closed when the measured state does not hold.  A second boot/unit
inventory is still owed before the baseline can be called stable.

**Ordering.**  0020 must follow 0015 (it rewrites `luofu-pie.c` and its
binding) and 0009 (it rewrites `luofu-gmac.c` and its binding/Kconfig
entry).  0021 must follow 0016 (it rewrites `luofu-switch.c` and its
binding) and applies after 0020, because 0020 edits the same
`drivers/net/ethernet/hisilicon/Kconfig` first (it applies with a 6-line
offset in the consolidated series).  Both sit after 0004/0014, whose CRG
clock/reset ABI they consume (0021 read-only).

**Staging.**  0020 keeps the PIE engine behind the kernel command-line
`luofu_pie.arm=1` gate: probe validates the resources and ring tables and
then refuses with -EOPNOTSUPP before the first register write, both
devicetree nodes stay disabled, and the armed path (the only PIE MMIO
write path in the series) has never run.  The RX completion decode and the
internal-switch egress map are still [U], so no frame is delivered even
when armed.  0021 contains no `writel()`, enables no clock, drives no
reset and always refuses probe; it validates the measured powered state,
the baseline words and the qid maps read-only and never programs a table.
The write campaigns that gate them are one attended, recovery-armed PIE
arm session (`target-skeleton/docs/port-notes/pie/SUMMARY.md` section 6)
and the LSW W2-W6 sequence (`target-skeleton/docs/port-notes/lsw/SUMMARY.md`
section 5, LSW-SPEC.md section 12.3).  Until those run, both patches are
`[RFC PATCH]` design-review material only; 0021's read-only property means
it can be reviewed without any write risk.
