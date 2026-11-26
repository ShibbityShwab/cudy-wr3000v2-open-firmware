# FIRE-RECIPE - the producer attribution pass (read-only, one attended boot)

**Lane:** wifi-rx-smac-complete (task-221). This lane is HOST-ONLY; the Lead fires. Everything
below is **read-only**; no writes, no flash, no new frames beyond the already-armed R5 AP ladder
plus the client's normal auth attempt. The fire gate and recovery door are the Lead's usual ones
(`tools/fire-preflight.sh`, `tools/recovery-card.sh`, UART capture read-only, human at bench).

## R0 - purpose

Decide, in one boot, which link of the F1 chain is the break:

1. does the **producer ISR** (0xB53A4) run at all? -> new counters P5/P5b + ack P6;
2. does the **consumer** (0xBBC9C -> 0xB9780) run? -> existing P10 counters + g1 id13 (P0);
3. is the RX source **delivered** at the SMAC/INTC level? -> P4/P7 vs P5, with P8/P9 as the only
   host-visible MAC-mask view.

## R1 - baseline (no client burst yet)

1. Confirm the current image/boot per the normal session brief; recovery door READY; UART capture
   (read-only) to an absolute path under `build/uart-logs/`.
2. Run the existing AP ladder exactly as in the last F1 run (0x0127 -> 0x0065 -> 0x0242 subcmd 1,
   per the committed rungs) so the comparison to the previous F1 boot is apples-to-apples.
3. **P0**: snapshot the D2H catalogue (`cat rd_catalog`): ready pair + HEARTBEAT must be there,
   g1 id13 absent. Note any group-2 id-3 frames.
4. **P1-P11** once ("pass A"), in the order P1 -> P2/P3 -> P4/P6 (follow the P2/P3 base) -> P5/P5b
   -> P7 -> P8/P9 -> P10 -> P11. Record raw values; do not interpret P4-P11 if P1 is not
   0x174BF0/0x175A18.

## R2 - differential (client auth attempt, the same laptop flow as the last F1 run)

5. Trigger the client's normal auth/association attempt (no monitor-mode injection needed; the
   F1 was reproduced with a plain client auth burst). If a UART capture is running, the firmware
   log lines 0xA4E / 0x49F / 0x497 / 0x2a1 / 0x257 / 0x275 are the consumer/producer failure
   signatures.
6. **P0** + **P4-P11** again ("pass B"), same order, immediately after the burst.
7. Re-read P5/P5b once more after ~2 s to catch a late ISR run.

## R3 - decision (from the pre/post deltas)

| # | observation | verdict | next rung |
| ---: | --- | --- | --- |
| 1 | P5++ and P6 changed | **producer ran** (this lane's question answered: the SMAC PPDU completion DOES reach the firmware) | if P10 moved too: the break is the CRX D2H delivery/host decode; if P10 did not: consumer gate (0xA4E RX-ctx NULL / 0x49F forward flag / 0x497 cap) - capture logs, escalate to the consumer rung |
| 2 | P5++ but P6 unchanged | handler entered, returned between counter and ack (dequeue/alloc failure) | firmware logs 0x2a1/0x257/0x275; P11 list state; escalate to the smac-start/ppdu-init attach rung |
| 3 | P5 0, P7 moved | RX source specifically not delivered -> gate G1 (P4 bit2) or the MAC mask bit (P8/P9) | if P4 bit2 is stuck SET: the SMAC status/context feed is the blocker (G1) -> smac-start attach rung; if P4 bit2 moves but P5 never does: MAC-mask-bit hypothesis is the best remaining cause -> firmware enable/mask decision lane (a patch, since the host cannot write the MAC mask) |
| 4 | P5 0, P7 0 | SMAC IRQ delivery dead on this boot (per-band context / MAC start / INTC) | re-run the AP ladder, re-read P1-P3; if P2/P3 stay boot defaults, smac-start attach rung |
| 5 | P0 shows a group-2 id-3 D2H frame | the producer's message is host-visible; direction question answered | route to the hcc/D2H lane (consumer 0xBBC9C is then not this producer's consumer) |
| 6 | page / mask anomaly | firmware fault | power-cycle, recovery door, UART capture read-only; do not fire |

## R4 - escalation (only if a rung above is exhausted)

* **E1 (G0/G1b, 5 GHz chain):** the band-1 SMAC context attach is still the open item from
  `wifi-smac-start`; its R7 probe pack is the next read. This lane's P2/P3 are the cheap
  same-pass check.
* **E2 (G0, 2.4 GHz):** if the MAC mask words (P8) equal the expected 0xF / 0x1029 and the ISR
  still never runs, the only remaining options are firmware-side (patch the enable/status path)
  - that is a separate lane with its own image/patch discipline; **this lane does not propose a
  write**.
* **E3 (consumer side, G4):** the existing `wifi-rx-mac-delivery` fire recipe (R2/R3) is the
  consumer rung; this pack's P10 is the shared hand-off.
* Never: write CA 0x400392F0, read the RC misc window, or add a speculative H2D frame for this
  question - the producer is firmware-internal and no host message gates it (the stock H2D
  capture has no RX-enable/TRX message; committed f1-from-capture negative).

## Safety summary

* Read-only loads only; no intended writes; abort on any read error instead of retrying.
* Keep the recovery door armed; UART capture read-only; one boot per tuple.
* No flash, no image #3 involvement, no vendor bytes in the repo (this lane is decode-only).
