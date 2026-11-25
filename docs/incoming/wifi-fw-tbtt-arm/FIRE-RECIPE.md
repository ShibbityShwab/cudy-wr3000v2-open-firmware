# FIRE-RECIPE - the 5 GHz TBTT arm: R3 read-only differential (task-193)

**The Lead fires.** This lane is host-only and authorises **no write**: R3 is a
pure read pass. Every write in a future rung needs its own attended decision and
the project preflight (tools/fire-preflight.sh, tools/recovery-card.sh); never
write CA 0x400392F0, never read the RC misc window 0x10160000..0x1016FFFF,
never rmmod the vendor modules. One boot per ladder; do not re-post a frame
that was already consumed in the same boot.

Address convention: firmware RAM phys = 0x40000000 + (CPU + 0x6B8000);
device IO phys = CPU + 0x3B8000. Read with the committed read-only helper
(O_RDONLY on resource0 with physical addressing). Keep every read; they are
the evidence. Probe output stays under build/ (gitignored).

## R0 already done (context, no re-fire needed)

The Lead's R0 pass (2026-11-28) established: band-1 per-link +0x6C4 = 16,
+0x6C6 = 1, beacon buffer pointer nonzero; band-1 MAC block 0x548/0x54C =
0x6101; G = nonzero, G+0x374 = 0x00057639, G+0x370 = 0. C1/C2/C3 are closed by
that. **Do not re-read those unless R3 needs them as the band-0 reference.**

## R3 - read-only differential pass (band 0 = working reference, band 1 = silent)

~120 dword reads, **no writes**, one boot. The R0 boot already proves the
2.4 GHz AP is up; if it is not, R3 loses its reference - stop and re-rank.

### R3.1 per-band run/enable matrix (the new, decisive part)

Read both bands' registers at the same offsets and write them into a table:

| # | object | CPU | phys | band-0 expected (reference) | band-1 expected if healthy |
| ---: | --- | ---: | ---: | --- | --- |
| 1 | MAC-start bit | 0x4011080C / 0x4011480C | 0x404C880C / 0x404CC80C | bit0 = 1 | bit0 = 1 |
| 2 | per-band run bits | 0x40101220 | 0x404B9220 | bit1 = 1 | **bit0 = 1** |
| 3 | beacon/TRX gate bits | 0x40101208 | 0x404B9208 | mask A | **mask == mask A** |
| 4 | SMAC IRQ enable (band1) | 0x4010C220 | 0x404C4220 | - | contains 0x1029 |
| 5 | SMAC IRQ status | 0x4010C200 | 0x404C4200 | idle/pending bits | same shape |
| 6 | band0 enable | 0x40108214 | 0x404C0214 | bits 0..3 = 0xF | - |
| 7 | band0 TRX | 0x4010820C | 0x404C020C | enable-TRX value | - |
| 8 | **band1 TRX triple** | 0x4010E150 / 0x4010E154 / 0x4010E158 | 0x404C6150 / 54 / 58 | - | **0x33 / 0xFF / 0xFF** |
| 9 | band-block reset/status | 0x40110500/51C/520 and 0x40114500/51C/520 | 0x404C85xx / 0x404CC5xx | reset done (51C/520 = 0xFF) | same |
| 10 | band-block init words | 0x40110548/54C/588/594/5BC and 0x40114548/54C/588/594/5AC/5BC | 0x404C85xx / 0x404CC5xx | 0x0101 class / 0x33/22 | 0x6101 / 0x35/24 |
| 11 | SoC INTC | 0x40002264/68/6C | 0x403BA264/68/6C | band TRX path | same |

Interpretation is in the decision table below; write the raw values down
before interpreting (this is a differential, not a name hunt).

### R3.2 beacon-state objects and the 92-byte descriptor diff

Resolve the two-step pointers (project convention):

~~~
band_b obj    = 0x15CA08 + 0x1B68*band      (band0 0x15CA08, band1 0x15E570)
perlink[b]    = u32(band_b obj + 0x1A00)    (phys = 0x40000000 + CA + 0x6B8000)
beacon state  = perlink[b] + 0x6B8 (ptr 280B), +0x6BC (ptr), +0x6C0 (ptr 92B),
                +0x6C4 (u16), +0x6C6 (u8), +0x11D (u8 band index)
~~~

Read for **both bands**: the six fields, then **dump 92 bytes at
*(perlink[b]+0x6C0)** and the **first 32 bytes at *(perlink[b]+0x6B8)**.
Also read the VAP pointer for both (ctx[b]+0x190; ctx slot table
0x171458+4k, bias 0x172504) and the VAP flags VAP+0x58 (bit2 = band != 0),
VAP+0x34C/+0x360/+0x10/+0x14/+0x138/+0x13A - the C4 hints, one read each.

