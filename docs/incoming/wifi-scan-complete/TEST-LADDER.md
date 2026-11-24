# TEST-LADDER - 13/13 channel scan completion + BSS entries (wifi-scan-complete)

**Lane:** wifi-scan-complete (shared task **task-123**). **Host-only delivery;
attended bench procedure only - this lane did not fire.**
**Date:** 2026-11-26. Build the module first: `./tools/build.sh` (record its
size/sha256 from BUILD-NOTES.md; module hashes are build-path dependent).

This ladder starts from the hardware-validated 2026-11-26 mode-1 VAP-create
milestone (per the wifi-scan-ap / wifi-vap-start ladders; the scan handler
hardcodes firmware VAP 0, so the VAP object must exist before the request) and
exercises the two new behaviours:

* the provider posts the proven 520-byte **g0 id0xC** scan request with the
  requested channel list and then waits long enough for **13 channels** and
  closes successfully
  when all requested channels have been reported (**channel-coverage** rule;
  g1 id16 / g2 id2 still win when present);
* each g1 id15 802.11 body is decoded into a complete cfg80211 BSS entry with
  SSID / DS / HT IEs, channel and capability fields, so `iw dev wlan0 scan`
  lists real BSS entries (not just channel slots), with one dmesg evidence
  line per BSS.

Every step below is written so the expected **failure** is as informative as
the expected success: the driver never fakes a scan or a BSS.

## 0. Preconditions (all required)

1. Attended session; recovery door READY (`tools/recovery-card.sh`), bench link
   PASS (`tools/bench-link.sh`); no autonomous/remote fire.
2. Our cfg80211 image boots on pair B, golden page 0x86807FEF/7/2/0xABCD,
   state 1 mask 0 (BENCH-FACTS); `cfg80211.ko` + the lane's
   `hi5622_wifi.ko` present on the box (paths per
   `../wifi-upstream-target/CFG80211-BENCH-IMAGE.md`).
3. PD4-patched research firmware + local Wi-Fi block staged exactly as in
   `../wifi-h2d-send/BENCH-PROCEDURE.md` (local bench inputs; never committed).
4. `iw` available (packaged iw from the img-integ lane is enough).
5. UART capture started read-only (`tools/uart-capture.sh start`), absolute
   log path.
6. **One session = one idea.** After the 0x5A5A release no `rmmod` (project
   stop rule); a failure ends the session and the box is power-cycled.

## 1. Load + C2 ladder (inherited, abridged)

Run the H2D/context-create ladder first (`../wifi-h2d-send/BENCH-PROCEDURE.md`);
`hi5622_frame_post()` only accepts frames once the core phase is ACTIVE:

    insmod /tmp/cfg80211.ko
    insmod /tmp/hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
            wifi_block_path=<local block> h2d_send_budget=8 \
            allow_frames=12 frame_gap_ms=100 \
            vap_start_first=1 vap_start_mode=1 vap_start_module=0
    D=/sys/devices/platform/0000:00:00.0      # check ls; PCI domain 0 = 2.4 GHz
    echo 1 > $D/prepare ; echo 1 > $D/upload ; echo 1 > $D/verify
    echo 1 > $D/seed ; echo 1 > $D/hcc_arm ; echo 1 > $D/hcc_loop
    echo 1 > $D/release
    printf 1 > $D/h2d_ctx_create ; printf 1 > $D/h2d_txq_sync72
    cat $D/h2d_status       # expect: phase=ACTIVE frames_sent=2 ok=2

New module parameters (defaults are safe; the ladder does not need them):

| parameter | default | ladder use |
| --- | ---: | --- |
| `scan_timeout_ms` | 0 = auto | auto = 14,700 ms for 13 channels; Variant B overrides |
| `scan_dwell_ms` | 0 = vendor 60 | Variant C sweeps 20/40/60 |
| `scan_trust_rssi` | 0 | leave 0; Variant F is the RSSI correlation run |

Then assert the wiphy and create the managed VAP:

    dmesg | grep -E 'wiphy registered|scan layer'
    ls /sys/class/ieee80211/phy0
    iw phy phy0 info                      # 2.4 GHz channels 1..13
    iw phy phy0 interface add wlan0 type managed
    ip link set wlan0 up
    cat $D/cfg80211_status
    # expect: provider=vapscan-0e4ad45 caps=... (0x19f: scan_req,scan_parse,
    #         user_add,user_add_notify,user_del,vap_add,vap_start)
    #         frame_post allow_frames=12 ; scan: phase=idle
    #         scan layer: ... timeout_ms=14700 (auto) dwell_ms=60

