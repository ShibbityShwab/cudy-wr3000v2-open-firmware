# wifi-5ghz-beacon - the 5 GHz AP's channel-activation decode and fail-closed candidate (task-175)

HOST-ONLY lane. This directory is the decode + fail-closed patch + fire recipe
for the last 5 GHz AP blocker after the E2 fire: the AP reaches AP-ENABLED
with the full vendor message order and a golden page, the 2.4 GHz AP with the
same code beacons, but the 5 GHz beacons are not on air. The bench state, the
vendor decode and the reasoning are in `REPORT.md`; every decoded address and
the readback audit are `TABLES.md`.

## What is here

| path | content |
| --- | --- |
| `REPORT.md` | vendor disassembly decode, the E2 readback audit, the firmware activation gates, the candidate fix, limits |
| `TABLES.md` | T1-T10: function addresses (both vendor builds), band struct, 0x0124 body, netdev priv, sync points, readback audit, corrected gate pack, parameter contract, host-test matrix, fire gates |
| `FIRE-RECIPE.md` | the bench rung G0-G7 with exact PASS/FAIL gates (the Lead fires) |
| `patches/0001-beacon5g-gate.patch` | the fail-closed patch against the E2 5 GHz module (58 params -> 59) |
| `src/hi5622_beacon5g.{c,h}` | the portable channel-activation planner used by the driver; byte-identical copy of the patched tree |
| `src/beacon5g_selftest.c` | host selftest of the planner, address arithmetic and channel-ctx predicate (437 checks) |
| `tools/run-host-tests.sh` | runs the planner selftest + the patch contract (no device) |
| `tools/build-beacon5g-ready.sh` | rebuilds the E2 base, applies the patch, builds the 6.18.60 module, runs the artifact contract |
| `tools/beacon5g_re.py` | static-RE probes of the vendor host module (55/55 on both pinned builds) |
| `evidence/` | host-test, build-artifact and RE-probe transcripts |

## Build (host-only)

    ./tools/build-beacon5g-ready.sh      # -> /tmp/beacon5g-artifact
    ./tools/run-host-tests.sh            # 447 host checks, no device

The build pins the E2 module under bench test (sha256 `13e7bf4c...2cc1abf`,
58 params). The produced module is 356,600 B stripped, sha256
`bc1cc0ead2c5b98a35eea875c353dc7e5bcf2e39b61ba9427408b714ea9a1bbb`, 59
params (`ap_chan_wait_ms` default 0). It is local evidence only, never
committed.

## The parameter

    ap_chan_wait_ms=0    legacy (default, fail closed; byte- and timing-
                         identical to the bench-fired E2 module)
    ap_chan_wait_ms=N    N in 1..1000: on a band-1 AP, after the ap_chan frame,
                         wait until the firmware module channel context
                         (ctx+0x10, worker body channel/band/valid) reports the
                         programmed chandef; timeout aborts BEFORE any AP frame
    ap_chan_wait_ms>1000 refused at use time (-EINVAL), no frame (never guessed)

The patch also corrects the module-context readback addresses (the E2
"VAP object ZERO"/"channel ctx garbage" lines were mis-addressed by
+0x4000/+0x8000 and are not firmware evidence) and adds a read-only
activation-gate pack (module ctx, per-link, band object, both band publish
words) so the next fire settles whether the firmware band-1 context exists at
all.

## Status

Decode, patch, host tests, host build and the RE probes are DONE and
committed. The fire is **not executed by this lane**: `FIRE-RECIPE.md` is the
one-shot rung the Lead runs. PASS = the 5180 observer scan finds
`cudy-ap-test-5g` + page golden; the recipe records the exact dmesg either
way and states what each outcome falsifies.
