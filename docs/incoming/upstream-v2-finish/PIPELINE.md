# The fold-pipeline decision - evidence and a concrete mechanism (2026-11-29)

[`../upstream-v4/V2-REVIEW-FORM-STATUS.md`](../upstream-v4/V2-REVIEW-FORM-STATUS.md)
section 2 states the blocker: `tools/gen-series.py` guarantees that every
review-form entry is **byte-identical to exactly one L0 patch**, and a fold
merges several L0 sources into one entry.  The v4 lane recommended **Option A**
(rewrite L0, delete the absorbed patches) without having executed a fold.
This lane executed them host-side; the evidence below revises that
recommendation.  The lane does **not** make the decision - it is an owner
decision because it changes the generator contract - but the mechanism is
ready.

## Evidence from executing the folds

1. **10 of 16 folds compose directly on the frozen v1 review form** when
   applied in L0 order, with nothing forced
   ([`validation/fold-sequence.md`](validation/fold-sequence.md)): 027, 031,
   032, 036, 038, 043, 044, 051 (with `-C1`), 066, 067.  The composed deltas
   are in [`artifacts/fold-overlays/`](artifacts/fold-overlays/); one entry
   fold (027 -> 0019) was executed and validated end-to-end
   ([`FOLD-EXECUTION.md`](FOLD-EXECUTION.md)).
2. **6 folds do not compose** (041, 046, 048, 053, 059, 060).  The blocker is
   not the v1 selection: their hunks/context interleave with the **DROPped
   bench diagnostics** (040, 045, 047, 052, 055, 057) and with each other
   (041 -> 048, 046 -> 059, 053 -> 060).  The dependency roots are attributed
   hunk-by-hunk in [`FOLD-EXECUTION.md`](FOLD-EXECUTION.md).  Manual
   re-contextualization is required **under either option** - the same six
   sources have to be rewritten once for the review form.
3. Therefore the real cost of the fold work is: 6 re-contextualizations +
   the gate/placement decisions for the composing folds + the 4 new RFC
   entries.  That cost is **identical** for Option A and Option B.

## Option A - rewrite L0 (v4's recommendation)

What it actually costs, given the evidence:

- 16 L0 files are deleted/absorbed and 4 new ones added (L0 66 -> 54).
  Their consumers in the L0 stack then have different context: the 24 DROP
  patches are interleaved with the FOLD sources (e.g. 040 follows 038, 045
  follows 043, 047/052 follow 048, 055/057 follow 053, 058 follows 057), so
  those 24 patches must be re-contextualized or deleted.  Deleting them
  threatens the bench-image reproducibility and the L1 release line that the
  bench baseline was validated with.
- L1 must be re-derived and re-validated (55-patch manifest, 0 fuzz goal),
  the image recipe re-run, and by project rule the re-derived bench line needs
  a bench boot before it is trusted again.
- The six blocked folds still need the same manual re-contextualization.
- Net: the content work **plus** a bench cycle.  The v4 status estimated this
  as "a lane-sized task"; the probe shows it is materially larger.

## Option B - upstream-composition source tree (recommended)

Keep L0/L1 frozen for the bench line; add a composition layer that the
generator reads.  This lane already produced the raw material (overlays +
one executed entry fold), so the mechanism below is enough to start.

### Mechanism

New directory under `opensource/upstream/series-6.18/`:

```
composition/
  manifest.tsv          # entry -> source kind -> sha256 -> expected tree hash
  overrides/0019-*.patch # folded entry diff body (replaces the L0-derived body)
  overrides/0012-*.patch # only after the 066/067 gate decision
  appends/0027-*.patch   # new RFC entries (after 0026)
  appends/0028-*.patch
  appends/0029-*.patch
  appends/0030-*.patch
  gates.tsv             # every remaining module_param and its decision state
```

Generator change (`tools/gen-series.py`):

1. `SERIES` grows to the 30 ordered stems; for each stem resolve the diff
   source as: `composition/overrides/<stem>.patch` if present, else
   `composition/appends/<stem>.patch` if present, else the L0 patch.
2. Drop the "diff body must equal one L0 patch" assertion for overrides and
   appends; replace it with:
   - every output entry applies in order on pristine linux-6.18.60;
   - the **composed tree hash** after applying all entries equals the hash
     recorded in `manifest.tsv` (single source of truth for the form);
   - the mapping table records source kind (`L0`, `folded`, `new`) and the
     L0 sources that were folded, so review traceability is preserved;
   - `gates.tsv` is checked: any `luofu_*` `module_param` still present in a
     patch must have a row explaining why (acceptance check becomes
     mechanical).
3. Drift check `--check` compares the regenerated files + composed tree hash;
   the old per-patch "byte-identical to L0" check remains for unmodified
   entries.

### Why B

- No L0/L1 change, no bench re-boot, no threat to the reproducible bench
  image; the 6 re-contextualizations are the same work either way.
- The two-source-tree risk is managed by a single composed-tree hash plus
  the mapping table; the old byte-identity invariant is replaced by a
  stronger outcome invariant (the composed tree), and unmodified entries keep
  the old check.
- Option A remains available later as a consolidation step once the v2
  content is settled and reviewed; doing it now pays the bench cost before
  the content is frozen.

### Residual risks (to record in the manifest)

1. Overrides are re-contextualized by hand for 6 folds; a reviewer must diff
   the override against its L0 source(s) (the mapping table lists them).
2. Overlay 051 composes only with `-C1`; its override must be re-derived
   manually, not taken from the overlay.
3. If the owner chooses to keep any bench gate (K2), `gates.tsv` records it
   and the acceptance check allows only the listed exceptions.
4. The composed-tree hash is a *host* invariant only; it says nothing about
   hardware behaviour.  The one attended boot and the per-feature bench
   gates stay as stated in [`ATTENDED-NEXT.md`](ATTENDED-NEXT.md).

## Recommendation

Adopt **Option B**; the v4 doc's Option A is more expensive than it looked
and would re-open the bench line before the v2 content exists.  Keep this page
as the decision record and update `V2-REVIEW-FORM-STATUS.md` section 2 when
the owner signs off.
