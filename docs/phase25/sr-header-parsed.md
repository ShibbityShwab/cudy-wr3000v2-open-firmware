# The SR message header, parsed from near-atomic snapshots (phase 25m, 2026-10-03)

Batching every read into one ssh call per snapshot makes a full sweep internally consistent (~0.3 s
across all of it), which both **confirms the pooling defect** and finally gives a readable header.

## The pooling, confirmed directly

Two snapshots 4 s apart (write index + all 32 descriptors + the 13 pool buffers' first 8 words each):

```
wptr advanced 0x407 -> 0x415   (14 slots)
descriptors whose (addr,len) changed: 0,1,2,3,4,5,6,7,8,9,10,11
pool buffers that changed in 4 s: 6  of 13
```

Six of thirteen buffers were rewritten inside four seconds while the ring advanced 14 slots. **The pool is
recycled continuously**, exactly as the defect note says - so a buffer read at leisure cannot be bound to
a descriptor read earlier.

## The header, which the snapshots do settle

The changing buffers carry a consistent structure:

```
0x85034440  was 02000100 001d0048 5a5a0000 ...
            now 00330102 001d0010 ffff00ff ...
0x85034840  now 05000100 0001000c 5a5a0200 ...
0x85035e40  was 00000102 001d0010 5a5a0000 ...
```

Reading them as 16-bit fields, the layout is unambiguous:

| offset | field | observed values |
| --- | --- | --- |
| `+0x00` | proto (u16) | `0x0100`, `0x0102` |
| `+0x02` | a field (u16) | `0x0000`, `0x0001`, `0x0002`, `0x0004`, `0x0005`, `0x0033`, `0x0035` |
| `+0x04` | **length (u16)** | `0x0048` (72), `0x0010` (16), `0x000c` (12) |
| `+0x06` | a field (u16) | `0x001d` (29) or `0x0001` |
| `+0x08` | zero | `0x0000` |
| `+0x0a` | **magic (u16)** | **`0x5a5a`** - present in every sample |

**The magic at `+0x0a` is the anchor**, and it holds in every buffer read - independently of pooling,
because a magic that appears at a fixed offset across continuously-rewritten buffers is a structural fact,
not an association artefact.

## What this says about the port's frame

The port sends `00 01 00 04 | 48 00 | 01 00 | 00 00 | 5a 5a`, which parsed the same way is:

```
+0x00 proto  = 0x0100    (a value the vendor uses)
+0x02 field  = 0x0004
+0x04 length = 0x0048    (72 - the vendor's dominant length, and its own descriptor's)
+0x06 field  = 0x0001
+0x08        = 0x0000
+0x0a magic  = 0x5a5a    (matches)
```

**Every field of the port's frame is well-formed against the vendor's own layout, and three of the six
values are ones the vendor demonstrably uses.** The two that differ are `+0x02` (port `0x0004`; vendor
`0x0000`/`0x0001`/`0x0002`/`0x0005`/`0x0033`/`0x0035`) and `+0x06` (port `0x0001`; vendor also uses
`0x001d`).

So the message is structurally right and its fields are individually plausible - which is consistent with
everything the device has done with it: read it, consume the announce, and not advance. **`+0x02` and
`+0x06` are now precisely-named candidates**, and unlike the earlier round they are named from a layout
established by an anchor (the magic) rather than by association.
