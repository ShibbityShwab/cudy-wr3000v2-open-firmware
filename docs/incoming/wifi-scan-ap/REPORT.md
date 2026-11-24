# REPORT - cfg80211 scan / AP / VAP-message wiring (M2/M3) for the Hi5622V100 clean-room driver

**Lane:** wifi-scan-ap, shared task **task-113**. **Date:** 2026-11-24(-25).
**Deliverable:** this directory (sources + configs + ladder + evidence), now
including the integrated **task-110 wifi-vap-scan builders**.
**Status:** **host-only.** Compile-clean (0 warnings), host-selftested, static
selftested; **not bench-fired by this lane**. No vendor binary, firmware byte,
calibration value, credential or per-unit identity is committed.

**Evidence markers** used throughout (project convention): `[V]` verified
against a vendor capture / hardware bench fact, `[V*]` vendor live read-out,
`[I]` inferred from committed reverse-engineering evidence, `[U]` undecoded
or unverified. The newest ledger entry wins over any older claim.

---

## 1. What this lane is (and is not)

The wifi-cfg80211 lane (task-104) delivered the M0 wiphy + M1 netdev model and
registered `scan` as an honest `-EOPNOTSUPP` stub: the wiphy appears only
after the HCC ready handshake, `iw phy`/`iw dev` enumerate, and
`iw phy phy0 interface add wlan0 type managed` creates a real netdev
(hardware-validated on our own image, STATUS.md sec 3k, 2026-11-25).

This lane wires the **next control-plane surface** onto that model:

1. **scan**: `.scan` builds the request through a provider, posts it through a
   single fail-closed gate, parses D2H g1 15/16/17 / g2 2 into cfg80211 BSS
   entries, and reports exactly one `cfg80211_scan_done()` with a bounded
   timeout/abort path;
2. **abort_scan**: cancellation observed by the bound work item;
3. **start_ap / stop_ap**: WAL group-2 design + provider-stub path (the exact
   payload is gap G1), gated identically to scan;
4. **add_virtual_intf / del_virtual_intf**: now also attempt the firmware
   user/vap messages (g0 id7 + optional id8 notify, g0 id9 del) after/before
   the already-validated netdev model;
5. **the frame-provider contract** (`hi5622_frames_api.h`) + registry
   (`hi5622_frames_provider.c`) that lets the still-undecoded payload
   builders drop in **without touching the cfg80211 code**;
6. **the fail-closed post gate** (`hi5622_frame_post.c`): the only path by
   which a provider-built frame can reach the wire; default `allow_frames=0`
   means it never writes anything until a supervised bench session says so;
7. **userspace fixtures + test ladder** (`configs/`, `TEST-LADDER.md`).

The H2D payload question for these messages is now **closed** by task-110:
the g0 id0xC (508 B), g0 id7/id8 (168 B) and g0 id9 (12 B) builders are
integrated and registered as the module's single frame provider
(`vapscan-0e4ad45`, opensource commit 0e4ad45).  What is still missing:
the **D2H** g1 id15 descriptor layout is [U], so the tolerant scan-result
decoder is always installed (tolerant) and `.scan` posts the real
520 B g0 id0xC frame whenever the post gate is open; the WAL AP-start/stop payloads are gap **G1** [U];
`add_key`/`connect` remain M3/M5 (NULL ops); the data path is M4.  No frame is
ever written unless the separate post gate is opened (`allow_frames=0`
default), so the committed module is still fail-closed end to end.

---

## 2. Starting point (inherited, hardware-validated)

| fact | evidence |
| --- | --- |
| wiphy registered only after the HCC ready handshake (g4 id1 + g4 id2 payload 0x01060850) | [V] STATUS.md sec 3k / ledger 2026-11-25 15:00 |
| `iw phy` enumerates Wiphy phy0; `interface add wlan0 type managed` creates netdev addr 02:00:68:69:35:01 and deletion works | [V] same ledger entry |
| clean-room module 505,076 B sha256 d2b3cf79... (`hi5622_cfg80211` tree) | [V] ../wifi-cfg80211/BUILD-NOTES.md |
| full-duplex HCC: ctx-create g0 id1 key 0x64 consumed (ctx+0x190 allocated), TXQ sync g0 id1 cmd 0xd8 consumed, calibration g0 id2 key 0x0101 answered | [V] STATUS.md sec 3j, ledger 2026-11-25 13:30/15:00 |
| PD4-patched vendor firmware is a **research artifact** (init-guarded out[0] poll stub); not upstreamable | [V] ledger 2026-11-24, status 3i |
| golden firmware state page CA 0x1BF000 = 0x86807FEF/7/2/0xABCD state 1 mask 0 | [V] every bench session |

