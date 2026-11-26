# FIRE-RECIPE - the band-1 beacon-TX gate (R0 read-only, R1 host arm)

**Lane:** wifi-5g-tx-scheduler (task-231). **HOST-ONLY lane: nothing in this
lane touches the device. The Lead fires.** This recipe is the only place the
lane's decode becomes a bench action.

Grades: **[V]** re-derived from the pinned image by
`tools/fw_tx_scheduler_re.py --selftest` (128/128 PASS); **[V\*]**
bench-measured by a committed lane; **[I]** inference; **[U]** needs a read.

---

## 0. The claim this recipe tests [V decode / I cause]

The firmware's per-band beacon-TX gate is two host-visible words:

* **0x40101220** - per-band enable; **bit0 = 5 GHz, bit1 = 2.4 GHz** [V].
* **0x40101208** (= 0x40101220 - 24) - per-band suspend latch;
  5 GHz bits **0x1|0x4**, 2.4 GHz **0x2|0x8** [V bits, I semantics].

The only writers are four firmware-internal SMAC message processors
(CA 0xE6A26 resume-5g, 0xE6A70 suspend-5g, 0xE6AB4 resume-2g, 0xE6B00
suspend-2g) [V], and **no host g0-id1 key reaches them** [V]. If the 5 GHz
band's gate is closed on the bench, every other host-visible register can
still equal the working 2.4 GHz values and the 5200 beacon stays absent -
exactly the observed definitive state. R0 measures it; R1 arms it directly.

**Preconditions (hard):**

1. `tools/bench-link.sh` PASS and `tools/recovery-card.sh` READY; human at the
   bench; the recovery door armed before any run.
2. The current 5 GHz AP bring-up ladder has run to the same end state as the
   2026-11-27 definitive test (2.4 GHz AP beaconing = the control band; the
   5 GHz AP enabled as far as the host can).
3. Snapshot calibration with a verified MANIFEST; per-fire slot/handover
   preflight as usual (`tools/fire-preflight.sh`).
4. One rung per boot. R0 first, always. No rung is combined with another.
5. Reads only in R0. R1 writes exactly two 16-bit words, described below;
   nothing else.

---

## 1. R0 - read-only differential (both bands, same boot) [V probe set]

Run at the end of the normal 5 GHz bring-up ladder, after the last message of
the decoded order has been consumed. Read (16-bit unless noted) and record
under `build/`:

| # | object | CA | BAR0 | phys | why |
| ---: | --- | ---: | ---: | ---: | --- |
| P1 | GATE_EN | 0x40101220 | 0x004B9220 | 0x404B9220 | 5 GHz bit0 / 2.4 GHz bit1 enable |
| P2 | GATE_SUS | 0x40101208 | 0x004B9208 | 0x404B9208 | 5 GHz suspend bits 0x1\|0x4 |
| P3 | band0 object slot | 0x0018081C | 0x0083881C | 0x4083881C | wrapper-selected band objects |
| P4 | band1 object slot | 0x00180830 | 0x00838830 | 0x40838830 | |
| P5 | 2.4G TRX | 0x4010820C / 0x40108214 | 0x004C020C / 0x004C0214 | 0x404C020C / 0x404C0214 | working-band reference |
| P6 | 5G TRX | 0x4010E150 / 0x4010E154 | 0x004C6150 / 0x004C6154 | 0x404C6150 / 0x404C6154 | enable-TRX worker effect |
| P7 | live IRQ table, 2.4G | 0x0017D4EC (47), 0x0017D4F4 (49), 0x0017D4F8 (50), 0x0017D4FC (51), 0x0017D500 (52), 0x0017D578 (82), 0x0017D57C (83), 0x0017D51C (59) | CA+0x6B8000 | CA+0xAB8000 | installed wrappers |
| P8 | live IRQ table, 5G | 0x0017D504 (53), 0x0017D50C (55), 0x0017D510 (56), 0x0017D514 (57), 0x0017D518 (58), 0x0017D580 (84), 0x0017D584 (85), 0x0017D520 (60) | CA+0x6B8000 | CA+0xAB8000 | installed wrappers |

`--probes` prints the full 16-slot list (evidence/probes.txt). Expected
values if the boot registered normally: P7 = odd values 0xAEC95, 0xB54C9,
0xB0D51, 0xAEC69, 0xAEB75, 0xAE48D, 0xB0D51, 0xB0C45 in IRQ order;
P8 = 0xAEC89, 0xB54BD, 0xB0D45, 0xAEC5D, 0xAEB69, 0xAE481, 0xB0D45, 0xB0C39.

### Interpretation matrix

