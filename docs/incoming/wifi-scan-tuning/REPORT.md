# Scan pace/tuning: full 13-channel scan (lane wifi-scan-tuning, task-133)

**Status:** host-only, compile-verified, host-selftested.  NOT bench-fired by
this lane; the attended procedure and its PASS gates are TEST-LADDER.md.  The
baseline measurement below was made on hardware by the bench wave
(self-contained image + combined module) before this lane; the request bytes
and firmware read offsets are re-derived here from the pinned local artifacts.

**Date:** 2026-11-27.  **Task:** shared task-133.  **Write scope:**
`opensource/docs/incoming/wifi-scan-tuning/` only.  **Nothing here was sent to
the bench router by this lane.**

Evidence markers: **[V]** verified from a named artifact/decode, **[V\*]**
hardware-measured by the bench wave, **[I]** inference, **[U]** unknown.

---

## 0. Headline

1. **The request was already correct.**  Our provider builds the 520-B g0 id0xC
   request with **13 channel entries and count=13** at payload+0x1EC (hexdump +
   field map: recipes/REQUEST-BYTES.md).  A host regeneration of the builder
   confirms every field; the firmware engine reads exactly that count
   (CA 0x6462A) and iterates 13 entries.  **A wrong count field or malformed
   channel array is ruled out [V].**
2. **The firmware completed only 6 channels, quickly.**  Measured run:
   6 g1 id17 results in **0.94 s total (~156 ms/channel)** for channels 1..6,
   then no frame for the remaining ~14 s until the host window closed
   (`finish-abort[host-timeout], channel=6, chans=6/13`) [V\*].  The retired
   "~830 ms/channel" figure was an artefact of that same host-bound run: it is
   not the firmware cadence.
3. **The firmware scan architecture is one-channel-per-inner-request.**
   CA 0x63CB8 starts a scan for ONE entry of the request array (index =
   link[0xfc]+link[0xfd]); the result handler emits g1 id17 for that index and
   advances the cursor while `link[0xfd] < link[0xfe]` (CA 0x64298).  The
   cursor/end fields live in a scan-context object, not in the host request;
   a hard-coded 6 is written to `link[0x106]` when it is zero (CA 0x643CC)
   [V].  The 6-channel stop is therefore attributed to the firmware's
   per-request scan-context (ranked hypotheses, sec 4), not to the wire
   request; the host-side fix is the same in either case: **split one
   cfg80211 scan into several g0 id0xC requests** [I].
4. **Tuning implemented**: auto window from the measured cadence
   (`1500 + 250*n` ms, 13 ch -> 4,750 ms; clamp raised 45 s -> 120 s), three
   new module parameters (`scan_chan_time_ms`, `scan_req_batch`,
   `scan_batch_gap_ms`) and chained multi-request scanning with compiled
   default `scan_req_batch=6` (13 channels = 3 requests of 6+6+1, 300 ms gap).
   The per-batch model keeps the proven channel-coverage/settle completion rule;
   a per-batch firmware completion advances the chain instead of ending the
   scan; every failure path stays an honest abort.
5. **Verification**: 37/37 new host checks, inherited host suites still green
   (196/196 + 290/290 + 454/454 + 242/242), kernel module builds clean on
   6.18.60 (stripped 246,688 B sha256 05dd5f6a...; vermagic
   `6.18.60 SMP mod_unload ARMv7 p2v8`; 9 cfg80211 imports, unchanged).
   Target floor stated in sec 6: full 13/13 should be reachable in ~4-6 s of
   firmware time; ladder PASS bound is 20 s.

---

## 1. Inputs and pins

| input | pin |
| --- | --- |
| diff base | bench-validated **combined AP+DP** source tree (ap-start v7 + dp-air merge + scan-complete); fired artifact 242,572 B stripped sha256 `44c6cc12...` |
| local vendor firmware (RE only, never committed) | original `FIRMWARE.bin`, sha256 `7fc87e2051e80b5e...`, 928,920 B; CPU address = file offset + 0x40000 |
| local vendor host module (RE only, never committed) | `hi5622v100_wifi.ko`, sha256 `de78ec07e46e70ce...`, 3,564,728 B; .text offsets in this report |
| baseline bench run [V\*] | self-contained image ae5e28bf; combined module 242,572 B sha 44c6cc12; insmod `fw_path=PD4 allow_frames=16 vap_start_first=1 vap_start_mode=1 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=4`; golden boot page 0x86807FEF/7/2/0xABCD, C2 clean, mode-1 VAP base |
| static RE helper | `tools/fw_scan_timing_re.py` (asserts every CA cited below from the two pinned artifacts) |
| host selftests | tools/scan_tuning_selftest.c + the inherited scan-complete/scan-logic/cfg80211/H2D suites |

