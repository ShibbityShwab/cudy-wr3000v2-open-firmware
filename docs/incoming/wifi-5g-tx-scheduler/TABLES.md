# TABLES - wifi-5g-tx-scheduler

Firmware CPU address convention: CA = file offset + 0x40000 of the pinned
FIRMWARE.bin (928,920 B, sha256 `7fc87e20...311b`). Host read paths: RAM
BAR0 = CA + 0x6B8000; device IO BAR0 = 0x3B8000 + (CA - 0x40000000); phys =
0x40000000 + BAR0. Every row below is re-derived by
`tools/fw_tx_scheduler_re.py` (`--selftest` 128/128 PASS); the long form is
in evidence/tables.txt.

## T1. The 45 name blob (CA 0x12266C..0x122BA4)

| # | name | CA |
| ---: | --- | ---: |
| 0 | smac_coex_rx_abort_end_5g_isr | 0x12266C |
| 1 | smac_coex_rx_abort_end_2g_isr | 0x12268C |
| 2 | smac_rx_complete_5g_isr | 0x1226AC |
| 3 | smac_rx_complete_2g_isr | 0x1226C4 |
| 4 | smac_tx_complete_5g_isr | 0x1226DC |
| 5 | smac_tx_complete_2g_isr | 0x1226F4 |
| 6 | smac_common_timer_5g_isr | 0x12270C |
| 7 | smac_common_timer_2g_isr | 0x122728 |
| 8 | smac_coex_pta_rx_abort_timeout_5g_isr | 0x122744 |
| 9 | smac_coex_pta_rx_abort_timeout_2g_isr | 0x12276C |
| 10 | smac_coex_tx_abort_end_5g_isr | 0x122794 |
| 11 | smac_coex_tx_abort_end_2g_isr | 0x1227B4 |
| 12 | smac_coex_pta_tx_abort_timeout_5g_isr | 0x1227D4 |
| 13 | smac_coex_pta_tx_abort_timeout_2g_isr | 0x1227FC |
| 14 | smac_msg_proc_set_mu_edca_timer_req | 0x122824 |
| 15 | smac_msg_proc_ac_suspend_req | 0x122848 |
| 16 | smac_msg_proc_set_cali_self_cts_tx_num_req | 0x122868 |
| 17 | smac_msg_proc_set_cali_ppdu_tx_num_req | 0x122894 |
| 18 | smac_msg_proc_rx_ring_reset_req | 0x1228BC |
| 19 | smac_msg_proc_update_tx_bypass_ctrl_req | 0x1228DC |
| 20 | smac_msg_proc_beacon_tx_resume_req | 0x122904 |
| 21 | smac_msg_proc_beacon_tx_suspend_req | 0x122928 |
| 22 | smac_msg_proc_clr_hw_fifo_req | 0x12294C |
| 23 | smac_msg_proc_tx_resume_req | 0x12296C |
| 24 | smac_msg_proc_tx_suspend_req | 0x122988 |
| 25 | smac_msg_proc_enable_trx_req | 0x1229A8 |
| 26 | smac_msg_proc_disable_trx_req | 0x1229C8 |
| 27 | smac_tbtt_5g_isr | 0x1229E8 |
| 28 | smac_tbtt_2g_isr | 0x1229FC |
| 29 | smac_rx_timeout_5g_isr | 0x122A10 |
| 30 | smac_rx_timeout_2g_isr | 0x122A28 |
| 31 | smac_pre_tx_5g_isr | 0x122A44 (3 pad bytes 0x01 0x02 0x03 at 0x122A41) |
| 32 | smac_pre_tx_2g_isr | 0x122A58 |
| 33 | smac_backoff_timeout_5g_isr | 0x122A6C |
| 34 | smac_backoff_timeout_2g_isr | 0x122A88 |
| 35 | smac_user_queue_empty_5g_isr | 0x122AA4 |
| 36 | smac_user_queue_empty_2g_isr | 0x122AC4 |
| 37 | smac_user_ps_5g_isr | 0x122AE4 |
| 38 | smac_user_ps_2g_isr | 0x122AF8 |
| 39 | smac_tx_abort_start_5g_isr | 0x122B0C |
| 40 | smac_tx_abort_start_2g_isr | 0x122B28 |
| 41 | smac_lifetime_expire_5g_isr | 0x122B44 |
| 42 | smac_lifetime_expire_2g_isr | 0x122B60 |
| 43 | smac_tx_exit_5g_isr | 0x122B7C |
| 44 | smac_tx_exit_2g_isr | 0x122B90 |

