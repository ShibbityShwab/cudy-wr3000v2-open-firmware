# Upstream submission form - luofu 6.18 kernel series

This directory is the **upstream-submission form** of the kernel driver
series that currently lives as plain patches in
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`.  It does
not replace or modify those files: it is a review artefact that adds
commit messages, a cover letter, a MAINTAINERS proposal, a tree/order
rationale, and a repeatable host-only validation, plus a commit-ready
(format-patch style) copy of the same 26 patches.

Status: **upstream review form; the target image built from this patch
stack is bench-verified, but the series itself has had no upstream review
and most drivers remain draft/RFC.**  Sequential application to pristine
linux-6.18.60 and checkpatch pass (see 04-validation.md); the series
compiles and its bindings validate (recorded elsewhere).  The target image
built from the extended patch stack (0018 + the loader hand-off DTS fix)
booted on hardware on 2026-11-21 with 2 CPUs, network (the documented
mirror workaround), SSH and a persistent overlay (canonical image
`974e6ca6`; see 05-release-line.md).  It has still not been
upstream-reviewed; per-driver hardware verification is partial and these
behaviours remain unmeasured: no clock rate or PLL/mux parent read back,
the PIE engine and RX/TX descriptors only exercised through the gated
bench path, the full LSW W3-W16 write campaign not run as an upstream
feature, no independent GEMAC netdev registration outside the bench gates,
and the bdinfo MAC never read from a real unit.  The form
was regenerated on 2026-11-18 four times: after patch 0017 (A/B boot
selector) was corrected against the local evidence reconstruction
`../../docs/SELECTOR-0x21.md`, after patch 0018 (SMP bring-up) was added
from the SMP lane, after patch 0019 (CRG clock tree) was added from the
clock-tree lane, and after patches 0020 (PIE datapath + GEMAC front end)
and 0021 (LSW read-only baseline probe) were added from the 2026-11-18
measured-inventory lanes.  On 2026-11-19 it was regenerated a fifth time
after patches 0022 (gated LSW W3/W4/W5 write path) and 0023 (GEMAC netdev
readiness gate) were added from the net lane, and a sixth time after
patches 0024 (PIE RX completion decode + TX descriptor), 0025 (gated LSW
W4 egress/init write stage) and 0026 (bdinfo-MAC nvmem GEMAC source) were
added from the pie-decode, lsw-egress and mac-nvmem lanes.  The series,
commit messages, validation logs and hashes were all regenerated in the
final 26-patch pass.

## 2026-11-21 release-line update (read this first)

- **Submission package (documentation only):** `submission/README.md` is the
  dry-run artifact checklist + one-page cover-email draft + file manifest for
  the kernel review form and DTS-0001.  Nothing has been submitted anywhere.
  Current devicetree final: commit **6c2cc2f** (conservative cleanup
  `7ae18f2` + boot-tested TWD level-high + the three final boot-tested
  board deltas); current DTS-purpose hardware baseline image **c3ef2a50**
  (f6685b80 was the previous DTS-purpose baseline, 7421bb7a the earlier
  verified baseline).
- **SMP is solved on hardware and the fix is DTS-only (release-critical).**
  The kernel release (0018) was correct; the stock U-Boot `bootfip` hand-off
  staging silently aborted because the board devicetree lacked the stock
  base DTB's `/aliases/console` and `/reserved-memory/flashinfo@80600000`
  (`compatible = "hsan,flashinfo_reserved"`, `reg = <0x80600000 0x4000>`,
  `atag-offset = <0x1000>`).  With those nodes in the board DTS the target
  image boots 2 CPUs (mpidr 80000001, 15-minute soak, reboot persistent).
  0018 is necessary but not sufficient; a future upstream DTS series must
  carry the two nodes.  Evidence: `../../docs/SMP-RE.md` section 24 and
  `05-release-line.md` section 2.

- **The DTS dependency is now an explicit upstream artefact: DTS-0001.**
  `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`
  (sha256 `c029fd44e686e4366d238056582d4bd6c7dcc1a280cadda3792b76991c4ff5b8`,
  current final 2026-11-21: conservative comment-only cleanup + four
  individually boot-tested active deltas) adds
  `arch/arm/boot/dts/hisilicon/luofu-r116.dts` (**681 lines**) and the
  `dtb-$(CONFIG_ARCH_LUOFU)` Makefile entry; it is the companion of kernel
  patch 0018 and is documented in `06-dts-series-dependency.md`.  The
  validator applies/compiles it and checks the loader hand-off contract.  The
  cleanup record, the **hardware regression of the superseded active cleanup**,
  the boot-test adoptions and the **two remaining upstream blockers** are in
  `07-dts-upstream-bindings.md`.

  **Hardware regression (2026-11-21):** the first active-DTS cleanup (commit
  538377c, image 6f19c6d6) did not boot - the kernel came up with SMP but MTD
  partition registration stalled and the unit reset on the U-Boot watchdog and
  fell back to stock pair A (UART log
  `build/evidence-bench-gates/2026-11-21-cleaned-dts-regression.log`,
  sha256 15dc3188...).  The known-good image 7421bb7a (pre-clean DTS) boots
  with partition scan 0.93-1.16 s.  The conservative cleanup restored the
  known-good active content (compiled DTB byte-identical); four active deltas
  were then adopted, each individually boot-tested: TWD level-high (commit
  `5b401c1`, image f6685b80) and MAC-nvmem removal + switch MMIO-first
  order + cudy-compatible ordering (commit `6c2cc2f`, image **c3ef2a50**:
  2 CPUs, `/proc/interrupts` `GIC-0 29 Level twd`,
  `/tmp/sysinfo/board_name = cudy,wr3000-v2`, userspace MACs ...:73/...:74,
  br-lan up, ping 15/15, no errors).  **Host checks are necessary but not
  sufficient - the image must be boot-tested after any active-DTS change.**
- **Board-feature refresh (2026-11-21).**  The stock board_id=3 overlay landed
  in the single-source DTS: gpio-keys-polled (reset/WPS) and six
  `oem:green:*` gpio-leds, with the matching target config additions;
  verified image **`7f270b8c`** (2 CPUs, 6 LEDs writable, `event0`,
  `watchdog0` sysfs; **thermal remains unsupported** - temp reads 0, vendor
  DT disables the sensor).  The compiled DTB became 13,041 B with an unaligned
  totalsize, which made the stock U-Boot bootfip gate-word load data-abort, so
  the image build now runs `Build/luofu-pad-dtb` (13,048 B) before
  `append-dtb` - see top-level `docs/TRAILER-FORMAT.md` section 11 and
  `06-dts-series-dependency.md` section 3.1.  Release-line patch hashes
  were refreshed for 054/056 (`dev_dbg` quieting).
- **The release line is defined in `05-release-line.md`.**  It is the
  target patch stack (**62 files: 001-058 + 062-065**, no duplicate
  numbers) minus 11 bench-only SMP diagnostic patches:
  `release-line/manifest.tsv` has one row per patch (include/exclude,
  class A/B/C/D, sha256, purpose), 51 included.  The upstream review form in
  `series/` stays at 26 kernel patches; the release line is the
  target/publication classification.  Patch 053 was regenerated against its
  post-052 context, so the release line now applies with **0 fuzz** and is
  strict `git apply`-clean (51/51).
- **Bench-only diagnostics are excluded** from the release line: 028, 029,
  033, 034, 035, 037, 039, 062, 063, 064, 065 (section 6 of
  `05-release-line.md`; the former duplicate 042 became 062 and its
  dependent diagnostics moved to 063/064/065 to keep filename order equal to
  dependency order).  They remain in the lab stack so the canonical image
  `974e6ca6` stays reproducible, but a release/publish snapshot must not
  carry them as the SMP fix.
- **Canonical configuration, no probe tokens:** the built-in cmdline is
  `ubi.mtd=rootfsb ... maxcpus=2 nr_cpus=2 additional_cpus=1` (see
  `config-6.18` / `05-release-line.md` section 4); `luofu_smp_*` tokens
  are investigation-only and are not part of the canonical or release
  command line.
- **The earlier "2-CPU hang" is retracted** - it was a host USB-Ethernet
  bench-link outage, not a kernel hang; no Oops/panic/BUG/WARNING was in
  the log and the same image passed the soak after the link was restored.

The 0018 commit message records the DTS-0001 dependency; the series files,
01-commit-messages.md, the release-line manifest/hashes and the validation
logs were refreshed on 2026-11-21.  Kernel-series content diffs are unchanged;
the release-line cleanup regenerated 053, renumbered 042/059/060/061 and added
DTS-0001.  A second 2026-11-21 pass refreshed DTS-0001 for the board LED/keys
overlay, hashed 054/056 after the `dev_dbg` quieting (manifest sha256
`4da8b960...`) and added the base-DTB pad-dtb requirement.

## What is in here

| file | purpose |
| --- | --- |
| `00-cover-letter.md` | the [PATCH 00/26] cover letter: scope, hardware, dependencies, test evidence, explicit no-hardware statement, ready-vs-RFC split, recommended submission sequence |
| `01-commit-messages.md` | per-patch commit-message drafts grouped by readiness (core platform, ready drivers and clock tree, RFC drivers, do-not-merge drafts including the PIE/LSW completion/egress/MAC follow-ups, SMP bring-up), plus the series-file -> source-patch -> status mapping table and sha256 provenance table |
| `02-maintainers-proposal.md` | MAINTAINERS entry snippet (developer identity from the repo git config, file patterns, S: status, S/RFC mapping) |
| `03-rationale-tree-placement.md` | why the files live where they do (mach-hisilicon vs mach-hisi, ARM core changes, DTS delivery, Kconfig symbol placement/naming including 0018/0019/0021/0022/0024/0025/0026 adding no symbol, 0020 changing only CONFIG_PHYLINK and 0023 adding only a Kconfig dependency, series order and bisectability, merge-order conflicts, SMP staging S1/S2, PIE/LSW staging and the gated W3/W4/W5 write path, the decoded RX completion/TX descriptor and the gated W4 egress stage, netdev readiness and the bdinfo-MAC source, and their measured-inventory/disassembly dependencies) |
| `04-validation.md` | the validation record: inputs, commands, apply results (RC, offsets), checkpatch table, earlier recorded compile/binding/DTS/config results, limitations, and the dated 17/18/19/21/23/26-patch re-validation sections |
| `series/0001-0026`.patch | commit-ready, format-patch style series (same diffs + commit messages); `git am` clean |
| `commit-messages/` | source of truth for the per-patch messages; edit these, not 01 or series/ |
| `tools/gen-series.py` | regenerates `series/` and `01-commit-messages.md` from `commit-messages/` + the source patches; `--check` verifies no drift |
| `tools/run-validation.sh` | reproduces section 04 on any host with git/patch/tar/xz (no network); extracts a pristine 6.18.60 tree if given the tarball |
| `05-release-line.md` | the 2026-11-21 release line: verified 2-CPU baseline, the DTS-only SMP fix, canonical configuration, working-LAN workaround, excluded bench diagnostics and the false-hang retraction |
| `06-dts-series-dependency.md` | the upstream DTS series dependency (DTS-0001): board DTS + loader hand-off nodes required by kernel patch 0018, ordering, single-source/regeneration rule and validation |
| `07-dts-upstream-bindings.md` | the board-DTS record: the hardware regression of the superseded active cleanup, the conservative comment-only cleanup, the four individually boot-tested active deltas, the manual binding review (no dtschema available), compile/pad facts and the two remaining upstream blockers |
| `dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` | commit-ready DTS-series entry (DTS-0001): board DTS + hisilicon DTS Makefile line; companion to 0018 |
| `tools/gen-dts-series-patch.py` | deterministic generator for DTS-0001 from the single-source target DTS |
| `release-line/manifest.tsv` | one row per target patch: include/exclude, class A/B/C/D, sha256, purpose (51 included, 11 excluded) |
| `tools/check-release-line.sh` | host-only validation of the release line (patch application order/offsets/fuzz + loader hand-off DTS contract) |
| `validation/` | raw logs and hashes from the final validation runs (review form + `validation/release-line/`) |

## How to apply

For review or a scratch test against pristine linux-6.18.60:

    tar -xJf linux-6.18.60.tar.xz
    cd linux-6.18.60
    git init && git add -A && git commit -m pristine    # optional, for git am
    git am /path/to/series-6.18/series/*.patch

or without git:

    cd linux-6.18.60
    for p in /path/to/series-6.18/series/*.patch; do patch -p1 -i "$p" || break; done

The series applies in numerical order (0001 -> 0026); 0019 extends the
CRG provider added by 0004 (and the 0014 polarity fix): its binding makes
seven input clocks required, and the board DTS that supplies them is not
part of this series.  0018 adds no binding and is inert unless the board
DTS names the `hisilicon,luofu-smp` CPU method.  0020-0026 are last:
0020 rewrites the GEMAC/PIE drivers from 0009/0015, 0021 extends the
switch driver from 0016 and applies with a small ethernet-Kconfig offset
because 0020 edits the same file first, 0022 extends the switch driver and
binding from 0021 (its default path is still the 0021 read-only probe),
0023 extends the GEMAC driver from 0020 and adds its Kconfig dependency on
LUOFU_SWITCH, 0024 extends the PIE/GEMAC drivers and their two bindings
from 0020/0023 (it makes the measured `hisilicon,port` property required,
decodes the RX completion and builds the full normal TX descriptor), 0025
extends the switch driver, its register header and Kconfig help from 0022
(the gated W4 R1/R3/R5 probe-configuration stage, which tightens readiness
to require both that configuration and the W2 qid map) and 0026 extends
the GEMAC driver and its binding from 0023 (the read-only bdinfo-MAC nvmem
source; it is refreshed against the post-0024 GEMAC file).  All seven are
drafts, fail-closed and have never run on hardware; see
03-rationale-tree-placement.md sections 6, 9, 10 and 11.

For the OpenWrt target build, keep using
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/` directly:
OpenWrt's patch application wants the plain diffs there, and the recorded
compile evidence was made with those files.  The `series/` copies are for
mailing/validation, not for the OpenWrt build; do not maintain both copies
by hand (the generator keeps the diffs identical).

