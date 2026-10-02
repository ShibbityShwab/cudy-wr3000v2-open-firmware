# Address verification: the corrected registers, checked against a live vendor boot (phase 24b, 2026-10-02)

After the phase-23x window bug, every register this module touches was re-verified the same way the
bug would have been caught: **read the address on a live vendor boot and confirm it behaves like the
register it claims to be.**

## The message window (device CA -> BAR0, via 0x3b8000 + (CA - 0x40000000))

Vendor boot, all read-only:

| register | BAR0 | vendor value | reading |
| --- | --- | --- | --- |
| `out[0]` | `0x3f1010` | `0x00000000` | idle, as expected |
| `out[1]` | `0x3f1014` | `0x00000000` | idle, as expected |
| glue status | `0x3f12ec` | `0x00000000` | idle |
| `out[5]` | `0x3f12f0` | `0x00000000` | idle |
| ack `out[3]` | `0x4b9438` | `0x00000000` | idle |
| re-arm `out[4]` | `0x4b9414` | `0x00000000` | idle |
| (neighbour) | `0x4b9410` | `0x00000001` | a real, non-zero register next door |

None reads `0xffffffff`, so the whole region decodes, and every message register reads zero on an
idle vendor boot - which is what an idle message interface should look like.

## The check that proves the old address was wrong

```
0x403f1010  (correct out[0])  = 0x00000000
0x403f0010  (old, one page low) = 0x40000004
```

Both the driver and the vendor boot returned `0x40000004` at `0x403f0010` - and that was taken as
confirmation that the address was right. It was two reads of the **same wrong address**: the
agreement was circular. On the correct address, an idle vendor boot reads `0`.

That is the concrete form of the lesson recorded in `ROOT-CAUSE-window-base.md`: **agreement between
two reads of the same address proves nothing about whether the address is the register you think it
is.** The only check that works is deriving the address from the documented CA plus the translation
rule and confirming the register *behaves* as documented - idle when it should be idle, asserting when
it should assert.
