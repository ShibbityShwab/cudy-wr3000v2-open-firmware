# upstream-v2-finish - executing the v2 review-form folds host-side (2026-11-29)

**Lane:** `upstream-v2-folds` (shared task **task-202**), **host-only** (no
router, no bench link, no UART/TFTP/flash; no vendor binary touched; nothing
posted or pushed).  Session date 2026-11-29, advancing the state recorded in
[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md).

**Question:** the 30-patch v2 review form is blocked on (a) the 16 remaining
content folds, (b) the fold-pipeline decision, (c) one attended boot, and (d)
the v2 generator/package rebuild.  Which of those can this lane execute
host-side, and exactly what is left for the attended session?

**Status: in progress.**  This directory is written incrementally; the
committed state is the evidence.

## Deliverables

| file | content |
| --- | --- |
| `README.md` | this page: verdict, state, what was executed and what was not |
| `tools/fold-probe.py` | host-only applicability probe for the 16 folds against v1 form states (per-hunk) |
| `FOLD-EXECUTION.md` | per-fold execution ledger: source hunks, gate hunks, applicability evidence, what was executed |
| `CONSISTENCY.md` | cross-document consistency checks of the v2 plan |
| `PIPELINE.md` | the fold-pipeline decision and the concrete mechanism |
| `ATTENDED-NEXT.md` | the remaining attended items + exact next commands |
| `EVIDENCE.md` | claim -> artefact/log map |
| `validation/` | probe/válidation logs (text only) |
| `artifacts/` | composed patches / generated forms (text only) |

## Hard boundaries kept

Host-only; no device command; no L0/review-form edits (the frozen v1 form and
the bench line stay untouched); no vendor bytes/secrets; commit inside
`opensource/` only; no push.
