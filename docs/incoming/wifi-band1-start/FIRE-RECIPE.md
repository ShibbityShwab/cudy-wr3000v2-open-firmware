# FIRE-RECIPE - the band-1 start arming (R3b extension), task-197

**The Lead fires.** This lane is host-only and authorises **no bench contact**.
Every write in R3b needs its own attended decision and the project preflight
(`tools/fire-preflight.sh`, `tools/recovery-card.sh`); never write CA
0x400392F0, never read the RC misc window 0x10160000..0x1016FFFF, never rmmod
the vendor modules. One boot per ladder; do not re-post a frame that was
already consumed in the same boot.

Address convention: firmware RAM phys = 0x40000000 + CA + 0x6B8000;
device IO phys = CA + 0x3B8000. Read-only helper: O_RDONLY resource0 physical
addressing; keep every read, output under `build/` (gitignored).

## What changed vs the R3b recipe in ../wifi-fw-tbtt-arm

The R3b rung there creates the fresh band-1-bound module context and re-runs
the fixed ladder to key 0x0065. That ladder reaches only CA 0x971D8, whose
G+0x370 hook is NULL, so it **cannot arm 0x4010C220**. This lane adds the
missing frame family: the direct band-start keys **0x0242** (WAL
set_green_ap) and **0x0243**, which call CA 0x4AF0C directly.

## P0/P1 - read-only probe pack (fire nothing yet)

| # | object | CPU | phys | expectation |
| ---: | --- | ---: | ---: | --- |
| P1.1 | G head | 0x1747C4 | 0x4082C7C4 | nonzero |
| P1.2 | **G+0x370 (the NULL hook)** | 0x174B34 | **0x4082CB34** | 0 (both boots, [I]) |
| P1.3 | G+0x374 | 0x174B38 | 0x4082CB38 | 0x00057639 |
| P1.4 | band1 0x4010C220 | 0x4010C220 | **0x404C4220** | **must become 0x1029** |
| P1.5 | band0 0x40108214 | 0x40108214 | 0x404C0214 | 0xF reference |
| P1.6 | band0 0x4010820C | 0x4010820C | 0x404C020C | enable-TRX value |
| P1.7 | gate band0 0x4011080C | 0x4011080C | 0x404C880C | bit0 = 1 |
| P1.8 | gate band1 0x4011480C | 0x4011080C -> 0x4011480C | 0x404CC80C | bit0 = 1 |
| P1.9 | run bits 0x40101220 | 0x40101220 | 0x404B9220 | band0 bit1=1, band1 bit0=1 |
| P1.10 | band1 TRX triple | 0x4010E150/54/58 | 0x404C6150/54/58 | 0x33 / 0xFF / 0xFF |
| P1.11 | band1 MAC reset/status | 0x40114500/51C/520 | 0x404CC500/51C/520 | 51C/520 = 0xFF |
| P1.12 | band0 MAC reset/status | 0x40110500/51C/520 | 0x404C8500/51C/520 | 51C/520 = 0xFF |
| P1.13 | band1 init marker | 0x40114548/54C | 0x404CC548/54C | 0x6101 |
| P1.14 | band0 init marker | 0x40110548/54C | 0x404C8548/54C | 0x0101 |
| P1.15 | band objects | 0x15CA08 / 0x15E570 | 0x40814A08 / 0x40816570 | nonzero |
| P1.16 | band state (+0x14D0) | bandobj+0x14D0 | 0x4081D240 (band1) | state[1] (start marker), state[4] (VAP index), state[0x64] counter |
| P1.17 | published per-link ptr (+0x1A00) | bandobj+0x1A00 | 0x4081D770 (band1) | ptr -> perlink; then read perlink+0x11D (band byte), +0x6C4/6C6 |
| P1.18 | ctx slot table | 0x171458 | 0x40829458 | module context pointers (field B lookup) |

Decision before any write rung: P1.15-P1.18 must be sane (as in R0/R3) and the
2.4 GHz AP must be beaconing. If P1.15-P1.18 differ from R3, stop and report.

## R3b-extension rungs (H2D frames only; **no register pokes**)

Fixed rules: one field B per ladder (the fresh module index N); never re-post a
consumed frame; record P1 reads after every rung.

| rung | frame | field B / body | check after |
| ---: | --- | --- | --- |
| F1 | g0-id1 **0x0064** VAP add (24 B) | field B = 0 (or parent), `body[4]` = N (free module <= 0x12), `body[0x12]` = 1, `body[1]` = 0x12, `body[0]` = 2 | ctx(N)+0x4B8 == 0x15E570 and +0x4B0 != 0 |
| F2 | g0-id1 **0x0242** (len >= 5) | field B = N, `body = {01, 00, 01, 00, 00}` (subcmd 1 = sync start, `body[2]=1` sets the state bit) | 0x4010C220 contains 0x1029; state[1] == 2; state[0x64] incremented |
| F3 | g0-id1 **0x0242** (fallback) | field B = N, `body = {02, 00, 00, 00, 00}` (subcmd 2 = forced start) | same |
| F4 | g0-id1 **0x0243** (fallback) | field B = N, small body | only arms if state[1] == 3; check 0x4010C220 |
| F5 | full R3 re-read + beacon check | - | report |

Interpretation:

| observation after F2/F3 | verdict | next |
| --- | --- | --- |
| 0x4010C220 gains 0x1029 | the band-1 MAC start is armed | re-read P1.9/P1.10, watch the 5 GHz beacon; if it still does not beacons, the remaining gap is TBTT-timer side (R3c/C5e) |
| 0x4010C220 unchanged and state[1] == 2 before the rung | the start marker was already set by an earlier path; a plain start is a no-op by design | report state bytes; consider a per-band state reset only with a separate decision |
| 0x4010C220 unchanged and state[4] == 0xFF | no bound VAP index; the 0x4AF0C guard rejects at 0x4AF32 | re-check F1 bind; if the idx stays 0xFF, the band has no resolvable VAP |
| 0x4010C220 unchanged, no state change | 0x4C524 refused earlier (ctx+0x4B8 NULL, or 0x4C3FC gate/0x961AE lookup failed) | capture the worker's log line if visible; escalate with the full P1 pack |

## Abort rules (any hit -> stop, power-cycle, analyse offline)

* exception mask CA 0x40008 != 0, golden page/state change, nonzero staging
  frame at BAR0 0x6F800C;
* any probe read faults, returns 0xFFFFFFFF unexpectedly, or the read helper
  reports a page/exception anomaly;
* the 2.4 GHz AP is not beaconing on this boot (reference lost);
* a 5 GHz beacon appears (stop; the observation is the result);
* any request to write a register in this pass.

## Deliverables of a fire

* the full P1 matrix (before/after each rung), band-0 and band-1 side by side;
* the F1 bind readback (ctx(N)+0x4B0/+0x4B4/+0x4B8) and the state bytes;
* the F2/F3 result and which guard (if any) refused;
* the 5 GHz beacon observation and the P1.4 final value;
* raw reads stay in `build/`, derived table in the lane dossier.
