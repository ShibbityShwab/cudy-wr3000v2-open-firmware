# REPORT - wifi-rx-watermark (task-156): EAPOL/data RX free-ring refill watermark

**Lane:** wifi-rx-watermark, shared task task-156. **Host-only** - this lane
builds and tests on the host; the Lead fires the bench.  Nothing in this report
was measured on hardware.

**Evidence markers:** [V] from the committed sibling GPL sources /
public research (docs under `opensource/docs/incoming/`); [V*] from the local
vendor module disassembly recorded by earlier lanes (never committed); [L] live
bench result recorded by earlier lanes; [I] inference; [U] unknown/unpinned.

---

## 1. TL;DR

The rx-static drain (`rx_enable=1` ladder) refilled the two RX free rings only
inside a drain action and only up to the live-consumer delta of that action. A
completion backlog larger than one bounded action (`rx_drain_max`, default 64)
therefore **leaked posted free-ring entries permanently** - the classic iwlwifi
`RX_STALLED` precondition (free rings starve while returned buffers sit idle).
This lane adds a sustained refill watermark to the same drain path:

* new module parameter `rx_refill_wm` = minimum posted free entries as a
  percent of ring depth (**default 75 = >= 3/4 posted**, ath11k-style; `0` =
  off = old behaviour; `>100` clamps to 100);
* the drain, after its normal window refill, re-reads the live ring bank and
  re-publishes queued completed buffers until the ring is at capacity again
  (or the queue is empty), **bounded** by `depth - posted` and by the clamped
  `rx_drain_max` per ring per action and **fail-closed** on any anomaly;
* the early "no new completion" path also runs the pass, so a leaked ring is
  repaired with no new traffic (host-model case: posted 120 -> 184 -> 248);
* telemetry now classifies `CREDIT_LOW` (posted still below target after all
  returned buffers were re-published = the next action cannot keep up), the
  P3/H4 stall-classifier shape from the sibling lane, as log-only counters;
* control: with `rx_refill_wm=0` the leak reproduces exactly, proving the fix
  is gated and the diagnosis is the real one.

Final module rebuilt (assoc tree + corrected mgmt carrier + this patch):
**347,388 B, sha256 `d2c608e7...`, 57 params, 0 warnings.** Not fired.

---

## 2. The bug (why the old drain leaks posted entries)

The RX credit model of this MAC (rx-static lane [L]): the **only** RX enable is
the published free-buffer producer; the device consumes one posted entry per
received frame and returns the buffer through the completion ring. The host
must therefore keep re-publishing returned buffers to stay ahead.

Old `dp_rx_drain` commit sequence:

1. consume up to `rx_drain_max` validated completion entries; queue the
   returned buffers in a per-ring FIFO (`rx_refill_norm_q`/`_small_q`);
2. `dptx_rx_refill()` for each ring: `avail = consumed_delta(last_rptr, live
   rptr)`, re-publish `min(avail, queued)`, **then set `last_rptr = live rptr`**;
3. publish the completion-ring consumer.

The `last_rptr = new` reset consumes the whole delta window even if only
`queued` buffers were re-published. Any completion backlog beyond one action
(>64) is therefore drained in later actions with an **empty delta window**: the
returned buffers go into the FIFO, but the window they belong to was already
closed, so they can only be re-published if the device later consumes *more*
entries - which it cannot do faster than the host feeds it. The posted count
ratchets down.

Host-model trace (norm ring, depth 256, `rx_drain_max=64`; device consumes a
200-entry burst, then drains; the model is exact - see
`tools/rx_wm_selftest.c`):

| action | completions drained | queued after drain | posted after window refill | posted after watermark |
| --- | --- | --- | --- | --- |
| init | - | 0 | 256 | - |
| burst | 200 consumed | - | 56 | - |
| 1 | 64 | 0 | 120 | 120 (CREDIT_LOW) |
| 2 | 64 | 64 -> 0 | 120 | **184** (CREDIT_LOW) |
| 3 | 64 | 64 -> 0 | 120 | **248** (above 3/4) |
| 4 | 8 | 8 | 248 | 248 |
| ... | 0 | 8 | 248 | 248 (floor, not full) |

