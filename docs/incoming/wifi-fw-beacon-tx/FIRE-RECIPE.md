# FIRE-RECIPE - the 5 GHz beacon TX path (task-192)

**The Lead fires.** This lane is host-only: nothing here was run on the device.
All steps are **read-only until R2**; every write is an H2D config event that
a committed lane has already fired successfully (../wifi-vap-start,
../wifi-ap-start, ../wifi-fw-dfs-worker, ../wifi-fw-rf-descriptor).
Never write CA 0x400392F0, never read the RC misc window 0x10160000..0x1016FFFF,
never rmmod the vendor modules. One boot per ladder; do not re-send a frame
that was already consumed in the same boot.

Conventions: firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO
phys = 0x40000000 + 0x3B8000 + (CA - 0x40000000). Read with the committed
read-only helper (O_RDONLY on /sys/bus/pci/devices/0000:00:00.0/resource0,
phys addressed). Keep every read; they are the evidence.

## R0 - read-only discriminator pass (no writes)

Goal: decide C1 (beacon state missing) vs C2/C3/C5 (state exists, engine not
armed). 40 reads, no writes, one boot.

1. **Context scan** (this is the decisive part):
   ```
   slot_ptr[k] = u32(0x171458 + 4*k),  k = 0..18
   ctx[k]      = slot_ptr[k] + u16(0x172504)
   read ctx[k]+0x4B0 (per-link), +0x4B4 (bank), +0x4B8 (band object), +0x190 (VAP)
   ```
   A context that was never created reads as zero for all four; a context bound
   to band0 has +0x4B8 = 0x15CA08, bound to band1 has +0x4B8 = 0x15E570.
   Bench reference: k=0 is 0x119758, +0x4B0 = 0x00175A18,
   +0x4B8 = 0x0015E570 (band1) [V*].
2. **Beacon state per band**:
   ```
   perlink[b]  = u32(0x15CA08 + 0x1B68*b + 0x1A00)     ; b = 0,1
   read perlink[b]+0x6B8 (ptr), +0x6BC (ptr), +0x6C0 (ptr),
        perlink[b]+0x6C4 (u16), +0x6C6 (u8)
   ```
   Expected after a successful VAP start on that band: +0x6C4 = 16,
   +0x6C6 = 1, three nonzero pointers. **This one byte (+0x6C6 on band1) is
   the primary discriminator.**
3. **Hooks**: u32(0x1747C4), u32(0x174B34), u32(0x174B38) (G head, G+0x370,
   G+0x374). All nonzero = the band-start hooks are installed.
4. **RF/AP state** (cross-checks): ctx0+0x190 VAP, VAP+0x10/+0x14/+0x138/
   +0x13A/+0x34C/+0x360; perlink[b]+0x11D band byte; perlink[b]+0xE20 P;
   P+0xF04 mode (3 after 0x01F7) and P+0xF87 (0 ch36-64); R = u32(P+0x1A00
   +0x2D8); R+0x3A8 = 0x01800060, R+0x3AC = 0x03E6138A for ch36-64.
5. **Device IO state, band1 vs band0** (same offsets in both blocks):
   ```
   band0: 0x40110500 0x4011051C 0x40110520 0x40110548 0x4011054C
          0x40110550 0x40110554 0x4011055C 0x40110560 0x40110564
          0x40110568 0x40110588 0x40110594 0x401105BC
   band1: same offsets on 0x40114000
   global: 0x4010C200 0x4010C220 0x40101220
           0x4010E150 0x4010E154 0x4010820C 0x40108214
           0x40002264 0x40002268 0x4000226C
   ```
   Reference expectations when band1 is initialised: 0x401145BC = 0 (band-id
   write of the band1 path), 0x40114548 = 0x6101, 0x4011454C = 0x6101,
   0x40114588 = 0x35, 0x40114594 = 0x24, 0x40101220 = 1 (band1) / 2 (band0).

### R0 interpretation

| observation | verdict | next |
| --- | --- | --- |
| band1 perlink+0x6C6 == 0 / pointers 0 | **C1**: the 5 GHz VAP start never ran on a band1-bound ctx (or used the wrong field B) | R1 |
| band1 perlink+0x6C6 == 1, pointers nonzero, G+0x374 == 0 | **C3**: hooks missing | report; do not patch blind |
| band1 state present, G hooks nonzero, 0x40114548/0x4C != 0x6101 or 0x40101220 != 1 | **C2**: band1 MAC block not started/applied | R2 band-start bias |
| band1 state present + MAC block looks initialised + still silent | **C5/C4**: TBTT arm or band hints | R3 |
| no ctx bound to band1 at all | the 5 GHz AP start never reached the firmware | R2 full re-post with field B = the AP-start module index |

