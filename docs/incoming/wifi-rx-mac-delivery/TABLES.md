# TABLES - addresses, opcodes and call sites for the MAC-RX -> firmware -> D2H decode

**Lane:** wifi-rx-mac-delivery (task-220).  CA = file offset + 0x40000 of
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`).  Every row below is
asserted by `tools/fw_rx_mac_delivery_re.py --selftest` (84/84 PASS) from the raw image.
Grades: **[V]** byte-verified here; **[V\*]** committed earlier lane; **[I]** inference;
**[U]** unknown/device read required.

## T1 - the boot chain to the PPDU descriptor list (cross-check of wifi-ppdu-init) [V]

| site (CA) | fact |
| --- | --- |
| 0x40000 | image word0 = 0x00046971 -> Thumb entry 0x46970 |
| 0x48314 | `bl 0xD1B68` (device init) |
| 0xD3BAA / 0xD3BAC | `mov r0,r5` / `bl 0xC016C` (PPDU list init, per band object) |
| 0xF4F48 | getter `get_device(band)`: band>1 or chip -> NULL, else 0x174BF0 + 0xE28*band |
| 0xF4F4E | `movw r0,#0xE28` (band stride) |
| 0xF4F60 | literal 0x00174BF0 (device base) |
| 0xC016C | list init: next +0x66C, prev +0x670, count +0x674, name +0x67C; pre-alloc 0x1A0 B via 0xBF128 |
| 0xC02B2 | `str.w sb,[r4,#0x66C]` (self pointer) |
| 0xC02B6 | `str.w sb,[r4,#0x670]` (self pointer) |
| 0xC02C2 | `str.w r3,[r4,#0x67C]` (name 0x1060F7) |
| 0xC02D0 | `str.w r6,[r4,#0x674]` (count = 0) |
| 0xC0402 | `bl 0xBF128` (descriptor allocator) |
| 0xC0484 | literal 0x001060F7 (the "&pst_device->st_device_alloced_ppdu_dscr_list..." name) |
| 0x1060F7 | the name string itself |

## T2 - the PPDU descriptor pool reserve helper 0xB52F0 [V]

| site (CA) | fact |
| --- | --- |
| 0xB52F0 | entry `movs r3,#0` + `push.w {r4,r5,r6,r7,r8,sb,lr}` (reserve/app function) |
| 0xB5330/0xB5332 | `add.w r7,r0,#0x678` (per-object list lock) |
| 0xB535A/0xB535C | `mov.w r3,#0x198` (descriptor block size) |
| 0xB5364 | `bl 0xC1E0C` (memset of the 0x198-B block) |
| 0xB536C/0xB536E | `ldr.w r3,[r5,#0x670]` (list tail) |
| 0xB5374/0xB5376 | `str.w r4,[r5,#0x670]` (link) |
| 0xB537E/0xB5380 | `ldr.w r3,[r5,#0x674]` (count) |
| 0xB5388/0xB538A | `str.w r3,[r5,#0x674]` (count++) |
| callers | 0xB5458, 0xBAF10, 0xBBD40 (RX/TX datapath) |

## T3 - the RX -> CRX consumer: queue intake, per-frame call and deposit [V]

| site (CA) | fact |
| --- | --- |
| 0xBA1C8 | datapath handler (large); calls the CRX builder once per frame |
| 0xBA48A | `bl 0xB9780` (per-frame CRX builder) |
| 0xB9780 | CRX builder entry (`push.w {r4..fp,lr}; sub sp,#0xe4`) |
| 0xB979C | `bl 0x409EA` (message header from the node) |
| 0xB97A0/0xB97AE | chip/band object from the node header + resolver 0xA4FC8 |
| 0xBA0C0 | `bl 0x1024EA` (pop the next received-frame node) |
| 0xBA0C6 / 0xBA0CE | `bl 0x409EA` (header) / `bl 0x40CBA` (RX info/descriptor pointer) |
| 0xBA0E2 | `bl 0x1024E6` (free the node) |
| 0xBA17A | `bl 0x102760` (deposit the built D2H message) |

## T4 - the CRX wire builder (re-derived; agrees with wifi-rx-crx-gating) [V]

