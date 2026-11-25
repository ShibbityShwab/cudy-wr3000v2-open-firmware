# Fold execution ledger - the 16 remaining v2 content folds (2026-11-29)

Host-only execution of the folds listed in
[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md)
section 1.  Nothing here was sent, and nothing here was measured on hardware;
the disposition column only says what can be *composed and validated on the
host*.

## Method (reproducible)

1. `tools/fold-probe.py` copies the pristine linux-6.18.60 tree into a scratch
   git repo, rebuilds the frozen 26-patch v1 review form (one commit per
   entry) and the full 66-patch L0 lab stack (one commit per patch), then runs
   `git apply --check` **per hunk** for every fold source patch at: its own
   native L0 context, the plan's target entry state(s), and the end-of-v1
   state.  Output: [`validation/fold-probe.json`](validation/fold-probe.json),
   [`validation/fold-probe.md`](validation/fold-probe.md).
2. `tools/fold-sequence.py` applies the 16 fold sources **in L0 order on top
   of the frozen v1 tree** (nothing forced) and records which compose.
   Output: [`validation/fold-sequence.json`](validation/fold-sequence.json),
   [`validation/fold-sequence.md`](validation/fold-sequence.md).
3. `tools/fold-blockers.py` explains the 6 failures without guessing: for
   every hunk of those sources it finds the context/removed lines absent from
   the composed tree and attributes them to the L0 patch (and v1 entry) that
   adds them.  Output: [`validation/fold-blockers.md`](validation/fold-blockers.md),
   [`validation/fold-blockers.json`](validation/fold-blockers.json).
4. The composing fold deltas were re-emitted against the composed tree into
   [`artifacts/fold-overlays/`](artifacts/fold-overlays/); the one entry fold
   that is fully mechanical was executed into
   [`artifacts/folded-entries/`](artifacts/folded-entries/).

The probe's native-context sanity check is 16/16 folds at 100 % hunks, so the
per-hunk matrix below is trustworthy.

## Result summary

| disposition | folds | count |
| --- | --- | --- |
| **executed** as a folded v2 entry (validated) | 027 -> 0019 | 1 |
| **composes** as a re-contextualized overlay on the v1 tree | 031, 032, 036, 038, 043, 044, 051, `066`, `067` | 9 (+027) |
| **blocked** - needs manual re-contextualization | 041, 046, 048, 053, 059, 060 | 6 |

Of the 9 overlays, 8 still need a fold decision (gate removal / entry
placement); only 067 is pure text.  None of the 4 new RFC entries (0027-0030)
can be completed end-to-end yet: 0027/0028 content partially composes, 0029
and 0030 are among the blocked folds.

## Per-fold matrix

`native` = hunks applying at the source patch's own L0 context; `target` =
the plan's named v2 entry state(s); `seq` = composes when applied in L0 order
on top of the frozen v1 form.

| fold | source (L0) | plan target | native | target state result | seq | disposition |
| --- | --- | --- | --- | --- | --- | --- |
| 027 | arm-clk readonly mux fix (23 L) | 0019 | 1/1 | v1:0019 1/1, whole patch yes | yes | **executed** -> [artifacts/folded-entries/0019-..-folded.patch](artifacts/folded-entries/0019-arm-clk-luofu-clock-tree-folded.patch) |
| 031 | net-lsw eps channel-map (143 L) | LSW group 0016/0021/0022/0025 | 6/6 | 0/6 at 0016/0021/0022; **6/6 at 0025** | yes | overlay; target must be 0025 (or a new entry after it), not the earlier group members |
| 032 | net-lsw eqm qminit (1366 L) | new RFC 0027 | 24/24 | 16/24 at 0025, 17/24 at 0026/v1end | yes | overlay -> 0027 content candidate (gate strip + message still owed) |
| 036 | net-lsw pfe init (357 L) | new RFC 0028 | 14/14 | 0/14 at 0025/0026/v1end | yes | overlay -> 0028 content candidate (needs 043 + 046) |
| 038 | net-gmac maxframe init (139 L) | 0009/0023 | 6/6 | **0/6 at 0009**, 5/6 at 0023, 6/6 at v1end | yes | overlay; 0009 is not a textual fold point, target must be re-sourced |
| 041 | net-gmac macconf replay (161 L) | 0009/0023 | 7/7 | 0/7 at 0009, 2/7 at 0023/v1end | **NO** | blocked: hunk context from DROP 040 (W9 diag) + 030 (trace) |
| 043 | net-lsw ifc default table (354 L) | new RFC 0028 | 11/11 | 0/11 at 0025/0026/v1end | yes | overlay -> 0028 content candidate |
| 044 | net-lsw gate-token fix (254 L) | 0022 | 19/19 | 3/19 at 0022, 8/19 at v1end | yes | overlay; 0022 is not a textual fold point (post-043 context) |
| 046 | net-lsw ifc eps ext (333 L) | new RFC 0028 | 16/16 | 0/16 at any v1 state | **NO** | blocked: context from DROP 045 (W11 state dump); also 043 |
| 048 | net-pie rxcfg w13 (114 L) | 0020 | 4/4 | 1/4 at 0020 and v1end | **NO** | blocked: context from fold 041 (itself blocked) + DROP 047 (W13 dump) |
| 051 | net-gmac macregs w16 (137 L) | 0009/0023 | 3/3 | 0/3 at 0009, 1/3 at 0023, 2/3 at v1end | yes (`-C1`) | overlay; composes only with reduced context - needs manual check |
| 053 | net-pie rxdecode vendor-exact (125 L) | 0024 | 3/3 | 2/3 at 0024/v1end | **NO** | blocked: context from DROP 047/052 (W13/W17 diagnostics) |
| 059 | net-lsw napt init (431 L) | new RFC 0029 | 12/12 | 4/12 at 0026/v1end | **NO** | blocked: depends on fold 046 + DROP 045/054/056 |
| 060 | net-pie exception delivery (70 L) | new RFC 0030 | 3/3 | 0/3 at 0026/v1end | **NO** | blocked: depends on fold 053 + DROP 055/057 (mirror/cpucopy) |
| 066 | thermal w23 (549 L) | 0012 | 3/3 | **v1:0012 3/3, whole patch yes** | yes | composes textually, but rewrites the driver + adds a gated enable/trim write path; needs the content decision (below) |
| 067 | thermal binding w23 (114 L) | 0012 binding | 2/2 | **v1:0012 2/2, whole patch yes** | yes | composes textually, but documents properties only the 066 driver consumes -> only valid folded together with 066 |

