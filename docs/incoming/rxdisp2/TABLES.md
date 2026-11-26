# TABLES - the local-dispatch wiring, the CRX gates and the probe conversions

**Lane:** rxdisp2 (task-242). CA = file offset + 0x40000. Every row is asserted by
tools/fw_rxdisp2_re.py --selftest (76/76 PASS). Grades: [V] re-derived here, [V*]
committed earlier lane, [I] inference, [U] needs a device read.

## T1 - the chain sites (dest 0, band 0)

| stage | CA | facts | grade |
| --- | ---: | --- | --- |
| SMAC producer (g2 id3) | 0xB53A4 body | group=2 (0xB5462/0xB5468), id=3 (0xB546E/0xB5470), dest nibble = u8[band] (0xB5472-0xB5476), hdr[0xC]=frame chain (0xB548C), hdr[0x14]=band (0xB5488), post 0x102760 (0xB5492); post-fail band+0xCCE++ (0xB5442) | [V*] |
| enqueue | 0x102760 | dest rec = 0x171B6C+0x448+0x18*dest (0x10279A); group resolver 0x102744 (0x1027B2); item stats++ (0x1027BA); queue = 0x1024CE(dest, group_rec[+8]) (0x1027CE); lock + list_add_tail(queue+0x40) (0x1027E2); notifier=queue+0xC0 -> flag notifier+0x48=1 + post 0xC2A54(notifier+4) (0x1027FA) | [V] |
| queue indexer | 0x1024CE | base=[dest+8], count=[map+8], stride 0xE4 | [V] |
| local handler tables | 0x102866 | copies a 12-B {u16 id; u16 pad; u32 handler(thumb); u32 aux} table into item[id] of dest0 group array; registrations: boot task 0x48336 -> group 2, 19 entries, 0x10CC18; 0x48340 -> group 0, 40 entries, 0x10CCFC; hcc init -> group 0, 1 entry, 0x10CBEC | [V] |
| g2 table | 0x10CC18 | 19 entries; id2 -> 0xB9781 (builder), id3 -> 0xBBC9D (consumer), id5 -> 0xB9781 (builder) | [V] |
| consumer | 0xBBC9C | band = hdr[0x14] (0xBBCB8); chip = 0xA4FC8(u8[band], u8[band+1]) (0xBBCC0); splice hdr[0xC] chain (0xBBCCE-0xBBCF0); 0xBA1C8(chip, band, list) (0xBBD0C); on 0x8B2D re-arm PPDUs 0xB52F0 (0xBBD40) | [V] |
| per-descriptor decoder | 0xBA1C8 | band+0xCC1++ (0xBA240-0xBA248); 0xB2C5C forward prep, fail -> +0xCCC++ (0xBA3FC-0xBA402); g2/id2 report msg alloc 0x102822, fail -> +0xCCD++ (0xBA41E-0xBA426); fills hdr[0x14]=band, hdr[0xC]=descriptor; calls 0xB9780 (0xBA48A); frees the wrapper (0x102820) | [V] |
| CRX builder | 0xB9780 | G1 [chip+0x1A00] (0xB97EE); forward flag/class nibble (0xB9816-0xB9832); class nibble gate (0xB9864-0xB98B2); per-frame loop (0xBA0A0); frame_len (0xBA0F8); fwd idx (0xBA0FC); dest 0xBC524 (0xBA108); cap 0x680 (0xBA110); g1/id13 (0xBA14A/0xBA150); dest bytes (0xBA15C-0xBA172); MPDU copy 0x4BDCE (0xBA174); post 0x102760 (0xBA17A) | [V] |
| chip resolver | 0xA4FC8 | chip = 0x15CA08 + 0x1B68*u8[band+1]; requires u8[band] == 0 (else log 0x361, return 0) | [V] |
| RX ctx installer | 0x48A68 | str.w sl,[chip+0x1A00]; chip+0xD70 = min(ctx[1],2) (0x48A70-0x48A7E) | [V] |
| fwd/dest resolver | 0xBC524 | index <= 0x12; dest = u16[0x170E08+0x6FC] + u32[0x170E08+0x650+4*idx] (0xBC54A-0xBC558) | [V] |
| D2H transport | 0xC61E0 / 0xC6170 | ETE deposit -> ring commit -> d2h notify id 6 (prior lanes) | [V*] |

