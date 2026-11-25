# Evidence map - upstream-v2-finish (2026-11-29, host-only)

Claim -> artefact/log.  Everything is reproducible from the two tools; no
network, no device, no vendor bytes.

| # | claim | evidence |
| --- | --- | --- |
| E1 | The probe's native-context sanity check is 16/16 folds at 100 % hunks | [`validation/fold-probe.md`](validation/fold-probe.md) (`l0pre` rows), [`validation/fold-probe.json`](validation/fold-probe.json) |
| E2 | Per-hunk applicability at the plan's named targets (the numbers in [`FOLD-EXECUTION.md`](FOLD-EXECUTION.md)) | fold-probe.json `folds.*.candidates.v1:*` entries incl. every hunk error |
| E3 | 10 of 16 folds compose in L0 order on the frozen v1 form; 6 do not | [`validation/fold-sequence.md`](validation/fold-sequence.md) ("composed 10/16"), [`validation/fold-sequence.json`](validation/fold-sequence.json) |
| E4 | The 6 blocked folds fail on DROP-patch/diagnostic context (040, 045, 047, 052, 055, 057) and fold chains (041->048, 046->059, 053->060) | [`validation/fold-blockers.md`](validation/fold-blockers.md), [`validation/fold-blockers.json`](validation/fold-blockers.json) (per-hunk missing context -> L0/v1 provider attribution, produced by [`tools/fold-blockers.py`](tools/fold-blockers.py)) |
| E5 | Fold 027 was executed: folded 0019 diff body `198d24f6...`, applies over entry 0018 | [`artifacts/folded-entries/0019-arm-clk-luofu-clock-tree-folded.patch`](artifacts/folded-entries/0019-arm-clk-luofu-clock-tree-folded.patch) |
| E6 | The folded 0019 integrates: full 26-patch partial-v2 series applies 26/26 with patch(1), 0 fuzz, 3 offsets; git am 26/26; identical trees; checkpatch 0 errors / 73 warnings | [`validation/partial-v2-L2/summary.txt`](validation/partial-v2-L2/summary.txt), `patch-apply.log`, `git-am-summary.txt`, `tree-equivalence.txt`, `checkpatch-*-summary.txt` |
| E7 | Folded 0019 is checkpatch-neutral (0 errors / 2 warnings, same as unmodified 0019) | [`validation/folded-0019/checkpatch-folded-strict.log`](validation/folded-0019/checkpatch-folded-strict.log), [`validation/folded-0019/checkpatch-original-strict.log`](validation/folded-0019/checkpatch-original-strict.log) |
| E8 | Current L2 review form re-validated PASS this session (26/26 patch(1)+git am, identical trees, 3 offsets, 0 fuzz, 0 errors / 73 warnings) | [`validation/baseline-L2/`](validation/baseline-L2/) |
| E9 | Current L1 release line re-validated PASS this session (55/55, 152 file events, 3 offsets, 0 fuzz, strict git apply all 55, loader hand-off DTS contract OK, board DTB 12,848 B `1c200daf...`, DTS-0001 DTB 12,680 B `2a5eb334...`) | [`validation/baseline-L1/summary.txt`](validation/baseline-L1/summary.txt) |
| E10 | The v2 plan inconsistencies K1-K6 are reproducible (21 checks, 18 PASS, 3 FAIL) | [`validation/consistency-check.md`](validation/consistency-check.md), [`tools/check-v2-plan.py`](tools/check-v2-plan.py) |
| E11 | 9 of 11 composed fold overlays add a `module_param` gate; only 027/044/067 do not | [`artifacts/fold-overlays/MANIFEST.md`](artifacts/fold-overlays/MANIFEST.md) |
| E12 | No hardware/bench contact, no L0/review-form edit, no post/push | the lane's commit history under `opensource/docs/incoming/upstream-v2-finish/` only; this page |

## Reproduce

```sh
cd opensource/docs/incoming/upstream-v2-finish
python3 tools/fold-probe.py  --pristine <linux-6.18.60 tree> --workdir /tmp/fold-probe
python3 tools/fold-sequence.py --workdir /tmp/fold-probe
python3 tools/fold-blockers.py --workdir /tmp/fold-probe
python3 tools/check-v2-plan.py --write /tmp/consistency.md
```

Selected logs are committed as text; the large patched trees stay in /tmp and
are not evidence (per project rule).
