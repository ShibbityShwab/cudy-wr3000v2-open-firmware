# FRAME-SPECS - first MAC data-plane TX/RX, byte/register level

Companion to REPORT.md. Little-endian; evidence markers: [V] decoded from a
named vendor artifact (wifi.ko 2.4.15 md5 1745ef77..., FIRMWARE.bin sha256
7fc87e20...), [V*] measured by an earlier attended lane, [I] inference, [U]
unknown (the code fails closed). Builders/checks: src/dp_txrx.c,
tools/dp_txrx_selftest.c (254/254), tools/dp_txrx_static_decode.py (87/87).

## 1. Addresses and window mapping (where every object lives)

| object | device address | host access | notes |
| --- | --- | --- | --- |
| MAC host-interface band 0 | CA 0x40040000 | BAR0 = 0x3b8000 + (CA-0x40000000) = **0x3f8000** | region 3; +0x00..+0x54 register block [V]/[V*] |
| device-RAM MSDU-info ring-pointer table | ready g4 id2 payload dword0, bench **CA 0x01060850** [V*] | ACP alias CA 0x02060850 -> BAR0 = 0x4d8000 + (ACP-0x02000000) = **0x538850** | region 4; 12-byte entries, index tid+user*8 [V] |
| table entry (user0,tid0) | CA 0x01060850 / ACP 0x02060850 | BAR0 0x538850 | {ring_devva, wptr, rptr} |
| table entry (user0,tid1) | CA 0x0106085c | BAR0 0x53885c | 12-byte stride |
| TX MSDU-info rings | host-DRAM coherent, DMA 0x8xxxxxxx [V*] | CPU deref | 8 rings x 64 entries x 8 B; ring tid stride 0x200 B |
| TX frame buffer | host-DRAM coherent | CPU deref | 20-B host area + 16-B zero DMA header + MPDU |
| RX norm free ring | 256 x 4 B DMA array | +0x20 addr, +0x2c size, +0x38 wptr [V] | entry = buffer DMA + 4 |
| RX small free ring | 512 x 4 B DMA array | +0x24 / +0x30 / +0x3c | entry = buffer DMA + 4 |
| RX completion ring | 4095 x 4 B DMA array | +0x28 / +0x34 / +0x40 rptr, +0x4c device wptr | entry format [U] |
| RX buffer pools | host-DRAM coherent | CPU deref | norm 4+0x63c B, small 4+0xb0 B; device writes descriptor at +4 |
| tx_ba_info | 0x800 B DMA | +0x00 depth 0x100, +0x04 addr | alternative completion path, not used by the first frame |

Device-CA -> BAR0 uses the transport's range-checked region table
(hi5622_ca_to_bar0); the driver refuses any CA outside the six inbound regions.

## 2. TX: 8-byte MSDU-info ring entry (per user/TID ring, depth 64) [V]

    +0 u32 buf_devva   device VA of the DMA header (frame buffer + 4)
    +4 u32 ctl

| ctl bits | field | this lane |
| ---: | --- | --- |
| 11:0 | frame length (= skb->len in the vendor) | 802.11 MPDU length, no FCS |
| 14:12 | tx path class | 0 (vendor default branch) |
| 15 | tx header flag | 1 for management, 0 for data [I] |
| 19:16 | tid/priority nibble | 0 (QoS TID is also 0 in the built frames) |
| 21:20 | vap type 1/2/3 (vendor writes 0x10/0x20/0x30 at ctl byte 2 bits 4/5) | mgmt=1, data/QoS=2 [I] |
| 31:22 | not written by the vendor builder | 0 |

Vendor evidence: hmac_tx_msdu_ring_push 0x18f18 (decodes length, calls
shuangta_set_tx_msdu_info_ring, dmb ish); hmac_tx_frame_control_dscr_set
0x18b9c (bic/orr 0x30 + 0x10/0x20/0x30 into entry byte 2);
shuangta_tx_msdu_info_ring_magic_set 0x389fc (ctl byte 7 |= 0xE0 when the host
has processed the entry). A fresh entry must have ctl[31:24] = 0.

## 3. TX: 12-byte device-RAM ring-pointer table entry - the publish path [V]

    +0 u32 ring_device_va   set once by ring-init (vendor writes {devva,0,0})
    +4 u32 published_wptr   packed producer index; host writes after the entry
    +8 u32 device_rptr      device-consumed index; the host reads it back

Evidence: shuangta_tx_msdu_ring_buf_alloc 0x38f5c stores the slot fields
(ring host ptr +0x40, table entry host VA +0x44, ring DMA +0x48, ring index
+0x4c/+0x50), converts with pcie_if_hostca_to_devva and writes the table entry
{devva,0,0}; hmac_tx_msdu_ring_push stores the new packed wptr into
[table_entry+4] (and the host-side ring shadow) only after the 8-byte entry is
visible; hmac_tx_msdu_ring_proc 0x1cab4 loads the device rptr from
[table_entry+8], validates the pair (hmac_tx_msdu_ring_rw_index_check 0x18cc0)
and walks entries while setting the 0xE0 magic. **This corrects the E1 lane's
"table dwords 1/2 not decoded" open item.** Packed index =
index[14:0] | phase<<15; next: index++; if index==depth then index=0,
phase^=1.

