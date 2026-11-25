# v2 review-form status - what the 013a fold did and did not unblock (2026-11-29)

**Question (task-171/(3)):** the 30-patch + DTS-0001 v2 review form was
"blocked on the 013a -> DT reset-gpios fold (+binding+DTS) and one attended
boot".  Does the fold, now committed, unblock the generation?

**Answer: no - the fold removes the last open content *decision*, but it does
not unblock generation.  Four things remain before v2 can be generated:**

1. **16 content folds are still unexecuted** (the 013a fold was 1 of 17).  They
   are real driver-content edits, not regeneration: see the table below.
2. **The fold pipeline needs a design decision.**  `tools/gen-series.py` maps
   one L0 source patch to one review-form entry and verifies the diff is
   byte-identical.  A fold merges content from several L0 patches into one
   entry, so either L0 must be rewritten (absorbed patches deleted, bench line
   re-validated) or the generator must gain an upstream-only composition source
   tree.  This decision was not needed for 013a (it folded into the one patch
   that already owned the file) but it is needed for every remaining fold.
3. **One attended boot** of the v4 active-DTS change is still owed (recovery
   armed; acceptance below).
4. **The v2 generator inputs and package must be rebuilt:** `SERIES` grows
   26 -> 30, four new commit messages 0027-0030 (drafts exist in
   `../wifi-upstream-target/submission-series.md`), GROUPS/mapping/cover letter
   updates, full host acceptance re-run (compile, dt_binding_check, image
   chain) and the submission package regeneration.

The 30-patch form itself is well specified (ordered list, per-patch sources,
commit-message drafts, acceptance criteria) in
[`../wifi-upstream-target/submission-series.md`](../wifi-upstream-target/submission-series.md)
and the 67-patch verdict table in
[`../wifi-upstream-target/PATCH-CLASSIFICATION.md`](../wifi-upstream-target/PATCH-CLASSIFICATION.md).
This page is the honest delta between that plan and today.

## 1. The remaining 16 folds (execution checklist)

"L0 source" is the current lab patch (line counts from the 66-file stack);
"target v2 entry" is where the content must end up.  Every one needs the
functional hunks kept, the bench gates/parameters either dropped or turned
into DT state, the read-only diagnostics left out, and the commit message
updated with the [V]/[U] evidence grade (acceptance rule: no `luofu_*` module
parameters / bench gates in the review form).

| # | L0 source (lines) | target v2 entry | what executing the fold means |
| --- | --- | --- | --- |
| 1 | 027-arm-clk-luofu-readonly-mux-fix (23) | 0019 clock tree | fold the 6.18 clk-core read-only mux fix into the clock-tree patch; drop the separate entry |
| 2 | 031-net-lsw-eps-channel-map (143) | LSW init RFC group (0016/0021/0022/0025) | fold the EPS channel-map init into the switch bring-up table set |
| 3 | 032-net-lsw-eqm-qminit (1366) | **new RFC 0027** EQM queue-manager | turn the gated W-bench EQM table programming into a normal-boot init (gates -> none/DT), new patch + message |
| 4 | 036-net-lsw-pfe-init (357) + 043-net-lsw-ifc-default-table (354) + 046-net-lsw-ifc-fix-eps-ext (333) | **new RFC 0028** PFE/IFC forwarding tables | merge three gated init stages into one normal-boot init, new patch + message |
| 5 | 038-net-gmac-maxframe-init (139) | 0009 GEMAC | fold the max-frame register setup (vendor formula), drop the W9 gate parameter |
| 6 | 041-net-gmac-macconf-nodoorbell (161) | 0009/0023 GEMAC | fold the functional MAC-config replay, drop the doorbell bisection gates |
| 7 | 043 (see row 4) | 0028 | (merged above) |
| 8 | 044-net-lsw-ifc-gate-token-fix (254) | 0022 qid-map write path | adopt the final `luofu_switch.*` option names into 0022 |
| 9 | 046 (see row 4) | 0028 | (merged above) |
| 10 | 048-net-pie-rxcfg-w13 (114) | 0020 PIE ring engine | fold the RXCFG RX-enable word into the engine init |
| 11 | 051-net-gmac-macregs-w16 (137) | 0009/0023 GEMAC | fold the static MAC configuration words |
| 12 | 053-net-pie-rxdecode-vendor-exact (125) | 0024 PIE RX completion decode | make the vendor-exact decode the implementation (not a follow-up) |
| 13 | 059-net-lsw-napt-init (431) | **new RFC 0029** NAPT flow engine | turn the gated NAPT init into a normal-boot init, new patch + message |
| 14 | 060-net-pie-exception-delivery (70) | **new RFC 0030** PIE exception RX | deliver native exception-class RX frames, retire the accepterr bench option |
| 15 | 066-thermal-luofu-tvsensor-w23 (549) | 0012 thermal | fold the v1/hwver-3 read path + gated enable/trim into 0012 |
| 16 | 067-dt-bindings-thermal-luofu-tvsensor-w23 (114) | 0012 binding | fold the binding window/property update into 0012 |