Control with `rx_refill_wm=0`: after the backlog is drained the ring is stuck at
**posted 120 / queued 136 forever** - the leak this lane fixes. Enabling
`rx_refill_wm=75` at runtime and writing `dp_rx_drain` once repairs it without
any new completion (184 -> 248, capped at 64/action).

Why it matters now: EAPOL msg2/msg4 and all client data are RX-ring frames
(sibling H1/H2 [V]), so the first association bench run is exactly the sustained
traffic shape that starves a leaking ring. The design note was already recorded
as the T4 item in
[../wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md](../wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md)
section T4, and the starvation risk as H4 in
[../wifi-sibling-gpl/REPORT.md](../wifi-sibling-gpl/REPORT.md).

---

## 3. Evidence basis (all committed, no new vendor artifacts)

The watermark number and shape come from a cross-check of the kernel drivers and
the two sibling HALs, already written up by the public-research-2 and
sibling-gpl lanes:

| source | fact used | where |
| --- | --- | --- |
| iwlwifi | RX write pointer publication is the enable; a wedged/starved RX ring enters `RX_STALLED` (the failure mode to avoid) | `../wifi-public-research-2/REPORT.md` section 6 [V] |
| ath11k | replenish from the refill ring in bounded batches; keep the fill level high (>= 3/4 in our reading) | same [V] |
| mt76 / ath10k / brcmfmac | fill/refill watermark + completion/used ring reap then refill/re-publish | same [V] |
| Hi1105 (GPL sibling) | dedicated **free-ring-empty ISR** schedules a refill of consumed free entries; refill is a first-class event, gated by MPDU-queue-full stall flags | `../wifi-sibling-gpl/REPORT.md` section 4.3, `TABLES.md` T4 [V] |
| Hi3881 (GPL sibling) | HCC RX high-waterline / drop policy, then refill | `TABLES.md` T4 [V] |
| local vendor module | no RX enable register/message beyond ring fill + producer publish; data-event handler refills free rings when stall flags are set | rx-static REPORT / sibling REPORT [V*] |
| our live lane | arm shows the live bank at CA 0x40050000 (status/mask/rptrs); the MAC had already consumed 0x20 prefetch entries [L] | `../wifi-rx-static/REPORT.md` |

Design consequence: do not invent a new enable message or RX filter (the T2
negative in sibling-gpl); the host's job is exactly *(a)* validate completions
by completion-entry buffer address, *(b)* re-publish returned buffers, *(c)*
publish the completion consumer. This lane makes *(b)* sustained and bounded.

---

## 4. Implementation

Patch: [patches/0001-rx-refill-watermark.patch](patches/0001-rx-refill-watermark.patch)
(409 lines, 3 files, applies cleanly to the final association tree **and** to
the association tree + corrected mgmt-carrier patch - disjoint files).

### 4.1 Portable rules - `hi5622_rx_live.{h,c}` (host-testable)

New section 4 of the RX model:

```
target  = depth * wm_pct / 100        (0 = disabled; >= 1 for 0 < wm_pct <= 100)
trigger = posted < target
refill  = min(avail, depth - posted, cap)   when triggered, else 0
```

* `posted` is the free-ring producer-consumer distance (the same packed
  15-bit index + phase model as the completion ring, so
  `hi5622_rx_ring_pending()` works in this direction too);
* `avail` is the number of returned buffers still queued;
* `depth - posted` is exactly the number of consumed-but-not-republished
  slots - a refill can never overwrite a posted entry;
* malformed inputs (posted/avail > depth, wm > 100, depth 0, NULL) return
  negative errors; nothing is clamped or guessed in the model (the driver
  clamps the parameter itself, see below).
* `HIL_RX_WM_PCT_MAX = 100`, `HIL_RX_WM_PCT_DEF = 75`.

### 4.2 Driver integration - `hi5622_dp_txrx.c`

