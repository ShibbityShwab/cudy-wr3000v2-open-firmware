# upstream-v4 EVIDENCE map - claim -> artefact

Every claim in [REPORT.md](REPORT.md) and
[V2-REVIEW-FORM-STATUS.md](V2-REVIEW-FORM-STATUS.md) with the file, command
or log that backs it.  All checks are host-only; no hardware was touched.

## 1. The fold is in the tree

| claim | evidence |
| --- | --- |
| review-form 0013 now carries the DT `reset-gpios` handling and the binding documents it | `target-skeleton/target/linux/hisilicon/patches-6.18/013-arm-pci-luofu-host.patch` sha256 `96c91d71...`; delivery copy [artifacts/013-arm-pci-luofu-host.patch](artifacts/013-arm-pci-luofu-host.patch) |
| the generated review-form entry is generator output, not hand-edited | `upstream/series-6.18/series/0013-arm-pci-luofu-host.patch` sha256 `875d4b08...`; `python3 tools/gen-series.py --check` -> `series up to date (26 patches)`; delivery copy [artifacts/0013-arm-pci-luofu-host.patch](artifacts/0013-arm-pci-luofu-host.patch) |
| the 0013 commit message covers the fold and states the pending boot | `upstream/series-6.18/commit-messages/0013-arm-pci-luofu-host.txt` sha256 `1a254c47...`; delivery copy [artifacts/0013-commit-message.txt](artifacts/0013-commit-message.txt) |
| the board DTS describes the RC0 endpoint reset lines as active-low reset-gpios with the GP1 timings, and no gpio-hog remains | `target-skeleton/dts/luofu-r116.dts` (715 lines) sha256 `6774221e...`; `grep -n 'reset-gpios\|reset-assert-us\|reset-deassert-us\|wifi-pcie-hog'` |
| 013a is gone from L0; L0 is 66 files | `ls target-skeleton/target/linux/hisilicon/patches-6.18/ | wc -l` -> 66; `git status` shows the deletion; `release-line/INCLUDED.txt` 55 lines |
| the release-line manifest/INCLUDED are consistent with the 66-file L0 | `release-line/manifest.tsv` sha256 `e077544b...` (55 include / 11 exclude, 013 row re-hashed); `release-line/INCLUDED.txt` sha256 `cd7ae90c...` |
| DTS-0001 was regenerated from the single source and has no W23 leak | `upstream/series-6.18/dts-series/0001-...patch` sha256 `6fa1c690...`, 716 lines / 703-line payload; canonical regeneration command in REPORT section 8; validator check below |

## 2. Validation results

| claim | evidence |
| --- | --- |
| L2 review form applies in order, patch(1) and git am produce identical trees | [validation/series-v4/patch-apply.log](validation/series-v4/patch-apply.log), [validation/series-v4/git-am.log](validation/series-v4/git-am.log), [validation/series-v4/tree-equivalence.txt](validation/series-v4/tree-equivalence.txt) ("identical"), [validation/series-v4/offsets.txt](validation/series-v4/offsets.txt) (3 offsets, 0 fuzz) |
| L2 checkpatch: 0 errors / 73 warnings | [validation/series-v4/checkpatch-summary.txt](validation/series-v4/checkpatch-summary.txt), [validation/series-v4/checkpatch-default.log](validation/series-v4/checkpatch-default.log), [validation/series-v4/checkpatch-strict.log](validation/series-v4/checkpatch-strict.log) |
| L1 release line: 55/55 apply, 0 fuzz, strict git apply clean | [validation/release-line-v4/summary.txt](validation/release-line-v4/summary.txt), [validation/release-line-v4/patch-apply.log](validation/release-line-v4/patch-apply.log), [validation/release-line-v4/git-apply-strict.log](validation/release-line-v4/git-apply-strict.log) |
| board DTS + DTS-0001 compile with 0 diagnostics and keep the loader hand-off contract | [validation/release-line-v4/summary.txt](validation/release-line-v4/summary.txt) (board DTB 12,848 B `1c200daf...`; DTS-series DTB 12,680 B `2a5eb334...`; both totalsizes 8-byte aligned; contract lines), [validation/release-line-v4/dts-cpp.err](validation/release-line-v4/dts-cpp.err) and `dts-dtc.err` (0 bytes), [validation/release-line-v4/dts-series-apply.log](validation/release-line-v4/dts-series-apply.log) |
| DTS-0001 payload is byte-identical to the filtered single source and W23-free | [validation/release-line-v4/summary.txt](validation/release-line-v4/summary.txt) ("board DTS byte-identical to the filtered target DTS"), [validation/release-line-v4/dts-series-expected.dts](validation/release-line-v4/dts-series-expected.dts), [validation/release-line-v4/dts-series-filter.log](validation/release-line-v4/dts-series-filter.log) |
| the folded driver compiles with W=1 on the OpenWrt GCC 14.4.0 toolchain | [validation/compile/pcie-luofu-compile.log](validation/compile/pcie-luofu-compile.log) (`CC drivers/pci/controller/dwc/pcie-luofu.o`, `EXIT=0`) |
| the binding YAML is well-formed and contains the new properties | `python3 -c 'import yaml; yaml.safe_load(open(...))'` (see REPORT section 2.2); no log committed - it is a single PyYAML parse of the tracked file |

## 3. The v2 status claims

| claim | evidence |
| --- | --- |
| v2 is not generated and needs 16 more content folds | [V2-REVIEW-FORM-STATUS.md](V2-REVIEW-FORM-STATUS.md) section 1 table vs the current L0 file list (`ls patches-6.18/`); fold verdicts in [../wifi-upstream-target/PATCH-CLASSIFICATION.md](../wifi-upstream-target/PATCH-CLASSIFICATION.md) |
| gen-series.py is one-L0-patch-to-one-entry and verifies byte identity | `upstream/series-6.18/tools/gen-series.py` lines ~26-34 (ORIGINALS), ~315-359 (`main`, byte-identity check) |
| commit-message drafts for 0027-0030 already exist | [../wifi-upstream-target/submission-series.md](../wifi-upstream-target/submission-series.md) section "Commit-message drafts for the new RFC entries" |
| the remaining fold sources exist and are class-B/C lab patches | `target-skeleton/target/linux/hisilicon/patches-6.18/027,031,032,036,038,041,043,044,046,048,051,053,059,060,066,067-*.patch` (line counts in the status table) |
| the attended-boot gate is a project rule | `AGENTS.md`/"No blind flash writes" + `07-dts-upstream-bindings.md` ("no active DTS change may be accepted from host checks alone") + submission-series acceptance criteria "every active-DTS change boot-tested on the unit" |

## 4. Supersession pointer

The v3 lane's status statements (L1 = 56 with 013a, DTS-0001 `1fd1d10a`, L0 =
67, "single remaining content decision") are superseded by this package.
Updated in-tree docs: `upstream/series-6.18/{00-cover-letter,04-validation,
05-release-line,06-dts-series-dependency,07-dts-upstream-bindings,README}.md`,
`upstream/series-6.18/submission/README.md` and
`docs/upstream/SUBMISSION-STATUS.md` (all carry a dated v4 update/note; the
older dated sections are kept as history per the project convention).