The four new RFC entries (0027-0030) come from the class-B functional bench
patches 032/036+043+046/059/060; their commit-message drafts with explicit
unverified lists are already in `submission-series.md`.  The 24 DROP patches
stay out of the review form and out of the submission (they stay in the lab
line for bench use).

## 2. The fold-pipeline decision (the concrete blocker)

Today's guarantee is: **every review-form `series/NNNN` diff body is
byte-identical to exactly one L0 patch** (`gen-series.py` asserts it).  A fold
breaks that one-to-one mapping.  Two implementable options:

- **Option A - rewrite L0 and delete the absorbed patches** (matches the
  "make the fold decisions in the target patches first" instruction).  The L0
  lab stack changes from 66 files to roughly 54 (16 absorbed patches deleted,
  four new RFC source patches added); the release line L1 and the
  bench-validated image must be re-derived, the L1 validator re-run and the
  bench line **re-booted** before the lab stack is trusted again.  Single
  source of truth stays; the full kernel build must be re-run.
- **Option B - add an upstream-composition source tree** (e.g.
  `target-skeleton/upstream/patches-6.18-v2/`) that the generator reads, while
  L0/L1 stay frozen for the bench line.  No hardware churn, but the folded
  driver source then exists in two places and drift must be policed by a new
  validator check (the generator would have to assert semantic equivalence of
  the overlapping content, not just bytes).

Recommendation: **Option A**, executed in a dedicated lane with its own full
compile + L1 re-validation + one attended boot, because the review form must
describe the driver the project actually intends to merge, and the two source
trees of Option B are a drift risk for exactly the kind of driver this is
(hand-maintained hardware workarounds).  Either way it is a lane-sized task,
not a regeneration.

## 3. The one attended boot (v4 active-DTS change)

The fold moved the endpoint reset lines from hardcoded driver writes (013a,
hardware-validated as GP1 on 2026-11-24) to the board DTS `reset-gpios`
property + GPIO-consumer code.  Per project rule, **every active-DTS change
needs one attended boot before it is trusted**.  Acceptance for that boot
(recovery armed, Lead fires; this lane does not touch the device):

- 2 CPUs up, serial console, NAND/UBI mounts, no Oops/BUG/panic;
- dmesg shows the new `endpoint reset pulse on 3 board line(s): 20000 us
  asserted, 100000 us settled` line from pcie-luofu on RC0 probe;
- both PCIe root complexes train (LTSSM L0) and both Wi-Fi endpoints enumerate
  (vendor/device `59e7:0005` at bus 0/dev 0/fn 0 of each domain);
- GPIO0 pins 20/21/28 settle released (high) after the pulse;
- a stock-boot control is not required (the line was already proven by GP1);
  the boot only has to prove the DT-driven equivalent.

Until that boot, GP1 stays the hardware-validated line and this fold's
devicetree half is host-validated only.

## 4. v2 host acceptance criteria - current status

From `submission-series.md` section "Acceptance criteria" (v1 rows marked with
the v4 evidence):

