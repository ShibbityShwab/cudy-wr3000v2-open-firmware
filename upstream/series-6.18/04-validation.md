# Validation record - luofu 6.18 series

Result summary: **PASS** for the host-only checks requested for this
submission form.  The 17 commit-ready series files apply in order to a
pristine linux-6.18.60 tree with both patch(1) and git am, the resulting
trees are identical, and checkpatch reports 0 errors.  There is no
hardware validation of any kind; the build/binding/DTS/config results
recorded earlier are summarised in section 9 and were not re-run here.

Evidence tags: [V] verified by the command/run cited, [I] inferred,
[U] unmeasured, [X] blocked.

## 1. Scope and inputs

- Host: author workstation.  No network access was needed; the kernel
  tarball was already in the local download cache.  No device, UART, TFTP,
  flash, boot or bench tool was used.  [V]
- Kernel input: pristine linux-6.18.60 tarball,
  `<local-download-cache>/linux-6.18.60.tar.xz`, sha256
  `3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`.
  The extracted tree reports VERSION=6, PATCHLEVEL=18, SUBLEVEL=60.  [V]
- Series input: `series/0001-...0017` generated from
  `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/` by
  `tools/gen-series.py`.  No existing patch or target-skeleton document
  was modified by this work.  [V]
- Tool versions: git 2.56.0, GNU patch 2.8.1, GNU tar 1.35, xz 5.8.4,
  checkpatch.pl 0.32 (from the 6.18.60 tree), perl 5.44.  [V]
- Scratch directory (not committed, local-only):
  `build/linux-kbuild/waves/upstream-6.18-series-20261118/` (gitignored
  by the parent repository).  [V]
- The checks below were run by `tools/run-validation.sh` and can be
  reproduced with one command (section 11).

## 2. Series generation and provenance

`tools/gen-series.py` reads each source patch verbatim, converts the bare
unified diff to format-patch style by inserting only the git extended
headers (`diff --git`, `new file mode`), and prepends the per-patch
commit message from `commit-messages/`.  [V]

- diff-body identity: for all 17 patches, removing the inserted
  `diff --git`/`new file mode` lines from the generated diff yields the
  source patch byte-for-byte.  Checked for every file; result
  `ALL_DIFF_BODIES_IDENTICAL=YES`.  [V]
- the generated series carries `[PATCH NN/17]` subjects, the commit
  messages and the `Signed-off-by` placeholder; `git am` reproduces the
  subjects exactly (section 5).  [V]
- hashes of all 17 series files and all 17 source patches are recorded in
  `validation/sha256sums.txt`.  [V]

## 3. Sequential apply with patch(1)

Command: for each series file in order,
`( cd patch-tree && patch -p1 -i <series file> )` on a copy of the
pristine tree.  [V]

- 17/17 patches: RC=0; no failed/reversed hunk; no `.rej` file.  [V]
- 2 hunks applied with an offset, 0 with fuzz.  Both are in patch 0008
  (PHY):
  - `drivers/net/phy/Kconfig`: `Hunk #1 succeeded at 253 (offset -75 lines).`
  - `drivers/net/phy/Makefile`: `Hunk #1 succeeded at 63 (offset -12 lines).`

  Cause [I]: patch 0008 was originally generated against the
  OpenWrt-patched kernel tree (OpenWrt generic/backport PHY patches add
  about 75 lines to that Kconfig and 12 to the Makefile before the
  insertion point), not against kernel.org 6.18.60.  The hunk content
  applies cleanly; the offset is cosmetic and is one of the "regenerate
  before sending" items in the cover letter.  [V for the offsets, I for
  the cause]
- GNU patch created `.orig` backups for exactly those two offset files
  (they were removed before the tree comparison).  [V]
- 65 "patching file" events across the series; no other diagnostics.  [V]

## 4. Sequential apply with git am

Command: `git init` + baseline commit on a second copy of the pristine
tree, then `git am <series file>` in order.  [V]

