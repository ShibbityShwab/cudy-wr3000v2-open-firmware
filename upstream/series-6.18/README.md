# Upstream submission form - luofu 6.18 kernel series

This directory is the **upstream-submission form** of the kernel driver
series that currently lives as plain patches in
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/`.  It does
not replace or modify those files: it is a review artefact that adds
commit messages, a cover letter, a MAINTAINERS proposal, a tree/order
rationale, and a repeatable host-only validation, plus a commit-ready
(format-patch style) copy of the same 17 patches.

Status: **host-only submission form; no hardware validation of any kind.**
Sequential application to pristine linux-6.18.60 and checkpatch pass
(see 04-validation.md); the series compiles and its bindings validate
(recorded elsewhere); nothing has booted.

## What is in here

| file | purpose |
| --- | --- |
| `00-cover-letter.md` | the [PATCH 00/17] cover letter: scope, hardware, dependencies, test evidence, explicit no-hardware statement, ready-vs-RFC split, recommended submission sequence |
| `01-commit-messages.md` | per-patch commit-message drafts grouped by readiness (core platform, ready drivers, RFC drivers, do-not-merge drafts), plus the series-file -> source-patch -> status mapping table and sha256 provenance table |
| `02-maintainers-proposal.md` | MAINTAINERS entry snippet (developer identity from the repo git config, file patterns, S: status, S/RFC mapping) |
| `03-rationale-tree-placement.md` | why the files live where they do (mach-hisilicon vs mach-hisi, ARM core changes, DTS delivery, Kconfig symbol placement/naming, series order and bisectability, merge-order conflicts) |
| `04-validation.md` | the validation record: inputs, commands, apply results (RC, offsets), checkpatch table, earlier recorded compile/binding/DTS/config results, limitations |
| `series/0001-0017`.patch | commit-ready, format-patch style series (same diffs + commit messages); `git am` clean |
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

- 17/17 series files apply in order to pristine linux-6.18.60 with
  patch(1) and with `git am`; the two resulting trees are identical.  [V]
  0 fuzz, 0 rejects; one patch (0008, PHY) has two apply offsets because
  the source patch was generated against an OpenWrt-patched kernel tree.
- checkpatch.pl --no-tree: 0 errors, 46 warnings (default and strict; plus
  4 strict CHECKs), all enumerated in 04-validation.md.  They are review
  work items (binding split, MAINTAINERS entry, line wrapping, help text,
  two spinlock comments, two alignments), not behavioural defects.  [V]
- series diff bodies are byte-identical to the source patches apart from
  the inserted git extended headers; hashes recorded.  [V]
- recorded earlier (not re-run here): the whole series builds in the
  OpenWrt patched tree (prepare RC=0, compile RC=0, 0 warnings under
  CONFIG_WERROR=y, zImage sha256 `a4e4f6c0...`), every new binding passes
  dt_binding_check, the board DTS compiles with zero warnings, and the
  kernel config re-derivation is idempotent.  Details and citations:
  04-validation.md section 9.  [V for the recorded commands/results]

## What a reviewer should NOT expect

- **No boot, no hardware validation, no Tested-by.**  Nothing here has run
  on the router; no driver has probed on silicon; the platform has never
  booted a mainline kernel.  The compile evidence proves buildability and
  interface conformance only.
- **Not a working network port.**  GEMAC (0009) is a binding/resource half
  that refuses to probe; PIE (0015) and the internal switch (0016) are
  scaffolding that refuse to probe; the switch data path and the GEMAC
  TX/RX DMA model do not exist yet.
- **Not a working storage/PCIe/restart/selector claim.**  The FMC
  (0006), PCIe host (0013), restart (0010) and A/B selector (0017) are RFC
  or provisional: hardware semantics are unmeasured.  None of them has
  been validated on this board.
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
| drivers ready for review (CRG, watchdog, MDIO, PHY, thermal, CRG polarity fix) | 0004, 0005, 0007, 0008, 0012, 0014 | ready-for-review (still no hardware run) |
| drivers RFC (FMC, pinctrl, PCIe, selector) | 0006, 0011, 0013, 0017 | RFC |
| partial/refusing drafts (GEMAC, restart, PIE, LSW) | 0009, 0010, 0015, 0016 | do not merge; RFC/design review only |

The recommended real submission splits this into per-subsystem series and
folds 0014 into 0004; see 00-cover-letter.md "Recommended submission
sequence".

## Decisions required from the owner before sending

1. Real legal name for `Signed-off-by` (the repo config identity is used
   as a placeholder in the messages and in the 13 binding files).
2. Approve the MAINTAINERS entry and submit it as its own patch.
3. Split bindings out of the driver patches (mechanical).
4. Fold 0014 into 0004; regenerate 0008 against mainline; consider
   renaming LUOFU_FMC to SPI_LUOFU_FMC and normalising the Kconfig gating.
5. Fix the checkpatch review items listed in 04-validation.md (line wrap,
   help text, spinlock/alignment comments) when the patches are touched
   for the split; they are deliberately not changed here because this task
   must not edit the existing patches.

## Provenance and policy

- Source-tree patches (unchanged): `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/001-017`.
- This directory was created for task-29; it modifies no existing patch,
  target-skeleton document or shared doc.  `series/` is generated, never
  hand-edited; the mapping table in 01-commit-messages.md ties every file
  back to its source patch.
- Evidence grading used across the project: [V] verified from an artifact
  or measurement, [I] inferred/implementation, [U] unmeasured,
  [X] blocked.  All hardware behaviour in this directory is [U] except the
  recorded vendor register values that the drivers cite; all claims of a
  working device are [X] for now.
