# Scan completion + beacon IE decode (lane wifi-scan-complete, task-123)

**Status:** host-only, compile-verified, host-selftested. NOT bench-fired by
this lane; the attended procedure is TEST-LADDER.md. The scan/vap-start parent
path is hardware-validated (2026-11-26 milestone: mode-1 VAP create -> g0 id0xC
scan request -> firmware g1 id17 / g1 id15 results). Nothing here sends a frame
unless the bench raises `allow_frames`, and no vendor binary, firmware byte or
calibration value is reproduced.

**Date:** 2026-11-26. **Task:** shared task-123. **Write scope:**
`opensource/docs/incoming/wifi-scan-complete/` only.

Evidence markers follow the project convention: **[V]** verified from a named
artifact/decode, **[V\*]** hardware-measured by an earlier bench lane, **[I]**
inference, **[U]** unknown (never guessed in code).

---

## 0. Headline

The 2026-11-26 14:00 milestone delivered the project's first real firmware scan
results, but the host closed the scan early and the BSS layer was thin:

* requested: 13 channels (2.4 GHz 1..13), 520-byte g0 id0xC frame;
* received inside the provider's fixed **5 s** bound: **6** g1 id17 per-channel
  results (channels 1..6) and **6** g1 id15 beacon frames (509 B each), decoded
  into 6 cfg80211 reports; **complete=0, timeout=1 -> finish-abort**
  (ledger 2026-11-26 14:00, bench dmesg/rd_catalog [V\*]);
* consequence: channels 7..13 were never collected and `iw dev wlan0 scan`
  ended on an aborted scan.

This lane makes the scan run to the end and makes each beacon a complete BSS:

1. **Auto scan window.** The measured per-channel cost is ~830 ms
   (6 channels / 5000 ms), so the request now gets
   `3000 + n_channels * 900` ms - **14,700 ms for 13 channels** - clamped to
   45 s. `scan_timeout_ms` overrides it (0 = auto); `scan_dwell_ms` can sweep
   the g0 id0xC dwell field (default remains the vendor 60).
2. **Host-side completion rule.** The scan is finished **successfully** when
   every requested channel has been reported (g1 id17) and a 500 ms settle
   window has passed, so the missing/unreached g1 id16 g2 id2 completion cannot
   turn a full 13-channel scan into `scan_done(aborted=1)`. A firmware
   completion (g1 id16 status 1 / g2 id2) still wins immediately; a firmware
   failure status or the bounded timeout still reports aborted.
3. **Full BSS decode.** The g1 id15 payload (802.11 beacon/probe-response body
   plus the 20-byte trailing record [V]) is walked element by element: SSID, DS
   parameter set, HT capabilities/operation, VHT, RSN, WMM, country and extended
   capabilities are decoded into an explicit summary, the channel/frequency is
   derived from DS or HT operation, the capability ESS/IBSS/privacy bits are
   extracted, and the validated raw IE chain is handed to
   `cfg80211_inform_bss()`. A per-BSS dmesg line
   (`cfg80211: BSS <bssid> '<ssid>' <freq> MHz (DS ch N) ...`) is the bench
   evidence that "iw scan lists BSSs, not just channel slots".
4. **Honest finish reasons.** The DONE line now distinguishes
   `finish-ok[channel-coverage]`, `finish-ok[complete-event]`,
   `finish-abort[host-timeout]`, `finish-abort[complete-failed]` and
   `finish-abort[abort-request]`, and reports `chans=seen/total` plus the
   decode/inform error counters.

---

## 1. Inputs, pins, conventions, safety

This lane is the **wifi-scan-ap** tree (task-113, module built and bench-fired
on 2026-11-26) with the **wifi-vap-start provider patch** (task-117) applied,
plus the changes below. The base provider advertises caps 0x1f (scan_req +
SCAN_PARSE + user_add + user_add_notify + user_del); with the task-117 VAP
add/start builders the module advertises caps 0x19f. The cfg80211 `.scan` op
requires SCAN_REQ|SCAN_PARSE either way. Base pins:

| input | pin |
| --- | --- |
| wifi-scan-ap src (provider, decoders, post gate) | opensource commits `edc80ba`/`231a39d` tree, module 704,720 B sha256 `20d38c5c...` |
| wifi-scan-d2h decoders (task-118) | integrated src, host vectors 85/85 |
| wifi-vap-start provider patch (task-117) | `provider/vapstart-provider.patch`, module 738,112 B sha256 `e09dc697...` |
| vendor firmware / module / calibration | local-only bench inputs, never read or copied by this lane |