- 17/17 patches: RC=0; clean work tree afterwards.  [V]
- the 17 commits have the expected subjects (oldest first):

      0001 ARM: mach-hisilicon: add Hi5671Y "luofu" machine support
      0002 ARM: luofu: add ARCH_LUOFU to Kconfig and set its text offset
      0003 ARM: head: materialize TEXT_OFFSET with mov_l
      0004 clk: hisilicon: add Luofu CRG clock and reset controller
      0005 watchdog: luofu: add Hi5671Y watchdog support
      0006 spi: spi-luofu-fmc: add Luofu FMC SPI-NAND controller
      0007 net: mdio: luofu: add MDIO bus controller
      0008 net: phy: add driver for HiSilicon Luofu "Genius" PHYs
      0009 net: ethernet: hisilicon: add partial Luofu GEMAC driver
      0010 power: reset: add Luofu machine restart driver
      0011 pinctrl: hisilicon: add Luofu peripheral pin controller
      0012 thermal: add Luofu thermal sensor driver
      0013 PCI: dwc: add Luofu PCIe host controller
      0014 clk: hisilicon: fix Luofu CRG reset polarity
      0015 net: ethernet: hisilicon: add Luofu PIE packet engine draft
      0016 net: ethernet: hisilicon: add Luofu switch probe skeleton
      0017 soc: hisilicon: add Luofu A/B boot selector

- combined diff of the 17 commits: `59 files changed, 6199 insertions(+),
  4 deletions(-)` (33 files added, 26 modified; no binary, object,
  firmware or DTB file added - checked by extension).  [V]
- commit messages and `Signed-off-by` lines survive `git am` unchanged;
  see `validation/git-log.txt`.  [V]
- no author/committer configuration other than the local scratch identity
  was used; this does not test list delivery.  [V]

## 5. Tree equivalence

`diff -r --brief --exclude=.git patch-tree amtree` (after removing the
patch(1) `.orig` backups): no output, RC=0.  The two application methods
produce identical trees.  [V]

## 6. checkpatch.pl --no-tree

Command: for every series file,
`pristine/scripts/checkpatch.pl --no-tree [--strict] <file>`.  Patch-mode
checks, i.e. the warnings a maintainer would see on the mailing list.  [V]

| # | patch | errors | warnings | checks (strict) |
| --- | --- | --- | --- | --- |
| 01 | 0001-arm-mach-hisilicon-luofu | 0 | 1 | 0 |
| 02 | 0002-arm-kconfig-textofs | 0 | 0 | 0 |
| 03 | 0003-arm-textofs-nonimmediate | 0 | 0 | 0 |
| 04 | 0004-arm-clk-reset-luofu-crg | 0 | 18 | 2 |
| 05 | 0005-arm-watchdog-luofu-wdt | 0 | 2 | 0 |
| 06 | 0006-arm-mtd-nand-luofu-fmc | 0 | 2 | 0 |
| 07 | 0007-net-mdio-luofu | 0 | 3 | 0 |
| 08 | 0008-net-phy-luofu | 0 | 2 | 0 |
| 09 | 0009-net-gmac-luofu | 0 | 2 | 0 |
| 10 | 0010-arm-luofu-restart | 0 | 2 | 0 |
| 11 | 0011-pinctrl-luofu | 0 | 2 | 0 |
| 12 | 0012-arm-thermal-luofu-tvsensor | 0 | 2 | 0 |
| 13 | 0013-arm-pci-luofu-host | 0 | 2 | 0 |
| 14 | 0014-arm-crg-reset-polarity | 0 | 1 | 0 |
| 15 | 0015-net-pie-luofu | 0 | 2 | 0 |
| 16 | 0016-net-lsw-luofu | 0 | 2 | 2 |
| 17 | 0017-soc-luofu-boot-selector | 0 | 3 | 0 |
| **total** | | **0** | **46** | **4** |

Warning breakdown (all expected, all fixable without touching driver
logic; this series was not edited because the task forbids changing the
existing patches):