To check a patch the way a maintainer would:

    <kernel>/scripts/checkpatch.pl --no-tree --strict series/0004-*.patch

To re-run the whole validation:

    tools/run-validation.sh /path/to/linux-6.18.60.tar.xz /tmp/luofu-val
    # or: tools/run-validation.sh --use-tree /path/to/extracted-6.18.60 /tmp/luofu-val

To regenerate the series after editing a message:

    python3 tools/gen-series.py
    python3 tools/gen-series.py --check

## What is validated (host-only)

- 26/26 series files apply in order to pristine linux-6.18.60 with
  patch(1) and with `git am`; the two resulting trees are identical.  [V]
  0 fuzz, 0 rejects; patch 0008 (PHY) has two apply offsets because the
  source patch was generated against an OpenWrt-patched kernel tree, and
  patch 0021 has a 6-line offset in the ethernet Kconfig because 0020
  edits the same file first.  Patches 0022-0026 are last and apply
  cleanly with no offset.  Re-run on 2026-11-21 after the 0018 message
  update: 26/26 patch(1) and 26/26 `git am` RC=0, identical trees, 3 offset
  hunks, 0 fuzz, 0 rejects, 0 errors / 58 checkpatch warnings.  [V]
- the release line (51 target patches, `05-release-line.md`) applies in
  order to pristine 6.18.60: 51/51 patch(1) RC=0, 146 file events, **3 offset
  hunks, 0 fuzz, 0 rejects**; strict `git apply`: **all 51 patches apply
  without fuzz**.  The L0 stack (62 files, 001-058 + 062-065) also applies
  62/62 with 0 fuzz and strict-clean.  The loader hand-off DTS contract
  compiles (`cpp`+`dtc` RC=0, 0 stderr, DTB **12,650 B** sha256
  `a0af289969ff7a458448a8171eaf56b8332e18c771c362880af88a1998721b94`,
  current final incl. all four boot-tested active deltas) and contains
  `/aliases/console` and the `flashinfo@80600000` node with the measured
  `reg`/`atag-offset`.  The totalsize is unaligned, so the validator
  exercises `pad-dtb.py` on a copy (**12,656 B** sha256 `01b74561...`,
  contract preserved) and records the required `Build/luofu-pad-dtb`
  image step; the current DTB was boot-tested as image **c3ef2a50**.
  **DTS-0001** (sha256 `c029fd44...`, 694 lines) applies, compiles to the
  same DTB and its board DTS is byte-identical to the target DTS.  Logs:
  `validation/release-line/`.  [V]
