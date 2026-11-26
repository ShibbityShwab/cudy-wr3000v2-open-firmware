# REPORT - the dfs_set ORDER fix: the 0x07E7 ARM before the 0x01F7

**Lane:** `wifi-5g-dfs-order` (shared task **task-228**), 2026-11-26. **HOST-ONLY**:
static analysis of the committed firmware decode + the committed bench datum +
a clean-room driver patch + host build. No device contact. Grades as in the
sibling lanes: **[V]** re-derived/compiled here, **[B]** bench datum of
2026-11-26, **[I]** inference, **[U]** needs a device read.

---

## 0. TL;DR

1. **The dfs_set rung was refused by its own fail-closed gate [B].** The
   fire of module `1b47f0da` printed
   `dfs_set pre: ... b4a0=0x0104fd90 ... (u32[mctx+0x4A0] gate is nonzero)`
   and `dfs_set REFUSED (-16)`. The stock reference boot's mctx CAC words
   `+0x49C..+0x4A8` are all zero; ours were populated.
2. **The cause is the ladder order [B/I].** The 0x01F7 dfs_mode (the
   DFS/RF mode activation) was posted **before** the 0x07E7 DFS-SET ARM, so by
   the time the ARM gate `u32[mctx+0x4A0] == 0` ran the firmware CAC state was
   already active. The ARM must precede the 0x01F7.
3. **The fix is a ladder reorder [V].** The 0x07E7 worker's started-module
   gate needs the 0x0065 VAP start, and the 0x0065 needs the 0x0124 channel
   post, so the only slot that satisfies every hard dependency is
   `0x0065 -> 0x07E7 -> 0x01F7`. The 0x01F7 step therefore moves after the
   0x0124 channel post; the 0x01F7 worker itself still writes the mode byte
   `P+0xF04 = 3` and the `R+0x3A8..R+0x418` image, but the 0x0124-time
   RF programme CA 0xA2FA2 no longer runs (its mode gate is not yet open).
   The fire's D11 probes that consequence.
4. **The module is built on the dfs_set base.** `/tmp/dfs-order-artifact/
   hi5622_wifi.ko` = **391,408 B, sha256 a5ec150c...**, 66 params,
   `.text` `832802cc...`; 84/84 C + 46/46 Python host checks. Local
   evidence only.
5. **The fire rung is ready** ([FIRE-RECIPE.md](FIRE-RECIPE.md)): R1a/R1b
   isolate the cause (the CAC pre-state producer), R2 is the fixed rung with
   the expected dmesg order, probes D3 (the four-word CAC pre-state) and D11
   (the RF-order consequence) are new.

---

## 1. The bench finding, restated

The 2026-11-26 23:00 ledger entry (the pair-A boot = the stock reference; the
fired boot = our image #3 with the R5 ladder + `dfs_set=1`):

    cfg80211: dfs_set pre: mctx=0x00119758 band=1 state=1 ec8=1
              b4a0=0x0104fd90 b4ac=0x010067a8 cac=0 regs_pattern=0x00
              (u32[mctx+0x4A0] gate is nonzero - the worker would return
               without setting the flag)
    cfg80211: dfs_set REFUSED (-16)

`mctx[0x0E]=1` (the 0x0065 VAP start ran), `bandobj+0xEC8=1`, the CAC-active
flag `u8[mctx+0x4A5]=0` - but `u32[mctx+0x4A0] = 0x0104fd90` (a firmware-RAM
pointer, nonzero) so the ARM half refuses. The stock boot reads
`+0x49C = 0, +0x4A0 = 0, +0x4A4 = 0, +0x4A8 = 0`. The four words look like a
firmware CAC context (two firmware-RAM pointers, a duration/flag word and a
state word) that the firmware allocates when the CAC is started and frees when
it completes.

The only ladder steps that ran before the refusal are: VAP add (0x0064), the
0x00F7 descriptor table, the 0x01F7 dfs_mode, the 0x0124 channel post, the
0x0127 AP config and the 0x0065 VAP start. Of these, the 0x01F7 DFS/RF mode
activation is the one that puts the firmware into DFS operation. **The fix
targets the order: the 0x07E7 ARM must precede the 0x01F7.** [B] gives the
pre-state; the causal attribution 0x01F7 -> CAC words is [I] and is measured
directly by the fire's R1b rung.

---

## 2. The ordering dependencies (why the slot is forced)

| step | hard dependency | source |
| --- | --- | --- |
| 0x00F7 desc_table | after the VAP base (0x0064) bound the band object; before the 0x0124 post whose RF programme evaluates the armed record | sibling desc-table lane [V] |
| 0x01F7 dfs_mode | its worker writes `P+0xF04 = 3` and the RF image R+0x3A8..R+0x418; CA 0xA2FA2 (from CA 0x40684, called by the 0x0124 worker) needs `P+0xF04 == 3` | wifi-fw-rf-programme / wifi-dfs-mode-send [V] |
| 0x0124 channel | its worker stores `mctx[0x10..0x13]` which the 0x0065 G8 gate and the 0x0242 pre-start read; must precede the 0x0065 | wifi-chan-worker / wifi-vap-start-gates [V] |
| 0x0127 AP config | before the 0x0065 and the 0x0242 (the firmware needs the beacon template when the band is armed) | R5 lane, the 2026-11-26 fire [B] |
| 0x0065 VAP start | after the 0x0124/AP config; it is our ladder's only writer of `mctx[0x0E]=1` (the 0x07E7 started-module gate CA 0xA4C0E needs state 1..3) | wifi-5g-dfs-set REPORT 2.2 [V] |
| **0x07E7 DFS-SET ARM** | after the 0x0065 (started-module gate); needs `u32[mctx+0x4A0] == 0`, flag 0 and the clean sub-base | wifi-5g-dfs-set REPORT 2.2/2.4 [V] + the bench refusal [B] |
| **0x01F7 dfs_mode** | **must follow the 0x07E7** (task-228): it starts the firmware CAC [B/I]; but the same step wants to precede the 0x0124 (CA 0xA2FA2 mode gate) | conflict |
| 0x0242 band start | after all of the above (per-band MAC start, the band-1 0x1029 bits) | R5 lane [V] |

The dependency graph has a cycle if the 0x01F7 stays before the 0x0124:
`0x01F7 -> 0x0124 -> 0x0065 -> 0x07E7 -> (must precede) 0x01F7`. Something
must give. The 0x07E7's gates are hard firmware gates (a wrong order makes the
worker a no-op returning 0x64, and the driver's fail-closed pre-gate refuses
before the wire); the 0x01F7's pre-0x0124 position is a **performance/effect
ordering**, not a gate: the 0x0124-time CA 0xA2FA2 programme silently returns
and the 0x01F7 worker later writes the same mode byte and RF image itself.
So the 0x07E7 constraints win and the 0x01F7 moves after the 0x0065 step.