| warning | count | where | disposition |
| --- | --- | --- | --- |
| added/moved/deleted file(s), does MAINTAINERS need updating? | 14 | every patch that adds files (all but 0002, 0003, 0014) | add the MAINTAINERS entry from 02-maintainers-proposal.md |
| DT binding docs and includes should be a separate patch | 14 | all 12 binding patches; 0004 counts twice (include/dt-bindings header + YAML), 0017 twice | split binding+header into their own patches before sending |
| line length of 112 exceeds 100 columns | 16 | 0004, drivers/clk/hisilicon/crg-luofu.c clock table | wrap the table |
| Kconfig help paragraph shorter than 4 lines | 2 | 0007 (LUOFU_MDIO), 0008 (LUOFU_PHY) | expand the help text |
| spinlock_t definition without comment | 2 (strict CHECK) | 0004, crg-luofu.c | add a one-line comment |
| Alignment should match open parenthesis | 2 (strict CHECK) | 0016, luofu-switch.c:225/257 | re-indent continuation lines |

Why earlier per-lane "checkpatch 0/0/0" results are not contradicted: those
were file-mode runs (checkpatch `-f`, default level) which do not report
the patch-mode checks above.  In file mode today: default `-f` on
crg-luofu.c is 0 errors / 0 warnings; strict `-f` reports the same items as
CHECKs, not WARNINGs.  The patch-mode result above is the one that matters
for review.  [V]

Checkpatch RC is 1 for patches with warnings (by design); the validation
script fails only on ERROR lines, and none exist.  [V]

## 7. What was not run in this validation

Per the task: no kernel rebuild was performed (it is expensive and was
already recorded for the whole series - see section 9), no device was
touched, and no `dt_binding_check`/`dtbs_check` was re-run (already
recorded per binding - section 9).  [V]

The `--use-tree` run of the validation script reuses an already-extracted
tree; the final run for the committed logs extracted the tarball from
scratch and verified its sha256.  [V]

## 8. Results of this validation, graded

[V] - series generated from the source patches with byte-identical hunk
content; 17/17 patch(1) RC=0; 17/17 git am RC=0; identical resulting
trees; 0 checkpatch errors; the four strict CHECKs and 46 warnings are
enumerated above.
[V] - only 0008 has apply offsets; no fuzz; no rejects.
[I] - the offset cause is the OpenWrt-patched source tree (see section 3).
[U] - every hardware behaviour.  No boot, no probe, no register I/O.
[X] - Wi-Fi, internal-switch data path, GEMAC/PIE data path, first-install
path: out of scope or blocked.

## 9. Earlier recorded host-side results (not re-run here)

These are the build/binding/DTS/config results recorded in the repository
before this submission form; they are the "test evidence already
recorded" referenced by the cover letter.  They were produced in isolated
OpenWrt clones (`openwrt main` 27663a0, linux-6.18.60, the tree's own
OpenWrt GCC 14.4.0), not on this validation's pristine tarball.  [V]

| check | result | recorded in |
| --- | --- | --- |
| platform patches 0001-0003: `make target/linux/prepare` and full compile | RC=0; CONFIG_WERROR=y; zImage 3,205,200 B sha256 `45ad00d5...`; _text=stext=0xc0608000; load/entry 0x80608000 | SKELETON-SMOKE.md, third session |
| whole series 0001-0017: prepare | RC=0, 1,198 patching lines, 0 .rej/.orig | SKELETON-SMOKE.md, A/B install session |
| whole series 0001-0017: full kernel compile | RC=0, 0 warnings under CONFIG_WERROR=y; zImage 3,262,472 B sha256 `a4e4f6c0...`; CONFIG_LUOFU_SELECTOR=y; TEXT_OFFSET contract unchanged | SKELETON-SMOKE.md, A/B install session |
| dt_binding_check for every new YAML | RC=0 for CRG, watchdog, FMC, MDIO, GMAC, restart, sysctrl, pinctrl, thermal, PCIe, PIE, switch, boot-selector (dtschema 2026.9; yamllint unavailable - that one step skipped in every lane) | SKELETON-SMOKE.md sessions 5-12 and A/B session |
| board DTS (not part of this series) | cpp + patched-kernel dtc RC=0, zero warnings, DTB 10,664 B sha256 `5726f7cd...`; dt-validate has no diagnostic for the new nodes (pre-existing draft-DTS uart/sp804 TODOs remain) | SKELETON-SMOKE.md, A/B install session |
| kernel config re-derivation | `make kernel_oldconfig` twice: RC=0, byte-identical (sha256 `4106877f...`), 309 symbols identical to the delivered annotated config | SKELETON-SMOKE.md, A/B install session |
| image recipe (OpenWrt target, not kernel submission scope) | `make world` RC=0; reproducible 6,175,022 B sysupgrade container from our DTB/overlay/kernel; later A/B-session container 6,185,262 B; mocked install tests 58/58 | SKELETON-SMOKE.md image-recipe and A/B sessions |
| any hardware run | none.  No boot, no flash, no UART, no TFTP, no bench tool | all sessions; TARGET-DESIGN.md section 9 "explicitly not met" |