```
Host (mechanical)
[ ] 30-patch series applies to pristine 6.18.60 with patch(1) and git am, identical trees
    v1: met (26/26); v4 re-ran PASS 26/26 for the folded form
[ ] checkpatch.pl 0 errors, warnings triaged (0 for new files)
    v4: 0 errors / 73 warnings (unchanged; the fold adds none)
[ ] full kernel compile RC=0 (OpenWrt GCC 14.4.0), W=1 clean for touched files
    v4: only the folded driver object was compiled (W=1, RC=0); full build owed for v2
[ ] dt_binding_check RC=0 for every binding; dtc W=1 clean for the DTS
    v4: dtc clean via validator; dt_binding_check NOT re-run (dtschema absent) - owed for v2
[ ] target image chain builds host-side from exactly this series
    not attempted in v4 (last target-tree isolated build: 2026-11-24)
[ ] gen-series.py --check clean; validation/sha256sums regenerated
    v4: clean (26); sha256sums refreshed; v2 needs its own refresh

Design (review)
[ ] no luofu_* module parameters / bench gates in the submission patches
    only 0013 is fold-complete; the 16 remaining folds are exactly where this is still false
[ ] no debugfs/printk diagnostics left behind
    DROP list stays out; the remaining folds must remove the class-B/C diagnostics they absorb
[ ] every compatible has a reviewed binding and a documented evidence grade
    v4: PCIe binding documents reset-gpios/reset-assert-us/reset-deassert-us; others unchanged
[ ] vendor-derived tables described by derivation method, not dumped
    still owed for the 0027-0029 LSW init entries
[ ] reset/clock/power claims limited to the verified subset
    v4: PCIe reset claim scoped to the hardware-validated pulse + host-validated DT form
[ ] MAINTAINERS entry and tree-placement rationale updated
    v2 work

Hardware ("do not send a claim you have not measured")
[ ] every active-DTS change boot-tested on the unit
    v4 fold: NOT yet (this is the one attended boot above)
[ ] platform group boots the unit, 2 CPUs, serial, NAND/UBI
    GP1 (2026-11-24) is the last full boot of a line with the pulse; v4 DTS form untouched
[ ] PCIe/thermal/pinctrl/GPIO feature exercised, unverified paths listed
    PCIe: link + both endpoints [V] (2026-11-22/24); thermal: W23 bench-verified
[ ] PIE/LSW RFC group: no traffic/support claim; gate documented as bring-up tooling
    v2 content work
[ ] SMP patch only with DTS-0001 carrying the two loader hand-off nodes
    v4 DTS-0001 carries them (contract re-checked by the L1 validator)
```

## 5. Ordered next steps (for the lane that executes v2)

1. Decide the fold pipeline (section 2).  If Option A: renumber/new-file plan
   for L0, then execute folds 1-16 in the L0 patches, **with the hardware
   evidence and the gate removals**, updating each commit message.
2. Add commit-messages/0027..0030 from the drafts; extend
   `gen-series.py` SERIES/GROUPS and the mapping document.
3. `python3 tools/gen-series.py` then `tools/run-validation.sh` on pristine
   linux-6.18.60; expect 30/30 patch(1) and git am, identical trees, 0 errors.
4. Regenerate DTS-0001 from the (possibly unchanged) single-source DTS; run
   `check-release-line.sh` on the new L1 (if Option A changed L0/L1).
5. Run the full acceptance set: full kernel compile RC=0 + W=1, dt_binding_check
   RC=0, dtc W=1, target image chain, `gen-series.py --check`, refresh
   `validation/sha256sums.txt` and `submission/MANIFEST.sha256`, refresh the
   submission README/cover-email draft.
6. One attended boot of the active-DTS change (section 3), then the owner
   posting decision.  Wi-Fi stays out of scope; PD4-patched firmware never
   ships; the GPL request is unanswered.

## 6. What this lane did NOT do (deliberate)

No content fold other than 013a; no L0 rewrite; no v2 generation; no full
compile; no dt_binding_check (dtschema unavailable); no attended boot, no
device/bench contact; no posting, no push, no release decision.
