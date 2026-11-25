# Release line - luofu 6.18 target, 2026-11-21

> **Status: bench-verified 2-CPU baseline; NOT a flashable or public release.**
> The canonical image `974e6ca6` was installed manually from the stock system on
> 2026-11-21 and verified on the unit (2 CPUs, network, SSH, persistent overlay,
> soak 0 failures, persistent across reboot).  Nothing in this directory has been
> upstream-reviewed; Wi-Fi is vendor-only [X]; the fail-closed install path
> (`platform_do_upgrade`) has still never been executed on a device; native CFE
> forwarding is a separate project.

This document is the source of truth for what "the release line" means after the
2026-11-20/21 SMP work, and for what is deliberately excluded from it.  It sits
next to the upstream review form (`series/0001-00NN.patch`, patches 001-026) and
does not replace it: the review form is the kernel-submission artefact, the
release line is the target patch stack that built and booted the verified image.

## 1. Three layers (do not conflate them)

| layer | what it is | size |
| --- | --- | --- |
| **L0 lab patch stack** | every file in `../../target-skeleton/target/linux/hisilicon/patches-6.18/`, applied in filename order by the OpenWrt target build | 67 patch files (001-060 + 062-067 plus `013a`; no duplicate numbers) |
| **L1 release line** | L0 minus the bench-only SMP diagnostic patches (section 6); this is the series a release/publish snapshot carries.  **v3 2026-11-29: 013a (the load-bearing GP1 endpoint-reset pulse) is included, so L1 is 56** (section 13) | **56 patch files** (`release-line/INCLUDED.txt`, manifest `release-line/manifest.tsv`) |
| **L2 upstream review form** | the kernel-only submission form in `series/` plus `00-06` docs | 26 patches (001-026), grouped merge-intent / RFC / do-not-merge |
| **L3 DTS series entry (DTS-0001)** | the companion DTS patch the kernel-only form cannot carry: the board DTS with the two loader hand-off nodes required by 0018 | 1 patch, `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` (section 2, `06-dts-series-dependency.md`) |

