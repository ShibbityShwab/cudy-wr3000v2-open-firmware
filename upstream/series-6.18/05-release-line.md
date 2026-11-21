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
| **L0 lab patch stack** | every file in `../../target-skeleton/target/linux/hisilicon/patches-6.18/`, applied in filename order by the OpenWrt target build | 62 patch files (001-058 + 062-065; no duplicate numbers) |
| **L1 release line** | L0 minus the bench-only SMP diagnostic patches (section 6); this is the series a release/publish snapshot carries | **51 patch files** (`release-line/INCLUDED.txt`, manifest `release-line/manifest.tsv`) |
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
(sha256 `8ee4b8e71be21f5fc61dd7c48546ce84bfb77550a08b021bdf4804707f4ce115`,
refreshed 2026-11-21 after the stock board_id=3 LED/keys overlay landed).
It adds `arch/arm/boot/dts/hisilicon/luofu-r116.dts` (1,329 lines,
byte-identical to the single-source target DTS including the gpio-keys-polled
and six gpio-leds nodes) plus the `dtb-$(CONFIG_ARCH_LUOFU)` Makefile entry;
post it together with 0018.  The DTS series must also state the base-DTB
padding requirement: the compiled LED/keys DTB is 13,041 B with an unaligned
totalsize and the image build MUST run `Build/luofu-pad-dtb` before
`append-dtb` (TRAILER-FORMAT.md section 11; details in
`06-dts-series-dependency.md` section 3.1).  The dependency record,
ordering and regeneration procedure is `06-dts-series-dependency.md`, and
`tools/check-release-line.sh` applies/compiles DTS-0001, checks the contract
and exercises the pad step on a copy on every validation run.  [V]

Patch `018` alone is necessary but **not sufficient** for SMP; the review-form
documentation now says so.  Do not present 064/065 (or 033-039/062/063) as the
SMP fix in a release or upstream submission - they are diagnostics (section 6).

## 3. Verified baselines

Two verified images are recorded (same kernel/target line; the second adds the
stock board-feature overlay content):

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

