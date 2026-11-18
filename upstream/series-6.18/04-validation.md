# Validation record - luofu 6.18 series

Result summary: **PASS** for the host-only checks requested for this
submission form.  The 21 commit-ready series files apply in order to a
pristine linux-6.18.60 tree with both patch(1) and git am, the resulting
trees are identical, and checkpatch reports 0 errors.  The series was
regenerated and every check re-run on 2026-11-18 when the PIE datapath
(0020) and LSW read-only baseline probe (0021) patches were added to the
review form; section 16 records that final 21-patch re-run.  Section 15
records the 19-patch (CRG clock tree) run, section 14 the 18-patch (SMP
0018) run and section 13 the 17-patch A/B-selector (0017) re-validation,
including the kernel compile, binding, DTS and config checks, which are
otherwise summarised in sections 9 and 16.  There is no hardware
validation of any kind.

Evidence tags: [V] verified by the command/run cited, [I] inferred,
[U] unmeasured, [X] blocked.

## 1. Scope and inputs

- Host: author workstation.  No network access was needed; the kernel
  tarball was already in the local download cache.  No device, UART, TFTP,
  flash, boot or bench tool was used.  [V]
- Kernel input: pristine linux-6.18.60 tarball,
  `<local-download-cache>/linux-6.18.60.tar.xz`, sha256
  `3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`
  (re-verified against the local download cache at the start of the final
  run).  The extracted tree reports VERSION=6, PATCHLEVEL=18, SUBLEVEL=60.  [V]
- Series input: `series/0001-...0021` generated from
  `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/` by
  `tools/gen-series.py`.  Patches 0020 (PIE datapath + GEMAC front end)
  and 0021 (LSW read-only baseline probe) were added to the review form on
  2026-11-18 from the two measured-inventory lanes; patch 0019 (CRG
  clock-tree extension: gate parents, read-only PLLs/muxes, extended CRG
  YAML and four appended clock IDs) was added the same day from the
  clock-tree lane; patch 0018 (SMP bring-up: new
  `arch/arm/mach-hisilicon/platsmp.c` plus the machine
  Kconfig/Makefile/comment refactor) had been added earlier the same day,
  and patch 0017 (and its commit message) had been corrected earlier that
  day (low-byte read-modify-write store, evidence-based 0x21 provenance,
  ABI/YAML wording); the other 16 source patches were not changed.  No
  source patch was edited for this review-form update - the series files
  are regenerated from the unchanged sources.  The target's base-files
  (`platform.sh` and `etc/init.d/luofu_record_pair`), the mocked harness
  and the board DTS are not part of the kernel series (the DTS supplies
  the seven input clocks 0019's binding requires, the PIE ring tables from
  the 0020 lane and the 17 switch windows/port bitmap from the 0021
  lane).  [V]
- Tool versions: git 2.56.0, GNU patch 2.8.1, GNU tar 1.35, xz 5.8.4,
  checkpatch.pl 0.32 (from the 6.18.60 tree), perl 5.44.  [V]
- Scratch directory (not committed, local-only):
  `build/linux-kbuild/waves/2026-11-18-0021review/upstream-validation/`
  for the final 21-patch run; the earlier 19-patch run used
  `.../2026-11-18-clkreview/upstream-validation/`, the 18-patch run
  `.../2026-11-18-smpreview/upstream-validation/` and the 17-patch run
  `.../2026-11-18-selectorfix/upstream-validation/`.  All are gitignored
  by the parent repository.  [V]
- The checks below were run by `tools/run-validation.sh` and can be
  reproduced with one command (section 11).

## 2. Series generation and provenance

`tools/gen-series.py` reads each source patch verbatim, converts the bare
unified diff to format-patch style by inserting only the git extended
headers (`diff --git`, `new file mode`), and prepends the per-patch
commit message from `commit-messages/`.  [V]

- diff-body identity: for all 21 patches, removing the inserted
  `diff --git`/`new file mode` lines from the generated diff yields the
  source patch byte-for-byte.  Checked for every file by the generator;
  independently re-checked for 0018 (body identical to
  `018-arm-mach-hisilicon-smp.patch`), 0019 (body identical to
  `019-arm-clk-luofu-clock-tree.patch`), 0020 (body identical to
  `020-net-pie-datapath-luofu.patch`) and 0021 (body identical to
  `021-net-lsw-init-luofu.patch`).  [V]
- the generated series carries `[PATCH NN/21]` subjects, the commit
  messages and the `Signed-off-by` placeholder; `git am` reproduces the
  subjects exactly (section 4).  [V]
- hashes of all 21 series files and all 21 source patches are recorded in
  `validation/sha256sums.txt`; `sha256sum -c` passes for all 42 entries
  in the final run.  [V]

## 3. Sequential apply with patch(1)

Command: for each series file in order,
`( cd patch-tree && patch -p1 -i <series file> )` on a copy of the
pristine tree.  [V]