The inherited op map (39 vendor-populated cfg80211 ops) lives in
`../wifi-cfg80211/src/hi5622_cfg80211_logic.c`; this lane changes the
implementation status of five of those rows and adds one op.

---

## 3. Architecture

```
 userspace (iw / hostapd / wpa_supplicant)
        |  nl80211
        v
 cfg80211 ops  --------------> hi5622_scan.c   (kernel: request lifetime,
 (hi5622_cfg80211_ops.c)        |                work item, cfg80211 BSS,
                                |                start_ap/stop_ap, VAP msgs)
                                v
                       hi5622_scan_logic.c     (PORTABLE model: classify,
                        (host-selftested)       accept/complete/abort/timeout,
                                                validators, counters)
                                |
                                v
                       hi5622_frames_api.h     (provider contract:
                        (registered object,     build_scan_req/... and
                         EMPTY by default)      parse_scan_result/...)
                                |
                                v
                       hi5622_frame_post.c     (ONE fail-closed wire path)
                                |                allow_frames=0 default,
                                |                budget + gap + structural
                                |                validation + hcc_core gates
                                v
                       hcc_core SR commit + coalesced mailbox id-3 announce
                                |
                                v
                       HCC link (already bench-validated)
```

Device->host scan events bypass nothing: the direct-read D2H path
(`d2h.c rd_deliver`) -> `hi5622_hcc_deliver()` -> `hi5622_scan_deliver()`
(trylock, classify, provider decode) -> `cfg80211_inform_bss()`; the same
frame then continues into `hcc_core_deliver_hdr()` exactly as before.

---

### 3.1 Bench update (2026-11-24): the first real firmware management frame

A supervised bench session (lead, 2026-11-24) drove the integrated provider on
the cfg80211 image.  Session evidence (bench-observed [V], relayed here):

* **PASS** - `frame post user_add: g0 id7 total 180 B in SR0 slot 2 devva
  0x8417c000 producer 2->3, coalesced announce consumed after 0 polls; handler
  risk=NO_ACCESS; posted 1/8` and `cfg80211: VAP wlan0 firmware user_add
  committed (g0 id7, user_id=0 vap_id=0)`: the first real firmware management
  frame sent by our clean-room host.
* **GAP fixed** - the immediate follow-up user_add_notify (g0 id8) was refused
  `previous mailbox announce still inflight`: the core coalescing state
  clears on its own service tick.  The post gate now bounded-waits (5 ms polls,
  <= 200 ms) for core-inflight=0, live out[0]=0 and the inter-frame gap;
  `frame_gap_ms` default is now 20 ms, so the vendor order user_add ->
  user_add_notify lands instead of being refused.
* **GAP fixed** - the provider advertised caps 0x1d, so `.scan` refused with
  `needs provider caps 0x3`.  It now advertises **caps 0x1f**
  (SCAN_REQ|SCAN_PARSE|USER_ADD|USER_ADD_NOTIFY|USER_DEL) and `.scan` builds
  the 520 B g0 id0xC frame and posts it when `allow_frames>0`; the D2H decode
  stays best-effort (g1 id15 trailer [U]).

Both fixes are compile-verified and host-tested in this commit; the D2 re-run
(scan frame on hardware) is the next bench step.

---

### 3.2 Scan D2H decoders integrated (task-118, commit 469b193)

The wifi-scan-d2h lane's vendor-verified decoder is now the provider's D2H
implementation (src/hi5622_scan_d2h.{c,h}, integrated unchanged except the
shared host-type guard; hash-pinned in tools/src-baseline.sha256):

