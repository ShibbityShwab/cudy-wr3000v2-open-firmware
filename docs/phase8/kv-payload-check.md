# Calibration payload structure: an independent check (phase 8, 2026-10-01)

A first-pass analysis of this payload claimed a fixed record template: 16 all-zero word columns at
offsets 0-3, 9-12, 18-21, 27-28, 31-32, a 38-word record length, a 9-word sub-stride, and data values
confined to `0x0000..0x1fff`. I could not reproduce any of that, in any of four interpretations, so
this document records what actually reproduces instead. The first-pass file is not published.

## What I ran and what I saw

All scans operate on `build/tmp/wifi_cali_data.kv` (8,912 bytes) and `wifi_cali_data_2g.kv`
(2,336 bytes), with the 4-byte `ZZZZ` magic and the 4-byte `a5a5a5a5` trailer stripped first.

1. **u32 words from offset 0**, records assumed to start at word 0:
   - dual: no all-zero column exists at all (out of 58 candidate 38-word records);
   - 2g: all-zero columns are `[19, 21, 29, 31, 37]`, not the claimed set;
   - the largest u16 half in the remaining columns is `0xfff8`, far outside `0x1fff`.
2. **u32 words with the claimed header/pre-region phases** (`352`, `68`, `284` words):
   - zero columns come out as `[13,19,21,29,31]`, `[9,11,31,37]`, `[5,11,13,21,23]` respectively;
   - the u16 maximum stays at `0xffe4`/`0xfff8`/`0xffe8` in all three.
3. **u16 arrays**: the same failure mode; no column set matches, and large (negative-looking) values are
   common in every arrangement tried.
4. **Raw byte runs of four or more zeros**: their start positions modulo 38 cover 0 through 9 and beyond,
   so the claimed "0, 9, 18 only" is not a property of the bytes either.

## What does reproduce

With the criterion "a run of at least 20 zero bytes", both stores show a clean periodic skeleton:

| file | payload bytes | long zero runs | run length | period |
| --- | --- | --- | --- | --- |
| `wifi_cali_data.kv` | 8,904 | 3, starting at 54, 166, 278 | 58 bytes each | **112 bytes** |
| `wifi_cali_data_2g.kv` | 2,328 | 2, starting at 22, 70 | 26 bytes each | **48 bytes** (one interval observed) |

That is the only periodicity I can defend from the data: a repeating block whose zero-fill region is 58
bytes inside a 112-byte record (dual), and 26 bytes inside 48 bytes (2.4 GHz). The number of such blocks
is small, which suggests each block carries a table-sized payload, not a per-channel entry.

## Status and what would settle it

- The payload's field layout stays **open**. A claim set that does not survive re-running is worse than
  no claim, so the earlier template is withdrawn rather than published.
- The decisive next step is not statistical: it is the driver function that **fills** this buffer before
  the save routine writes it. The writer only copies and stamps magic; the populator is where the field
  order is defined, and it is a different symbol. That disassembly is the target of the next pass.

## Limits

- Everything here is measured on the two stores from this one unit; a second unit's stores would confirm
  the 112/48-byte skeleton.
- Byte-level periodicity does not by itself prove field widths; the populator disassembly is still
  required, and this note does not claim any field semantics.
