# FRAME-SPECS - byte-level and register-level specs for the data-plane host side

Companion to REPORT.md. Little-endian; evidence markers as in REPORT.md:
[V] decoded from a named vendor artifact, [V*] device-measured earlier,
[I] inference, [U] unknown. Builders/checks: src/dp_rings.c,
tools/dp_logic_selftest.c (69/69), tools/dp_static_decode.py (34/34).

## 1. g0 id1 key 0x0320 TXQ-table address sync (the one new frame) [V]

Vendor call: hdpp_config_send_event(vap, cmd=0x320, len=4, data=&dev->txq_table_dma)
from hmac_txq_table_addr_sync (wifi.ko 2.4.15 0x1af00 / 2.5.24). The 4-byte data
is the device VA of the 0x2524-byte zeroed host-DRAM TXQ table (vendor
oal_noncache_alloc; this driver dma_alloc_coherent, window-checked).

    header : 00 01 00 00 20 00 01 00 00 00 5a 5a       (group 0, alloc 1, fb 0,
                                                        total 0x20, id 1, tag)
    payload: 00 00 00 00 00 00 00 00                   token[8] = 0
             20 03                                     u16 key = 0x0320
             04 00                                     u16 len = 4
             <devva LE 4 bytes>                        host TXQ table device VA
             00 00 00 00                               pad[4] = 0

Example devVA 0x84543c00 -> 32-byte frame:

    000100002000010000005a5a000000000000000020030400003c548400000000

Node length = declared total (0x20); node word1 = (0x20<<16)|0x6d2b. Gate: this
load's context-create, one per load, plus the inherited budget/phase/pending
gates. The firmware worker CA 0x81445 (Thumb code at CPU 0x81444) executes
`*(u32 *)(band_array_base + 0x252c*band + 0x2524) = data[0]` and returns 0.

## 2. MAC slot-5 register program (band 0 CA 0x40040000) [V]/[V*]

Executed by dp_setup in this order; every write is read back and the masked
fields compared; the first mismatch stops the program.

| # | off | width/mask | value | role |
| ---: | ---: | --- | --- | --- |
| 1 | +0x10 | 32, full | ACP(ready id2) e.g. 0x02060850 | MSDU-info ring-pointer table base |
| 2 | +0x1c | 32, full | 0x00b0063c | rx_norm_len[15:0] / rx_small_len[31:16] |
| 3 | +0x20 | 32, full | devVA(norm free ring) | RX norm free ring address |
| 4 | +0x24 | 32, full | devVA(small free ring) | RX small free ring address |
| 5 | +0x28 | 32, full | devVA(cmp ring) | RX completion ring address |
| 6 | +0x2c | bits 11:0 | 256 | norm ring size |
| 7 | +0x30 | bits 11:0 | 512 | small ring size |
| 8 | +0x34 | bits 11:0 | 4095 | completion ring size |
| 9 | +0x00 | bits 15:0 | 256 | tx_ba_info depth |
| 10 | +0x04 | 32, full | devVA(tx_ba_info) | tx_ba_info buffer address |
| 11 | +0x48 | 32, full | 0xfffffcc1 | host MAC interrupt mask (vendor init) |
| 12 | +0x38 | 32, full | 0x8000 | norm free-ring wptr (index 0, phase 1) |
| 13 | +0x3c | 32, full | 0x8000 | small free-ring wptr |
| 14 | +0x40 | 32, full | 0 | completion-ring rptr |

Read-only diagnostics: +0x44 int status (W1C; not touched), +0x4c cmp wptr,
+0x50 norm rptr, +0x54 small rptr (device producers/consumers).

## 3. Host-DRAM ring geometries [V]

| object | size | entries | entry | notes |
| --- | ---: | ---: | ---: | --- |
| TXQ table | 0x2524 B | - | - | zeroed; per-user blocks at +0x524+user*0x40: 8x u16 len, 8x u16 sn, 8x u32 bytes; device VA sent by the 0x320 frame |
| TX MSDU-info rings | 8 x 64 x 8 B | 8 TIDs x 64 | 8 B | {u32 buf_devva; u32 ctl} ctl[11:0] len, [14:12] path, [15] hdr flag, [19:16] tid, [21:20] vap type |
| tx_ba_info | 0x800 B | 256 | 8 B host view | depth field 0x100 |
| rx_norm_data_free_ring | 0x400 B | 256 | 4 B devVA | entry = buffer dma + 4 |
| rx_small_data_free_ring | 0x800 B | 512 | 4 B devVA | entry = buffer dma + 4 |
| rx_data_cmp_ring | 0x3ffc B | 4095 | 4 B [U] | allocated/programmed, not consumed by this lane |
| RX norm buffers | 256 x 0x640 B | - | - | 4-B host bookkeeping word + 0x63c B DMA area |
| RX small buffers | 512 x 0xb4 B | - | - | 4-B host word + 0xb0 B DMA area |

Ring indices are packed `index[14:0] | phase<<15`; publishing one entry advances
the index and toggles the phase on wrap. After filling a ring exactly once the
producer is 0x8000 (live vendor value [V*]). Buffers are zeroed coherent
allocations; the host owns byte 0..3 of each buffer, the device DMA area starts
at +4 (the value put in the ring).

## 4. Device-RAM MSDU-info ring-pointer table [V]

    table_ca  = ready g4 id2 payload dword0     (PKTRAM CA, bench 0x01060850)
    table_acp = table_ca + 0x01000000           (ACP alias, bench 0x02060850)
    entry(user,tid) CA = table_ca + 12*(tid + user*8),  user <= 0x91, tid <= 7

    12-byte entry at ring init: {u32 ring_device_va; u32 0; u32 0}

The host writes the entries through the ACP alias (region 4: BAR0 = 0x4d8000 +
(ca - 0x02000000)) and reads them back; the firmware reads the table by its own
device-side mapping of the same ACP area. MAC +0x10 holds table_acp.

## 5. Rings vs the ETE plane (why this does not need the A/B copy engine) [I]

The ETE register file (CA 0x4003a000) carries HCC messages; the MAC host
interface (CA 0x40040000) carries frames. The two blocks are owned by distinct
vendor modules (plat.ko vs wifi.ko) and share only the address-conversion
helper. RX frame deposits are MAC DMA into the free-ring buffers plus a
completion-ring entry and/or host-MAC interrupt, not an ETE transfer; TX frame
fetches are MAC DMA from the MSDU-info rings. So the idle ETE A->B copy (the
reason for the D2H direct reader) is irrelevant to the frame plane. This
remains [I]/working hypothesis until a bench run observes a completion.