- 21/21 patches: RC=0; no failed/reversed hunk; no `.rej` file.  [V]
- 3 hunks applied with an offset, 0 with fuzz.  Two are in patch 0008
  (PHY) and are unchanged from the earlier runs:
  - `drivers/net/phy/Kconfig`: `Hunk #1 succeeded at 253 (offset -75 lines).`
  - `drivers/net/phy/Makefile`: `Hunk #1 succeeded at 63 (offset -12 lines).`

  Cause [I]: patch 0008 was originally generated against the
  OpenWrt-patched kernel tree (OpenWrt generic/backport PHY patches add
  about 75 lines to that Kconfig and 12 to the Makefile before the
  insertion point), not against kernel.org 6.18.60.  The hunk content
  applies cleanly; the offset is cosmetic and is one of the "regenerate
  before sending" items in the cover letter.  [V for the offsets, I for
  the cause]
- The third offset is new in this run: patch 0021's Kconfig hunk,
  `Hunk #1 succeeded at 188 (offset 6 lines).` in
  `drivers/net/ethernet/hisilicon/Kconfig`.  Patch 0020 edits the same
  file earlier in the series, so the insertion point has moved by six
  lines; the hunk content still applies cleanly.  [V]
- GNU patch created `.orig` backups for exactly those three offset files
  (they were removed before the tree comparison).  [V]
- 82 "patching file" events across the series; no other diagnostics.
  Patch 0018 applies four files at its position (three modified machine
  files plus the new platsmp.c), all RC=0.  Patch 0019 applies its three
  files (the CRG YAML binding, crg-luofu.c and the clock-ID header), all
  RC=0 and with no offset.  Patch 0020 applies its six files (the
  ethernet Kconfig, luofu-pie.c, the new luofu-pie.h, luofu-gmac.c and
  the two net YAML bindings) RC=0 and with no offset.  Patch 0021 applies
  its four files (the same Kconfig, the new luofu-switch-regs.h,
  luofu-switch.c and the switch YAML) RC=0 with the 6-line Kconfig offset
  above.  [V]

## 4. Sequential apply with git am

Command: `git init` + baseline commit on a second copy of the pristine
tree, then `git am <series file>` in order.  [V]

- 21/21 patches: RC=0; clean work tree afterwards.  [V]
- the 21 commits have the expected subjects (oldest first):

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
      0018 ARM: mach-hisilicon: add Hi5671Y SMP bring-up support
      0019 clk: hisilicon: add the Luofu CRG clock tree
      0020 net: ethernet: hisilicon: add the Luofu PIE datapath and GEMAC front end
      0021 net: ethernet: hisilicon: add the Luofu LSW read-only baseline probe

- combined diff of the 21 commits: `62 files changed, 8818 insertions(+),
  4 deletions(-)` (36 files added, 26 modified; no binary, object,
  firmware or DTB file added - checked by extension).  Patch 0018 itself is
  4 files / 169 insertions(+) / 8 deletions(-): the 159-line platsmp.c plus
  the Kconfig, Makefile and machine-comment refactor.  Patch 0019 itself is
  3 files / 697 insertions(+) / 96 deletions(-): it modifies the CRG
  driver, the CRG YAML binding and the clock-ID header, and every line it
  deletes was added by an earlier series patch, so the combined file counts
  are unchanged and the combined insertion count grew by 601 (6424 ->
  7025), not by 697.  Patch 0020 itself is 6 files / 1443 insertions(+) /
  393 deletions(-): it adds luofu-pie.h and rewrites luofu-pie.c,
  luofu-gmac.c and their bindings.  Patch 0021 itself is 4 files / 966
  insertions(+) / 223 deletions(-): it adds luofu-switch-regs.h and
  rewrites luofu-switch.c and its binding.  Because both rewrite lines that
  earlier series patches added, the combined file count grows by exactly
  the two new files (60 -> 62) while the combined insertion count grows by
  1793 (7025 -> 8818), not by 2409.  [V]
- commit messages and `Signed-off-by` lines survive `git am` unchanged;
  see `validation/git-log.txt` (the script keeps the last 20 oneline log
  lines by design).  [V]
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
| 18 | 0018-arm-mach-hisilicon-smp | 0 | 1 | 0 |
| 19 | 0019-arm-clk-luofu-clock-tree | 0 | 2 | 2 |
| 20 | 0020-net-pie-datapath-luofu | 0 | 2 | 0 |
| 21 | 0021-net-lsw-init-luofu | 0 | 2 | 0 |
| **total** | | **0** | **53** | **6** |

Warning breakdown (all expected, all fixable without touching driver
logic; this series was not edited because the task forbids changing the
existing patches):

| warning | count | where | disposition |
| --- | --- | --- | --- |
| added/moved/deleted file(s), does MAINTAINERS need updating? | 17 | every patch that adds files (all but 0002, 0003, 0014, 0019); 0020 adds luofu-pie.h and 0021 adds luofu-switch-regs.h | add the MAINTAINERS entry from 02-maintainers-proposal.md |
| DT binding docs and includes should be a separate patch | 18 | 16 patches that mix binding (Documentation/devicetree or include/dt-bindings) and driver files; 0017 and 0019 cross the boundary twice each; 0020 and 0021 edit existing bindings | split binding+header into their own patches before sending |
| line length of 112 exceeds 100 columns | 16 | 0004, drivers/clk/hisilicon/crg-luofu.c clock table | wrap the table |
| Kconfig help paragraph shorter than 4 lines | 2 | 0007 (LUOFU_MDIO), 0008 (LUOFU_PHY) | expand the help text |
| spinlock_t definition without comment | 2 + 2 (strict CHECK) | 0004 and 0019, crg-luofu.c structs | add a one-line comment |
| Alignment should match open parenthesis | 2 (strict CHECK) | 0016, luofu-switch.c:225/257 | re-indent continuation lines |