- the target image built from the extended stack is hardware-verified for
  the release baseline: 2 CPUs (mpidr 80000001, BogoMIPS 3987.86), LAN via
  the documented mirror workaround, SSH, ubifs overlay, 15-minute soak
  0 failures and reboot persistence (image `974e6ca6`).  This is
  target-image evidence, not an upstream Tested-by.  [V for the recorded
  bench evidence, [I]/[U] for the per-driver claims it does not cover]
- checkpatch.pl --no-tree: 0 errors, 58 warnings (default and strict; plus
  6 strict CHECKs), all enumerated in 04-validation.md.  The 2 added
  warnings are the expected per-patch review items for 0024 and 0026 (one
  binding-split warning each; 0025 adds no file, no binding hunk and no
  warning; 0024/0025/0026 add no file, so the MAINTAINERS-warning count
  stays 18); all are review work, not behavioural defects.  [V]
- series diff bodies are byte-identical to the source patches apart from
  the inserted git extended headers; hashes recorded (52/52 sha256
  entries verify).  [V]
- kernel build of the final 26-patch series was not re-run here (no rebuild
  needed): the pie-decode lane's recorded fully-clean 001-026 build on a
  fresh kernel extraction is prepare RC=0 (1,233 patching-file lines,
  0 .rej) and compile RC=0 with **0 errors and 0 compiler warnings** under
  CONFIG_WERROR=y (37 pre-existing modpost MODULE_DESCRIPTION warnings);
  zImage **3,282,528 B sha256 `08b76747...`**, `_text = stext =
  0xc0608000`; the same lane's 001-024 build is RC=0 with 0 warnings
  (zImage 3,281,400 B sha256 `f4a736cf...`) and its 001-026 config
  re-derivation leaves the target config-6.18 byte-identical to the
  001-023 artifact at `d1af1b2a...` (0024-0026 add no Kconfig symbol).
  The LSW-init/net 001-0021/001-0023 builds (zImages 3,279,096 B sha256
  `96b927d3...`, 3,281,376 B sha256 `5903db04...`) and the mac-nvmem
  lane's 001-023+026 build (zImage 3,281,744 B sha256 `8443c8c0...`) stay
  current as intermediate records, and the lsw-egress lane compiled
  024+025 (vmlinux 133,286,956 B sha256 `63648e75...`, W=1 clean for the
  changed drivers).  The earlier 001-0019 clock-tree build (zImage
  3,264,560 B sha256 `4456b0ec...`) and the SMP records remain current;
  the extended PIE/GMAC/switch bindings pass dt_binding_check and the
  lanes' board DTS (not part of this series) compiles with zero warnings
  (the final integrated 001-026 DTS is 12,076 B sha256 `5386bade...`).
  Details and citations: 04-validation.md sections 9 and 13-18.  [V]