Host publish order used by this lane (kernel dp_tx_frame):
1. build MPDU, place it in the coherent frame buffer at offset 20;
2. write the 8-byte ring entry for (user 0, tid, index wptr&0x7fff);
3. dma_wmb();
4. writel(packed next wptr, BAR0 table_entry+4); wmb();
5. read back the three dwords; fail closed if +0 != ring devva or +4 != wptr;
6. record the read-back +8 (device rptr) as the completion observable.

## 4. TX: frame buffer layout [V]/[I]

Vendor hmac_tx_msdu_dscr_add 0x19b40: pskb_expand_head(skb, 0x14) -> memset the
20 bytes below skb->data -> *(u32 *)(skb->data-0x14) = skb -> DMA map
(skb->data-0x14, skb->len+0x14) -> ring entry buf_devva =
devva(skb->data-0x10) (shuangta_get_msdu_info_ring_addr 0x38ad8: data-0x10).

Clean-room buffer (one coherent allocation per posted frame):

    +0x00  u32  host owner/bookkeeping word ("TXRX" 0x54585258 in this lane)
    +0x04  16 B zero DMA header  (the device-visible part; all zeros [V])
    +0x14  802.11 MPDU           (ctl frame_len counts only these bytes)
    ...    up to DP_TXRX_FRAME_AREA = 0x400 B

The device VA written into the MSDU entry is buffer+4, which is the DMA
address of the zero header in this driver's identity translation [V*] E1.
What the MAC does with the 16 zero header bytes is [I] (the vendor zeroes
them); the first bench frame is the experiment that settles it.

## 5. TX: the 802.11 frames this lane builds (no FCS; the MAC appends it)

| type | FC | length | header | body |
| ---: | ---: | ---: | --- | --- |
| 1 | 0x0040 probe request | 32 B | addr1=broadcast DA, addr2=addr3=BSSID, seq | SSID wildcard IE (id 0 len 0) + supported rates IE (id 1 len 4: 0x82 0x84 0x8b 0x96) |
| 2 | 0x0048 null-data | 24 B | addr1=broadcast, addr2=addr3=SA | none |
| 3 | 0x00c8 QoS-null | 26 B | same + QoS control 0 | none |
| 4 | 0x0088 QoS-data | 60 B | same | LLC/SNAP AA AA 03 00 00 00 08 06 + ARP request (documentation IPs 192.0.2.10 -> 192.0.2.1) |

SA/BSSID are the project's documentation-example address 02:00:68:69:35:01
(never a per-unit value). Broadcast destinations avoid an ACK/BA dependency
for the first frame. The sequence number increments per post (0..0xfff).

## 6. RX: MAC counters and the capture path (read-only)

| MAC band0 reg | role |
| --- | --- |
| +0x38 / +0x3c | host-published norm/small free-ring producers (0x8000 after a full fill [V*]) |
| +0x40 | completion-ring consumer index (host-owned) |
| +0x4c | completion-ring producer (device) [V] |
| +0x50 / +0x54 | device norm/small free-ring consumers [V] |

dp_rx_poll: bounded 2000 ms poll of +0x4c; pending = packed-index distance from
the host completion rptr (4095 entries). Then it scans all 256 norm and 512
small buffers in host DRAM for the 0x28-byte device-written descriptor
(shuangta_host_rx_get_msdu_info_dscr 0x3843c field accesses) and copies the raw
completion word + descriptor + up to 128 MPDU bytes into the read-only
dp_rx_capture buffer. **No ring pointer is moved and nothing is reposted**:
the completion-word -> (free ring, slot) decoder is still [U], so the consume
path exists in portable code but the ladder runs capture-only.

## 7. RX: 0x28-byte device descriptor (observed accesses only) [V]/[U]

| off | width | parser field | meaning |
| ---: | --- | --- | --- |
| 0x08 | u16 & 0x0fff | len_a | first segment/MPDU length [I] |
| 0x0c | u16 | word0c | [U] |
| 0x0e | u16 & 0x0fff | len_b | second length [I] |
| 0x10 | u32 | word10 | device pointer field [U] |
| 0x14 / 0x16 | u16 | word14/word16 | [U] |
| 0x18 | u16 & 0x0fff | len_c | length [I] |
| 0x19 | u8 bits 6:5 | band_vap | maybe band/vap [I] |
| 0x1b | u8 | flags_1b | [U] |
| 0x1c | u16 >> 4 | word1c | [U] |
| 0x1e | u16 | payload_len | vendor skb_put(len+0x28) length [V] |
| 0x20 | u16 & 0x0fff | word20 | length [I] |
| 0x21 | u8 bits 6:4 | flags_21 | [U] |
| 0x23 | u8 bits 3:0, bit7 | flags_23 | tid?/flags [U] |
| 0x28 | - | - | 802.11 MPDU start |

Acceptance is fail-closed: payload_len must be >= 0x28 and <= the buffer DMA
area (0x63c norm / 0xb0 small); otherwise the candidate is refused and the raw
bytes are still captured as evidence.

## 8. Ring index bookkeeping [V]

Packed = index[14:0] | phase<<15. Distance producer->consumer: if phases
equal, p-c (p>=c required); if phases differ, depth-c+p (valid 1..depth).
Depth: TX ring 64, RX norm 256, RX small 512, completion 4095. The host keeps
the completion rptr at MAC +0x40 and the free-ring rptrs internally; the
consume path advances completion rptr and the matching free-ring rptr, and a
repost writes the same buffer devva back into the free ring at the tail.