## R1 - the likely fix: one consistent field B across the 5 GHz VAP lifecycle

Only if R0 shows C1. Re-post, in this order, with **the same field B in every
frame** (the AP-start module index: 0 in the F2 recipe; the F1 fire's mistake
class was field B=4 on the RF frames, see ../wifi-fw-dfs-worker):

| rung | frame | body | check after |
| ---: | --- | --- | --- |
| 1 | g0-id1 0x0064 VAP add | the AP-start body (../wifi-vap-start TABLES T5) with **body[4] = field B** and **body[0x12] = 1 (band1)** | ctx[fieldB]+0x4B8 == 0x15E570, +0x4B0 == 0x00175A18 |
| 2 | g0-id1 0x0124 channel | band=1, ch36 (the already-fired channel frame) | perlink[1]+0xE20 chain and P+0xF04 (should stay 3) |
| 3 | g0-id1 0x01F7 | len 1, body {0x03} (the F2 frame, hdr[3] = field B) | P+0xF04 == 3, R+0x3A8/0x3AC image |
| 4 | g0-id1 0x0124 channel | same as rung 2 | the R image is written (it already is on the fired boot) |
| 5 | g0-id1 0x0127 AP config | the 5 GHz start_ap body (156 B; body+0x00 interval, +0x04 dtim, +0x8A..+0x8D the 5 GHz HT/VHT caps) | VAP+0x10/+0x14/+0x13A |
| 6 | g0-id1 0x0065 VAP start | 12 B, body[0]/[1] as for the working 2.4 GHz start | **perlink[1]+0x6C6 == 1**, +0x6B8/+0x6BC/+0x6C0 nonzero, +0x6C4 == 16 |

If rung 6 sets +0x6C6 = 1 and the beacon still does not appear, the TX state
now exists; continue in R3 (do not re-post rung 6 - a second start skips the
allocation by design).

R2 alternative (C2): the band-1 MAC start/enable is reached from the VAP start
-> CA 0x971D8 -> G+0x370/G+0x374 (CA 0x4AF0C and the enable-TRX worker
CA 0xE8520 are the internal callees). There is **no known H2D key** that calls
the per-band MAC start directly (TABLES T9); the only supported way is the
VAP start on the band-bound ctx, i.e. R1 rung 6. If rung 6 set the state but
the band1 block still reads uninitialised, that is a firmware arming gap:
**report, do not patch** (snapshot MANIFEST, new lane).

## R3 - read-only TBTT identification (only if C5 remains)

Sample the same 27 band1/band0 IO reads twice, about 200 ms apart, in one boot
(no writes). The beacon interval is ~100 TU = 102.4 ms; the register pair that
advances by the TU count (or that shows a TBTT pending/status bit that toggles)
identifies the TBTT bucket. Candidates from the firmware's own writes (TABLES
T6/T7): 0x4011451C/0x40114520 (status after the block reset), 0x40114548/
0x4011454C/0x40114550/0x40114554/0x4011455C/0x40114560/0x40114564/
0x40114568/0x40114588/0x40114594 (init-table targets), 0x4010C200/0x4010C220
(SMAC IRQ status/enable), 0x4010E150/0x4010E154 (band1 TRX enable). Report the
pair that moves; the next lane needs that diff, not another guess.

## Abort rules (any hit -> stop, power-cycle, analyse offline)

* exception mask CA 0x40008 != 0, golden page/state change, nonzero staging
  frame at BAR0 0x6F800C (../wifi-ap-start TABLES T9);
* any posted frame not consumed inside the provider's bounded poll, or the H2D
  lane closing, or a dmesg fault;
* 5 GHz beacon appears (then stop; the observation is the result);
* page/mask not golden before the ladder, or the module boot not validated
  (../wifi-fw-ready-gate, ../wifi-fw-audit);
* any read that would touch the RC misc window or any write not listed above.

## Deliverables of a fire

* full read log with the R0 table (C1/C2/C3/C5 verdict), per-band beacon-state
  bytes, and the two R3 samples;
* the exact frames posted (rungs), their field B and body[0x12]/[0x4];
* a new mem-ledger entry; if C1 was confirmed, the working 5 GHz AP order goes
  into the driver lane as an ordering fix (no firmware patch).