45 names = 32 per-band ISR names (16 x 5g/2g) + 13 smac_msg_proc names.
**No u32 literal / movw+movt constant equals any CA above [V]** (dead static
data; the tables it names are installed by direct literals, not via pointers).

## T2. ISR handler table CA 0x10B058 (32 thumb pointers, name order) [V]

| idx | name | handler | idx | name | handler |
| ---: | --- | ---: | ---: | --- | ---: |
| 0 | coex_rx_abort_end_5g | 0xE6D00 | 16 | rx_timeout_5g | 0xE698E |
| 1 | coex_rx_abort_end_2g | 0xE6CBC | 17 | rx_timeout_2g | 0xE6984 |
| 2 | rx_complete_5g | 0xE6C94 | 18 | pre_tx_5g | 0xE77FA |
| 3 | rx_complete_2g | 0xE6EE8 | 19 | pre_tx_2g | 0xEB438 |
| 4 | tx_complete_5g | 0xE6C10 | 20 | backoff_timeout_5g | 0xE77DA |
| 5 | tx_complete_2g | 0xE7060 | 21 | backoff_timeout_2g | 0xE77A8 |
| 6 | common_timer_5g | 0xE700C | 22 | user_queue_empty_5g | 0xE7788 |
| 7 | common_timer_2g | 0xE6A00 | 23 | user_queue_empty_2g | 0xE7768 |
| 8 | coex_pta_rx_abort_timeout_5g | 0xE6BF4 | 24 | user_ps_5g | 0xE7748 |
| 9 | coex_pta_rx_abort_timeout_2g | 0xE69DC | 25 | user_ps_2g | 0xEB3B8 |
| 10 | coex_tx_abort_end_5g | 0xE6BBC | 26 | tx_abort_start_5g | 0xE7736 |
| 11 | coex_tx_abort_end_2g | 0xE6B48 | 27 | tx_abort_start_2g | 0xE770C |
| 12 | coex_pta_tx_abort_timeout_5g | 0xE6F74 | 28 | lifetime_expire_5g | 0xE76A0 |
| 13 | coex_pta_tx_abort_timeout_2g | 0xE6F1C | 29 | lifetime_expire_2g | 0xE6A1C |
| 14 | **tbtt_5g** | **0xE69CA** | 30 | tx_exit_5g | 0xE7144 |
| 15 | **tbtt_2g** | **0xE69A2** | 31 | tx_exit_2g | 0xE83F4 |

The TBTT handlers are leaf stubs: tbtt_5g = 6 insns, tbtt_2g = 11 insns,
no BL, no device-register literal [V]. They report per-band
`ctx+1256/1440..` timer counters into the SMAC event message
(`msg+142..154`, type byte `msg+274`) - no beacon build, no queue, no MMIO.

## T3. SMAC message-processor table CA 0x10B0DC (10 thumb pointers) [V]

| idx | handler | note |
| ---: | ---: | --- |
| 0 | 0xE70EC | [U] |
| 1 | 0xDB7A0 | [U] |
| 2 | **0xE8520** | enable/disable-TRX worker (band byte u8(msg+0xA)) |
| 3 | **0xE6B00** | beacon TX suspend 2.4 GHz |
| 4 | **0xE6AB4** | beacon TX resume 2.4 GHz |
| 5 | **0xE6A70** | beacon TX suspend 5 GHz |
| 6 | **0xE6A26** | beacon TX resume 5 GHz |
| 7 | 0xDB718 | [U] |
| 8 | 0xE8214 | [U] (one of the TRX disable twins) |
| 9 | 0xE83AC | [U] (one of the TRX disable twins) |

