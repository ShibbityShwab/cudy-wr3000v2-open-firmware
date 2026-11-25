# wifi-rx-completion-phase - the completion-ring consumer phase (task-168)

**Lane:** `wifi-rx-completion-phase`, shared task **task-168** (host-only; candidate 2 of the final
RX-capture hunt). **Status:** closeout report, written after the lane's artifacts were committed and
the bench fire had run. **Scope:** completion-ring consumer/phase decode + a fail-closed one-shot
`prepublish` rung + host tests + the fire record; the Lead fired, this lane never contacted the
bench.

**Result in one line:** the completion-ring consumer **phase** is real and the device's completion
accounting responds to it - the phase-1 prepublish moved the host-MAC status word from
`0x00000182` to `0x00000186` (**bit 2, RX-complete, observed for the first time**) - but **no frame
was written** (`frames=0`, completion ring empty). The completion-consumer phase is therefore
**not** the capture blocker.

Committed artifacts: `src/hi5622_rx_cmp_phase.{h,c}` (portable model), `patches/0001-rx-cmp-phase-prepublish.patch`
(kernel glue), `tools/build-prepublish-module.sh` (reproducible build), `tools/run-host-tests.sh` +
`tools/rx_cmp_phase_selftest.c` (device-free test). The lane died before writing this report; the
Lead built and fired the module. Honest gaps in section 8.

---

## 1. The question this lane was given

The RX-hunt state before this lane: the host-side RX static aspects were vendor-correct (ring/buffer
format, CRX gating, the vendor's 12-write enable replicated byte-for-byte, device-owned live-bank
state matching stock), yet the MAC never wrote a frame into host memory and the sticky bit-10
status fired when frames arrived. One host-written word had never been modelled as a *phase*
quantity: the completion-ring **consumer**.

The vendor ring library publishes a packed 16-bit pointer = `index[14:0] | phase << 15`
(`hal_ring_set_sw2hw` 0x31f1c, type 2 = completion ring -> a 32-bit store to the host-interface
config word `+0x40`); the consumer advances with a phase toggle on wrap, and the device producer is
read from the live bank `+0x10`. The vendor's **initial** completion consumer is `0x0000`
(`shuangta_rx_host_init_dscr_queue` 0x37888 zeroes the 0x28-byte completion-ring struct and never
writes `+0x40`; the committed vendor stock dump reads cfg `0x40040040 = 0` and live
`0x40050010 = 0`), while the *free-ring* producers publish `0x8000` after the fill.

Task: test whether the device's DMAC requires a non-zero completion-consumer phase before it
accepts its first completion write - a one-shot, pre-arm, fail-closed experiment: publish
`0x8000` (index 0, phase 1) to the completion consumer before the RX arm, watch the status word
for a device reaction, and restore the vendor initial `0x0000` if rejected.

## 2. The decoded vendor semantics (pinned in the model)

| fact | value | source |
| --- | --- | --- |
| packed pointer | `index[14:0] \| phase<<15` | `hal_ring_set_sw2hw` / `hal_ring_set_entries` (vendor host module) [V] |
| completion depth (band 0) | 4095 entries | vendor stock wiring / rx-format geometry [V] |
| completion consumer word | host-interface config `+0x40` (CA `0x40040000`) | vendor setters [V] |
| device producer word | live bank `+0x10` (CA `0x40050000`) | vendor `hal_ring_get_hw2sw` [V] |
| phase toggle | on index wrap (index -> 0, phase flips) | `hal_ring_set_entries` / `hal_ring_get_entries` [V] |
| pending arithmetic | phase-aware producer-consumer distance; equal index + opposite phase = one full lap (pending = depth) | `hal_ring_get_entry_count` 0x31f80 [V] |
| vendor initial consumer | **`0x0000`** (index 0, phase 0) - not `0x8000` | init function + committed stock dump [V] |
| free-ring full publish | `0x8000` (index 0, phase 1 = ring full) - a different ring | stock dump `+0x38/+0x3c = 0x8000` [V] |
| experiment value | `0x8000` (index 0, phase 1): to a phase-aware consumer this looks like **one full lap ahead** of a zero producer | model + selftest [V] |

