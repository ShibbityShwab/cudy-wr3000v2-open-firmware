# TABLES - lane wifi-smac-start (task-215)

Conventions and safety: see ../wifi-tbtt-timer/TABLES.md T1 (same pinned
firmware, same host region-3 window, forbidden writes CA 0x400392F0, never
read RC misc window 0x10160000..0x1016FFFF). Everything here is re-derived by
`tools/fw_smac_start_re.py --selftest`. Grades as in REPORT.md.

## T1. Identity

| item | value |
| --- | --- |
| firmware | 928,920 B, sha256 7fc87e20...311b, CPU = file + 0x40000 |
| SMAC wrapper | CA 0x180814: +8 band0 obj, +0x1C band1 obj |
| band object accessor | CA 0xF4F48: 0x174BF0 + 0xE28*band |
| band objects | band0 0x174BF0, band1 0x175A18 |
| band region base / size | 0x174BE0 / 7,272 B (memset at 0xD1B76) |
| boot SMAC init | CA 0xD1B68 (caller 0x48314), per-band loop body 0xD1CB6 |

## T2. Timer words - writers [V]

| CA | width | meaning |
| ---: | ---: | --- |
| 0x401015CC | 4 (strh low half / str 32-bit) | TSF/timer latch: write 1 start (0x89816), 0 stop (0x897F0), count/clear in the per-link init paths |
| 0x400024B0 | 2 | interval word: 100 written by 0x9F440/0xA02AC; saved/restored around the latch pair (RAM save slot 0x1664C8) |

| writer site | function | writes |
| ---: | --- | --- |
| 0x897EE | stop helper 0x897D6 | strh 0 -> 0x401015CC; restore interval |
| 0x89814 | start helper 0x8980C | strh 1 -> 0x401015CC; save interval; write 100 |
| 0x9F436 / 0x9F43E | per-link init A | count -> 0x401015CC; 0x400024B0 = 100 |
| 0xA0052 | per-link init B | 0 -> 0x401015CC |
| 0xA02A0 / 0xA02A8 | per-link init C | count -> 0x401015CC; 0x400024B0 = 100 |
| 0xA047A | per-link init D | 0 -> 0x401015CC |

Called only from CA 0xE8670 (start 0xE868E / stop 0xE8804, caller 0xDC74A in
fn 0xDC6FC) and CA 0xF3B22 (start 0xF3C02 / stop 0xF3D1C, callers
0x9C974/0x9C9A4/0x9C9B6/0x9C9E6 in fn 0x9C76C). Object state byte: +796.
Bench (Lead): both bands 0x401015CC = 0, 0x400024B0 = 0x64 => not the 5G
discriminator.

## T3. Per-band SMAC object fields (r4 = 0x174C30 + 0xE28*band) [V]

| offset | band0 | band1 | set at |
| ---: | ---: | ---: | ---: |
| +576 | 0x114F18 | 0x114F18 | 0xD2CA2 |
| +580 | 0x114F18+0x1E8 | same | 0xD2CAC |
| +584 | 0x115128 | same | 0xD2CA6 |
| +588 | 0x183438 | same | 0xD2CB4 |
| +592 | 0x1817A0 | same | 0xD2CBA |
| +596 | 0x180904 | same | 0xD2CC0 |
| +600 | 0x180E68 | same | 0xD2CC6 |
| +608, +612, ... +644 (step 0x2000) | 0x40042000 | 0x40062000 | 0xD2046, 0xD204E, ... |
| +648 .. +708 (step 0x800) | 0x40080000 | 0x400B0000 | 0xD20FE, 0xD2106, ... |
| +720 / +724 | 0x4010A000 / 0x40109000 | 0x4010E000 / 0x4010D000 | 0xD1E44 / 0xD2180 |

Address relation: wrapper object obj (0x174BF0/0x175A18), r4 = obj + 0x40, so
r4+576 = obj+640 and r4+608 = obj+672. The ISR bodies read obj+672 as the
"SMAC context block" pointer; the Lead's silent band-1 read of obj+672 =
0x40062000 is exactly the boot device-base default of this table. The Lead's
working band-0 read of obj+672 = 0x114F18 equals the value the same init writes
at r4+576 = obj+640 (0x20 offset discrepancy - R7 must probe both).

## T3b. Context status/mask pair [V addresses / U bits]

| field | site | meaning |
| --- | --- | --- |
| [ctx]+228 | 0xAE46A read, 0xAE474 write (body 0xAE45C) | SMAC ack/mask word (live band0 read 0x00A00096) |
| [ctx]+232 | 0xAE46A read (body 0xAE45C), 0xAEB8A (body 0xAEB80) | SMAC clear word (live band0 read 0) |
| mask literal | 0xAE468 = 0x01000200 | bits 9 and 25 of the mask source [ctx+700] |

Bit assignment of 0x00A00096 stays [U]; R7 #10/#11 record it as a liveness
signature (the ISR writes it).

## T4. RAM block set [V]

| block | value / role |
| --- | --- |
| 0x114F18 | written by the init fill loop 0xD2BD8..0xD2C50 from tables 0x1B2A87 / 0x1B2AAC (61 x 4 u16); read as a u16 table by 0x85EF2 / 0xA5414 / 0xA54D2 / 0xA5652 / 0xA5770 / 0xDA824 |
| 0x115128 | second block base (0x114F18 + 0x210) |
| 0x183438 / 0x1817A0 / 0x180904 / 0x180E68 | remaining per-band block pointers |
| image initial content of 0x114F18..0x115100 | 488 B of 0x00960096 fill; live +228 word 0xA00096 => firmware wrote at +230 |

## T5. Stock-vs-ladder H2D keys (g0 id1) [V*/V]

