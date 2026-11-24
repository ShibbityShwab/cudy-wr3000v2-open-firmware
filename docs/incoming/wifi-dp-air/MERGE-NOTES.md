# MERGE-NOTES - wifi-dp-txrx/air data plane merged into the mode-1 provider

**Lane:** wifi-dp-air (shared task task-126), host-only, 2026-11-26.

## 1. Why this merge exists

The bench has two different module builds that were each hardware-validated in
isolation:

1. **the mode-1 provider** (../wifi-scan-complete/src = wifi-scan-ap +
   wifi-vap-start provider patch + scan-complete): cfg80211 wiphy, real VAP
   lifecycle, real scan with firmware results, 13-channel/beacon work. Module
   748,888 B sha256 b42ff4ec... (D2 TRUE PASS, 2026-11-26).
2. **the data plane** (../wifi-datapath-bringup + ../wifi-dp-txrx): E1
   dp_probe/dp_setup/dp_rings + the key-0x0320 TXQ-table address sync, and the
   wifi-dp-txrx first-TX/RX actions. Those live in a *separate, smaller*
   module with no cfg80211 and no VAP/scan path, so a posted TX frame had no
   started firmware VAP/user object - the air side could not be exercised.

The air-TX precondition is the firmware VAP/user object, which the working
chain creates through the *.provider* mode-1 VAP ladder (vap_add g0 id1
key 0x64 + vap_start key 0x65, posted from the scan path). One module load must
therefore carry **both** the provider and the data-plane objects. This lane
builds that merged module and the first air-TX/RX ladder on top of it.

## 2. Merge base and result

| item | value |
| --- | --- |
| base | `../wifi-scan-complete/src` - the provider tree, 47 source files |
| lane source | `src/` - 55 source files (47 unchanged + 8 added) |
| merge patch | `patches/0001-merge-dp-txrx-into-provider.patch`, 142,595 B sha256 084c2d34... (revision 2) |
| changed/added files | exactly 16 (8 added + 8 modified), enforced by tools/verify_merge.py |
| module | **886,864 B** sha256 ca32aec4... (canonical build path in BUILD-NOTES.md; revision 2 adds the dp_probe dual read/write-1 form + clearer probe-gate refusals) |
| provider delta | +137,976 B over the 748,888 B provider module (data-plane objects + their debug info); the provider code itself is unchanged |

The merge is reproducible and drift-checked: tools/verify_merge.py regenerates
the patch from (base -> src), compares it byte-for-byte with the committed
patch, stages a copy of the base tree, applies the committed patch and compares
every resulting file with `src/`. tools/build.sh runs it first.

## 3. What was added (8 files, byte-identical to their source lanes)

| file | origin | role |
| --- | --- | --- |
| dp_rings.c / dp_rings.h | wifi-datapath-bringup | portable E1 ring geometry, device-RAM MSDU-info table arithmetic, MAC slot-5 register program vendored into the provider build unchanged |
| hi5622_dp.c / hi5622_dp.h | wifi-datapath-bringup | E1 host-side data plane: dp_probe (one READ-ONLY word MAC reachability read), dp_setup (ONE ring-allocation + device-RAM table + MAC band-0 program with read-back), dp_txq_sync (key-0x0320 hand-off), dp_status/dp_rings (RO evidence). `hi5622_dp.c` additionally carries the wifi-dp-txrx patch hunks (a `hi5622_dp_txrx_bind()` call at the end of a successful dp_setup, `hi5622_dp_txrx_release()` on failure and in dp_remove, and the four txrx device attributes inside the existing dp attribute group) **and the revision-2 probe-gate UX fix** (see section 8) |
| dp_txrx.c / dp_txrx.h | wifi-dp-txrx | portable, host-tested TX/RX logic: 8-B MSDU-info ring entry encode/decode, 12-B device-RAM table entry pack/publish fields, packed ring index arithmetic, 802.11 probe-request/null-data/QoS-null/ARP builders + FC mapping, [owner][16-B zero][MPDU] frame-buffer layout, RX 0x28-B descriptor parser + bounded capture |
| hi5622_dp_txrx.c / hi5622_dp_txrx.h | wifi-dp-txrx | kernel extension: one coherent TX frame buffer, dp_tx_frame (WO: first MAC data-plane TX post + device-RAM wptr publish + read-back), dp_tx_status (RO), dp_rx_poll (WO: ONE bounded read-only RX capture action), dp_rx_capture (RO); gates on the E1 setup + this load's 0x0320 sync |

## 4. What was modified (8 files)

