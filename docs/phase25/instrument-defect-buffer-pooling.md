# The live-ring instrument is weaker than I claimed: the buffers are pooled and recycled (phase 25l, 2026-10-03)

Reading the ring **entirely within one ssh call per group** (batching many `devmem` reads, which is the
real fix for an instrument that had been taking ~0.3 s per word) exposed something the earlier passes had
missed:

```
descriptors: 32 | distinct buffers: 13
buffer reuse:  0x85034440 x4   0x85034a40 x5   0x85034840 x3
               0x85035240 x3   0x85035e40 x4   0x85034e40 x3   ...
```

**Thirty-two descriptors point at only thirteen buffers.** So a descriptor's buffer address is **not**
proof of what that message contained: several descriptors can name the same buffer, and whichever message
wrote it last is what a read returns.

## What this invalidates

Phases 25g-25k read a descriptor, then read its buffer, and treated the pair as that message's content. The
"recurring header" (`0x01000100`, `+6=0x001d`) and the later type tally
(`0x04000100 x2`, `+6` taking six values) were **built on those pairs** - and with a 13-buffer pool, a
stable descriptor can still hand back **recycled** bytes from a different message. The mid-read stability
check I used (read the descriptor, read the buffer, re-read the descriptor) guards against the *descriptor*
changing, which is not the failure mode that matters.

That directly undermines two things I recorded as established:

- **that `0x04000100` is "a real vendor type"** - it appeared in buffer reads that may not have belonged
  to the descriptors that named them;
- **that `+6` "is not a header field"** - the six values may be six different messages' bytes, not six
  variants of one field.

Neither is *refuted*. Both are now **unsupported**: the instrument as I built it cannot tell a message's
own bytes from a pooled buffer's leftovers.

## What the instrument DOES establish

- The **descriptor** data is sound: `{word0 = buffer address, word1 = (len<<16)|flag}`, and lengths of
  **72 (dominant), 48, 29, 184** really do occur - the length histogram is descriptor-derived and unaffected
  by pooling.
- The **layout** claim (verified three ways - the fill disassembly, the vendor's own
  `get_dscr_len`/`get_dscr_flag` decoders, and live descriptors) stands.

## What a sound instrument would need

Reads that are atomic with respect to the ring, or a way to know which write last touched a buffer. The
practical version: capture a buffer **immediately after** its descriptor is seen to advance, before the
pool recycles it - i.e. poll the write index and read the just-written slot's buffer, rather than sweeping
all descriptors and reading their buffers at leisure as this pass did.

## The lesson

This is the third time this project has been bitten by generalising from a measurement whose association
was assumed rather than proven - after the address that "matched" because both readers read the same wrong
page, and the header inferred from three samples. **A stable read is not a bound read.** The ulw-loop
criterion C002 anticipated the churning *descriptor*; the churning **buffer pool** is the one that actually
lied.
