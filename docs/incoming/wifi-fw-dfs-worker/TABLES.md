# TABLES - addresses, dispatcher, worker, contexts, frames, probes

All CPU addresses (firmware = file + 0x40000). BAR0 = CPU + 0x6B8000; host phys
= 0x40000000 + BAR0. Everything here is re-derived by
`tools/fw_dfs_worker_re.py --selftest` (142/142) from the pinned local
artifacts.

## T1. Dispatch of a g0-id1 config event [V]

| element | address / value | fact |
| --- | --- | --- |
| g0-id1 handler | CA 0x43D58 | reads field B at hdr+3, key u16 hdr+20, len u8 hdr+22, body hdr+24 |
| message validation | CA 0x409EA | pool membership + class `hdr[5]>>1&7 <= 3`; failure -> no worker |
| field-B lookup | CA 0xFE578 | `ctx = u32(G+0x630+4*fieldB) + u16(G+0x6DC)`; fieldB 0..0x12 |
| G (user-manager) | CA 0x170E28 | boot memset 0x700 + 19 context pointers |
| table A | CA 0x10C23C | 217 x 8 B `{u16 key; u16 pad; u32 worker\|thumb}`, bound 0x6C8 |
| table B | CA 0x103BF8 | 88 x 8 B, bound 0x2C0 (searched second) |
| row 93 | CA 0x10C524 | **key 0x01F7**, pad 0, worker **0x0B5B9D** |
| row 92 | CA 0x10C51C | key 0x00F7, pad 0, worker 0x045A31 |
| worker call | CA 0x43DF2 `blx r3` | `worker(r0=ctx, r1=u8 len, r2=hdr+24)` |
| length gate | none | there is no length/flag/checksum compare in the dispatcher |

## T2. Worker CA 0xB5B9C (key 0x01F7, the only band+0xF04 writer) [V]

| CA | encoding | meaning |
| ---: | --- | --- |
| 0xB5B9E | `d0 f8 b8 44` | `ldr.w r4,[r0,#0x4B8]` band object |
| 0xB5BA6 | `04 f5 d0 56` | `add.w r6,r4,#0x1A00` |
| 0xB5BAA | `30 68` | `ldr r0,[r6]` published per-link; 0 -> log **1442**, ret 0x64 |
| 0xB5BC8 | `94 f8 70 1d` | `ldrb.w r1,[r4,#0xD70]` attach; `cmp #1`; !=1 -> log **1448**, ret 0 |
| 0xB5BE2 | `15 78` | `ldrb r5,[r2,#0]` mode = body[0] |
| 0xB5D72 | `df e8 03 f0` | `tbb [pc,r3]` (r3 = mode-1) |
| 0xB5D76 | `9b 90 9b 97 90` | tbb entries: m1->0xB5E10, m2->0xB5E06, m3->0xB5E10, m4->0xB5E0C, m5->0xB5E06 |
| 0xB5D1C | `84 f8 04 5f` | **`strb.w r5,[r4,#0xF04]`** (mode byte; only writer in the image) |
| 0xB5CEE | `00 f2 c3 10` | `addw r0,r0,#0x1C3` (mode-word destination in the published per-link) |
| 0xB5CF2 | `0c f0 a7 f8` | `bl CA 0xC1E44` memcpy_s(dest=O+0x1C3, destMax=4, src=stack word, count=4) |
| 0xB5E78 | `03 2d` | mode 3 branch: `cmp r5,#3` |
| 0xB5E7C | `d3 f7 52 fd` | mode 3: `bl CA 0x89924` (ch36..64 image; takes O = published per-link) |
| 0x89924 | `38 b5 42 4b` | push; image literal |
| 0x89928 | `d0 f8 d8 42` | `ldr.w r4,[r0,#0x2D8]` R = `*(O+0x2D8)` |

Mode map: 1/2/5 have explicit image blocks; 3 calls CA 0x89924; 4 falls
through to the common block; outside 1..5 logs 1485. In all cases the mode
byte is stored before the tail dispatch.

## T3. Boot-created context table [V]

`G = 0x170E28`; init CA 0x49204: memset G (0x700 B), then CA 0x49242..0x49264
fills 19 slots `*(u32*)(G+0x630+4k) = 0x119758 + k*0xB40` (end 0x126D18) and
memsets each 0xB40 B body; CA 0x4923A stores the single u16 common offset at
G+0x6DC.

| k | table addr | ctx (base 0) | ctx+0x4B8 | ctx+0x190 |
| ---: | ---: | ---: | ---: | ---: |
| 0 | 0x171458 | 0x119758 | 0x119C10 | 0x1198E8 |
| 1 | 0x17145C | 0x11A298 | 0x11A750 | 0x11A428 |
| 2 | 0x171460 | 0x11ADD8 | 0x11B290 | 0x11AF68 |
| 3 | 0x171464 | 0x11B918 | 0x11BDD0 | 0x11BAA8 |
| 4 | 0x171468 | 0x11C458 | 0x11C910 | 0x11C5E8 |
| 5 | 0x17146C | 0x11CF98 | 0x11D450 | 0x11D128 |
| ... | ... | (+0xB40 each) | ... | ... |
| 18 | 0x1714A0 | 0x1261D8 | 0x126690 | 0x126368 |

The common offset (u16 at 0x171504) is added to every slot; the probe reads it
so the Lead can adjust the addresses. Boot value [I] 0 (PD4 report; the F1
fire readback `bandobject=0x15e570` is consistent with base 0).

## T4. Who binds a context's band object [V]

The key-0x0064 worker CA 0x449E4 (VAP-add / ctx-create) after creating the
module from `body[4]` binds, from `body[0x12] = band`:

