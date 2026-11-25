# PATCH PROPOSAL - RX consumer hardening + association ordering guard

**Lane:** wifi-sibling-gpl (task-151), host-only.  **Status:** proposal + host-tested model;
**not applied to the project build tree** and **not fired on hardware**.  Target base: the final
association module tree (base artifact `/tmp/wifi-assoc-artifact/hi5622_wifi.ko`, 344,472 B,
sha256 `251d08f0...`, 56 params; source tree used by the stability lane).

Every change below is justified by a cited public sibling source (GPLv2 Hi3881V100 and/or the
vendor Hi1105 reference tree) or by our own committed decode.  The code is original clean-room
code; nothing is copied from either sibling (see REPORT.md §1.3 - Hi1105 is not even
open-licensed).

---

## P1 - completion-entry validity wait + clear-after-read (RX, fail-closed)

### Why

The device writes completion-ring entries asynchronously.  The Hi1105 HAL explicitly tolerates a
window where the completion producer pointer is visible before the entry payload is committed:

```c
/* HM wifi/dpe/hal/host_hal_ring.c:439-463 (abridged) */
uint32_t hal_comp_ring_wait_valid(...)
{
    ...
    while (oal_unlikely(*(uint64_t *)(entries + ring_ctl->entry_size * rd_idx)) == 0) {
        ...
        wait_cnt++;
        if (wait_cnt > COMPLETE_RING_WAIT_CNT) { ... return OAL_FAIL; }
    }
    return OAL_SUCC;
}
```

and it clears every consumed completion entry after copying it out:

```c
/* HM wifi/dpe/hal/host_hal_ring.c:501-521 (abridged) */
    if (count + read_idx >= ring_ctl->entries) {
        remains = (count + read_idx) % ring_ctl->entries;
        hal_ring_get_ring(entries, src_addr, entry_size * (ring_ctl->entries - read_idx));
        /* 内容读出后，清0 */
        memset_s(src_addr, ..., 0, ...);
        ...
```

Our current `dp_rx_drain` reads the entry once; entry 0 or an out-of-pool address goes to
`drain_refuse` and **closes the RX lane** (`hi5622_dp_txrx.c`, "completion/header validation").
A transient visibility window or a stale zero entry after AP restart/lane re-arm is therefore
reported as fatal corruption, and the lane cannot be retried without a module reload.

### What

Files: `hi5622_rx_live.h/.c` (portable model), `hi5622_dp_txrx.c` (glue + stats + params).

1. **Portable model additions** (no kernel dependency, mirror the existing style):
   ```c
   /* #define HIL_RX_ENTRY_WAIT_MAX 256u, HIL_RX_SPURIOUS_MAX 64u */
   enum hi5622_rx_entry_state {
       HIL_RX_ENTRY_VALID = 0,   /* non-zero, in a host RX pool */
       HIL_RX_ENTRY_WAIT = 1,    /* zero on this read; retry (producer early) */
       HIL_RX_ENTRY_SPURIOUS = 2,/* zero after the retry budget: no commit */
       HIL_RX_ENTRY_BAD = 3,     /* non-zero but out-of-pool/misaligned: hard error */
   };
   /* classifies one completion entry; ring_kind only valid for VALID */
   int hi5622_rx_entry_classify(rx_u32 entry, rx_u32 norm_lo, rx_u32 norm_hi,
                                rx_u32 small_lo, rx_u32 small_hi,
                                rx_u8 *ring_kind, rx_u8 *state);
   /* clear one consumed completion slot (clear-after-read) */
   int hi5622_rx_cmp_clear(rx_u32 *ring, rx_u32 depth, rx_u32 packed);
   /* spurious-wptr gate: below cap -> transient (no lane close), at/above -> fatal */
   int hi5622_rx_spurious_gate(rx_u32 spurious_total, rx_u32 cap, rx_u8 *fatal);
   ```
