# wifi-ba-tx-impl - the BA TX-direction cut: the in-kernel ADDBA + the g0 0x0A/0x0B/0x1C sends

**Lane:** `wifi-ba-tx-impl`, shared task **task-217**, 2026-11-26.
**HOST-ONLY**: this lane built and host-tested the module; it never contacted
the router, never fired, never flashed. **The Lead fires.**

**Base:** the current merged-module base - the **F2/R5 lineage** (the 5 GHz
chain tip + the R4b/R5 order, 65 params; the R5 fire module `24f7221b`,
383,240 B; the tree `/tmp/r5-build`). Pins in
[BUILD-NOTES.md](BUILD-NOTES.md).
**Starting point:** the committed BA skeleton (`../wifi-ba-agg`, task-178,
artifact `78460841...cc33de02`, 183 host checks): its portable
`hi5622_ba.[ch]` + fail-closed `ba_enable` are ported here and extended; the
skeleton was **provably unwired**, this lane **wires it**.

## What this lane delivers

The minimal **BA-1 TX-direction** cut recommended by the `wifi-ba-agg` lane:
the AP is the **TX originator** of the block-ack agreement, and the host sends
the firmware the three decoded H2D messages:

| message | wire | role |
| --- | --- | --- |
| g0 id **0x0A** BA relay | **44 B** (12 + 32) | ADDBA-response relay (subtype 1) / DELBA relay (subtype 2) - programs the firmware per-TID BA context |
| g0 id **0x0B** A-MPDU start/end | **20 B** (12 + 8) | the aggregation-window boundaries on the TX path |
| g0 id **0x1C** TX BA notify | **32 B** (12 + 20) | informational state notification (0 = pending->active, 2 = teardown) |

plus the **in-kernel ADDBA negotiation** (no userspace help: stock hostapd has
none of the vendor WAL commands): the AP sends the 33-B ADDBA request, accepts
the 33-B ADDBA response, sends the 30-B DELBA on teardown - all through the
**proven mgmt-TX carrier** (HCC g0 id 6, 48-B control + inline 802.11 frame).

The cut is **fail-closed**: `ba_enable=0` (default) means every entry point is
inert - no frame, no H2D message, no state change. `ba_enable=1` arms it;
`ba_enable>1` is refused (`-EINVAL`).

## Deliverables

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | architecture, the negotiation sequence, the wire/group facts (incl. the vendor group-2 notify anomaly), the implementation, the fail-closed matrix, limits |
| [TABLES.md](TABLES.md) | T1-T8 evidence tables: the replay-verified wire KATs, the group-byte decode, the firmware handlers, the H2D post gate, the state machine, the probe pack |
| [BUILD-NOTES.md](BUILD-NOTES.md) | the exact chain/pins, the artefact contract, the host tests, reproduce, apply-on-another-base |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the Lead's fire: B1 (already passed) / **B2 (evaluable now: ba_enable=1, no peer)** / B3 (blocked on the RX gate), exact PASS/FAIL/abort rules |
| [patches/0001-ba-tx-impl.patch](patches/0001-ba-tx-impl.patch) | the whole change on the pinned F2/R5 base: 4 new files + 6 integration edits |
| [src/hi5622_ba.{c,h}](src/) | portable core (ported + extended): complete wires, negotiation, TX-path originator step, notify, stats |
| [src/hi5622_ba_glue.{c,h}](src/) | kernel binding: gate, `hi5622_frame_post()` sends, g0 id6 action carrier, deferred D2H/mgmt/TX workqueue, AP binding, status line |
| [src/ba_tx_selftest.c](src/ba_tx_selftest.c) | the host model (ported 183-check skeleton + extended): **211 checks** standalone, **225** with the driver's H2D decode gate |
| [tools/build-ba-tx.sh](tools/build-ba-tx.sh) | full host chain: base pins -> patch -> module build -> artefact contract -> host tests |
| [tools/apply-integration.py](tools/apply-integration.py) | the reviewable exact-match integration edits (patch regeneration) |
| [tools/ba_tx_frame_test.py](tools/ba_tx_frame_test.py) | independent Python re-derivation of the wires + the source contract: **99 checks** |
| [tools/run-host-tests.sh](tools/run-host-tests.sh) | the C host tests against a built tree |
| evidence/ | build/artefact pins, host-test and contract-test transcripts (host-only) |

## Status (2026-11-26)

* **module builds clean** against linux-6.18.60: **406,168 B** debug-stripped,
  sha256 **`234b3d4b...0104d841`**, **66 params** (`ba_enable` read-only
  0444 default 0), 0 build warnings, `.text` sha256 `70259123...20ec8fc`,
  vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, `depends=cfg80211`;
* **host tests: 535 check executions, 0 failures** (the C model 211
  standalone and 225 with the driver's H2D gate - the same source compiled
  twice; the independent Python contract test 99);
* **provably wired** (unlike the skeleton): `hi5622_ba_glue_d2h`,
  `hi5622_ba_glue_mgmt_rx`, `hi5622_ba_glue_tx_data`, `hi5622_ba_glue_ap_up`
  and `hi5622_ba_glue_status_line` each have a call site in the driver's own
  paths and a symbol in the `.ko`;
* **nothing fired, no device contacted.** The only currently-evaluable fire
  rung is **B2** (load with `ba_enable=1`, 2.4 GHz AP, **no peer** -> zero BA
  frames/messages, page golden). The real BA session rungs are gated behind
  the RX gate (the firmware-internal RX thread): with no client able to
  associate, no agreement can be negotiated end-to-end yet.

## One-line reproduce

    opensource/docs/incoming/wifi-ba-tx-impl/tools/build-ba-tx.sh

## Related lanes

`../wifi-ba-agg` (the decode + the skeleton this lane ports and wires),
`../wifi-tbtt-timer` (the 5 GHz residual: the firmware-internal SMAC/TBTT
timer), `../wifi-rx-h2d-trace` / `../wifi-f1-from-capture` (the RX gate: the
F1 is not a missing H2D frame), `../wifi-ap-mgmt` (the mgmt-TX carrier this
lane reuses for the action frames), `../wifi-data-tx` (the AP data-TX path
that triggers the negotiation), `../wifi-image-3` (the merged-module
inventory this base lineage comes from).
