# Upstream submission form - luofu 6.18 kernel series

This directory is the **upstream-submission form** of the kernel driver
series that currently lives as plain patches in
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`.  It does
not replace or modify those files: it is a review artefact that adds
commit messages, a cover letter, a MAINTAINERS proposal, a tree/order
rationale, and a repeatable host-only validation, plus a commit-ready
(format-patch style) copy of the same 26 patches.

Status: **host-only submission form; no hardware validation of any kind.**
Sequential application to pristine linux-6.18.60 and checkpatch pass
(see 04-validation.md); the series compiles and its bindings validate
(recorded elsewhere); nothing has booted, CPU1 has never been released and
no clock rate or PLL/mux parent has been read back, the PIE engine has
never been armed, no RX completion or TX descriptor has ever been observed,
the LSW write campaign has never run, no GEMAC netdev has ever registered
and the bdinfo MAC has never been read from the MTD nvmem path.  The form
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
| `validation/` | raw logs and hashes from the final validation run |

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
  cleanly with no offset.
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

- **No boot, no hardware validation, no Tested-by.**  Nothing here has run
  on the router; no driver has probed on silicon; the platform has never
  booted a mainline kernel, and the SMP release has never brought up CPU1.
  The compile evidence proves buildability and interface conformance only.
- **No SMP.**  Patch 0018 decodes the vendor CPU1 release but is
  RFC/attended: the SCU pre-state, the pen-word consumer and the CRG
  resume-bit semantics are unmeasured, and the devicetree enable-method is
  not in this kernel series (board DTS), so 0018 is inert in this form.
  It is not part of the UP-first mergeable set and its posting gate is the
  attended S2 bring-up in `../../docs/SMP-RE.md`.
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
| SMP bring-up | 0018 | RFC/attended; host-validated only, never run on our kernel; not in the UP-first set |

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
6. Approve the SMP plan (0018): the `hisilicon,luofu-smp` method name,
   the driver-data (not DT property) decisions, and the attended S2
   bring-up before posting; keep 0018 out of the UP-first submission.
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
