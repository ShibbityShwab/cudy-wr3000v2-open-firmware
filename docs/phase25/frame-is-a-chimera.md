# The port's frame is a chimera: the header of one message, the body of another (phase 25v, 2026-10-03)

Tracing where the port's body bytes came from - rather than assuming they were arbitrary - found the
actual error, and it is exactly what the body-zeroing test corrects.

## Two DIFFERENT real vendor messages, both type `0x04000100`

`docs/phase20/tx-path.md` section A.5 recorded the vendor's **first** host->device SR frame, byte-exact:

```
buffer 0x85257C40 (descriptor length 0x48 = 72 B):
  +00 = 0x04000100   +04 = 0x00010030   +08 = 0x5A5A0000   +0c = 0x00000000
  +10 = 0x00000001   +14 = 0x001400D8   +18 = 0x00000001   +1c..+28 = 0
  +2c..+44 = 0xFFFFFFFF x7
```

Read the fields properly: **`+0x04` is a u16 total length = `0x0030` = 48**, and `+0x06` is the u16 id =
`0x0001`. So this is the vendor's **id-1, length-48** message, carrying `{1, 0x001400d8, 1, 0,0,0}` and a
`0xFF` tail.

The phase-25 bound capture gives a **different** frame of the same type:

```
04000100 001d0048 5a5a0000 00000000 ... (zeros to +0x47)
```

i.e. **`+0x04` = `0x0048` = 72**, **`+0x06` = `0x001d` = 29**, and an **all-zero body**.

So the type word `0x04000100` is shared, and **the two messages differ in length, id AND body**.

## The port took the header from one and the body from the other

| field | phase-20 message (id 1, len 48) | phase-25 bound (id 29, len 72) | the port sent |
| --- | --- | --- | --- |
| `+0x04` length | `0x0030` (48) | `0x0048` (72) | **`0x0048`** <- from the 72-byte message |
| `+0x06` id | `0x0001` | `0x001d` | `0x0001` then **`0x001d`** <- from the 72-byte message after phase 25p |
| `+0x10` | `0x00000001` | `0x00000000` | **`0x00000001`** <- from the 48-byte message |
| `+0x14` | `0x001400d8` | `0x00000000` | **`0x001400d8`** <- from the 48-byte message |
| `+0x18` | `0x00000001` | `0x00000000` | **`0x00000001`** <- from the 48-byte message |
| `+0x2c..` | `0xff` x7 | `0x00` | **`0xff`** <- from the 48-byte message |

**The port's frame is a chimera**: the length and id of the 72-byte message, with the body of the 48-byte
message appended. Both halves are individually real vendor bytes, and the combination has never existed on
any vendor ring.

## Why the header series was negative, precisely

Every header candidate was tested against a frame whose **body belonged to a different message**, so no
header change could make it consistent - and no header change could be *expected* to, once the body is
known to be wrong. The twelve eliminated candidates were eliminated correctly; they were simply aimed at
the half of the frame that was already right.

And the body had never been compared because the comparison needed a trustworthy counterpart, which
needed the bound capture, which needed the pooling defect to be found and fixed first.

## What this predicts

The body-zeroing test makes the frame **self-consistent with the 72-byte message**: length 72, id 29,
zero body, zero tail. If the firmware's rejection was caused by the inconsistency, the port should now see
something it has not seen before. If it is unchanged, then the 72-byte zero-body message is not the one the
firmware wants at this point either - which would shift the question to *which* of the vendor's messages
the firmware is waiting for, rather than what its bytes should be.

