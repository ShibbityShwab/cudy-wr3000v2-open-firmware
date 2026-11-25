# FIRE-RECIPE - the H2D 0x00F7 descriptor table (C1)

**This lane never fires.** The Lead fires on the bench with the resident bench
procedure (link PASS + recovery card READY + human present). Everything below
is the message content and the read-only verification; no new transport code is
needed - the frame rides the existing h2d_post machinery that already sends the
0x0064/0x0065/0x0067/0x0124/0x0127 ladder.

## R0 - host-side pre-fire (no bench)

1. `python3 tools/fw_desc_table_re.py --selftest` - asserts every decode fact
   (emitter site, len formula, builder layout, blob filler fields, gate strcmp,
   boot pool, worker flow, predicate bit) against the pinned local artifacts.
2. `python3 tools/fw_desc_table_re.py --frame` - emits the 68-byte frame; assert
   total 0x44, key 0x00F7, len 0x28, count 2, rec1 word 0x00800201 (bit23 set),
   bitmap 0xFF, cc "US", tag 0x5A5A, node word1 0x00446D2B.

## R1 - the frame (byte-exact)

```
00010004 4400 0100 0000 5a5a
0000000001000000
f700
2800
5553000002000000
000080001e002c0100000000ff1f0000
010280001e002c0100000000ff000000
ffffffff
```

Decoded (TABLES T2/T3):

* header: group 0, alloc/dst 0x01, fa 0x00, fb 0x04, total **0x0044**,
  id **0x0001**, fc 0x00, dst 0x00, tag **0x5A5A**;
* preamble 8 B, key **0x00F7**, payload length **0x0028**;
* blob 40 B: cc **"US"**, chip id 0, count **2**;
  * rec0 word **0x00800000** (band 0, byte2 bit7 = armed), +4 30, +6 300,
    bitmap **0x1FFF** (2.4G ch1..13, unchanged from boot default);
  * rec1 word **0x00800201** (band 1, bw 20 MHz, byte2 bit7 = armed),
    +4 30, +6 300, bitmap **0x000000FF** (**5 GHz ch36,40,44,48,52,56,60,64**);
* 4-byte inherited tail FF FF FF FF.

Why these values: the blob *is* the firmware pool image for N=2 (worker:
`memcpy_s(pool,0x148,body,8+body[4]*16)`); the fields +4/+6/byte0/byte1 mirror
the boot-default table (30/300, band 1, bw 20 MHz) so the only change is the
armed bit and the 5G bitmap; **cc must not be "99"** (the boot pool's string -
the gate CA 0x1037BC is a strcmp dedupe and would silently skip the install).

## R2 - host implementation notes (clean driver)

1. Add one builder next to the existing ones in the h2d_frames family, e.g.
   `h2d_build_g0_id1_desc_table(f, dst, token, cc[2], rec, n)`:
   - `len = 8 + 16*n`; `payload_len = 0x10 + len`; blob at payload+12;
   - total = 12 + 16 + len = len + 28; tag/ids exactly as T2;
   - refuse n < 1, n > 20, a bitmap outside its band width, or cc "99"/empty.
2. Emit from the existing h2d_send selector (a new `desc_table` mode) after the
   `ctx_create` (0x0064) and `start_vap` (0x0065) steps and **before** the
   0x0124 channel post, so the 0x0124 worker's RF branch (CA 0x40684 ->
   predicate CA 0x101890) sees the armed map. If the ladder already posted
   0x0124, re-post it after 0x00F7 (the populator is idempotent) - the Lead's
   R1 self-clobber rule of the RF lane still applies to the VAP context window.
3. One-shot semantics: the worker installs and populates; it returns 0x8B2D on
   an oversized blob, 0x64 if the band object is absent (too early in the
   ladder) - so send it after the VAP start, not before 0x0064.
4. Fallback variants (change only the blob):
   * **full 5G**: rec1 bitmap **0x1FFFFFFF** (ch36..196) if the AP/scan needs
     the higher rows;
   * **40/80 MHz**: rec1 byte1 = 04 / 05 if the fire uses wider channels;
   * **cc retry**: if probe 1 shows the pool country string unchanged, resend
     with a different cc (e.g. "GB") - the gate is a string compare, not a
     policy check;
   * **both-armed**: identical frame but rec0 word 0x00800000 already is the
     armed 2.4G record in this recipe.

## R3 - fire and verification (Lead, read-only probes)

Order: the working five-frame ladder with the new frame inserted:
**0x0064 -> 0x0065 -> 0x00F7 -> 0x0067 -> 0x0124 -> 0x0127** (or insert
0x00F7 right after 0x0065 and before 0x0124 in whatever order the current
ladder uses). Pad/fuzz the transport as before; the frame is a normal g0/1
config event with a 68-byte wire node.

Read-only verification (probe pack TABLES T7; phys = CPU + 0x46B8000):

| step | read | pass |
| ---: | --- | --- |
| 1 | pool header CPU 0x1ADC98 (8 B) | cc = "US", count = 02 |
| 2 | pool rec1 word CPU 0x1ADCB0 | **0x00800201** (bit23 set) |
| 3 | pool rec1 bitmap CPU 0x1ADCBC | 0x000000FF |
| 4 | band-1 map ch36 row CPU 0x117AD4 | byte1 = **01** (was 0xFF) |
| 5 | band-0 map ch1 row CPU 0x117B48 | byte1 = 00 (unchanged) |
| 6 | **P+0x1A00 (O)** CPU 0x178238 (phys 0x40830238) | **nonzero** (the descriptor/PHY publish word) |
| 6a | **P+0xF04** CPU 0x17773C (phys 0x4082F73C) | **nonzero** (mode byte = 3: band-1 PHY init ran) |
| 7 | R+0x410 after a 0x0124 re-post | **bit0 = 1** (the predicate saw the armed record) |
| 8 | R+0x3AC | **0x03E6138A** (the RF chain image) |
| 9 | ch36 beacon | present (host scan) |

Chain for the pointers (sibling lanes): ctx0 = u32(0x171458) + u16(0x172504);
perlink = u32(ctx0+0x4B0); O = u32(perlink+0x2820); R = u32(O+0x2D8). Probes 6/7
are the discriminator between "table installed + predicate 1" and "table
installed + predicate still 0" (then the 0x40684 branch gates - sibling probes).

## R4 - failure modes and next moves

| observation | meaning | next |
| --- | --- | --- |
| pool cc still "99", count 2 | the gate skipped the install (cc collision) or the frame never reached the worker | resend with another cc / check the frame bytes + key row (0xF7 != 0x00DA) |
| pool cc changed but rec1 word bit23 clear | frame truncated or fields misplaced | re-check len/total/offsets vs T2/T3; the worker copies 8+N*16 from hdr+24 |
| pool word + map row good but R+0x410 bit0 = 0 | predicate/0x40684 branch not reached (ctx[4] != 2 or a gate byte 0) | probe the sibling 0x40684 gates |
| bit0 = 1 but R+0x3AC stale | the armed bit is not the RF-image gate; the RF image path is gated elsewhere | hand back to the RF lane; C1 re-ranked |
| worker logged 0x8B2D | count > 20 (frame bug) | fix the count |
| worker logged 0x64 | band object missing (sent too early) | move after 0x0065 |

## Abort rules (unchanged, from the bench procedure)

* page 0x86807fef / state 1 / mask exception -> power-cycle, no retry;
* never write CA 0x400392F0; never read 0x10160000..0x1016FFFF;
* no fire without the bench link PASS + recovery card READY + a human at the
  bench; one change per fire; record the probe results under
  `build/linux-kbuild/waves/<phase>/`.