The last row is why the rung is split: the prepublish itself makes the ring look full to a
phase-aware device, so the first fire must be **no-traffic** and must observe the status word. A
device that does not accept the phase rejects it visibly (status bits 7/10 class) or leaves the
ring state inconsistent; only a clean no-reaction run permits the traffic rung.

## 3. The implementation (kernel glue + model)

### 3.1 The portable model (`src/hi5622_rx_cmp_phase.{h,c}`)

Original, device-free, kernel-header-free C (compiles for both host and `__KERNEL__`). It owns every
decision; the kernel glue only performs the one 32-bit store:

- `rxc_pack`/`rxc_unpack`/`rxc_next`/`rxc_advance`: the vendor packed-pointer arithmetic with the
  wrap phase toggle (fail-closed on a bad depth);
- `rxc_pending`/`rxc_ring_state`: the phase-aware count (equal index/opposite phase = full lap);
- `rxc_prepublish_gate`: the ordered gate chain -
  **DISABLED** (param 0; fail-closed default) -> **NO_RX_ENABLE** -> **NOT_READY** (bind/transport/
  write gate down) -> **ARMED** (the arm already ran; the prepublish must be before it) ->
  **ALREADY_DONE** (one-shot per load) -> **LANE_FAILED** -> **CONSUMER_DIRTY** (cfg `+0x40` is not
  the vendor initial `0x0000`; never overwrite a moved consumer) -> **ALLOW**;
- `rxc_store_value`: exact-width store - keep the upper 16 bits, replace the low 16 (the vendor
  setters are 32-bit accesses with a zero-extended u16 value in the stock state);
- `rxc_readback_ok`: the read-back must carry the requested packed value in `[15:0]`;
- `rxc_status_reaction`: which of the two watchable flags rose (bit 7 rptr-overtake, bit 10
  ppdu-desc/completion-descriptor error class);
- `rxc_phase1_looks_full`: the predicted one-full-lap state, fail-closed to "full".

### 3.2 The kernel glue (`patches/0001-rx-cmp-phase-prepublish.patch`)

- new module parameter `rx_cmp_prepublish` (uint, default **0** = action refused; the exact old
  behaviour);
- new write-only-ish action attribute `dp_rx_cmp_prepublish` (`0200`), one shot per load;
- the action runs the gate chain **before** any I/O, refuses on a transport miss
  (`0xffffffff`), refuses if the consumer is not `0x0000`, then does:
  1. read the status word (live bank) as "before",
  2. 32-bit exact-width store of `0x8000` into cfg `+0x40`, read back,
  3. re-read the status word and compute the reaction mask (bit 7 / bit 10),
  4. if the read-back fails or a reaction flag rose -> **restore the vendor initial `0x0000`** and
     report `PHASE1-REJECTED-BY-DEVICE` (rollback reported OK/FAILED),
  5. else report `PHASE1-ACCEPTED-IDLE` with the phase-aware view (producer/consumer/pending/
     full_lap) and the explicit line *"probe is clean only for the no-traffic discriminator; do NOT
     claim a fix"*;
- the arm path logs the prepublish state (published consumer, probe record) so the fire's evidence
  is in dmesg even if the sysfs reads are lost;
- every field is recorded in the dp status block (`rx_cmp_prepublish_{done,active,old,new,rb,
  status_before,status_after,reaction,rollback}`).

Fail-closed properties: param default 0; must run before the arm; one shot; consumer-dirty refusal;
readback required; automatic rollback on any device reaction; no retry; no write outside
cfg `+0x40` (and the rollback to its previous 0 value).

## 4. Host verification