| file | change |
| --- | --- |
| Kbuild | + `dp_rings.o hi5622_dp.o dp_txrx.o hi5622_dp_txrx.o`; comment records the merged data-plane layer |
| main.c | `#include "hi5622_dp.h"`; `{ "dp", ... }` attribute group registered after d2h; `hi5622_dp_probe()` at PCI probe after the cfg80211 init; `hi5622_dp_remove()` at remove (before hcc/cfg80211 teardown); module description updated |
| hi5622_driver.h | forward-declare `struct hi5622_dp`; add `ctx->dp` next to the existing `ctx->cfg` (both layers coexist) |
| hi5622_hcc.h | add `h2d_txq_addr_sent` counter (the 0x0320 hand-off's per-load evidence) |
| h2d_send_logic.h | add H2D_TXQ_ADDR_TOTAL/KEY, two gate values (NEED_CTX, DUP_TXQ_ADDR), `h2d_live.txq_addr_sent`, and the hand-off builder/build/gate declarations |
| h2d_send_logic.c | split `h2d_gate_check()` into common pre/post helpers (semantics preserved, see invariant I2); add gate names; add the exact key-0x0320 builder `h2d_build_txq_addr_sync()`, the validated wire builder `h2d_msg_build_txq_addr()` (refuses a zero device VA) and the hand-off gate `h2d_gate_check_txq_addr()` |
| h2d_send.c | `h2d_post()` gains an out-of-band `txq_devva` parameter (every existing call site passes 0) selecting the data-plane build/gate path; increments `h2d_txq_addr_sent`; adds the driver-internal entry point `h2d_post_txq_addr()`; h2d_status now reports `txq_addr_sent`, the `txq_addr_sync` gate and the hand-off as the last attempt |
| h2d_send.h | declare `h2d_post_txq_addr()` |

## 5. Design invariants (why the provider cannot silently change)

**I1 - the hand-off is not a new H2D message family.** `H2D_TXQ_ADDR_SYNC` was
*not* added to `enum h2d_msg` and the `h2d_send` sysfs grammar is unchanged, so
(a) the inherited every-message emission loop and the 242-check control-plane
selftest are untouched, and (b) the 0x0320 frame can only be sent by the driver
itself after dp_setup (`h2d_post_txq_addr()`, fed by a DMA address the driver
allocated - never user input). The gate still applies every common boot/phase/
budget/coalescing rule plus "needs this load's ctx_create" and "once per load".

**I2 - gate semantics for every existing message are unchanged.** The gate
refactor moved the identical checks into `h2d_gate_check_pre()` (armed,
released, boot_latched, lane_closed, phase) and `h2d_gate_check_post()`
(liveness, budget, announce_inflight, out0), preserving the original order and
return values across the message-specific checks in between. Proof: the
inherited H2D selftest is 242/242 on the merged tree, exactly as on the base.

**I3 - the provider surface is additive only.** No cfg80211/scan/VAP file is
modified; the unchanged provider regressions are 290/290 (scan logic), 195/195
(scan-complete) and 454/454 (cfg80211) on the merged tree, and the cfg80211
provider symvers still declares exactly the 9 imported symbols.

## 6. Deliberately NOT merged

* **The C2 calibration family** (`h2d_cfg_cal`, g0 id2 key 0x0101, six
  alg set_* frames) from the datapath-bringup lane is *not* part of this merge.
  The one-command matrix runs calibration as its own phase/module (C2); the
  merged F-AIR module has no `h2d_cfg_cal` attribute and the F-AIR sequence
  does not claim a calibrated run. A future single-module C2+F-AIR build is a
  separate lane (it would re-introduce the E1 `h2d_msg_build_ext`/cfg_cal
  code paths that this merge avoided on purpose).
* **RX consume/repost** stays disabled (capture-only, no ring pointer moved):
  the RX completion-word format is still [U]; see ../wifi-dp-txrx/REPORT.md 4.4.
* **Band 1** is not programmed (same as E1); every MAC write is band 0 only.

## 8. Revision history

* **revision 1** (2026-11-26, commit a615a8c): first merge; module 884,576 B
  sha256 ec74fccd...; patch 139,848 B sha256 254148c3...
* **revision 2** (2026-11-26, after the air-TX fire): module 886,864 B sha256
  ca32aec4...; patch 142,595 B sha256 084c2d34.... Changes are limited to
  `hi5622_dp.c`: `dp_probe` now accepts BOTH `cat dp_probe` and
  `echo 1 > dp_probe` (the write-1 form runs the same single read-only MAC
  word read; never a device write) and the missing-probe setup refusal returns
  -ENODATA with the gate name `probe_read_required` plus an explicit
  "cat dp_probe first" warning; dp_txq_sync's not-set-up refusal prints the
  full attr order. Gate semantics are unchanged (a `dp_setup` still requires
  this load's probe read, ctx_create, ready phase and boot gates). The
  revision-1 artifact remains ladder-correct as long as `dp_probe` is read
  with `cat`.

## 7. Verification map

| claim | check |
| --- | --- |
| merge reproduces from the provider base, exact scope | `tools/verify_merge.py` (patch drift, scope, apply-and-compare) |
| provider behaviour unchanged | inherited host regressions 290/195/454/242 on the merged tree |
| txrx logic intact | inherited portable TX/RX selftest 254/254 on the merged dp_txrx.c |
| merge invariants + E1 constants + 0x0320 frame/gate | `tools/merged_selftest.c` 48/48 |
| clean module | `tools/build.sh` section 5: 0 warnings, vermagic 6.18.60, 7 data-plane symbols, 9 cfg80211 imports, parameters |
| ladder is executable + allowlisted | F-AIR sequence validation through the bench tooling (30 steps, 344 lines, 7 writes) |