```
ctx+0x4B0 = band published per-link   (=(u32)(bandobj+0x1A00))
ctx+0x4B4 = third field
ctx+0x4B8 = band object = 0x15CA08 + 0x1B68*band
```

Band objects: band 0 = 0x15CA08, band 1 = 0x15E570. Accessor CA 0xA4FC8.

| binding | field B | body[0x12] | ctx+0x4B8 | ctx+0x4B0 |
| --- | ---: | ---: | ---: | ---: |
| boot C2 `h2d_ctx_create` (key 0x64, module 4) | 4 | 0 | **band 0 0x15CA08** | 0x174BF0 |
| AP-start `vap_add` (key 0x64, module 0, band 1) | 0 | 1 | **band 1 0x15E570** | 0x175A18 |

The module-4 row is [V*]: committed wifi-vap-start bench readback
"module-4 ctx+0x4B0 = 0x00174BF0, ctx+0x4B8 = 0x0015CA08".

## T5. Read-only probe pack [U live / model V]

The F1 AP is still up; these are read-only reads of firmware RAM through the
driver/PCIe window (BAR0 = CPU + 0x6B8000, phys = 0x40000000 + BAR0).

| # | read | CPU | phys | expected |
| ---: | --- | ---: | ---: | --- |
| 1 | u16 G+0x6DC (common offset) | 0x171504 | 0x40829504 | 0x0000 [I] |
| 2 | u32 ctx-table slot0 | 0x171458 | 0x40829458 | 0x00119758 |
| 3 | u32 ctx-table slot4 | 0x171468 | 0x40829468 | 0x0011C458 |
| 4 | **u32 ctx4+0x4B8** | **0x11C910** | **0x407D4910** | **0x0015CA08 (H2) / 0 (H1)** |
| 5 | u32 ctx0+0x4B8 | 0x119C10 | 0x407D1C10 | 0x0015E570 |
| 6 | u8 band1+0xD70 attach | 0x15F2E0 | 0x408172E0 | 1 |
| 7 | u32 band1+0x1A00 published | 0x15FF70 | 0x40817F70 | 0x00175A18 |
| 8 | u8 band1+0xF04 mode | 0x15F474 | 0x40817474 | 0 (about to become 3) |
| 9 | u32 band1 R = *(O+0x2D8) | 0x175CF0 | 0x4082DCF0 | 0x400B2000 |
| 10 | **u8 band0+0xF04 mode** | **0x15D90C** | **0x4081590C** | **3 => H2 (the worker ran on band 0); 0 => H1** |
| 11 | u8 band0+0xD70 attach | 0x15D778 | 0x40815778 | 1 |
| 12 | u32 band0+0x1A00 published | 0x15E408 | 0x40816408 | 0x00174BF0 |
| 13 | u32 band0 R = *(O0+0x2D8) | 0x15E6E0 | 0x408166E0 | nonzero; R0+0x3A8/0x3AC should show the ch36..64 image if H2 |
| 14 | u32 ctx4+0x190 | 0x11C5E8 | 0x407D45E8 | nonzero (C2 create ran) |
| 15 | u32 ctx0+0x190 | 0x1198E8 | 0x407D18E8 | nonzero (AP vap_add ran) |

If #1 is non-zero, add it to every 0x11xxxx (context-derived) address.
Predicted under H2 after the F1 fire: #4 = 0x0015CA08, #10 = 3, #13 shows the
image; band 1 (#8/#9) unchanged. Under H1: #4 = 0, #10 = 0.

## T6. The 29-byte frame: fired vs fixed [V bytes]

Both frames are *our* host-built protocol bytes; they differ in exactly one
byte: hdr[3] (field B).

```
current (fired, field B=4): 00 01 00 04 1d 00 01 00 00 00 5a 5a
                            00 00 00 00 01 00 00 00 f7 01 01 00 03 ff ff ff ff
fixed   (field B=0):        00 01 00 00 1d 00 01 00 00 00 5a 5a
                            00 00 00 00 01 00 00 00 f7 01 01 00 03 ff ff ff ff
```

Hex: current `000100041d00010000005a5a0000000001000000f701010003ffffffff`;
fixed `000100001d00010000005a5a0000000001000000f701010003ffffffff`.
hdr total 0x001D = 29 B (len + 28). Key u16 at hdr+20, len u16 (low byte read
by the handler) at hdr+22, body at hdr+24, inherited 4-byte 0xFF tail.
The 0x00F7 desc-table frame needs the same hdr[3] change (68 B, b[3]).

## T7. Vendor-side joins [V]

| element | module address | fact |
| --- | ---: | --- |
| header builder | wifi.ko .text+0x57090 | hdr[2]=ctx[1], hdr[3]=ctx[0], hdr[1] high nibble=ctx[2] |
| config-event sender | .text+0x5717C | key at hdr+20, len hdr+22, data hdr+24 |
| country/DFS emitter | .text+0x5762C `hmac_config_set_country_for_dfs` | `movw r1,#0x01F7` at +0x5763C then `bl hmac_config_send_event`; payload forwarded verbatim |
| header helper raw | 0x57090: f0 41 2d e9; 0x570EC: 02 10 d5 e5; 0x570F0: 11 22 c7 e7; 0x570FC: 01 10 d5 e5; 0x57100: 02 10 c4 e5; 0x57104: 00 10 d5 e5; 0x57108: 03 10 c4 e5 | proves hdr[3] = ctx[0] |
| emitter raw | 0x5762C: 30 40 2d e9; 0x5763C: f7 11 00 e3; 0x57644: fe ff ff eb | `movw r1,#503`, `bl 0x5717C` |