Device-free test (the lane's committed harness, re-run at closeout):

```
$ sh tools/run-host-tests.sh
== compile: cc -std=c99 -Wall -Wextra -Werror -pedantic -O2
== run
rx_cmp_phase selftest: 102 checks, 0 failure(s)
RESULT: PASS
```

The 102 checks pin the packed-pointer arithmetic, the wrap/phase toggle, the phase-aware pending
count and its refusal cases, the gate chain order and precedence, the exact-width store, the
read-back rule, the status-reaction watch mask, and the phase-1 full-lap prediction.

Build harness `tools/build-prepublish-module.sh` (committed): stages the lane's base chain
(FINAL association tree + corrected `g0 id6` carrier + RX watermark), applies the sibling
observe-only vendor-diff patch (59 params) and then this lane's patch, builds against the project
6.18.60 tree, rejects warnings/errors/undefined symbols, strips, and enforces identity:
**60 parameters**, the presence of `rx_cmp_prepublish` + `rx_drain_write` + `rx_status_service` +
`rx_enable`, the feature strings (`DP rx_cmp_prepublish`, `PHASE1-REJECTED-BY-DEVICE`,
`PHASE1-ACCEPTED-IDLE`, `RX completion-phase`), the expected symbols
(`hi5622_dp_rx_cmp_prepublish_store`, `rxc_prepublish_gate`, `rxc_pending`,
`rxc_phase1_looks_full`), and a byte-identical `.text` across strip. The fire's module
(**60 params**, `bc942e13...`) was built from this chain.

## 5. The bench fire (the result)

Record: RX-hunt ledger entry "PREPUBLISH FIRE (the completion-phase candidate)", 12:00; the same
entry chain continues with the WOC-class fire at 13:00.

| item | value |
| --- | --- |
| artifact | **`bc942e13...`, 60 parameters**, built from this lane's chain (base + vendor-diff + this patch) |
| load | PD4 firmware, fresh boot; `rx_cmp_prepublish=1` at load |
| ladder | AP-ENABLED, `dp_setup` clean, then the prepublish action |
| prepublish result | **`pre=0` = success**: completion consumer cfg `+0x40 = 0x8000` (phase 1) |
| status before | `0x00000182` |
| status after | **`0x00000186` = bits 1, 2, 8** - **bit 2 (RX complete / rx-completion-pending) observed for the first time** |
| page | stayed **golden:1** (no exception in this run) |
| traffic | the laptop sent 3 authentication frames to the AP |
| capture | **`frames=0`**; completion ring empty (`cmp_wptr=0 / cmp_rptr=0 / head_cmp=0`) |
| verdict | the completion-consumer phase is **not** the capture blocker |

Interpretations, kept deliberately narrow:

1. **The phase mechanism is real.** The MAC's completion accounting reacted to the consumer phase
   by raising the RX-complete status bit - proof that the device sees and evaluates the published
   consumer, and that the completion path is alive up to the "frame complete" bookkeeping.
2. **No frame data arrived.** The status bit is an event/accounting flag, not a capture: no
   completion entry was produced and no buffer was written (the observe-only drain shows
   `frames=0`). The body of the completion path (the write of a completion entry + the data) still
   did not execute.
3. **The page-loss/fault is intermittent.** Earlier `rx_enable=1` runs lost the page during the
   auth window; this run stayed golden with the same `frames=0` / sticky bit-10 pattern. This is a
   race/timing component, recorded, not explained by this lane.

## 6. What the result rules out - and what it remains

**Ruled out:** the completion-consumer phase value alone explains the byte-exact-zero host write.
The vendor-initial phase `0x0000` is not "too idle" for the device to start a completion, and the
phase-1 candidate does not unlock the write.

**Kept as context for the next fire:**

- completion-ring *ownership/format* beyond phase: the completion entry is the 4-byte device VA of
  `buffer+4`; the device producer is live `+0x10`; the ring stayed empty even when bit 2 rose;
  whether the device requires a different completion entry *address* source (a devVA the device
  writes/echoes) or a specific pre-arm ordering remains [U];
- the **buffer write path**: the sibling WOC-class lane reproduced the vendor buffer memory class
  and also got `frames=0` - so neither the phase nor the buffer class explains it;
- the **firmware-side RX init** (F1): the vendor sends a pre-RX request class
  (`enable_trx`/`rx_ring_reset`) inside its boot/init message sequence that our minimal host never
  sends; this is the leading remaining cause after both candidate fires;
- the intermittent receive-path fault on arriving frames (the R2 isolation: with our RX rings absent
  the same frames are harmless; with them armed the status moves to the `0x582` class and the page
  can be lost).

**Converged state:** the device-side RX state matches vendor stock exactly, every host-side static
aspect is vendor-correct, the MAC receives the client's frames (status bit 10 class) but writes
nothing to host memory (`frames=0`), the completion phase can tick bit 2 without a frame landing,
and the WOC buffer class does not change it. The firmware RX configuration/init is the remaining
target (`wifi-init-decode`).

## 7. Evidence map

| claim | artifact / record |
| --- | --- |
| completion-ring/phase semantics (packed pointer, phase toggle, pending, vendor initial consumer) | this report section 2; `src/hi5622_rx_cmp_phase.{h,c}`; committed vendor disassembly/dump references in the header |
| the gate chain / exact-width store / rollback | `patches/0001-rx-cmp-phase-prepublish.patch`; `src/hi5622_rx_cmp_phase.c` |
| host tests (102 checks PASS) | `tools/rx_cmp_phase_selftest.c` + `tools/run-host-tests.sh` (re-run at closeout) |
| reproducible build + identity gates (60 params) | `tools/build-prepublish-module.sh` |
| the artifacts were committed despite the lane dying | opensource commit `04c0521` |
| the bench fire (phase-1 pre=0 success, `0x182 -> 0x186`, frames=0, ring empty) | superproject `mem-entries.md` entries "2026-11-26 12:00 - PREPUBLISH FIRE ..." and the 13:00 WOC fire |
| the intermittent page loss / race component | same ledger chain (12:00/13:00 entries) |
| the R0/R2 evidence and the surrounding RX state | `wifi-rx-vendor-diff/REPORT.md`, `wifi-rx-capture-plan/REPORT.md`, `wifi-rx-format/REPORT.md`; `wifi-re-report` v7 sections 6.26-6.29 |
| the sibling candidate (WOC class) fire | `wifi-rx-woc-class/REPORT.md` |

## 8. Honest limitations / what remains

1. **The lane's build was not run by the lane** - the Lead built and fired the module from the
   committed patch chain; this report records the fire from the ledger, not from a lane artifact.
   The build script is committed and reproducible.
2. **No traffic-state rung was ever run with the prepublish accepted**: the rung was designed as
   "R1 no-traffic discriminator first; R2 traffic only if clean", and the fire's own log says the
   clean result is *only* the discriminator - no claim beyond "the phase is accepted and the
   completion accounting reacts".
3. **The status bit-2 semantics** are from the capture-plan lane's MAC status map ("rx_complete /
   rx-completion-pending"); the exact device signal that raises it is [I].
4. **The completion entry format/address source** is a separate sub-question from the phase; this
   lane pinned the phase, not the entry producer contract (see rx-format/report).
5. **No completion-ring memory capture** was taken in the no-traffic run beyond the empty ring
   counters; a ring-memory snapshot after a received frame remains an open instrument ask.

## 9. Reproduce

```
$ sh tools/run-host-tests.sh                  # 102 checks, 0 failures, PASS (device-free)
$ ./tools/build-prepublish-module.sh           # needs the lane base tree + the 6.18.60 kernel tree;
                                               # produces the 60-param module, checks the identity gates
```

Then the attended rung (Lead only, recovery armed, fresh boot, PD4 firmware):
load with `rx_cmp_prepublish=1`, AP first, `dp_setup`, `printf 1 > dp_rx_cmp_prepublish` **before**
`dp_rx_arm`; expected in the no-traffic run: `pre=0`, cfg `+0x40 = 0x8000`,
`PHASE1-ACCEPTED-IDLE`, status `0x182 -> 0x186` (bit 2), page golden; **stop** on any exception /
page loss / status bit 7 or 10 reaction (the action rolls back and refuses). Only after a clean
no-traffic run may the laptop auth frames be sent; the PASS condition for the capture remains
`frames>0` + no page loss, which this rung did **not** achieve.

**Safety:** never write CA `0x400392F0`; no retries; no RX action without the arm + write gate;
nothing in this document authorises a device write - the Lead's fire rules and recovery door
govern every run.