Conventions: firmware CPU address = file offset + 0x40000; vendor `.ko`
offsets are .text section offsets; D2H payloads are handed to the decoders with
the 12-byte HCC header stripped (`d2h.c` rd_deliver).

Safety: no device access in this lane. The only wire path is
`hi5622_frame_post()` and its default gate is `allow_frames=0`
(fail-closed). No register is read or written by the changed code; the golden
page/state check, recovery door and attended rules are unchanged (BENCH-FACTS /
RUNBOOK).

---

## 2. The 2026-11-26 scan result and what it proves

Bench observables from the mode-1 ladder on the PD4-patched vendor firmware
(rebuilt module, params
`allow_frames=12 vap_start_first=1 vap_start_mode=1 vap_start_module=0
frame_gap_ms=100 h2d_send_budget=8`):

| observable | value |
| --- | --- |
| g0 id0xC scan request | 520 B total, CONSUMED (5/12) |
| g1 id17 channel results | 6 x 124 B, slots 0..5, channels 1..6 (2412..2437 MHz) |
| g1 id15 BSS results | 6 x 509 B total (802.11 body + 20 B trailer) |
| driver scan counters | results=6 bss=6 complete=0 channel=6 sta=0 unknown=0 timeout=1 |
| final cfg80211 call | `cfg80211_scan_done(aborted=1)` after the 5 s bound |

Reading (all [I] unless marked):

* The scan request was accepted and the firmware scan engine ran: the earlier
  2026-11-26 06:30 zero-result session was a **VAP-object refusal** (firmware
  handler g0 id0xC CA 0x6494C looks up VAP 0 via CA 0x581C8 and returns 100
  without scheduling a scan - scan_d2h REPORT [V]); the task-117 mode-1 VAP
  create (g0 id1 keys 0x64/0x65) removed that precondition, on top of the
  user_add g0 id7 + notify g0 id8 frames that create the firmware user object.
  Results flowing is the proof.
* The 6 g1 id17 events arrived over the full window (~1 per 830 ms): the
  firmware was still scanning when the host gave up. `complete=0 / timeout=1`
  means **no completion event was seen before the host bound** - this
  finish-abort is a **host-side bounded timeout**, not a firmware abort and not
  a refusal. Only a g1 id16 status != 1 would be a firmware-reported failure.
* 6 BSS results for 6 channels is consistent with a 6-AP test environment; it
  does not prove the firmware emits g1 id17 only when a BSS is found (a
  per-channel heartbeat is what the task-118 sender RE says). The completion
  rule below therefore accepts either shape: all requested channels reported OR
  a firmware completion OR the bounded timeout.

---

## 3. RE: scan timing and completion semantics

### 3.1 The g0 id0xC request fields (schema [V], semantics [V\*]/[I])

520 B wire total = 12 B HCC header + 508 B payload,
`hmac_enc_scan_send_req` (task-110 builder, vendor-verified):

| offset | field | value sent | evidence |
| --- | --- | --- | --- |
| +0x000 | flags0 | 1 | vendor builder [V] |
| +0x001 | flags1 | 0 (single band) | [I] |
| +0x002 | has_bssid | 1 | [V] |
| +0x003 | n_ssids | request count (1 wildcard for `iw scan`) | [V] |
| +0x00B | type_b | 2 = active scan | [V]/[I] |
| +0x00C | bssid | broadcast | [V] |
| +0x018 | 8 x 36 B SSID slots | {str, 0} | [V] |
| +0x140 | up to 42 x 4 B channel entries | {num, band, 0, idx} | [V] |
| +0x1EC | channel count | 13 (bench band) | [V] |
| +0x1EE | dwell | **60** (vendor default; 20/40/60 family) | [V] |
| +0x1F0 | scan_type | 0 (firmware handler rejects 4 and 8) | [V] |
| +0x1F1 | one | 1 | [V] |
| +0x1F2 | type | 11 (vendor STA scan) | [V] |

Firmware side [V]: the g0 id0xC handler (CA 0x6494C) validates field B, looks
up VAP 0, reads payload+0x13B into VAP+0x502 and tail-calls the scan engine
(CA 0x644A4) which rejects scan_type 4/8, requires the VAP scan-in-progress byte
(VAP+0x51C) to be clear, copies the 508-byte request into VAP+0x300 and starts
the scan timers/work. The provider now passes the requested channel list and
the (optional) dwell override into that builder unchanged otherwise.