**STOP** if cfg80211_status is absent, provider caps lack SCAN_PARSE, or
allow_frames is not the planned budget. Record the loaded .ko sha256
(`sha256sum /tmp/hi5622_wifi.ko`).

## 2. Main rung: full 13-channel scan (Variant A)

    iw dev wlan0 scan > /tmp/scan-A.out
    echo "returncode=$?"          # expect 0 on a finished (non-aborted) scan
    dmesg | tail -80

Expected dmesg sequence (the numbers are the pass criteria):

1. `cfg80211: scan request posted by provider 'vapscan-0e4ad45' (1 ssids,
   13 channels, 0 IE bytes, dwell 60 ms, total 520 B); waiting for g1 15/16/17
   or g2 2, timeout 14700 ms (auto), host completion on all-channel coverage`
2. 13 x `cfg80211: scan channel result slot=N ch=N (... MHz)` (g1 id17; order
   may vary, last around 10-11 s)
3. one `cfg80211: BSS <bssid> '<ssid>' <freq> MHz (DS ch N) beacon
   cap=0x... ess=1 priv=... bcn=... TU ie=.../... ht_cap=1 ht_op=1 rsn=1
   wmm=1 tsf=... signal=0 mBm` per decoded BSS (**the IE decode evidence**)
4. exactly one `cfg80211: scan DONE (finish-ok[channel-coverage], results=...
   bss=... complete=... channel=13 chans=13/13 ... inform_fail=0 timeout=0) -
   cfg80211_scan_done(aborted=0)`
5. `iw` output lists the BSS entries with SSID / DS channel / HT capabilities
   (the count matches the number of BSS dmesg lines)

Post-run read-only assertions:

    cat $D/cfg80211_status
    # expect: scan: ... channel=13 chans=13/13 ...
    #         scan finish: last=channel-coverage ok=1 abort=0 coverage_ok=1
    iwinfo wlan0 scan | head        # optional userspace cross-check
    cat $D/rd_catalog               # read-only: g1 id0x11 x13, g1 id0x0f x N

**Pass criteria:** `cfg80211_scan_done(aborted=0)`, `chans=13/13`,
`channel=13`, at least one BSS line and `iw` lists the same BSSes,
`inform_fail=0`, golden page/state/mask unchanged, no firmware exception on
UART. A `finish-ok[complete-event]` (instead of channel-coverage) is also a
PASS - it means the firmware completion event arrived.

## 3. Variant B - manual scan window

Same boot; reload is not possible after 0x5A5A release (stop rule) - run this
variant in its own session (fresh boot) if needed:

    insmod ... scan_timeout_ms=25000
    iw dev wlan0 scan ; echo "returncode=$?"

Expected: same chain as Variant A with `timeout 25000 ms (manual)`. Use it if
the measured per-channel cost exceeds the auto budget (e.g. heavy environment):
the coverage rule should finish before the window anyway; a window hit is
reported `finish-abort[host-timeout]` with `chans=<13` - a recorded negative
result, never a pass.

## 4. Variant C - dwell sweep (why ~830 ms/channel?)

Separate sessions, each fresh boot + reload with `scan_dwell_ms=20` then
`=40`, then default 60. Measure the time between the first and the 13th
`scan channel result` dmesg line (UART timestamp or `dmesg -T`):

* if the 13-channel duration scales with `scan_dwell_ms` -> the field is in ms
  and the residual is probe/scan-engine overhead;
* if it does not scale -> the unit is not ms ([U]) and only request-side
  channel/dwell tuning can shorten it.

Record the three durations; do not change the default (60) on the strength of
one run.

## 5. Variant D - negative control (short bound)

Fresh boot; load with `scan_timeout_ms=3000`; `iw dev wlan0 scan`.

Expected: results flow for the channels scanned within the window, then exactly
one `scan DONE (finish-abort[host-timeout], ..., chans=<13, timeout=1) -
cfg80211_scan_done(aborted=1)`. This proves the finish-reason split (host
timeout vs firmware abort) and that the coverage rule does not claim success
early.

## 6. Variant E - decode-gap control (only if a BSS is NOT listed)

