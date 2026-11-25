# Series status - luofu 6.18 platform submission (2026-11-29)

Three layers plus the DTS companion; **nothing has been posted or reviewed**.
The authoritative long-form pages are `../../upstream/series-6.18/README.md`,
`05-release-line.md` and `06-dts-series-dependency.md` (updated by this lane);
numbers here are the current state after the v3 pass.

## 1. State table

| layer | what it is | state (2026-11-29) | evidence |
| --- | --- | --- | --- |
| **L0** lab patch stack | `opensource/target-skeleton/target/linux/hisilicon/patches-6.18/` (build source of the bench image) | **67 files** (001-060 + 062-067 + 013a) | directory listing; `05-release-line.md` section 1 |
| **L1** release line | L0 minus the 11 bench-only SMP diagnostics; what a release snapshot carries | **56 include / 11 exclude**; `013a` added by this lane (was missing the load-bearing reset pulse); validator **PASS** | `release-line/manifest.tsv` (sha256 `17d714be...`), `INCLUDED.txt` (`da4966aa...`), logs `validation/release-line-v3/` |
| **L1 validator** | `tools/check-release-line.sh` on pristine linux-6.18.60 | **PASS**: 56/56 `patch(1)` RC=0, 153 file events, 5 offset hunks, **0 fuzz, 0 rejects**; strict `git apply` **all 56** without fuzz; board DTS + DTS-0001 contract checks pass | `validation/release-line-v3/summary.txt` |
| **L2** upstream review form | `upstream/series-6.18/series/` (26 format-patches) + per-patch messages + cover letter | **frozen 26-patch v1**, unchanged; validated PASS; **v2 (30 patches) NOT generated** | `series/` 26 files; `validation/*`; v2 plan in `../wifi-upstream-target/submission-series.md` |
| **L2 drifts** | generated form vs L0 mapping | `gen-series.py --check` -> `series up to date (26 patches)`, RC=0 | command |
| **L2 validation** | `tools/run-validation.sh` on pristine 6.18.60 | **PASS**: 26/26 `patch(1)` and 26/26 `git am`, identical trees (`diff -r`), 3 offset hunks, 0 fuzz; checkpatch default and `--strict` **0 errors / 73 warnings** | `run-validation.sh` output this session; `04-validation.md` |
| **L2 build** | full kernel compile of the 26-patch tree | **not re-run this lane**; the patches are byte-identical to the last recorded full-compile pass (RC=0, `04-validation.md`) and the only new build product this lane is the DTS entry, compiled with cpp+dtc | `04-validation.md`; `06-dts-series-dependency.md` |
| **L3** DTS companion | `dts-series/0001-...patch` (DTS-0001) | **regenerated v3**: sha256 `1fd1d10a...`, 703 lines / 690-line payload, no hog; applies + compiles + contract verified | [DTS-0001-V3.md](DTS-0001-V3.md) |
| **submission package** | `submission/` cover letter + manifests | v3 update block + refreshed hashes; `MANIFEST.sha256` **105/105** verified; `validation/sha256sums.txt` all entries verify | `submission/README.md`, `submission/MANIFEST.sha256` |
| **posted?** | - | **no**; no list, no maintainer, no push | this lane |

## 2. What is still between this tree and a posted platform series

1. **Generate the v2 review form (30 kernel patches + DTS-0001).** Requires the
   content folds decided in 2026-11-24 (folds 027/038/041/044/048/051/053/066/067
   and the four new RFC entries 0027-0030 from L0 031/032/036/043/046/059/060),
   plus the `013a` -> DT `reset-gpios` fold (driver + binding + this DTS).
   Regenerate with `gen-series.py` from L0; never hand-edit `series/`.
2. **One attended boot** of the `reset-gpios` active-DTS change (recovery
   armed; 2 CPUs, both PCIe endpoints, NAND/UBI, serial).
3. **Cover-letter truth**: Wi-Fi is out of scope; PD4-patched firmware never
   ships; GPL request unanswered; RFC status and no support claims.  Use the
   v2 long-form wording in `../wifi-upstream-final/UPSTREAM-SERIES-FINAL.md`
   section 7-9 and `../wifi-upstream-final/GPL-PD4-STATUS.md` section 5.
4. **No vendor bytes / no PD4 / no research modules** in the posted bundle
   (satisfied by the current artefacts; keep it that way).

Owner gates (not kernel): publication path (fresh curated repos recommended vs
legacy history purge), COPYING/DCO/SPDX closure, target PR only after the
install path ran end-to-end on a recovered unit, and the actual posting
decision.  See [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md).

## 3. What this lane did NOT do (deliberate)

* no change to any L0 patch content, to the 26 review-form patches or to the
  target tree/image recipe - so the bench image line and the target PR are
  untouched;
* no v2 series generation (content decisions + attended boot gate);
* no push, no history rewrite, no unshallow, no release decision;
* no device/bench contact (the Lead fires).