Safety: no device access by this lane; `allow_frames=0` default remains; no
vendor binary, firmware byte, calibration value or secret is copied into this
directory; the only wire family touched is the already-bench-validated
g0 id0xC scan request (split across requests, never extended/changed).

---

## 2. Baseline: what the bench wave measured [V\*]

Mode-1 load on the self-contained image, fresh golden boot, AP not running,
`iw dev wlan0 scan`:

| t (s) | observable |
| ---: | --- |
| 73.77 | `scan request posted ... (1 ssids, 13 channels, 0 IE bytes, dwell 60 ms, total 520 B)` |
| 73.91 | g1 id17 slot=0 ch=1 2412 MHz |
| 74.03 | slot=1 ch=2 2417 MHz |
| 74.15 | slot=2 ch=3 2422 MHz |
| 74.47 | slot=3 ch=4 2427 MHz |
| 74.62 | slot=4 ch=5 2432 MHz |
| 74.71 | slot=5 ch=6 2437 MHz |
| 74.71 - 87.8 | **no further scan frame** (heartbeat/service traffic continues) |
| 87.8 | `scan DONE (finish-abort[host-timeout], results=8 bss=8 complete=0 channel=6 chans=6/13)` -> `cfg80211_scan_done(aborted=1)` |

Derived: **6 channels in 0.94 s = ~156 ms/channel**; the firmware emitted no
g1 id16/g2 id2 completion before the host bound; the stop is clean (no log,
no exception mask, page stays 0x86807FEF golden).  A second observation from
the same wave: with the AP running the scan produced 0 results - consistent
with the same stop/gating rather than a pace problem (see ladder R6).

This invalidates the assumptions of the scan-complete lane's window formula
`3000 + 900*n` (it assumed every channel would eventually appear at
~830 ms/channel).  Both parts are re-tuned here.

---

## 3. The request is not the problem [V]

`tools/request_dump.c` regenerates the exact 520-B frame from the lane's
clean-room builder; the full hexdump and per-field evidence are in
recipes/REQUEST-BYTES.md.  Key facts for the diagnosis:

- payload+0x1EC (wire 0x1F8) = **13**, the count the firmware engine reads at
  CA 0x6462A (`ldrb r6,[r4,#0x4EC]`, r4 = VAP, request copied to VAP+0x300);
- 13 valid 4-B entries at payload+0x140: `{num=i+1, band=0, pad=0, idx=i}`,
  the remaining 29 slots zero (inside the 42-slot array);
- dwell payload+0x1EE = 60 (LE), scan_type +0x1F0 = 0 (4/8 rejected), one +0x1F1
  = 1, type +0x1F2 = 11 - all as vendor-verified in wifi-vap-scan;
- header family byte = 0x0C, tag 0x5A5A, total 0x0208 - consumed by the
  handler with no refusal log in the baseline run (a refusal returns 100 and
  logs; the engine was reached and the g1 id17 stream proves it ran).

So the 6-channel stop is downstream of request parsing.  **Do not "fix" the
count/array encoding; it is correct.**

---

## 4. Firmware RE: why six, and what the fix must target

### 4.1 Path (all [V], regenerated by tools/fw_scan_timing_re.py)

    g0 id0xC handler CA 0x6494C
      -> VAP-0 lookup CA 0x581C8 (NULL -> log + return 100)
      -> store payload+0x13B -> VAP+0x502 (CA 0x649B8)
      -> tail-call scan engine CA 0x644A4
         -> scan_type check (reject 4/8), VAP+0x51C busy check
         -> set VAP+0x51C = 1 (CA 0x645A2)
         -> memcpy_s 0x1FC B payload -> VAP+0x300 (CA 0x645B0, helper CA 0xC1E44)
         -> timer create (movw 0x9c40 = 40000, CA 0x64616; id stored VAP+0x518)
         -> enumerate request: count [VAP+0x4EC], band bytes [VAP+0x440+4i+1]
            (CA 0x64624-0x646D0 one-loop band counter; 13 iterations with our
            request)
         -> per-channel start CA 0x63CB8, then result/advance loop

### 4.2 One channel per inner request