If `channel=13 chans=13/13` but fewer BSS lines than `/tmp/scan-A.out`
entries (or `[U]` fields):

* check `inform_fail` and `parse` counters in the DONE line/status;
* a `parse` increment means a g1 id15 payload did not pass the IE/layout
  validator (decoder gap: capture the raw payload, extend the decoder - never
  widen it by guessing offsets);
* an `inform_fail` increment means the decoded BSS is on a frequency this
  wiphy does not list (inform_fail= N > 0);
* zero g1 id15 events with 13 channel results = the scan truly heard no BSS on
  channels 7..13 (environment/range), not a decode failure. Record and stop.

## 7. Variant F - trailer RSSI correlation (optional, [I])

Fresh boot; load with `scan_trust_rssi=1`; scan the same environment twice
(default 0 then 1). Compare the reported `signal=` mBm against a known AP
whose real RSSI is available on a second radio. Only if the correlation holds
(e.g. trailer+5 == dBm*100) is the heuristic promotable; otherwise it stays an
opt-in diagnostic. Never change the default 0 on one reading.

## 8. Optional userspace rungs (hostapd / wpa_supplicant) - expected refusals

These exercise the same provider but are NOT part of this lane's pass criteria:

    cp ../wifi-scan-ap/configs/hostapd.conf /tmp/ ; hostapd -d /tmp/hostapd.conf
    # expect: start_ap -> -EOPNOTSUPP (WAL AP-config 0x127 body is gap G1 [U])
    cp ../wifi-scan-ap/configs/wpa_supplicant.conf /tmp/
    wpa_supplicant -D nl80211 -i wlan0 -c /tmp/wpa_supplicant.conf -d
    # expect: scan works; connect/assoc is not implemented (M3) -> clean failure

Assert hostapd/wpa_supplicant return codes are the documented refusals, not a
driver crash, and that the scan rung still behaves as in Variant A.

## 8b. Relationship to the wifi-bench-tooling D2 ladder (task-124)

The committed bench tooling D2 ladder
(`../wifi-bench-tooling`, bringup-d2-scan.json, commit `19ee94d`) already
asserts `iw dev wlan0 scan` rc=0, the `cfg80211: scan channel result`
signature, results>0 and the box-health gates. This ladder keeps all of those
and adds the completion criteria: exactly one `scan DONE` with
`finish-ok`/`chans=13/13`/aborted=0 and at least one `cfg80211: BSS` line.
Suggestion for the tooling lane: add those two dmesg signatures as
`expect_present` rows (and the Variant D `finish-abort[host-timeout]` as the
documented blocked pattern for short-bound runs).

## 9. Stop rules (apply to every rung)

Stop and power-cycle on any of:

* `frame post 'scan_req': REFUSED ...` or `announce NOT consumed` / H2D lane
  closed (gate refusal is a stop, not a retry);
* core phase leaves ACTIVE, firmware exception frame on UART, golden page or
  mask changes, heartbeat loss;
* **no D2H scan event of any kind within ~6 s after the scan request**
  (the firmware may have refused the scan - record and stop; the work item will
  still close the scan as `host-timeout`);
* the **first parse error** in a g1 id15 event (decoder gap - do not widen the
  decoder by guessing offsets);
* any `cfg80211: ... REFUSED`/oops/WARN line, or an `iw` command hanging
  beyond the configured timeout (the auto window is 14.7 s; `iw scan` may wait
  for the driver's scan_done);
* duplicate/multiple `scan DONE` lines for one request (lifetime bug).

Never re-send a failed frame, never retry the same session after a REFUSED
post, never write the mailbox id-5 path, never use the RC misc window. After a
session: `tools/bench-teardown.sh --apply`, capture the UART log and the full
dmesg, append the ledger entry.

## 10. Expected observables summary

| observable | Variant A pass |
| --- | --- |
| g1 id17 count | 13 (124 B each) |
| g1 id15 count | >= 1 (509 B-class, one per discovered BSS) |
| scan DONE | exactly one, `finish-ok[...]`, aborted=0 |
| channels | chans=13/13, channel_events=13 |
| BSS lines | >= 1, with SSID / DS channel / HT presence decoded |
| iw scan | lists the same BSS entries |
| errors | inform_fail=0, parse=0, invalid=0 |
| box | golden page/state/mask unchanged, no exception, recovery READY |
