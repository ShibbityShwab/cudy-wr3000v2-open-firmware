# The interrupt block at device CA `0x40161100` is not host-visible - and BAR0 `0x519100` is not it (phase 32, 2026-10-04)

**Answer.** The firmware's interrupt block at device address `0x40161100` (the enable bitmap;
its priority sibling `0x40161800`) is **not reachable through any host window**. The only inbound
window onto the device's `0x40xxxxxx` register space is region 3, `SHUANGTA_REGION_IO`, whose device
range is `0x40000000`-`0x4011ffff` (1.125 MiB). `0x40161100` is `0x41100` bytes **past** that
ceiling, and no other region targets `0x40xxxxxx`.

The host read at BAR0 `0x519100` that phase 30/31 treated as "the interrupt block" is not a device
read at all. The probe computes `ioremap(BAR0 + 0x3b8000, 0x120000) + 0x161100`, which is `0x41100`
bytes **past the end of a 1.125 MiB kernel mapping** - an out-of-bounds read that lands in adjacent
kernel memory. Its eight words are byte-identical to `FIRMWARE.bin[0x40100..0x40120]`, the firmware's
own code: that is why they are stable, code-like, identical across boots, and never move on a
doorbell.

Everything below is read-only; no router was touched.

## 0. Summary of the two questions

| question | answer | evidence |
| --- | --- | --- |
| Is device CA `0x40161100` reachable through any host window? | **No.** Region 3's device range ends at `0x4011ffff`; no region targets `0x40xxxxxx` else. | phase17 §A.6, phase18 §A.6 (quoted in §A) |
| Why does BAR0 `0x519100` read **zero** in normal operation? | It resolves to region 4 (`SHUANGTA_REGION_ACP`) device CA `0x02041100`, and that ACP SRAM is zero there. | `barmap_ep0_bar0.bin` @ `0x519100` = 32 zero bytes (§C) |
| Why does the **takeover** read the same offset as stable, code-like words? | It does not read BAR0 `0x519100`; it reads `0x41100` bytes past an `ioremap` of region 3, i.e. adjacent kernel memory. The bytes are `FIRMWARE.bin[0x40100..0x40120]`, the firmware's own image. | `wifidrv1.c` `omo_intrsamp`; the live dump's `0x80100`/`0x738100` copies (§D) |

## A. The region table, quoted

### A.1 `opensource/docs/phase17/fw-download.md` §A.6 - device ranges

Quoted verbatim (device range column is `+0x18..+0x20` of each descriptor):

> | # | name | device range (+0x18..+0x20) | size |
> | - | ---- | --------------------------- | ---- |
> | 0 | `SHUANGTA_REGION_ROM_WRAM` | `0x00000000`-`0x001bffff` | 1.75 MiB |
> | 1 | `SHUANGTA_REGION_TCM_NOACP` | `0x00400000`-`0x00417fff` | 96 KiB |
> | 2 | `SHUANGTA_REGION_PKTRAM_NOACP` | `0x01000000`-`0x011dffff` | 1.875 MiB |
> | 3 | `SHUANGTA_REGION_IO` | `0x40000000`-`0x4011ffff` | 1.125 MiB |
> | 4 | `SHUANGTA_REGION_ACP` | `0x02000000`-`0x021dffff` | 1.875 MiB |
> | 5 | `SHUANGTA_REGION_ACP` | `0x01200000`-`0x01417fff` | 2.125 MiB |

### A.2 `opensource/docs/phase18/inbound-map.md` §A.6 - the same six with host windows

Quoted verbatim (columns `host BAR0 window` / `size` / `device CA target`):