The `974e6ca6` details:
- built from: L0 patch stack (62 files: 001-058 + 062-065), the DTS hand-off
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
release patch depends on them (verified: the 51-patch L1 applies in order,
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
- **B - functional gated bench path (18):** 031, 032, 036, 038, 040, 041,
  042-net, 043, 044, 046, 048, 051, 053, 054, 055, 056, 057, 058 - the staged
  switch/PIE/GEMAC power/write/readiness gates and the canonical-config docs
  patch.  Default-off, fail-closed; required for the working LAN workaround.
- **C - included default-off diagnostics (6):** 030, 045, 047, 049, 050, 052 -
  trace/dump gates compiled into the release image for bench parity but never
  enabled by the canonical command line.
- **D - excluded bench diagnostics (11):** section 6.

Counts: 27 + 18 + 6 = 51, plus 11 excluded = 62 L0 files.

Ordering notes (both items fixed 2026-11-21):

- **No duplicate numbers.**  042-net stays; the former 042-arm is 062 and its
  dependent SMP diagnostics are 063/064/065.  Filename order is now dependency
  order for the whole L0 stack (verified 62/62 patch(1) RC=0, 0 fuzz, and strict
  `git apply` 62/62).
- **Patch 053 was regenerated against its post-052 context.**  The old patch was
  authored before 052 merged into the same `MODULE_PARM_DESC(nodoorbell...)`
  anchor, which caused the one fuzz hunk and the strict `git apply` divergence.
  The regenerated patch keeps exactly the same functional content/gates (the
  resulting `luofu-pie.c` is byte-identical to the old stack's final file) and
  now applies with **0 fuzz**; strict `git apply` passes for all 51 L1
  patches.
- **Release-line patch content refreshed (2026-11-21, board-feature pass).**
  Periodic W17/W19 relatch logs were quieted from `dev_warn~/`dev_info`
  to `dev_dbg`: patch **054** (`dev_warn` -> `dev_dbg`) and patch
  **056** (`dev_info` -> `dev_dbg`).  Manifest hashes were refreshed
  (054 `9e3c01c3...`, 056 `eae38ae3...`; manifest sha256
  `4da8b960c3ad83939e0766a86969e8de2737a5b6eb79c91e80d28905f7030fd0`).
  The base-files heartbeat interval also changed 5 s -> 60 s (target content,
  not a kernel patch).

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
- release-critical DTS contract (target DTS and DTS-0001): both `cpp` + `dtc`
  RC=0, **0 stderr bytes**, DTB **13,041 B**, sha256
  `49fe5c471786862e1057409ab920f866312e5f93e77585179dc20cdaebfcf536`;
  `fdtget` confirms `/aliases/console`, `compatible = "hsan,flashinfo_reserved"`,
  `reg = <0x80600000 0x4000>`, `atag-offset = <0x1000>` and
  `/cpus/enable-method = "hisilicon,luofu-smp"`;
- **totalsize alignment:** the 13,041 B DTB totalsize is **not** 8-byte
  aligned; the validator runs `target-skeleton/tools/pad-dtb.py` on a copy,
  which pads it to **13,048 B** and keeps the hand-off contract.  The image
  build MUST run `Build/luofu-pad-dtb` (TRAILER-FORMAT.md section 11; the
  unaligned load made stock U-Boot data-abort and fall back to stock on
  2026-11-21) [V];
- **DTS-0001**: applies to the L1 tree RC=0; its added board DTS is
  byte-identical to the single-source target DTS; the compiled DTB and the
  contract checks are identical to the target-DTS ones [V];
- upstream review form: 26/26 apply with patch(1) and `git am` (3 offset
  hunks, 0 fuzz, identical trees; checkpatch 0 errors / 58 warnings) [V];
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
- **No thermal support on this board:** the temperature register reads 0 and the
  vendor DT disables the sensor; the thermal driver/zone remains host-only and
  is not a release claim [U]/[X].
- **No performance/soak beyond the recorded 15 minutes**; no long-term thermal or
  NAND-degradation campaign.

## 11. Next actions after this consolidation

1. **Post DTS-0001 with kernel patch 0018** (same window, DTS entry first or
   together), referencing SMP-RE.md section 24 and the base-DTB 8-byte
   alignment/pad-dtb requirement (TRAILER-FORMAT.md section 11) in its cover
   letter.  Regenerate DTS-0001 only from the target DTS via
   `tools/gen-dts-series-patch.py` (2026-11-21 refresh: board LEDs/keys).
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
- `validation/release-line/` - 2026-11-21 harness logs: L1/L0 patch
  application, offsets, strict git-apply, target DTS contract, DTS-0001
  apply/compile/contract, summary.
- Board-feature refresh: submodule `aff7e27` (gpio-keys-polled + six
  gpio-leds DTS nodes from the stock board_id=3 overlay, pad-dtb, watchdog
  sysfs config), verified image `7f270b8c`; measurement in
  `../../docs/TRAILER-FORMAT.md` section 11 and
  `target-skeleton/tools/pad-dtb.py`.
- Canonical image evidence (parent repo, gitignored/local):
  `build/evidence-bench-gates/2026-11-20-{smp-2cpu.log,smp-reboot.log}`,
  `.../2026-11-21-{soak-2cpu.log,final-reboot-2cpu.log}`, UART captures, images
  in `<local-staging-dir>/` (img-canonical.bin 974e6ca6, feature image
  7f270b8c).
- Root-cause record: `docs/SMP-RE.md` sections 21-24;
  `target-skeleton/docs/port-notes/smp-decode/SUMMARY.md`.
- Parent live state: `STATUS.md` (2026-11-21 FINAL SMP entry) and
  `EVIDENCE-INDEX.md` (SMP rows).