* `rx_refill_wm` module parameter (uint, default 75). Values >100 are clamped
  to 100 at use: every direction of the clamp only reduces refills.
* New helpers `dptx_live_read_checked()` (live-bank read with the
  `0xffffffff` transport-miss refusal) and `dptx_rx_wm_ring()` /
  `dptx_rx_wm_refill()` (the per-ring pass and its two-ring wrapper).
* Drain commit order becomes:
  1. normal window refill (unchanged old path, kept as the fast path);
  2. **fresh live-bank consumer read**;
  3. **watermark pass** norm then small: if posted < target, re-publish up to
     `min(queued, depth-posted, rx_drain_max)` entries, `dma_wmb()`, publish the
     producer once per ring;
  4. publish the completion-ring consumer (unchanged last commit point).
* The early `pending == 0` path also runs the pass (bounded by the same
  `rx_drain_max`) before deciding "no pointer moved": this is what repairs a
  ring leaked by an earlier bounded drain with no new traffic.
* After a triggered pass, `last_*_rptr` is refreshed to the fresh consumer, so
  the next window refill's delta cannot double-count a slot the watermark pass
  already re-published. Ring entries written are always the producer slots;
  only buffers popped from the completion-returned FIFO are used (no live
  buffer is ever re-published).
* Fail-closed: any error from the portable rules, a `0xffffffff` live read, an
  out-of-range producer index or a failed producer publish goes through the
  existing `drain_refuse` path - it closes the RX lane for the load and never
  retries. With `rx_refill_wm=0` the pass returns before touching anything and
  the drain is the exact old rx-static code path.
* Gating is unchanged: `rx_enable=1` module load, a successful `dp_rx_arm`,
  and the `dp_setup`+TXQ-sync write gate are all required before a drain can
  write. A plain boot/autoload therefore cannot move RX pointers, whatever the
  watermark default is.

### 4.3 Telemetry (no new behaviour, read-only)

* the `DP rx_drain` dmesg line now includes
  `wm{pct,trips,credit_low,refilled{norm,small},posted{norm,small},target{norm,small}}`;
* `dp_rx_capture` gains an `RX watermark:` line:
  `rx_refill_wm=75% target{norm=192 small=384} posted{...} trips=... credit_low=... [CREDIT_LOW] refilled{...}`;
* `CREDIT_LOW` = after publishing everything returned, the ring is still below
  the watermark (the completion backlog is larger than one action can hand
  back). It is the sibling H4/P3 "starvation precondition" classification and
  is log-only: it does not close the lane. A growing `credit_low` on the bench
  is the signal to drain more often (or raise `rx_drain_max`), not to change
  the watermark.

---

## 5. Host verification ([tools/rx_wm_selftest.c](tools/rx_wm_selftest.c))

`./tools/run-host-tests.sh`: **127 checks, 0 failures, RESULT: PASS**
(full log: [evidence/host-test.txt](evidence/host-test.txt)). Strict C99,
`-Wall -Wextra -Werror -pedantic`, no kernel, no vendor data. Groups:

1. **Rule table with literal values** - 256/75 = 192, 512/75 = 384, 100% = full,
   33% floors, 1% = 2 slots, 0 = disabled, >100 / depth 0 / NULL refused;
   trigger boundary 191 vs 192; plan bounded by avail/room/cap; free-ring
   direction of `hi5622_rx_ring_pending` incl. one-lap phase cases.
2. **Ring state-machine model** - buffer states *posted / in-flight /
   queued-for-refill*, real packed-index helpers; every simulated drain checks
   the full partition invariant (no buffer twice, posted == produced-consumed,
   queue/in-flight counts match) so the fix cannot "work" by corrupting state.
3. **Burst recovery** - the trace in section 2 (120 -> 184 -> 248, CREDIT_LOW
   twice then clear, above-watermark queue left alone).
4. **Control** - `rx_refill_wm=0` reproduces the old leak exactly (posted 120 /
   queued 136 stuck) and the pass does nothing.
5. **Runtime enable repair** - leaked state repaired by one `dp_rx_drain` with
   no new completions (184 -> 248, cap observed), then stable.
