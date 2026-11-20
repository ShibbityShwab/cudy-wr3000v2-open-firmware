# incoming/qm-decode - round 4b: W9 diag/write split + formula fix (patch 040)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 7).  Prior record: `032` (tiered
W7), `036` (W8 PFE), `038` (W9 MAC max-frame) - all integrated.  This package delivers
**patch 040**: the W9 maximum-frame formula correction plus the safety split forced by the round-4b
traffic-triggered reset, with hang instrumentation.  No device access in this lane.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Bench results that drive this patch

- Image a6b57361 (001-037 + 038, 2 CPUs, early_probe): seven gates armed including W9, eth2 linked,
  then the box **reset/fell back to stock during/after the traffic test**.  W9 logged
  `0x100 = 0x334005e0, mtu 1500 + 4, snapshot 0x334005ee`.
- Image 45d248ff (001-038, seven gates, **stable 1-CPU, no early_probe**): booted cleanly, W7/W8/W9
  all verified, GEMACs registered, eth2 1 Gbps, br-lan forwarding, then **the traffic test reset the
  box again** (192.168.10.1 reachable = stock fallback).  So the reset correlates with **W9 plus
  traffic**, not with boot.
- Image 6cb3d908 (W8 PFE + reserve_low1m, **no W9**): survived the same traffic test (no frames, no
  reset).
- The oops register/backtrace dump is not in either capture (the loader fallback reset the box; the
  kernel has no pstore/ramoops), so the faulting function is still unidentified.

## 2. Finding: the W9 value was 14 bytes short (fixed here)

Found default @BT@0x334005ee@BT@ = 1518 = **MTU + 18**; the vendor @BT@hi_mac_hw_mtu@BT@ stores
(arg + 4) and the argument is the frame length without FCS (MTU + 14), so the correct register value
is MTU + 18 - exactly the 1518 default.  Patch 038 wrote MTU + 4 (1504), which would mark every
full-size frame over-max in both directions.  Patch 040 writes MTU + 18 and **never lowers a found
limit** (raises only when the MTU needs more).

## 3. Safety redesign (the safe next step requested by the lead)

- **W9 is now read-only by default.**  `luofu_gmac.hwinit=1` logs `max-frame 0x100`,
  `ctrl 0x0` and `port_mode 0x40` for every GEMAC and writes nothing - the stock-vs-ours
  comparison without touching the vendor state.  As before, the GEMAC front end refuses to register
  without it once the attended LSW/PIE chain is armed.
- **The write is a separate explicit gate**: `luofu_gmac.hwinitwrite=1` (requires hwinit=1)
  writes the corrected value **only on the port named by** `luofu_gmac.hwinit_port` (default
  **11 = eth2, the bench port**; @BT@0xffffffff@BT@ = all four).  Pre-write and post-write values are
  logged, and the snapshot / stable-two-reads / readback / restore-on-mismatch discipline is kept.
- **Hang instrumentation**: `luofu_gmac.trace=1` logs the MAC control/max-frame/port-mode
  words **before and after every GMAC xmit** (first 32 frames), on top of the existing PIE TX trace,
  so a watchdog reset can be attributed to the enqueue or to the engine processing the descriptor.
- Also fixes the misleading W7 STRICT wording: entries whose differences are outside the
  bench-proven fields are accepted, but patch 036 printed "failing closed" for them.

## 4. Answer: does the vendor write MAC 0x100 before the link?

[I, decode-based] The only writer of MAC register 0x100 in hi_kmac.ko is `hi_mac_hw_mtu`, which
is referenced **solely** from the GEMAC ops table (`.data+0x4@BT@; `hi_gemac_probe` passes
`.data` into `hi_mac_probe`).  The ops table is invoked from the MAC config/link callbacks
(`hi_mac_config`/`hi_mac_link_up` path), i.e. the vendor programs the maximum-frame limit
**as part of link configuration, after the MAC is probed and reset** - not from an earlier bare
probe and not as an enable.  It only sets a length limit; the MAC TX enable remains in the control
word 0x0 that our link-up path already writes, so writing 0x100 cannot by itself arm a TX path.

## 5. Crash hypotheses (for the lead's bisection)

(a) *Write on all four GEMACs* - unlikely to be a NULL/bus hang by itself (single RMW of a mapped
register, readback verified, executed ~4 s before the crash), but patch 040 still restricts writes
to the bench port so the next runs can tell.
(b) *Other MAC config words* - plausible: the vendor's `hi_mac_hw_config` writes
0x14/0x18/0x1c and read-modifies 0x40 around the same config path; our link-up writes only
0x40/0x1b4/0x0.  A wrong/incomplete sequence could leave the MAC in a state that faults on the
first queued frame.  Not implemented blindly; decoded values are documented for the next patch.
(c) *TX needs the PIE/LSW egress path we still lack* - plausible: with a frame now being accepted
for TX, a descriptor may reach an engine path (queue handle / egress deque) that is not fully
configured.  The `trace=1` instrumentation is designed exactly to catch where it dies.
(d) *Other 036/037 changes* - 037 was off in 45d248ff (no early_probe) and the reset still happened,
so 037 is not required for the crash; W8 alone did not reset (6cb3d908 survived), so the evidence
points at W9's change of MAC state/value rather than 036/037.

If the clean diagnostic image still resets, capture the dump with `oops=panic panic=0` on the
cmdline (halts with a full register/backtrace instead of the loader fallback; recovery stays
physical), and bisect W9-write off vs. write-on-bench-port.

## 6. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/040-net-gmac-w9-diag.patch` | W9 read-only diagnostic + explicit single-port write (MTU+18, never-lower) + `trace=1` xmit instrumentation + W7 wording fix | `58f15ad0344cfa7bd3e0e6160a21bc63ebd14f3eb3f9ccfadcef5a3aee4ed4f4` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | LSW-SPEC 7.11 correction/crash/vendor-ordering note, SKELETON-SMOKE round-4b session, README 040 row | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 7. Validation

- **Series apply (001-040, current series):** 40/40 applied, 0 failed hunks, 0 .rej/.orig;
  `luofu-gmac.c` byte-identical to the lane tree (`logs/10-series-apply-001-040b.log`).
- **WERROR compile:** `luofu-gmac.o` rebuilt clean under `W=1 -Werror` in the validated tree.
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning
  (`logs/11-checkpatch-040.log`).
- **Numbering note:** the SMP lane added `039-arm-hisilicon-smp-early-safe.patch`, so my
  formula fix and diag split are folded into this single patch **040** against the current series.
- No devicetree/binding/Kconfig change.

## 8. Next step guidance

Run the current series with **`luofu_gmac.hwinit=1` only** first (read-only diagnostic): boot,
compare `max-frame/ctrl/port_mode` against stock expectations, no traffic write risk.  Then, if
the diagnostic image survives, enable `luofu_gmac.hwinitwrite=1 luofu_gmac.trace=1` for the
bench port and run the traffic test: the W9 pre/post lines localize the write, and the gmac-trace
pre/post lines localize whether the hang happens at enqueue or when the engine processes the
descriptor.  Do not resume next-layer decode until this regression is understood.
