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
| **L0 lab patch stack** | every file in `../../target-skeleton/target/linux/hisilicon/patches-6.18/`, applied in filename order by the OpenWrt target build | 62 patch files (001-061 plus the duplicate `042` pair) |
| **L1 release line** | L0 minus the bench-only SMP diagnostic patches (section 6); this is the series a release/publish snapshot carries | **51 patch files** (`release-line/INCLUDED.txt`, manifest `release-line/manifest.tsv`) |
| **L2 upstream review form** | the kernel-only submission form in `series/` plus `00-06` docs | 26 patches (001-026), grouped merge-intent / RFC / do-not-merge |

L1 is new here.  L0 is what the canonical image was built from; L2 is unchanged
except for the documentation corrections in this update.  The loader hand-off DTS
fix (section 2) is part of the board devicetree supplied by the **target tree**
(`target-skeleton/dts/luofu-r116.dts`), not of any kernel patch, so it is
release-critical but not a member of L0/L1/L2 patches.

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
- the fix makes CPU1 consume the real `secondary_startup` pen; the 060/061
  sentinel stubs stayed untouched, which is the clean contrast to "diagnostic
  stub consumed";
- resulting boot: `smp: Brought up 1 node, 2 CPUs`, `CPU1: thread -1, cpu 1,
  socket 0, mpidr 80000001`, 3987.86 BogoMIPS, `/proc/cpuinfo` = 2, online 0-1,
  CPU0/CPU1 interrupt columns, SCU CONFIG bit 5 set (the CPU1-ran monitor);
- persistent across reboot (fresh boot 110 s, rootfsb, pair b recorded, 2 CPUs).

Where it is committed: `opensource` submodule `5a1b5b3` (DTS nodes) and `9e17d5c`
(SMP-RE.md sections 21-24 with the root-cause record).

Upstream action: a future kernel DTS series must carry these two nodes verbatim
(or their accepted equivalents).  Patch `018` alone is necessary but **not
sufficient** for SMP; the review-form documentation now says so.  Do not present
060/061 (or 033-042) as the SMP fix in a release or upstream submission - they are
diagnostics (section 6).

## 3. Verified 2-CPU baseline image 974e6ca6

- image: `<local-staging-dir>/img-canonical.bin`, **6,226,222 B**,
  sha256 `974e6ca63f6e86418761bc0f135e438f07d0e6ef909c61bb0339f00c5596c508`.
- built from: L0 patch stack (001-061, 62 files incl. duplicate 042), the DTS
  hand-off fix, canonical command line, DEVMEM + static devmem tool + boot-pair
  recorder; **no `luofu_smp_*` probe tokens**.
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
| `042-arm-hisilicon-smp-scu-gic.patch` | `dc8511ed` | SCU/GIC reference dumps |
| `059-arm-luofu-cpu1-up-trigger.patch` | `e8528316` | late `add_cpu(1)` sysfs retrigger |
| `060-arm-hisilicon-smp-handoff-probe.patch` | `e1d9dc5f` | decompressor/stext hand-off markers |
| `061-arm-hisilicon-smp-trampoline.patch` | `5e254fe4` | sentinel protocol + re-poll hand-off trampoline |

Why exactly these: after the DTS fix nothing in the SMP path needs them; no later
release patch depends on them (verified: the 51-patch L1 applies in order,
section 9); they were designed to answer "does CPU1 ever poll the pen", and the
060/061 sentinels did exactly that.  Full hashes and one-line purposes are in
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
- **B - functional gated bench path (18):** 031, 032, 036, 038, 040, 041,
  042-net, 043, 044, 046, 048, 051, 053, 054, 055, 056, 057, 058 - the staged
  switch/PIE/GEMAC power/write/readiness gates and the canonical-config docs
  patch.  Default-off, fail-closed; required for the working LAN workaround.
- **C - included default-off diagnostics (6):** 030, 045, 047, 049, 050, 052 -
  trace/dump gates compiled into the release image for bench parity but never
  enabled by the canonical command line.
- **D - excluded bench diagnostics (11):** section 6.

