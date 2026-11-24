# wifi-stability - long-run soak, error paths and driver hardening

**Lane:** wifi-stability (shared task task-136), Cudy WR3000 v2.0 clean-room
Hi5622 Wi-Fi RE project.  **Host-only lane:** every deliverable was produced and
verified on the host; the attended bench ladders are designed and audited here,
and the parts that could not be tested without the device are marked as such.
Nothing was flashed, no image was built, no vendor bytes are committed.

## Status in one paragraph

The lane delivered (1) a device-side **read-only soak sampler** plus a host
**orchestrator/analyzer** with objective PASS criteria and 33 host selftests,
(2) the **error-path matrix and bench ladders** (module cycle without release,
VAP delete / stop_ap / restart, failed-start retry, repeated C2 across reboots,
exception recovery), (3) a **hardening patch** on top of the polished image
module that fixes two hardware-confirmed state-machine bugs and adds the
requested fail-closed guards, and (4) the bench evidence that pinned both root
causes (boots A-C of the scan-AP diagnostic).  The hardened module is
**host-built and NOT fired** - the attended validation ladders are E2 (controlled
stop_ap + restart) and E3 (failed-start retry).

## Bench findings this lane had handed to it (hardware-confirmed by the Lead)

| boot | sequence | result |
| --- | --- | --- |
| A | fresh boot -> C2 -> managed iface scan (posts the mode-1 VAP base) -> iface del + re-add `__ap` -> hostapd | **AP start FAILED and the firmware took an EXCEPTION**: `ap_ssid` posted, `ap_cfg` announce never consumed (out[0]=8 after 50 polls), H2D lane closed, boot page lost (0x00119758...), hostapd "Failed to set beacon parameters". The v7 module-global base-post latch stayed SET across the user_del/user_add cycle, so start_ap skipped the VAP base and posted AP frames against a dead VAP object. |
| B | fresh boot -> C2 -> iface `__ap` -> hostapd (AP-ENABLED) -> kill -> hostapd again (NO scan) | second start fails: cfg80211 returns -EALREADY because its `ap.beacon_interval` state is only cleared by a successful `stop_ap`, and the driver's `stop_ap` returned -EOPNOTSUPP (no firmware AP-stop message). |
| C, D | AP -> scan -> restart; managed scan -> del -> `__ap` | same root causes; C/D were still running when this lane was written (Lead to append). |

Both are state-machine bugs in the driver, not cfg80211 misbehaviour: cfg80211
6.18 has **no scan-state check** in `nl80211_start_ap` (verified in the project
kernel tree), but it does gate NEW_BEACON on `wdev->links[link].ap.beacon_interval`
(-EALREADY, cleared only on a successful `rdev_stop_ap`).

## Deliverables

| path | what it is |
| --- | --- |
| [REPORT.md](REPORT.md) | full lane report: findings, fixes, soak policy, evidence, limits |
| [HARDENING.md](HARDENING.md) | driver state-machine gaps + the hardening patch map + build identities |
| [SOAK.md](SOAK.md) | 60+ min AP soak protocol, PASS criteria, stop/recovery rules |
| [ERROR-PATHS.md](ERROR-PATHS.md) | error-path matrix, ladders E1-E5, recovery policy |
| [patches/0001-stability-hardening.patch](patches/) | the source delta (applies on top of the polished image module) |
| [patches/files/hi5622_stability.h](patches/files/hi5622_stability.h) | pure, host-tested AP-start policy gate |
| [tools/soak.py](tools/soak.py) | host orchestrator + analyzer (dry-run/analyze/run; attended-only run) |
| [tools/soak_sampler.sh](tools/soak_sampler.sh) | read-only on-device sampler (busybox shell) |
| [tools/soak_selftest.py](tools/soak_selftest.py) | 33 host tests: analyzer verdicts, TSV format, script audit, CLI |
| [tools/error_paths.py](tools/error_paths.py) | error-path ladder definitions + dry-run/audit/plan generator |
| [tools/build-hardened-module.sh](tools/build-hardened-module.sh) | reproduces base 44c6cc12 + polished d145a205, then builds the hardened module |
| [tools/run-hardening-check.sh](tools/run-hardening-check.sh) | host gate: policy test + patch anchors + apply check (+ --with-build) |
| [ladders/](ladders/) | machine-readable bench ladders (soak, error paths E1-E5, scan-AP diag A-D) |
| [results/RESULT-TEMPLATE.md](results/RESULT-TEMPLATE.md) | soak result template with the PASS table |
| [evidence/](evidence/) | committed host evidence: hardening check, build chain, soak selftest |

## Reproduce the host checks (no device)

    cd opensource/docs/incoming/wifi-stability
    ./tools/run-hardening-check.sh          # 13 policy checks + patch checks, PASS
    python3 tools/soak_selftest.py          # soak_selftest: 33 passed, 0 failed
    python3 tools/error_paths.py --list     # ladder index
    ./tools/build-hardened-module.sh        # heavy: 6.18.60 tree; base+polished
                                            # identities asserted, hardened build

## The hardening (why it fixes the two bench bugs)

1. **Per-context VAP-base/AP latches** replace the v7 module-global
   `static bool hi5622_vap_base_posted`; the latch is cleared on a committed
   `user_del` (g0 id9) and lives in `struct hi5622_pci`, so an iface
   delete/re-add - or a bind/unbind in one module load - re-posts the mode-1 VAP
   base before any AP frame (fixes boot A's crash class).
2. **Controlled stop_ap + restart**: when no AP-stop builder exists, a
   `stop_ap` on a started AP posts the VALIDATED `user_del` (g0 id9) for the AP
   user, clears the host/cfg80211 state and returns 0 (so cfg80211 clears
   `ap.beacon_interval` and a restart is possible); a subsequent `start_ap`
   re-creates user_add/notify (g0 id7/id8) + the mode-1 VAP base.  Any message
   failure is returned to cfg80211 - never a faked success (fixes boot B's
   blocker).  This restart sequence has NOT been bench-fired yet (ladder E2).
3. **Stale-VAP readback guard**: before posting AP frames against a VAP base
   that was already posted by an earlier operation, the driver does a READ-ONLY
   readback of the bench-proven module-table slot-0 + ctx+0x190 recipe and
   refuses (-EIO, no frame) if the firmware VAP pointer is zero - a host bug can
   no longer crash the chip.
4. **Stale module reload refusal**: probe latches handshake page +8==2; a
   reload after a live boot refuses upload/seed/release/H2D frames (power-cycle
   is the reset; never rmmod after release).
5. **Fail-closed AP/scan state rules**: one AP start per load, no re-post after
   a dirty aborted sequence, no AP frames against a non-AP iftype base, scan
   after an AP start refused (-EBUSY, measured 0 results), scan against a dead
   VAP refused.
6. **Remove ordering fix** (cfg80211 teardown before HCC rings are freed) and
   **mgmt_diag** already owned by the image-polish lane (this patch builds on it
   and does not duplicate it).

## Honest limits

* The hardened module is host-built/compile-verified, **not fired**.  Ladders
  E2/E3 (and C/D of the scan-AP diagnostic) need an attended session.
* The soak harness was validated against synthetic sampler transcripts
  (33 host tests) - a 60+ min real run needs the bench.
* The controlled stop_ap reuses messages validated individually on the bench
  (g0 id7/id8/id9) but the full stop->restart ORDER is unvalidated; E2 is the
  ladder that decides whether it may replace the power-cycle rule.
* RX/TX data-plane completion and on-air beaconing remain outside this lane.