* **g1 id15** - 802.11 beacon/probe-response body plus the 20-byte trailer
  (frame_len = skb_len - 0x14 [V]).  The decoder probes the layout instead of
  assuming it (frame+trailer / bare frame / prefix variants) and validates the
  IE chain, so an unrecognised variant is -EOPNOTSUPP, never a fake BSS.  The
  signal stays 0 mBm (the trailer RSSI bytes are [U]; the decoder exposes all
  trailer fields but only fills signal when a caller opts into the heuristic).
* **g1 id16** - completion status (payload[0], success == 1 [V]): a non-1
  status is now reported to cfg80211 as `scan_done(aborted=1)`, not a fake
  success (new `hi5622_scan_model_complete()` in the portable model).
* **g1 id17** - per-channel result fully decoded (slot, channel, frequency,
  fixed 124-byte frame); logged per event and counted, for the bench
  observables ("scan running, no BSS" vs "decode gap").
* **g2 id2** - wal_scan_comp_proc_sta, the vendor cfg80211 feed/completion
  point [V]: decoded (status/core/chip) and treated as a **completion
  candidate** exactly like g1 id16, so a scan cannot hang waiting only for
  g1 id16.

The signal value remains 0 until the trailer RSSI byte is correlated on the
bench.  The bench-observable decision table (firmware produced nothing vs
decode gap vs cfg80211 channel-not-listed) is in
`../wifi-scan-d2h/REPORT.md` sec 10.  Remaining external blocker for the full
VAP start -> user_add/notify -> scan -> results ladder: the VAP-start frame
family is currently rejected by the inherited h2d_frames allowlist (reported
by the bench; the wifi-vap-start lane is fixing it) - our own provider frames
(g0 id7/8/9/0xC) already pass the same validator.

---

## 4. The frame-provider contract (`hi5622_frames_api.h`)

One statically allocated `struct hi5622_frame_ops` object is registered with
`hi5622_frame_ops_register()` (validated: name, capability bits, every
capability must have its callback).  The **generic registry** still defaults
to empty; the committed module now registers exactly ONE provider,
`vapscan-0e4ad45` (hi5622_vapscan_provider.c), built on the task-110
builders.  Its caps are `SCAN_REQ|USER_ADD|USER_ADD_NOTIFY|USER_DEL`
(0x1f): SCAN_REQ + SCAN_PARSE (the cfg80211 `.scan` op requires both) +
USER_ADD + USER_ADD_NOTIFY + USER_DEL.  `SCAN_PARSE` is always advertised; the
decoder is tolerant because the g1 id15 trailing descriptor is [U].  AP start/stop caps stay absent (no
builder).  `hi5622_frame_post()` still cannot write anything unless
`allow_frames>0`.

| capability | callback | message / data |
| --- | --- | --- |
| `SCAN_REQ` | `build_scan_req(wire, cap, &len, params)` | g0 id0xC hmac_enc_scan_send_req, payload 508 B [U] |
| `SCAN_PARSE` | `parse_scan_result(payload, len, result)` | D2H g1 id15 BSS - task-118 vendor-verified decoder [V] |
| `SCAN_PARSE` | `parse_scan_complete(payload, len, complete)` | D2H g1 id16 completion status [V] |
| (optional) | `parse_scan_channel` / `parse_scan_sta_complete` | D2H g1 id17 channel result [V] / g2 id2 WAL completion [V] |
| `USER_ADD` | `build_user_add(...)` | g0 id7 hmac_user_add, payload 168 B [U] |
| `USER_ADD_NOTIFY` | `build_user_add_notify(...)` | g0 id8 notify-alg [U] |
| `USER_DEL` | `build_user_del(...)` | g0 id9 hmac_del_user_notify_dmac, 12 B exact [V] |
| `AP_START` | `build_ap_start(...)` | WAL cfg group-2 start-AP, gap G1 [U] |
| `AP_STOP` | `build_ap_stop(...)` | WAL cfg group-2 stop-AP, gap G1 [U] |

Builder contract: fill a **complete HCC wire frame** (12-byte header + payload,
tag 0x5A5A, total == len) and return 0; return `-EOPNOTSUPP` for an undecoded
payload, `-EINVAL`/`-ENOSPC` on malformed input. The kernel always re-validates
the finished frame with the inherited `h2d_frames` library
(transport + firmware tables + non-NULL handler) before a write, so a builder
bug cannot post a malformed frame. Decoder contract: never read past
`payload+len`, bound every field; a missing decoder is a counted error, never
a guessed offset.