- CA 0x63CB8: index = `[link+0xFC] + [link+0xFD]` (CA 0x63D10/0x63D14);
  `entry = VAP + 0x440 + 4*index`; num/band from the host request are copied
  into the channel-start path (CA 0x63D64/0x63D72) and the low-level scan is
  started (CA 0x5E028; single-channel request builder CA 0x4B88C, which
  itself writes `count=1` to payload+0x1EC, `scan_type=4` to +0x1F0 and
  `dwell=20` to +0x1EE).
- On a per-channel result the firmware emits g1 id17 (CA 0x642D0-0x64374:
  slot = link[0xFC]+link[0xFD], 108-B record from ctx+0x1688, hcc_msg_tx
  CA 0x102760 called at CA 0x64358) and advances:
  `step=link[0xFD]; end=link[0xFE]; if step<end -> CA 0x63CB8 next channel`
  (CA 0x64298 / CA 0x643EE).
- When a scan-control field is uninitialised the firmware writes a **default
  6**: `movs r3,#6; strb.w r3,[r4,#0x106]` at CA 0x643CC.

### 4.3 Where those cursor/end/control fields come from

The per-band scan-context fields (+0xFC cursor, +0xFE end, +0x104/+0x106
control/defaults, +0x103 mode) are written by the firmware's **band/channel
configuration handler, and it is a **direct group-0 H2D message, id 10**: the
group-0 dispatch table at CA 0x10CD08 (12-B rows {id, fn|1, 0}) maps
{id=10, fn=0x5FF65} to handler entry **CA 0x5FF64** (not 0x5FF60 - that was a
four-byte-misaligned window; corrected in tools/fw_scan_timing_re.py F7/F7b).
It writes the per-slot scan-context fields (+0xFC/+0xFE/+0x103/+0x104/+0x106)
from its H2D payload (CA 0x60092/0x6009E/0x600B2/0x600BA/0x603C6/0x603DC),
i.e. this is the context the per-channel scheduler later trusts.  Our
clean-room driver boots the mode-1 VAP base and the scan request but **never
sends g0 id10**, so the context keeps firmware-init defaults.  The request
count (13) is parsed, but the inner scheduler's own cursor/end pair is not the
host request's count.  See section 8 for the H1 follow-up decision.

### 4.4 Hypotheses, ranked, and the discriminator each needs

| # | hypothesis | evidence for | discriminator (ladder) |
| --- | --- | --- | --- |
| H1 | **scan-context not configured by our host** (cursor/end/defaults from g0 id1 band family; default 6) - firmware stops cleanly after its context allows | engine parses 13 [V]; inner scheduler uses context fields, not request count [V]; hard default 6 exists [V]; stop is clean/no completion [V\*] | R7 D2H catalogue after timeout: only 6 id17 -> firmware stop (H1/H2). Then a vendor-capture comparison of the g0 id1 band config sequence (open [U]) confirms H1 |
| H2 | firmware accepts only **6 explicit channel entries per request** (silent truncation) | stop is exactly at the 6th [V\*]; fits the observed cadence | R1/R2 batch=6 vs batch=13: batch=6 covering 13 channels PASSES 13/13 -> batching works regardless of which is true; batch=13 alone fails identically to baseline (control) |
| H3 | host **D2H delivery gap**: firmware emitted >6 g1 id17 but the direct reader decoded 6 | possible by construction (reader is a workaround) but the 14 s silence after a fast burst argues against a transport stall (frames would drain when the next request/heartbeat arrives) | R7 catalogue: >6 id17 frames visible in the D2H record while dmesg says 6 -> H3; only 6 -> H1/H2 |

**The chosen fix works for H1 and H2 and is the discriminator instrument for
H3**: the host splits the scan so no single request asks for more than the
observed limit, and it reports per-batch observables that the ladder checks.

---

## 5. Tuning implemented (diff over the combined bench tree)

### 5.1 Window

| item | old (scan-complete) | new (this lane) |
| --- | --- | --- |
| auto formula | 3000 + 900*n ms (13 ch -> 14,700 ms) | **1500 + 250*n ms** (13 ch -> 4,750 ms; per request/batch) |
| rationale | assumed ~830 ms/ch from a host-bound run | measured **~156 ms/ch**; 250 ms = measured + margin |
| clamp | 45,000 ms | **120,000 ms** (task asks the 45 s clamp be raised/removed) |
| manual override | scan_timeout_ms clamped [1000,45000] | scan_timeout_ms clamped [1000,120000], applies per request/batch |
| new | - | `scan_chan_time_ms` (0 = compiled 250; 50..2000) |

