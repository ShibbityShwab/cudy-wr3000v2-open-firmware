# BENCH-PROCEDURE - HCC link/event observability + id-1 wake ladder

**Lane:** wifi-hcc-link (task-75). Attended, host-only bench procedure.
Read REPORT.md sec 5/7 and SPEC.md sec 2/5 before running.

This procedure answers exactly one question in the first session:

> **Does the running firmware consume H2D mailbox traffic at all, and are the
> four per-link `+0x4B0` contexts set?**

If the answer is "not consumed" (expected from the current evidence), the H2D
lane stops and the next work is firmware-internal (REPORT sec 7). No payload
frame, no RAM write, no "link create" experiment is part of this procedure.

---

## 0. Preconditions (project hard rules; do not relax)

* Human at the bench; **recovery door armed** (`tools/recovery-card.sh` READY);
  bench link PASS (`tools/bench-link.sh`); no agent-only fire.
* UART capture is **read-only** (`tools/uart-capture.sh`, absolute path under
  `build/uart-logs/`); never type to the tty.
* No image build in this lane; the module is built host-side with
  `tools/build.sh` (external-module compile only).
* Start from a **fresh power-cycle** if the chip was previously released in
  this session; the module must be loaded and rings armed **before** release.
* Never `rmmod` the vendor modules (none are loaded on our image anyway); never
  write CA `0x400392F0` (out[5]); never post mailbox id 5; never read the RC
  misc window `0x10161000`; never write device RAM or the per-link `+0x4B0`
  words.
* One idea per session. If anything below is not as expected, stop and
  power-cycle; do not continue the ladder "to see what happens".

## 1. Build and install (host)

    cd opensource/docs/incoming/wifi-hcc-link
    ./tools/build.sh                    # applies h2d 0001 + this lane 0002, builds hi5622_pciet_b3.ko
    # copy the module to the router per the usual lab flow (tools/rscp.sh), not part of this doc

The build applies the wifi-h2d-inject patch and this lane's patch to a copy of
`../wifi-transport-port/b3`; the resulting module has the existing `b3_*`, `h2d_*`
and the new `hcc_link_*` attributes. Build evidence: `hi5622_pciet_b3.ko` sha256
`dab5ca76...`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`.

## 2. Boot procedure (unchanged from B3/C1e)

1. `insmod hi5622_pciet_b3.ko poll_ms=0` (base poll thread OFF so the D2H
   contract is fully under the operator's control).
2. `echo 1 > prepare` -> expect 6/6 viewport readbacks.
3. `echo 1 > upload` -> 928,920 B; `echo 1 > verify` -> diffs=0.
4. `echo 1 > b3_p0_ringinit` -> 7/7 channels armed (SR0-2, DR0-3), depth-1 0x1f.
5. Start the integrated service loop **before** release: `out[1]` ack/drain/
   re-arm on every non-zero word + `0xcece` to CA 0x4000010C (B3 `b3_drain` /
   the C1e loop; cadence <100 us recommended).
6. `echo 1 > release` -> `0x5a5a` to CA 0x40000108.
7. Keep the service loop running. Optional: `echo 1 > b3_p1_watch` for the
   event capture (existing B3 probe).

## 3. Ladder

### L0 - baseline (read-only, mandatory)

    cat /sys/bus/pci/devices/<BDF>/hcc_link_status | tee /tmp/hcc-l0.txt

Record: `windows`, `cpu_released`, `out[0]/out[1]/out[2]`, `exc_mask`,
`state`, `g_ptr` and the `g` fields, the registry record, the four
`per-link +0x4B0` words, ETE fill indices, `VERDICT` line (should read
`h2d_consumer=UNKNOWN` before L1).

Sanity expectations on a cold-booted chip (from prior lanes):
`windows=1 cpu_released=1`, `g_ptr ~0x0010C0F4`, registry populated,
`exc_mask` 0x08 or 0x0A, `state` 1, four `+0x4B0` words 0.
If `windows=0` -> prepare was not run; fix that first (no writes).

### L1 - id-1 wake (the only device write in this procedure)

    echo 1 > /sys/bus/pci/devices/<BDF>/hcc_link_wake
    dmesg | tail -3

* rc 0 (consumed): kernel prints "id-1 wake CONSUMED ..." with
  `out[0] before/after`, `out[2] before/after`, `out[5]`, `out[1]`. Proceed to L2.
* `-ETIMEDOUT`: kernel prints "NOT consumed within 100 ms ...". **STOP the H2D
  lane**: report the line + L0 snapshot; go to REPORT sec 7 lane 1 (firmware
  internal). Do not post any other frame. Do not clear `out[0]`.
* `-EPERM` (not released), `-EIO` (windows not programmed) or `-EBUSY`
  (out[0] foreign bitmap / >3 attempts): fix the precondition or stop; these are
  operator/procedure errors, not device results.

### L2 - post-wake classification (read-only)

    cat /sys/bus/pci/devices/<BDF>/hcc_link_status | tee /tmp/hcc-l2.txt

Read the `VERDICT` line and apply SPEC.md sec 5 decision table:
* consumer DEAD -> firmware-internal lane (stop here);
* consumer ALIVE + `link_ctx=0/4` -> firmware-internal stage still missing; do
  not fabricate RAM or payload frames;
* consumer ALIVE + `link_ctx=4/4` -> link/event contexts present; the next lane
  is the driver replica (post-ready H2D config events).

### L3 - optional minimal dispatch liveness (only if L1 consumed and `out[0]==0)

