# TABLES - the post path, the builders, the routing structures and the host conversion

**Lane:** wifi-rx-d2h-post (task-226). CA = file offset + 0x40000. Every row below is asserted
by `tools/fw_rx_d2h_post_re.py --selftest` (146/146 PASS). Grades: [V] re-derived here,
[V\*] committed earlier lane, [I] inference, [U] needs a device read.

## T1 - hcc_msg_tx CA 0x102760 (queue enqueue) [V]

| CA | instruction | meaning |
| ---: | --- | --- |
| 0x102760 | `push {r4,r5,r6,lr}` | r5 = msg |
| 0x102776 | `bl 0x409EA` | fetch the 12-byte HCC header |
| 0x102782 | `ldrb r3,[r0,#1]; ubfx r3,r3,#4,#4; strb r3,[r0,#9]` | dest nibble = hdr[1]>>4 -> hdr[9] |
| 0x10279A | `mov.w r3,#0x448; movs r2,#0x18; ldrb r1,[r0,#9]; smlabb r2,r2,r1,r3` | dest_record = 0x171B6C + 0x448 + 0x18*dest |
| 0x1027B2 | `bl 0x102744` | group resolver -> group record |
| 0x1027BA | `ldrh r1,[r4,#6]; ldr r3,[r0]; cmp r1,r3; ldr r3,[r0,#4]; add.w r3,r3,r1,lsl #4; ldr r1,[r3,#8]; adds r1,#1; str r1,[r3,#8]` | id = u16[hdr+6]; if id < max_id: item[id].stats(+8)++ |
| 0x1027CE | `ldr r1,[r0,#8]; mov r0,r2; bl 0x1024CE` | queue index = group_record[+8]; queue object lookup |
| 0x1027E2 | `bl 0xfdf8e; add.w r1,r4,#0x40; mov r0,r5; bl 0x102662; bl 0xfdf50` | lock; list_add_tail(queue+0x40, msg); unlock |
| 0x1027FA | `ldr.w r4,[r4,#0xc0]; ... bl 0x1024BC; adds r0,r4,#4; bl 0xC2A54` | notifier = queue+0xC0; if set, flag notifier+0x48=1 and schedule |
| 0x102770 | `movw r0,#0x8b2d` | failure return |

Supporting functions [V]:

| CA | function | facts |
| ---: | --- | --- |
| 0x102744 | group resolver | `map=[dr+4]`; `groups=[map+0x10]`; `group_rec=[dr+0x10]+0xC*group`; returns 0 if group out of range |
| 0x1024CE | queue-array indexer | `base=[dr+8]`; `count=[map+8]`; stride **0xE4**; returns 0 out of range |
| 0x102662 | intrusive list append | list struct at queue+0x40 (next/prev/count) |
| 0x1024EA / 0x1024E6 | list pop head / free | used by the firmware worker and the CRX loop |
| 0xFD7E8 | firmware-local dispatcher | group=u8[hdr]&0xf; id=u16[hdr+6]; resolver 0xFD4C8; `bx [item+4]` |
| 0xFD4C8 | local table resolver | same map/group-record/item layout as 0x102744 |
| 0x102866 | table registrar | copies a 12-B input table into item[id] = {id, handler} of the *destination-0* map; only groups 0/2 are registered |

Queue object layout (constructed at CA 0x47F14-0x47F68) [V]:

| queue offset | meaning |
| ---: | --- |
| +0x00 | 0 |
| +0x04 | per-queue config ptr |
| +0x08 | state (initialised 1) |
| +0x0C | lock |
| +0x10 | 0x104294 |
| +0x18 | 1 |
| +0x40 | message list head (next/prev/count at +0x40/+0x44/+0x48) |
| +0x48 | list count (also the word set by the notifier flag at 0xC0+0x48) |
| +0x80 | second list |
| +0xC0 | notifier (initialised NULL, installed by the customer registration) |
| +0xC4 | notifier-set flag |
| +0xC8 | 0 |

## T2 - the transport deposit path [V]

| CA | function | facts |
| ---: | --- | --- |
| 0x10CEDC | ETE ops table | +0 = 0x4003A000, +4 = 0x40039508, **+0x10 = 0xC61E1 (deposit)** |
| 0xC61E0 | D2H deposit op | node = {buf dev VA, (len<<16) | 0x6D2B (present) / flags}; calls 0xC192A; d2h_notify(chip0, 6); ring_rec->[0x340]++ |
| 0xC192A | ring commit | base=[rec]; idx=[rec+0xC]&0x3FF; base[8*idx]=node.w0; base[8*idx+4]=node.w1 |
| 0xC1906 | producer advance | packed producer: index bits 0..9, phase bit 10 toggles on wrap |
| 0xC6170 | d2h_notify | id <= 0xA, chip 0; lock g+0xC0; in-flight latch g+0xB4; mask -> *(g+0x9C) = out[1]; *(g+0xA4) |= 1 = doorbell; deferred shadow g+0xB8 |
| 0xC6108 | deferred flush | pulse g+0xB0; spin while out[1] != 0; publish g+0xB8 |
| - | observed notify ids | 6 = D2H data staged; 5 = SR receive drain; 3 = host-interface event tail-call [V\*] |

**No per-message-type branch in this half** [V]: the notify id is a mailbox bitmap bit, not the
HCC group/id; the ETE node carries no type/source discriminator; the doorbell mask is constant.

