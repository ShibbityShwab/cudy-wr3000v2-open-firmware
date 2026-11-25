# LANE VERIFICATION - the backlog swarm (verification pass, 2026-11-27)

The Lead's verification of each lane's committed artifacts. A lane is PASS only when its
claimed tests actually run from the committed files and its patch/recipe matches the proven
bench protocol.

## rx-h2d-trace (task-176, RX-1/RX-2) - PASS
- Artifacts: README/REPORT/TABLES-debug-interfaces/H2D-FORMATS/RUNBOOK + tools (a shell sampler
  wrapper + the C sampler source + its build script).
- The decisive result: the vendor's H2D messages are plain HCC frames in host DRAM posted as
  8-byte SR nodes into a PCIe-visible ring -> the stock pair-A boot is H2D-traceable READ-ONLY
  from userspace via the BAR0 window + /dev/mem (the phase-25 mechanism) or kprobes on
  hcc_msg_tx (tracefs proven on the stock 5.10.201); the vendor debug modules carry no H2D log
  (their save_hostmem is a DDR dump). RUNBOOK T1-T7 is a complete attended procedure with the
  safety rules.
- Verdict: PASS (the plan is executable; no device writes; the fire is the Lead's).

## ax-he-caps (task-177, AX-1) - PASS
- Host tests: ./tools/run-host-tests.sh -> 109 checks, 0 failures (verified by the Lead).
- The patch (patches/0001-he-caps.patch) is a cfg80211 diff (the wiphy HE caps advertisement).
- Verdict: PASS (the tests reproduce; the patch exists; the fire recipe is in the REPORT).

## rx-ba-agg (task-178, BA-1/BA-2) - PARTIAL
- The decode is substantive (the hmac_ba_* machinery, the ADDBA/DELBA builders, the host/fw BA
  message contract, the minimal A-MPDU session-setup sequence).
- GAP: the committed selftest (src/ba_selftest.c) is not userspace-buildable from the committed
  files alone: src/hi5622_ba.h includes <linux/types.h> and src/hi5622_ba.c includes
  <linux/moduleparam.h>, i.e. it is a kernel-module pair; the "183 checks" claim was produced by
  the lane's local /tmp build with a types shim, which is not committed. The byte-level claims
  therefore need either (a) a userspace-compilable portable model committed (the pattern of the
  other lanes: a standalone .c with no kernel headers), or (b) a kernel M= build script.
- Verdict: PARTIAL (the decode is usable; the tests need the portable model before the fire).

## rx-5g-beacon (task-175, 5G-4) - IN PROGRESS
- The WIP dir exists; the lane is still running.

## rx-rate-ctl (task-179, RC-1) - IN PROGRESS

## The standing rule
A lane's fire recipe is NOT trusted until: the host tests run from the committed files, the
patch applies to the pinned base, and the recipe matches the proven bench protocol
(BENCH-FACTS/LESSONS: recovery armed, UART read-only, one AP per load, power-cycle on the
exception mask/page change).

## Update (after the lanes' final commits)

### rx-ba-agg - the PARTIAL verdict is now PASS
The lane's final commit (792d2af) added the userspace-compilable portable model:
./tools/run-host-tests.sh -> "ba_selftest: 183 checks, 0 failures" (verified by the Lead).
It also adds the fail-closed ba_enable skeleton patch + 147 static anchors re-verified against
the vendor module/firmware + the fire recipe. The earlier gap (a kernel-header-dependent
selftest) is resolved.

### rx-rate-ctl (task-179) - the decode delivered
docs/incoming/wifi-rate-ctl/ (README/REPORT/TABLES/src/patches/tools/evidence) - the vendor
alg_cfg engine / rate-control command surface decode + the prepared bench ladder. Host tests
to be re-run by the Lead before any fire (the verification pattern).

### The swarm's final state (all 5 lanes delivered)
- rx-h2d-trace (RX-1/RX-2): PASS - the stock H2D trace plan + the tools + the F1 bound
  ("more static analysis cannot close F1 - the trace is the discriminator").
- ax-he-caps (AX-1): PASS - the HE decode + the module (475975d3) + the fire recipe.
- rx-ba-agg (BA-1/BA-2): PASS - the BA machinery + the portable model + the patch + the recipe.
- rx-5g-beacon (5G-4): PASS - the mis-addressed-readback correction + the synchronous ap_chan
  insight + the module (bc1cc0ea) + the G6a/G6b recipe.
- rx-rate-ctl (RC-1): the decode delivered.