Counts: 27 + 18 + 6 = 51, plus 11 excluded = 62 L0 files.

Ordering notes:

- L0 contains **two patches numbered 042** (`042-arm-hisilicon-smp-scu-gic` and
  `042-net-gmac-w9-master-off`).  Filename order applies the ARM one first; they
  touch different files.  This duplicate number is a lab artefact and should not
  survive a clean re-publication (renumber).
- The target stack was authored against the OpenWrt-prepared 6.18 tree.  On
  pristine kernel.org 6.18.60 it applies with 6 offset hunks and **1 fuzz hunk**
  (patch 053 hunk 1), because 052 and 053 both insert at the same
  `MODULE_PARM_DESC(nodoorbell...)` anchor and 053 was generated before 052 was
  merged.  patch(1) RC=0, no rejects; strict `git apply` diverges at 053.
  Regenerate/merge 052+053 before publishing a clean release series (section 11).

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
  6 hunks with offset, **1 hunk with fuzz** (patch 053, see section 7),
  **0 rejects** [V];
- strict `git apply` (no fuzz): diverges at 053 for the same ordering reason;
  recorded as a release-line cleanup item, not a build failure [V];
- release-critical DTS contract: `cpp` + `dtc` RC=0, **0 stderr bytes**, DTB
  **12,296 B**, sha256 `50daf3d0d50929aaa3af35fa40fae69a73428fd5b1c5bcc686795e89cb92d8eb`;
  `fdtget` confirms `/aliases/console`, `compatible = "hsan,flashinfo_reserved"`,
  `reg = <0x80600000 0x4000>`, `atag-offset = <0x1000>` and
  `/cpus/enable-method = "hisilicon,luofu-smp"` [V];
- upstream review form unchanged in content: 26/26 apply with patch(1) and
  `git am` (section 04-validation.md records the latest run); the only update is
  the 0018 commit-message text recording that the DTS contract is required [V];
- no kernel build in this pass: the last full-series build records (compile
  RC=0, CONFIG_WERROR, zImage/hash, TEXT_OFFSET unchanged) are in
  `04-validation.md` and SKELETON-SMOKE.md; the release image itself was built
  and booted by the bench sessions (section 3).

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
- **No performance/soak beyond the recorded 15 minutes**; no long-term thermal or
  NAND-degradation campaign.

## 11. Next actions after this consolidation

1. Sync L1 + the updated docs to the curated publication repo staging branch
   (separate repositories; not touched from the notebook).
2. Regenerate patches 052/053 as one clean pair (remove the fuzz); renumber the
   duplicate 042 pair; then re-run `tools/check-release-line.sh` for a strict
   `git apply`-clean release line.
3. Add the two loader hand-off nodes to the future kernel DTS series (they are
   already in the target DTS), and reference SMP-RE.md section 24 in its cover
   letter.
4. Native CFE forwarding, Wi-Fi and the install path remain the separate follow-
   ups; do not fold them into this release line.
5. Parent repo: record the release-line consolidation in EVIDENCE-INDEX.md and
   the ledger (newest entry wins; STATUS.md is the live page).

## 12. Evidence map

- `release-line/manifest.tsv` - one row per L0 patch: status, class, sha256,
  purpose.
- `validation/release-line/` - 2026-11-21 harness logs: patch application,
  offsets, strict git-apply divergence, DTS contract, summary.
- Canonical image evidence (parent repo, gitignored/local):
  `build/evidence-bench-gates/2026-11-20-{smp-2cpu.log,smp-reboot.log}`,
  `.../2026-11-21-{soak-2cpu.log,final-reboot-2cpu.log}`, UART captures, images
  in `<local-staging-dir>/` (img-canonical.bin 974e6ca6).
- Root-cause record: `docs/SMP-RE.md` sections 21-24;
  `target-skeleton/docs/port-notes/smp-decode/SUMMARY.md`.
- Parent live state: `STATUS.md` (2026-11-21 FINAL SMP entry) and
  `EVIDENCE-INDEX.md` (SMP rows).