## T3 - builders: group / id / destination nibble [V]

| class | builder CA | group | id | dest nibble source | post |
| --- | ---: | ---: | ---: | --- | ---: |
| SMAC RX-complete (g2 id3) | 0xB53A4 | 2 | 3 | `u8[band_obj]` @0xB5472 | 0xB5492 |
| heartbeat (g4 id4) | 0xC6B96 | 4 | 4 | **0** (bfc #4,#4 @0xC6BDA) | 0xC6C0A |
| ready pair (g4 id1/id2) | boot task | 4 | 1/2 | 0 (host checks hdr[1]>>4 == 0) [V\*] | [V\*] |
| config replies (g1 id3) | hmac | 1 | 3 | 0 (host-visible) [V\*] | [V\*] |
| CRX RX (g1 id13) | 0xB9780 | 1 (sb @0xBA0AA) | 13 (0xBA150) | dest-table byte from 0xBC524 (0xBA15C) [U] | 0xBA17A |

SMAC producer field table (body 0xB53A4) [V]: hdr[0] group=2 (0xB5468), hdr[6]=3 (0xB5470),
hdr[1] high = u8[band_obj] (0xB5472/0xB5476), hdr[2] = u8[band_obj+2] (0xB547C/0xB5480),
hdr[3] = 0x12 (0xB5482), hdr[8] = 0 (0xB547E), hdr[0x13] = 1 (0xB548A), hdr[0x14] = band_obj
(0xB5488), hdr[0xc] = received-frame chain (0xB548C), hdr[0x1d] = 0 (0xB548E).

CRX builder field table (0xB9780/0xBA0AA-0xBA17A) [V]: dequeue 0x1024EA (0xBA0C0),
frame_len = u16[rxinfo+0x11] (0xBA0F8), forward/dest via 0xBC524 (0xBA108), total cap
frame_len+0xC <= 0x680 (0xBA110-0xBA118) else band+0xCC5++ (0xBA12C), group=1 (0xBA14A),
id=13 (0xBA150/0xBA152), total=frame_len+0x3C (0xBA146), hdr[1] high = u8[dest_rec+2],
hdr[2]=u8[dest_rec+1], hdr[3]=u8[dest_rec] (0xBA15C-0xBA172), post 0x102760 (0xBA17A).

## T4 - runtime structures addressed by the probes [V decode + U read]

| object | CA | fields |
| --- | ---: | --- |
| hcc instance | 0x171B6C | +0x448 dest-0 record base |
| dest record 0 | 0x171FB4 | +0 dest index; +4 map ptr; +8 queue-array ptr; +0xC second-array ptr; +0x10 group-array ptr; +0x14 third-array ptr |
| queue array (dest 0) | 0x171B70 | +0xE4*qidx; q0 = 0x171B70, q1 = 0x171C54, q2 = 0x171D38 |
| group array (dest 0) | 0x171F54 | 12-byte records {max_id, item_array, qidx u16} |
| band-0 object | 0x174BF0 | getter 0xF4F48: 0x174BF0 + 0xE28*band; +0xCC0 producer entry counter; +0xCC5 CRX drop; +0xCCE post-failure counter; +0x2A0 SMAC base; +0x2A0 -> +0xE4 ack; +0x2A0 -> +0xE8 status |
| band-1 object | 0x175A18 | same +0xE28 offset |
| item array (group 2) | P16 | 16-byte entries {u32 id, u32 handler, u32 aux, u32 stats}; entry[3] = {3, 0xBBC9C, ..., stats} |
| ETE ops table | 0x10CEDC | +0x10 = deposit 0xC61E0 |
| D2H notify object | *(0x172130) | +0x9C out[1] value reg, +0xA4 doorbell reg, +0xB0 ack pulse, +0xB4 latch, +0xB8 deferred shadow, +0xC0 lock |

## T5 - host conversion and probe formulas

| window | formula | examples |
| --- | --- | --- |
| firmware RAM (region 5) | phys = CA + **0x6B8000** | dest0 0x171FB4 -> 0x8283B4; band0 0x174BF0 -> 0x82CBF0 |
| device IO (region 3) | phys = CA + **0x3B8000** (CA 0x40000000..0x4011FFFF only) | out[1] 0x40039014 -> 0x403C1014 |
| PKTRAM (region 2) | phys = 0x1D8000 + (CA - 0x01000000) | - |
| ACP (region 4) | phys = 0x4D8000 + (CA - 0x02000000) | - |

## T6 - host D2H acceptance (vendor modules; committed lanes) [V\*]

| layer | fact |
| --- | --- |
| mailbox dispatch | `pcie_msg_init` registers D2H ids {1,3,6,7}: id3 = drain DR, id6/7 = wake worker; ids 2/4/5/8/9/10 dropped |
| D2H order | ack out[3]=1; drain out[1]=0; re-arm out[4]=1; then dispatch lowest set bit first |
| message families | plat.ko chip group 4 table (5 ids 0..4); wifi.ko hmac chip groups 1 (28), 2 (12), 3 (8); wal chip group 2 (19) |
| unknown family | logged as `frame_unknown` (`HCC_EVENT_FRAME_UNKNOWN`); the raw DR frame is still readable by the direct catalogue |