Why earlier per-lane "checkpatch 0/0/0" results are not contradicted: those
were file-mode runs (checkpatch `-f`, default level) which do not report
the patch-mode checks above.  In file mode today: default `-f` on
crg-luofu.c is 0 errors / 0 warnings; strict `-f` reports the same items as
CHECKs, not WARNINGs.  The patch-mode result above is the one that matters
for review.  [V]

Checkpatch RC is 1 for patches with warnings (by design); the validation
script fails only on ERROR lines, and none exist.  Patch 0017 is 436
checked lines (it grew with the corrected comments/ABI and no new
warnings).  Patch 0018 is 189 checked lines (new platsmp.c plus three small
modifications) with the single expected MAINTAINERS warning, 0 errors and
0 strict CHECKs.  Patch 0019 is 931 checked lines (the extended
crg-luofu.c plus its YAML and clock-ID header) with 2 binding-split
warnings, 0 errors and 2 strict `spinlock_t` CHECKs (it rewrites the two
structs and re-adds their lock declarations).  Patch 0020 is 2111 checked
lines (the revived PIE engine, the new header, the GEMAC front end and
their two YAML bindings) with 0 errors, the expected MAINTAINERS and
binding-split warnings, and no strict CHECKs.  Patch 0021 is 1456 checked
lines (the read-only LSW probe, the new register header, the switch
binding and the Kconfig hunk) with the same 2 expected warnings and no
strict CHECKs.  The 21-patch totals are therefore the 19-patch totals plus
4 warnings (2 per new patch: one MAINTAINERS for the new header file and
one binding split) and no new CHECKs.  [V]

## 7. What was not run in the checkpatch/apply validation

No device was touched.  The kernel rebuild, `dt_binding_check`, DTS
compile and config re-derivation are not part of `tools/run-validation.sh`;
they were re-run separately in the isolated clones per lane and are
recorded in section 13 (selector correction), section 14 (SMP addition),
section 15 (clock-tree addition) and section 16 (PIE datapath + LSW
baseline probe addition); the earlier per-lane records are in section 9.  [V]

The `--use-tree` run of the validation script reuses an already-extracted
tree; the final run for the committed logs extracted the tarball from
scratch and verified its sha256.  [V]

## 8. Results of this validation, graded