### Rejected alternatives

* **(b) post the 0x07E7 before the 0x0124 but after the 0x0064 base** -
  rejected: the started-module gate needs `mctx[0x0E] in 1..3`, and only the
  0x0065 sets it in our ladder; the worker would no-op.
* **(c) keep the 0x01F7 early and CLEAR the CAC state first** - rejected: the
  bench state is `b4a0 = 0x0104fd90` with the CAC flag `0`; the CLEAR branch
  also requires the flag `1` and `b4a0 != 0` + the armed register pattern, so it
  would refuse exactly like the ARM did.
* **(d) move the 0x0065 VAP start before the 0x01F7/0x0124** - rejected: the
  0x0065 G8 gate reads `mctx[0x10..0x13]` stored by the 0x0124 worker (R4 fire:
  G8 refused with `mctx[0x11]=0`).
* **(e) keep the old order and drop the fail-closed gate** - rejected by
  design; the gate is correct and the order is the bug.

---

## 3. What this lane changes (the patch)

`patches/0001-dfs-order.patch` on the dfs_set tree; exactly three files:

1. **hi5622_scan.c** - the whole `dfs_mode` (0x01F7) block (parameter gate,
   builder, post, worker-precondition dmesg) is moved from before the 0x0124
   channel post to **after the `dfs_set` (0x07E7) block**. The `dfs_set`
   block itself is byte-identical: same fail-closed parameter, same gates,
   same 29-B frames, same post-reads. The new order is
   `vap_add -> desc_table -> ap_chan -> ap_ssid -> ap_cfg -> vap_start_g0 ->
   dfs_set -> dfs_mode -> band_start -> ap_vap_start`. Two dmesg position
   strings and the module-parameter comments are updated; the compiled dmesg
   strings carry the order evidence.
2. **hi5622_scan_logic.c** - the host-tested ordering plan now emits
   `HI5622_BEACON_STEP_DFS_MODE` after `HI5622_BEACON_STEP_VAP_START_G0`, so
   the dmesg plan line reads
   `vap_add desc_table ap_chan ap_ssid ap_cfg vap_start_g0 dfs_mode band_start ap_vap_start`.
   The plan comments record the task-228 rule and the skipped 0x0124-time
   CA 0xA2FA2 programme. The dfs_set step stays plan-external by the sibling
   lane's design (the plan stays 9 steps).
3. **hi5622_dfs_set.h** - docs only.

No new module parameter; 66 params unchanged; no frame bytes changed.

---

## 4. Effects on the firmware path (what to watch on the fire)

* **Expected:** the `dfs_set pre:` line now reads `b4a0=0x00000000` (the
  clean stock pre-state) and `dfs_set arm post: cac=1 regs_pattern=0x2d`
  (PASS), i.e. the 0x07E7 worker's six sub-base bit0 writes and the CAC flag
  land.