## What a reviewer should NOT expect

- **No upstream Tested-by and no per-driver hardware validation.**  The
  target image is bench-verified (2 CPUs, network through the documented
  mirror workaround, SSH, overlay, soak, reboot), but the series itself has
  not been upstream-reviewed and most drivers have not been individually
  exercised on silicon.  The compile/binding evidence proves buildability
  and interface conformance; it does not prove every driver works.  Do not
  add a Tested-by you did not run.
- **SMP is no longer an open question on the release image, but 0018 is
  still an RFC for upstream.**  0018 implements the decoded release and is
  necessary but not sufficient: the target images get 2 CPUs only when the
  board DTS also carries the two loader hand-off nodes (console alias +
  flashinfo reserved-memory).  Both are board-DTS content, not part of this
  kernel series, so in this form 0018 is inert until the DTS series lands.
  See `../../docs/SMP-RE.md` section 24 and `05-release-line.md` section 2.
- **No measured clock rates or PLL/mux parents.**  Patch 0019 registers
  the CRG's two PLLs and two muxes as read-only clocks (no set_rate, no
  enable/disable, set_parent returns -EOPNOTSUPP) and parents the gates on
  the decoded vendor-DT input clocks.  The PLL rate equation and the
  mux/gate parents are decoded from the vendor hi_clk.ko and devicetree;
  the boot-programmed control words, the 0x90 status bits and every derived
  rate are [U] until an attended read-only clk_summary plus control-word
  readback.  Its binding requires seven input clocks that the board DTS
  (not in this series) must provide.  The CPU mux must never become
  writable: it selects the CPU clock source.
