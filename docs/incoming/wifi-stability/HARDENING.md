# HARDENING - driver state-machine gaps and the stability patch

**Lane:** wifi-stability (task-136).  Host-only.  The patch applies on top of the
image-polish source delta (image-polish owns `mgmt_diag`; this lane does not
duplicate it) and is compile-verified against the 6.18.60 tree.

## 1. Gaps found (ranked by bench evidence)

| id | gap | evidence | disposition |
| --- | --- | --- | --- |
| G1 | v7 `static bool hi5622_vap_base_posted` is MODULE-GLOBAL and never cleared by a user_del: after a scan posted the base, an iface delete/re-add makes start_ap skip the base, so ap_cfg runs against a dead firmware VAP object | bench boot A + D: firmware EXCEPTION (page 0x00119758/0x9a..0x9c/0/2, golden 0), ap_cfg announce out[0]=8 after 50 polls, hostapd "Failed to set beacon parameters" | **fixed**: per-context latch + clear on committed user_del + reset on probe |
| G2 | `.stop_ap` is a stub returning -EOPNOTSUPP although the firmware AP-stop message is [U]; cfg80211 therefore keeps ap.beacon_interval and every later NEW_BEACON in the boot fails -EALREADY before the driver | bench boot B (AP-ENABLED then restart fails) and C (same with a scan in between; clean page, no exception) | **fixed**: controlled stop_ap (two modes, see 2.2) that clears cfg80211 state safely; default mode does no firmware write |
| G3 | no validation that the firmware VAP object (module_ctx+0x190) is alive before AP frames are posted | boot A/D crash mechanism | **fixed**: read-only readback guard (bench-proven ladder addresses) -> refuse -EIO before any AP frame |
| G4 | no AP-start state machine: a partial sequence that posted frames, or a base posted for a non-AP iftype, could be re-posted | boot A failure path; v7 "one start per load" was documented but not enforced | **fixed**: started/dirty/wrong-iftype gates + dirty latch on abort |
| G5 | scan after an AP start is posted although the firmware scan engine was measured to return 0 results; scan against a dead VAP object possible | tuning baseline (scan while AP up = 0 results); boot A latch state | **fixed**: -EBUSY refusals with clear messages |
| G6 | a module reload after a live boot could re-run upload/seed/release against running firmware (would overwrite live firmware RAM) | code read: only `release` checked page+8 | **fixed**: probe latch + upload/seed/release/H2D refusal (-EBUSY, power-cycle) |
| G7 | teardown order: `hi5622_hcc_remove()` ran before `hi5622_cfg80211_remove()`, so a pending scan work could post through freed HCC state | code read: probe order vs remove order inconsistent | **fixed**: remove = reverse of probe (dp, cfg80211, hcc) |
| G8 | mgmt_tx dump_stack noise on every AP bring-up | bench boot v5/v6 dmesg; requested by the Lead | **fixed in image-polish** (`mgmt_diag`, default 0); this lane builds on it |
| G9 | hostapd AP stop/restart cannot be exercised in one boot; firmware AP-stop message unknown | boots B/C | **documented** + controlled stop_ap modes; full VAP-teardown restart needs ladder E2 |

## 2. Patch map (patches/0001-stability-hardening.patch)

Files changed (7): `hi5622_driver.h`, `main.c`, `transport.c`,
`hi5622_frame_post.c`, `hi5622_scan.c`, `hi5622_cfg80211_ops.c`, plus the new
`hi5622_stability.h` (pure host-tested gate; copy kept in patches/files/).

### 2.1 Per-context latches (G1, G4)

`struct hi5622_pci` gains:
`chip_booted_at_probe`, `vap_base_posted`, `vap_base_iftype`,
`ap_started`, `ap_seq_dirty`, `ap_seq_frames`, `fw_vap_deleted`.
The module-global latch is deleted.  `start_ap` gate:
`hi5622_stab_ap_gate(started, dirty, base_posted, base_iftype)` ->
ALREADY_STARTED (-EBUSY), SEQ_DIRTY (-EIO), BASE_WRONG_IFTYPE (-EIO).
On a committed user_del the base/AP latches are cleared, so the next scan or
AP start re-posts the mode-1 VAP base (the exact boot-A fix).

### 2.2 Controlled stop_ap (G2) - two modes, default safest

Module param `stop_ap_mode` (default **0**):
* **0 = host-side stop only (default): no firmware write at STOP time.**
  Clears ap_started/dirty state and returns 0, so cfg80211 clears
  ap.beacon_interval and hostapd may restart.  Bench E2 (first fire): the
  restart then reached start_ap (the -EALREADY barrier is gone) but the second
  hostapd still failed later in the sequence - a plain AP-config re-post against
  the already-started firmware AP is not reliable.  Fix: the NEXT start_ap sees
  ap_stopped_hostside and first RESETS the firmware VAP with the individually
  validated user_del (g0 id9), then re-creates user_add/notify + the mode-1 VAP
  base, then posts the fresh AP config.  Any reset failure aborts fail-closed.
  Expected restart dmesg is in [evidence/e2-restart-expected-dmesg.txt](evidence/e2-restart-expected-dmesg.txt).
* **1 = controlled firmware teardown:** posts the individually validated
  user_del (g0 id9) for the AP user, marks `fw_vap_deleted`, and a later
  start_ap re-creates user_add/notify (g0 id7/id8) + the mode-1 VAP base.  Any
  message failure returns the error to cfg80211 (no fake success, no in-boot
  retry).  **Not bench-validated as a sequence** - ladder E2 decides.