## T2 - dest0 runtime wiring (static, from the init immediates) [V]

| object | CA | phys (region 5) | derivation |
| --- | ---: | ---: | --- |
| queue array | 0x171B70 | 0x40829B70 | hcc instance 0x171B6C + 4 |
| q0 | 0x171B70 | 0x40829B70 | +0*0xE4 |
| q1 | 0x171C54 | 0x40829C54 | +1*0xE4 |
| q2 | 0x171D38 | 0x40829D38 | +2*0xE4 |
| q0+0xC0 (notifier slot) | 0x171C30 | 0x40829C30 | expect 0x171E1C |
| **q1+0xC0 (notifier slot)** | **0x171D14** | **0x40829D14** | **expect 0x171E84** |
| q2+0xC0 (notifier slot) | 0x171DF8 | 0x40829DF8 | expect 0x171EEC |
| customer array | 0x171E1C | 0x40829E1C | instance + 0x2B0 |
| customer0 | 0x171E1C | 0x40829E1C | "HCC TX Thread" (entry 0xC515E) |
| **customer1** | **0x171E84** | **0x40829E84** | **"HCC RX Thread" (entry 0x102552)** |
| customer2 | 0x171EEC | 0x40829EEC | "HCC OAM Thread" (entry 0xC515E) |
| group array | 0x171F54 | 0x40829F54 | instance + 0x3E8 |
| dest0 record | 0x171FB4 | 0x40829FB4 | instance + 0x448 |
| band0 object | 0x174BF0 | 0x4082CBF0 | stride 0xE28; +0xCC0/0xCC1/0xCC5/0xCCE counters |
| chip0 object | 0x15CA08 | 0x40816408-0x1B68 | BSS; +0x1A00 = rx ctx, +0xD6D = channel, +0xD70 = band index |
| chip1 object | 0x15E570 | - | band-1 mirror |
| chip-manager object | 0x170E08 | 0x40828E08 | BSS; +0x650 dest table entries, +0x6FC base |

Customer record layout [V]: +0 dest record; +4 wait object (0xC71B8 init); +0x48 pending
flag (atomic); +0x4C thread table record; +0x50 task handle (0xC745C); +0x54/+0x58
binding list head/self-pointers. Queue record layout [V]: +0 state word, +4 cfg ptr, +8
state=1, +0xC lock, +0x40 list head, +0x48 list count, +0x80 second list head, +0xC0
notifier, +0xC4 flag, +0xC8/+0xCC callbacks (zero).

## T3 - static tables [V]

Queue cfg (0x1040C0, 8 B each): {u8 qidx @0, u8 @2, u8 customer @3, u16 round limit @4, u8
@6, u8 priority @7} -> q0 {idx0, c0, 100}, q1 {idx1, c1, 80}, q2 {idx2, c2, 80}.

Thread table (0x10CFC8, 0x30 B records): name[16] @0, id @0x10, stack @0x14, prio @0x18,
aff @0x1C, flag @0x20, 0xff @0x24, entry @0x28, init-cb @0x2C:
"HCC TX Thread" (0x10CFC8, id 0, entry 0xC515E), "HCC RX Thread" (0x10CFF8, id 1, entry
0x102552), "HCC OAM Thread" (0x10D028, id 2, entry 0xC515E).

hcc map 0x103FB8: +0 dest start 0; +4 dest count 1; +8 n_queues 3; +0xC n_customers 3;
+0x10 n_groups 7; +0x14 3; +0x18 queue cfg 0x1040C0; +0x1C thread table 0x10CFC8; +0x20 7;
+0x24 group table 0x10CF74; +0x28 queue records 0x1040A8. Group table: g0->q1, g1->q0,
g2->q1, g3->q0, g4->q2 (max ids 50/40/43/21/5).

## T4 - the builder gate matrix (re-derived, extends task-164 G1-G10) [V]