> | idx | host BAR0 window | size | device CA target | /proc/iomem name | what it decodes |
> | --- | ---------------- | ---- | ---------------- | ---------------- | --------------- |
> | 0 | `0x40000000`-`0x401bffff` | `0x1c0000` | `0x00000000` | `SHUANGTA_REGION_ROM_WRAM` | ROM/RAM code ... |
> | 1 | `0x401c0000`-`0x401d7fff` | `0x018000` | `0x00400000` | `SHUANGTA_REGION_TCM_NOACP` | device TCM |
> | 2 | `0x401d8000`-`0x403b7fff` | `0x1e0000` | `0x01000000` | `SHUANGTA_REGION_PKTRAM_NOACP` | packet RAM |
> | 3 | `0x403b8000`-`0x404d7fff` | `0x120000` | `0x40000000` | `SHUANGTA_REGION_IO` | the register/IO block (ETE engine at CA `0x4003a000` appears at BAR0+0x3f2000) |
> | 4 | `0x404d8000`-`0x406b7fff` | `0x1e0000` | `0x02000000` | `SHUANGTA_REGION_ACP` | ACP SRAM |
> | 5 | `0x406b8000`-`0x408cffff` | `0x218000` | `0x01200000` | `SHUANGTA_REGION_ACP` | ACP SRAM; **holds the firmware** at CA `0x01240000` = BAR0+`0x6f8000` |

The same doc's §A.5 records the live `/proc/iomem` names, which pin each row to its host window:

> ```
>   40000000-40ffffff : 0000:00:00.0
>     40000000-401bffff : SHUANGTA_REGION_ROM_WRAM
>     401c0000-401d7fff : SHUANGTA_REGION_TCM_NOACP
>     401d8000-403b7fff : SHUANGTA_REGION_PKTRAM_NOACP
>     403b8000-404d7fff : SHUANGTA_REGION_IO
>     404d8000-406b7fff : SHUANGTA_REGION_ACP
>     406b8000-408cffff : SHUANGTA_REGION_ACP
>   41000000-417fffff : 0000:00:00.0
>   41800000-41803fff : 0000:00:00.0
>     41800000-41803fff : iatu_bar1
> ```

BAR0's base is `0x40000000`; "BAR0+0x…" below means the absolute address `0x40000000 + 0x…`.

### A.3 The viewport programming (why the windows have exactly these extents)

`opensource/docs/phase18/inbound-map.md` §A.3/§A.4 (config-space and membar paths) and §A.5 record
that the vendor driver programs six inbound iATU viewports, one per row above, in the order
`ctrl2=disable`, `ctrl2=enable|BAR`, `base_lo`, `base_hi`, `limit = base + size - 1`, `target_lo`,
`target_hi` (`0x104 + 0x200*i` in BAR2, or `0x908`/`0x90c`/`0x910`/`0x914`/`0x918`/`0x91c` in config
space). The **size** is what makes each window finite: region 3's `limit` is `base + 0x120000 - 1`,
so its device reach stops dead at `0x4011ffff`. The record's takeover modules reproduce these same
six descriptors (`wifidrv1.c` `omo_program_inbound`, `program=1`).

## B. The corrected mapping

Region 3's translation, for a device CA in its range, is

```
BAR0 absolute = 0x403b8000 + (CA - 0x40000000)      valid only for CA in [0x40000000, 0x40120000)
```

For the interrupt block:

```
CA 0x40161100 - 0x40000000 = 0x161100   >= 0x120000  (region 3 size)
```

`0x161100` is greater than region 3's `0x120000`, so **the lookup falls off the end of region 3**.
The number `0x519100` in the briefs is the arithmetic `0x3b8000 + 0x161100` - a linear extrapolation
past region 3's window end (`0x4d7fff`). It is not a valid BAR0 address for CA `0x40161100`.

What the address really hits: absolute `0x40519100` lies in **region 4**, `0x404d8000`-`0x406b7fff`
(device CA target `0x02000000`), at

```
device CA = 0x02000000 + (0x40519100 - 0x404d8000) = 0x02041100
```

so it is **ACP SRAM**, not the interrupt block.

Which device CAs *are* host-visible in the `0x40xxxxxx` space: only region 3's
`0x40000000`-`0x4011ffff`. Every mailbox CA the record relies on is inside it:

| CA | region-3 offset | BAR0 |
| --- | --- | --- |
| `0x40039010` (H2D pending, out[0]) | `0x39010` | `0x3f1010` |
| `0x40039014` (D2H pending, out[1]) | `0x39014` | `0x3f1014` |
| `0x400392d4` (H2D doorbell, out[2]) | `0x392d4` | `0x3f12d4` |
| `0x400392f0` (H2D ack, out[5]) | `0x392f0` | `0x3f12f0` |
| `0x40101434` (D2H doorbell) | `0x101434` | `0x4b9434` |
| `0x40101418` | `0x101418` | `0x4b9418` |

