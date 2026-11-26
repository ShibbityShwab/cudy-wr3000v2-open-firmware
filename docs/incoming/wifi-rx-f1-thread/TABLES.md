# TABLES - lane wifi-rx-f1-thread (task-216)

CA = file offset + 0x40000 of the pinned `FIRMWARE.bin` (928,920 B, sha256
`7fc87e20...c3ef311b`).  Host read paths **[V\*]** (committed lanes /
`hi5622_contract.h`): firmware RAM BAR0 = 0x6B8000 + CA, physical =
0x40000000 + BAR0; device IO BAR0 = 0x3B8000 + (CA - 0x40000000); PKTRAM BAR0 =
0x1D8000 + (CA - 0x01000000); ACP BAR0 = 0x4D8000 + (CA - 0x02000000).
Everything here is re-derived by `tools/fw_rx_f1_thread_re.py --selftest`
(86 checks) from the local artifacts.

## T1. The firmware thread descriptor table [V]

| field | value / note |
| --- | --- |
| table CA | 0x10CFC8; stride 0x30; 3 records |
| record layout | `char name[16]; u32 id; u32 stack; u32 prio; u32 aff; u32 flag; u32 0xff; u32 entry; u32 arg` [V data, I fields] |
| TX | name 0x10CFC8 `HCC TX Thread`; id 0; stack 0x100; prio 3; entry 0x000C515F |
| RX | name 0x10CFF8 `HCC RX Thread`; id 1; stack 0x100; prio 3; entry 0x00102553 |
| OAM | name 0x10D028 `HCC OAM Thread`; id 2; stack 0x100; prio 4; entry 0x000C515F |
| literal xrefs into the table | only 0x10CFC8, one word at CA 0x103FD4; 0x10CFF8 and 0x10D028: none |
| BL callers of the entry functions | none (0x102552, 0xC515E): table-installed tasks |
| RX entry body | CA 0x102552; queue lock/pop/splice (`FDF8E/FDF50/1024BC/1024EA/102530/102514`), then resolver 0xFD4C8 (0x102612) and dispatch 0xFD7E8 (0x102644) |
| TX/OAM entry body | CA 0xC515E; outgoing queue service (same helpers) |

## T2. HCC dispatch (H2D) [V]

| item | CA | facts |
| --- | ---: | --- |
| dispatch | 0xFD7E8 | reads header (msg struct at +?), calls resolver, calls `[handler_base + id*16]` |
| resolver | 0xFD4C8 | `ldr r3,[r0+4]` count; `ldr r0,[r0+0x10]` group base; 12-byte records `{u32 count; u32 base}`; handler slots 16 B |
| resolver callers | 0xFD810 (inside dispatch), 0x102612 (RX thread) | [V] |
| dispatch callers | 0x102644 (RX thread), 0x102734 | [V] |
| queue init/helpers | 0x102514 / 0x1024BC / 0x1024EA / 0x102530 | list head at +0, count +8, lock +0xc |

## T3. D2H publish [V]

| item | CA | facts |
| --- | ---: | --- |
| notify(id) | 0xC6170 | first `cmp r1,#0xa` (id<=10); object from global word CA 0x172130; lock; set pending bit; publish mask to `[obj+0x9c]`; doorbell `[obj+0xa4]` |= 1 |
| notify call sites | 0xC6224 (id 6), 0xC6998 (id 5) | `movs r1,#6`/`#5` four bytes before each BL |
| ETE node fill | 0xC192A | 8-byte node `{devva, (len<<16)|0x6d2b}`; call sites 0xC621C, 0xC6254 |
| object global | CA 0x172130 (literal) | pointer to the HCC message object at runtime [I: expect 0x0010C0F4] |
| idle words (contract) | CA 0x0010C0F4 object; +0xB4 = CA 0x0010C1A8; +0xB8 = CA 0x0010C1AC | image words 0 (idle) [V] |
| delivery preconditions | phase43: channel enable (fw), `out[1]` drained, id<=10, gate CA 0x4000010C == 0xCECE | all satisfied on our boot [V\*] |

## T4. The PPDU descriptor list [V]

| item | CA | evidence |
| --- | ---: | --- |
| debug name | 0x1060F7 | `&pst_device->st_device_alloced_ppdu_dscr_list.st_spin_lock` |
| literal pool word | 0xC0484 | value 0x001060F7; loaded by `ldr r3,[pc,#0x1c4]` at 0xC02BE |
| name-pointer field store | 0xC02C2 | `str.w r3,[r4,#0x67c]` |
| list next / prev | +0x66C / +0x670 | self-pointers stored at 0xC02B2 / 0xC02B6 |
| list count | +0x674 | zeroed at 0xC02D0 |
| init function | 0xC016C | prologue bytes `2de9f74f`; single caller 0xD3BAC (`bl 0xC016C`) |
| descriptor allocator | 0xBF128 | `bl 0xBF128` at 0xC0402; size argument 0x1a0 |
| append / count++ | 0xC043A..0xC0450 | doubly-linked append + counter |
| runtime object | [U] | = per-chip ctx (T5); probe P4 |

