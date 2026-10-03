# The message-type tally, and a correction to my own recent reasoning (phase 25k, 2026-10-03)

Running the characterisation in **normal operation** (the regime the instrument needs) over a full pass
of the live vendor ring, 13 stable pairs, gave this:

```
+0 histogram                      length histogram
  0x00000000  x3                    72   x11
  0x04000100  x2   <-- THE PORT'S   29   x2
  0x00000104  x1        ORIGINAL
  0x00000102  x1        VALUE
  0xefaa8b46  x1
  0x00450102  x1

+6 histogram
  0x0010 x4   0x0000 x2   0x000c x2   0x0048 x1   0x0024 x1   0x0d01 x1
```

## Two things this establishes

1. **The port's original `+0 = 0x04000100` IS a real vendor message type** - it appears twice in the
   live ring, alongside several others (`0x01000100`, `0x00000104`, `0x00000102`, `0x00450102`).
   The captured frame was **not** a transcription error; it is a genuine type.
2. **Length 72 dominates (11 of 13)** - so the port's 72-byte length is right, and the earlier
   `sr_desclen=0x30` variant was indeed the wrong direction.
3. **`+6` is NOT a fixed header field.** It takes six different values across 13 messages
   (`0x0010`, `0x0000`, `0x000c`, `0x0048`, `0x0024`, `0x0d01`). It varies per message, i.e. it is part
   of the payload, not the header.

## The correction I owe my own record

In phases 25g-25j I inferred a "recurring type-1 header" from **three** samples (`+0=0x01000100,
+6=0x001d`) and changed the port's frame to match it, treating the capture's `0x04000100`/`0x0001` as
the discrepancy. **A larger sample shows that inference was partly wrong**:

- `0x04000100` was a legitimate type, not a mistake - so changing `+0` moved the port from one real
  message type to another, which is why the result was neutral rather than a fix;
- `+6` is not a header field at all, so "matching `0x001d`" was matching a payload byte against one
  sample of a varying field.

That is the same failure mode this project has now been bitten by repeatedly, in a new dress: **drawing a
general rule from too few samples.** Three readings of a churning ring is not a distribution. The tally
that would have prevented it took one pass and is cheap.

## What the tally leaves

The port is sending **a real vendor message type (0x04000100), at a real vendor length (72)**, with a
header that matches the vendor's shape, and the firmware reads it (the SR index catches up, the announce
is consumed) without the dialogue advancing. The header is not the answer, and neither is the type:
**what remains is the meaning of the payload**, which is defined by the HCC command table and the
message's semantics - not by any field the ring alone can reveal.

The honest next instruments, in order of cost: characterise the *payload* structure of the `0x04000100`
messages specifically (the type the port sends), or obtain the vendor's source/command table.