All offsets are `< 0x120000`, i.e. inside region 3 - which is exactly why those CAs are "provably
shared" between firmware and host (`docs/phase22/fw-hostmem.md`, the ctx field table). The
interrupt block is the first `0x40xxxxxx` object the firmware uses that lies **outside** region 3.

## C. Why the same offset reads zero in normal operation

In a vendor boot the region map is programmed, so a real `devmem` read of absolute `0x40519100`
(BAR0 `0x519100`) lands on region 4 device CA `0x02041100`. The live vendor BAR0 dump
(`opensource/build/register-dumps/barmap_ep0_bar0.bin`, 16 MiB, phase 11) shows that location is
zero-sized:

```
BAR0+0x519100 (region 4, device CA 0x02041100):
  00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
```

(zero run spans `0x513ffc`-`0x51ade8`, 0x6dec bytes). So "the block reads zero in normal operation"
is simply region 4 ACP SRAM being zero at that offset - it says nothing about the interrupt bitmap,
and it does not require the bitmap to be "transient" or "write-only from the host's side"
(the phrasing phase 30 used).

Anchors that the same dump is the vendor's programmed map and not raw ROM state:

```
BAR0+0x3f2000 (region 3, ETE CA 0x4003a000): 0a010000 00000000     (0x0000010a, as phase 17/18 record)
BAR0+0x4d7ff0 (region 3 tail, CA 0x4011fff0): ffffffffffffffff...  (region 3 ends here)
BAR0+0x4d8000 (region 4 head, CA 0x02000000): 0000000000000000 02000012 20000600
```

## D. Why the takeover read is stable and code-like

### D.1 What the probe actually reads

`opensource/lab/wifidrv1/wifidrv1.c` (the phase-31 instrument):

```
#define OMO_IO_WIN      0x3b8000UL
#define OMO_IO_BYTES    0x120000UL      /* region 3 size, docs/phase18/inbound-map.md row 3 */
...
omo_rel = ioremap(omo_bar0_base + OMO_IO_WIN, OMO_IO_BYTES);
...
#define OMO_IRQ_BLOCK   0x161100UL      /* region-3 offset of CA 0x40161100 */
...
pre[i] = omo_rd(omo_rel, OMO_IRQ_BLOCK + i * 4);
```

So the address is `ioremap(BAR0 + 0x3b8000, 0x120000) + 0x161100`. The mapping covers
`0x…0000`-`0x…120000`; the read is at offset `0x161100`, which is

```
0x161100 - 0x120000 = 0x41100   bytes past the end of the mapping
```

and past the one-page vmalloc guard that follows it. It is an out-of-bounds kernel read; the value it
returns is whatever kernel mapping sits at that virtual address, never the device's interrupt block.
(`0x161100` is the one region-3 offset the module never range-checked; its own other region-3
offsets - `0x108` release, `0x392f0` ack, `0x39000…` message, `0x101414`/`0x101438` - are all
`< 0x120000` and in bounds.)

### D.2 The returned words are the firmware image

The dmesg (evidence `build/register-dumps/exp/20261004-052201/`, `.../20261004-053006/`; identical in
both boots):

```
[intrsamp] pre: irqblock=41f0e8bd b872f7fa 3023f890 bf282b01 429d2301 f7f9d29e b148fbf8 30a9f894 ack(out5)=00000000
```

Those words are printed as 32-bit values; as little-endian bytes they are
`bd e8 f0 41 fa f7 72 b8 90 f8 23 30 01 2b 28 bf 01 23 9d 42 9e d2 f9 f7 f8 fb 48 b1 94 f8 a9 30`.
That 32-byte string is exactly `FIRMWARE.bin[0x40100..0x40120]`:

```
FIRMWARE.bin[0x40100]: bde8f041faf772b890f82330012b28bf01239d429ed2f9f7f8fb48b194f8a930
```