### 5.2 Chained multi-request scan

- `scan_req_batch` (default **6**): batch size in channels.  `0` or `>= n` =
  one request (legacy, used as ladder control); `1` = one request per channel.
- `scan_batch_gap_ms` (default 300): pause between requests (firmware
  scan-in-progress clear + late BSS flush; 0..5000).
- hi5622_scan.c keeps the full cfg80211 channel plan; for each batch it builds
  the same g0 id0xC request with only that batch's channels (same fields,
  count = batch size), starts the model window for that request, posts, and
  on host-complete coverage (all batch channels + 500 ms settle) or a firmware
  success completion for the batch, closes the batch model and posts the next
  one after the gap.  The final batch falls through to the normal model tick ->
  `cfg80211_scan_done(aborted=0)`.  The DONE line reports the last request's
  plan plus totals; per-batch dmesg lines make each request observable.
- Failure paths: first-batch build/post failure returns the errno from the
  scan op (no scan_done, cfg80211 contract as before); a chained batch failure
  after acceptance reports `scan_done(aborted=1)`; a batch timeout aborts the
  whole scan (honest partial result: chans=<partial>/13).
- Wire delta: **none in field encoding**; the only change is that the channel
  array of each request carries fewer entries.  Nothing else is sent: no new
  frame family, no config event, no extra IE bytes.

### 5.3 Portable policy surface (host-testable)

New pure helpers in hi5622_scan_logic.c: `hi5622_scan_timeout_for(n,
chan_ms)`, `hi5622_scan_batch_size/count/len(n, param, i)`,
`hi5622_scan_coverage_ready(m, now)`; the existing `hi5622_scan_timeout_auto`
now delegates to `timeout_for(n, 0)`.  The kernel layer holds no duplicated
arithmetic.

---

## 6. Verification (host-only) and the achievable floor