| R0 result | reading | next |
| --- | --- | --- |
| P1 bit1 set (2.4G works) and **P1 bit0 clear** | C1 confirmed: firmware closed the 5 GHz TX enable | **R1** |
| P1 bit0 set but **P2 & 0x5 != 0** | C1 variant: 5 GHz suspend latch set | **R1** (same writes also clear the latch) |
| P1 bit0 set and P2 & 0x5 == 0 | C1 refuted: gate open | **R2**; do not write |
| P8 != the T4 expected wrappers | boot registration differs from the image (unexpected for this firmware) | stop, escalate; do not write |
| P6 != {0x33, 0xFF} while P1/P2 show the 5 GHz gate closed | C2 also closed (TRX) | **R1**, then re-read P6; if still closed see R2b |
| P1/P2 as working 2.4G but no beacon | C3/C4 downstream (band-1 engine/descriptor) | **R2** |

---

## 2. R1 - arm the 5 GHz gate words (host only, 2 writes) [I]

Imitate exactly what CA 0xE6A26 (resume-5g) does, with **16-bit**
read-modify-writes, 5 GHz bits only:

1. read 0x40101220 (16-bit), write back `value | 0x1`;
2. read 0x40101208 (16-bit), write back `value & ~0x5`.

Order matters (handler writes 0x40101220 first, then clears the latches).
Do not touch bit1 or any 0x2/0x8 bit: that is the working 2.4 GHz band.

Then wait at least 2 beacon intervals (VAP+0x10 = interval, default ~100 TU
= ~102 ms) and look for the 5200 beacon with the usual host-side capture,
plus re-read P1/P2/P6.

| R1 outcome | meaning | action |
| --- | --- | --- |
| 5200 beacon appears | C1 was the last layer; the firmware gate was the gap | record the exact writes and the appearance latency; keep the state; this is the minimal arm |
| no beacon, P1/P2 hold the written values | the gate was not sufficient: C3/C4 downstream | revert, go to R2 |
| no beacon, P1/P2 revert on their own | a firmware suspend re-ran (or the MAC cleared it) | record; stop; the gate is not the stable knob |
| 2.4G beacon stops or the box misbehaves | the write hit a shared/hardware side effect (unexpected) | power-cycle; do not repeat; treat C1 as untestable until the stock reference is captured |

**Revert:** restore the pre-read P1/P2 16-bit values. Revert is only needed
when R1 does not produce the beacon.

**Safety:** the two words are in the SMAC enable block; never write any other
bit of them, never touch the forbidden windows (`0x400392F0`, RC misc
0x10160000..0x1016FFFF), no UART writes. If the box stops responding to the
bench link, recover with the armed TFTP door.

---

## 3. R2 - if the gate is already open (C1 refuted)

1. Re-read P3/P4 (band objects) and the per-link beacon state from the prior
   lane (perlink+0x6B8 buffer, +0x6BC translated address, +0x6C0 descriptor,
   +0x6C4 = 16, +0x6C6 = 1) - the staged frame must exist for 5 GHz.
2. Compare the live IRQ wrapper values (P7/P8) with T4: if equal, the ISR
   entry layer is confirmed identical and the failure is in the band-1
   engine/descriptor (C3/C4).
3. **R2b (conditional):** if P6 shows the 5 GHz TRX gate still closed, run the
   already-committed enable-TRX rung from the prior lanes (the CA 0xE8520
   effect: 0x4010E150 = 0x33, 0x4010E154 = 0xFF, 0x4010E158 = 0xFF) instead of
   inventing a new write.
4. If the gates are all open and the beacon is still absent, the remaining
   difference is the firmware per-band bring-up (C3): capture the stock 5 GHz
   boot and diff P1/P2/P5/P6 at the moment the stock beacon appears. If stock
   also runs with the 5 GHz gate open, the gate model is refuted for stock and
   C1 is retired.

## 4. R3 - stock reference capture (the never-fired rung)

Before any further host-side invention: capture the stock 5 GHz boot (stock
Cudy firmware, same firmware image) with the same P1/P2/P5/P6 read points at
(a) before the AP starts, (b) after the stock 5 GHz AP is beaconing. This
single capture turns the gate model into a fact or retires it, and it costs no
write on the research unit. It is the recommended rung if R0 is ambiguous.

---

## 5. What this recipe deliberately does not do

* No firmware patch, no host-driver patch, no new H2D key invention: the gate
  words are the firmware's own writers' effect, written directly.
* No writes in R0; no writes outside the two words in R1.
* No claim that R1 reproduces the stock sequence: R1 is a minimal host
  *emulation* of the internal resume message [I]; it is a discriminator, not a
  supported operating mode.