`FIRMWARE.bin` is the device image (928,920 B, md5 `0e530b976d5a20e87358671f1a577695`); runtime address = file offset
`+ 0x40000` (phase 4/6/8; `docs/phase22/fw-hostmem.md` §0), so file `0x40100` is device CA `0x80100`.
The live BAR0 dump holds the same bytes verbatim, at region 0's identity offset and at the
region-5 alias:

```
BAR0+0x80100  (region 0,  device CA 0x00080100): bde8f041faf772b890f82330012b28bf01239d429ed2f9f7f8fb48b194f8a930
BAR0+0x738100 (region 5,  device CA 0x1280100):  bde8f041faf772b890f82330012b28bf01239d429ed2f9f7f8fb48b194f8a930
```

(`0x738100 == 0x80100 + 0x6b8000`, the region-5 base - the two copies are the same SRAM, the
`dev 0x1240000 / dev 0x40000` alias of phase 17.)

So the eight "interrupt block" words are **the firmware's own code text**, read through an adjacent
kernel mapping of the same on-chip memory. Static image bytes explain every property that puzzled
phase 31: *stable* (the image does not change), *code-like* (`0xe8bd` is the Thumb `pop`), and
*unaffected by the doorbell* (it is not a register).

### D.3 Why it is certainly not region 4

Had the read landed on region 4 (device CA `0x02041100`, what BAR0 `0x519100` truthfully maps to), it
would have returned the zero bytes of §C. It returned firmware code instead. The two facts together
prove the read is out of bounds and reaches neither region 4 nor the interrupt block.

## E. What this retracts and what still stands

**Retracted.** Phase 30's statement "the block is readable through EP0's IO region (BAR0 `0x3b8000` +
`0x161100` = `0x519100`)" is wrong on both halves: CA `0x40161100` is outside region 3, and the
`0x519100` address is not a valid mapping of it. Phase 30's follow-on inference ("the bitmap is
transient … or write-only from the host's side") and phase 31's "the block's init state differs
between a working boot and a takeover" are both unsupported: the read never touched the block, and
the normal-operation zero was region 4 ACP SRAM.

**Still stands.** The genuine negative is the ack, `out[5]` CA `0x400392f0`, which *is* in region 3
and *is* read every iteration of the same probe: it stays `0` across the doorbell (values 1 and 8),
in both experiments. So "the doorbell produces no observable firmware dispatch" survives; only the
"interrupt block" half of the evidence was an artifact. The chain - ctx armed, H2D handler table
`0x118d68` armed, interrupt fn array `0x17d430` armed (`id 0x2d = 0x62f9`, `id 0x2e = 0x624d`),
dispatcher `0x818ac` never entered - is unchanged.

Consequently the interrupt block is **unobservable from the host**, which is stronger than "reads
zero": there is no host alias to read. Any future claim about the enable bitmap or the priority block
must account for the fact that neither CA is in the host's window.

## F. If the block is ever to be reached

It is reachable only by adding a window, not by reading a bigger offset: the endpoint has 16 inbound
iATU viewports and the vendor programs 6, so a spare viewport could in principle be targeted at
device `0x40160000`. The record never does this, the vendor never does this, and the firmware's own
CPU address map (`0x40161100` is its literal) is not by itself proof that the PCIe inbound path
decodes that range. Until such a viewport is programmed and validated (and it must not disturb the
six named windows or calibration), the interrupt block's state can only be inferred indirectly.

---

### Method and files

- Read-only: the region table, the viewport programming, the measured dmesgs and the live BAR0 dump
  already in the repo. No router access, no device writes.
- Sources: `opensource/docs/phase17/fw-download.md` §A.6, `opensource/docs/phase18/inbound-map.md`
  §A.3-A.6, `opensource/docs/phase20/fw-accept.md` (mailbox CAs), `opensource/docs/phase22/fw-hostmem.md`
  (ctx CA table), `opensource/docs/phase30/the-gate-located.md`, `opensource/docs/phase31/*`,
  `opensource/lab/wifidrv1/wifidrv1.c`, `opensource/build/register-dumps/barmap_ep0_bar0.bin`,
  `build/register-dumps/exp/20261004-052201/` and `.../20261004-053006/`, `build/tmp/FIRMWARE.bin`
  (md5 `0e530b976d5a20e87358671f1a577695`).
