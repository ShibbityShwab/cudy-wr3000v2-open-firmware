# upstream-v2-finish - executing the v2 review-form folds host-side (2026-11-29)

**Lane:** `upstream-v2-folds` (shared task **task-202**), **host-only**: no
router, no bench link, no UART/TFTP/flash, no vendor binary touched, nothing
posted or pushed.  Advances the state recorded in
[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md).

## Verdict in one paragraph

The v2 blocker list was: 16 content folds, the fold-pipeline decision, one
attended boot, the v2 generator/package rebuild.  This lane executed the
folds **as far as the host can**: a per-hunk applicability probe shows
**10 of 16 fold sources compose in L0 order on the frozen v1 review form**
and **6 are blocked** by diagnostic context (DROP patches 040/045/047/052/
055/057) and fold chains - not by the plan's target entries; the dependency
roots are attributed hunk-by-hunk.  The one fully mechanical entry fold
(**027 -> 0019**) was executed, integrated into the full 26-patch series and
validated (26/26 patch(1)+git am identical, checkpatch-neutral).  The other
9 composing folds are delivered as re-contextualized overlays; 8 of them
still need a gate/placement decision (**9 of 11 overlays add a
`module_param` gate**), so **no new RFC entry 0027-0030 is complete yet**.
The lane also found the v2 plan does not agree with itself (fold 031 is
missing from the submission-series delta table; the gate policy is
contradictory; the classification page has stale pre-v4 counts) and
re-validated the current L1/L2 baselines PASS.  Evidence argues **against**
v4's Option A (L0 rewrite) as the first step and **for an Option B
upstream-composition tree**; the mechanism is specified but the decision is
the owner's.  The one attended boot and all bench-side claims remain
unexecuted and are listed with exact commands.

## One-screen state (2026-11-29)

| area | state | evidence |
| --- | --- | --- |
| fold probe (per-hunk, 16 folds x 3-6 candidate states) | native sanity **16/16 @ 100 %**; target-state matrix produced | [validation/fold-probe.md](validation/fold-probe.md), [fold-probe.json](validation/fold-probe.json) |
| sequential composition on v1 | **10/16 compose**, 6 blocked with attributed roots | [validation/fold-sequence.md](validation/fold-sequence.md) |
| executed fold | **027 -> 0019**: folded entry artifact, applies, full 26-patch series 26/26 patch(1)+git am identical, checkpatch-neutral | [artifacts/folded-entries/](artifacts/folded-entries/), [validation/partial-v2-L2/](validation/partial-v2-L2/) |
| composing fold overlays | 10 re-contextualized deltas (includes 027/066/067); 9/11 add a gate; 051 needs `-C1` review | [artifacts/fold-overlays/MANIFEST.md](artifacts/fold-overlays/MANIFEST.md) |
| blocked folds | 041, 046, 048, 053, 059, 060 (diagnostic context + chains) | [FOLD-EXECUTION.md](FOLD-EXECUTION.md) |
| v2 plan consistency | 21 checks, 18 PASS, **3 FAIL** (031 gap, gate policy, v1 gates); stale pre-v4 counts documented | [CONSISTENCY.md](CONSISTENCY.md), [validation/consistency-check.md](validation/consistency-check.md) |
| fold pipeline | decision analysed; **Option B recommended** with a concrete composition-tree mechanism | [PIPELINE.md](PIPELINE.md) |
| L1 baseline | **PASS** 55/55, 0 fuzz, strict git apply clean, DTS contract OK, DTBs 12,848 B / 12,680 B | [validation/baseline-L1/summary.txt](validation/baseline-L1/summary.txt) |
| L2 baseline | **PASS** 26/26 patch(1)+git am identical, 3 offsets, 0 fuzz, 0 errors / 73 warnings | [validation/baseline-L2/](validation/baseline-L2/) |
| v2 form itself | **NOT generated**: needs 6 re-context folds, gate/placement decisions, commit messages 0027-0030 and the generator extension | [ATTENDED-NEXT.md](ATTENDED-NEXT.md) |
| attended boot (v4 active-DTS 013) | **NOT run**; acceptance list + exact procedure documented; the Lead fires | [ATTENDED-NEXT.md](ATTENDED-NEXT.md) §A1 |
| bench claims for folded content | **none made**; per-group measurement list documented | [ATTENDED-NEXT.md](ATTENDED-NEXT.md) §A2 |
| posted/pushed? | **No.** | this page |

## Deliverables / read order

| file | content |
| --- | --- |
| [FOLD-EXECUTION.md](FOLD-EXECUTION.md) | the 16-fold ledger: per-hunk applicability, sequential composition, executed vs blocked with dependency roots, what is still missing |
| [CONSISTENCY.md](CONSISTENCY.md) | the v2 plan cross-document findings K1-K6 + the reproducible checker |
| [PIPELINE.md](PIPELINE.md) | Option A vs B analysis on the new evidence, recommended Option B mechanism and residual risks |
| [ATTENDED-NEXT.md](ATTENDED-NEXT.md) | the attended boot acceptance + bench gates + exact next host commands |
| [EVIDENCE.md](EVIDENCE.md) | claim -> artefact/log map |
| [artifacts/folded-entries/](artifacts/folded-entries/) | the executed folded v2 entry (diff body) |
| [artifacts/fold-overlays/](artifacts/fold-overlays/) | re-contextualized fold deltas + their MANIFEST |
| [tools/fold-probe.py](tools/fold-probe.py) / [tools/fold-sequence.py](tools/fold-sequence.py) / [tools/fold-blockers.py](tools/fold-blockers.py) / [tools/check-v2-plan.py](tools/check-v2-plan.py) | the four reproducible host-only tools |
| [validation/](validation/) | probe/sequence output, baseline L1/L2, partial-v2 L2, consistency check |

## Hard boundaries kept

Host-only; no device command; no edit to L0, the v1 review form, the generator
or the release line (the frozen v1 and the bench line are untouched); no
vendor bytes/secrets; text-only artefacts; commits inside `opensource/` only;
no push.

## Reproduce

```sh
cd opensource/docs/incoming/upstream-v2-finish
python3 tools/fold-probe.py  --pristine <linux-6.18.60 tree> --workdir /tmp/fold-probe
python3 tools/fold-sequence.py --workdir /tmp/fold-probe
python3 tools/fold-blockers.py --workdir /tmp/fold-probe
python3 tools/check-v2-plan.py
```