The neutral parameter structs passed to the builders are cfg80211-agnostic:
scan request (user id, iftype, flags, up to 12 SSIDs, up to 32 centre
frequencies restricted to OUR advertised channels, up to 128 B IEs), VAP add
(name/mac/iftype/user/vap ids), VAP del (12-bit user id or 0x0fff + addr), AP
start (SSID, centre freq, width, beacon/DTIM, privacy/auth, opaque crypto
summary, beacon head/tail bounded to 512/256 B), AP stop.

### 4.1 Integrated provider (task-110 builders)

`src/vapscan_frames.c/.h` are integrated from the wifi-vap-scan lane
(task-110, commit 0e4ad45) with one **portability-only** edit (shared host-type
guard; no protocol byte changed; `src/hi5622_vapscan_map.c` maps the neutral
params to the builders, including the [I] channel index rule
`idx = band-table position` and the [V] bench fact that the firmware scan
handler hardcodes VAP 0.  The provider always installs the tolerant g1 id15
decoder: it strips the 20-byte [U] trailer, validates the
802.11 beacon/probe-response, derives the channel from the DS or HT-operation
IE, and reports signal 0 mBm (UNKNOWN) instead of inventing the descriptor
RSSI.

### 4.2 Replacing/extending the provider (handoff contract)

1. Copy another lane's `vapscan_frames.c/.h`-style pure-C library into
   `src/` and add its object to `src/Kbuild`.
2. Add a tiny adapter object (example contract):

```c
static int hi5622_vs_scan_req(u8 *wire, size_t cap, size_t *len,
                              const struct hi5622_scan_req_params *p)
{
        struct vs_frame f;
        struct vs_scan_params sp = { .user_id = p->user_id, /* map fields */ };
        if (vs_build_scan_req(&f, p->user_id, &sp, 0))
                return -EOPNOTSUPP;
        return vs_frame_export(&f, wire, cap, len);      /* complete HCC wire frame */
}
static const struct hi5622_frame_ops vs_provider = {
        .name = "vapscan-v1",
        .caps = HI5622_FRAME_CAP_SCAN_REQ | HI5622_FRAME_CAP_SCAN_PARSE |
                HI5622_FRAME_CAP_USER_ADD | HI5622_FRAME_CAP_USER_DEL,
        .build_scan_req = hi5622_vs_scan_req, /* ... */
};
/* module_init: hi5622_frame_ops_register(&vs_provider); */
```

3. Run `./tools/build.sh`; `gen_cfg80211_symvers.py --check` and
   `selftest.py` must stay green (if the adapter adds cfg80211 symbols,
   regenerate the provider file with `--update`).
4. Only then: `TEST-LADDER.md` Mode P, with `allow_frames` raised deliberately
   in an attended session. The provider registry accepts exactly ONE provider
   and validates capability/callback consistency at registration time.

---

## 5. cfg80211 op -> firmware message mapping (deltas to the inherited 39-row map)

| op | milestone | before this lane | now | H2D message | D2H |
| --- | --- | --- | --- | --- | --- |
| `scan` | M2 | stub `-EOPNOTSUPP` | provider-gated implementation: build g0 id0xC, post through the gate; BSS from events; exactly one `scan_done`; 5 s bound | g0 id0xC (508 B [U]) | g1 id15 bss / g1 id16 complete / g1 id17 channel, g2 id2 [I] |
| `abort_scan` | M2 | not registered | abort flag observed by the work item -> `scan_done(aborted=1)` | - | - |
| `add_virtual_intf` | M1 | netdev only, "HCC add pending" | netdev model + attempt g0 id7 (+id8), failure recorded, netdev usable | g0 id7/id8 (168 B [U]) | g1 id3 config-syn [V] |
| `del_virtual_intf` | M1 | netdev teardown only | + attempt g0 id9 before teardown (exact 12 B builder [V]) | g0 id9 | g1 id3 |
| `start_ap` | M3 | deferred (NULL) | registered, provider-gated design path for the WAL group-2 AP start; experimental | g2 WAL cfg (G1 [U]) | g1 id3 / beacons |
| `stop_ap` | M3 | deferred (NULL) | registered, provider-gated | g2 WAL cfg (G1 [U]) | - |
| `get_station`/`dump_station` | M2 | `-EOPNOTSUPP` stubs | unchanged (station events [U]) | - | g2 3/4/5/6 [I] |

**Payload builders for the four implemented messages now exist** (task-110,
integrated here): g0 id7/id8/id9/id0xC are built byte-exactly by
`vapscan_frames.c` + the neutral-parameter mapping.  The cfg80211 op remains
provider-gated on `SCAN_REQ|SCAN_PARSE`, and the provider advertises both,
so `.scan` posts the real 520 B g0 id0xC frame when the post gate is open;
the D2H scan-result payloads are still [U], which only limits what results can
be reported.

The scan-request handler CA and risk class are inherited from the committed
catalogue: `hmac_enc_scan_send_req` (g0 id0xC) has a firmware handler that
reads payload words (risk PTR_DEREF [V, h2d_catalogue]), which is exactly why
the lane refuses to synthesize the payload and validates the finished frame
against the firmware tables before posting.

---

## 6. The fail-closed post gate (`hi5622_frame_post.c`)

Wire order (same vendor order the validated h2d_send path uses): body copy ->
SR node {devva, (len<<16)|0x6D2B} -> wmb -> packed producer (low10 | phase<<10,
depth 32) -> wmb -> `hcc_core_sr_commit()` (allowlisted coalesced mailbox id-3
announce) -> bounded out[0] consumption poll (200 x 5 ms, **no retry**).

Refusals happen **before any write**:

| gate | reason |
| --- | --- |
| `allow_frames` | 0 = fail-closed default; >0 = per-module-load budget of provider-built frames (independent of `h2d_send_budget`) |
| `frame_gap_ms` | minimum gap between two provider frames (default 100 ms) |
| structure | `h2d_frame_decode` transport + firmware table + handler-present; tag/length/dst/alloc/group rules |
| id 5 | mailbox id 5 is the firmware BUG() trap - always refused |
| state | windows programmed, SR/DR armed, golden boot latched, 0x5A5A released, core phase **ACTIVE** (needs ctx-create + TXQ sync), lane open, no inflight announce, live out[0] == 0 |
| buffer | SR0 slot within depth, SR buffer inside the host DMA window |

A refusal is logged once and returns the error; a committed-announce timeout
closes the lane and is recorded - never retried. After the wire write the core
refusing the SR commit is a hard error (record producer/out[0], power-cycle).

---

## 7. Integration diff against the inherited wifi-cfg80211 tree

New files (this lane): `hi5622_frames_api.h`, `hi5622_frames_provider.c`,
`hi5622_frame_post.h/.c`, `hi5622_scan.h/.c`, `hi5622_scan_logic.h/.c`,
`hi5622_vapscan_map.h/.c`, `hi5622_vapscan_provider.c`,
`cfg80211-provider.symvers` (regenerated: 9 symbols).
Integrated from the sibling lane: `vapscan_frames.h/.c` (task-110, commit
0e4ad45; the header carries the portability-only type-guard edit, the .c is
byte-identical and hash-pinned in `tools/src-baseline.sha256`).

| inherited file | edit | why |
| --- | --- | --- |
| `src/Kbuild` | + 7 objects | build the new layer + the integrated task-110 builders |
| `src/hi5622_cfg80211.h/.c` | embed `struct hi5622_scan`; init/remove/status hooks | scan layer lifecycle tied to the wiphy; exactly one scan_done on removal |
| `src/hi5622_cfg80211_ops.c` | real `.scan/.abort_scan/.start_ap/.stop_ap`; VAP add/del attempts the g0 messages | the actual wiring; M2 stubs unchanged for stations |
| `src/hcc.c` | call `hi5622_scan_deliver()` for every frame before the core | g1 15/16/17 / g2 2 are outside the core's groups |
| `src/d2h.c` | deliver the **full staged payload** (`p + 12, plen`), not the 48-byte diagnostic copy; staging buffer moved from stack to `rd->snap` | scan results carry IEs far beyond 48 B; keeps the service-thread stack small |
| `src/hi5622_contract.h` | `HI5622_D2H_SNAP_MAX` 96 -> full HCC frame | capacity for one whole frame snapshot |
| `src/hi5622_d2h.h` | + `rd->snap[]` | staging buffer owned by the serialized reader state |
| `src/h2d_frames.h` | shared portable-type guard | host tests include both portable headers under `-pedantic` |
| `tools/gen_cfg80211_symvers.py` | Module.symvers grammar (no comments, export column) + KDIR default | modpost parses the file strictly; the old format was a latent parse error |

All other inherited files are hash-pinned in `tools/src-baseline.sha256` and
verified by `tools/selftest.py`.

---

## 8. Device->host event routing (why g1 15/16/17 / g2 2 can be parsed at all)

The direct reader used to feed the core only the 48-byte diagnostic copy of a
frame; a scan result's IEs would never fit. The lane delivers the full staged
payload (up to one maximum HCC frame, 0x680 B) from the ring buffer snapshot to
`hi5622_hcc_deliver()`, which now first calls `hi5622_scan_deliver()`:

* classification is pure C (`hi5622_scan_classify()`): g1 15 bss, g1 16
  complete, g1 17 channel, g2 2 station scan response [I]; anything else is
  ignored by the scan layer;
* the delivery hook uses **mutex_trylock** so the HCC service thread never
  blocks on a cfg80211 op; a lost event is counted (`dropped_busy`) and the
  5 s bound still closes the scan;
* a result is validated (`hi5622_scan_result_validate`: unicast BSSID,
  advertised channel, bounded IEs/SSID, sane mBm signal) before
  `cfg80211_inform_bss()`, and the referenced BSS is released with
  `cfg80211_put_bss()`;
* a complete event sets the finish decision; the delayed work reports
  `cfg80211_scan_done()` exactly once (scan work or driver removal), never
  before `.scan` returned 0 (a `posting` latch prevents that race).

---

## 9. Verification (all device-free)

| check | result |
| --- | --- |
| host scan/AP/provider/vapscan/scan-d2h selftest (284 checks) | **284/284 PASS** |
| host inherited cfg80211 VAP/op-map selftest | **454/454 PASS** |
| host inherited H2D protocol selftest | **242/242 PASS** |
| kernel module build against project 6.18.60 (-Werror, modpost) | PASS, **0 warnings**, 704,720 B, sha256 20d38c5c... |
| cfg80211 imports | 9 (incl. scan_done / inform_bss_data / put_bss) |
| provider-symbols check vs net/wireless exports | PASS 9/9 |
| static source/contract selftest | **317/317 PASS** (one scan_done call pair, fail-closed defaults, provider caps, task-118 decoder wiring, D2H hook, baseline parity, configs, docs, no vendor material) |

The selftest explicitly proves the fail-closed properties: provider registry
starts empty, `allow_frames` has no initializer (0), ops-table entries are
provider-gated, `scan_done` appears only in the work/remove paths, and the
only INHERITED exact message the test double builds (g0 id9 del_user)
round-trips through the inherited h2d_frames validation.

---

## 10. Bench ladder (detail in TEST-LADDER.md)

Summary - with the default load (`allow_frames=0`) the steps are **negative
controls**, i.e. the expected result is a clean refusal with no device write;
Mode P exercises the real task-110 H2D frames and the best-effort scan path
(the scan decoder is always installed):

1. load cfg80211.ko + hi5622_wifi.ko (`allow_frames=0`) -> probe only;
2. inherited C1-C6 boot ladder (prepare/upload/verify/seed/hcc_arm/hcc_loop/
   release, 0x5A5A only) -> ready handshake -> wiphy registered;
3. VAP add/up -> `cfg80211_status` shows empty provider, `posted=0`;
4. `iw dev wlan0 scan` -> expected `Operation not supported (-95)`, dmesg
   provider warning, golden page/mask unchanged;
5. hostapd OPEN -> expected `start_ap` `-EOPNOTSUPP`;
6. hostapd WPA2 -> same, or (with a future provider) stops at `add_key` (M3);
7. wpa_supplicant -> expected failure at scan prerequisite / CONNECT (M5).

Mode P (provider adapters present, `allow_frames>0`, attended): request post ->
exactly one `scan DONE` within 5 s -> BSS entries; stop rules in the ladder.

---

## 11. Risks and limitations

| # | risk | impact | mitigation |
| --- | --- | --- | --- |
| R1 | H2D payloads delivered by task-110 are [V]/[I] vendor-derived, **not hardware-validated** | a builder byte may still be rejected/fault the firmware | existing post gate + one-frame bench budget + Ladder V1/V2/S1 stop rules; no frame is sent with `allow_frames=0` |
| R11 | provider registered but frame gate closed | every message op logs a refusal (EPERM/EBUSY) until `allow_frames>0` | status file shows gate state; benching raises the budget only in an attended session |
| R12 | firmware scan handler hardcodes VAP 0; host-only M1 netdev has no firmware VAP object | a scan request may be a graceful refusal with no D2H results | scan path accepts "no results" and always reports a bounded `scan_done`; VAP-create follow-up is the next RE item |
| R13 | the VAP-start frame family is currently rejected by the inherited h2d_frames allowlist (bench report; wifi-vap-start lane is fixing it) | the full VAP start -> user_add/notify -> scan -> results ladder cannot run until both land | our provider frames (g0 id7/8/9/0xC) pass the same validator; the allowlist fix is tracked as an external dependency |
| R2 | WAL group-2 AP-start payload is gap G1 [U] | start_ap cannot be real | design + provider stub, clearly marked experimental |
| R3 | provider frames are not EXACT captured builders | firmware handlers read payload words (id0xC risk PTR_DEREF [V]) | structural validation + handler-present + id-5 refusal + registration validation + bench stop rules; a malformed builder frame is refused before any write |
| R4 | `allow_frames` could be raised blindly | budget-limited frames could reach the firmware | default 0; status file exposes the value; ladder requires preflight and one-frame-per-idea discipline |
| R5 | full-payload staging increases the reader state by ~1.6 KB | memory/behaviour change in a validated component | buffer owned by serialized reader state (no stack growth); full inherited selftests re-run green |
| R6 | trailer RSSI byte is [U]; the 802.11 body is decoded vendor-verified | signal reported 0 mBm until correlated | decoder exposes raw trailer fields, signal opt-in only; unsupported variants return `-EOPNOTSUPP` |
| R7 | scan work vs cfg80211 request lifetime | multiple scan_done or use-after-free | exactly one scan_done site pair; posting latch; removal aborts; static+host checks |
| R8 | cfg80211 BSS entries visible to all userspace | garbage could mislead netifd/hostapd | strict result validation before inform |
| R9 | start_ap returning 0 is not an AP milestone | overclaiming | dmesg says EXPERIMENTAL; ladder requires keys/beacons/station events/data path as separate milestones |
| R10 | PD4-patched firmware is a research artifact | bench-only path | never upstream; firmware remains closed; documented in project ledger |

---

## 12. References

* `../wifi-cfg80211/` REPORT.md (M0-M2 model, 39-op map), BUILD-NOTES.md,
  src-baseline.sha256 - the hardware-validated base.
* `../wifi-h2d-send/` BENCH-PROCEDURE.md - the boot/ctx-create/TXQ sequence
  the scan ladder follows; REPORT.md - post gate semantics.
* `../wifi-vap-scan/` - the g0 id0xC / id7 / id8 / id9 payload builders
* `../wifi-scan-d2h/` - task-118 vendor-verified D2H scan decoder (g1 id15/16/17,
  g2 id2), bench observables and the decode-gap decision table (integrated
  here as src/hi5622_scan_d2h.{c,h}).
  (task-110, commit 0e4ad45) integrated here as the module's frame provider;
  its recipes/frames.md has the byte-exact bench frames and SR node word1.
* `../wifi-proto-seq/` - g1 id3 / g1 id0x12 / calibration decodes.
* `../wifi-vendor-userspace/` - vendor userspace interface scope, 39-op map,
  D2H scan ids [I].
* `../wifi-upstream-target/CFG80211-BENCH-IMAGE.md` - the image + cfg80211.ko
  + clean driver deployment sequence.
* STATUS.md sec 3h-3k and the 2026-11-24/25 ledger entries - the live state.

**No vendor material:** this report and the lane contain no vendor source,
object, firmware byte, calibration value, credential or per-unit identity; the
PD4 patched image and Wi-Fi PHY block stay local bench inputs.