### 3.2 Measured pacing and the auto window

The only timing measurement available is [V\*]: 6 per-channel results within
5000 ms -> **~830 ms/channel**. A 13-channel request therefore needs ~10.8 s
plus completion/latency margin. Fixed window per request:

    timeout = clamp(3000 + n_channels * 900, 1000, 45000) ms
    n = 13  ->  14,700 ms                      (scan_timeout_ms = 0 = auto)

`scan_timeout_ms` (uint, 0644, default 0) overrides the formula for a bench
run (clamped to [1000, 45000]); `scan_dwell_ms` (uint, 0644, default 0 = keep
the vendor 60) overrides the dwell field when in 20..200 ms, so a sweep can test
whether the ~830 ms/channel cost tracks the dwell value (the observed cost is
~14x the nominal 60 ms, which suggests either a non-ms dwell unit or additional
per-channel scan-engine cost - both [U]/[I] until measured).

### 3.3 Completion families

| event | vendor consumption | meaning for us |
| --- | --- | --- |
| g1 id16 `hmac_scan_proc_scan_comp_event` | status byte frame+0xC, compared with 1; scan timer unregistered | completion; **status == 1 is the only proven success value [V]**, anything else is reported as `complete-failed` -> aborted |
| g2 id2 `wal_scan_comp_proc_sta` | wal_scan_comp_proc_sta -> wal_inform_all_bss -> cfg80211 feed | completion candidate; status semantics [I]/[U], so it only latches "completion seen" (never a fake failure) |
| g1 id17 per-channel | hmac_scan_process_chan_result_event | 124 B fixed; slot + 108-byte per-channel record; used as the coverage heartbeat |
| g1 id15 BSS | hmac_scan_proc_scanned_bss -> hmac_fill_bss_dscr | one BSS per frame (section 4) |

Until this lane, finishing required a completion event, and the 2026-11-26 run
showed none before the bound. The scan model now has a **host-side completion
rule** in addition to the firmware events:

1. an explicit completion event (success) wins immediately;
2. a completion event reporting failure -> aborted (firmware failure, not a
   timeout);
3. otherwise, when **every requested frequency has been reported by g1 id17**
   (distinct, order-independent; duplicate/foreign channels do not count) the
   scan waits a **500 ms settle window** (so BSS frames emitted just after the
   last channel report are still decoded and informed) and then finishes
   **successfully** as `channel-coverage`;
4. otherwise the bounded timeout -> aborted as `host-timeout`.

The rule is host-side only; it never changes a wire byte and never claims a
success the firmware did not reach unless every requested channel was actually
reported. It is also why the 13-channel bench pass is now achievable even if
the firmware does not emit g1 id16 on this path ([U]).

---

## 4. g1 id15 beacon decode into complete cfg80211 BSS entries

### 4.1 Layout (task-118 RE, [V])

Payload = complete 802.11 beacon (subtype 8) or probe-response (subtype 5)
frame || 20-byte trailing record. The vendor host computes
`frame_len = skb_len - 0x14` and hands the tail to its BSS filler; the decoder
probes layouts (frame+20, bare frame, 12-byte-prefixed variants) and requires
the IE chain to land exactly on the layout boundary, otherwise -EOPNOTSUPP.

### 4.2 Decoded BSS facts (this lane)

From the 802.11 fixed fields: BSSID (addr3), TSF, beacon interval, capability.
From the validated IE walk:

| fact | source | use |
| --- | --- | --- |
| SSID + hidden flag | IE 0 | cfg80211 entry, dmesg line |
| DS channel | IE 3 | channel/frequency |
| HT capabilities info | IE 45 (26 B) | HT summary + cfg80211 IEs |
| HT operation primary + secondary offset | IE 61 (22 B) | channel fallback (5 GHz), 40 MHz fact |
| VHT cap/op presence | IE 191/192 | presence bit |
| RSN presence | IE 48 | security presence |
| WMM presence | vendor IE 221 OUI 00:50:f2 type 2 | QoS presence |
| country / extended capabilities | IE 7 / 127 | presence bits |
| ESS / IBSS / privacy | capability bits 0 / 1 / 4 | cap_flags |

The raw IE chain (SSID first) is forwarded verbatim to
`cfg80211_inform_bss()` together with BSSID/TSF/capability/beacon interval;
cfg80211 parses SSID/DS/HT itself and builds the scan-list entry. The explicit
summary exists for two reasons: the per-BSS dmesg evidence line
(`cfg80211: BSS ... ie=.../... ht_cap=1 ht_op=1 rsn=... wmm=...`) and the
bench decision table ("decoded N BSS: all have SSID+DS, M have HT").

