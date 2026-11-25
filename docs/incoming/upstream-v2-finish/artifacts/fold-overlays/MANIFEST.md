# Composed fold artefacts - what they are and what they are not

**These are composition inputs, not final review-form patches.**  Both sets
were generated on the host from the pristine linux-6.18.60 tree and the
frozen 26-patch v1 review form; nothing was measured on hardware.

## `folded-entries/`

Diff bodies that **replace** a v1 entry in the v2 form.

| file | replaces | sha256 | status |
| --- | --- | --- | --- |
| `0019-arm-clk-luofu-clock-tree-folded.patch` | series/0019 (folds L0 027) | `198d24f65dc8e2842a0f20a64526527daa30c5a817d58a6d29c6c69a39c4006e` | validated: applies over entry 0018, full 26-patch series applies 26/26 patch(1)+git am identical, checkpatch-neutral ([validation/partial-v2-L2](../../validation/partial-v2-L2/)) |

Diff body only: the format-patch mail header is supplied by
`tools/gen-series.py` from `commit-messages/0019`.  The commit message still
has to be updated to mention the folded 027 content.

## `fold-overlays/`

Re-contextualized fold deltas emitted by
[`tools/fold-sequence.py`](../../tools/fold-sequence.py): each is the diff that
the fold source produced when applied **in L0 order on top of the v1 form**
at the point where it composed.  They are raw material for the v2
composition tree; a hand re-contextualization / gate strip is still required
before any of them becomes a review-form entry.

| overlay (source L0) | plan target | files | +lines/-lines | module_param added | gate var added |
| --- | --- | --- | --- | --- | --- |
| 027-arm-clk-luofu-readonly-mux-fix | 0019 (executed separately) | 1 | +6/-6 | 0 | 0 |
| 031-net-lsw-eps-channel-map | LSW group -> applies after 0025 | 2 | +95/-2 | 1 | 1 |
| 032-net-lsw-eqm-qminit | new 0027 | 3 | +1118/-48 | 1 | 1 |
| 036-net-lsw-pfe-init | new 0028 | 3 | +235/-13 | 1 | 1 |
| 038-net-gmac-maxframe-init | 0009/0023 -> only after 0026 | 1 | +96/-1 | 1 | 1 |
| 043-net-lsw-ifc-default-table | new 0028 | 3 | +261/-5 | 1 | 1 |
| 044-net-lsw-ifc-gate-token-fix | 0022 -> only after 043 | 3 | +34/-34 | 0 | 0 |
| 051-net-gmac-macregs-w16 | 0009/0023 -> -C1 | 1 | +114/-0 | 1 | 1 |
| 066-thermal-luofu-tvsensor-w23 | 0012 (pending gate decision) | 2 | +404/-60 | 1 | 0 |
| 067-dt-bindings-thermal-luofu-tvsensor-w23 | 0012 binding (with 066) | 1 | +65/-22 | 0 | 0 |

Hashes: see the commit that added this file (`sha256sum` of each overlay is
recorded in the lane commit message history); the overlays are regenerated
deterministically by `tools/fold-sequence.py` from the L0 stack.

Deviations to review before use: **051** composed only with one line of
context (`git apply -C1`) and must be re-derived by hand; **066/067** are
textually clean but carry an unresolved gate/content decision (see
[`FOLD-EXECUTION.md`](../../FOLD-EXECUTION.md) and `CONSISTENCY.md` K2).