Consequence for reviewers: the series compiles into a real 6.18.60 kernel
for this target and its bindings pass dt_binding_check, but no driver has
probed on silicon.  The compile evidence above is the only build evidence;
nothing here claims the image boots.

## 10. Limitations of this validation

1. The pristine tree is kernel.org 6.18.60, while the recorded compile was
   in the OpenWrt-patched tree.  Apply-order and checkpatch results are
   therefore from the strictest (vanilla) baseline; a compile of this
   exact series on vanilla 6.18.60 was not re-run here.
2. checkpatch warns about missing MAINTAINERS/binding-split items by
   design; they are review work, not defects in the code's behaviour.
3. Patch 0008's offsets mean the series is not byte-identical to a
   mainline-generated posting yet; regenerate it before sending.
4. The series is not bisect-clean until 0014 is folded into 0004 (see
   03-rationale-tree-placement.md section 6).
5. No list delivery, no CI, no stable-tree backport was tested.
6. yamllint was not installed in any lane; that one dt_binding_check lint
   step is unexercised everywhere.  [U]

## 11. Reproduction

    # from the repository root
    opensource/upstream/series-6.18/tools/run-validation.sh \
        /path/to/linux-6.18.60.tar.xz /tmp/luofu-series-validation

or, reusing an already extracted 6.18.60 tree:

    opensource/upstream/series-6.18/tools/run-validation.sh --use-tree \
        /path/to/linux-6.18.60 /tmp/luofu-series-validation

The script extracts/copies the tree, applies the series with patch(1) and
git am, checks tree equality, runs checkpatch in both modes, writes logs
to <workdir>/logs/, and exits non-zero on the first failure.  Regenerating
the series itself (after editing a commit message) is:

    cd opensource/upstream/series-6.18
    python3 tools/gen-series.py          # rewrite series/ + 01-commit-messages.md
    python3 tools/gen-series.py --check  # verify no drift

## 12. Evidence index for this directory

`validation/` contains the raw logs of the final run:

- `patch-apply.log` - 17 patch(1) applications with RC lines and the two
  offsets;
- `git-am.log`, `git-am-summary.txt`, `git-log.txt`, `git-status.txt`,
  `git-diffstat.txt` - git am application and resulting 17 commits;
- `tree-equivalence.txt`, `tree-diff.log` - equality of the two trees;
- `checkpatch-default.log`, `checkpatch-default-summary.txt`,
  `checkpatch-strict.log`, `checkpatch-strict-summary.txt`,
  `checkpatch-summary.txt`, `checkpatch-version.txt` - checkpatch runs;
- `offsets.txt`, `version.txt`, `sha256sums.txt` - offsets, kernel version
  and all hashes (series, source patches, tarball);
- `validation/` also records the kernel tarball sha256 in
  `sha256sums.txt`; the tarball itself is not copied into the repository.

Host-specific absolute paths in the committed logs were replaced by
`<scratch>`/`<series>`; no command, result or hash depends on them.

The full run also produced `patch-tree/` and `amtree/` (two ~1.4 GB kernel
trees) inside the gitignored scratch directory; they are disposable.
