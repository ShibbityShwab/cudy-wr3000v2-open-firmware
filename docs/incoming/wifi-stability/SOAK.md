# SOAK - 60+ minute AP stability soak protocol

**Lane:** wifi-stability (task-136).  The soak measures the steady-state AP
stack: no firmware exception, continuous heartbeat, clean D2H, hostapd/AP
alive, bounded memory, no dmesg anomalies.  It is an ATTENDED bench procedure;
the harness and analyzer are host-tested (33 checks) but the 60 min run itself
needs the box.

## 1. Prerequisites

* Flash state: self-contained image (ae5e28bf) or later image carrying the
  driver; kernel 6.18.60; native iw/hostapd.
* Driver artifact: hardened module (`/tmp/wifi-stability-artifact/`) deployed
  by the operator, or the image's polished module - record size+sha256 and put
  it in the result header.  The module is never committed.
* PD4 firmware staged by hand: `/lib/firmware/hi_wifi/FIRMWARE.PD4.bin`
  sha256 `cfe24bfb...`, 928,920 B.  Policy: never packaged into the image.
* `/root/hi5622-bar0read` (bench tooling read-only helper) present+executable.
* Bench link PASS, recovery door armed (physical RESET+TFTP), UART capture
  read-only, human at the bench.
* One fresh golden boot (page 0x86807FEF/7/2/0xABCD, state 1, mask 0,
  staging 0).  Never start a soak on a boot that already had an exception or an
  AP restart.

## 2. Run (one command)

    cd opensource/docs/incoming/wifi-stability
    python3 tools/soak.py run --duration 3600 --interval 10 \
        --yes-really --attended --recovery-armed

The harness: preflights identity (kernel/driver/firmware/helper/hostapd),
brings the AP up with the documented single-boot flow (insmod with the tuned
params, boot ladder, C2, iface `__ap`, hostapd), gates once on the golden
readback, starts the read-only sampler, waits the duration, stops it, pulls the
TSV + dmesg, analyzes and writes a RESULT.md/result.json evidence directory
(default /tmp/wifi-stability-results/<utc>/).  `--skip-bringup` samples an
already-running AP (still requires the preflight and a golden readback).

Dry-run first (no device contact):

    python3 tools/soak.py dry-run --duration 3600 --interval 10

## 3. What the sampler records (read-only, every interval)

uptime; boot page 4 words; state; exception mask; staging; heartbeat count
(g4 id4); exception_info count (g4 id3); D2H frames/overruns/drop; hcc phase;
MemAvailable/MemFree; load; hostapd procs/RSS/CPU; iw wlan0 AP state+ssid;
dmesg delta; all appends to /tmp/wifi-soak/soak.tsv + dmesg.log.
The sampler does no device writes, no rmmod, no reboot, no network.

## 4. PASS criteria (tools/soak.py POLICY)

| check | PASS rule | verdict on fail |
| --- | --- | --- |
| boot-page | golden page 0x86807FEF/7/2/0xABCD + state 1 + mask 0 + staging 0 in EVERY sample | FAIL-EXCEPTION |
| no-reboot | uptime monotonic | FAIL-REBOOT |
| dmesg | no BUG/Oops/panic/WARNING/lane-closed/not-posted/REFUSED lines | FAIL-DMESG |
| excp-info | g4 id3 exception_info frames == 0 | FAIL-EXCEPTION |
| heartbeat | rate >= 0.2/s, no zero-heartbeat run > 15 s (baseline 1.01/s) | FAIL-HEARTBEAT |
| d2h | overruns 0, drop 0, frames monotonic and >= 0.5 x duration | FAIL-D2H |
| ap-liveness | hostapd >= 1 proc + iw type AP + ssid cudy-ap-test in every sample | FAIL-HOSTAPD |
| hcc-phase | ACTIVE in every sample | FAIL-HOSTAPD |
| memory | MemAvailable drift <= 8 MB + 128 KB/min; hostapd RSS growth <= 4 MB | FAIL-MEMORY |
| duration/coverage/gaps | span >= 98%, samples >= 95%, no gap > 3x interval | FAIL-SAMPLING |

A verdict is PASS only if all checks pass.  Result template:
[results/RESULT-TEMPLATE.md](../results/RESULT-TEMPLATE.md).

## 4b. Console noise

The hardened module defaults hcc_trace=0, so a steady-state soak has a quiet
dmesg (only one-shot/failure hcc-core lines).  For a debugging session pass
hcc_trace=1 on the insmod line; the analyzer's dmesg check only looks for
fatal/error patterns, so both settings are valid.  The final combined module
also carries the scan-tuning params (scan_req_batch=6, scan_batch_gap_ms=300,
scan_chan_time_ms) for a 3-batch 13-channel scan.

## 5. Stop and recovery rules (hard)

* mask != 0, staging != 0, page lost or exception_info > 0: **FIRMWARE
  EXCEPTION**.  Stop the run, capture dmesg + the sampler TSV + the readback,
  power-cycle (physical RESET/TFTP recovery door).  Never rmmod, never retry H2D,
  never reuse that boot.
* hostapd died / AP state lost: capture hostapd.log + iw info; do not restart
  in the same boot if the firmware AP state is unknown (controlled stop_ap is
  NOT yet bench-validated - ladder E2).
* heartbeat/D2H stall without exception: capture hcc_status/rd_status, power-
  cycle; classify offline before another run.
* Any run shorter than requested or with sampler gaps is INVALID, not FAIL.

## 6. After the run

1. Keep the evidence directory; attach the RESULT.md here as
   `results/soak-<date>.md` (text only - no vendor bytes, no firmware files).
2. If PASS, record the metrics baseline (heartbeat rate/gap, frames/s,
   MemAvailable drift, hostapd RSS) for the tuning lane.
3. If FAIL, open a finding with the exact check + captured lines; do not
   weaken a criterion to make a run pass.