- **Not a working network port.**  GEMAC (0009) is a binding/resource half
  that refuses to probe; PIE (0015) and the internal switch (0016) are
  scaffolding that refuse to probe.  The follow-ups 0020 (PIE ring engine
  + GEMAC netdev/phylink front end), 0021 (LSW read-only baseline probe),
  0022 (gated LSW W3/W4/W5 write path), 0023 (GEMAC netdev readiness
  gate), 0024 (PIE RX completion decode + TX descriptor), 0025 (gated LSW
  W4 egress/init write stage) and 0026 (bdinfo-MAC nvmem source) implement
  the measured and disassembled models, but 0020's data path is behind the
  `luofu_pie.arm=1` command-line opt-in and has never been armed, 0021
  never writes a register, 0022's write gates have never been set, 0023
  registers nothing unless the PIE arm gate and all LSW gates pass in one
  attended boot, 0024's decoded completion/TX descriptor has never been
  observed by the engine, 0025's egress gate has never been set, and 0026's
  nvmem read has never run.  The RX completion field positions are [V] from
  the vendor disassembly but the live values are unobserved, and the
  TX-to-egress queue/port mapping is still [U].  No frame reaches a wire.
- **No measured PIE/LSW write behaviour and no GEMAC netdev.**  Patches
  0020-0026 encode the 2026-11-18 read-only inventory of the stock vendor
  system and the read-only vendor disassembly of its GPL modules.  No PIE
  register has been written by our code, the engine has never been armed,
  no RX completion or TX descriptor has been accepted by the engine, no
  LSW clock/reset, probe-configuration or qid-map write has run (0022's
  W3/W4/W5 gates and 0025's W4 egress gate have never been passed), no
  readback/restore path has ever executed, no netdev has registered and
  the bdinfo nvmem MAC has never been read.  The measured baseline is one
  boot (no second-unit stability measurement yet), and the decode is
  unobserved on silicon.  The attended PIE arm session, the LSW W3-W5
  campaign and the decode/egress validation sequence are owed, and 0023's
  combined PIE+LSW gate boot has never happened; see
  `../../target-skeleton/docs/port-notes/pie/SUMMARY.md`,
  `../../target-skeleton/docs/port-notes/lsw/SUMMARY.md`,
  `../../target-skeleton/docs/port-notes/net/SUMMARY.md`,
  `../../target-skeleton/docs/port-notes/pie-decode/SUMMARY.md`,
  `../../target-skeleton/docs/port-notes/lsw-egress/SUMMARY.md` and
  `../../target-skeleton/docs/port-notes/mac-nvmem/SUMMARY.md`, and
  `../../docs/PIE-DATAPATH.md` / `../../docs/LSW-SPEC.md`.
- **Not a working storage/PCIe/restart/selector claim.**  The FMC
  (0006), PCIe host (0013), restart (0010) and A/B selector (0017) are RFC
  or provisional: hardware semantics are unmeasured.  For 0017 the
  recorded-pair contract is now reconstructed from local evidence (env
  `bootflag` selects the pair, the SYSCTRL cell records it, 0x21 is the
  project's bench value and never persists) and the store is the loader-style
  low-byte read-modify-write, but the protocol and the env re-stamp are not
  hardware-validated and its post-boot stability depends on a boot-time
  recorder in the target image, not on this series.  None of these drivers
  has been validated on this board.
- **No Wi-Fi.**  Hi5622V100 is vendor-only [X]; the series contains no
  Wi-Fi code and makes no radio claim.
- **No board DTS in this series.**  The draft DTS lives in
  `target-skeleton/dts/` and is delivered through the OpenWrt target for
  now.  A kernel DTS submission is a later series, after the bindings are
  accepted and a full-draft `dtbs_check` is clean (03-rationale).
- **No installation path.**  First install/factory image and A/B
  sysupgrade are host-only, mocked or explicitly refusing; nothing has
  been flashed.
- **Not publication-ready.**  `opensource/docs/PUBLISH-READINESS.md` is
  still open (vendor .o history purge, GPL reply, DCO/licence files).
  This directory does not change that status.
- **No vendor code or binaries.**  The drivers are GPL-2.0 code derived
  from documented reverse engineering of the vendor's GPL binaries; no
  vendor blob, object, extracted DTB or calibration data is included.

## Status split (summary)

| group | patches | status |
| --- | --- | --- |
| core platform (machine, Kconfig/TEXT_OFFSET, ARM early boot) | 0001-0003 | RFC |
| drivers and clock tree ready for review (CRG, watchdog, MDIO, PHY, thermal, CRG polarity fix, CRG clock tree) | 0004, 0005, 0007, 0008, 0012, 0014, 0019 | ready-for-review (still no hardware run; 0019 is read-only and its decoded clock values are unmeasured) |
| drivers RFC (FMC, pinctrl, PCIe, selector) | 0006, 0011, 0013, 0017 | RFC |
| partial/refusing drafts (GEMAC, restart, PIE, LSW) | 0009, 0010, 0015, 0016 | do not merge; RFC/design review only |
| PIE datapath + GEMAC front end, LSW read-only baseline probe (follow-ups to those drafts) | 0020, 0021 | do not merge; 0020 is an arm-gated draft that has never run and still cannot decode RX completions; 0021 is read-only and its baseline is one measured boot |
| gated LSW write path and GEMAC netdev readiness (follow-ups to 0020/0021) | 0022, 0023 | do not merge; 0022 keeps the read-only default and gates every clock/reset/table write behind never-set module parameters with readback/restore; 0023 registers nothing until the PIE arm gate and both LSW gates pass in one attended boot |
| PIE completion/TX decode, gated LSW W4 egress stage and MAC-source nvmem (follow-ups to 0020/0022/0023) | 0024, 0025, 0026 | do not merge; 0024 stays behind the PIE arm gate and its decode has never been observed by the engine; 0025 gates the R1/R3/R5 probe-configuration write set behind never-set module parameters (requiring the W3 power step) and tightens readiness so both that configuration and the qid map must verify; 0026 reads the MAC from a read-only bdinfo nvmem cell but registers nothing until the 0023 gate passes and the read has never run.  None has run on hardware |
| SMP bring-up | 0018 | RFC for upstream review; the target image is hardware-verified with 2 CPUs (2026-11-21) once the board DTS carries the loader hand-off nodes; 0018 alone is necessary but not sufficient. The release-line diagnostics around it are excluded from the release series |
| DTS series entry | DTS-0001 (not a kernel patch) | cleaned board DTS required by 0018; carries the two loader hand-off nodes and the board features; post with 0018; remaining upstream blockers are listed in `07-dts-upstream-bindings.md` |

The recommended real submission splits this into per-subsystem series and
folds 0014 into 0004; see 00-cover-letter.md "Recommended submission
sequence".

## Decisions required from the owner before sending

1. Real legal name for `Signed-off-by` (the repo config identity is used
   as a placeholder in the messages and in the 13 binding files).
2. Approve the MAINTAINERS entry and submit it as its own patch.
3. Split bindings out of the driver patches (mechanical).
4. Fold 0014 into 0004 (and decide whether 0019 is folded in or posted
   as the immediate clock-tree follow-up); regenerate 0008 against
   mainline; consider renaming LUOFU_FMC to SPI_LUOFU_FMC and normalising
   the Kconfig gating.
5. Fix the checkpatch review items listed in 04-validation.md (line wrap,
   help text, spinlock/alignment comments) when the patches are touched
   for the split; they are deliberately not changed here because this task
   must not edit the existing patches.
6. Approve the SMP posting (0018) as RFC: the `hisilicon,luofu-smp`
   method name and driver-data decisions are settled and the target image
   is bench-verified (2 CPUs, 2026-11-21).  0018 alone is not sufficient:
   the board DTS series must carry the enable-method plus the two loader
   hand-off nodes (`/aliases/console` and `/reserved-memory/flashinfo@80600000`
   with `atag-offset = <0x1000>`).  Decide whether to post 0018 now with
   that dependency stated, or together with the DTS series; the dependency is
   the commit-ready DTS-0001 entry (see `06-dts-series-dependency.md` and
   the `dts-series/` directory).  Do not post the SMP investigation
   diagnostics (064/065 etc.) as the fix.
7. Approve the clock-tree staging (0019): read-only PLL/mux model, decoded
   rates/parents pending one attended read-only readback, no set_rate, and
   the binding change that makes seven input clocks (board-DTS content)
   required.
8. Approve the PIE/LSW staging (0020/0021): the `luofu_pie.arm=1`
   command-line gate and the disabled devicetree nodes for 0020; the
   read-only LSW probe with its fail-closed measured-baseline check; and
   whether 0021 is posted for review now (it has no write risk) while 0020
   waits for the attended arm session.  Also decide the long-term shape of
   the PIE/GEMAC split (private `luofu-pie.h` backend today) before the
   netdev reviewers see it.
9. Approve the connectivity gates (0022/0023): the default-off
   `luofu_lsw.arm=1` / `luofu_lsw.qidmap=1` staging of the W3/W4/W5
   write path, the W2 qid-map rows as the first reviewed write set, the
   pre-write conformance gate plus readback/restore policy, the
   `luofu-switch.h` readiness contract, the measured `hisilicon,port`
   binding property (board-DTS content), and the 0023 netdev registration
   gate with the PHYLINK_NETDEV carrier fix.  Decide whether 0022/0023 are
   posted at all before the W3-W5 campaign and the combined gate boot have
   run (they are drafts like 0020), and re-check the checkpatch
   binding-split/MAINTAINERS items when they are touched.
10. Approve the completion/egress/MAC staging (0024/0025/0026): the
    decoded v250 RX completion and normal non-TSO TX descriptor behind
    the existing `luofu_pie.arm=1` arm path, with the measured
    `hisilicon,port` property now required on every MAC node and a
    port->net_device registration table (0024); the default-off
    `luofu_lsw.egress=1` W4 R1/R3/R5 probe-configuration stage with its
    conformance gate, derived readback expectations and restore, which
    tightens `luofu_switch_ready()` to require both that configuration
    and the W2 qid map (0025); and the read-only bdinfo-MAC nvmem source
    with its -EPROBE_DEFER handling, userspace-MAC compatibility and
    OpenWrt `mac-base` backport dependency (0026).  Decide whether any
    of them is posted before the attended decode/egress/gate session has
    run - they are drafts like 0020-0023 - and whether the DTS nvmem
    wiring and guarded board.d map stay target content for now.

## Provenance and policy

- Source-tree patches (unchanged): `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/001-026`.
- This directory was created for task-29 and extended for task-34 (patch
  0018), task-37 (patch 0019), task-48 (patches 0020/0021), task-54
  (patches 0022/0023) and task-59 (patches 0024/0025/0026); it modifies no
  existing patch, target-skeleton document or shared doc.  `series/` is
  generated, never hand-edited; the mapping table in 01-commit-messages.md
  ties every file back to its source patch.
- Evidence grading used across the project: [V] verified from an artifact
  or measurement, [I] inferred/implementation, [U] unmeasured,
  [X] blocked.  All hardware behaviour in this directory is [U] except the
  recorded vendor register values that the drivers cite; all claims of a
  working device are [X] for now.
