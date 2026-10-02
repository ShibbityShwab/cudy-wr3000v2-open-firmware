# PROVEN: the SR descriptor post triggers the id-6 word (phase 24i, 2026-10-02)

A controlled A/B, same device, same session, one variable. **The localization is confirmed.**

## The A/B

| | wifidrv1 (no descriptor post) | fwaccept (posts SR nodes) |
| --- | --- | --- |
| artifact | `wifidrv1.ko` (own build) | md5 `55165b75c6ac0e4b3e23b02a25574b46` - **the exact module phase 20 recorded** |
| poll | 25 ms from the instant of release | 25 ms |
| evidence | `exp/20261002-213111` | `exp/20261002-213557` |
| `out[1]` words | **id 2 only**, at +550 ms | **id 6 -> id 2 -> clear** |

fwaccept, at 25 ms:

```
SR ch0 posted 32 nodes word0=0x84de0000 word1=0x00486d2b; commit SR+0x18 <= 0x00000400 readback=0x00000400
[poll +730ms]  MBOX out[0] H2D mask CA=0x40039010 0x00000000 -> 0x00000008
[poll +750ms]  MBOX out[1] pending  CA=0x40039014 0x00000000 -> 0x00000040   <- id 6
                 bit 6 (id 6 = pcie_trigger_ete_sending_handle)
[poll +1260ms] MBOX out[1] pending  CA=0x40039014 0x00000040 -> 0x00000004   <- id 2
[poll +1400ms] MBOX out[1] pending  CA=0x40039014 0x00000004 -> 0x00000000   <- cleared
```

wifidrv1, at 25 ms:

```
[poll      ]  t=0     out[1] 0x00000000 (baseline)
[poll +550ms] out[1] 0x00000000 -> 0x00000004                                <- id 2 only
poll done: 1 transitions in 4000 ms
```

**Same device, same kernel, same poll resolution, one variable: the SR descriptor post.** With it, the
firmware's full dialogue appears; without it, only the second word.

## The extra thing the A/B revealed

fwaccept's poll also shows a register `wifidrv1` never watches:

```
[poll +770ms]  STAT abank_code CA=0x40005060 0x0000010d -> 0x0000010c
[poll +950ms]  STAT abank_code CA=0x40005060 0x0000010c -> 0x0000010b
[poll +1100ms] STAT abank_code CA=0x40005060 0x0000010b -> 0x0000010d
... and it keeps toggling through the whole window
```

**`abank_code` oscillates `0x10b <-> 0x10c <-> 0x10d`** while the dialogue runs. In every wifidrv1 run
it was a single settled value (`0x0000010c`). An analog bank register changing repeatedly means the
firmware is **actively driving the radio**, not sitting in a ready loop - so with a descriptor posted,
the chip is doing real work.

## What this corrects in the record

- Phases 19/23/24 read the firmware as emitting a *single* ready word. That was a consequence of the
  host never posting a descriptor: the firmware had nothing to wake a thread about, so it never said
  `pcie_trigger_ete_sending_handle`. **The dialogue is three transitions, not one.**
- Phase 22's "submitting without the device-side accept gate does nothing" was drawn from a
  ring-ownership-only configuration and is now seen to have the dependency backwards: the device does
  not advance *until* a descriptor is posted.
- `wifidrv1`'s "no descriptor submitted, no doorbell rung (bounded by design)" was the correct
  engineering call for a ring-ownership test, but it also removed the trigger for the very word the
  later phases were trying to elicit.

## Next: port the SR post into wifidrv1

The pieces, all present in `lab/fwaccept/fwaccept.c` and mostly already in `wifidrv1`:

1. the outbound iATU viewport (`omo_program_outbound`, `outwin=1`, devva/hostca base `0x80000000`) so
   the device can read host memory;
2. the node fill (`shuangta_ete_sr_dscr_fill`): word0 = the payload's device VA, word1 =
   `(len << 16) | 0x6d2b`;
3. the producer commit to SR `+0x18` (the recorded value for 32 nodes at depth 32 is `0x400`);
4. the channel enable at `+0x00`/`+0x48`.

Device state after the A/B: `W=2 I=6 OFF=0 FAIL=0`.