* **The 0x0124 channel post no longer runs CA 0xA2FA2** (the mode byte is not
  yet 3). CA 0xA2FA2 is the RF register-block programmer; its silent return
  means the channel-range branch flag `R+0xF87` and the descriptor-predicate
  latch `R+0x410 bit0` (CA 0x89840) are not written by that path on this pass.
  The 0x01F7 worker still writes the mode byte and the R image itself, so D11
  measures what actually landed. **This is a documented trade, not a
  surprise:** the rung's PASS is the CAC writer taking effect (D1-D3), and
  the RF-ordering consequence is a first-class probe.
* **The band start (0x0242)** still runs after both, so the "CAC armed before
  the MAC/BSS start" semantics are preserved.
* **The late 0x01F7 may itself start the firmware CAC** after our ARM; that is
  the intended order (we arm first, the firmware CAC machinery second). The
  post-read only requires the CAC flag 1 / pattern 0x2D after the ARM.

---

## 5. Host evidence

| item | value |
| --- | --- |
| base tree | the dfs_set tree of module `1b47f0da` (391,388 B, lane wifi-5g-dfs-set) |
| patch | `patches/0001-dfs-order.patch`, sha256 `978c3d5b5795...` |
| module | `/tmp/dfs-order-artifact/hi5622_wifi.ko`, 391,408 B, sha256 `a5ec150cfc7d...` |
| .text | `832802cc212d...` |
| params | 66 (unchanged) |
| host tests | 84/84 C contract + 46/46 independent Python order/contract |

The Python test (`tools/dfs_order_test.py`) re-derives the 0x07E7 KAT frames
from the tree's own `#define`s, checks the sibling dfs_set contract is intact,
asserts the block order in hi5622_scan.c (`0x0124 < 0x0065 < 0x07E7 < 0x01F7 <
0x0242` by byte offset), the plan emission order, the moved block's contents,
and that the old 0x01F7-before-0x0124 text is gone. Pins and logs are in
[evidence/](evidence/).

---

## 6. The fire (summary; [FIRE-RECIPE.md](FIRE-RECIPE.md))

* **R1a** `dfs_mode=0 dfs_set=0`: read `mctx+0x49C/+0x4A0/+0x4A4/+0x4A8` ->
  expect all zero (the new D3 baseline; the stock's clean pre-state).
* **R1b** `dfs_mode=1 dfs_set=0` (the new late position): the same four words
  after the start_ap -> expected **nonzero**, which would prove the 0x01F7 is
  the CAC starter; if zero the causal model is wrong - STOP, report, do not
  fire R2.
* **R2** `dfs_mode=1 dfs_set=1`: the fixed rung; expected dmesg
  `vap_start_g0` -> `dfs_set pre` (`b4a0=0x00000000`) -> ARM posted ->
  `dfs_set arm post: cac=1 regs_pattern=0x2d` -> `dfs_mode` posted ->
  `band_start`; PASS = D1 pattern 0x2D + D2/D5-D8 unchanged + D3 CAC flag 1;
  D9 (beacon) is the optional jackpot; D10 producer counter observation;
  D11 the RF-order consequence.
* **R3** `dfs_set=2` ARM+CLEAR; **R4** the fail-closed negatives
  (`dfs_set=3/4`).

---

## 7. Limits / next steps

* Host-only; nothing here is fired. The 0x01F7->CAC causality is [I] until
  R1b measures it.
* If R2 still refuses on `u32[mctx+0x4A0] != 0`, the next re-rank is:
  the CAC words are written by something else before the 0x07E7 (the 0x0065
  worker's own CA 0xF593C/0x5F450 path or a boot-time firmware CAC start, not
  the 0x01F7). The order candidate after that would be an ARM before the
  0x0065 with a state-early writer (the vendor's 0x0069 VAP FSM) - a
  different lane.
* The full CAC state machine (60 s/600 s timer, NOL, `hmac_start_all_bss_of_band`)
  remains unimplemented; this lane changes the order only.
* The consumed-frame rule stands: one rung per fresh power-cycled boot, no
  in-place re-post.

---

## 8. Citations

* `mem-entries.md` 2026-11-26 23:00 - the refusal + the pair-A stock
  comparison + the order finding [B].
* `../wifi-5g-dfs-set/REPORT.md` - the 0x07E7 frame/worker/gates and the
  fail-closed integration [V].
* `../wifi-5g-dfs-set/FIRE-RECIPE.md` - the rung this lane's recipe is a delta
  of.
* `../wifi-dfs-mode-send/`, `../wifi-fw-rf-programme/`,
  `../wifi-fw-rf-descriptor/` - the 0x01F7 worker, CA 0x40684/CA 0xA2FA2 and
  the `P+0xF04` mode gate [V].
* `../wifi-vap-start-gates/`, `../wifi-chan-worker/` - the 0x0065/0x0124
  dependencies [V].
* `../wifi-r5-rebuild/CHAIN.md` + `../wifi-band1-start-send-5/` - the R5
  base ladder [V].