L1 is new here.  L0 is what the canonical image was built from; L2 is unchanged
except for the documentation corrections in this update.  The loader hand-off DTS
fix (section 2) is part of the board devicetree supplied by the **target tree**
(`target-skeleton/dts/luofu-r116.dts`), not of any kernel patch: it is carried
for review as the separate DTS series entry **DTS-0001** (L3), which is a
release-critical dependency of kernel patch 0018.  L0/L1 are unchanged by that
entry (the target's own `patches-6.18/` stack remains the build source).

## 2. THE SMP fix (release-critical): loader hand-off devicetree contract

SMP was never a kernel-release bug.  The kernel's vendor-faithful release
(patch `018`: SYSCTRL pen write, CRG resume-bit read-modify-write, wakeup IPI) was
correct; the stock U-Boot `bootfip` hand-off staging was silently aborting because
the board devicetree lacked two nodes that the stock vendor base devicetree
carries.  When staging aborts, CPU1 never enters the U-Boot `start.S` pen-poll
loop, so every kernel release has no consumer.  That is what patches 033-042
measured and what the 060/061 sentinel probes proved.  [V]

The fix is **DTS-only**.  In `target-skeleton/dts/luofu-r116.dts`:

    aliases {
        serial0 = &uart1;
        serial1 = &uart0;
        console = &uart1;      /* vendor base DTB contract; bootfip staging */
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

Measured evidence [V]:

- without the nodes, our UART showed the vendor warning `alias console not found`
  and the stock staging lines (`alias console=...`, `serialnr->high/low`,
  `atag_addr=0x80601000`) never appeared; with them, they print exactly like stock;
- the fix makes CPU1 consume the real `secondary_startup` pen; the 064/065
  sentinel stubs (formerly 060/061) stayed untouched, which is the clean contrast to "diagnostic
  stub consumed";
- resulting boot: `smp: Brought up 1 node, 2 CPUs`, `CPU1: thread -1, cpu 1,
  socket 0, mpidr 80000001`, 3987.86 BogoMIPS, `/proc/cpuinfo` = 2, online 0-1,
  CPU0/CPU1 interrupt columns, SCU CONFIG bit 5 set (the CPU1-ran monitor);
- persistent across reboot (fresh boot 110 s, rootfsb, pair b recorded, 2 CPUs).

Where it is committed: `opensource` submodule `5a1b5b3` (DTS nodes) and `9e17d5c`
(SMP-RE.md sections 21-24 with the root-cause record).

Upstream action: the DTS contract must ship with kernel patch 0018.  The
dependency is an explicit, commit-ready upstream artefact: **DTS-0001**,
`dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
(sha256 `886f3ad095fc075cdf746cb0372eaf28c912fef91592f2885a739a7211bdf74c`,
current final 2026-11-21: conservative cleanup + five individually boot-tested
active deltas: TWD level-high, MAC-nvmem removal, switch MMIO-first order,
cudy-compatible ordering, flashinfo compatible-first).  It adds
`arch/arm/boot/dts/hisilicon/luofu-r116.dts` (681 lines, current final)
plus the `dtb-$(CONFIG_ARCH_LUOFU)` Makefile entry; post it together with
0018.  The DTS series must also state the base-DTB padding requirement: the
compiled DTB is 12,678 B with an unaligned totalsize and the image build MUST
run `Build/luofu-pad-dtb` before `append-dtb` (TRAILER-FORMAT.md
section 11; details in `06-dts-series-dependency.md` section 3.1).  The
cleanup record, the boot-test adoptions, the hardware regression of the
superseded active cleanup and the closed blocker list (zero remain) are in
`07-dts-upstream-bindings.md`; **host checks are necessary but not
sufficient - every active-DTS change must be boot-tested on the unit** (TWD:
image f6685b80; final deltas: image c3ef2a50; flashinfo: image
`6e9c54d0`).  The dependency record, ordering and regeneration procedure
is `06-dts-series-dependency.md`, and `tools/check-release-line.sh`
applies/compiles DTS-0001, checks the contract and exercises the pad step on a
copy on every validation run.  [V]

Patch `018` alone is necessary but **not sufficient** for SMP; the review-form
documentation now says so.  Do not present 064/065 (or 033-039/062/063) as the
SMP fix in a release or upstream submission - they are diagnostics (section 6).

## 3. Verified baselines

Three verified images are recorded (same kernel/target line; the second adds
the stock board-feature overlay content; the third is the known-good
pre-clean-DTS image used as the hardware baseline for the DTS work):

- **2-CPU/SMP baseline `974e6ca6`** - 6,226,222 B,
  sha256 `974e6ca63f6e86418761bc0f135e438f07d0e6ef909c61bb0339f00c5596c508`;
- **board-feature baseline `7f270b8c` (2026-11-21)** - same 2-CPU +
  network + SSH + overlay behaviour, plus the stock board_id=3 overlay features:
  six `oem:green:*` LEDs writable, `event0` = gpio-keys-polled
  (reset/WPS), `watchdog0` exposed via sysfs (CONFIG_WATCHDOG_SYSFS).
  **Thermal remains unsupported:** the temperature register reads 0 and the
  vendor DT disables the sensor (reported as-is; no thermal claim).  The
  feature image also needed the base-DTB padding fix (section 3.1).
  [V] (see also `target-skeleton/RELEASE-NOTES.md` and the parent ledger
  entry for 2026-11-21.)
- **current DTS-purpose baseline `6e9c54d0` (2026-11-21)** - built from
  the current adopted DTS with only the flashinfo compatible list changed
  (documented `hisilicon,luofu-flashinfo` first, vendor string fallback),
  flashed over the broker channel to pair b (env bootflag=b, selector 0x11
  verified) and booted: 6.18.60, 2 CPUs, `GIC-0 29 Level twd`,
  bootfip staging lines with the new first compatible (`atag_addr=0x80601000`),
  `/proc/device-tree/reserved-memory/flashinfo@80600000/compatible` =
  `hisilicon,luofu-flashinfo hsan,flashinfo_reserved`, cmdline
  `ubi.mtd=rootfsb` + full `luofu_switch` marker, no Oops/BUG/panic/RCU
  stall.  DTB 12,678 B sha256 `62574b11...` (pad 12,680 B sha256
  `8d79794e...`).  This supersedes c3ef2a50 **for DTS purposes**;
  c3ef2a50 was the previous DTS-purpose baseline, f6685b80 the one before, and
  7421bb7a the earlier verified baseline.  Adopted in commit `71b2167`;
  DTS-0001 sha256 `886f3ad0...`; zero blockers remain.  [V]
- **previous DTS-purpose baseline `c3ef2a50` (2026-11-21)** - MAC-nvmem
  removal, switch MMIO-first order and cudy-compatible ordering; 2 CPUs,
  `GIC-0 29 Level twd`, board_name=cudy,wr3000-v2, MACs ...:73/...:74,
  ping 15/15; adopted in commit `6c2cc2f`.  Superseded by 6e9c54d0.
- **earlier DTS-purpose baseline `f6685b80`** - TWD level-high delta
  (commit `5b401c1`); TWD counts ~100 Hz, ping 20/20.  Superseded by
  c3ef2a50.  [V]
- **previous known-good pre-clean-DTS baseline `7421bb7a`** - same kernel
  config and userspace, DTB 13,041 B padded to 13,048 B, boot observed at
  partition scan 0.93-1.16 s, luofu-reboot 2.28 s, luofu-wdt 2.30 s, ubi
  attach 2.68 s, init 3.01 s.  The conservative comment-only DTS (commit
  `7ae18f2`) compiled to this exact DTB (sha256 `49fe5c47...`), proving
  the prose-only step; the superseded active cleanup (image `6f19c6d6`) did
  NOT boot and is recorded in `07-dts-upstream-bindings.md` section 1.  [V]

The `974e6ca6` details:
- built from: L0 patch stack (66 files: 001-060 + 062-067), the DTS hand-off
  fix, canonical command line, DEVMEM + static devmem tool + boot-pair recorder;
  **no `luofu_smp_*` probe tokens**.
- verified on hardware (2026-11-21) [V]:

| check | result | evidence |
| --- | --- | --- |
| U-Boot hand-off staging | `alias console=`/`serialnr->`/`atag_addr=0x80601000` print as stock | smp-2cpu/smp-reboot captures |
| SMP | 2 CPUs, mpidr 80000001, 3987.86 BogoMIPS, SCU bit 5 set | 2026-11-20-smp-2cpu.log, 2026-11-21-final-reboot-2cpu.log |
| CPU online | `/proc/cpuinfo` = 2, online 0-1, both CPUs in /proc/stat and /proc/interrupts | 2026-11-21 logs |
| network (bench workaround, section 5) | br-lan 192.168.1.1/24, ping 20/20 0% loss; repeat runs 0% loss | w20/ping captures, STATUS/ledger |
| SSH | login over br-lan works, and again after reboot | 2026-11-20-w20-ssh / 2026-11-21 logs |
| overlay | `/dev/ubi0_1 on /overlay type ubifs (rw)`; files survive reboot | mounts + reboot capture |
| stability | 15-minute continuous-ping soak, 0 failures; no Oops/panic/BUG/WARNING (only literal `panic=0` cmdline text) | 2026-11-21-soak-2cpu.log, UART capture |
| reboot persistence | fresh boot 2 CPUs, rootfsb, pair b recorded | 2026-11-21-final-reboot-2cpu.log |
| LAN data path | wire->PIE RX->CPU->kernel->PIE TX->wire loop, host ping 0% loss | round-4s session, SKELETON-SMOKE.md |

This is the **release baseline**: a demonstrably working 6.18 kernel + target
image on the unit.  It is not a distributable image: see section 10.

## 4. Canonical configuration (no probe tokens)

**Built-in kernel command line** (the stock loader supplies none; forced by
`CONFIG_CMDLINE_FORCE` in `target/linux/hisilicon/luofu/config-6.18`):

    ubi.mtd=rootfsb ubi.block=0,0 root=/dev/ubiblock0_0 rootfstype=squashfs
    rootwait console=ttyS0,115200 earlycon noinitrd cma=0 maxcpus=2 nr_cpus=2
    additional_cpus=1

**Canonical attended module gates** (the working LAN workaround; hardware
recovery armed for every fire, defaults are all off):

    luofu_switch.arm=1 luofu_switch.egress=1 luofu_switch.qidmap=1
    luofu_switch.eps=1 luofu_switch.qminit=1 luofu_switch.pfe=1
    luofu_switch.ifc=1 luofu_switch.ext=1
    luofu_pie.arm=1 luofu_pie.rxcfg=1 luofu_pie.accepterr=1
    luofu_pie.cpucopy_deliver=1 luofu_pie.cpucopy_port=11
    luofu_gmac.macregs=1
    luofu_switch.cpu_mirror=1 luofu_switch.mirror_dst=4

Rules:

- **No `luofu_smp_*` tokens** are part of the canonical or release command line.
  Those tokens belong to the SMP investigation only (SMP-RE.md sections 14-23).
- The diagnostic-readout gates (e.g. `luofu_pie.rxdiag=1`, `luofu_pie.rxstroke=1`,
  `luofu_pie.rxtrace=1`, `luofu_switch.dump/scan/pagedump=1`) are **not** part
  of the canonical line; they are optional bench diagnostics.
- DEVMEM is compiled in and the static devmem tool is present on the canonical
  image (optional for debugging, not required for the release functionality).
- The maximum-CPU-count and lockdep images are diagnostics only:
  `d101f96b...` (maxcpus=1) and `18b3cb1d...` (lockdep) were built for the
  retracted hang investigation; they are not release baselines.
- The canonical gating is documented by patch `058` and by
  `target-skeleton/README.md` "Canonical attended bench command line" (updated
  2026-11-21 to the 974e6ca6 baseline).

## 5. Working LAN = documented bench workaround (not native forwarding)

The verified LAN path uses the internal switch's CPU-copy mirror:

- static ingress mirror from eth2/port 11 to CPU port destination 4
  (`luofu_switch.cpu_mirror=1 mirror_dst=4`; re-applied by patch 056 after every
  MAC link-up and re-latched every 2 s because the OPP mirror registers do not
  survive switch port-up);
- port-0 normal-path completions delivered to the eth2 netdev
  (`luofu_pie.cpucopy_deliver=1 cpucopy_port=11`, patch 057);
- exception completions accepted onto the decoded error-path netdev
  (`luofu_pie.accepterr=1`, patch 053), which is how the host frames are
  delivered on this bench.

This is a **bench workaround**: it gets a working wire->CPU->wire loop and is the
documented configuration of the release baseline, but it is not the clean
forwarding path.  Native vendor-CFE-equivalent L2/VLAN/CPU-port forwarding is a
**separate project** (the CFE table-manager modules cannot load on 6.18; the
missing runtime state is the open item in LSW-SPEC.md/PIE-DATAPATH.md).  Do not
describe the release baseline as "native switch forwarding works".

## 6. Bench-only diagnostics excluded from the release series

Eleven patches in L0 are SMP investigation instrumentation.  They are **excluded
from L1** (the release line) and must not be carried by a release/publish
snapshot as features: [V]/[I]

| patch | sha256 (short) | reason |
| --- | --- | --- |
| `028-arm-hisilicon-smp-diagnostics.patch` | `64cfc487` | release-path pr_info trace only |
| `029-arm-hisilicon-smp-release-bits.patch` | `d5031f71` | bench hypothesis (clear all four resume bits); not needed after the DTS fix |
| `033-arm-hisilicon-smp-release-diagnostics.patch` | `cf0df0ee` | release-path dumps and status prose |
| `034-arm-hisilicon-smp-release-probe.patch` | `cc27347f` | pen stub/marker probe experiment |
| `035-arm-hisilicon-smp-release-marker.patch` | `8724e926` | marker protocol iteration |
| `037-arm-hisilicon-smp-early-probe.patch` | `2b073ec0` | early `setup_arch()` release experiment |
| `039-arm-hisilicon-smp-early-safe.patch` | `37b7932b` | safe early-probe variant |
| `062-arm-hisilicon-smp-scu-gic.patch` | `dc8511ed` | SCU/GIC reference dumps (renumbered from the duplicate 042) |
| `063-arm-luofu-cpu1-up-trigger.patch` | `e8528316` | late `add_cpu(1)` sysfs retrigger (renumbered from 059 to keep dependency order) |
| `064-arm-hisilicon-smp-handoff-probe.patch` | `e1d9dc5f` | decompressor/stext hand-off markers (renumbered from 060) |
| `065-arm-hisilicon-smp-trampoline.patch` | `5e254fe4` | sentinel protocol + re-poll hand-off trampoline (renumbered from 061) |

Why exactly these: after the DTS fix nothing in the SMP path needs them; no later
release patch depends on them (verified: the 55-patch L1 applies in order,
section 9); they were designed to answer "does CPU1 ever poll the pen", and the
064/065 sentinels did exactly that.  The numbering was cleaned on 2026-11-21:
the duplicate 042 is gone (042-net stays; the former 042-arm is now 062) and its
dependent diagnostics moved to 063/064/065 so that filename order stays
dependency order.  Full hashes and one-line purposes are in
`release-line/manifest.tsv`.

The L0 lab stack still contains them so that the exact canonical image 974e6ca6
remains reproducible; L1 is the publication/release classification.

## 7. Release patch classification (A/B/C/D)

`release-line/manifest.tsv` has one row per L0 patch:

- **A - core/functional (27):** patches 001-027: platform, CRG/watchdog/FMC/MDIO/
  PHY/pinctrl/thermal/PCIe/restart/selector drivers, SMP release driver 018,
  read-only clock tree 019, PIE/LSW/GEMAC drafts 020-026, CRG mux fix 027.  The
  A group up to 026 is what the upstream review form covers; the review form is
  not extended by this release-line change.
- **B - functional gated bench path (22):** 031, 032, 036, 038, 040, 041,
  042-net, 043, 044, 046, 048, 051, 053, 054, 055, 056, 057, 058 - the staged
  switch/PIE/GEMAC power/write/readiness gates and the canonical-config docs
  patch, the W22 native-forwarding patches and the W23 gated thermal driver/binding.
  Default-off (W22 NAPT init is the exception: default on), fail-closed where gated.
- **C - included default-off diagnostics (6):** 030, 045, 047, 049, 050, 052 -
  trace/dump gates compiled into the release image for bench parity but never
  enabled by the canonical command line.
- **D - excluded bench diagnostics (11):** section 6.

Counts: 27 + 22 + 6 = 55 (the 2026-11-29 v3 L1 adds `013a`, so 56 included, plus 11 excluded = 67 L0 files).

Ordering notes (both items fixed 2026-11-21):

- **No duplicate numbers.**  042-net stays; the former 042-arm is 062 and its
  dependent SMP diagnostics are 063/064/065.  Filename order is now dependency
  order for the whole L0 stack (verified 66/66 patch(1) RC=0, 0 fuzz, and strict
  `git apply` 66/66).
- **Patch 053 was regenerated against its post-052 context.**  The old patch was
  authored before 052 merged into the same `MODULE_PARM_DESC(nodoorbell...)`
  anchor, which caused the one fuzz hunk and the strict `git apply` divergence.
  The regenerated patch keeps exactly the same functional content/gates (the
  resulting `luofu-pie.c` is byte-identical to the old stack's final file) and
  now applies with **0 fuzz**; strict `git apply` passes for all 55 L1
  patches.
- **Release-line patch content refreshed (2026-11-21, board-feature pass).**
  Periodic W17/W19 relatch logs were quieted from `dev_warn~/`dev_info`
  to `dev_dbg`: patch **054** (`dev_warn` -> `dev_dbg`) and patch
  **056** (`dev_info` -> `dev_dbg`).  Manifest hashes were refreshed
  (054 `9e3c01c3...`, 056 `eae38ae3...`; manifest sha256
  `619eb15a8561deb6cd7ccc67709930cffa454b82c49d5305491e1d4ca5c128c7`).
  The base-files heartbeat interval also changed 5 s -> 60 s (target content,
  not a kernel patch).

- **Native forwarding W22 (release-line, 2026-11-21).**  Patches **059** (luofu_switch.naptinit, PFE NAPT/flow-engine init) and **060** (PIE native exception-class delivery, retires the accepterr gate) were added to L0/L1: L0 64 files (001-060 + 062-065), L1 53 files, manifest sha256 619eb15a8561deb6cd7ccc67709930cffa454b82c49d5305491e1d4ca5c128c7. Bench-proven: the NAPT init restores native CPU delivery (M1a) and the canonical command line drops the CPU-copy mirror tokens (mirror patches 054-057 stay in the line as documented fallback).  Validator PASS (53/53), target build PASS, cold boot + reboot determinism + 200/200 ping with no mirror tokens.

- **Thermal W23 (release-line, 2026-11-22).**  Patches **066** (luofu_tvsensor driver: correct hwver-3/v1 read path, variant formulas, gated enable/trim sequence) and **067** (binding update: 0x408 window, vendor properties) added to L0/L1: L0 66 files (001-060 + 062-067), L1 55 files, manifest sha256 2bb7413e8291214fe09ccd84920166fc668ae72007dcbcc91727bb29e30eea70. Bench-proven: gate off -> raw 0x0 and thermal zone ENODATA; enable -> raw 0x25e -> 59000 mC (formula exact), registers match the decoded sequence, no IRQ registered, 0 warnings; CPU load 59 -> 63 C, cooling observed.  The release DTS gains the thermal node properties (release-line-only delta; the L2 upstream review form and DTS-0001 are intentionally not regenerated). Validator PASS (55/55), target build PASS, cold boot PASS.

- **PCIe hardware GPIO fix (release-line, 2026-11-22).** The bench bring-up
  proved the PCIe link failure was board GPIO state, not the RC register program:
  GPIO0 pins 20/21/28 must be outputs driven HIGH before training (stock vendor
  GPIO0 DDR=0x1330000f / DATA=0x12300004).  Patch **013** is updated in L0/L1
  (hardware-verified GPIO note + compact 7-word/absolute endpoint-iATU note; the
  hardcoded workaround remains until the DTS hog alone is boot-tested) and the
  release DTS carries the equivalent `wifi-pcie-hog` plus the enabled PCIe nodes
  (commits 32d44e6 / 5a6a500 / 4e959f2).  Host re-validation: release-line validator
  PASS (55/55 include patches, 0 fuzz, 0 rejects, strict git apply 55/55); target
  DTS DTB **12,865 B** sha256 `d8b41ac3...`, pad **12,872 B**; DTS-0001
  regenerated with the new release-line W23 filter (sha256 `33956380...`,
  712 lines, 699-line filtered payload) and byte-identical to the filtered target
  DTS (DTB 12,697 B sha256 `288a5c3f...`, pad 12,704 B).  Release-line manifest
  refreshed (row 013 `2a43c953...`; manifest sha256 `925442e0...`).  L2 review
  form re-ran 26/26 patch(1)+git am, identical trees, 0 errors / **74**
  checkpatch warnings (the +16 are patch 0013's own comment-style warnings from the
  hardware-proven GPIO block, plus the known binding-split warning).  Detail:
  `04-validation.md` section 31.  Hardware caveat: the 2026-11-22 DTS additions
  still need one regression boot as a unit, and the DTS-hog-only path has not been
  boot-tested without the hardcoded driver block.

## 8. Retraction: the "2-CPU hang" was a host bench-link outage

On 2026-11-20 the box ran ~23 minutes with 2 CPUs, 0% loss and SSH, then went
silent; the UART capture had ended and both management IPs were unreachable, and
the working hypothesis was a hard SMP deadlock.  That hypothesis is **RETRACTED**
[V]:

- overnight the host USB-Ethernet adapter (`enp195s0f3u1`) lost carrier/IPs
  (NetworkManager changed state); that is why both IPs failed;
- after the power-cycle the box booted normally with the heartbeat running and
  the UART showed no Oops/panic/BUG/WARNING (the only grep hit is the literal
  `panic=0` cmdline text);
- after `tools/bench-link.sh --apply` restored the host link, the same canonical
  image ran the full verification matrix and a 15-minute soak with 0 failures;
- the maximum-CPU/lockdep images (d101f96b/18b3cb1d) built for the false hang
  remain available only as diagnostics.

There is no SMP stability bug.  Bench lesson: split "box unreachable" into
box-side and host-side checks before attributing a hang to the kernel; the UART
and the userspace heartbeat are the authority.

## 9. Host-only validation (2026-11-21, no hardware, no kernel build)

Run by `tools/check-release-line.sh` against pristine linux-6.18.60
(sha256 `3c2f44ef...`); logs in `validation/release-line/`:

- L1 release line: **51/51 patches apply with patch(1), RC=0**, 146 file events,
  **3 offset hunks, 0 fuzz, 0 rejects**; strict `git apply`: **all 51 patches
  apply without fuzz** [V];
- L0 lab stack (62 files, 001-058 + 062-065): 62/62 patch(1) RC=0, 0 fuzz;
  strict `git apply` 62/62 [V];
- release-critical DTS contract (current target DTS and DTS-0001): both
  `cpp` + `dtc` RC=0, **0 stderr bytes**, DTB **12,678 B**, sha256
  `62574b118afd0aaf445b03d7ed49c9e021be78d39049dcb0626b9130f94ca899`;
  `fdtget` confirms `/aliases/console`,
  `compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved"`,
  `reg = <0x80600000 0x4000>`, `atag-offset = <0x1000>` and
  `/cpus/enable-method = "hisilicon,luofu-smp"`;
- **totalsize alignment:** the 12,678 B DTB totalsize is **not** 8-byte
  aligned; the validator runs `target-skeleton/tools/pad-dtb.py` on a copy,
  which pads it to **12,680 B** (sha256 `8d79794e...`) and keeps the
  hand-off contract.  The image build MUST run `Build/luofu-pad-dtb`
  (TRAILER-FORMAT.md section 11; an unaligned load made stock U-Boot
  data-abort and fall back to stock on 2026-11-21) [V];
- **DTS-0001**: regenerated from the current source (sha256
  `886f3ad095fc075cdf746cb0372eaf28c912fef91592f2885a739a7211bdf74c`, 694
  lines); applies to the L1 tree RC=0; its added board DTS is byte-identical to
  the single-source target DTS; the compiled DTB and the contract checks are
  identical to the target-DTS ones [V];
- upstream review form: re-ran after adopting the MMIO-first switch binding
  YAML into patch 021: 26/26 patch(1)+`git am`, identical trees, 3 offset
  hunks, 0 fuzz, 0 errors / 58 checkpatch warnings; source patch sha256
  `86f6f020...`, series file sha256 `d249a83a...` (driver unchanged,
  by-name lookups).  Not re-run for the DTS-only flashinfo compatible update
  (no series file changed) [V];
- current final DTS: conservative cleanup (comments only; commit `7ae18f2`)
  + boot-tested TWD level-high (commit `5b401c1`, image f6685b80) +
  boot-tested MAC-nvmem removal, switch MMIO-first order and cudy-compatible
  ordering (commit `6c2cc2f`, image c3ef2a50) + boot-tested flashinfo
  compatible-first (commit `71b2167`, image `6e9c54d0`).  The
  conservative step's DTB was byte-identical to the pre-clean DTB
  (`49fe5c47...`); the current 12,678 B DTB was boot-tested as image
  6e9c54d0.  No dtschema/dt-validate tooling was available, so the manual
  binding review in `07-dts-upstream-bindings.md` section 4 stands in for
  `dtbs_check` [V]/[I].  **Any future active-DTS change must be
  boot-tested**; host checks alone are not sufficient (the superseded
  active-DTS cleanup regression in section 3 / 07 section 1 proves it).
- no kernel build in this pass: the last full-series build records (compile
  RC=0, CONFIG_WERROR, zImage/hash, TEXT_OFFSET unchanged) are in
  `04-validation.md` and SKELETON-SMOKE.md; the release image itself was built
  and booted by the bench sessions (section 3).

### 2026-11-22 re-run (patch 013 hardware update + DTS-0001 regeneration)

The 2026-11-21 numbers above are kept as history.  After the hardware PCIe GPIO fix
(patch 013) and the board-DTS 2026-11-22 additions, `tools/check-release-line.sh`
was re-run against pristine linux-6.18.60 (sha256 `3c2f44ef...`):

- L1 release line: **55/55** include patches apply (152 file events, 3 offset hunks,
  0 fuzz, 0 rejects); strict `git apply` 55/55 [V];
- target board DTS: cpp+dtc RC=0, 0 stderr, DTB **12,865 B** sha256
  `d8b41ac37ec68a10ccaf2e68db0675dd6f28a83c1bb603603bbd97cc68af3ce3`, unaligned ->
  `pad-dtb.py` pads to **12,872 B**; the loader hand-off contract is unchanged
  (`/aliases/console`, flashinfo compatible pair/reg/atag-offset, enable-method) [V];
- **DTS-0001** regenerated from the release-line-filtered single source:
  sha256 `33956380db9ca5c7b91404596d6078d2f35e4a349978e41e4a1c5d5b9b5155fa`,
  712 lines (699-line filtered payload).  Applies RC=0 and its board DTS is
  byte-identical to the filtered target DTS (the W23 thermal delta is documented in
  `06-dts-series-dependency.md` / produced by `tools/dts-upstream-filter.py`);
  compiled DTB **12,697 B** sha256 `288a5c3f...`, pad **12,704 B**, contract intact [V];
- release-line `manifest.tsv` row 013 refreshed to `2a43c953...`; manifest sha256
  `925442e00b0b212d9080b57a5ef7f01bfc51a14722a661cd4403c5927c71000c`;
- logs: `validation/release-line/` refreshed; `validation/sha256sums.txt`
  regenerated (93 entries, `sha256sum -c` 93/93 OK).  The L2 review-form re-run and
  the PCIe `dt_binding_check` (RC=0) are recorded in `04-validation.md` section 31.

**Final (2026-11-22, after the W27b hog-only boot):** patch 013's hardcoded GPIO
workaround was removed (commit `ad711d1`): W27b (image sha256 `4af1d1fa...`), built from
the board DTS `wifi-pcie-hog` with no board-GPIO driver code, trains both RC links and
enumerates both endpoints, so the devicetree hog alone is the board fix.  Re-validation:
release line 55/55 strict-clean; target DTB 12,865 B `d8b41ac3...`; DTS-0001 unchanged
(`33956380...`, 12,697 B `288a5c3f...`); review form 26/26, **0 errors / 73 warnings**
(patch 0013: 0/17); PCIe `dt_binding_check` RC=0; release-line manifest sha256
`98931166...`; `validation/sha256sums.txt` 93/93 OK.  Detail: `04-validation.md`
section 32.

## 10. What is NOT in the release line / not claimed

- **Not a flashable or distributable image.** The image was installed manually
  from the stock system; `platform_do_upgrade` and the first-install path have
  never been executed on a device; the container is not marked installable.
- **No Wi-Fi.** Vendor-only Hi5622V100 modules; no open driver or firmware
  licence position [X].
- **No native CFE forwarding.** The LAN path is the CPU-copy mirror workaround
  (section 5); the native L2/VLAN path is a separate project [U]/[I].
- **No upstream review.** The A-group drivers are drafts; L2 remains the RFC /
  do-not-merge review form.  Being bench-verified in the target image does not
  make the drivers upstream-ready.
- **No hardware validation of everything in L1.** The canonical image exercises
  the platform, storage, SMP, PIE/LSW/GEMAC gated path, overlay and network; many
  other driver behaviours (pinctrl consumer states, PCIe enumeration, FMC
  ECC/OOB details, restart sequence, selector env re-stamp, A/B install) remain
  [U] as recorded in TARGET-DESIGN.md, SMP-RE.md and the lane summaries.
- **Thermal (W23, bench-proven):** the temperature register read 0 with the old driver because it used the wrong variant/register/formula and never enabled the sensor; W23 implements the vendor hwver-3/v1 path and a gated enable sequence. Bench: gate off -> ENODATA; enable -> 59 C idle, 63 C under 2-CPU load, registers match, no IRQ, no warnings. The canonical image ships the driver with the gate default off; the value is a bench-validated measurement, not a thermal policy.
- **No performance/soak beyond the recorded 15 minutes**; no long-term thermal or
  NAND-degradation campaign.

## 11. Next actions after this consolidation

1. **Post DTS-0001 with kernel patch 0018** (same window, DTS entry first or
   together), referencing SMP-RE.md section 24 and the base-DTB 8-byte
   alignment/pad-dtb requirement (TRAILER-FORMAT.md section 11) in its cover
   letter.  Regenerate DTS-0001 only from the target DTS via
   `tools/gen-dts-series-patch.py` (2026-11-21: conservative comment-only
   cleanup; active content is the hardware-verified one).  Any future
   active-DTS change is a separate patch that must be boot-tested on the unit
   before it is relied on.
2. Sync L1 + L3 + the updated docs to the curated publication repo staging
   branch (separate repositories; not touched from the notebook).
3. Native CFE forwarding, Wi-Fi and the install path remain the separate follow-
   ups; do not fold them into this release line.
4. Parent repo: record the release-line cleanup (052/053 regeneration,
   062-065 renumbering, DTS-0001) in EVIDENCE-INDEX.md and the ledger (newest
   entry wins; STATUS.md is the live page).

## 12. Evidence map

- `release-line/manifest.tsv` - one row per L0 patch: status, class, sha256,
  purpose (51 include / 11 exclude, no duplicate numbers).
- `06-dts-series-dependency.md` + `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
  - the upstream DTS-series entry (DTS-0001) and its regeneration/ordering rules.
- `07-dts-upstream-bindings.md` - the board-DTS bindings cleanup: every
  removal/rename, the retained measured facts, the manual binding review, the
  compile/pad facts and the remaining upstream blockers.
- `validation/release-line/` - 2026-11-21 harness logs: L1/L0 patch
  application, offsets, strict git-apply, target DTS contract, DTS-0001
  apply/compile/contract, summary.
- Board-feature refresh: submodule `aff7e27` (gpio-keys-polled + six
  gpio-leds DTS nodes from the stock board_id=3 overlay, pad-dtb, watchdog
  sysfs config), verified image `7f270b8c`; measurement in
  `../../docs/TRAILER-FORMAT.md` section 11 and
  `target-skeleton/tools/pad-dtb.py`.
- DTS hardware regression and conservative recovery:
  `build/evidence-bench-gates/2026-11-21-cleaned-dts-regression.log`
  (sha256 `15dc3188...`) for the failed image `6f19c6d6`, and the
  known-good pre-clean-DTS baseline image `7421bb7a`; full record in
  `07-dts-upstream-bindings.md` sections 1-2.
- TWD level-high adoption and boot-test: image **f6685b80** (previous
  DTS-purpose baseline), 2 CPUs, `/proc/interrupts` `GIC-0 29 Level twd`,
  TWD counts ~100 Hz both CPUs, ping 20/20 0% loss, no Oops/BUG/panic/RCU
  stall; measured GICD_ICFGR1 data and draft/evidence in
  `../../docs/incoming/upstream-blockers/06-twd-irq-flags/`.
- Final board deltas boot-test: image **c3ef2a50** (current DTS-purpose
  baseline, sha256 `c3ef2a50...`): 2 CPUs, `GIC-0 29 Level twd`,
  `board_name=cudy,wr3000-v2`, compatible order, userspace MACs
  ...:73/...:74, br-lan up, ping 15/15, no errors; adopted in commit
  `6c2cc2f` (MAC-nvmem removal, switch MMIO-first, cudy compatible +
  SUPPORTED_DEVICES).
- Canonical image evidence (parent repo, gitignored/local):
  `build/evidence-bench-gates/2026-11-20-{smp-2cpu.log,smp-reboot.log}`,
  `.../2026-11-21-{soak-2cpu.log,final-reboot-2cpu.log}`, UART captures, images
  in `<local-staging-dir>/` (img-canonical.bin 974e6ca6, feature image
  7f270b8c).
- Root-cause record: `docs/SMP-RE.md` sections 21-24;
  `target-skeleton/docs/port-notes/smp-decode/SUMMARY.md`.
- Parent live state: `STATUS.md` (2026-11-21 FINAL SMP entry) and
  `EVIDENCE-INDEX.md` (SMP rows).

## 13. v3 update (2026-11-29): L1 grows to 56 with the GP1 endpoint-reset patch

**This section supersedes the 55-patch numbers above; the older dated records stay as
history.** The 2026-11-24 GP1 work made the committed
`013a-arm-pci-luofu-endpoint-reset-pulse.patch` load-bearing: without the RC0
GPIO0 20/21/28 assert/release pulse the endpoint mask ROM never runs S0/S1 and
the link never trains (parent ledger 2026-11-24 GP1; `STATUS.md` section 3c).
The release line therefore had to grow 55 -> 56; it was missing the very patch
the hardware-validated line boots with.

**What was changed (host-only, no device, no kernel build):**

- `release-line/manifest.tsv`: added the `include` row for `013a` (class A,
  sha256 `d4b544a398099e23793e39e4cebc9b81533b749bea1498cae9ef3d5f4636386b`,
  "PCIe endpoint reset pulse at RC0 probe (GP1, load-bearing)") after the 013
  row; 56 included / 11 excluded; new manifest sha256
  `17d714be7733af092eb4859b1e0830a3283e0c5c5ca9ef7b8732e01de14e4cf4`.
- `release-line/INCLUDED.txt`: added `013a-...` after `013-...` (56 lines);
  new sha256 `da4966aac90448cfe4e903f52792da18b6df4f0f2b01f73d53ba8a440c1511e4`.
- `dts-series/0001-...patch`: regenerated from the current single-source DTS
  (see `06-dts-series-dependency.md` section 4) so the DTS-series entry no
  longer carries the retired `wifi-pcie-hog`.

**Validation result (`tools/check-release-line.sh --use-tree`, pristine
linux-6.18.60, host-only): PASS.**

    56 included / 11 excluded
    patch(1): 56/56 patches RC=0, 153 file events, 5 hunks with offset,
              0 hunks with fuzz, 0 rejects
    strict git apply: all 56 patches apply without fuzz
    board DTS: cpp RC=0, dtc RC=0, stderr 0/0, DTB 12722 B
               sha256 40b71de5...; unaligned -> pad 12728 B
    DTS contract: console=/serial@1010f000
               flashinfo-compatible=hisilicon,luofu-flashinfo hsan,flashinfo_reserved
               reg=80600000 4000 atag-offset=1000
               enable-method=hisilicon,luofu-smp
    DTS series: patch apply RC=0; board DTS byte-identical to the filtered
               target DTS; DTB 12554 B sha256 4f2e0e8d...; pad 12560 B;
               same hand-off contract; no W23 leak; no wifi-pcie-hog
    PASS

Logs: `validation/release-line-v3/` (19 files, 132 KB: summary, patch/git-apply
logs, offsets, DTS cpp/dtc stderr, the compiled DTBs and the filter log).  The
dtc used is the host system `dtc` v1.8.1, which produces smaller blobs than the
kernel `scripts/dtc` build used for the 2026-11-22 figures above; compare DTB
sizes/hashes only within one validator run.

**Remaining (not done here):** the L2 review form is still the frozen 26-patch
v1 and the planned 30-patch v2 is still not generated: the only remaining
content decision is the `013a` -> DT `reset-gpios` fold in patch 0013 (+ PCIe
binding + DTS + one attended boot).  Nothing was sent anywhere.