2. **Glue** (`hi5622_dp_txrx.c`):
   * new module param `rx_entry_wait` (uint, 0..`HIL_RX_ENTRY_WAIT_MAX`, default 32); 0 reproduces
     the old behaviour exactly (fail-closed on first zero entry) for the negative control rung.
   * in the drain loop: on `HIL_RX_ENTRY_WAIT` retry (re-read the entry after `cpu_relax()` +
     `dma_rmb()`); on `HIL_RX_ENTRY_SPURIOUS` increment `rx_spurious_wptr`, apply
     `hi5622_rx_spurious_gate(t->rx_spurious_wptr, 64, &fatal)`: if not fatal, **do not move any
     pointer**, log the classification and return (nothing published, buffers not refilled);
     if fatal, close the lane (fail-closed).
   * on `HIL_RX_ENTRY_BAD` keep the current hard failure path (no behaviour change).
   * after a valid entry is queued for refill and before advancing `cons`, clear the completion
     slot with `hi5622_rx_cmp_clear()` (the memory is the coherent completion ring buffer).
   * new stats fields `rx_spurious_wptr`, `rx_entry_waits`, `rx_waits_exhausted`; include them
     in the existing `dp_rx_drain` dev_info line.
3. **Why the hard cap**: a persistent desync (producer keeps advancing, entries never become
   valid) is a real defect and must stay fail-closed; 64 consecutive spurious observations with no
   progress is far beyond any plausible transient and still far below the 4095 ring depth.

### Tests (host, in this lane)

`tools/rx_model_selftest.c` implements the exact policy and asserts:
* a zero entry that becomes valid after k <= `rx_entry_wait` re-reads is accepted;
* a zero entry that never becomes valid is classified SPURIOUS and does **not** move the
  consumer, nor publish, nor refill;
* 64 spurious observations trigger the fatal gate; 63 do not;
* `rx_entry_wait=0` reproduces the old fail-closed classification on the first zero;
* a non-zero out-of-pool entry is BAD (hard) regardless of the wait budget;
* consumed slots are cleared and a re-read returns 0;
* pending/consumed arithmetic and one-lap phase handling still hold for 256/512/4095 depths.

### Risk / interactions

* Pure pointer-motion gating: no hardware register or firmware message changes.
* Default on (`rx_entry_wait=32`) only adds bounded waiting; setting it to 0 restores today's
  behaviour for the fail-closed rung.
* Does not require any update order; it only affects the host consumer.

---

## P3 - RX stall classification telemetry (log-only)

### Why

`dp_rx_drain`'s "no new completion" line does not distinguish the three very different states the
next bench test has to tell apart: (a) armed but no traffic reached the MAC data path, (b) the MAC
prefetched free buffers but never forwarded a frame, (c) completions exist and are being drained.
The sibling HALs name the free-ring-empty/refill event explicitly (Hi1105
`hal_irq_host_rx_ring_empty_isr`, Hi3881 HCC RX high-waterline) and the management/data split is
explicit in both (REPORT.md §2.3/§4.3), so the classification is not speculative.

### What

Files: `hi5622_rx_live.h/.c` + `hi5622_dp_txrx.c`.

```c
enum hi5622_rx_stall_class {
    HIL_RX_CLASS_IDLE = 0,        /* arm snapshot == now: nothing moved */
    HIL_RX_CLASS_PREFETCH = 1,    /* free rptrs advanced, cmp wptr never did */
    HIL_RX_CLASS_PROGRESS = 2,    /* cmp wptr advanced: completions exist */
    HIL_RX_CLASS_SPURIOUS = 3,    /* P1 zero-entry path */
};
int hi5622_rx_stall_classify(rx_u32 arm_cmp_wptr, rx_u32 cmp_wptr,
                             rx_u32 arm_norm_rptr, rx_u32 norm_rptr,
                             rx_u32 arm_small_rptr, rx_u32 small_rptr,
                             rx_u8 *cls);
```

The no-pending branch of `dp_rx_drain` prints the class plus a one-line interpretation; IDLE adds
the explicit sentence that management frames are expected on the D2H management path (event 13),
not on the data completion ring.  No pointer/register/behaviour change, no new global state.

