# TABLES - lane wifi-band1-start-send-2 (task-207)

Firmware CPU address (CA) = file offset + 0x40000 of the pinned local
FIRMWARE.bin (928,920 B, sha256 `7fc87e20...311b`).  Host module offsets are
link-time `.text` offsets.  Grades as in REPORT.md.

---

## T1. The key-0x0064 g0-id1 frame fields [V]

Carrier: **12-B HCC header | 8-B token | key u16 | len u16 | body(20) |
4 x FF tail**, total 48 B, hdr total field 0x30, node word1 0x00306d2b.

| frame offset | width | field | fire value (index 0, band 1) |
| ---: | ---: | --- | --- |
| hdr+0x00 | u8 | group 0 | 0x00 |
| hdr+0x01 | u8 | alloc 1 / dst 0 | 0x01 |
| hdr+0x02 | u8 | fa (inherited) | 0x00 |
| **hdr+0x03** | u8 | **field B = VAP/module index** (dispatcher ctx) | **0x00 = vap_add_index** |
| hdr+0x04 | u16 | total = 0x30 | 48 |
| hdr+0x06 | u16 | message id 1 | 1 |
| hdr+0x0a | u16 | tag 0x5a5a | 0x5a5a |
| hdr+0x14 | u16 | key 0x0064 | 0x0064 |
| hdr+0x16 | u16 | body length 20 | 20 |
| body+0x00 | u8 | create type (2 = AP) | 0x02 |
| body+0x01 | u8 | parent module (self-parent) | 0x00 = index |
| **body+0x04** | u8 | **module/VAP index created (ctx lookup CA 0x961AE)** | **0x00 = vap_add_index** |
| body+0x12 | u8 | band selector (1 = band 1) | 0x01 |

Byte-exact KATs (evidence/frame.txt):

    index 0: 000100003000010000005a5a0000000001000000640014000200000000000000000000000000000000000100ffffffff
    index 4: 000100043000010000005a5a0000000001000000640014000204000004000000000000000000000000000100ffffffff

## T2. The key-0x0242 field-B agreement [V]

The 0x0242 worker CA 0x4C524 takes ctx = CA 0xFE578(0, hdr[3]) and the band
object from ctx+0x4B8 - the object the key-0x0064 bind stored.  The start
frame's field B therefore must equal the 0x0064 data[4]/hdr[3] index.  This
lane's host test builds the 33-B primary frame for every index 0..0x12 and
asserts hdr[3] == index (plus total 33, key 0x0242, body {01,00,01,00,00}).

## T3. Firmware code sites this lane's decode relies on [V]

| CA | role |
| ---: | --- |
| 0x43D58 | g0-id1 dispatcher: ctx = CA 0xFE578(0, hdr[3]) before the worker |
| 0x449E4 | key-0x0064 worker: body[0] create type, body[1] parent gate (parent_ctx[1] must be 0), body[4] -> 0xBC726 refcount -> CA 0x961AE ctx lookup; bind CA 0x44BBC-0x44BC6 (ctx+0x4B0/+0x4B8/+0x4B4); band VAP list append CA 0x44BF8-0x44C32 (bandobj+0x12 count, bandobj+count = data[4]); the branch requires `*(bandobj+0x1A00)` published != 0 (CA 0x44B6C-0x44B7C) |
| 0x4C524 | key-0x0242 worker; subcmd 1 -> CA 0x4C3FC before the start gate |
| 0x4C3FC | the only writer of state[4]: success CA 0x4C448 (`strb r0,[r4,#4]` from CA 0x4AE7C(bandobj)[0]), failure CA 0x4C4EE-0x4C4F0 (**0xFF**) |
| 0x4AE7C | band VAP resolver: reads bandobj+0x12 and needs exactly 1; resolves the entry, band gate CA 0x944DE, RF programme CA 0x101890, then returns the VAP or 0 (CA 0x4AEB4-0x4AEE2) |
| 0x4AF0C | per-band MAC start; refuses state[1]==2 (0x4AF2C) and state[4]==0xFF / unresolvable (0x4AF32) |

## T4. Fail-closed matrix (host-tested) [V]

| input | result |
| --- | --- |
| `vap_add_index` = 0, `vap_start_module` = 0 (fire) | allowed; 0x0064 bytes index 0, field B 0; all frame bytes as T1 |
| `vap_add_index` = 4, `vap_start_module` = 4 | allowed; data[1]/data[4]/hdr[3] = 4 |
| `vap_add_index` > 0x12 | start_ap REFUSED before any frame |
| `vap_add_index` != `vap_start_module` | start_ap REFUSED before any frame |
| builder called with module != the parameter | provider -EINVAL; start aborted |
| plan band > 1 / unknown iftype / NULL out | -EINVAL, no frame |

## T5. Pins and artifact [V]

| pin | value |
| --- | --- |
| fired base tree | /tmp/bandstart-build (hi5622_scan.c `272e47ac...`, hi5622_vapscan_provider.c `c306e75f...`, vapstart_frames.c `7dc7bc35...`, hi5622_band_start.c `71019718...`) |
| fired base artifact | /tmp/bandstart-artifact/hi5622_wifi.ko, 371,212 B, sha256 `e2312402...a028`, 63 params |
| lane patch | patches/0001-vap-add-index.patch sha256 `968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb` |
| lean artifact | /tmp/vapbound-artifact/hi5622_wifi.ko, **373,056 B**, sha256 `b8f6fe35...5b71`, **64 params**, .text `ff4fb171...` |
| host tests | 283 checks / 0 failures |

## T6. Read-only probe pack for the fire (inherited; corrected addresses) [V\*]

| # | object | phys | expect |
| ---: | --- | ---: | --- |
| P0 | new dmesg line | - | `cfg80211: vap_add post: index=0 band=1 ... bandobject=nonzero bandlist_count=1 (...)` |
| P1 | band-1 arm word 0x4010C220 | **0x404C4220** (NOT 0x404C8220) | **0x1029** after a successful start |
| P2 | G+0x370 (the NULL hook) | 0x4082CB34 | 0 |
| P4 | band-0 arm word 0x40108214 | 0x404C0214 | 0xF |
| P9 | band-1 run-state (bandobj 0x15E570 + 0x14D0) | **0x40817A40** | u8+1 = 2, u8+4 = the VAP index (0, not 0xFF), u32+0x64 incremented |
| P12 | RF image R+0x3A8/0x3AC (F2 context) | two-step via perlink+0x2D8 | 0x01800060 / 0x03E6138A |
| P13 | on-air 5200 MHz beacon | observer scan freq 5200 | ch40 beacon / SSID |

## T7. Data-path gates the readback exposes [V]

The key-0x0124 channel worker bails on ctx+0x4B0 == 0 before it stores its
body; the key-0x0242 worker returns without a start when ctx+0x4B8 == 0.
So `vap_add post: ... perlink=0x00000000` / `bandobject=0x00000000` are
sufficient to explain BOTH the missing state[4] and a missing channel
programme - the fire's readback line settles it in one boot.
