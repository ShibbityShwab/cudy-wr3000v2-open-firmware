# TEST-LADDER - attended bench proof for a full 13/13-channel scan (task-133)

Host-only lane: **this ladder is the proof, not this lane.**  Every rung is
attended (bench rules: BENCH-FACTS/STATUS/RUNBOOK; recovery door armed first;
never contact/transmit from the host except through the documented bench tool).
The module is the lane's build over the bench-validated combined AP+DP tree
(see BUILD-NOTES.md: stripped 246,688 B sha256 05dd5f6a...).  Push it to the
box as `/tmp/wifi-scan-tuning/hi5622_wifi.ko` and keep the pinned PD4 firmware
artifact local (policy).  No new /tmp deployment is needed beyond the module
and the firmware: the self-contained image already has iw/hostapd/regdb.

## 0. Preconditions (all must hold before rung R0)

- golden boot on the self-contained image (ae5e28bf) with the combined-module
  baseline already proven in this session; recovery door READY;
  UART capture running (read-only); page state `0x86807FEF/7/2/0xABCD`.
- cfg80211.ko loaded; the module loads with

      insmod /tmp/wifi-scan-tuning/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
        allow_frames=16 frame_gap_ms=100 h2d_send_budget=4 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0

  (`modprobe` does not pass params reliably on this image - use insmod [V\*]).
- wiphy registered; enumerate `/sys/class/ieee80211/` (phy index can advance);
- no AP running for R0-R5 (the AP is started only in R6).

## 1. Rungs and PASS gates

Enumerate the actual phy: `PHY=$(ls /sys/class/ieee80211/ | head -1)`.
Each rung: reload the module with the rung params (or rewrite the writable
params under /sys/module/hi5622_wifi/parameters/ where noted), add the
interface, run `iw dev wlan0 scan`, collect dmesg + `iw dev wlan0 scan dump`.

| rung | params (delta) | expectation | PASS gate |
| --- | --- | --- | --- |
| **R0 baseline control** | `scan_req_batch=0` (single request = legacy) | reproduces the measured stop: 6 results in <=2 s, then host timeout | `finish-abort[host-timeout]`, `chans=6/13`, exactly one `scan request batch 1/1` (control only - this rung is EXPECTED to fail 13/13) |
| **R1 tuned default** | (none; compiled defaults `scan_req_batch=6 scan_chan_time_ms=0 scan_batch_gap_ms=300`); artifact **249,580 B sha256 `1710bf11...`** (`/tmp/wifi-scan-tuning/hi5622_wifi.ko`; R3 parser + batch window) | 3 requests 6+6+1, chained; 13 channel results; scan closes success | **3 batch lines** `batch 1/3`, `2/3`, `3/3`; per-batch firmware completion (if any) is treated batch-local - no abort after batch 1; batch 2/3 has a 5.5 s window and a repost-once recovery if refused (look for the "reposting it once" line); real g1 id15 frames decode via the length-prefix layout; `scan BSS list check: entries>0 iw_visible>0`; `scan DONE (finish-ok[...], channel=13, chans=13/13 batches=3/3, timeout=0)` -> `cfg80211_scan_done(aborted=0)`; `iw dev wlan0 scan` rc=0 with **>=1 BSS line**. If `iw_visible=0`: report the DONE line; if `entries=0`: collect the new `scan BSS decode gap: len=.. first=..` hexdump lines (relaxed layout probe failed) |
| **R2 batching granularity** | `scan_req_batch=4` | 4 requests 4+4+4+1 | finish-ok, chans=13/13, 4 batch lines; total time within 20 s |
| **R3 longer batch** | `scan_req_batch=7` | 2 requests 7+6; if the firmware really caps at 6 the first request reports 6/7 and the batch times out | PASS if finish-ok 13/13; FAIL is informative: `request 1/2` then `finish-abort[host-timeout] chans=6/13` -> the per-request cap is 6 (H2) |
| **R4 gap sweep** | R1 + `scan_batch_gap_ms=0`, then `1000` | finds the scan-in-progress clear latency | PASS at 0 or 300 or 1000: finish-ok 13/13; record the smallest gap that gives 13/13. A gap of 0 that aborts means the firmware still held VAP+0x51C |
| **R5 dwell sweep** | R1 + `scan_dwell_ms=20`, `40`, `60` | whether per-channel cost tracks dwell | record per-channel delta; PASS for any value that reaches 13/13 within 20 s. A flat cost means additionally fixed per-request overhead |
| **R6 scan with AP up** | R1 params; start the AP (hostapd AP-ENABLED, ssid cudy-ap-test) on wlan0 first, then create a second managed VAP if the wiphy allows (or scan on wlan0 while AP is up) | checks the baseline "0 results with AP running" observation | PASS = 13/13 finish-ok + BSS >= 1; if 0 results, record the AP state and stop (do not retry blindly) |
| **R7 D2H catalogue discriminator** | R0 (single request) run; after the host timeout, dump the direct D2H catalogue (`rd_catalog` tail from the bench tooling) | separates firmware stop from host delivery gap | if the catalogue holds >6 g1 id17 frames for the run -> **H3** (host delivery gap) and R1's PASS is still valid; if exactly 6 -> **H1/H2** (firmware stop) |
| **R8 per-channel requests** | `scan_req_batch=1` | 13 requests of one channel each (worst case) | finish-ok 13/13 within 20 s = the chained path is robust; useful if a request beyond the first is silently refused at batch=6 |
| R9 (research, NOT this ladder) | g0 id10 scan-context config (handler CA 0x5FF64) | future H1 experiment: seed the context so one 13-entry request works without batching | separate attended lane only; see REPORT sec 8 + recipes/BAND-CONFIG-G0ID10.md. Do not fire in the default ladder |

