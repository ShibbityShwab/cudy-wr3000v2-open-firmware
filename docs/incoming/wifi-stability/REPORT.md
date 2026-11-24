# wifi-stability REPORT - soak, error paths and hardening

**Lane:** wifi-stability (shared task task-136).  Host-only deliverables; the
bench evidence cited is the Lead's (boots A-C) and the project ledger.  Date:
2026-11-27.  Base: self-contained image ae5e28bf (kernel 13e81214, rootfs
ff9f3e1c) + combined AP+DP driver 242,572 B sha256 44c6cc12, polished image
module 242,920 B sha256 d145a205.

## 1. Scope and method

Three work items: (1) a long-run AP soak harness with objective PASS criteria,
(2) the error-path matrix + bench ladders, (3) driver hardening for the
state-machine gaps found.  The lane is host-only: code was read against the
6.18.60 kernel tree and the project's driver sources, the soak analyzer was
tested on synthetic transcripts, the module was compile-verified (and the whole
patch stack reproduces the fired artifacts byte-for-byte), and everything that
needs the device is written as an attended ladder (fired by the Lead).

## 2. Bench-confirmed root causes

### 2.1 Boot A: stale VAP-base latch -> firmware exception

Sequence: fresh golden boot -> C2 (ctx+txq) -> `iw phy ... interface add wlan0
type managed` -> up -> full scan -> `iw dev wlan0 del` -> `iw phy ...
interface add wlan0 type __ap` -> up -> hostapd.

Observed (Lead): scan posted the VAP base (`vap_add` g0 id1 48 B,
`vap_start` 40 B, then `scan_req`); del/re-add posted `user_del`, `user_add`,
`user_add_notify`; start_ap reached the driver; `ap_ssid` posted (64 B);
`ap_cfg` announce NOT consumed (out[0]=0x8 after 50 polls) -> H2D lane closed
-> sequence aborted -> firmware EXCEPTION (boot page lost:
0x00119758/0x0000009c/0x00000000/0x00000002, golden=0, gate 0x0000), hostapd
"Failed to set beacon parameters; Could not connect to kernel driver".

Root cause (driver): the v7 ap-start patch introduced `static bool
hi5622_vap_base_posted;` - a MODULE-GLOBAL once-per-load latch set by the scan
path (vap_start_first=1).  It was never cleared when a user_del removed the
firmware VAP.  After the iface delete/re-add, start_ap skipped the base and
posted AP frames against a dead VAP object; the firmware's ap_cfg worker could
not consume the announce and the chip exceptioned.  This is exactly the class
of crash the v7 base-post was meant to prevent (the v6 fire crashed the same
way when the base was missing).

### 2.2 Boot B: cfg80211 -EALREADY / missing stop_ap

Sequence: fresh boot -> C2 -> iface `__ap` -> hostapd (AP-ENABLED) -> kill
hostapd -> hostapd again (no scan).

Observed (Lead): first hostapd rc=0 (AP-ENABLED), second fails; kernel log has
no driver `start_ap` message for the second attempt.

Root cause: cfg80211 `nl80211_start_ap` refuses with -EALREADY while
`wdev->links[link_id].ap.beacon_interval` is non-zero; cfg80211 clears that
only after `rdev_stop_ap` returns 0 (`net/wireless/ap.c` in the 6.18.60 tree).
The driver's `.stop_ap` returns -EOPNOTSUPP (the firmware AP-stop message is
still [U]), so every restart in the same boot fails before the driver.
The "Could not connect to kernel driver" line is hostapd's teardown flush
(`.del_station` is not implemented -> -EOPNOTSUPP) and the mgmt_tx stack dump
is hostapd's broadcast-deauth via nl80211_tx_mgmt - both are teardown noise, not
causes.  cfg80211 has NO scan-state check in start_ap (verified), so the scan
was exonerated for boot B.

### 2.3 Scan-during-AP

