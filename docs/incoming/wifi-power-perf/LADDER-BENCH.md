# Attended bench ladders - power/perf safe subset (wifi-power-perf, task-160)

**Nothing in this file has been fired.**  This lane is host-only: no device
register was written, no module was loaded, no image was built beyond a local
cross-compile of the patched module.  The Lead fires; every rung is attended
and requires the standard bench preconditions.

## Preconditions for every rung

1. `tools/session-brief.sh` clean; `tools/bench-link.sh` PASS (carrier up,
   both IPs); `tools/recovery-card.sh` READY and a human at the box.
2. UART capture running read-only (`tools/uart-capture.sh` to an absolute
   path under `build/uart-logs/`).
3. Image B (or the next bake) contains the **final association module + this
   lane's patch** (60 params).  Check
   `cat /sys/module/hi5622_wifi/parameters/ | grep -E 'pm_enable|hcc_poll_idle_ms|data_tx_poll_ms'`.
4. Record the baseline counters before the rung:
   `cat $DEV/hcc_pm_status; cat $DEV/hcc_status; cat $DEV/dp_tx_status`
   where `DEV` is the endpoint device directory that exposes the driver's
   attribute group (the same path the earlier lanes used for
   `hcc_service`/`hcc_sr_announce`).
5. Stop rule for all rungs: any page/gate change, firmware exception,
   `h2d_lane_closed`, data-TX lane stall, or console silence > 30 s ->
   stop, capture `hcc_events`, power-cycle is the recovery (never rmmod
   after release).

## P0 - inert patch sanity (no semantics changed)

- Load the patched module with **all defaults**; bring the AP up with the
  proven WPA2 sequence (image B ladder).
- Expect: `hcc_pm_status` shows `enabled=0` and all PM counters 0; the
  service thread log shows `active cadence 5 ms, idle cadence 20 ms`;
  `dp_tx_status` behaves as before.
- PASS: AP-ENABLED, beacon on air, heartbeat unchanged, no new dmesg lines
  beyond one loop-start line and the ASPM probe line.

## P1 - idle wakeup cost (safe, reversible)

- Idle AP, no clients.  Sample for 120 s:
  `for i in 1..12: grep '^cpu ' /proc/stat; sleep 10` and count
  `hi5622_wifi-svc` context switches in `/proc/<pid>/status`.
- Run once with `hcc_poll_idle_ms=5` (back-off disabled) and once with the
  default 20, in the same boot if possible (module reload is NOT safe after
  release - reload needs a power-cycle; prefer two boots).
- PASS: idle poll rate drops ~4x (200 -> ~50/s/endpoint); no frame loss,
  no h2d timeout, page golden/mask 0 before and after.
- STOP: anything above.

## P2 - idle back-off vs RX latency (safe)

- With the back-off on (default 20 ms), have the external observer (or a
  second client once association works) send a probe/ARP burst after >200 ms
  of quiet.
- PASS: first-reply latency <= ~25 ms and no dropped EAPOL/ARP; fast cadence
  is restored by the first decoded frame (visible in dmesg only with
  `hcc_trace=1`, or in `hcc_status` event counters).
- This is the only rung that touches interactive latency; keep
  `hcc_poll_idle_ms<=20` unless P2 passes with a documented margin.

## P3 - TX completion rearm / in-flight sweep (bench-only, data plane)

- Requires the data-TX lane enabled (`data_tx=1`, peer-aware default).
- Ladder: `data_tx_poll_ms` = 5 (shipped) -> 2 -> 1, with
  `dp_tx_probe` (synthetic msg1-shaped frame through the same builder) or,
  once association exists, a UDP flood through the AP.
- Read `dp_tx_status`/data_tx counters: posts, completed, drops(full),
  timeouts.  PASS: completed==posts, drops(full)==0, no timeouts, and the
  measured frame rate approaches the model's host window (16*1000/poll).
- Do **not** change the in-flight pool in this rung: HDTX_POOL_DEPTH=16 is a
  compiled constant and the DMA slabs are sized for it.  A pool-depth change
  is a separate patch + ladder if the PHY turns out faster than 2.4 GHz MCS7
  (see REPORT.md 5.2).
- STOP: any completion timeout closes the lane for the module load by design;
  record and power-cycle.

## P4 - PCIe ASPM parity assertion (safe)

- At probe expect exactly one line:
  `PCIe ASPM off at probe (LNKCTL=0x....) - vendor parity`.
- `cat $DEV/hcc_pm_status` -> `aspm: ... enabled=0 vendor_parity=ok`.
- If ASPM is ever found enabled: `echo 1 > $DEV/aspm_disable`, expect
  readback bits 0-1 = 0, then record and continue.
- PASS: parity holds; no link retrain/link-down line after the check.

## P5 - PM sleep one-shot (EXTREME RISK - last rung, physical recovery armed)

Not part of the safe subset.  It exercises the vendor H2D group-0 id1 path
under the local gate.  The vendor follows this message with a BAL close and a
GPIO wake handshake; **our stack has neither**, so a device that accepts the
request may stop servicing the host until a physical power-cycle.

- Load with `pm_enable=1` **only for this boot**; keep everything else at the
  proven AP configuration.  Verify `hcc_pm_status`: `enabled=1`.
- Arm UART to disk, note the page/gate state.
- Fire ONCE: `echo 1 > $DEV/hcc_pm_sleep`.  Expect one dmesg warning and
  `pending=1`, `deadline_ms` in the future.
- Observe for <= 2 s: `hcc_pm_status` counters `disallow` or `wake_ok`
  (D2H ids 4/5) and the ack timeout counter.  Do NOT repeat.
- PASS (evidence-only): the request was posted and the device answered
  disallow (id4) with the host service alive.  **Any hang -> physical
  power-cycle; do not attempt a module reload.**
- After the rung, reboot the box (not just module reload) so the next boot
  starts from a clean chip state.

## P6 - client power experiments (post-association only)

- Once the association chain is green: sweep hostapd `dtim_period`
  2 -> 3 -> 5 on the AP and measure client wakeups (observer/power meter).
  Beacon interval stays 100 TU unless a client issue requires otherwise.
- PASS: clients association-stable, multicast latency acceptable; AP power
  unchanged (expected - the AP chip is always-on).

## P7 - scan batch/dwell sweep (after a scan-capable VAP exists)

- `scan_req_batch` 6 (default) vs 7/8 with the H1 band-config rung;
  `scan_dwell_ms` default 60 vs 40/80; `scan_batch_gap_ms` 300.
- PASS: every requested channel yields a result within the batched window,
  or the 6-channel DMAC cap is re-confirmed (record it; do not chase 13).