[V] - series generated from the source patches with byte-identical hunk
content; 21/21 patch(1) RC=0; 21/21 git am RC=0; identical resulting
trees; 0 checkpatch errors; the six strict CHECKs and 53 warnings are
enumerated above.
[V] - three apply offsets (two in 0008, one in 0021); no fuzz; no
rejects.
[I] - the offset cause is the OpenWrt-patched source tree (see section 3).
[U] - every hardware behaviour, including the decoded clock-tree PLL
rates, status bits and mux/gate parents (never read back).  No boot, no
probe, no register I/O.
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
| whole series 0001-0018: prepare and full kernel compile (SMP lane, 2026-11-18) | prepare RC=0 (1,202 patching lines, 0 .rej/.orig); compile RC=0, no compiler warning attributable to a new or changed file (38 pre-existing modpost MODULE_DESCRIPTION warnings); zImage 3,264,472 B sha256 `ae59d998...`; platsmp.o built, `luofu_smp_*` and `__cpu_method_of_table_luofu_smp` in System.map; CONFIG_SMP=y, CONFIG_HAVE_ARM_SCU=y, CONFIG_HAVE_ARM_TWD=y; config re-derivation byte-idempotent; TEXT_OFFSET contract unchanged | SKELETON-SMOKE.md (SMP session), target-skeleton/docs/port-notes/smp/SUMMARY.md |
| whole series 0001-0019: prepare and full kernel compile (clock-tree lane, 2026-11-18) | prepare RC=0 (patch 019 applied, no `.rej`/`.orig`); compile RC=0, **0 warnings and 0 errors** under CONFIG_WERROR=y; zImage 3,264,560 B sha256 `4456b0ec...`; `drivers/clk/hisilicon/crg-luofu.o` 43,900 B with `luofu_crg_pll_recalc_rate`/`luofu_crg_mux_get_parent` in the object/System.map; CONFIG_LUOFU_CRG=y, CONFIG_COMMON_CLK=y; no new Kconfig symbol (config-6.18 re-derivation byte-idempotent `4106877f...`, kernel `.config` byte-identical to the 001-0018 lane); TEXT_OFFSET contract unchanged | SKELETON-SMOKE.md (clock-tree session), target-skeleton/docs/port-notes/clk/SUMMARY.md |
| whole series 0001-0020: prepare and full kernel compile (PIE-datapath lane, 2026-11-18) | 001-020 chain apply RC=0 (0 rejects; the only .orig files are patch 0008's two pre-existing offset files); prepare RC=0 (1,211 patching-file lines); compile RC=0, **0 warnings and 0 errors** under CONFIG_WERROR=y; zImage 3,276,656 B sha256 `dfc7b753d34ad443a76ed3ffe60e77d7e63a28b771546ba0dccc4a40a9515a5a` (build-time dependent); `luofu-pie.o` 76,740 B and `luofu-gmac.o` 45,492 B built; PIE/GEMAC/phylink symbols in System.map; CONFIG_LUOFU_PIE=y, CONFIG_LUOFU_GMAC=y, CONFIG_PHYLINK=y (resolved m -> y), CONFIG_WERROR=y; config delta symbol-identical and byte-idempotent; TEXT_OFFSET contract unchanged | target-skeleton/docs/port-notes/pie/SUMMARY.md sections 4 and 5 |
| whole series 0001-0021: prepare and full kernel compile (LSW-init lane, 2026-11-18) | the 21-patch series applied to pristine 6.18.60 (0 failed hunks, 0 rejects; 0021 has the 6-line Kconfig offset only); prepare RC=0 (1,215 patching lines, 0 rejects); compile RC=0 with CONFIG_WERROR=y, **0 errors and 0 warnings**; zImage 3,279,096 B sha256 `96b927d35e2e6c6be9980c7683de20c20a288a5df58ddf08ed7fbbe9bb1e92d3` (build-time dependent); `luofu-switch.o` built; CONFIG_LUOFU_SWITCH=y; config delta symbol-identical and byte-idempotent (`76aaafc8...`); TEXT_OFFSET contract unchanged | target-skeleton/docs/port-notes/lsw/SUMMARY.md sections 4.1 and 6 |
| dt_binding_check for every new YAML | RC=0 for CRG, watchdog, FMC, MDIO, GMAC, restart, sysctrl, pinctrl, thermal, PCIe, PIE, switch, boot-selector (dtschema 2026.9; yamllint unavailable - that one step skipped in every lane); 0018 adds no binding and 0019 no new YAML file (its CRG YAML extension re-checked RC=0 in the clock-tree lane); the patch-020 (PIE/GMAC) and patch-021 (switch) binding extensions were re-checked RC=0 in their lanes, with both examples `DTEX`/`DTC [C]` clean | SKELETON-SMOKE.md sessions 5-12, A/B, SMP, clock-tree, PIE-datapath and LSW-init sessions |
| board DTS (not part of this series) | cpp + patched-kernel dtc RC=0, zero warnings; 10,664 B sha256 `5726f7cd...` without the SMP line, 10,710 B sha256 `bc599a4e...` with the enable-method line (SMP lane), 11,346 B sha256 `a31ab53f...` with the seven CRG fixed clocks and the SMP line (clock-tree lane), 11,430 B sha256 `d3a9dd86...` with the PIE ring tables (PIE-datapath lane), 11,516 B sha256 `43c1e94b...` with the switch windows and port bitmap (LSW-init lane); dt-validate has no diagnostic for the new nodes, rings, clocks or switch node (pre-existing draft-DTS uart/sp804 TODOs remain) | SKELETON-SMOKE.md, A/B, SMP, clock-tree, PIE-datapath and LSW-init sessions |
| kernel config re-derivation | `make kernel_oldconfig` twice: RC=0, byte-identical (sha256 `4106877f...`), 309 symbols identical to the delivered annotated config; the 001-0019 re-derivation is byte-idempotent at the same hash with no new symbol; the 001-0020 delta changes only CONFIG_PHYLINK m -> y; patch 0021 adds no Kconfig symbol and the 001-0021 re-derivation stayed symbol-identical and byte-idempotent (`76aaafc8...`) | SKELETON-SMOKE.md, A/B install, clock-tree, PIE-datapath and LSW-init sessions |
| image recipe (OpenWrt target, not kernel submission scope) | `make world` RC=0; reproducible 6,175,022 B sysupgrade container from our DTB/overlay/kernel; later A/B-session container 6,185,262 B; mocked install tests 58/58 | SKELETON-SMOKE.md image-recipe and A/B sessions |
| any hardware run | none.  No boot, no flash, no UART, no TFTP, no bench tool | all sessions; TARGET-DESIGN.md section 9 "explicitly not met" |

No kernel rebuild was performed for the 0020/0021 review-form update; the
001-0020 and 001-0021 build evidence is the two lanes' recorded results
(rows added above; the review form adds commit messages and commit-ready
copies, not source changes).  The earlier 001-0019 result is the
clock-tree lane's record, 001-0018 the SMP lane's.
Consequence for reviewers: the series compiles into a real 6.18.60 kernel
for this target and its bindings pass dt_binding_check, but no driver has
probed on silicon, CPU1 has never been released, no clock rate or PLL or
mux parent has been read back, the PIE engine has never been armed and the
LSW write campaign has never run.  The compile evidence above is the only
build evidence; nothing here claims the image boots.

## 10. Limitations of this validation

1. The pristine tree is kernel.org 6.18.60, while the recorded compile was
   in the OpenWrt-patched tree.  Apply-order and checkpatch results are
   therefore from the strictest (vanilla) baseline; a compile of this
   exact series on vanilla 6.18.60 was not re-run here.
2. checkpatch warns about missing MAINTAINERS/binding-split items by
   design; they are review work, not defects in the code's behaviour.
3. Patch 0008's offsets mean the series is not byte-identical to a
   mainline-generated posting yet; regenerate it before sending.
4. **The SMP release is host-only and therefore unproven.** Patch 0018 is
   the decode of the vendor CPU1 release; it has never executed on our
   kernel, the pen-word consumer, the CRG resume-bit semantics and the
   SCU pre-state are unmeasured (SMP-RE.md sections 7 and 10), and the
   devicetree enable-method is not part of this kernel series (board DTS),
   so the review-form series alone leaves the platform UP.  Posting it is
   gated on the attended S2 bring-up (SMP-RE.md section 8), not on this
   host validation.
5. The series is not bisect-clean until 0014 is folded into 0004 (see
   03-rationale-tree-placement.md section 6).
6. No list delivery, no CI, no stable-tree backport was tested.
7. yamllint was not installed in any lane; that one dt_binding_check lint
   step is unexercised everywhere.  [U]
8. **The CRG clock tree (0019) is decoded, not measured.**  The PLL rate
   equation, field layout, mux parents and gate parents are decoded from
   the vendor hi_clk.ko and the vendor devicetree; the boot-programmed
   control words at 0x198/0x1e0, the 0x90 status bits, the 0x138 mux
   parents and every derived rate have never been read back on this board.
   The driver is read-only by design (no set_rate, no enable/disable,
   set_parent returns -EOPNOTSUPP), so this is a description-accuracy
   question, not a write risk.  The binding now requires seven input
   clocks; the board DTS that provides them is not in this series.
9. **The PIE datapath (0020) is host-compiled only and has never been
   armed.**  The coherent rings, IRQ/NAPI scaffolding and TX producer are
   compiled, but no PIE or GEMAC register has been written by this code,
   the arm gate (kernel command line `luofu_pie.arm=1`) has never been
   passed, and both devicetree nodes stay disabled.  The RX completion
   status/length decode is [U] and no frame is delivered to the stack or
   reaches a wire (the LSW egress map does not exist).  The arm-gated
   write path is the first PIE MMIO write in the project and needs the
   attended, recovery-armed session in port-notes/pie/SUMMARY.md
   section 6.
10. **The LSW baseline probe (0021) is read-only and its baseline is one
    measured boot.**  The driver never writes a register, enables no
    clock, drives no reset and always refuses probe; the 30 baseline words
    and 384 qid-map words are the measured stock-vendor state of
    2026-11-18, so the checks are fail-closed by construction rather than
    a general model.  A second boot/unit inventory is owed, and the
    attended W2-W6 write experiments have not run
    (port-notes/lsw/SUMMARY.md section 5, LSW-SPEC.md section 12.3).

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

- `patch-apply.log` - 21 patch(1) applications with RC lines and the
  three offsets;
- `git-am.log`, `git-am-summary.txt`, `git-log.txt`, `git-status.txt`,
  `git-diffstat.txt` - git am application and resulting 21 commits
  (`git-log.txt` keeps the script's last 20 oneline entries);
- `tarball.txt` - the kernel tarball sha256 checked at the start of the
  run (the host download path is scrubbed to `<local-download-cache>`);
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

## 13. Selector-correction re-validation (2026-11-18, shared task-31)

The A/B selector driver (0017) and the target's base-files were corrected
against the local evidence reconstruction `opensource/docs/SELECTOR-0x21.md`:
0x21 is the project's bench handover value and not a vendor value; the
bootloader selects the pair from its env `bootflag` and the SYSCTRL cell
is its record; and the store is now the loader-style low-byte
read-modify-write (upper 24 bits preserved) with a whole-cell read-back.
Because the source patch changed, the series was regenerated and every check
in this document was re-run; this run superseded the earlier 17-patch
record for the 0017 correction.  The apply/checkpatch numbers quoted below
are that 17-patch run; section 14 is the 18-patch run, section 15 the
19-patch run and section 16 the final 21-patch run.
(Build/binding/DTS/config results here remain current.)  [V]

- series 0017 sha256
  `c58107a58f0618956fd5f73ec36c41bd89a0443e1167869f783a4a975ee37566`
  (the hash changed from the 17-patch run `1bac7bdd...` only because the
  `[PATCH NN/17]` subject denominator became `NN/18` in the final
  regeneration; the diff body and the source patch sha256
  `c991aaa50419f4b6ba0c9a37018a682c0a7a5afc66d43c611c947af25cce0f1d` are
  unchanged); commit message rewritten; `tools/gen-series.py --check`
  clean.  [V]
- sequential patch(1): 17/17 RC=0; git am: 17/17 RC=0; resulting trees
  identical; checkpatch default and strict: 0 errors / 46 warnings (0017
  still 3 - the MAINTAINERS and binding-split review items).  [V]
- isolated kernel clone (a reflink copy of the A/B-install lane's clone):
  `make target/linux/prepare V=s` RC=0 (1,198 patching lines, no
  `.rej`/`.orig` in the kernel tree); `make target/linux/compile V=s
  -j16` RC=0, **0 warnings and 0 errors** under `CONFIG_WERROR=y`;
  zImage 3,265,056 B sha256
  `5b417ce976d8bc7fb90f34056e3a3da02f271f1613fb7945ec54d99639839bb1`;
  `CONFIG_LUOFU_SELECTOR=y` built in; `_text = stext = 0xc0608000` and
  `DTEXT_OFFSET=0x00608000` (TEXT_OFFSET contract unchanged).  [V]
- boot-selector YAML `dt_binding_check` RC=0; board DTS cpp + kernel dtc
  RC=0 with zero warnings (DTB 10,664 B sha256 `5726f7cd...`, unchanged
  because only comments in the DTS source change, delivered via the incoming
  shared-doc patch); `dt-validate` RC=0 and no boot-selector diagnostic.  [V]
- config canonical re-derivation, two runs: RC=0/RC=0, byte-identical
  (`4106877fdb6dad7ee9d5d2895ed6eb878da0c6a70ff4067d664be3da380a2a1f`),
  309/309 symbols identical to the delivered delta.  [V]
- checkpatch file-mode: 0 errors / 0 warnings on the corrected driver, YAML
  and ABI doc.  [V]
- mocked sysupgrade harness (install path + boot-time recorder): 94/94
  fixture assertions and 100/100 with the real 6,185,262 B container
  (sha256 `e869f105...`).  [V]
- image build in a second isolated clone with the corrected target and the
  recorder: `make world -j16` RC=0 (0 warnings, 0 errors) produced a
  6,185,262 B container sha256
  `362ca4ece4df2b77b0e527ad5d948bbd997201ccb3bf6e2518d5ddaccbb59899`;
  the rootfs carries `/etc/init.d/luofu_record_pair` (mode 0755, byte-identical
  to the repository file) and the enabled `/etc/rc.d/S96luofu_record_pair`
  symlink; `tools/luofu-image.py check-container` PASS; the mocked harness
  passes 100/100 against this newly built container.  [V]
- the boot-time pair recorder (target base-files, not this kernel series) is
  enabled in the built rootfs and behaviourally covered by the mocked tests;
  it has never run on hardware.  [V]/[U]

Everything hardware-related remains [U]: no boot, no flash, no UART, no
device.  The env re-stamp, the cell's upper 24 bits and the first boot of a
written pair still need the attended A/B validation.

## 14. Final 18-patch re-validation (2026-11-18, shared task-34)

The SMP bring-up patch 018 was added to the review form: its source patch
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/018-arm-mach-hisilicon-smp.patch`
(sha256 `5509ca597852143a22040d8421cd794397f8f85fee841d611de68e53cca5e00b`)
is included **unchanged**, `commit-messages/0018-arm-mach-hisilicon-smp.txt`
was written, `tools/gen-series.py` learned the new series entry and group,
and the series/ files plus 01-commit-messages.md were regenerated.  The
only byte change to series files 0001-0017 is the subject denominator
`[PATCH NN/17]` -> `[PATCH NN/18]`; their diff bodies and their source
patches are unchanged.  No source patch, target-skeleton file or shared doc
was edited.  [V]

Commands (run from `opensource/upstream/series-6.18`; host paths
scrubbed):

    python3 tools/gen-series.py
    python3 tools/gen-series.py --check        # series up to date (18 patches), RC=0
    tools/run-validation.sh /path/to/linux-6.18.60.tar.xz <scratch>/upstream-validation

The tarball was hash-checked at the start of the run
(`3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`)
and the extracted tree reports 6.18.60.  The script exited 0
("PASS: all validation steps completed").  [V]

| check | result |
| --- | --- |
| sequential patch(1) | **18/18 RC=0**, 69 "patching file" events, 0 failed/reversed hunks, 0 `.rej`; patch 0018 is the 18th and applies its four files cleanly |
| offsets/fuzz | 2 hunks with offset (both patch 0008: `Kconfig` -75, `Makefile` -12), 0 with fuzz - unchanged from the 17-patch run |
| sequential git am | **18/18 RC=0**, clean work tree; 18 commits with the expected subjects including `0018 ARM: mach-hisilicon: add Hi5671Y SMP bring-up support` |
| combined diff | `60 files changed, 6424 insertions(+), 4 deletions(-)`; 34 files added, 26 modified; patch 0018 alone is 4 files / +169 / -8 |
| tree equivalence | `diff -r --brief --exclude=.git` RC=0, no output: the patch(1) and git am trees are identical |
| checkpatch default and strict | 0 errors, **47 warnings** (46 previous + 1 new MAINTAINERS warning in 0018), 4 strict CHECKs (0004 x2, 0016 x2); patch 0018 alone: 0 errors, 1 warning, 0 CHECKs, 189 lines |
| hashes | `sha256sum -c validation/sha256sums.txt`: 36/36 OK (18 series + 18 source patches); series 0018 `b4268e65d5e3d7ad4aa7bc2c9a733234649484b904041b46d77c92f621091e75`, source 0018 `5509ca59...`; tarball hash recorded in the same file |
| kernel build evidence | not re-run here (no rebuild needed): the SMP lane's recorded 001-0018 result is prepare RC=0 (1,202 patching lines), compile RC=0 with zImage 3,264,472 B sha256 `ae59d998...`, platsmp.o built, config re-derivation byte-idempotent; see section 9 and `target-skeleton/docs/port-notes/smp/SUMMARY.md` [V] |
| hardware | **none**.  No boot, no CPU1 release, no UART/TFTP/flash, no device.  The SMP release stays [U] until the attended S2 bring-up. |

The raw logs for this run are the refreshed files under `validation/`
(scrubbed of host-specific paths); `validation/sha256sums.txt` was
regenerated and re-verified.  [V]

This run is superseded by section 15: when 0019 was added, every
0001-0018 series file changed again only in the subject denominator
(`NN/18` -> `NN/19`) with unchanged diff bodies and source patches, and
the series hashes quoted in this section are the 18-patch form.

## 15. Final 19-patch re-validation (2026-11-18, shared task-37)

Patch 0019 (the CRG clock-tree extension) was added to the review form: its
source patch
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/019-arm-clk-luofu-clock-tree.patch`
(sha256 `9d86498b405e43cc05b77b46b5ce74d4dfa5256a3dd6f444690d85e0cbc0513c`)
is included **unchanged**,
`commit-messages/0019-arm-clk-luofu-clock-tree.txt` was written,
`tools/gen-series.py` learned the new series entry and group, and the
series/ files plus 01-commit-messages.md were regenerated.  The only byte
change to series files 0001-0018 is the subject denominator
`[PATCH NN/18]` -> `[PATCH NN/19]`; their diff bodies and their source
patches are unchanged.  No source patch, target-skeleton file or shared doc
was edited.  [V]

Commands (run from `opensource/upstream/series-6.18`; host paths
scrubbed):

    python3 tools/gen-series.py
    python3 tools/gen-series.py --check        # series up to date (19 patches), RC=0
    tools/run-validation.sh /path/to/linux-6.18.60.tar.xz <scratch>/upstream-validation

The tarball was hash-checked at the start of the run
(`3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`)
and the extracted tree reports 6.18.60.  The script exited 0
("PASS: all validation steps completed").  [V]

| check | result |
| --- | --- |
| sequential patch(1) | **19/19 RC=0**, 72 "patching file" events, 0 failed/reversed hunks, 0 `.rej`; patch 0019 is the 19th and applies its three files cleanly (CRG YAML, crg-luofu.c, clock-ID header) |
| offsets/fuzz | 2 hunks with offset (both patch 0008: `Kconfig` -75, `Makefile` -12), 0 with fuzz - unchanged from the 18-patch run; patch 0019 has no offset |
| sequential git am | **19/19 RC=0**, clean work tree; 19 commits with the expected subjects including `0019 clk: hisilicon: add the Luofu CRG clock tree` |
| combined diff | `60 files changed, 7025 insertions(+), 4 deletions(-)`; still 34 files added, 26 modified; patch 0019 alone is 3 files / +697 / -96 (all 96 deleted lines were added by earlier series patches, so the combined file counts are unchanged) |
| tree equivalence | `diff -r --brief --exclude=.git` RC=0, no output: the patch(1) and git am trees are identical |
| checkpatch default and strict | 0 errors, **49 warnings** (47 previous + 2 new binding-split warnings in 0019), 6 strict CHECKs (0004 x2, 0016 x2, 0019 x2); patch 0019 alone: 0 errors, 2 warnings (both `DT binding docs ... separate patch`), 2 CHECKs (`spinlock_t definition without comment` on the two struct fields it re-adds), 931 lines |
| hashes | `sha256sum -c validation/sha256sums.txt`: 38/38 OK (19 series + 19 source patches); series 0019 `9d9a89799f6398cc61c8e2bc4e23aa53c723c7d31ba18d2bb00fe4b76fe0ea5d`, source 0019 `9d86498b...`; tarball hash recorded in the same file and in `validation/tarball.txt` |
| kernel build evidence | not re-run here (no rebuild needed): the clock-tree lane's recorded 001-0019 result is prepare RC=0 (patch 019 applied, no rej/orig), compile RC=0 with 0 warnings/0 errors under CONFIG_WERROR=y, zImage 3,264,560 B sha256 `4456b0ec...`, `crg-luofu.o` built, no new config symbol (config re-derivation byte-idempotent), TEXT_OFFSET unchanged; see section 9 and `target-skeleton/docs/port-notes/clk/SUMMARY.md` [V] |
| hardware | **none**.  No boot, no clock/PLL/mux readback, no UART/TFTP/flash, no device.  The decoded clock-tree values stay [U] until the attended read-only readback. |

The raw logs for this run are the refreshed files under `validation/`
(scrubbed of host-specific paths); `validation/sha256sums.txt` was
regenerated and re-verified.  [V]

This run is superseded by section 16: when 0020/0021 were added, every
0001-0019 series file changed again only in the subject denominator
(`NN/19` -> `NN/21`) with unchanged diff bodies and source patches, and
the series hashes quoted in this section are the 19-patch form.

## 16. Final 21-patch re-validation (2026-11-18, shared task-48)

Patches 0020 (PIE datapath + GEMAC front end) and 0021 (LSW read-only
baseline probe) were added to the review form: their source patches
`opensource/target-skeleton/target/linux/hisilicon/patches-6.18/020-net-pie-datapath-luofu.patch`
(sha256 `806a113c9dc581ccb3d9248f1298f4cf8a48e1645228b525b1a4853f70bfa6e8`)
and `.../021-net-lsw-init-luofu.patch` (sha256
`de69296d80f2419b574d3af4ec4a8b83c877a711d278fc71350d14a231fe307e`)
are included **unchanged**, `commit-messages/0020-net-pie-datapath-luofu.txt`
and `commit-messages/0021-net-lsw-init-luofu.txt` were written,
`tools/gen-series.py` learned the two series entries and their statuses
(both in the "RFC / partial drafts, do not merge" group) and extended its
status legend, and the series/ files plus 01-commit-messages.md were
regenerated.  The only byte change to series files 0001-0019 is the subject
denominator `[PATCH NN/19]` -> `[PATCH NN/21]`; their diff bodies and their
source patches are unchanged.  No source patch, target-skeleton file or
shared doc was edited.  [V]

Commands (run from `opensource/upstream/series-6.18`; host paths
scrubbed):

    python3 tools/gen-series.py
    python3 tools/gen-series.py --check        # series up to date (21 patches), RC=0
    tools/run-validation.sh /path/to/linux-6.18.60.tar.xz <scratch>/upstream-validation

The tarball was hash-checked at the start of the run
(`3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e`)
and the extracted tree reports 6.18.60.  The script exited 0
("PASS: all validation steps completed"), and the committed logs,
`validation/sha256sums.txt` and `validation/tarball.txt` were refreshed
from that run (host paths scrubbed to `<scratch>`/`<series>`/
`<local-download-cache>`).  [V]

| check | result |
| --- | --- |
| sequential patch(1) | **21/21 RC=0**, 82 "patching file" events, 0 failed/reversed hunks, 0 `.rej`; patch 0020 is the 20th and applies its six files cleanly; patch 0021 is the 21st and applies its four files cleanly (the only diagnostic is the 6-line Kconfig offset, because 0020 edits the same file first) |
| offsets/fuzz | 3 hunks with offset, 0 with fuzz: patch 0008 `drivers/net/phy/Kconfig` -75 and `drivers/net/phy/Makefile` -12 (pre-existing, unchanged), patch 0021 `drivers/net/ethernet/hisilicon/Kconfig` +6 (new) |
| sequential git am | **21/21 RC=0**, clean work tree; 21 commits with the expected subjects including `0020 net: ethernet: hisilicon: add the Luofu PIE datapath and GEMAC front end` and `0021 net: ethernet: hisilicon: add the Luofu LSW read-only baseline probe` |
| combined diff | `62 files changed, 8818 insertions(+), 4 deletions(-)`; 36 files added, 26 modified; patch 0020 alone is 6 files / +1443 / -393, patch 0021 alone is 4 files / +966 / -223 |
| tree equivalence | `diff -r --brief --exclude=.git` RC=0, no output: the patch(1) and git am trees are identical |
| checkpatch default and strict | 0 errors, **53 warnings** (49 previous + 2 per new patch: one MAINTAINERS and one binding-split warning each), 6 strict CHECKs (0004 x2, 0016 x2, 0019 x2; 0020/0021 add none); patch 0020 alone: 0 errors, 2 warnings, 0 CHECKs, 2111 lines; patch 0021 alone: 0 errors, 2 warnings, 0 CHECKs, 1456 lines |
| hashes | `sha256sum -c validation/sha256sums.txt`: 42/42 OK (21 series + 21 source patches); series 0020 `5f3e1e52c9a5fd8fa73ee3fc678b7116deef568d4eb619db52edfc3ce36f11f6`, series 0021 `7ed09990b2261716a1cf74ae48eec537168d07314d0303cfada15ff8b1604064`; source 0020 `806a113c...`, source 0021 `de69296d...`; tarball hash recorded in the same file and in `validation/tarball.txt` |
| kernel build evidence | not re-run here (no rebuild needed): the PIE-datapath lane's recorded 001-0020 result and the LSW-init lane's recorded 001-0021 result are both prepare RC=0 / compile RC=0 with **0 warnings and 0 errors** under CONFIG_WERROR=y, zImages 3,276,656 B sha256 `dfc7b753...` and 3,279,096 B sha256 `96b927d3...`, with the new objects built, the TEXT_OFFSET contract unchanged and the config re-derivations symbol-identical/byte-idempotent; see section 9 and the two port-notes summaries [V] |
| hardware | **none**.  No boot, no PIE arm (the arm path has never executed), no LSW W2-W6 write, no UART/TFTP/flash, no device.  RX completion decode and every PIE/LSW write effect remain [U]. |

The 53 warnings are the 49-warning 19-patch set plus the two expected
per-patch review items for 0020 and 0021: each adds one new header file
(`luofu-pie.h`, `luofu-switch-regs.h`, the MAINTAINERS warning) and
carries a binding hunk in the same patch (the binding-split warning).
The six strict CHECKs are unchanged.

The per-patch insertion/deletion counts above are taken from the `git am`
commit diff.  The diffstat trailer embedded in each generated patch is
computed from the patch text by `git apply --stat` and counts +1434/-384
for 0020 and +963/-220 for 0021 instead: git aligns the rewritten
comment/context blocks differently when it diffs the resulting tree, so
the two views differ by 9/9 and 3/3 lines without any difference in the
applied content: the patch(1) and git am trees are identical (section 5
and section 16).

The apply, git-am and checkpatch numbers above are the committed record;
sections 2-6 were updated to the same 21-patch form, and sections 13-15
keep the dated 17/18/19-patch history.
