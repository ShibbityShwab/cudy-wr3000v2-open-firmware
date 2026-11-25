# v2 plan consistency check (2026-11-29, host-only, read-only)

The v2 plan lives in three documents written at different times; this page is
the delta between them and the tree, with a reproducible checker:
[`tools/check-v2-plan.py`](tools/check-v2-plan.py).  Full checker output:
[`validation/consistency-check.md`](validation/consistency-check.md)
(21 checks, 18 PASS, 3 FAIL at the time of writing).

## Findings

### K1 - fold 031 is missing from the submission-series v2 delta table (FAIL)

[`../wifi-upstream-target/submission-series.md`](../wifi-upstream-target/submission-series.md)
"deltas from v1" lists only 15 of the 16 fold sources; `031-net-lsw-eps-channel-map`
is absent from its "source L0 patches" column, while both
[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md)
and [`../wifi-upstream-target/PATCH-CLASSIFICATION.md`](../wifi-upstream-target/PATCH-CLASSIFICATION.md)
include it.  A lane executing v2 from the submission-series table alone would
silently drop a fold.  (Checker row: "plan delta table fold set == v4-status
fold set".)

### K2 - the gate policy contradicts itself (FAIL)

- V2 status section 1 rows 3/4/13/14: the new RFCs are "a normal-boot init
  (gates -> none/DT)" and "retire the accepterr bench option"; the acceptance
  list says **no `luofu_*` module parameters / bench gates in the submission
  patches**.
- The commit-message drafts for the same RFC entries in submission-series.md
  say the opposite: 0027/0028 "the write sequence remains behind the attending
  gate; a normal boot keeps the block read-only", 0030 "the engine stays
  behind the attending arm option".
- The tree confirms the gap: the frozen v1 form already carries 6
  `module_param` gates (0020:1, 0022:2, 0025:3); nine of the eleven composed
  fold overlays in [`artifacts/fold-overlays/`](artifacts/fold-overlays/)
  add a `module_param` gate (all but 027/044/067).

Impact: the v2 patch content and the v2 commit messages cannot both be right;
one policy must be chosen before the form is generated.  This is an owner
decision, not a text merge.

### K3 - PATCH-CLASSIFICATION.md has stale pre-v4 counts (verified stale)

The 2026-11-24 classification says L0 is **67 files** (incl. `013a`) and that
"L1 must grow 55 -> 56: add 013a", and that DTS-0001 "must be regenerated
(after 013a moves into the DTS)".  The v4 lane already executed the 013a fold:

- L0 is **66 files** today (013a deleted, content in 013 and the board DTS);
- L1 is **55 include / 11 exclude** and the L1 validator re-ran **PASS 55/55**
  this session (not 56);
- DTS-0001 is **716 lines sha256 `6fa1c690...`**, applies/compiles, carries
  no `wifi-pcie-hog`.

A lane that acts on the classification page would duplicate completed work.

### K4 - the named fold targets are not the textual fold points (new)

The plan names a target entry per fold, but per-hunk probing
([`validation/fold-probe.md`](validation/fold-probe.md)) shows several
sources do not apply at their named target at all:

| fold | named target | applies there | applies at |
| --- | --- | --- | --- |
| 038 | 0009 | 0/6 hunks | end of v1 form (6/6) |
| 041 | 0009 | 0/7 | end of v1 form (2/7, still blocked) |
| 051 | 0009 | 0/3 | end of v1 form (2/3, 3/3 with -C1) |
| 031 | 0016/0021/0022 | 0/6 | after 0025 (6/6) |
| 044 | 0022 | 3/19 | end of v1 form (8/19) |
| 048 | 0020 | 1/4 | nowhere until 041 is folded |
| 053 | 0024 | 2/3 | end of v1 form (2/3, still blocked) |

The 0009/0023 GEMAC target is the clearest case: the GEMAC folds patch the
phylink front end that patch 0020 introduces, so they cannot be text-merged
into 0009 (resource half only).  Either the target is re-sourced (e.g. the
content becomes its own entry after 0026) or the patch is re-contextualized.
"Make the fold decisions in the target patches first" is therefore not a
mechanical operation for these six.

### K5 - the v2 generator/package rebuild has not started (verified absent)

- `commit-messages/`: **26** files, no 0027-0030 (drafts only in
  submission-series.md);
- `tools/gen-series.py` `SERIES` is still 26 entries; `--check` reports
  "series up to date (26 patches)";
- cover letter is still `[PATCH 00/26]`; GROUPS/mapping are 26-entry;
- `submission/` package and `validation/sha256sums.txt` are the v1 ones.

### K6 - two acceptance items stay environment-blocked, not content-blocked

- `dt_binding_check`: `dtschema` is still not installed in this session
  (`python3 -c "import dtschema"` fails); the review-form acceptance item
  cannot be closed here.  The recorded host DTS/DTS-0001 compile was re-run
  this session with the system dtc (see [`EVIDENCE.md`](EVIDENCE.md)).
- Full kernel compile for the v2 form: owed (v4 compiled only the folded
  driver object at W=1).

## Checks that pass (re-verified this session)

- L0 = 66 files; v1 review form = 26 files; release-line manifest = 55
  include / 11 exclude; all manifest sha256 match the current L0 patches
  (0 drift).
- `gen-series.py --check` clean; `0013` carries
  `reset-gpios`/`reset-assert-us`/`reset-deassert-us`; DTS-0001 hash/line
  count match the v4 record; no `wifi-pcie-hog`.
- Baseline validations re-ran PASS this session: L2 26/26 patch(1)+git am,
  identical trees, 3 offsets, 0 fuzz, 0 errors / 73 warnings; L1 55/55,
  0 fuzz, strict git apply clean, loader hand-off DTS contract OK.