### Test

Same selftest: all four classes including the prefetch-only case (rptr advanced, wptr unchanged)
and the arm==now idle case; phase-wrap snapshots are handled by the existing consumed arithmetic.

---

## P2 - association ordering guard (mgmt/assoc, fail-closed)

### Why

The GPL AP SME adds the client's user context **before** it encapsulates/sends the association
response:

```c
/* OH mac/hmac/hmac_mgmt_ap.c around the assoc-request handler (line 1456) */
*asoc_rsp_len = hmac_mgmt_encap_asoc_rsp_ap(asoc_req.hmac_vap->base_vap, &asoc_rsp_ap_info);
/* reached only after hmac_user_add() succeeded for this peer (same AP-assoc path) */
```

and the user add posts a device context event before the state sync
[V OH `mac/hmac/hmac_user.c:598-645,647-704`].  Our sta-ops lane implemented the same chain with
the vendor messages (`g0 id7 user_add` then `0x07d2 asoc-state`), and hostapd calls
`add_station` before sending the assoc response - but nothing enforces that order.  If hostapd
ever sends an assoc response for a peer the clean-room station table does not know (e.g. a failed
add_station, or a race), the firmware would have no user context and the frame would be dropped
silently (the Hi3881 data path drops frames for unknown users, and the AP path adds the user first
for the same reason [V OH `hmac_rx_data.c:790-846,1001-1056`]).

### What

File: `hi5622_mgmt.c` (uses the sta-ops station table helper; no new message).

* In `.mgmt_tx`, after the existing validation and only when `params->buf` is a management frame
  whose subtype is **association response (0x10)** or **reassociation response (0x30)**, look up
  `params->mac` in the station table.  If absent: log the sibling-derived rule and return
  `-ENOENT` (fail-closed) *without* posting the frame.
* Unicast probe responses, auth responses, deauth/disassoc and action frames are unaffected
  (those legitimately have no station entry yet).
* The guard is behind a module param `mgmt_assoc_guard` (default 0 = off; 1 = on for the
  ladder) so the fail-closed default path is untouched until fired.

### Tests (host)

Portable classification of the 802.11 management subtypes used by hostapd (assoc req/resp,
reassoc, probe req/resp, auth, deauth, action), plus the guard decision table (peer present ->
allow, peer absent -> refuse, non-assoc subtype -> allow).  These are pure functions and are part
of the same selftest file; the kernel glue change is a 20-line addition at the `.mgmt_tx` entry.

### Risk

* Refusing a response is safer than silently dropping it (hostapd sees the failure and retries
  teardown), but it changes behaviour, so it is param-gated default-off until the ladder proves
  the station-table ordering on hardware.
* No firmware message change; no effect on the already-proven AP start / keys / TX paths.

---

## Integration order and confidence

| id | area | code size | risk | needs new firmware message? | host-tested here |
| --- | --- | --- | --- | --- | --- |
| P1 | RX consumer | ~120 lines portable + ~40 glue | low (param-gated, pointer rules only) | no | yes |
| P3 | RX telemetry | ~40 lines portable + ~15 glue | none (logging) | no | yes |
| P2 | mgmt/assoc | ~60 lines | medium (behaviour gate, default off) | no | yes (decision table) |

Suggested integration: P1+P3 first (single RX patch, no behaviour change beyond transient
tolerance), ladder L-A/L-B/L-C; then P2 with its own negative-control rung L-D.  The parent's
pinned association artifact must not be overwritten with an un-fired build; this lane deliberately
did not touch it.

---

## What this proposal does NOT claim

* It does not claim to make RX captures appear: per REPORT.md §6/H1, scanning-only traffic is
  management traffic and belongs on the D2H management path, not the data ring.
* It does not claim the firmware delivers probe requests to the host at all - that is ladder L-A's
  open question.
* It is not a fix for the AP-start/MAC-reset ordering observation (REPORT.md H7); that remains a
  bench instrumentation item.