6. **Sustained workload** - 400 deterministic steps with up to 64 consumed
   completions per action: ring returns to full after every action, no
   `CREDIT_LOW`, every invariant holds, every action's refills <= cap.
7. **One full lap** (256 consumed, phase bit flips): ring recovers to the 3/4
   target (192 posted, 128 watermark refills after the 64 window refills);
   **small ring** (512): 500-entry burst recovers to 396 posted (target 384)
   with 116 above-watermark buffers left queued - the floor-not-full policy.
8. **Plan grid property test** - 2 depths x 4 watermarks x 3 caps x posted x
   avail: refill always within `min(avail, depth-posted, cap)`, never
   nonzero without a trigger, trigger implies the exact expected value.

---

## 6. Build and artifact

See [BUILD-NOTES.md](BUILD-NOTES.md). Summary: final association tree
`251d08f0` + corrected mgmt-TX carrier patch + this patch, 0 warnings,
**57 params**, strip keeps `.text` byte-identical, all baseline feature strings
and cfg80211 imports retained, corrected carrier symbol
`himg_build_hcc_mgmt_tx` present.

| artifact | size | sha256 |
| --- | --- | --- |
| stripped (bake/fire this) | 347,388 B | `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6` |
| unstripped | 1,332,996 B | `f11835e8c47839d6ccba368ea64800912bfe5e93bd5ef5581f868b73ce858dc7` |

Independent rebuild at a different host path: `.text`, `.rodata` and modinfo
params byte-identical; only the 20-byte GNU build-id differs (path hash embedded
in the pre-strip DWARF). Quote the sha of the exact baked file.

---

## 7. Bench ladder and tuning (the Lead fires)

The attended rungs are in [TEST-LADDER.md](TEST-LADDER.md): default-off
fail-closed check, `rx_refill_wm=0` control, the primary `75` rung inside the
association/EAPOL run, and the `CREDIT_LOW` diagnostic rung. Stop rules are
unchanged: page `0x86807FEF/7/2/0xABCD`, mask 0, staging 0; any RX lane
"REFUSED"/closed line or exception ends the run with a power-cycle, no retry.

Tuning notes:
* `credit_low` staying 0 across the EAPOL burst = the ring kept >= 3/4 posted;
  that is the T4 PASS signal (with captures proving frames actually flowed).
* `credit_low` growing while the ring still drains means the device produced
  more completions between two `dp_rx_drain` writes than one action can return;
  drain more often (or raise `rx_drain_max`, still <= 64) - do not lower the
  watermark, the leak is on the completion side.
* If `posted` sits at the target while buffers remain queued (host-model small
  ring case), that is by design (minimum-free floor, not forced full); only a
  persistent below-target posted count is credit-low.

---

## 8. Limits and open questions

* **Not fired.** The whole combination is host-built; the association chain it
  rides is itself unproven on air (client association is currently blocked on
  RF/antenna and the flaky client NIC). A watermark PASS line cannot be claimed
  from this lane.
* The pass runs inside the sysfs-driven drain; there is still no RX interrupt /
  NAPI bottom half (deliberate: sibling P3 is log-only and the bench model is
  attended). Sustained-load behaviour between two drain writes is bounded by
  how often the operator writes `dp_rx_drain`; `credit_low` names that.
* Watermark 75% is the ath11k-style default chosen from public research, not
  measured on this chip; `rx_refill_wm` makes it a one-boot tuning knob.
* The sibling P1 robustness proposal (tolerate a producer-visible-before-entry
  completion window; clear-after-read) is **not** applied here - a stale/zero
  completion entry still hard-fails closed exactly as in rx-static. That is a
  separate, independently host-tested proposal
  ([../wifi-sibling-gpl/PATCH-PROPOSAL.md](../wifi-sibling-gpl/PATCH-PROPOSAL.md));
  combining them is a future lane.
* Ring sizes and the live-bank offsets are the rx-static model [V/L]; this lane
  changed no register, no constant and no enable semantics.
