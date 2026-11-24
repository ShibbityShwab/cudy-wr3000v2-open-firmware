# FRAME-SPECS - byte-level builder specifications for the first data-plane messages

Companion to REPORT.md. All byte orders little-endian; u16/u32 unaligned-safe. Every layout
has an evidence marker: [V] decoded from firmware/vendor instructions or a live capture,
[I] inference, [U] unknown. Builders: src/dp_frames.h; selftest: src/dp_selftest.c (77/77).

## 1. HCC 12-byte header (all H2D frames on the ETE SR ring)

| off | width | field | H2D value rules |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group = low nibble | 0 (chip) or 2 (core) only; 4 is D2H-only [V] |
| +0x01 | u8 | alloc state low nibble / dst high nibble | low = 1; high = zero for dst0 [V] |
| +0x02 | u8 | field A (hdpp: vendor hdr[6]; hmac: vap id) | builder 0 unless case-specific [V] |
| +0x03 | u8 | field B (module index the fw handler resolves) | e.g. 4 for create/sync [V]/[V*] |
| +0x04 | u16 | total length = payload + 12 | nonzero, <= 0x680 [V] |
| +0x06 | u16 | message id | dispatch key in group [V] |
| +0x08 | u8 | field C (wal: sequence counter) | 0 [V] |
| +0x09 | u8 | dst = hdr[1] >> 4 | must equal hdr[1]>>4 [V] |
| +0x0a | u16 | transport tag | 0x5a5a, stamped by the SR/ETE layer [V] |
| +0x0c | - | payload | max 0x674 B [V] |

Firmware hard checks on receive: tag == 0x5a5a (file 0x8699e), declared total != 0
(file 0x869b0), then dispatch on hdr[0]&0xf and u16 hdr[6] (../../phase37). Rejection order
mirrored in dp_frame_validate().

## 2. Group-0 config event (id 1 hdpp / id 2 alg) [V]

Payload layout at hdr+0x0c, total = 0x1c + len:

    +0x00  token[8]     inherited ring-buffer bytes; the validated frames carry
                        token = 00 00 00 00 01 00 00 00 (word0 0, word1 1)
    +0x08  u16 key      id1: hdpp command key (section 3); id2: alg cfg id
    +0x0A  u16 len      data length
    +0x0C  data[len]
    +len   pad[4]       inherited; validated frames carry ff ff ff ff

Header: byte0 00, byte1 01, byte2 fieldA, byte3 fieldB, byte4..5 total, byte6..7 id,
byte8 00, byte9 00, byte10..11 5a 5a.

Builder: dp_build_g0_cfg(f, id, fb, key, data, len).

## 3. Data-plane keys of the group-0 id-1 command table

Dispatch: firmware handler CA 0x43d58 -> key table A (CA 0x10c23c, 217 slots) then B
(CA 0x103bf8, 88 slots); worker = 8-byte record {u16 key; u16 pad; u32 worker} [V].

    key 0x0064  worker 0x449e5  context create (data block below)
    key 0x00d8  worker 0x81039  TXQ config sync (data[0] only observed)
    key 0x0320  worker 0x81445  TXQ-table address sync (4-byte devVA)
    key 0x0004  worker 0x811cd  module flag write through created ctx
    key 0x0078  worker 0xa5825  key install
    key 0x0128  worker 0x8313d  hmac band config (table B entry 28)
    key 0x0311  worker 0x436f9  waveapp scene config

Full decoded tables: tables/firmware-config-keys.md
(regen: tools/fw_dp_tables.py --emit-md ...; assertions: --selftest 35/35).

### 3.1 Context-create frame (key 0x0064) - bench-validated [V]/[V*]

    header : 00 01 00 04 30 00 01 00 00 00 5a 5a
    payload: 00 00 00 00 01 00 00 00   token (word1 = 1)
             64 00                     u16 key   = 0x0064
             14 00                     u16 len   = 0x0014
             03 04 00 00 04 00 00 00   data: type=3, parent=4, ... user=4 at data[4]
             00 00 00 00 00 00 00 00
             00 00 00 00
             ff ff ff ff               inherited pad

48-byte frame (node length = total):
000100043000010000005a5a0000000001000000640014000304000004000000000000000000000000000000ffffffff