| # | CA | condition | on failure | counter |
| --- | ---: | --- | --- | --- |
| G1 | 0xB97EE | [chip+0x1A00] != 0 | log 0xA4E, return 0x8B2D | none |
| G2 | 0xB97AE/0xA4FC8 | chip resolvable; u8[band]==0 and u8[band+1]<=1 | log 0x361/0xA94 | none |
| G3 | 0xB98F8-0xB9A48 | descriptor carries the 48-B info fields | error log | none |
| G4 | 0xB9A84 | info[+2] <= frame_len | log 0x9A4 | none |
| G5 | 0xB9A8E | chip ctx +0x5E class split | different class path | none |
| G6 | 0xB9A9A | frame_len windows (0x1C..0xFE etc.) | log 0x9F5/0x9EE | none |
| Gsel | 0xB9816-0xB9832 | forward flag bit6; class nibble | drop/forward | none |
| Gclass | 0xB9864 | class nibble in {1,2} | drop list, log 0xA6A | none |
| G7 | 0xBA0FC/0xBA134 | dest=0xBC524(u8[rxinfo+6]) != 0 | log 0x49F | none |
| G8 | 0xBA110 | frame_len+0x3C <= 0x680 | log 0x497 | band+0xCC5 |
| Gpost | 0xBA17A | post 0x102760 == 0 | log 0x4B3, free node | none |

New in this lane: Gclass (a drop-all no-counter gate before the per-frame build) and the
per-descriptor counters at 0xBA240/0xBA3FC/0xBA41E.

## T5 - probe conversion and expected values

Firmware-RAM window: **phys = 0x40000000 + 0x6B8000 + CA**. Device-IO window:
phys = 0x40000000 + 0x3B8000 + CA (CA 0x40000000..0x4011FFFF). PKTRAM:
phys = 0x1D8000 + (CA - 0x01000000).

| item | CA | phys | width | expected |
| --- | ---: | ---: | ---: | --- |
| q1 notifier | 0x171D14 | 0x40829D14 | 4 | **0x00171E84** |
| customer1+0 | 0x171E84 | 0x40829E84 | 4 | 0x00171FB4 |
| customer1+0x48 | 0x171ECC | 0x40829ECC | 4 | 0 (idle) / 1 (pending) |
| customer1+0x4C | 0x171ED0 | 0x40829ED0 | 4 | 0x0010CFF8 |
| customer1+0x50 | 0x171ED4 | 0x40829ED4 | 4 | nonzero task handle |
| customer1+0x54 / +0x58 | 0x171ED8 / 0x171EDC | 0x40829ED8 / DC | 4 | 0x00171C54 (q1) |
| chip0+0x1A00 | 0x15E408 | 0x40816408 | 4 | nonzero rx-ctx ptr |
| chip0+0xD70 | 0x15D178 | 0x40815178 | 1 | 0 or 1 |
| dest table base | 0x171504 | 0x40829504 | 2 | u16 base offset |
| dest table entries | 0x171458 | 0x40829458 | 0x13*4 | dest_i = base + entry_i |
| band0 +0xCC0 | 0x1758B0 | 0x4082D8B0 | 1 | producer entries (moves already) |
| band0 +0xCC1 | 0x1758B1 | 0x4082D8B1 | 1 | **descriptor stage** |
| band0 +0xCCC | 0x1758BC | 0x4082D8BC | 1 | forward-prep fail |
| band0 +0xCCD | 0x1758BD | 0x4082D8BD | 1 | report alloc fail |
| band0 +0xCC5 | 0x1758B5 | 0x4082D8B5 | 1 | CRX cap drop |
| band0 +0xCCE | 0x1758BE | 0x4082D8BE | 1 | g2id3 post fail |
| band1 mirrors | +0xE28 | - | - | P12b/P13b/P19/P24b band1 |

## T6 - provenance and grades

| item | source | grade |
| --- | --- | --- |
| queue/customer/worker wiring | this lane, 0x47DF8-0x48124 + 0x102552 | [V] |
| notifier expected values 0x171E1C/0x171E84/0x171EEC | this lane (init immediates) | [V] |
| chip+0x1A00 installer and gate | this lane, 0x48A68 / 0xB97EE | [V] |
| class nibble gate 0xB9864 | this lane | [V] |
| per-descriptor counters +0xCC1/+0xCCC/+0xCCD | this lane | [V] |
| fwd/dest resolver formula 0xBC524 | this lane + task-164 | [V] |
| g2 table contents id2/id3/id5 | this lane + task-220/221 | [V] |
| "q1 is drained because H2D works" | this lane (inference from bench state) | [I] |
| runtime values of P20-P24 | bench read-only pass | [U] |
