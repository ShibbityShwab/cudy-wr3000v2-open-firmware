# incoming/qm-decode - round 4d: zero-engine hang + W9 master switch (patch 042)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 9).  Prior record: `032`, `036`,
`038`, `040`, `041` (all delivered; 038/040 integrated).  This package delivers **patch 042**:
a master switch that makes all W9 code inert at runtime, for the isolation matrix.  No device access.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Round-4d result (image 2d904721 = 001-041, seven gates, W9 read-only, trace=0, nodoorbell=1)

- Boot fine; four `TX doorbell SUPPRESSED@BT@ lines confirm descriptor build **without** doorbell writes
  (the old `pie-trace TX doorbell=N@BT@ line is only the trace print; source confirmed).
- Traffic test -> still hung.  Last lines: second eth2 link-up / br-lan forwarding at 40.4 s, two
  more xmit attempts (len 110/130) and eth3 PHY init at 40.95 s, then silence; no Oops/panic, no
  loader fallback (physical recovery needed).
- The 001-037 six-gate image survived the same test.  Deltas left: 038/040 W9 code (probe-time diag
  reads of 0x100/0x0/0x40 on all four GEMACs; extra `ndo_change_mtu@BT@ op) and the SMP patches
  039/040-arm that the 001-037 image predates.

## 2. Audit: what 038/040/041 actually do with gates off / read-only

| code | when inert | when active (this run) |
| --- | --- | --- |
| 038/040 W9 | `w9/hwinit/hwinitwrite@BT@ gates | probe did **3 MMIO reads** on each of 4 GEMACs; registration still required hwinit=1; `ndo_change_mtu@BT@ op present |
| 040 macconf | `macconf=0@BT@ (off) | nothing |
| 040 trace | `trace=0@BT@ (off) | nothing in xmit |
| 040 txhalt | `txhalt=0@BT@ (off) | nothing |
| 041 nodoorbell | `nodoorbell=1@BT@ | skips the TX wptr doorbell write; logs first 4; no other MMIO |

So in image 2d904721 the only new runtime effects vs 6cb3d908 were the **probe-time W9 diag reads**
and the extra MTU op - or the SMP delta.  Note the hang phase (second link-up / bridge bring-up at
~40 s) is when userspace opens/closes the netdevs and traffic starts.

## 3. Patch 042: W9 master switch (default OFF)

- New `luofu_gmac.w9=0|1@BT@ (default **0**).  With `w9=0@BT@:
  - no W9 register is read or written in probe (the diag function is not called),
  - no W9 registration gate is applied (the netdev registers exactly as on 001-037),
  - the pre-038 netdev ops table is installed (no `ndo_change_mtu@BT@).
- With `w9=1@BT@ the previous behaviour is restored: hwinit=1 required for the read-only diagnostic,
  hwinitwrite/hwinit_port control the single-port write, and the MTU op is present.
- The 041 instrumentation (`trace@BT@, `txhalt@BT@) and PIE `nodoorbell@BT@ stay independent of the master.

## 4. Proposed isolation matrix (build from any patch subset)

1. **Idle boot of the current full series, no traffic test, 2+ min**: if it hangs at the same phase
   the trigger is not traffic; if it survives, traffic/engine interaction is involved.
2. **Full 001-042 image, cmdline WITHOUT `luofu_gmac.w9@BT@** (W9 inert) + `nodoorbell=1 trace=0@BT@:
   survives => W9 probe reads / MTU op implicated; hangs => delta is elsewhere (next: SMP).
3. **001-037 only** (control): re-run the exact traffic test on the known-stable subset.
4. **001-037 + SMP 039 + SMP 040-arm** (no 038/040-net/041): isolates the SMP delta directly.

Also worth adding to the next runs: keep `oops=panic panic=0@BT@ (a bus hang produces no dump, but a
fault will), and note whether the 30 s luofu-wdt reset fires or the box needs physical recovery
(2d904721 did not reset by itself).

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/042-net-gmac-w9-master-off.patch` | W9 master switch (default off): zero W9 MMIO/gate/MTU-op when off; restores 038-041 behavior when on | `2e49e3eb6d94fe47887f146781073b5eabde15f2db9c1c32d172a26683092dab` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | LSW-SPEC 7.11 round-4d note + isolation matrix, SKELETON-SMOKE round-4d session, README 042 row | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- **Series apply over the current full series + 042 (43 patches):** 43/43, 0 failed hunks,
  0 .rej/.orig; `luofu-gmac.c` byte-identical to the lane tree (`logs/10-series-apply-042.log`).
- **WERROR compile:** `luofu-gmac.o` rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected commit-description warning
  (`logs/11-checkpatch-042.log`).
- No devicetree/binding/Kconfig change; no vendor binary or per-unit value added.

## 7. Numbering note

The skeleton currently holds committed `040-net-gmac-w9-diag.patch` plus untracked
`040-arm-hisilicon-smp-scu-gic.patch` (SMP) and my `041@BT@/`042` patches.  Series application in sorted
filename order works (43/43), but renumber the SMP 040 to a free number if a strictly monotonic
series is wanted.