Full per-hunk errors: [`validation/fold-probe.json`](validation/fold-probe.json).
`-C1` means the hunk applied only after reducing context to one line; that is
recorded as a warning, not as a clean fold.

## Executed fold: 027 -> 0019

- Source: `027-arm-clk-luofu-readonly-mux-fix.patch` (removes the writable
  `.set_parent` from the read-only CRG mux ops and records why).
- Artifact: [`artifacts/folded-entries/0019-arm-clk-luofu-clock-tree-folded.patch`](artifacts/folded-entries/0019-arm-clk-luofu-clock-tree-folded.patch)
  (diff body only; the generator supplies the mail header).
  sha256 `198d24f65dc8e2842a0f20a64526527daa30c5a817d58a6d29c6c69a39c4006e`.
- Evidence:
  - applies over the v1 entry-0018 state with `git apply --check` RC=0;
  - substituted for `series/0019` in the full 26-patch v1 series, the series
    applies **26/26 with `patch(1)`**, 0 fuzz, the same 3 offsets as the v1
    form, and the resulting CRG files are byte-identical to the L0-order fold
    composition (`git diff` empty);
  - checkpatch `--strict` on the folded patch: **0 errors / 2 warnings**,
    identical to the unmodified 0019 (the known binding-split WARNING and the
    spinlock CHECK); the fold adds none.
  - logs: [`validation/folded-0019/`](validation/folded-0019/),
    [`validation/partial-v2-L2/`](validation/partial-v2-L2/).

The other mechanical-looking entry folds are **not** text merges:

- **0012 (066 + 067)**: 066 rewrites the thermal driver to the v1/hwver-3
  read path and adds the vendor enable/trim write sequence behind a
  `module_param` gate; 067 documents exactly those properties.  Folding them
  into 0012 means either keeping a module parameter (violates the review-form
  acceptance rule) or making previously gated sensor writes run on a normal
  boot - an unmeasured behaviour that needs the bench and an owner decision.
  Not executed; both overlays are delivered for the decision.
- **GEMAC (038/041/051 -> 0009/0023)**: 038 applies 0/6 hunks at 0009 and only
  6/6 after the whole v1 form (its context is the post-0020 GEMAC front end);
  the plan's target is therefore not the textual fold point.  The fold needs a
  re-source decision (target 0023 and reorder it after 0026, or make the
  content a new entry) - see [`PIPELINE.md`](PIPELINE.md).
- **LSW (031 -> group; 044 -> 0022)**: 031 only applies after 0025; 044 only
  after the post-043 switch content.  Entry placement must be re-decided or
  the content re-contextualized.
- **PIE (048 -> 0020; 053 -> 0024)**: both are blocked by diagnostic context
  (047/052 and the blocked 041), not by the plan target.

## What is still missing for the 30-patch form

1. 6 blocked folds need manual re-contextualization (dependency roots in the
   table above; a re-context must drop the diagnostic surrounding, not carry
   it).
2. 8 composing folds need the gate/placement decision; the overlays in
   `artifacts/fold-overlays/` are the raw material, not the final patches.
   Nine of the eleven overlays add a `module_param` gate (only 027/044/067 do
   not) - the acceptance rule "no `luofu_*` module parameters / bench gates"
   cannot be met without stripping them.
3. The four new RFC entries (0027-0030): content candidates exist for 0027
   (032) and part of 0028 (036+043), but 046/059/060 are blocked.
4. Commit messages 0027-0030 (drafts exist in
   [`../wifi-upstream-target/submission-series.md`](../wifi-upstream-target/submission-series.md)),
   the generator extension and the full acceptance set - see
   [`ATTENDED-NEXT.md`](ATTENDED-NEXT.md).