## T5. Per-chip module context (probe formula) [V instructions, I application]

| item | value |
| --- | --- |
| resolver | CA 0x961AE: `mov r1,r0; movs r0,#0; b.w 0xFE578` (bytes `0146002068f0e1b9`) |
| manager | CA 0x943AC(0) returns 0x00170E28 (literal at 0x943C8) |
| formula | `ctx = u32[0x00171458] + u16[0x00171504]` (chip 0) |
| index math | `u32[0x170E28 + 4*(0x18C+chip)]` and `u16[0x170E28+0x6DC]` |
| self-check | `u32[ctx+0x67C]` must equal 0x001060F7 after the PPDU-list init |
| context fields used by RX/EDCA | +0x4B8 band object; +0x66C..+0x67C PPDU list; +0x4AA/+0x533/+0x0DB EDCA; +0x6E0/+0x6E8/+0x708 (seen in an H2D handler) |

## T6. EDCA handler (g0 id29) - re-derived effect [V]

| item | value |
| --- | --- |
| entry chain | g0 id29 handler CA 0xB568C -> hook slot G+0x378 -> hook CA 0x68C0A |
| context | `bl 0x961AE` at 0x68C12 (per-chip ctx) |
| RF gate | `ldr [ctx+0x4B8]`; `+0x1A00`; value must be nonzero |
| checksum | payload[0..19] as 4 x 5-byte entries: sum of first-4-bytes of each entry -> u16 store at `ctx+0x4AA` (0x68C74) |
| per-AC flags | u8 store at `ctx+0x533` (0x68C94), bitfield per comparator |
| bit2 | u8 `ctx+0x0DB` bit2 update (0x68D16) |
| stock payload | 20 x 00 + 40 x 0xFF; zero entries -> predicted checksum 0x0000 |
| direct BL callers of the hook | none (installed through the hook table) |

## T7. Stock reference words (committed `opensource/dumps/reg_all.txt`) [V\*]

| CA | stock | CA | stock |
| ---: | ---: | ---: | ---: |
| 0x40040020 | 0x844C7000 | 0x40050000 | 0x00000400 |
| 0x40040024 | 0x844C8000 | 0x40050008 | 0x00000020 |
| 0x40040028 | 0x83734000 | 0x4005000C | 0x00000020 |
| 0x4004002C | 0x00000100 | 0x40050010 | 0x00000000 |
| 0x40040030 | 0x00000200 | 0x40050014 | 0x00200020 |
| 0x40040034 | 0x00000FFF | 0x40050024 | 0x00800000 |
| 0x40040038 | 0x00008000 | 0x40050028 | 0x01800800 |
| 0x4004003C | 0x00008000 | 0x4005002C | 0x02801000 |
| 0x40040040 | 0x00000000 | 0x40050030 | 0x03801800 |
| 0x40040048 | 0xFFFFFCC1 | 0x4003A400 | 0x00000001 |
| 0x4003A410 | 0x844DB000 | 0x4003A590 | 0x00000001 |
| 0x4003A430 | 0x01060750 | 0x4003A5A0 | 0x01060440 |
| 0x4003A5C0 | 0x844D8000 | 0x4003AC00 | 0x00030003 |
| 0x4003AC18 | 0x001D001D | 0x4010E150 | 0x00000033 |
| 0x4010E154 | 0x000000FF | 0x4010E158 | 0x000000FF |
| 0x4010820C | 0x00000040 | 0x40108214 | 0x0000000F |
| 0x4010C200 | 0x00001630 | 0x4010C220 | 0x00001039 |
| 0x40039010 | 0x00000000 | 0x40039014 | 0x00000000 |
| 0x40101434 | 0x00000000 | | |

Host addresses from the stock boot (0x8373..../0x844d....) are stock-DMA values;
our boot's window differs - compare structure, enables and indices, not host
addresses.

## T8. Negatives verified here [V]

* No literal xref to the `HCC RX Thread` / `HCC OAM Thread` name blocks.
* No BL caller of the RX thread entry 0x102552 or the TX/OAM entry 0xC515E.
* The PPDU-list init function has exactly one caller (0xD3BAC).
* The EDCA hook 0x68C0A has no direct BL caller (hook-table installed).
* The notify function id bound is 10 (matches the committed contract).
* No device write is proposed anywhere in this lane; all probes are read-only.