| set | keys |
| --- | --- |
| stock boot (25 distinct, committed FAMILY-TABLE) | 0x0000, 0x0005, 0x000B, 0x003C, 0x0052, 0x0065, 0x0069, 0x006F, 0x0073, 0x0078, 0x00D8, 0x00F7, 0x011E, 0x0124, 0x0127, 0x015D, 0x01CF, 0x01EE, 0x01F7, 0x025F, 0x02BD, 0x03E4, 0x07D9, 0x07E7, 0x07E8 |
| our documented ladder | 0x0064, 0x0065, 0x0067, 0x00D8, 0x0124, 0x0127, 0x01F7, 0x0242, 0x0243 |
| stock-only (20) | 0x0000, 0x0005, 0x000B, 0x003C, 0x0052, 0x0069, 0x006F, 0x0073, 0x0078, 0x00F7, 0x011E, 0x015D, 0x01CF, 0x01EE, 0x025F, 0x02BD, 0x03E4, 0x07D9, 0x07E7, 0x07E8 |

Stock-only key -> dispatch worker: 0x01CF->0x84D14, 0x03E4->0x84260,
0x0073->0x84920, 0x0000->0x840B4, 0x0069->0x839B4, 0x0005->0x82878,
0x025F->0x82850, 0x0078->0xA5824, 0x011E->0xA1A58, 0x00F7->0x45A30,
0x02BD->0x438F0, 0x003C->0x811B8, 0x01EE->0x4213C, 0x006F->0x81C44,
0x000B->0xA09F0, and 0x07D9/0x07E7/0x07E8 internal 0x4xxxxx/0x5xxxxx/
0xB5xxx workers.

## T6. SMAC ISR name table (CA 0x12266C) [V strings, I mapping]

Print order (5g/2g pairs, then msg-proc names, then event names):

| # | name | inferred body (band0 IRQ / band1 IRQ) |
| ---: | --- | --- |
| 1 | smac_coex_rx_abort_end_{5g,2g}_isr | 0xAEC74 (47 / 53) |
| 2 | smac_rx_complete_{5g,2g}_isr | 0xB53A4 (49 / 55) |
| 3 | smac_tx_complete_{5g,2g}_isr | 0xB0CE0 (50,83 / 56,85) |
| 4 | smac_common_timer_{5g,2g}_isr | 0xAEB80 (51 / 57) |
| 5 | smac_coex_pta_rx_abort_timeout_{5g,2g}_isr | 0xAEA9C (52 / 58) |
| 6 | smac_coex_tx_abort_end_{5g,2g}_isr | 0xAE45C (82 / 84) |
| 7 | smac_coex_pta_tx_abort_timeout_{5g,2g}_isr | 0xB0AB8 (59 / 60) |

Then 12 `smac_msg_proc_*` names (set_mu_edca_timer, ac_suspend,
set_cali_self_cts_tx_num, set_cali_ppdu_tx_num, rx_ring_reset,
update_tx_bypass_ctrl, beacon_tx_resume, beacon_tx_suspend, clr_hw_fifo,
tx_resume, tx_suspend, enable_trx, disable_trx) and per-band named handlers
`smac_tbtt_{5g,2g}_isr`, `smac_rx_timeout_*`, `smac_pre_tx_*`,
`smac_backoff_timeout_*`, `smac_user_queue_empty_*`, `smac_user_ps_*`,
`smac_tx_abort_start_*`, `smac_lifetime_expire_*`, `smac_tx_exit_*`.
The blob is 45 printable strings in total (the `smac_pre_tx_5g_isr`
entry is preceded by the 3-byte separator 01 02 03). `smac_tbtt_*` is NOT
among the 8 registered INTC sources => TBTT runs in firmware timer/task
context (consistent with task-214).

## T7. Probe pack (R7, read-only) [V addresses]

Device IO: BAR0 offset = CA + 0x3B8000 - 0x40000000, phys = CA + 0x3B8000.
Firmware RAM: BAR0 offset = CA + 0x6B8000, phys = 0x40000000 + CA + 0x6B8000.

| # | object | CA | width | band0 ref / band1 ref |
| ---: | --- | ---: | ---: | --- |
| 1 | wrapper +8 / +0x1C | 0x18081C / 0x180830 | 4 | 0x174BF0 / 0x175A18 |
| 2 | band0 obj +576 / +608 / +640 / +672 | 0x174E30 / 0x174E5C / 0x174E70 / 0x174E90 | 4 | RAM block / device chain A / RAM block (+576) / [ctx] |
| 3 | band1 obj +576 / +608 / +640 / +672 | 0x175C58 / 0x175C90 / 0x175C98 / 0x175CB8 | 4 | same probe on band1 |
| 4 | band0 ctx +228 / +232 (via +672 if RAM) | two-step | 4 | 0x00A00096 / 0 |
| 5 | band1 ctx +228 / +232 (via +672) | two-step | 4 | 0 / 0 (device default) |
| 6 | timer words | 0x401015CC / 0x400024B0 | 4 / 2 | 0 / 0x64 on both |
| 7 | TBTT block | 0x40101584/88/94/9C/A4/AC | 4 | per-band slots |
| 8 | MAC start / SMAC IRQ enable | 0x4011080C/0x4011480C, 0x40108214/0x4010C220 | 4 | R5 references |
| 9 | RAM blocks | 0x114F18 / 0x115128 / 0x183438 / 0x1817A0 / 0x180904 / 0x180E68 | 4 | attached-set check |

Rules: read only; two-step pointer follow only if the value is in firmware RAM
(0x00040000..0x00180000) or device IO (0x40000000..0x4011FFFF); never write;
never read the RC misc window.
