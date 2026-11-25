# FIRE-RECIPE - corrected chain probe + the H2D 0x01F7 RF-mode arm

**Lane:** wifi-fw-rf-descriptor (task-189). **HOST-ONLY** decode; the Lead
fires. This rung is read-only until F1; F1 is a small host-driver patch, not a
firmware rebuild.

Companions: [REPORT.md](REPORT.md), [TABLES.md](TABLES.md).

## 0. What changed vs the committed rf-programme recipe

* The chain is `perlink = u32(ctx+0x4B0); P = u32(perlink+0xE20);
  O = u32(P+0x1A00); R = u32(O+0x2D8)`. The old probe read
  `perlink+0x2820` (0x178238) as if +0xE20 were inline; that address is not
  part of the chain.
* The gate that stays 0 is the RF **mode byte** `P+0xF04`
  (`bandobj+0xF04` = CPU 0x15F474). Its only image writer is the g0-id1
  config-event worker for **H2D key 0x01F7**.
* Our ladder has no 0x01F7. The stock host's only emitter is the WAL command
  `set_country_for_dfs` (`hmac_config_set_country_for_dfs` in wifi.ko).
* The publish word `bandobj+0x1A00` (O) is written by the boot task at
  CA 0x48A68 and is already non-zero on our boot [V*]; O is not the blocker.

## 1. F0 - READ-ONLY chain probe (no fire)

Run on the parked/running CW-0 or CW-2 AP using the existing read-only path
(`bar0read`; phys = 0x40000000 + CPU + 0x6B8000). 17 reads, no writes.
Step 1 resolves ctx0, then reads 4/5; every later address is derived from that
readback (if a pointer differs, re-derive before the next step).

| step | read | CPU | expected |
| ---: | --- | ---: | --- |
| 1 | module-table slot0 u32 | 0x171458 | 0x00119758 |
| 1 | module-table base u16 | 0x172504 | 0 -> ctx0 = 0x119758 |
| 2 | u32(ctx0+0x4B0) | 0x119C08 | 0x00175A18 |
| 2 | u32(ctx0+0x4B8) | 0x119C10 | 0x0015E570 |
| 3 | **u32(perlink+0xE20) = P** | **0x176838** | **0x0015E570 (band object)** |
| 4 | u32(P+0x1A00) = O | 0x15FF70 | 0x00175A18 |
| 5 | **u8(P+0xF04) = mode** | **0x15F474** | **0 (before) / 3 (after F1)** |
| 6 | u8(P+0xF87) | 0x15F4F7 | 0 / 1 by channel range |
| 7 | u8(P+0xD70) attach flag | 0x15F3E0 | 1 |
| 8 | u32(perlink+0x2D8) = R | 0x175CF0 | nonzero |
| 9 | u32(R+0x3A8) | R+0x3A8 | 0x01800060 if the image ever ran |
| 9 | u32(R+0x3AC) | R+0x3AC | 0x03E6138A (ch36-64) / 0x056B0597 (ch100+) |
| 9 | u32(R+0x410) | R+0x410 | bit0 = predicate; read the channel-table row first |
| 10 | channel-table row u32 | 0x117AD4+4*idx | byte1 = slot (0xFF = no descriptor) |
| 10 | pool record u32 (if slot != 0xFF) | 0x1ADC98+16*slot+8 | bit23 = valid |

Decision:

| readback | verdict | next |
| --- | --- | --- |
| P == 0 | the per-link->band pointer is not established on this boot; no image writer exists, no H2D re-post can fix it | report; F1 is pointless |
| P != 0, P != u32(ctx+0x4B8) | P is not the band object we expect | capture P and its +0x1A00; re-open the decode |
| mode == 0 and R+0x3A8 == 0 | the RF block was never armed | **F1** |
| mode == 3 and R+0x3AC == 0 | mode armed but the 0x0124 programme never ran | re-post 0x0124 (F2) then re-read |
| mode == 3 and R+0x3AC == 0x03E6138A | programme completed (ch36-64 image) | go to the beacon-engine candidates (C4/C5 of rf-programme lane) |
| mode == 3 and R+0x3AC == 0x056B0597 on ch40 | stale/wrong variant | F2 re-post, then re-read |

