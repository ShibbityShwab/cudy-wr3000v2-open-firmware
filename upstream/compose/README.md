# Upstream composition descriptors (the Option B fold mechanism)

<!-- SPDX-License-Identifier: GPL-2.0-only -->

**Status:** scaffold 2026-11-27 (goal round 70). This directory implements the
recommended option from
[FOLD-PIPELINE-DECISION.md](../upstream-v4/FOLD-PIPELINE-DECISION.md): the v2 review
form's folds are review-side compositions over the unchanged L0 bench stack.

## Why descriptors

The generator (`../series-6.18/tools/gen-series.py`) maps one L0 patch to one review
entry with a byte-identical guarantee. A fold merges several L0 patches (and drops bench
gates/diagnostic hunks) into one entry - a many-to-one mapping the 1:1 generator cannot
express. A descriptor states the composition explicitly, and the generator's `--compose`
mode builds the entry from it and verifies the result against the recorded hash.

## The descriptor format

One JSON file per composed review entry, named after the entry stem:

```json
{
  "entry": "0027-net-lsw-eqm-qminit",
  "message": "commit-messages/0027-net-lsw-eqm-qminit.txt",
  "status": "RFC",
  "pieces": [
    {"patch": "032-net-lsw-eqm-qminit.patch", "hunks": "all"},
    {"patch": "027-arm-clk-luofu-readonly-mux-fix.patch", "hunks": [2, 3]}
  ],
  "drop": [
    {"pattern": "luofu_lsw_gate", "reason": "bench gate - the fold rules require dropping it"}
  ],
  "expected_sha256": "<the generated .patch sha256, recorded after the first accepted generation>"
}
```

The fields:

- `entry` - the review-form stem (must match the `SERIES` entry it replaces);
- `message` - the commit-message path relative to `../series-6.18/`;
- `status` - the review status label (`RFC` / `ready-for-review`);
- `pieces` - the ordered L0 sources; `hunks` is `"all"` or a list of 1-based hunk
  indices of that patch's diff;
- `drop` - the textual drops applied to the composed diff (the bench gates/parameters
  and the diagnostic-only lines the fold rules exclude); each drop is a literal
  `pattern` (a substring that must match exactly one line-run) with a `reason`;
- `expected_sha256` - the composed output's hash; the generator fails closed on a
  mismatch. Recorded empty until the first accepted generation.

## The generator contract (`--compose`)

1. For each descriptor whose `entry` is in `SERIES`, assemble the diff from the `pieces`
   (the selected hunks of each source patch, in order, with the file headers merged
   when consecutive pieces touch the same file).
2. Apply the `drop` patterns to the assembled body; every pattern must match or the
   composition fails closed.
3. Prepend the message's mail header exactly as the 1:1 path does (the same `IDENT`,
   `DATE`, the index and the `SERIES` length).
4. Verify `expected_sha256` when recorded; on a first generation, write the descriptor's
   hash back and mark the entry `unverified`.
5. `--check` covers both paths: the 1:1 entries keep the byte-identical verification,
   the composed entries verify against their recorded hashes.

## Current state

The directory is a scaffold: no descriptors exist yet. The 16 folds of the
V2-REVIEW-FORM-STATUS checklist are the first users. The generator's `--compose` mode is
the next implementation step (the hunk-selection engine + the drop application + the
merged file headers), and each descriptor is written together with its fold.
