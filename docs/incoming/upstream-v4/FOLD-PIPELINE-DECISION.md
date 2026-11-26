# The fold-pipeline design decision (the v2 review form's blocker)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** decision record, 2026-11-27 (goal round 69). This page answers the design
question the V2-REVIEW-FORM-STATUS raises: the v2 review form needs 16 content folds,
and the current generator cannot express a fold. The options, the tradeoffs and the
recommendation follow.

## The current model (why folds do not fit)

`upstream/series-6.18/tools/gen-series.py` maps **one L0 source patch to one review-form
entry**: it reads the unified diff verbatim from
`target-skeleton/target/linux/hisilicon/patches-6.18/<NNN-...>.patch`, prepends the
mail header from `commit-messages/<NNN-...>.txt`, and verifies the generated diff body is
**byte-identical** to the source. The 26-entry `SERIES` list and the status labels
(`RFC` / `ready-for-review`) are part of the generator.

A fold merges content from several L0 patches into one review entry (e.g. the
read-only-mux fix 027 into the clock-tree entry 0019, or the three LSW/IFC patches into
one PFE/IFC RFC). The generator has no concept of a many-to-one mapping, so it cannot
produce a fold without a design change.

## Option A - rewrite L0 (absorb the folded patches)

Merge the L0 patches themselves: delete the absorbed patches, keep the bench line
byte-identical in behavior, and let the generator's 1:1 mapping continue to hold.

- **Pro:** the generator stays trivial; the byte-identical guarantee survives.
- **Con:** the L0 stack is the **bench line** - the 66-patch stack that the release
  images were built and fired from. Rewriting it invalidates the fired-image hashes and
  requires re-validating the whole lab chain (the pads, the FIP slice, the image chain).
  It also destroys the per-milestone traceability the ledger leans on.

## Option B - an upstream-only composition source tree (recommended)

Add an `upstream/compose/` directory holding one **composition descriptor per review
entry**: the entry's commit message plus an ordered list of (L0 patch, hunk selector)
pieces. The generator gains a second mode: for entries with a descriptor, assemble the
entry from the selected L0 hunks (applying the bench-gate/diagnostic drops the fold
rules specify), then verify the result against the descriptor's expected output hash.
Entries without a descriptor keep the current 1:1 byte-identical path.

- **Pro:** the L0 bench line is untouched (the fired-image hashes stay valid); the folds
  are review-side artefacts only; the composition is explicit and reviewable; the
  generator's 1:1 path and its byte-identical guarantee remain for the 10 unfolded entries.
- **Con:** the generator grows a real hunk-selection engine (a few hundred lines with the
  parser + the expected-output hashes), and every fold needs its descriptor written.

## The recommendation

**Option B.** The L0 stack is evidence-bearing bench material: rewriting it to satisfy a
review-form convenience would invalidate the fired-image hashes and the per-milestone
record. Option B keeps the evidence intact and puts the fold logic where it belongs - in
the review-side composition layer. The 16 folds then become 16 descriptors, each with the
fold rules from the V2-REVIEW-FORM-STATUS checklist, and the generator's `--check`
mode verifies every composed entry against its recorded expected hash.

## The implementation sketch (Option B)

1. `upstream/compose/<entry>.json`: the commit-message path + the ordered pieces
   `[{patch, hunks: [..], drop: [..]}]` + the expected output sha256.
2. `gen-series.py` gains `--compose`: for a descriptor entry, apply the selected hunks,
   apply the drop list (bench gates/parameters, diagnostic-only hunks), prepend the
   message, verify the sha, write.
3. The `--check` mode covers both paths (the 1:1 byte-identical entries + the composed
   entries' expected hashes).
4. The 013a fold already in review-form 0013 is the template: it folded into the one
   patch that already owned the file, so it needs no descriptor - the 16 remaining folds
   are the first users of the mechanism.

## The consequence for the v2 form

With Option B chosen, the v2 generation becomes: (a) execute the 16 folds as descriptors
(the driver-content work), (b) generate, (c) run the full host acceptance (the compile,
the dt_binding_check, the image chain), (d) the one attended boot of the v4 active-DTS
change, (e) the package regeneration. The decision removes the *pipeline* blocker; the
16 folds and the attended boot remain the real work.