| site (CA) | fact |
| --- | --- |
| 0xBA0F8 | `ldrh.w fp,[r0,#0x11]` = frame_len (info+0x11) |
| 0xBA0FC | `ldrb r0,[r0,#6]` = forward flag (info+6) |
| 0xBA0FE-0xBA104 | total payload pre-add (frame+0x30) |
| 0xBA110/0xBA114 | `add.w r1,r2,#0xC` / `cmp.w r1,#0x680` (hard cap) |
| 0xBA126-0xBA12E | cap-drop path bumps `dev+0xCC5` (u8) |
| 0xBA134 | `cbnz r0` - forward flag 0 -> drop (log 0x49F) |
| 0xBA146/0xBA148 | `add.w fp,fp,#0x3C` (total = frame + 60) |
| 0xBA14A/0xBA14C | `bfi r3,sb,#0,#4` - group nibble (sb=1) |
| 0xBA150/0xBA152 | `movs r3,#0xD` / `strb r3,[r4,#6]` - id 13 |
| 0xBA15C-0xBA172 | destination/hdr fields from info[+2], info[+1], info[+0] |
| 0xBA174 | MPDU copy |
| 0xBA17A | deposit |

## T5 - the per-chip RX context gate [V]

| site (CA) | fact |
| --- | --- |
| 0xB97EE/0xB97F0 | `add.w r3,r6,#0x1a00` |
| 0xB97F2/0xB97F4 | `ldr.w r8,[r3]` - the per-chip RX context |
| 0xB97F6/0xB97FA | `cmp.w r8,#0` / `bne` - NULL -> log 0xA4E, return 0x8B2D |
| 0xBAF14-0xBAF42 | datapath counter words on the device object: +0x146C (u8), +0x1ADA (u8), +0x1AD4 (u8, cmp #1) |

## T6 - the D2H deposit path and notify ids [V]

| site (CA) | fact |
| --- | --- |
| 0x102760 | `hcc_msg_tx`: dest nibble = `hdr[1]>>4` stored to `hdr[9]`; per-chip group record base 0x448; 56 callers |
| 0x1028EC | literal 0x00171B6C (global HCC instance) |
| 0xC6170 | `d2h_notify`: `cmp r1,#0xA`, r0 must be 0, object from literal 0xC61DC = 0x00172130, pending mask base +0xB8 |
| 0xC6224 | `bl 0xC6170` with r1=6 (0xC6220 `movs r1,#6`) |
| 0xC6998 | `bl 0xC6170` with r1=5 (0xC6994 `movs r1,#5`) |
| 0x462AC | `b.w 0xC6170` with r1=3 (0x462AA `movs r1,#3`, host-interface doorbell service) |
| 0xC192A | ETE 8-byte node fill; called at 0xC621C and 0xC6254 |
| 0xC6C0A | HEARTBEAT builder `bl 0x102760`; literal "HEARTBEAT" at 0x106328 |
| 0x10271E-0x10273A | HCC message thread: pop 0x1024EA -> dispatch 0xFD7E8 -> free 0x1024E6 |

## T7 - the SMAC ISR / message-proc name table (CA 0x12266C) [V data, U mapping]

32-byte NUL-padded name records, starting at CA 0x12266C:

| CA | name |
| ---: | --- |
| 0x12266C | smac_coex_rx_abort_end_5g_isr |
| 0x12268C | smac_coex_rx_abort_end_2g_isr |
| 0x1226AC | smac_rx_complete_5g_isr |
| 0x1226C4 | smac_rx_complete_2g_isr |
| 0x1226DC | smac_tx_complete_5g_isr |
| 0x1226F4 | smac_tx_complete_2g_isr |
| 0x12270C | smac_common_timer_5g_isr |
| 0x122728 | smac_common_timer_2g_isr |
| 0x122744 | smac_coex_pta_rx_abort_timeout_5g_isr |
| 0x12276C | smac_coex_pta_rx_abort_timeout_2g_isr |
| 0x122794 | smac_coex_tx_abort_end_5g_isr |
| 0x1227B4 | smac_coex_tx_abort_end_2g_isr |
| 0x1227D4 | smac_coex_pta_tx_abort_timeout_5g_isr |
| 0x1227FC | smac_coex_pta_tx_abort_timeout_2g_isr |
| 0x122824 | smac_msg_proc_set_mu_edca_timer_req |
| 0x122848 | smac_msg_proc_ac_suspend_req |
| 0x122868 | smac_msg_proc_set_cali_self_cts_tx_num_req |
| 0x122894 | smac_msg_proc_set_cali_ppdu_tx_num_req |
| 0x1228BC | smac_msg_proc_rx_ring_reset_req |
| 0x1228DC | smac_msg_proc_update_tx_bypass_ctrl_req |
| 0x122904 | smac_msg_proc_beacon_tx_resume_req |
| 0x122928 | smac_msg_proc_beacon_tx_suspend_req |
| 0x12294C | smac_msg_proc_clr_hw_fifo_req |
| 0x12296C | smac_msg_proc_tx_resume_req |
| 0x122988 | smac_msg_proc_tx_suspend_req |
| 0x1229A8 | smac_msg_proc_enable_trx_req |
| 0x1229C8 | smac_msg_proc_disable_trx_req |
| 0x1229E8 | smac_tbtt_5g_isr |
| 0x1229FC | smac_tbtt_2g_isr |
| 0x122A10 | smac_rx_timeout_5g_isr |
| 0x122A28 | smac_rx_timeout_2g_isr |
| 0x122A44 | smac_pre_tx_5g_isr |
| 0x122A58 | smac_pre_tx_2g_isr |
| ... | backoff_timeout / user_queue_empty / user_ps / tx_abort_start / lifetime_expire / tx_exit (both bands) |

**No 32-bit literal in the image points into this table** (word scan 0x122400-0x123300
finds only an unrelated 0x00122424) -> it is a build-artifact name table for offline
crash symbolisation, **not** a dispatch table; the ISR handler addresses cannot be
recovered from it.  This is why the RX producer entry is [U].

## T8 - the firmware's HCC dispatch tables (re-derived at the CRX entry) [V]

| table (CA) | fact |
| --- | --- |
| 0x10CC18 | group-2 dispatch table (19 entries, 12-byte records {u16 id; u16 pad; u32 handler; u32 aux}) |
| 0x10CC18+0x0 | id 2 -> 0x000B9781 (CRX builder 0xB9780) |
| 0x10CC18+0x24 | id 5 -> 0x000B9781 (same handler) |
| 0x10CCFC | group-0 dispatch table (40 entries) |
| 0x10CFC8/0x10CFF8/0x10D028 | thread-name records: HCC TX/RX/OAM Thread |
| 0x171B6C | global HCC instance (literal 0x1028EC) |

## T9 - conversion constants for the probes (committed ppdu-init convention) [V\*]

| item | formula / value |
| --- | --- |
| firmware RAM phys | CA + 0x406B8000 |
| device IO phys | CA + 0x3B8000 |
| PKTRAM phys | 0x40000000 + 0x1D8000 + (CA - 0x01000000) |
| ACP phys | 0x40000000 + 0x4D8000 + (CA - 0x02000000) |
| MAC live bank (CA 0x40050000) via bench iATU | phys 0x40408000 |
| MAC config block (CA 0x40040000) | phys 0x403F8000 (+0x48 = mask) |
| RC misc window | **never read** (phys 0x10160000..0x1016FFFF) |

## T10 - per-band device object addresses (CA -> phys) [V / derivation]

| item | band 0 | band 1 |
| --- | ---: | ---: |
| object base | 0x174BF0 | 0x175A18 |
| list next +0x66C (CA / phys) | 0x17525C / 0x4082D25C | 0x176084 / 0x4082E084 |
| list prev +0x670 | 0x175260 / 0x4082D260 | 0x176088 / 0x4082E088 |
| count +0x674 | 0x175264 / 0x4082D264 | 0x17608C / 0x4082E08C |
| name +0x67C (expect 0x1060F7) | 0x17526C / 0x4082D26C | 0x176094 / 0x4082E094 |
| +0xCC5 (u8) | 0x1758B5 / 0x4082D8B5 | 0x1766DD / 0x4082E6DD |
| +0x1AD4 (u8) | 0x1766C4 / 0x4082E6C4 | 0x1774EC / 0x4082F4EC |
| +0x1ADA (u8) | 0x1766CA / 0x4082E6CA | 0x1774F2 / 0x4082F4F2 |