Handler precedence in `hi5622_scan_op_stop_ap`: if a provider ever supplies a
real AP-stop builder it is used first; otherwise if an AP was started the
controlled policy above applies; otherwise -EOPNOTSUPP.

### 2.3 Stale-VAP readback guard (G3)

Before AP frames are posted against a base that was already posted by an
earlier operation, and before a scan request when the base pre-existed:
`hi5622_scan_vap_object_valid()` reads (READ-ONLY) module-table slot 0
(firmware CPU 0x175458), the module-table base offset (CPU 0x17a504) and the
VAP pointer at ctx+0x190; zero => refuse -EIO with "stale/dead VAP,
power-cycle" and post NO frame.  Addresses are the bench-proven AP-start
ladder readback recipe.  When start_ap itself posts the base in the same call,
the object is fresh and the check is skipped.

### 2.4 Scan guards (G5)

* scan after a committed AP start: -EBUSY ("firmware scan engine does not
  complete while the AP is active - measured 0 results; scan before AP start").
* scan when the base latch was set but the VAP pointer reads zero: -EIO.

### 2.5 Stale reload refusal (G6)

`hi5622_probe()` reads handshake page +8 once (READ-ONLY); if it is 2
(firmware already booted from a previous module load) it latches
`chip_booted_at_probe`; `upload`, `seed`, `release` and the H2D frame post
path then refuse with -EBUSY and a power-cycle message.  The pre-existing
`release` page-check stays as the last gate.

### 2.6 Remove order (G7)

`hi5622_remove()`: dp_remove -> cfg80211_remove -> hcc_remove (reverse of
probe), so scan work is cancelled and VAPs unregistered before the HCC service
and rings are freed.

### 2.7 hcc_trace hot-path suppression (final polish item)

The bench console census found 128 hot-path hcc-core lines per session
(per-frame txq_sync/h2d_sent/h2d_complete/d2h_word/d2h_dispatch plus the
heartbeat ~1 line/s; console printk measured 9.99 ms/line => ~1.3 s/session
and continuous console I/O while the AP is up).  New module param **hcc_trace**
(default **0**): when 0 the adapter drops the hot events (TXQ_SYNC, HEARTBEAT,
H2D_SENT, H2D_COMPLETE, D2H_WORD, D2H_DISPATCH) before the dev_info; every
one-shot/failure line (boot latch, ready, timeouts, refusals, invalid/unknown
frames, exception vector capture, cece) stays always on.  hcc_trace=1 restores
the historic output for debugging.  No periodic HB print exists in this tree
beyond HCC_EVENT_HEARTBEAT, which is covered by the same gate.

## 3. Build identities (host, not fired)

```
stack 1  dp-air + ap-start v7                   242,572 B  44c6cc12...  (hardware-fired)
stack 2  + wifi-image-polish delta              242,920 B  d145a205...  (polished, not fired)
stack 3  dp-air + ap-start + dp-tuning TXQ       251,924 B  175a4617...  (bench-validated, TX COMPLETED)
stack 4  stack3 + polish + scan-tuning 0002
         + this lane's harden.                   263,196 B  d6b85fff...  (FINAL COMBINED, NOT fired)
```

`tools/build-hardened-module.sh` asserts stacks 1-3 byte-for-byte (base
44c6cc12, polished d145a205, dp-tuning 175a4617) before building the final
combined artifact, so a drift in any upstream pin fails closed.  Stack 4 is
the build the Lead should fire for the final validation.
Warnings: 0; vermagic 6.18.60 SMP mod_unload ARMv7 p2v8; depends=cfg80211.

Build-dir note: .note.gnu.build-id hashes the debug info, which embeds the
absolute build path.  Each stack is built in the canonical directory recorded
for its identity; the hardened artifact's sha is path-dependent for that
reason (documented in the script).  No .ko is committed.

## 4. Host verification

* `tools/run-hardening-check.sh`: 13 policy-gate checks (hardening_selftest.c)
  + patch static checks (7 files, expected anchors, no duplicated mgmt_diag)
  + apply dry-run on the polished source tree -> HOSTCHECK PASS.
* `tools/build-hardened-module.sh`: full three-stack build, 0 warnings.
* Evidence: [evidence/hostcheck.txt](evidence/hostcheck.txt),
  [evidence/build-chain.txt](evidence/build-chain.txt).

## 5. What still needs the bench

1. **E2** (controlled stop_ap + restart): first with `stop_ap_mode=0` (expected:
   hostapd stop succeeds, restart AP-ENABLED, golden page), then optionally
   `stop_ap_mode=1` (firmware VAP teardown + re-create).  E2 is the gate for
   replacing the "power-cycle after AP stop" rule.
2. **E3** (failed-start retry): clean failure (bad channel) then correct config;
   confirms the clean-failure retry policy.
3. **E1** (module cycles without release) and **E4** (repeated C2 across
   reboots): idempotency/leak ladders.
4. Soak: 60 min run against the hardened module (or polished, for a baseline).
5. A final combined build must merge this patch with dp-tuning's module
   `175a4617` and image-polish `d145a205` feature sets (each lane's patch is a
   separate delta; the combined build needs an explicit rebase - flagged to the
   Lead).