Abort rules: any page/exception anomaly -> power-cycle, no retry; never read the
RC misc window 0x10160000..0x1016ffff; never write 0x400392f0; the whole probe
is read-only.

## 2. F1 - send the missing H2D config event key 0x01F7 with mode 3

Ship a small host patch that, after the 0xF7 descriptor-table arm and before
(or immediately around) the 0x0124 channel start, emits the stock's RF-mode
config event:

~~~
g0-id1 config event: key 0x01F7, len 1, body { 0x03 }
frame: 12 B header | 8 B prologue (msg id 1, tag 0x5A5A) |
       u16 key at +20 | u16 len at +22 | body at +24 | 4 B inherited tail
~~~

Firmware side (fully decoded, TABLES T5): worker CA 0xB5B9C reads body[0] as
the mode (1..5 via `tbb`), requires `u8(bandobj+0xD70)==1` and a published
per-link at `bandobj+0x1A00`, then writes the mode byte `bandobj+0xF04` and
the whole RF register image `R+0x3A8..R+0x418` for that mode. CA 0xA2FA2 (the
0x0124 path) requires mode == 3.

Ordering [I]: send 0x01F7 (mode 3) **before** the 0x0124 channel frame, so the
programme's mode gate is satisfied when the channel worker runs; a re-post of
0x0124 after it is the safe belt-and-braces (F2).

Then re-read F0 rows 5-9: expect mode == 3, R+0x3A8 == 0x01800060 and
R+0x3AC == 0x03E6138A (ch40). Success criterion for this rung: the ch36-64 RF
image appears in R and a 5 GHz beacon/assoc becomes possible; the RF programme
is then proven run, and any remaining silence is beacon-engine work.

## 3. F1b - mode enumeration (only if F1 with mode 3 fails)

If mode 3 is stored (+0xF04 == 3) but `R+0x3AC` still reads 0, the 0x01F7
worker ran but its later path errored; capture all of F0 and the console log ids
(1442 published-per-link missing, 1448 attach flag, 2999 P/O missing on the
0x0124 path). Only then repeat F1 with modes 1,2,4,5 and compare the R image
words; never loop blindly and never write any firmware RAM value by hand.

## 4. F2 - re-post the 0x0124 channel frame after the arm

Reuse the committed wifi-fw-rf-programme R1/R2 rung: once the mode byte reads 3
(F1), re-send the existing 0x0124 ch40 body (no rebuild) and re-read
`R+0x3AC`/`R+0x3B4`/`R+0x410`. If R shows 0x03E6138A/0x026F138A and bit0
reflects the predicate, the RF programme has run; capture the predicate bit
with the channel-table slot read first (0xFF = descriptor subsystem inactive,
bit0 not meaningful).

## 5. Abort / safety rules (unchanged from the committed lanes)

* page 0x86807fef / state 1 / mask 0 exception -> power-cycle, no retry;
* never write CA 0x400392f0; never read 0x10160000..0x1016ffff;
* the whole F0 pack is read-only; F1 only adds one H2D frame, no firmware/
  artifact writes; no vendor binaries or firmware bytes enter the repo;
* keep the page golden and the recovery door armed per RUNBOOK.md; the Lead
  fires, this lane does not.

## 6. What this rung settles

1. whether P/O/R are live on our boot (F0) - the question the old probe could
   not answer;
2. whether the silent skip is the missing 0x01F7 mode arm (F1);
3. if the programme runs, the remaining 5 GHz gap moves to the beacon/TBTT
   engine candidates (C4/C5), which need none of this chain.