## T4. Per-band IRQ registration - boot init CA 0xD1B68 [V]

| band | irq | flags | wrapper | wrapper band object | shared body |
| --- | ---: | ---: | ---: | --- | ---: |
| 2.4G | 47 | 5 | 0xAEC94 | 0x180814+0x08 | 0xAEC74 |
| 2.4G | 49 | 5 | 0xB54C8 | +0x08 | 0xB53A4 |
| 2.4G | 50 | 5 | 0xB0D50 | +0x08 | 0xB0CE0 |
| 2.4G | 51 | 5 | 0xAEC68 | +0x08 | 0xAEB80 |
| 2.4G | 52 | 5 | 0xAEB74 | +0x08 | 0xAEA9C |
| 2.4G | 82 | 14 | 0xAE48C | +0x08 | 0xAE45C |
| 2.4G | 83 | 5 | 0xB0D50 | +0x08 | 0xB0CE0 |
| 2.4G | 59 | 5 | 0xB0C44 | +0x08 | 0xB0AB8 |
| 5G | 53 | 5 | 0xAEC88 | +0x1C | 0xAEC74 |
| 5G | 55 | 5 | 0xB54BC | +0x1C | 0xB53A4 |
| 5G | 56 | 5 | 0xB0D44 | +0x1C | 0xB0CE0 |
| 5G | 57 | 5 | 0xAEC5C | +0x1C | 0xAEB80 |
| 5G | 58 | 5 | 0xAEB68 | +0x1C | 0xAEA9C |
| 5G | 84 | 14 | 0xAE480 | +0x1C | 0xAE45C |
| 5G | 85 | 5 | 0xB0D44 | +0x1C | 0xB0CE0 |
| 5G | 60 | 5 | 0xB0C38 | +0x1C | 0xB0AB8 |

Wrapper shape (3 insns): `ldr r3,=0x180814 ; ldr r0,[r3,#0x1C or #0x08] ;
b <shared body>`. Registration call: `CA 0xC6DA2(irq, flags, wrapper|1)` then
`CA 0xC6FF4(irq)`. Install function **CA 0xC74B0** stores the handler at
**RAM `0x17D398 + 0x98 + 4*irq`** (`str.w r8,[r3,#152]` at CA 0xC74E8; base
literal at CA 0xC7534 = 0x17D398). So the live table is host-readable:
`0x17D430 + 4*irq` in CA space (T7).

## T5. The per-band beacon-TX gate words [V bits / I semantics]

| word | address | role | bits |
| --- | ---: | --- | --- |
| GATE_EN | **0x40101220** | per-band SMAC/TX enable | bit0 = 5 GHz, bit1 = 2.4 GHz |
| GATE_SUS | **0x40101208** | per-band suspend latch (= GATE_EN - 24) | 5 GHz 0x1|0x4, 2.4 GHz 0x2|0x8 |

| handler CA | role | effect (16-bit accesses) |
| ---: | --- | --- |
| 0xE6A26 | resume 5 GHz | `0x40101220 |= 1` ; `0x40101208 &= ~(0x1|0x4)` |
| 0xE6A70 | suspend 5 GHz | `0x40101220 &= ~1` ; `0x40101208 |= (0x1|0x4)` |
| 0xE6AB4 | resume 2.4 GHz | `0x40101220 |= 2` ; `0x40101208 &= ~(0x2|0x8)` |
| 0xE6B00 | suspend 2.4 GHz | `0x40101220 &= ~2` ; `0x40101208 |= (0x2|0x8)` |

Band-to-bit mapping: MAC start CA 0x4AF0C reads `u8(perlink+0x11D)` and ORs
**1** into 0x40101220 for band 1 / **2** for band 0 (literals CA 0x4B104 =
0x40101220) [V]. Each handler also bails out with status 35629 when
`msg[2] != 0` (the "reject/already" path); the success path is the bit
sequence above [V].