| check | result |
| --- | --- |
| new host selftest `tools/scan_tuning_selftest.c` | **37/37 PASS** (retuned window, clamp, batch split incl. partial last batch/empty plan/param 0/1/42, coverage-ready incl. abort suppression) |
| inherited scan-complete selftest | **196/196 PASS** (two window assertions updated to the re-measured cadence; 830 ms feed replaced by 156 ms) |
| inherited scan/provider selftest | 290/290 PASS |
| inherited cfg80211 regression | 454/454 PASS |
| inherited H2D regression | 242/242 PASS |
| kernel module build (6.18.60 tree) | PASS, 0 warnings; stripped 246,688 B sha256 `05dd5f6a...`; vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`; depends cfg80211; 9 imports unchanged |

**Achievable floor:** the firmware spends ~156 ms per channel; 13 channels is
at least ~2.0 s of firmware time.  With ceil(13/6)=3 requests and a 300 ms
gap plus the 500 ms settle window, the host-visible full scan should complete
in roughly **3.5-6 s** (no completion event depended on).  The ladder PASS
bound is **20 s total**, which leaves margin for the firmware per-request
stop/restart latency and for a slower environment; a scan that needs the
bounded timeout still ends as `aborted=1` with `chans<13` (never a fake
success).  The floor is bounded by the firmware, not by this driver: if the
batch gap is too short for the firmware to clear its scan-in-progress byte,
the ladder's R4 gap sweep is what finds the real value (0/300/1000 ms).

---

## 7. What is not proven / open items

| item | state |
| --- | --- |
| which hypothesis (H1 vs H2 vs H3) explains the 6-channel stop | open; the ladder R0/R1/R2/R7 matrix discriminates |
| whether chained requests are accepted while the firmware still has scan-in-progress set (batch gap too short -> silent refusal) | open; default 300 ms + R4 sweep; a refusal is visible as a batch with no results then host-timeout abort |
| 13/13 + BSS decode on hardware | **not proven here** - TEST-LADDER.md is the proof; this lane must not claim it |
| vendor g0 id1 band/scan config sequence (the H1 completion path) | [U]; finding it would remove the need for batching entirely |
| AP-up scan behaviour (baseline: 0 results) | [U]; ladder R6 |
| 5 GHz / 19-channel plan | untested; the helpers are generic but the bench band is 2.4 GHz |
| per-batch counters in the final DONE line | the model's counters accumulate across batches (clear keeps them); the final line still shows the last request's `chans` plan - batch lines carry the per-request numbers |

## 8. H1 follow-up: the config message is g0 id10 (not implemented here)

The H1 hypothesis is now structurally confirmed: the missing configuration is
**group-0 direct message id 10**, handler entry CA 0x5FF64 (dispatch table
CA 0x10CD08 row {10, 0x5FF65} [V]).  Payload field map (type byte +0x00 must
be 3, subtype +0x01 = 1/2, then channel/slot/end fields), the context writes
and the reason it is *not* part of this lane's module are in
**recipes/BAND-CONFIG-G0ID10.md**.

**Decision:** no speculative g0 id10 builder is bound into the module or the
final combined build.  Reasons: (a) the accepted value semantics/ordering are
[U] (only read-sites are [V]); (b) it is a firmware configuration message -
the class where wrong-order use has already caused data aborts (AP-start VAP
pointer; ctx-create precondition), so a bad frame can cost a power-cycle;
(c) batching reaches 13/13 without any new firmware interaction and is
ladder-provable now; (d) a dedicated attended H1 lane is the correct container
for firing g0 id10 (single-slot, one field per experiment, recovery armed).
The finding still matters: it names the exact message a later lane must
validate (and would allow scan_req_batch=0 single-request scans once proven).

---

**Bottom line:** the wire request was never the problem; the host window and
single-request assumption were.  The tuned defaults + chained-request scanner
are the smallest host-side change that can cover all 13 channels, and the
ladder is built so a bench run can prove it (or falsify H1/H2/H3) without
speculative register or firmware work.

---

## 9. R1 bench result and the fixes in this revision (2026-11-27)

The lead fired R1 with the first artifact (246,688 B sha `05dd5f6a`).  The
chain started but did not advance:

    scan is split into 3 g0 id0xC requests ...
    scan request batch 1/3 posted (6 of 13 channels, ch 1..6)
    scan batch 1/3 complete - next request in 300 ms
    scan DONE (finish-ok[channel-coverage], results=8 bss=8 complete=0
               channel=6 chans=6/6 ... timeout=0) -> scan_done(aborted=0)

Root cause: after arming the inter-batch gap the work item **fell through to
the model tick in the same pass**; the model's batch-local plan was 6/6
covered and its settle window had already elapsed, so the tick returned
FINISH_OK before the gap timer fired.  Second observation: 8 BSS were
parsed/informed while `iw dev wlan0 scan` printed no BSS lines - the BSS-list
visibility path needed its own evidence.

Fixes in this revision (device-free verified):

1. **Chain advance**: while the next request is pending the model tick is not
   allowed to finish the cfg80211 scan; the work item waits out the gap, closes
   only the batch-local model, and posts the next request.  An abort request
   still cuts the chain.  This is the fix that makes 3/3 batch lines reachable.
2. **Accumulated coverage**: the kernel layer tracks distinct requested
   frequencies across all batches; the DONE line now reports
   `chans=<seen>/<requested>` (13/13) and `batches=<sent>/<total>` instead of
   the last request's local counts.
3. **Late-BSS settle**: the completion predicate settles after the last channel
   **or BSS** event, so g1 id15 frames arriving just after the last channel
   report are included before `cfg80211_scan_done()`.
4. **iw-visibility readback**: immediately before scan_done the driver calls
   `cfg80211_bss_iter()` and logs
   `scan BSS list check: entries=<n> iw_visible=<m> (reported=... inform_fail=0)`.
   entries=0 -> the inform path failed (check inform_fail); entries>0
   iw_visible=0 -> entries hidden by the nl80211 `use_for` dump filter
   (ABI/version issue); entries>0 iw_visible>0 -> results are visible and an
   empty iw dump points at the iw invocation, not the driver.  This adds
   `cfg80211_bss_iter` as the 10th cfg80211 import; the flashed image's
   cfg80211.ko (sha256 949393f9) exports it (checked against the local artifact).
5. Rebuilt artifacts: **247,776 B sha256 `613989ff...`** (base+polish+scan,
   final-stack parity, `/tmp/wifi-scan-tuning/hi5622_wifi.ko`; for the ladder)
   and 247,424 B sha256 `fd2082ab...` (bare combined base).  0 warnings; host
   suites 37/37, 196/196, 290/290.  Patches 0001/0002 regenerated with all five
   files (the earlier 0002 had only hi5622_scan.c because a `set -e` aborted
   the diff loop after the first file - stability's patch-gap report).

Expected R1 dmesg after the fix: three `scan request batch k/3 posted` lines,
two `scan batch k/3 complete - next request in 300 ms` lines, 13 channel
results, `scan BSS list check: entries>0 iw_visible>0`, and
`finish-ok[...], channel=13, chans=13/13 batches=3/3, timeout=0` ->
`scan_done(aborted=0)`.

---

## 10. Second R1 fire: completion-event semantics + BSS decode fixes (R2)

The second R1 fire (artifact 247,776 B sha 613989ff) exposed two more issues:

1. a firmware completion event arrived after batch 1 (complete=1, status not 1)
   and was treated as scan-global -> finish-abort[complete-failed], so batch
   2/3 was never posted.  The firmware emits its completion event for the
   request it reached (6 channels), not for our cfg80211 scan.  Fix: while
   more batches remain a completion event terminates only the CURRENT batch
   (advance the chain); on the final batch the accumulated channel-coverage
   rule wins over a partial-request status (logged: status=0x.. but all N
   channels covered -> finishing OK).  An explicit abort still aborts.
2. all six g1 id15 BSS frames failed decode (parse=6, inform_fail=0, BSS list
   entries=0): none of the four fixed layout candidates matched.  Fix: the
   decoder now tries a relaxed alignment sweep (frame offset 0..32 x trailer
   length 0/4/8/12/16/20/24) that still requires a syntactically valid
   management beacon/probe-response with a derivable channel; the recognised
   layout is reported as "relaxed" (0x80) and the kernel logs it.  If even
   that fails, a ratelimited `scan BSS decode gap: len=.. first=<hex>` dmesg
   line prints the first 32 bytes for the next bench iteration.  The BSS-list
   readback (entries/iw_visible) is kept as the visibility gate.

Rebuilt artifacts (0 warnings; host suites 37/37, 196/196, 290/290, 7/7
relaxed-decoder): the ladder artifact is **248,668 B sha256 ca81660f...**
(base+polish+scan, /tmp/wifi-scan-tuning/hi5622_wifi.ko); bare variant
248,312 B sha256 118c3b31....  Patches 0001/0002 now carry all seven files
(incl hi5622_scan_d2h.c/.h and cfg80211-provider.symvers; both patch --dry-run
clean).  Expected R1 after R2: three batch lines (no completion-event abort
after batch 1), 13 channel results, BSS entries>0 iw_visible>0, DONE
finish-ok channel=13 chans=13/13 batches=3/3 -> scan_done(aborted=0).

---

## 11. Third R1 fire: the real g1 id15 framing + batch-window fixes (R3)

The chain advanced to batch 2/3 (batch-local completion handling works), then
the bounded per-request window expired during batch 2 with no new channel
result.  The decode-gap hexdumps (kept from R2) captured the real g1 id15
payload framing:

    len=457 first=6d011800ff000404110e000000ff0403009901ff0f80009c0f0000ff0f000000
    len=457 first=6d011800ff000004110e000000ff0403009901ff0f80006e050000ff0f000000
    len=497 first=95011800ff000404110e000000ff040300c101ff0f80009e0a0000ff0f000000
    len=497 first=95011800ff000004110e000000ff040300c101ff0f8000e80a0000ff0f000000

Decoding: the u16 at payload[0] is the 802.11 frame length (LE: 0x016d=365,
0x0195=405) and the payload is [72-byte vendor scan-result header][802.11
frame][20-byte trailer]: 457 - 20 - 365 = 72 and 497 - 20 - 405 = 72 for the
two payload sizes (497 is the previously documented 509-byte-total BSS
payload).  The four variant pairs differ only in flags/TSF-like fields, not in
the framing.  The parser now derives the frame offset from the length prefix
(layout "len-prefix"), keeps the fixed/relaxed probes as fallbacks and keeps
the decode-gap hexdump.

Chain fixes: default inter-batch gap 300 -> 800 ms; every batch after the
first gets +2500 ms of request window (batch 2/3: 3000 + 2500 = 5500 ms); a
non-first request that produces no channel result within 2000 ms is reposted
once (a silently refused request leaves no other host-visible trace).

R3 artifacts: ladder 249,580 B sha256 1710bf11...; bare 249,228 B sha256
8e6f3b17...; host tests 12/12 + 37/37 + 196/196 + 290/290; 0 warnings;
patches regenerated (7 files).  Expected R1 after R3: three batch lines,
13/13 coverage, BSS list entries>0 iw_visible>0 (the real BSS frames now
decode), DONE finish-ok chans=13/13 batches=3/3.
