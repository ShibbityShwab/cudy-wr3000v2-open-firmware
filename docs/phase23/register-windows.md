# Register windows: the verified mapping (phase 23e, 2026-10-02)

Written after two device-measured corrections. This is the authoritative offset table for the port's
driver, with each value checked against the vendor's own artifacts **and** against a live readback.

## The translation rule (docs/phase7/userspace-bar-access.md)

```
resource0_offset(CA) = 0x3b8000 + (CA - 0x40000000)     [CA in 0x40000000..0x4011ffff]
host_phys(CA)        = 0x40000000 + resource0_offset = 0x3b8000 + CA
```

So a device chip address `0x4003XXXX` lands at BAR0 offset `0x3XXXX`. The region-3 IO window
(`SHUANGTA_REGION_IO`, host `0x403b8000..0x404d7fff` -> dev CA `0x40000000`,
`docs/phase18/inbound-map.md` row 3) is what makes those registers reachable at all - **it must be
programmed via the inbound iATU viewports before any of them decodes.**

## The blocks

| block | device CA | BAR0 offset | what it is |
| --- | --- | --- | --- |
| message `out[0]` | `0x40039010` | `0x3f1010` | H2D pending/message mask |
| message `out[1]` | `0x40039014` | `0x3f1014` | the register the released chip writes |
| message `out[2]` | `0x400392d4` | `0x3f12d4` | doorbell |
| channel res | - | `0x3f12e8` | `pcie_ete_chn_res` read/clear `& 0xfffffc20` |
| message `out[5]` | `0x400392f0` | `0x3f12f0` | `pcie_msg_send_irq` writes 8 (**never written by us**) |
| status | `0x40039508` | `0x3f1508` | the ETE status block |
| ETE rings | `0x4003a000` | `0x3f2000` | SR ch `+0x400/0x450/0x4a0` stride `0x114`; DR ch `+0x590/0x5e0/0x630/0x680` stride `0x6c` |

## How this was established (and how it was got wrong first)

Two identical-looking mistakes were made and caught by measurement, which is why the table is
trustworthy now:

1. The first driver mapped the SR/DR registers at BAR0+0x39000 - that is the **message** block's low
   offsets, not the ring block. Measured symptom: the ring fields read `0xffffffff`.
2. After the viewports were programmed (`program=1`), the ETE block decoded correctly (ring fields
   `0x00000000`) while the **message** block flipped to `0xffffffff`, because the driver was reading
   it at `0x39000` instead of `0x3f0000`.

**The diagnostic rule, measured twice on hardware: a mis-addressed or unprogrammed window reads
`0xffffffff`.** That value is not a hardware state to interpret - it is the signature of a wrong
address or a missing viewport program. Both times it named the bug exactly.

## Vendor corroboration

The BAR0 offsets in the table above are quoted from the vendor-derived slot table in
`docs/phase20/runtime-msg.md` ("| slot | device CA | BAR0 off |"), which lists `out[0]` at
`0x3f1010`, `out[1]` at `0x3f1014`, `out[2]` at `0x3f12d4` and `out[5]` at `0x3f12f0`, and from
`docs/phase18/inbound-map.md` (ETE at `BAR0+0x3f2000`). The arithmetic above reproduces every one of
them from the single translation rule, which is why the two independent sources agreeing is meaningful
rather than circular.
