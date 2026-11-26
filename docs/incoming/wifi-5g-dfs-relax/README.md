# wifi-5g-dfs-relax - the +0x4A0 pre-gate relax (the 0x07E7 ARM reaches the firmware)

**Lane:** shared task **task-230**, 2026-11-26. **HOST-ONLY**: host build +
host tests + fire recipe; no device contact, no fire, no flash. Deliverables
only under `opensource/docs/incoming/wifi-5g-dfs-relax/`.

## The finding (input lane `../wifi-5g-cac-context`, task-229, commit 0c76fd8)

The firmware 0x07E7 ARM worker (CA 0xB58FC) writes the six band-1 SMAC sub-base
bit0 registers (CA 0x4010E000/+4/+8/+A0/+A4/+A8) **FIRST**; then, if
`u32[mctx+0x4A0] != 0`, it returns 0 - **silent success**, skipping only
CA 0xF593C/CA 0x5F450 and the +0x4A5 CAC flag (the branch at 0xB5B0C ->
0xB590C). CA 0x5F450, called from the 0x0065 VAP-start worker, is the writer of
+0x49C/+0x4A0/+0x4A4: a normal 5 GHz AP-start ladder therefore has
`+0x4A0 != 0` and the ARM is the **register-only** arm.

Our driver's dfs_set pre-gate refused on exactly that word (`-EBUSY`), so the
R2 ARM frame never reached the firmware. **The fix is on the host.**

## What this lane changes (3 files, no frame byte, no new parameter)

| file | change |
| --- | --- |
| **hi5622_dfs_set.c/.h** | the ARM/ARM_CLEAR `+0x4A0` pre-gate refusal is removed (the other gates stay fail-closed: mctx/helper/band/state/band-up/CAC-flag); precheck records `ctx_present = (b4a0 != 0)`; the postcheck takes the probe and grades `regs_pattern == 0x2D` + CAC flag 1 (full arm) / 0 (register-only arm); new `hi5622_dfs_set_arm_kind()` ("register-only"/"full"); the probe carries `b49c/b4a4/b4a8` |
| **hi5622_scan.c** | probe reads the four CAC words; `dfs_set pre:` prints them + `ctx_present=`; a new `dfs_ctx pre0x0065:` read-only line before the 0x0065 post; `dfs_set arm post:` prints `ctx_present=`/`arm_kind=` and the register-only PASS; the ARM_CLEAR pre-clear refusal records `ctx_present` (still fail-closed) |

CLEAR (dfs_set=3) and the CLEAR half of ARM_CLEAR keep the existing
fail-closed gates: with a present CAC context the ARM never sets the flag, so
ARM_CLEAR refuses the CLEAR by design (use `dfs_set=1` on the A-rung).

## Result (host-only; the .ko is LOCAL EVIDENCE, never committed)

`/tmp/dfs-relax-artifact/hi5622_wifi.ko` - 392,868 B stripped,
sha256 `6c50d796...`, **66 params**, .text `5653b024...`, built on the
`wifi-5g-dfs-order` base (`a5ec150c`, order `0x0065 -> 0x07E7 -> 0x01F7 ->
0x0242` unchanged).

Host tests: **101/101** C contract selftest, **55/55** independent Python
RELAX test, **46/46** sibling ORDER-test regression.

## Files

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the defect, the two arm kinds, the host change spec mapping, the test coverage, limits |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the A-rung (0x0065 -> ARM -> E1-E8), the C-BASE/C-NOG0 controls, PASS/FAIL/ABORT |
| [BUILD-NOTES.md](BUILD-NOTES.md) | base/patch/artifact pins, the three-file change, reproduce |
| [patches/0001-dfs-relax.patch](patches/0001-dfs-relax.patch) | the source patch (`a7e1a286...`) |
| [tools/build-dfs-relax.sh](tools/build-dfs-relax.sh) | the host build + tests + pins |
| [tools/dfs_relax_test.py](tools/dfs_relax_test.py) | the independent Python RELAX test (55 checks) |
| [src/dfs_set_selftest.c](src/dfs_set_selftest.c) | the extended C contract selftest (101 checks) |
| [evidence/](evidence/) | build.txt, the three host-test logs, pins.txt |

## Reproduce

    opensource/docs/incoming/wifi-5g-dfs-relax/tools/build-dfs-relax.sh

Outputs: `/tmp/dfs-relax-build` (patched tree), `/tmp/dfs-relax-artifact`
(module + test logs). The base tree is rebuilt by
`../wifi-5g-dfs-order/tools/build-dfs-order.sh` when absent.

## Grades / limits

[V] = decoded by `../wifi-5g-cac-context` (static firmware decode, 70/70);
[B] = bench (the R2 refusal, the mctx words). This lane adds [I] host logic
only. **No bench contact**: the A-rung is unverified; the Lead fires it. No
vendor binaries, firmware bytes or secrets in this lane. The module is local
evidence only and must never be committed.