Only with the h2d injector present and armed in the same module:

    echo 1 > .../h2d_arm                # programs SR0 (host-owned fields)
    echo 'group=0 id=0 dst=0' > .../h2d_inject

Group 0 id 0 is the firmware's payload-free stub handler (`0x41695`) - the
safest possible HCC frame. It proves the firmware's own HCC dispatch can be
reached; it is NOT a link bring-up frame. Result: `h2d_stats` / dmesg.
Never id 5, never out[5], never payloads in this session.

## 4. Stop rules (hard)

1. Wake probes >=3 total or any `-EBUSY` on `out[0]` != 0 -> stop, do not clear.
2. Any new `out[1]` bit, any staged-abort frame change beyond the recorded
   `mask`, any kernel oops/hang -> stop, power-cycle, report; no in-session retry.
3. Consumer DEAD -> the H2D lane is closed for this boot; no frame/payload
   experiment is justified by the evidence.
4. Never write device RAM or the per-link `+0x4B0` words, even if the status
   shows them zero; the abort-rca lane proved the handlers dereference
   sub-fields on first use (external-abort risk).
5. Do not post mailbox id 5, do not write out[5], do not touch `0x10161000`.

## 5. Post-run artifacts

* `/tmp/hcc-l0.txt`, `/tmp/hcc-l2.txt` (snapshots), the dmesg lines for
  `hcc-link`, `out[0]/out[1]/out[2]` and ETE registers as captured by the
  existing B3 probes.
* One ledger entry with the verdict and the chosen next lane.
* End with the standard teardown (`tools/bench-teardown.sh --apply`).

## 6. What success/failure means for the project

* **Wake not consumed** (most likely given the cold-boot evidence): proves that
  no host-message lane can fix the S10 abort; the next work is the
  firmware-internal link/event bring-up (boot-region dispatch table at CPU
  `0x474B8`, event-thread loop entries) - REPORT sec 7 lane 1.
* **Wake consumed, `link_ctx=0/4`**: the firmware H2D layer is alive but the
  per-link contexts were never bound; same next lane, with one extra clue (the
  missing stage is downstream of the H2D dispatch).
* **Wake consumed, `link_ctx=4/4`**: the link/event layer is actually up and the
  earlier abort analysis needs revision; proceed to the driver-replica lane.

