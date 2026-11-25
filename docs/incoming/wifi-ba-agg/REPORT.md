# The vendor BA machinery and the minimal A-MPDU TX/RX (BA-1/BA-2)

**Lane:** wifi-ba-agg (shared task-178, owner rx-ba-agg). HOST-ONLY clean-room
static analysis; this lane never contacted the router. **Written:** 2026-11-25.
**Base:** vendor host module `hi5622v100_wifi.ko` 2.5.24 (local, unstripped,
17,059 defined symbols) + vendor firmware `FIRMWARE.bin` (928,920 B, sha256
`7fc87e20...3ef311b`), both read in place and never copied into the repo.
Evidence markers: **[V]** verified from the named artifact, **[I]** inference,
**[U]** parked/unknown.

## 0. Headline

1. **The BA engine is split.** The firmware owns the per-TID BA contexts that
   gate aggregation; the host owns the BA *negotiation* (the 802.11 action
   frames) and a per-TID session mirror. The host tells the firmware the
   negotiated parameters with **three** messages: g0 id `0x0A` (ADDBA/DELBA
   relay, 32 B payload), g0 id `0x0B` (A-MPDU TX start/end, 8 B payload) and
   g0 id `0x1C` (TX BA notify, 20 B payload). The firmware drives the host
   with D2H events `2` (create BA), `3` (delete BA), `0x0F` (peer DELBA) and
   the unified `0x1D` BA-action message.
2. **The vendor's userspace does BA.** The vendor's custom hostapd/supplicant
   calls vendor WAL commands (`addba_req` cmd 0xBA, `delba_req` 0x324,
   `set_baw_size` 0x32C) and an `ampdu_end` command to drive the host side.
   The WAL `ampdu_start` handler is literally a NOP [V]. **Stock hostapd has
   no such commands**, so in the clean-room driver the BA negotiation must run
   **in-kernel** (or via our own debugfs/nl80211 hook). This is the single most
   important finding for the implementation plan.
3. **Complete decode delivered for:** the three over-the-air action frames
   (ADDBA request 33 B, ADDBA response 33 B, DELBA 30 B, byte-exact), the two
   relay payloads (ADDBA rsp relay / DELBA relay, byte-exact incl. the fw's
   clear-flag field), the A-MPDU start/end payload, the TX BA notify payload,
   the per-TID session layouts (TX 0x88 stride, RX 0x80 stride + a 0x1824-byte
   reorder window with a 256 x 24 B software scoreboard), the RX timeout path,
   and the firmware dispatch rows (`0x0A -> CA 0x5FF64`,
   `0x0B -> CA 0x5E2F4`, `0x1C -> CA 0x5DFA0`).
4. **Minimal A-MPDU implementation identified** (section 9): a per-TID
   session table + the three frame builders + the two relay builders + the
   start/end message on the TX path. RX reorder (the firmware's reorder or the
   host scoreboard) is **not** on the critical path for BA-1 because RX is
   behind the RX-capture gate; the host-side scoreboard is decoded but
   deliberately not implemented yet (it cannot be tested before frames arrive).
5. **Status:** decode + report + tables + fail-closed patch skeleton + host
   tests + fire recipe are done in this directory. The bench gate is RX: no
   client can associate yet, so the only *currently evaluable* bench rung is
   the no-peer BA smoke (module loads with `ba_enable=1`, no BA frame may
   appear, page golden). The real BA rungs are written but blocked on RX.

## 1. Inputs and pins