Builder: dp_build_ctx_create(f, type, parent, user, fb). data[0] in {1,2,3}; data[1] =
parent module; data[4] = module created (4 = the TXQ sync's field B); all other data bytes
zero; len 0x14 (0x13 is the firmware's highest read).

### 3.2 TXQ config sync (key 0x00d8) - vendor live capture replay [V]

    header : 00 01 00 04 30 00 01 00 00 00 5a 5a
    payload: 00 00 00 00 01 00 00 00   token
             d8 00                     u16 key = 0x00d8
             14 00                     u16 len = 0x0014
             01 00 00 00 + 16 x 00     data (only data[0] is consumed: stored to ctx+0x190+0x4c)
             ff ff ff ff               pad (inherited, as captured)

48-byte frame:
000100043000010000005a5a0000000001000000d80014000100000000000000000000000000000000000000ffffffff

The capture's ring node declared a 72-byte buffer while the header declares 48 (inherited
buffer length); the header-consistent rule is node length = frame total. Builder:
dp_build_txq_sync_capture(); dp_frame_export(..., pad_to=72, pad_byte=0xff) reproduces the
capture-style 72-byte buffer.

### 3.3 TXQ-table address sync (key 0x0320) - decode of the shipped host module [V]

Vendor call: hdpp_config_send_event(dev, cmd=0x320, len=4, data=&devva) from
hmac_txq_table_addr_sync (wifi.ko 0x1af00 in 2.4.15; same shape in 2.5.24). The 4-byte
data is the device VA of the 0x2524-byte host-DRAM non-cached TXQ table
([device+0x948], the DMA address from oal_noncache_alloc).

    header : 00 01 00 00 20 00 01 00 00 00 5a 5a      (fb = 0)
    payload: 00 00 00 00 00 00 00 00   token (all zero for this builder)
             20 03                     u16 key = 0x0320
             04 00                     u16 len = 4
             <devva LE 4 bytes>
             00 00 00 00               pad (builder leaves zero; firmware ignores)

Total 0x20 = 32 bytes. Example devVA 0x84543c00:
000100002000010000005a5a000000000000000020030400003c548400000000

Builder: dp_build_txq_addr_sync(f, txq_devva, fb).

## 4. TX MSDU-info ring entry (8 bytes) [V]

    +0 u32 buf_devva   device VA of the frame buffer (vendor: shuangta_get_msdu_info_ring_addr
                       + pcie_if_hostca_to_devva; host/device transform needs bench check [I])
    +4 u32 ctl         see bit map

| bits | field | vendor source |
| ---: | --- | --- |
| 11:0 | frame length | skb->len (12-bit bound) |
| 14:12 | tx path class | from skb path class byte |
| 15 | tx header flag | from the 802.11 header info |
| 19:16 | tid / priority nibble | from the tx frame header |
| 21:20 | vap type 1/2/3 | hmac_tx_frame_control_dscr_set writes 0x10/0x20/0x30 |
| 31:22 | not observed written | zero |

Builder: dp_tx_msdu_entry_encode(entry, buf_devva, frame_len, path_class, hdr_flag, tid,
vap_type).

Per frame the vendor also builds a 20-byte DMA descriptor at skb->data-0x14 (first dword =
host skb pointer, dma_mapped, then the 802.11 MPDU) [V]; the ring entry points at the
buffer, and the exact header/ring address relationship is the open [I] of REPORT.md 6.2.

## 5. Ring index encodings [V]

TX MSDU-info ring (per user/TID) and MAC RX ring wptr/rptr share the same packing:

    packed = (index & 0x7fff) | (phase << 15)
    next:   index++; if index == depth { index = 0; phase ^= 1; }

Master interrupts / cache: publisher must dmb ish + dsb ishst and sev after publishing
(vendor hmac_tx_msdu_ring_push). Helpers: dp_tx_idx_pack/unpack/next.

## 6. MAC slot-5 register program (host writer spec) [V]/[V*]

See REPORT.md 2.1 for the full table with live values. Band 0 CA 0x40040000, band 1 CA
0x40060000. Program order recommended:

1. +0x10 msdu_info_ring_ptr_table_base = ACP(ready g4 id2 payload; live 0x02060850)
2. +0x1c rx_norm_buff_len | rx_small_buff_len << 16  (0x063c | 0x00b0 << 16)
3. +0x20/+0x24/+0x28 free/cmp ring device VAs (pcie_if_hostca_to_devva)
4. +0x2c/+0x30/+0x34 ring sizes 256/512/4095
5. +0x00/+0x04 tx_ba_info depth (256) / device VA
6. +0x48 interrupt mask (vendor live 0xfffffcc1), then unmask handled bits
7. read back every word; only then advance +0x38/+0x3c producers

## 7. RX buffer layout [V]

    +0x00 .. +0x27   device-written RX descriptor (0x28 bytes; REPORT.md 7.2)
    +0x28 ..        802.11 MPDU (one per completion entry; A-MSDU sub-MSDU chains use
                     additional buffers linked by the descriptor fields [U])

Small buffers are 0xb0 B (176) => the 0x28 descriptor leaves 0x88 B for a small frame;
normal buffers are 0x63c B (1596). Builder/constants: DP_RX_DESC_LEN, DP_RX_NORM_BUFLEN,
DP_RX_SMALL_BUFLEN.

## 8. Message catalogue rows used by the validator

src/dp_frames.c dp_msg_catalogue[] carries group/id, vendor sender name, firmware handler CA
and fixed payload size for 46 data-plane rows (from ../wifi-h2d-catalog + phase37). It is
used to refuse frames for (group,id) the firmware cannot receive (group 4 H2D, NULL slots)
and to document which payloads are catalogue-only (no builder - structs [U]).