### 4.3 Error paths (fail closed)

* malformed/truncated IE chain -> -EOPNOTSUPP, counted as `parse` (never a BSS);
* non-beacon/probe-response management frame -> rejected;
* no derivable channel -> rejected (cannot place the entry);
* a wiphy that does not list the decoded frequency -> `inform_fail` counter and
  a ratelimited warning, no fake entry;
* `cfg80211_inform_bss()` returning NULL -> `inform_fail`, never silently
  dropped;
* cfg80211 signal is **0 mBm** by default. The 20-byte trailer has two
  signal-like signed bytes (+5/+6, [I]); `scan_trust_rssi=1` opts into the
  +5 heuristic for a bench correlation run only.

---

## 5. Implementation diff

Over the task-117 base tree (wifi-scan-ap + provider patch):

| file | change |
| --- | --- |
| `hi5622_scan_logic.h/.c` | auto window helper, channel plan + per-channel coverage tracking, coverage/settle tick branch, finish reasons, n_base/consistency, dwell validation (0 or 20..200), status line `chans=seen/total` + finish block |
| `hi5622_scan.c` | `scan_timeout_ms` / `scan_dwell_ms` module params, timeout/dwell selection, channel plan registration, g1 id17 -> `hi5622_scan_model_channel()`, enriched per-BSS dmesg + inform-failure warning, DONE line with channels/finish reason/error counters, teardown abort reason |
| `hi5622_frames_api.h` | `dwell_ms` in scan request params; BSS result IE summary fields (`ds_chan`, `ie_count`, `cap_flags`, `ie_flags`, `ht_cap_info`) + flag constants |
| `hi5622_scan_d2h.h/.c` | IE walk fills DS/HT-cap/HT-op/VHT/RSN/WMM/country/ext-cap presence, HT cap info, capability flags; bounds unchanged |
| `hi5622_vapscan_map.c/.h` | dwell override mapping; IE summary mapping; decoder wrapper with the signal opt-in |
| `hi5622_vapscan_provider.c` | `scan_trust_rssi` param, caps comment (0x19f), decoder opt-in |
| `tools/` | `scan_complete_selftest.c` (195 checks), updated inherited `scan_logic_selftest.c` (task-117 caps; 290 checks), build.sh step 1b + param checks, selftest.py lane checks, baseline manifest notes |
| docs | this REPORT.md, README.md, BUILD-NOTES.md, TEST-LADDER.md, patches/ |

All comments/docs identify the vendored/copied provenance of the base trees
(task-113/117/118); the new code is original clean-room C.

---

## 6. Host verification