Baseline (lead): scan while AP is up returns 0 results (chans=0/13).  The
driver now refuses such a scan with -EBUSY and a clear message instead of
posting a scan the firmware will not complete (fail-closed; the firmware scan
engine's AP-state behaviour is not reverse-engineered).

## 3. Hardening patch

Applied on top of the polished image source delta (image-polish lane owns
`mgmt_diag`; this lane builds on it).  Full map in [HARDENING.md](HARDENING.md).

| # | fix | bench bug addressed |
| --- | --- | --- |
| H1 | per-ctx `vap_base_posted` / `vap_base_iftype` + clear on committed user_del + clear on probe | A (stale latch crash) |
| H2 | controlled `stop_ap` via the validated g0 id9 user_del (+ clear cfg80211 state by returning 0), restart re-creates user_add/notify + base | B (restart blocker) |
| H3 | read-only stale-VAP guard (`module_ctx+0x190`) before AP frames / scan against a pre-posted base | A (defence in depth) |
| H4 | AP-start state machines: already-started / dirty-sequence / wrong-iftype base gates | A/B failure isolation |
| H5 | stale module reload refusal (probe page+8 latch; upload/seed/release/H2D) | reload-after-release safety |
| H6 | remove ordering: cfg80211 teardown before HCC rings free | teardown race |
| H7 | scan-after-AP and dead-VAP scan refusal | scan/AP interaction |
| H8 | hcc_trace=0 default suppresses per-frame/periodic hcc-core console lines (hcc_trace=1 restores historic output) | console/perf census: 128 lines/session, 9.99 ms/line |

Final combined identity: hardened module 262,852 B sha256 `62a1aeb1...`, unstripped 970,604 B
sha256 `a51d8685...`, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8,
depends=cfg80211, 0 warnings.  Chain asserted by the build:
base 44c6cc12 (hardware-fired) -> polished d145a205 (image lane) -> dp-tuning 175a4617 (first MAC TX COMPLETED) -> scan-tuning 0002 all-five-files (batched scan; scan_req_batch=6, scan_batch_gap_ms=300; 10-line cfg80211 provider symvers incl cfg80211_bss_iter) -> hardened.
NOT fired; the attended ladders are E2 (stop/restart) and E3 (failed start).
Note: the stripped sha is build-directory dependent via .note.gnu.build-id
(the debug info embeds the build path; the base and polished builds use their
canonical directories so the pinned identities reproduce exactly).

## 4. Soak harness and PASS criteria

Delivered: `tools/soak_sampler.sh` (read-only device sampler, busybox shell:
golden page/state/mask/staging via bar0read, heartbeat count from
`rd_catalog` g4 id4, D2H frames/overruns from `hcc_ready`, dmesg delta,
hostapd pid/RSS/CPU, iw AP state, meminfo), `tools/soak.py` (host
orchestrator + offline analyzer + dry-run), `tools/soak_selftest.py` (33
checks).

Policy (grounded in the 20:30 tuning baseline: heartbeat 1.009-1.011 s, D2H
~145 polls/s 0 overruns, CPU idle, AP persists):

| criterion | PASS rule |
| --- | --- |
| boot page / exception | page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0 in EVERY sample |
| reboot | uptime monotonic |
| heartbeat | rate >= 0.2/s and no zero-heartbeat run > 15 s |
| D2H | overruns == 0, d2h drop == 0, frame count monotonic, frames >= 0.5 x duration |
| exception frames | g4 id3 `excp_info` == 0 |
| AP | hostapd >= 1 proc and `iw dev wlan0 info` type AP + ssid cudy-ap-test in every sample |
| hcc phase | ACTIVE in every sample |
| memory | MemAvailable drift <= 8 MB + 128 KB/min; hostapd RSS growth <= 4 MB |
| dmesg | no BUG/Oops/panic/WARNING/lane-closed/not-posted/REFUSED lines |
| sampling | span >= 98% of duration; coverage >= 95%; no gap > 3x interval |

Verdict classes and recovery: PASS, FAIL-EXCEPTION (stop, capture, power-cycle,
no retry), FAIL-REBOOT, FAIL-DMESG, FAIL-HEARTBEAT, FAIL-D2H, FAIL-HOSTAPD,
FAIL-MEMORY, FAIL-SAMPLING.  See SOAK.md for the protocol and
results/RESULT-TEMPLATE.md for the operator template.

## 5. Error-path matrix (what is host-tested, what needs the bench)

| id | path | host evidence | bench ladder |
| --- | --- | --- | --- |
| E1 | module rmmod/insmod cycling WITHOUT a release | stale-reload refusal compiled + policy review; no release in the script | E1 ladder (3 cycles + leak/counter checks) |
| E2 | AP -> hostapd stop -> VAP del (g0 id9) -> restart | controlled stop_ap implementation + host checks | E2 ladder (the decisive restart ladder) |
| E3 | failed start -> retry (clean failure) | gate logic (clean failures may retry; dirty may not) | E3 ladder (bad-channel failure then correct config) |
| E4 | repeated C2 ladders across reboots (idempotency) | soak/dry-run plan; analyzer test | E4 ladder (3 reboot cycles, gate equality) |
| E5 | exception recovery (mask != 0 -> power-cycle) | analyzer EXCEPTION verdict + recovery text | E5 check-only mode (readback -> recovery card) |
| D | scan -> AP interaction (boots A-D) | code + cfg80211 source read; A/B hardware-confirmed | C/D running (Lead) |

## 6. Evidence

* Boot A/B/C sequences + numbers: Lead messages (quoted in section 2 and in
  ladders/scan-ap-diag.json); boot A exception readback:
  page 0x00119758/0x0000009c/0x00000000/0x00000002, `ap_cfg` announce
  out[0]=0x8 after 50 polls, hostapd failure lines.
* Host checks: evidence/hostcheck.txt (HOSTCHECK PASS; 13 policy checks),
  evidence/soak-selftest.txt (33/33), evidence/build-chain.txt (base
  identity, polished identity, hardened identity).
* Build script assertions: base 242,572/44c6cc12 reproduced; polished
  242,920/d145a205 reproduced; hardened built with 0 warnings.
* Ledger baselines used for PASS policy: 2026-11-27 20:30 tuning entry.

## 7. Limits and next steps

1. Fire ladders E2/E3 with the hardened module (one attended session each,
   fresh boot): E2 decides whether controlled stop/restart is production-safe;
   E3 confirms clean-failure retry.
2. Run the 60 min soak (`tools/soak.py run --duration 3600 --attended
   --yes-really --recovery-armed`) against the hardened or polished module.
3. Rebuild the image with the hardened kmod once E2/E3 pass (image lane owns
   image rebuild); keep PD4 policy (never packaged) and the power-cycle rule
   for any exception state.
4. C/D results (Lead) to be appended to ladders/scan-ap-diag.json and this
   report when available.