## T6. TRX enable/disable worker CA 0xE8520 (msg table idx 2) [V]

| band | registers written |
| --- | --- |
| 5 GHz (u8(msg+0xA)=1) | 0x4010E150 = 0x33, 0x4010E154 = 0xFF, 0x4010E158 = 0xFF |
| 2.4 GHz | 0x4010820C / 0x40108214 (via 0x4010C200/0x4010C220) |

Both gate surfaces (beacon TX and TRX) are therefore firmware-internal SMAC
messages, not host keys: no g0-id1 table A (217 rows) or table B (88 rows)
worker equals any of these handlers [V] (T9).

## T7. R0 probe pack (read-only; CA / BAR0 / phys)

| object | CA | BAR0 | phys | expected |
| --- | ---: | ---: | ---: | --- |
| GATE_EN | 0x40101220 | 0x004B9220 | 0x404B9220 | bit1 set on working 2.4G; bit0 = 5G |
| GATE_SUS | 0x40101208 | 0x004B9208 | 0x404B9208 | suspend bits per band |
| band0 object slot | 0x0018081C | 0x0083881C | 0x4083881C | nonzero |
| band1 object slot | 0x00180830 | 0x00838830 | 0x40838830 | nonzero |
| 2.4G TRX | 0x4010820C / 0x40108214 | 0x004C020C / 0x004C0214 | 0x404C020C / 0x404C0214 | enable-worker values |
| 5G TRX | 0x4010E150 / 0x4010E154 | 0x004C6150 / 0x004C6154 | 0x404C6150 / 0x404C6154 | 0x33 / 0xFF |
| live IRQ 47 | 0x0017D4EC | 0x008354EC | 0x408354EC | 0x000AEC95 (5G: irq 53 = 0x0017D504 -> 0x000AEC89) |
| live IRQ 84 | 0x0017D580 | 0x00835580 | 0x40835580 | 0x000AE481 (2.4G: irq 82 = 0x0017D578 -> 0x000AE48D) |

Full slot list for all 16 IRQs is printed by `--probes`
(evidence/probes.txt); each live slot is `0x17D430 + 4*irq` in CA space.

## T8. Name blob layout notes [V]

* 0x12266C..0x122BA4 = 45 NUL-terminated ASCII names, contiguous except:
* 0x122A41..0x122A43 = 3 non-text bytes `01 02 03` before
  `smac_pre_tx_5g_isr` (0x122A44) - a build artefact, not a pointer;
* 0x122BA4..0x122BB0 = zero padding, then 0x122BB4.. = static data tables
  (u16/u32 records) that are not referenced by any code either [V].
* The 45 names are the source-order name tables for the two handler tables
  CA 0x10B058 (ISR, 32 entries) and CA 0x10B0DC (msg, 10 entries) [V order].

## T9. Host-trigger boundary [V]

| test | result |
| --- | --- |
| g0-id1 table A (CA 0x10C23C, 217 rows) worker == any of the 45 handlers / 4 gate processors / TRX worker | none |
| g0-id1 table B (CA 0x103BF8, 88 rows) worker == the same set | none |
| sentinel row 0x1451/NULL terminates both tables | yes |
| consequence | the beacon resume/suspend and TRX enable messages are firmware-internal; the host can only set the gate words |

## T10. Correction - the 0x401015CC pair [V pair / I semantics]

| address | role evidence |
| --- | --- |
| 0x401015CC | written by helpers CA 0x897D6 (clear) / 0x8980C (set=1, save+override 0x400024B0=100, countdown at ctx+796 then restore) and by per-band MAC init CA 0x9F2C6 (set/assert at 0x9F436, deassert at 0xA047A) |
| 0x400024B0 | the paired settle-delay value (100), saved/restored by the same helpers |
| meaning | clock/power/analog enable + settle delay [I]; **not** the TBTT timer: the working 2.4 GHz band leaves 0x401015CC = 0 [V*] and no TBTT/beacon handler touches it [V] |
