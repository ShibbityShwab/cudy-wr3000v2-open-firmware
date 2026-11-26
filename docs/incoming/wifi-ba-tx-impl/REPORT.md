# wifi-ba-tx-impl - the BA TX-direction cut (the in-kernel ADDBA + the g0 0x0A/0x0B/0x1C)

**Lane:** `wifi-ba-tx-impl` (shared task **task-217**), 2026-11-26.
**HOST-ONLY**; this lane never contacted the router. **The Lead fires.**
**Base:** the current merged-module base - the **F2/R5 lineage** (the 5 GHz
chain tip + the R4b/R5 order, 65 params, the R5 fire module `24f7221b`).
**Starting point:** the committed BA skeleton (`../wifi-ba-agg`, task-178,
artifact `78460841...`, 183 host checks): its portable `hi5622_ba.[ch]` and
fail-closed `ba_enable` are ported and extended; the skeleton was provably
unwired and this lane wires it.

## 1. Headline

1. **The TX-direction cut is implemented, built and host-tested.**
   `ba_enable=1` arms the whole path: the AP as **TX originator** sends the
   33-B ADDBA request through the proven mgmt-TX carrier (HCC g0 id 6), and on
   the peer's 33-B ADDBA response the host posts the decoded firmware
   messages **g0 0x0A** (44-B BA relay), **g0 0x0B** (20-B A-MPDU start/end)
   and **g0 0x1C** (32-B TX BA notify), then clears on DELBA. The negotiation
   runs **in the kernel** because stock hostapd has none of the vendor WAL
   commands (`addba_req` 0xBA / `delba_req` 0x324 / `ampdu_end`: the vendor
   userspace contract).