## 2. Expected dmesg shapes (new lines are the evidence)

    cfg80211: scan is split into 3 g0 id0xC requests of <=6 channels (chained multi-request scan, scan_req_batch=6, gap 300 ms)
    cfg80211: scan request batch 1/3 posted by provider 'vapscan' (6 of 13 channels, ch 1..6, 1 ssids, dwell 60 ms, total 520 B); request window 3000 ms (auto), ...
    cfg80211: scan batch 1/3 complete - next request in 300 ms
    ...
    cfg80211: scan request batch 3/3 posted ... (1 of 13 channels, ch 13..13 ...)
    cfg80211: scan channel result slot=12 ch=13 (2472 MHz)
    cfg80211: BSS <bssid> '<ssid>' <freq> MHz ...
    cfg80211: scan DONE (finish-ok[channel-coverage], results=N bss=N complete=0 channel=13 chans=1/1 ...) - reporting cfg80211_scan_done(aborted=0)

(Per-request windows are `1500 + 250*channels`: 3,000 ms for a 6-channel
request, 1,750 ms for the 1-channel last request.  The DONE `chans=` counts the
last request's plan; the batch lines carry the per-request coverage.  Gate on
`chans=13/13` only where the rung uses one request (R0/R3 partial); for
chained rungs gate on `finish-ok` + all batch lines + `iw` BSS >= 1 + bounded
time.)

## 3. PASS/FAIL interpretation (decision table)

| observation | interpretation | action |
| --- | --- | --- |
| R1: 13 channel results, finish-ok, aborted=0, BSS>=1, <=20 s | tuning works; H1+H2 both covered by batching | record rung PASS; then R2/R4/R5 optional sweeps |
| R1: batches 1..3 posted but only 6 channels total, timeout | chained requests refused or first batch never completed | check R7; if catalogue shows 6 only -> firmware scan-context (H1), the batch mode cannot restart it; report and escalate to the g0 id1 band-config open item |
| R1: batch 1/3 only, then `finish-abort[host-timeout]` | first batch never reached coverage (e.g. only 5 of 6 channels) | lower batch to 4 (R2) or raise scan_chan_time_ms / scan_timeout_ms; record |
| R1: `scan post` error / cfg80211 op error | frame path failure (allow_frames/budget/queue) | stop; no scan claim; page check |
| any rung: exception mask != 0, page != 0x86807FEF/7/2/0xABCD/state 1/mask 0 | firmware fault | **STOP, power-cycle via the recovery door; do not retry** |

## 4. Stop rules

- Stop on the first firmware exception / non-golden page / missing heartbeat.
- No blind retries of a rung: change one parameter per fire, reload the module
  between rungs (writable params are 0644 but a reload guarantees a clean scan
  model and VAP state).
- Never write registers, never send AP/DP frames in this ladder; only the
  mode-1 load + scan (+ hostapd start in R6) is in scope.
- The box must be power-cycled (recovery door) after any firmware fault; the
  agent cannot recover alone (bench rule).

## 5. Evidence to keep

For each rung: full dmesg, `iw dev wlan0 scan dump`, the batch lines, the DONE
line, page/state readback, and (R7) the D2H catalogue tail.  Rung evidence
belongs in `build/wifi-bench-session/<id>/` on the host; the lane REPORT.md
only carries the host-side verification until the ladder runs.
