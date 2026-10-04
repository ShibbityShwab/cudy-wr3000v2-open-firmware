# VIEWPORT: the GIC region stays host-INVISIBLE through a spare inbound viewport (phase 49, 2026-10-04)

Task 13 of the wifi-forward plan: program a spare inbound viewport at the GIC base and read it from the
host, with an in-range positive control in the same run. The module is a one-shot (`iatuview.ko`, built by
CI run 37218448504), loaded with NO params. Evidence `build/register-dumps/exp/20261004-171523/`; the
first attempt's dir `build/register-dumps/exp/20261004-171302/` is kept for the hook error (below).

## The measurement

`module-log.txt` (raw register lines):

```
iatuview: P3 pre  named idx0-5 CTRL2: 0x00000000 x6
iatuview: P3 post named idx0-5 CTRL2: 0x00000000 x6
iatuview: P3 six named viewports idx0-5 unchanged=YES
iatuview: STEP5 readback A idx6 mismatch=0, B idx7 mismatch=0, total=0
iatuview: STEP5 OK - all 14 readbacks match the spec tables
iatuview: STEP6 B pre-doorbell  raw[0x2e4]=0x00000000 mask[0x2e8]=0x000003ff status[0x2ec]=0x00000000
iatuview: STEP7 doorbell out[2] CA 0x400392d4 [B+0x2d4] <= 0x00000001 (ringed once)
iatuview: STEP8 B post-doorbell raw[0x2e4]=0x00000001 mask[0x2e8]=0x000003ff status[0x2ec]=0x00000000
iatuview: WINDOW A idx6 base=0x40500000 limit=0x4050ffff target=0x40160000 read=0xffffffff class=no-decode
iatuview: WINDOW B idx7 base=0x40510000 limit=0x40510fff target=0x40039000 raw_pre=0x00000000 raw_post=0x00000001 bit0=1 class=decode
iatuview: RESULT PASS verdict=expected-negative A=no-decode B=decode (GATE G5 default branch)
```

`build/register-dumps/exp/20261004-171523/module-log.txt`.

- **Window A (idx6, target CA `0x40160000` = the GIC block)** reads `0xffffffff` at `0x40500000`, and all
  five A reads (`0x40500000`, `0x4050010c`, `0x40501100`, `0x40501108`, `0x40501800`) are `0xffffffff`.
  No-decode.
- **Window B (idx7, target CA `0x40039000` = the ctrl-rb, in-range positive control)** decodes: raw status
  `0x400392e4` rises bit 0 (`0x00000000 -> 0x00000001`) across the single doorbell write, so both the
  programming and the host route work. B's post-mask status `0x400392ec` reads `0x00000000` (bit 0 masked
  because `mask[0x2e8] bit0 = 0`), recorded as-is.
- **14/14 iATU readbacks match** (A idx6 and B idx7), so the base/limit/target programming is exactly the
  spec's; the parameter values in the WINDOW lines are the measurement.
- **P3 unchanged=YES**: the six named viewports idx0-5 read `0x00000000` before AND after, i.e. the spares
  (idx6/idx7) were used, not the named ones.

## Why this is a statement about the device, not the harness

`interp.txt` names the spec's row (spec section 5): A reads `0xffffffff` while B reads a non-`0xffffffff`
value with bit 0 = 1 in the SAME run. The positive control is what makes A's no-decode interpretable - if B
had also read `0xffffffff`, the programming itself would be suspect (a defect, not a GIC verdict). B
decoded, so the spare-viewport iATU programming and the host-side route both work, and CA `0x40160000` still
does not decode on the PCIe inbound path. This matches phase 47's static claim that the GIC/distributor is
PCIe-unreachable from the host (its `THE-LAST-LINK.md` section 4 cites phase 34's spare viewport at
`0x40160000` returning `0xffffffff`).

## Verdict class and GATE G5

`RESULT PASS verdict=expected-negative (NOT a GIC-visible result)` -> **GATE G5 default branch**: the GIC
block stays host-invisible; the parameters (base/limit/target readbacks of A and B) are the measurement,
and the lead picks the next branch from `THE-LAST-LINK.md` section 3's table. `acceptance.txt` ends with
`TOTAL FAILURES: 0`.

## The hook blemish (recorded, superseded)

Attempt 1 (`build/register-dumps/exp/20261004-171302/`) reported `EXP RESULT: FAIL` because the OPTIONAL
`EXP_CAPTURE_CMD` hook was rejected by the device's ash (`ash: syntax error: unexpected "("`; the hook's
`echo` strings contained parentheses and exp.sh wraps the hook in `{ ... ; }`). `capture-cmd.txt` there is
0 bytes. The module result was identical to this clean run (NOTA: `NOTE-attempt1-hook-error.txt`). Named
host-side harness error -> one clean re-run (`20261004-171523`, `EXP RESULT: PASS`).

## Provenance and health

`ARTIFACT-PROVENANCE.txt`: CI run 37218448504 success, artifact `iatuview-ko`, `iatuview.ko` md5
`805bdb207cebccf838bbb8df7044771f`, vermagic `5.10.201 SMP mod_unload ARMv7`; exactly ONE module param
(`domain`, default 0); spec `opensource/docs/phase49/spare-viewport-spec.md` sha256
`adbb2bb81fa5825c385a469c4b93f0c6b0497811969b019bd5a84b109ffc1fc1`. `health.txt`: `WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0`; `run-task13-clean.log` ends `EXP RESULT: PASS`.