2. **Every byte is pinned twice**: in C (`src/ba_tx_selftest.c`, 211
   standalone / 225 with the driver's H2D decode gate) and independently in
   Python (`tools/ba_tx_frame_test.py`, 99 checks). The complete wires are
   KATs, not field tests only: header group/id/total/tag/dst/alloc are
   asserted for all three messages.
3. **The core is no longer inert**: six integration points are in the driver's
   own code paths (probe/remove, D2H deliver, mgmt RX, data TX, start_ap/
   stop_ap, cfg80211_status) and the symbols are in the `.ko`; the Python
   contract test fails if any hook disappears.
4. **The fail-closed contract is unchanged and tested**: `ba_enable=0`
   (default) = no frame, no message, no state change; `>1` refused;
   unknown user indices, stale dialogs, non-action frames and post-gate
   refusals all refuse before a device write.
5. **The only evaluable fire today is the no-peer rung B2**
   (`ba_enable=1`, 2.4 GHz AP, no client): zero BA frames/messages, page
   golden. The real session rungs are gated behind the RX gate (see sec. 8):
   with no client able to associate, no agreement can be negotiated
   end-to-end. B1 (the `ba_enable=0` no-regression fire) already passed on
   the skeleton per the ledger (2026-11-27 06:00).

## 2. The base and why this lineage

The image #3 module merge (F2 + RX watermark + HE + BA skeleton) was baked
but never fired; the later R4/R5 5 GHz-start line was fired and produced the
current bench-proven tree: **F2 5 GHz chain + band_start + vap_start_g0 +
R4b order + gate inversion + R5 order** = 65 params, R5 module `24f7221b`
(383,240 B). That tree is the "current merged-module base" this lane extends:
it is the only lineage whose every host message the bench has actually
executed. The BA skeleton was built on an older 58-param 5 GHz-ready tree, so
its portable core is ported here rather than its patch.

The integration therefore touches only the six files named in TABLES T7 and
adds four files; the R5 order, the frames and the gate logic are untouched.
The build script pins the base by content (`hi5622_scan.c 86d6c57e...` etc.)
and refuses a drifted tree.

## 3. Architecture of the cut

    userspace (hostapd)                      kernel (hi5622_wifi.ko)
    --------------------                     -------------------------
    AP data TX (ndo_start_xmit) ----------> hi5622_data_tx: ring post
                                                 |
                                                 v (xmit ctx: spinlock+schedule_work)
                                           ba_glue tx hook: first MPDU to an
                                           associated station ->
                                           ADDBA request (33 B) via g0 id6
                                                 |
    peer ADDBA response (mgmt RX / D2H) -----> ba core: validate ->
                                               g0 0x0A relay (44 B)   [fw BA ctx]
                                               g0 0x1C notify 0 (32 B) [info]
                                               state active
                                                 |
    next MPDUs -----------------------------> g0 0x0B start/end (20 B)
                                                 |
    DELBA (peer / fw D2H) ------------------> g0 0x0A relay subtype 2 (44 B)
                                               g0 0x1C notify 2 (32 B)

The portable core (`hi5622_ba.c`) owns the state machine and the bytes; the
kernel glue (`hi5622_ba_glue.c`) owns the two wire paths, the settings
(`ba_enable`), the AP binding (channel/band/MAC from the committed
start_ap) and the user-index resolution (the cfg80211 station table's AID =
the firmware user index the BA messages are keyed by).

**Why the posts are deferred.** The D2H and mgmt hooks run in the HCC
service kthread, and the TX hook runs in `ndo_start_xmit` context;
`hi5622_frame_post()` is the sleeping wire path (bounded waits + msleep).
Both hooks therefore copy into a one-slot queue and `schedule_work()`; the
workqueue owns the mutex around the core. This also avoids the service
thread waiting on its own announce-consumption loop.

## 4. The wire contract (full detail in TABLES T1-T4)

* **g0 0x0A BA relay, 44 B**: 12-B HCC header (group 0, alloc 1/dst 0,
  total 44, id 0x0A, tag 0x5A5A) + 32-B payload (`03 01` = type 3 /
  subtype 1 ADDBA-response relay; `03 02` = DELBA relay; user index
  `+0x02`, TID `+0x07`, status `+0x09`, BAW `+0x0C`, timeout `+0x10`,
  A-MSDU `+0x12`, dialog `+0x14`, policy `+0x15`; DELBA initiator `+0x08`,
  clear flag `+0x14` = 0; the whole payload is zeroed before filling).
* **g0 0x0B A-MPDU start/end, 20 B**: payload `[0]` 0/1, `[1]` TID,
  `[2..3]` seq, `[4..5]` user key, `[6]` flow flag.
* **g0 0x1C TX BA notify, 32 B**: payload `[0..1]` user key, `[2]` TID,
  `[3]` value 0 (pending->active) / 2 (teardown).
* **The group byte of the notify (the one new wire fact of this lane).**
  Re-decoding the three vendor builders this lane: the relay builder forces
  `hdr[0]=0`, the A-MPDU builders keep the allocator's group 0, and the
  **notify builder forces `hdr[0]=2`** (`mov r2,#2; bfi r3,r2,#0,#4`
  @ vendor `.text` 0x16be4). The firmware receive tables advertise an
  id-0x1C handler only in the **group-0** table (CA 0x5DFA0), and the
  committed h2d catalogue itself flags the mismatch. The implementation
  posts the notify as **group 0** because `hi5622_frame_post()` requires an
  advertised handler, and the decoded contract makes the notify
  informational (it builds a follow-up MAC-core message; it is not a
  precondition of aggregation). This is the only place the lane deviates
  from the vendor builder bytes, it is documented in TABLES T1, and it does
  not weaken any gate. If a future fire shows the firmware rejects g0 0x1C,
  the alternative (g2 0x1C) needs a deliberate, reviewed gate extension -
  not a bypass.
* **Action frames** (33/33/30 B) through `himg_build_hcc_mgmt_tx()`: the
  proven HCC g0 id 6 carrier with the 48-B device control and the frame
  inline. The control needs the AP channel; the glue records it at the
  committed start_ap (and clears it at stop_ap), so a BA action frame can
  never be sent for an AP that is not up.

## 5. The negotiation state machine (host-tested)

Per peer (8) / TID (8): idle → pending (dialog token, ADDBA request) →
active (relay + notify) → cleared (relay subtype 2 + notify 2 / failing
status). `hi5622_ba_tx_data()` is the in-kernel originator step: idle starts
the negotiation; pending refuses to aggregate (`-EAGAIN`, never re-asks on
every MPDU); active emits the 0x0B window boundaries (open on the first MPDU,
close at `HI5622_BA_BURST_MAX` = 32 MPDUs or when the burst goes quiet -
the glue's 20 ms deferred close). The D2H path (ids 2/3/0x0F/0x1D) is
handled idempotently and only for a bound user index; unknown indices are
refused, never guessed.

The vendor's two D2H generations are both accepted (id 2 create / id 0x1D
action 0), and the id-0x1D action-1 embedded ADDBA-response relay (frame at
`hdr+20`) feeds the same validator as a mgmt-RX frame. The RX-direction
path (peer as originator) answers the ADDBA request and mirrors the session
host-side; the vendor per-TID RX sync to the firmware goes through the
user-sync writer whose wire encoding is parked [U] (`../wifi-ba-agg`
REPORT sec. 11) - that is the RX direction, not this cut.

## 6. The fail-closed contract (the fire's safety argument)

| item | value |
| --- | --- |
| `ba_enable` | uint, read-only 0444, default **0** |
| `ba_enable=0` | every entry point `-EOPNOTSUPP`; zero frames, zero H2D, no state change (byte-identical to the R5 module apart from the compiled-in code) |
| `ba_enable=1` | armed; a frame is only built after the API/state/gate checks pass |
| `ba_enable>1` | `-EINVAL`, no frame |
| no peer | the negotiation never starts (it is driven by an associated station's data MPDU / a fw D2H event with a known user index): **no BA frame can appear** |
| no committed AP | action frames refused (`-ENOTCONN`) |
| post-gate refusal | propagates; the session rolls back (request) or stays pending (relay) - never a silent active state |
| notify failure | counted; informational only (documented) |

## 7. Evaluation (host)

* module: **406,168 B** stripped sha256 `234b3d4b...`, .text `70259123...`,
  **66 params**, 0 warnings/errors, vermagic 6.18.60, depends=cfg80211 (the
  `cfg80211-provider.symvers` technique is unchanged);
* C host tests: **211 checks** standalone and **225 checks** with the patched
  tree's `h2d_frames.c` compiled in - i.e. every built H2D wire is decoded
  by the exact gate `hi5622_frame_post()` applies (transport + firmware
  handler) before it can reach the wire; 0 failures;
* Python contract test: **99 checks** (independent wire re-derivation, the
  catalogue handler rows, the fail-closed/gate source contract, all six
  integration hooks), 0 failures;
* the module is provably wired: `hi5622_ba_glue_{d2h,mgmt_rx,tx_data,ap_up,
  status_line}` have call sites and `.ko` symbols (evidence/
  artifact-pins.txt).

## 8. Limits and the dependency on the RX gate

1. **B3+ need the RX gate.** A client cannot associate today: the RX path is
   gated by the firmware-internal RX thread (the F1 is not a missing H2D
   frame - `../wifi-f1-from-capture`), and the mgmt-RX carrier that would
   deliver the peer's ADDBA response is the same group1 id13 path. Until the
   RX gate opens, the session rungs cannot be negotiated end-to-end. B2 (the
   no-peer fail-closed rung) is the only honest fire today.
2. **The D2H event group is [I].** The vendor host-core dispatch ids 2 / 3 /
   0x0F / 0x1D are registered on the D2H event path; our clean-room hcc core
   treats group 3 as the device→host event/data group, and the glue filters
   the BA ids to group 3 (group 4 is the boot/ready group and its ids 2/3
   must never be misread as BA events). If the bench dmesg shows the firmware
   sends the BA events on another group, the filter is the one place to
   adjust - fail-closed by construction.
3. **The notify group divergence** (sec. 4) is the only byte deviation; it is
   informational and documented.
4. **No retry/timer.** The vendor keeps a 500 ms ADDBA-response timer and
   retries; this cut starts the negotiation once per idle session and never
   re-asks while pending. A retry is a small follow-up (the glue already has
   a delayed work for the burst close).
5. **The RX reorder scoreboard is not implemented** (deliberately: the
   `wifi-ba-agg` minimal cut parks it behind the RX gate; the firmware may
   itself reorder in the working stack [I]).
6. **The 0x0B windows are boundaries, not MPDU aggregation.** The firmware
   decides the A-MPDU; the host only tells it where a burst starts/ends
   (decoded contract, TABLES T4/T5 of `../wifi-ba-agg`).
7. **The 5 GHz residual is unrelated**: the beacon timer is
   firmware-internal (`../wifi-tbtt-timer`); BA TX on 2.4 GHz does not
   depend on it.

## 9. Policy

Text only in git: the `.ko` is local evidence and never committed; no vendor
binary, firmware byte, calibration value or secret is stored in this lane.
The only protocol bytes committed are the project's own host-built frames and
the decoded field maps (as text). The fixtures use documentation MACs.