Canonical run: `./tools/build.sh` (device-free). Exact artifact numbers are in
BUILD-NOTES.md (module size/sha256 are build-path dependent; the recorded run
is the lane's own tree).

| check | command | expected |
| --- | --- | --- |
| scan-complete selftest | tools/scan_complete_selftest.c | **195/195 PASS** |
| inherited provider/scan selftest | tools/scan_logic_selftest.c | **290/290 PASS** |
| inherited cfg80211 regression | tools/cfg80211_logic_selftest.c | 454/454 PASS |
| inherited H2D regression | tools/h2d_logic_selftest.c | 242/242 PASS |
| kernel module build | project 6.18.60 tree, -Werror | PASS, 0 warnings, depends=cfg80211 |
| provider symvers | tools/gen_cfg80211_symvers.py --check | PASS (9 symbols) |
| static lane selftest | tools/selftest.py --kdir | PASS |

What the host tests prove: the auto-window arithmetic, the coverage/settle/
completion/timeout/abort decision matrix (including duplicate and foreign
channels), the synthetic beacon/probe-response IE decode (SSID/DS/HT/VHT/RSN/
WMM/country/ext-cap, hidden SSID, 5 GHz HT-op channel, ch14, trailer, prefix
variant, negative cases), the map round-trip into `hi5622_scan_result`, the
cfg80211 result validation, and that the kernel layer wires the new model calls.
They do **not** prove the firmware timing or the on-air beacon layout: that is
the attended ladder.

---

## 7. Bench ladder summary (details in TEST-LADDER.md)

Mode-1 load: `allow_frames=12 vap_start_first=1 vap_start_mode=1
vap_start_module=0 frame_gap_ms=100 h2d_send_budget=8` (unchanged from the
proven milestone), then `iw phy phy0 interface add wlan0 type managed` and
`iw dev wlan0 scan`. Expected chain:

1. `scan request posted ... dwell 60 ms ... timeout 14700 ms (auto), host
   completion on all-channel coverage`;
2. 13 x `cfg80211: scan channel result slot=N ch=N (freq MHz)` (order may
   vary; last around 11 s);
3. one `cfg80211: BSS <bssid> '<ssid>' <freq> MHz (DS ch N) beacon ...` line
   per decoded BSS;
4. exactly one `scan DONE (finish-ok[channel-coverage], ..., chans=13/13,
   complete=0, timeout=0) - cfg80211_scan_done(aborted=0)` (or
   `finish-ok[complete-event]` if the firmware completion is emitted);
5. `iw` lists the BSS entries (SSID, channel, HT).

Pass criteria: aborted=0, chans=13/13, at least one BSS line, `inform=0`,
golden page 0x86807FEF/.../state 1 mask 0 and no firmware exception.
Negative/variant rungs (manual timeout, dwell sweep, short-bound control,
decode-gap control) and the stop rules are in TEST-LADDER.md.

---

## 7b. Integration with the wifi-bench-tooling D2 ladder (task-124)

The committed D2 scan ladder (`docs/incoming/wifi-bench-tooling`, commit
`19ee94d`) already gates on: `iw dev wlan0 scan` rc=0, the
`cfg80211: scan channel result` dmesg signature, `results>0`, golden
page/state/mask, and the provider refused/timeout/bad counters. Those gates
remain satisfied by this lane's module - it only makes the scan *finish*
instead of aborting and adds the BSS entries. The tooling lane can tighten D2
to the scan-complete pass by adding two checks:
`scan DONE (finish-ok` and `chans=13/13` (and optionally
`finish-ok[channel-coverage]`/`finish-ok[complete-event]`); the negative
control is `finish-abort[host-timeout]` with `chans=<13` (TEST-LADDER
Variant D). No tooling file is edited by this lane (write scope).

## 8. What remains [U], risks, and next steps

| item | state | note |
| --- | --- | --- |
| dwell field unit / why ~830 ms per channel | [U] | `scan_dwell_ms` sweep is the measurement (Variant C) |
| whether g1 id16 is emitted at the end of this scan path | [U] | coverage rule makes the pass independent of it; complete=1/0 is recorded either way |
| g1 id15 trailer semantics (+0/+4/+5/+6/+8/+0xC/+0x10) | [U]/[I] | signal 0 mBm default; `scan_trust_rssi` opt-in correlation only |
| g1 id16 / g2 id2 status code space | [U] | only success==1 is proven; g2 id2 status logged, not guessed |
| 5 GHz scan (19 LU channels) | [U] | same code path; channel count feeds the auto window; never measured |
| BSS entries from an aborted scan | [I] | with the coverage rule the normal run is not aborted; a timeout run reports aborted=1 honestly |
| duplicate BSSIDs across channels | [I] | cfg80211 de-duplicates by BSSID; the decoder does not |
| AP path (start_ap, hostapd) | [U] | WAL AP-config 0x127 body still gap G1; `start_ap` stays -EOPNOTSUPP |
| MSDU TX/RX | [U] | out of scope (datapath lane) |

Risks: a slow/refused firmware scan now costs one 14.7 s window per `iw scan`
(the module param can shorten it); the coverage rule can only declare success
after every requested channel was reported, so it cannot hide a partial scan;
`iw` shows whatever cfg80211 accepted, and the per-BSS dmesg lines make the
decode gap vs firmware-produced-nothing call explicit.

The remaining M2/M3 work after this lane: the AP-start WAL payload, then the
connect/assoc and data-path milestones - none of them touched here.

---

## 9. Provenance and clean-room statement

* Every protocol constant/offset used here comes from the project's committed
  RE of the vendor artifacts (task-110/113/117/118 documents) or from IEEE
  802.11 element definitions; no vendor source, object code, firmware byte,
  calibration value or secret is reproduced or committed.
* The committed C code is original; the host selftests build their 802.11
  frames synthetically (SSID/DS/HT/RSN/WMM/country/ext-cap element builders in
  tools/scan_complete_selftest.c).
* No device contact, no register write, no image/slot operation.
* Base-tree provenance: wifi-scan-ap (task-113), wifi-scan-d2h (task-118),
  wifi-vap-scan builders (task-110), wifi-vap-start provider patch (task-117);
  integration edits are listed in tools/src-baseline.sha256.
