# wifi-5ghz-vap-msg - the 5 GHz AP user/VAP message order (task-174)

HOST-ONLY lane. This directory is the decode + fail-closed patch + fire
recipe for the last 5 GHz AP blocker: the AP interface creation's firmware
user add (g0 id7/id8) is not consumed by the firmware on the one-radio/
two-band profile, so the H2D lane closes and the AP-start VAP add is
refused. The bench state, the vendor decode and the fix rationale are in
`REPORT.md`; the evidence tables (every decoded offset) are `TABLES.md`.

## What is here

| path | content |
| --- | --- |
| `REPORT.md` | vendor disassembly decode, the root finding (order, not fields), the patch contract, limits |
| `TABLES.md` | T1-T9: function offsets, g0 id7/id8 layouts, band model, AP-start band-1 fields, diff, patch contract, host-test matrix |
| `FIRE-RECIPE.md` | the bench rung G0-G7 with exact PASS/FAIL gates (the Lead fires) |
| `patches/0001-vap5g-order.patch` | the fail-closed patch against the 5 GHz ready module (57 params -> 58) |
| `src/hi5622_vapmsg5g.{c,h}` | the portable vendor-order planner used by the driver; byte-identical copy of the patched tree |
| `src/vapmsg5g_selftest.c` | host selftest of the planner matrix (450 checks) |
| `tools/frame_bytes_selftest.c` | host byte checks of the g0 id7/id8 and VAP-add messages (103 checks) |
| `tools/run-host-tests.sh` | runs both host tests (no device) |
| `tools/build-vapmsg5g-ready.sh` | rebuilds the 5 GHz base, applies the patch, builds the 6.18.60 module, runs the artifact contract |
| `evidence/` | host-test and build-artifact transcripts |

## Build (host-only)

    ./tools/build-vapmsg5g-ready.sh      # -> /tmp/wifi-vapmsg5g-artifact
    ./tools/run-host-tests.sh            # 553 host checks, no device

The build pins the 5 GHz ready base artifact (sha256 `13366151...892d53`,
57 params, the module under bench test) and refuses if the base chain
drifted. The produced module is 353,652 B stripped, sha256
`13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf`, 58
params (`vap5g` default 0). It is local evidence only, never committed.

## The parameter

    vap5g=0   legacy ordering (default, fail closed; byte-identical to the
              bench-validated 57-param module)
    vap5g=1   vendor order for an AP on a multi-band wiphy: the interface-
              creation user add is DEFERRED and posted after the AP's
              structural vap_add in start_ap, where the chandef knows the
              band; the frame bytes keep the decoded encoding (user add on
              mac chip 0, band carried by the VAP create)
    vap5g>1   refused at use time (-EINVAL), no frame (never guessed)

## Status

Patch, host tests and host build are DONE and committed. The fire is
**not executed by this lane**: `FIRE-RECIPE.md` is the one-shot rung the
Lead runs on a fresh (power-cycled) chip. PASS = the deferred user add is
consumed after the VAP add + the five AP-start frames + hostapd AP-ENABLED +
golden page; FAIL = stop, capture, power-cycle, no retry (the recipe records
which gate failed and what that falsifies).