### R3 decision table

| observation | verdict | next |
| --- | --- | --- |
| 0x40101220 bit0 == 0, or 0x4011480C bit0 == 0, while band0's are 1 | **C5a**: the per-band MAC start never ran for band 1 (or a stop cleared it) - the beacon engine was never enabled | R3b (fresh-context re-start) |
| 0x40101220 bit0 == 1 and 0x4011480C bit0 == 1, but 0x4010E150 != 0x33 / 0x4010E154 != 0xFF / 0x4010E158 != 0xFF | **C5b**: the band-1 TRX-enable worker (CA 0xE8520) never ran for band 1 | R3c (report; no host key reaches 0xE8520) |
| 0x4010C220 does not contain 0x1029 on band 1 | **C5a-IRQ**: the band-1 SMAC IRQ enable is not armed - the TBTT ISR cannot run even if the timer fires | R3b |
| all enable bits look like band 0's, but the band-1 92-byte descriptor differs from band 0's | **C5c**: the beacon descriptor was not staged/queued on band 1 although the state object exists | report with both dumps; escalate (path is firmware-internal) |
| all enable bits look like band 0's, 0x40101208 masks differ between bands | **C5d**: a beacon-TX/TBX gate is left set on band 1 | report; do not poke 0x40101208 blind |
| everything matches band 0 and the descriptor shapes match, still no beacon | **C5e**: the missing arm is the firmware-internal TBTT timer/ISR (no MMIO or H2D path in the image) | capture the full pack; next lane = stock differential or firmware trace; do not guess a register |
| 5 GHz beacon appears during/after the pass | stop; the observation is the result | document immediately |

## R3b - conditional: fresh-context band-1 re-start (writes; only if C5a/C5a-IRQ)

Only if R3 shows the band-1 MAC/IRQ enable was never armed. A plain re-post is
a **no-op by design**: (a) the VAP-start guard skips the beacon allocation when
perlink+0x6C6 == 1, and (b) the MAC-start function early-outs when its request
byte is already 2. So the rung must create a **fresh band-1-bound module
context** (new VAP / field B) and drive the full ladder in the fixed order:

| rung | frame | field B / body | check after |
| ---: | --- | --- | --- |
| 1 | g0-id1 0x0064 VAP add | new module index in body[4], **body[0x12] = 1 (band1)** | ctx+0x4B8 == 0x15E570 |
| 2 | g0-id1 0x0124 channel | band 1, ch36 | RF chain stays mode 3 |
| 3 | g0-id1 0x01F7 | len 1, body {0x03} | P+0xF04 == 3, R image live |
| 4 | g0-id1 0x0124 channel | again | R+0x3A8/0x3AC image written |
| 5 | g0-id1 0x0127 AP config | 156 B, **5 GHz HT/VHT hints at body+0x8A..+0x8D** | VAP+0x10/0x14/0x138/0x13A staged |
| 6 | g0-id1 0x0065 VAP start | 12 B | new perlink+0x6C6 == 1; then re-read R3.1 |

Keep field B identical across rungs 1-6 (the F1 failure class was a field-B
mismatch). After rung 6 re-run the R3.1 reads for the new ctx: if 0x40101220
bit0 / 0x4011480C bit0 / 0x4010E150-58 become healthy, the silent 5 GHz
beacon was a start-order/enable problem, not hardware.

## R3c - conditional: missing band-1 TRX triple (no write rung)

If R3 shows 0x4010E150/54/58 are not 0x33/0xFF/0xFF, the enable-TRX worker was
never run for band 1. **No g0-id1 key in table A/B reaches CA 0xE8520** (this
lane's check), so there is no known host frame to post; do not invent one and
do not poke the registers blind. Report with the evidence and open a lane for
the WAL group-2 / vendor-boot differential.

## Abort rules (any hit -> stop, power-cycle, analyse offline)

* exception mask CA 0x40008 != 0, golden page/state change, nonzero staging
  frame at BAR0 0x6F800C;
* any probe read faults, returns 0xFFFFFFFF unexpectedly, or the read helper
  reports a page/exception anomaly;
* the 2.4 GHz AP is not beaconing on this boot (reference lost);
* a 5 GHz beacon appears (stop; the observation is the result);
* any request to write a register in this pass.

## Deliverables of a fire

* the full R3.1 matrix (band0 and band1 raw values side by side),
* both per-link beacon-state field sets and both 92-byte descriptor dumps,
* both VAP flag sets (C4 data),
* the exact frames of any R3b rung (field B, body[0x12]/[0x4]), and the
  post-rung R3.1 re-read,
* a new mem-ledger entry with the verdict (C5a / C5b / C5c / C5d / C5e).