| input | pin |
| --- | --- |
| vendor host module (RE only, local) | `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko`, 3,581,748 B, not stripped; all addresses below are `.text` offsets |
| vendor firmware (RE only, local) | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`; CA = file offset + 0x40000, Thumb-2 |
| decode tool | OpenWrt ARM objdump at `.../staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump`; firmware with capstone (project venv) |
| reproduction | `tools/ba_decode_check.py` (asserts every anchor in TABLES against the local artifacts, prints PASS/FAIL) |
| driver base for the skeleton | 5 GHz ready base tree (`/tmp/wifi-5ghz-ready-build` lineage, 43 objects; the tree the bench-tested 57-param module was built from) |
| related lanes | `wifi-scan-full` (fully decodes the fw g0-0x0A handler 0x5FF64), `wifi-rx-init-msg` (H2D builder scan), `wifi-5ghz-vap-msg` (wire-shape conventions + fire-recipe style), `wifi-dp-*` (TX data path) |

## 2. Architecture - who owns what

The Hi5622 stack is a split MAC: the chip firmware owns the per-TID BA
contexts and the aggregation decisions, while the host driver (this vendor
module) owns the 802.11 management frames and a host-side session mirror.

    peer <--- ADDBA req/rsp, DELBA (action frames) ---> host driver
                                                         |   ^
                       g0 0x0A / 0x0B / 0x1C (H2D)       |   | D2H 2/3/0x0F/0x1D
                                                         v   |
                                                     firmware BA engine
                                                     per-TID contexts
                                                     (obj + tid*0x20 + ...)

The firmware can *initiate* BA (D2H `2` create / `0x1D` action 0: "set up a TX
BA session for this user/TID"), it can *relay* a received ADDBA response
(D2H `0x1D` action 1 embeds the frame at hdr+20), it can ask the host to send
a DELBA (D2H `0x1D` action 2), and it reports its own session teardown
(D2H `3`, `0x0F`). The host parses/validates and answers with the three H2D
BA messages. On the wire the host builds the ADDBA/DELBA action frames itself
and sends them through the already-proven mgmt-TX carrier (g0 id 6).

Consequence for the clean-room driver: **we must implement the negotiation
state machine in the kernel** because stock hostapd will not send the vendor
WAL commands. That is small (section 9) and testable on the host; it needs no
new firmware message beyond the three decoded ones.

## 3. The host BA machinery (function map in TABLES T1)

Three layers:

1. **Frame layer** - `hmac_mgmt_encap_addba_req` (0x43e50),
   `..._addba_rsp` (0x43fe8), `..._delba` (0x44184) build the 33/33/30-byte
   802.11 action frames; `hmac_mgmt_tx_addba_req/rsp/delba`
   (0x44734/0x44a70/0x44f30) wrap them into an skb and push through
   `hmac_tx_mgmt_send_event` (the g0 id-6 mgmt-TX path on the bench).
   `hmac_mgmt_tx_action` (0x490e4) is the category-3 dispatcher (0 request,
   1 response, 2 delba).
2. **Relay layer** - `hmac_mgmt_rx_addba_rsp` (0x45a84) and
   `hmac_mgmt_rx_delba` (0x45d78) build the **g0 0x0A** messages that program
   the firmware BA context (fully decoded in TABLES T3; fw side in
   `../wifi-scan-full/REPORT.md` sec. 3). `hmac_mgmt_rx_addba_rsp_verify`
   (0x45888) checks a received response against the pending TX session (state,
   dialog token, TID) before the relay.
3. **Session layer** - `hmac_tx_ba_setup` (0xaf180),
   `hmac_tx_ba_session_process` (0x16cf0), the RX session create/delete
   (`hmac_ba_rx_add/del/win_init`) and the reorder scoreboard
   (`hmac_ba_rx_buffered_data_software_check` 0xdbcb8) + timeout path
   (`hmac_ba_timeout_fn` -> host-core id 8 -> `hmac_ba_timeout_proc`).

Session structs and the RX window are in TABLES T2. The key facts:

* TX session per (user, TID): 0x88-byte region, state at +0x84
  (0 idle, 1 ADDBA-pending, 2 active), dialog at +0x85, policy at +0x86,
  ADDBA-response timer object at +0x10 (500 ms registration in
  `hmac_update_user_tx_addba_req`).
* RX session per (user, TID): 0x80-byte *session* + a separately allocated
  **0x1824-byte window**: start seq, 12-bit end seq, next-release seq, BAW,
  window state (+0x15: 0 init, 2 active), pending count (+0x16), and 256 x 24 B
  reorder slots in +0x1C..+0x1820. The RX inactivity timer is created with a
  per-TID timeout of 60/100/100/100/40 TU depending on TID (vendor values
  [V]).

## 4. The H2D BA messages (what we must send; byte maps in TABLES T3-T6)

All three are standard 12-byte-header HCC messages (`hdr[6]` = id) with the
device triple in hdr[1].high/hdr[2]/hdr[3] (chip id in hdr[3] = 0 for both
bands) and the payload at hdr+12.

### 4.1 g0 id 0x0A - the BA relay (44 B total: 12 + 32) [V]

* **subtype 1** (payload+0 = `0x0103`): ADDBA response relay. Carries user
  index (+2), TID (+7), ADDBA status (+9), BAW (u16 +0x0C, clamped), BA timeout
  (u16 +0x10), A-MSDU flag (+0x12), dialog token (u16 +0x14), BA policy
  (+0x15). The firmware maps these into its per-TID context
  (obj+slot*0x20+0xEC: BAW -> +0x10, timeout -> +0x18, A-MSDU -> +0x17,
  enable -> +0x13=2); a nonzero status takes the clear path [V via scan-full].
* **subtype 2** (payload+0 = `0x0203`): DELBA relay. Carries user index
  (+2), TID (+7), initiator bit (+8); payload+0x14 is the firmware's clear
  flag and must be 0. We zero the whole 32-byte payload before filling.

### 4.2 g0 id 0x0B - A-MPDU TX start / end (20 B total: 12 + 8) [V]

* start: payload {0, TID, seq u16, user key u16, flow flag, 0}; the vendor
  skips it if the TX session is not yet active and the flow flag is 0.
* end: payload {1, TID, 0, user key u16, 0, 0}.

Firmware side: g0 id 0x0B -> CA 0x5E2F4; start programs the per-TID
aggregation context via `0x95188(module, payload)`; end looks the peer object
up by the user key (0xD6A1E) and clears `obj+(tid+8)*0x20+6` (u16) and
`obj+tid*0x20+0xF1` (u8) [V].

**Interpretation [I, high confidence]:** these are the host's *aggregation
window boundaries* handed to the firmware TX scheduler for a burst of posted
MPDUs; they are not per-frame. The clean-room driver should emit start before
a burst of QoS data MPDUs for an active TX BA session and end after the burst
(and on TID queue empty / BA teardown).

### 4.3 g0 id 0x1C - TX BA notify (32 B total: 12 + 20) [V]

Payload {user key u16, TID, value 0/1/2, zeros}. Built by
`hmac_tx_ba_notify` from the TX BA state machine (`hmac_tx_ba_session_process`
sends value 0 for pending -> active transition and 2 for teardown). The
firmware handler is CA 0x5DFA0; it builds a follow-up MAC-core message and the
exact firmware semantics are parked [U] - for the minimal path this message is
a *state notification*, not required to establish the session; our skeleton
sends it on every state transition and it is fail-closed by `ba_enable`.

### 4.4 Config surface (context, not needed for the minimal path) [V]

Per-VAP BA knobs go through the vendor WAL commands and H2D config events:
BAR request key 0x00D7, BA-bitmap dump key 0x00F1, aggregation number key
0x01DB, BAW size is kept in host fields `user+0xf44/+0xf46`, per-VAP
ampdu-tx switch in `vap+0x18c -> +0x49`. The vendor WAL `ampdu_start` is a
NOP; `ampdu_end` runs the g0 0x0B end. These are *vendor userspace* controls;
we do not need them for a first A-MPDU (our driver can use its own
module params), but they are the reason the vendor hostapd exists at all.

## 5. The D2H BA events (what the firmware sends; table in TABLES T7) [V]

* **id 2 - create BA**: {uid u16, TID, peer index, protocol mode} -> host
  validates chip/uid/vap, checks the TX session, calls `hmac_tx_ba_setup`
  and sends an ADDBA request action frame. This is the firmware saying "a TX
  BA session is wanted for this peer/TID".
* **id 3 - delete BA**: for every active TX TID of that user, send a DELBA
  action frame to the peer and clear the session (protocol mode refreshed).
* **id 0x0F - peer DELBA report**: {uid, tid, initiator, reason} -> host
  sends a DELBA action frame.
* **id 0x1D - BA action** (the unified message):
  - action 0: TX BA setup (`hmac_tx_ba_setup` + ADDBA request);
  - action 1: **embedded received ADDBA response** (frame bytes at hdr+20,
    >= 9 bytes) -> host validates and sends the g0 0x0A subtype-1 relay;
  - action 2: send DELBA (initiator=1, reason 1) to the peer.
  Header: chip in hdr[1].high/hdr[3], uid in hdr[12..13], TID at hdr[14],
  action at hdr[15].

Note the vendor has **two generations** of the same contract (ids 2/3 vs the
unified 0x1D); both handlers exist in 2.5.24. A host implementation should
handle all four ids idempotently [I].

## 6. The firmware BA messages (what the firmware does with ours; TABLES T9) [V]

Group-0 H2D dispatch table at CA 0x10CCFC, 12-byte rows:

* id 0x0A -> CA 0x5FF64: the BA relay config/clear (already fully decoded by
  the `wifi-scan-full` lane: type=3/subtype=1|2 gate, object key lookup
  0xD6A1E(payload+2), per-TID context obj+slot*0x20+0xEC, BAW/timeout/A-MSDU/
  enable writes, clear path).
* id 0x0B -> CA 0x5E2F4: A-MPDU start/end (TABLES T4).
* id 0x1C -> CA 0x5DFA0: TX BA notify -> builds a follow-up MAC-core message
  with id byte 0x0B; the host-side wire contract is complete, the fw-internal
  follow-up is parked [U].

The D2H senders for ids 2/3/0x0F/0x1D live in the firmware; their exact CA is
not needed to implement the host side (the host handler field maps are decoded
from the receiving side) and is parked [U].

## 7. The decoded minimal A-MPDU sequence

Notation: `ADDBA-RSP` etc. are 802.11 action frames; `g0 0x0A` etc. are HCC
messages; `D2H n` are firmware events. All sequences assume **one associated
peer** and TID t.

### 7.1 AP is the TX originator (AP -> client data, i.e. the AP is the
recipient of the BA agreement; the client's RX BA). This is the direction the
proven 2.4/5 GHz AP needs first.

Firmware-initiated path (what the vendor actually does):

1. fw -> host: **D2H 2 (create BA)** or **D2H 0x1D action 0** {uid, t, mode}.
2. host: validate chip/uid/TID; create/refresh the TX session at
   `user+t*0x88`; state = 1 (pending); start the 500 ms response timer;
   write the per-TID state through the user-sync item 4 (vendor).
3. host -> peer: **ADDBA request** action frame (33 B; dialog, BAW, timeout,
   start seq 0).
4. peer -> host: **ADDBA response** action frame (33 B; status=0, negotiated
   BAW/param).
   * In the vendor stack this arrives either through the normal mgmt-RX path
     (`hmac_mgmt_rx_action_ba` subtype 1) or embedded in **D2H 0x1D action 1**
     (firmware relay); both end in the same validator.
5. host: validate (TID, dialog, status). On success: TX session state = 2;
   stop the response timer; refresh the user-sync state.
6. host -> fw: **g0 0x0A subtype 1** {uid, t, status, BAW, timeout, A-MSDU,
   dialog, policy}. The firmware now has the per-TID TX BA context; it will
   aggregate MPDUs for that TID.
7. host -> fw (data path): **g0 0x0B start** {t, seq, user key, flag} before a
   burst of QoS data MPDUs on that TID; **g0 0x0B end** {t} after the burst
   and on TID teardown.
8. steady state: fw aggregates on air; per-transition **g0 0x1C notify** is
   informational.
9. teardown: fw -> host **D2H 3 / 0x1D action 2 / 0x0F**, or peer DELBA ->
   host sends **DELBA** (30 B) and **g0 0x0A subtype 2**; clear the session.

### 7.2 AP is the RX recipient (client -> AP data, the client is the TX
originator). This direction is what a throughput test of the AP's downlink
would need too, and it is the one that depends on the RX gate.

1. peer -> AP: **ADDBA request** (client asks to aggregate its TX).
2. host: parse it (`hmac_mgmt_rx_addba_req`, category 3 action 0); allocate
   the RX session (`hmac_ba_rx_add`): window = 0x1824 B, BAW/start seq from
   the request, state = 2, timer with the per-TID vendor timeout.
3. host -> peer: **ADDBA response** action frame (33 B, status 0, negotiated
   param).
4. host -> fw: **g0 0x0A subtype 1** with the same negotiated parameters
   (this is what programs the firmware RX BA context; the firmware then
   accepts A-MPDUs for that TID and passes MPDUs up).
5. RX data: the host software scoreboard (256-slot ring, 12-bit seq window)
   releases in-order MPDUs and flushes gaps on timeout
   (`hmac_ba_rx_buffered_data_software_check`, `hmac_ba_timeout_proc`).
6. teardown: DELBA either way -> **g0 0x0A subtype 2** + session/window free.

**Minimal BA-1 cut (this lane's recommendation):** implement 7.1 steps 1-7
plus the 7.2 steps 2-4; park the RX scoreboard (7.2 step 5) until the RX gate
is solved, because it cannot be exercised before frames arrive and the
firmware may itself reorder in the working vendor stack [I]. The TX direction
is fully testable as soon as a peer can associate.

## 8. The wire shapes we must not get wrong (fail-closed rules)

* Total lengths are fixed by the vendor shape: g0 0x0A = **44 B** (32 B
  payload), g0 0x0B = **20 B** (8 B), g0 0x1C = **32 B** (20 B). The ADDBAs
  are **33 B**, DELBA **30 B**. An earlier lane lost a fire to an 8-byte
  short relay frame; do not repeat that class of error.
* The relay payload must be **zeroed before filling** - the firmware reads
  payload+0x14 as a clear flag on subtype 2 and payload fields we do not set
  are read on some paths (scan-full sec. 3).
* Category must be 3 and the action subtype exact: 0 request, 1 response,
  2 delba. Anything else is logged and dropped by the firmware/host.
* BAW is a 10-bit field in the BA parameter set; clamp to the user max
  (`user+0xf44/+0xf46` in the vendor, 64/256 in our skeleton).
* Every entry point is inert unless `ba_enable=1`.

## 9. The minimal implementation in the clean-room driver (patch skeleton)

Delivered as `patches/0001-ba-skeleton.patch` against the 5 GHz ready base
tree plus the portable core in `src/hi5622_ba.[ch]`. The skeleton is
**compiled in but inert** (`ba_enable=0` default); the integration hooks are
deliberately left for the next rung (they touch files other lanes are editing:
`d2h.c`, `hi5622_mgmt.c`, `hi5622_data_tx.c`, `h2d_frames.c`). The hook
points, with exact signatures, are:

1. **D2H dispatch** (`d2h.c`): route message ids 2, 3, 0x0F, 0x1D to
   `hi5622_ba_d2h()`. The message bytes are already in the driver's D2H
   frame buffer; the handler needs only (hdr, len).
2. **Mgmt action RX** (`hi5622_mgmt.c` / the mgmt frame handler): when a
   received management action frame has category 3, action 0/1/2, call
   `hi5622_ba_mgmt_rx_action(peer, frame, len)`. The TX mgmt path already
   exists (g0 id 6) and is the frame TX callback.
3. **DATA TX path** (`hi5622_data_tx.c`): after a QoS data MPDU for an active
   TX BA session is posted, call `hi5622_ba_tx_mpdu()`; it emits a start
   boundary on the first MPDU of a burst and an end boundary when the burst
   ends (bounded MPDU count, e.g. 32, and a max hold time).
4. **Ops plumbing** (`hi5622_ba.h`): the core is callback-driven
   (`post_h2d`, `tx_action_frame`, `log`), so it has no direct dependency
   on the driver internals and is unit-testable on the host.

What is **not** in the skeleton (deliberately): the RX scoreboard, the user
sync writer (the vendor's per-TID state sync item 4 encoding is not decoded
[U]; the g0 0x0A relay carries the same parameters, so this is second-order),
and any vendor WAL command emulation.

## 10. Evaluation

* **Host tests** (`src/ba_selftest.c`, `tools/run-host-tests.sh`): every
  frame byte, every relay payload byte, parser rejects, the state machine,
  the D2H actions, fail-closed behaviour. The selftest compiles
  `src/hi5622_ba.c` standalone; it needs no kernel, no vendor artifact, no
  device.
* **Static evidence** (`tools/ba_decode_check.py`): regenerates every address
  and byte-map anchor from the local vendor artifacts and prints PASS/FAIL;
  the committed evidence transcripts are the tool's output.
* **Bench**: `FIRE-RECIPE.md`. The gate is RX; only rung B0 (the no-peer BA
  smoke) is evaluable on the current healthy chip. The BA session rungs must
  wait for a client association - BA-1 cannot be proven earlier, and a fire
  without RX would only re-measure the existing gate.

## 11. Limits and open questions

1. The firmware's aggregation granularity is inferred [I]: g0 0x0B start/end
   are per-burst boundaries and the A-MPDU size is decided in fw/alg. The
   exact fw aggregation policy (and the g0 0x1C follow-up) is [U].
2. The RX software scoreboard is decoded structurally [V offsets] but the
   slot-base/flag relation is [I]; it is not needed for BA-1's minimal cut.
3. The user-sync writer's wire encoding (item 4 per-TID BA state, item 8 RX
   seq) is [U]. The vendor sends it in addition to g0 0x0A. Our skeleton
   leaves the hook but does not encode it; if the fw turns out to require it
   for the first aggregation, it is the next decode target (32-byte descriptor
   builder at 0x44670 is the TX-para sync, a good starting point).
4. The D2H 0x1D message carries the received ADDBA response frame at hdr+20;
   the exact min length the fw sends is 9 bytes + the rest of the frame
   [V: the host handler passes 9 to the validator, and reads frame[0..8]].
5. The vendor WAL commands are the userspace contract of the vendor image
   only; the clean-room driver does not implement them, and stock hostapd
   does not use them.
6. No vendor byte, firmware byte, calibration value or secret is reproduced in
   this directory; the evidence tool reads the local artifacts only.
