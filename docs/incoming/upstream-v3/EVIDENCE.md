# Evidence map - upstream-v3 (task-161), 2026-11-29

Every claim in [REPORT.md](REPORT.md) / [DTS-0001-V3.md](DTS-0001-V3.md) /
[SERIES-STATUS.md](SERIES-STATUS.md) with its artefact or command.  All checks
host-only; no device was touched (`host-only` boundary).

| # | claim | evidence (file / command / hash) |
| --- | --- | --- |
| E1 | DTS-0001 regenerated from the current single source | `upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch` sha256 `1fd1d10a...`; source `target-skeleton/dts/luofu-r116.dts` 702 lines sha256 `df0d8e59...`; filter 690-line payload sha256 `03640448...`; payload-equals-filter check in the tool run |
| E2 | v2 entry was stale (carried the retired hog) | v2 sha256 `33956380...`; `git show HEAD~ -- ...` / old file grep found `wifi-pcie-hog`; v3 grep count 0; consumer `06-dts-series-dependency.md` section 1 |
| E3 | DTS-0001 applies and compiles, contract present | `git apply --check` + `patch -p1` clean on the post-series tree; cpp+dtc v1.8.1 -> 12,554 B sha256 `4f2e0e8d...`, pad 12,560 B `9deed1a8...`; decompiled nodes (aliases/flashinfo/enable-method); `check-release-line.sh` summary in `validation/release-line-v3/summary.txt` |
| E4 | L1 now includes 013a (56 patches) | `release-line/manifest.tsv` row `include A 013a-... d4b544a3...`; `INCLUDED.txt` line 14; hashes `17d714be...`/`da4966aa...`; patch file sha256 `d4b544a3...` |
| E5 | L1 validator PASS on pristine 6.18.60 | `upstream/series-6.18/validation/release-line-v3/summary.txt`: 56/56, 153 file events, 5 offsets, 0 fuzz/rejects, strict all 56, DTS + DTS-0001 contract, byte-identical filtered DTS, no W23 leak |
| E6 | Review form unchanged + clean | `upstream/series-6.18/series/` 26 files; `python3 tools/gen-series.py --check` -> "series up to date (26 patches)"; `run-validation.sh` PASS (26/26 patch & git am, identical trees, 3 offsets, 0 fuzz, checkpatch 0 errors / 73 warnings) |
| E7 | v2 review form not generated | no v2 artefacts in `series/`; plan in `../wifi-upstream-target/submission-series.md`; decision list in `../wifi-upstream-final/UPSTREAM-SERIES-FINAL.md` section 7 |
| E8 | Submission records refreshed and verify | `submission/MANIFEST.sha256` (105/105, `sha256sum -c` clean); `validation/sha256sums.txt` (all entries verify); `05-release-line.md` section 13; `06-dts-series-dependency.md` section 4 |
| E9 | Publication checker improved 13/10/9 -> 18/6/8 | `bash tools/publish-prep-check.sh`; emitted lines 23 PASS / 12 FAIL / 9 WARN; full output quoted in [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md) |
| E10 | Broken links fixed (G PASS) | `tools/fix-publication-links.py` (idempotent; `--check` -> 0); checker G: "all 1395 relative markdown links resolve"; 9 files listed in REPORT.md section 8 |
| E11 | COPYING + CONTRIBUTING added; DCO/NOTICE checks PASS | `opensource/COPYING` (18,515 B, GPL-2.0 text), `opensource/CONTRIBUTING.md`; checker A/B lines; NOTICE.md update |
| E12 | Credential false positives removed from tree scan | five files listed in [PUBLICATION-CHECKLIST.md](PUBLICATION-CHECKLIST.md) section 2; `bash -n`/`py_compile` clean; checker F "no high-confidence credential pattern in the tracked tree" PASS; history scan still flags old blobs (owner purge) |
| E13 | Wi-Fi is research-only and not in the submission | no Wi-Fi file in `series/`, `dts-series/`, `submission/` or the target tree change set; boundary wording quoted in REPORT.md section 5 from `../wifi-upstream-final/GPL-PD4-STATUS.md` and `../wifi-upstream-final/AP-MILESTONE-HONEST.md`; owning-lane reports linked there |
| E14 | No device / bench / push / history rewrite | lane transcript has no device command; `git -C opensource status` shows only file edits; no `git push`; commit is inside `opensource/` only |
| E15 | Concurrent lane work not committed by this lane | commit path list = REPORT.md section 8; `wifi-re-report` edits and the new untracked lane dirs left untouched |
