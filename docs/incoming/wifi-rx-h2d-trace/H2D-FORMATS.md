# H2D-FORMATS - the wire formats used to validate a captured vendor H2D sequence

Everything here is **derived text**: field maps and addresses only; no vendor binary, no firmware
image byte, no calibration value. Sources: the committed phase 17/20/24/25 decode lanes **[V\***],
the committed `wifi-rx-init-msg` tables **[V\***], and this lane's re-verification of the module
symbols/relocations **[V]**.

Host base addresses (device CA <-> host mapping through BAR0's region-3 viewport):
`host = 0x403b8000 + (CA - 0x40000000)` - so the ETE window (CA `0x4003a000`) is at host
`0x403f2000`.

## F1. SR channel registers (H2D ring), 0x50-byte stride [M / V*]

Channel blocks: ch0 `CA 0x4003a400` / host `0x403f2400`; ch1 `+0x50`; ch2 `+0xa0`.
Measured live values on a normal vendor boot (phase-20 A.7): ch0 base `0x84543000`, depth-1 `0x1f`,
producer `0x14`, consumer `0x14`; ch1/ch2 idle.

| reg | width | meaning |
| --- | --- | --- |
| `+0x08` | u32 | ctrl (vendor leaves 0 at runtime; the init writes `cfg[5]` nibble then the device clears it) |
| `+0x10` | u32 | **node-array base (host physical)** |
| `+0x14` | u32 | depth-1 (measured `0x1f` = 32 nodes) |
| `+0x18` | u32 | **producer index** (the commit value `pcie_ete_sr_reg_init` writes; packed `index[9:0] | phase[10]` per phase-20 A.4; observed plain `0x14`) |
| `+0x1c` | u32 | consumer index (device side) |
| `+0x30` | u32 | side/device-SRAM pointer (not a host ring) |
| `+0x38` / `+0x3c` | u32 | cached producer/consumer neighbours (observed equal to +0x18/+0x1c) |

## F2. SR descriptor node (8 bytes, host DRAM) [V* / V]

```
u32 word0 = message-buffer address   (host physical under the vendor identity inbound window)
u32 word1 = (len << 16) | flags      flags = 0x0d2b | 0x2000 | 0x4000 = 0x6d2b
```

`len` is the HCC frame length (it must equal the frame's own `+4` total: the vendor invariant
measured in phase 25). The ring wraps at `depth` (32); the producer carries the phase bit.

## F3. HCC frame header (12 bytes) and payload

```
off  size  field
0    1     type: bits[3:0] = group (0 = chip, 1/2 = band core; 3 seen in vendor tables)
1    1     bid: bits[7:4] = destination band/core, bits[3:0] = source band
2    1     vap id
3    1     chip id
4    2     total frame length (u16, includes the 12-byte header)
6    2     message id (u16) - the dispatch key within the group
8    1     reserved (0)
9    1     dst (written by hcc_msg_tx = bid[7:4])
10   2     transport tag 0x5a5a (vendor SR/tag constant)
12   ...   payload
```

Byte-exact oracle (live vendor, first post-ready frame - committed in phase 20 A.5 and re-attributed
in `wifi-rx-init-msg`): 
```
00 01 00 04  30 00  01 00  00 00  5a 5a   | 00 00 00 00 01 00 00 00 | d8 00 14 00 | <20 B data> | ff ff ff ff
g0 bid=01    total=0x30 id=0001 tag      preamble (8 B)            key=0x00d8 len=0x0014  data        tail
```
This is a **g0 id1 config event** (group 0, id 1) with key `0x00d8`, data length 20,
total `0x30 = 28 + data_len`. It is what our `h2d_txq_sync` frame replicates byte-exactly
(`wifi-h2d-send` lane) - it is the decoder's regression oracle.

## F4. g0 id1 config-event geometry (the main H2D family)

```
off  size  field
0    12    HCC header (F3), id must be 1, group 0
12   8     preamble (the vendor capture: 00 00 00 00 01 00 00 00)
20   2     config key (u16)         <- the firmware key-table-A dispatch key
22   2     data length (u16)
24   N     data
24+N 4     0xff tail (inside total; the vendor capture's geometry)  => total = 28 + N
```

Firmware reception is already decoded: handler CA `0x43D58` reads `ctx = lookup(group,hdr[3])`,
`key = u16 hdr+0x14`, `data = hdr+0x18` **[V\***, `wifi-ctx-create`]. The 8 firmware-live keys are
`0x0004, 0x0064, 0x0065, 0x0066, 0x0067, 0x0069, 0x00D8, 0x0320` **[V\***].

## F5. The id/key spaces (for the diff)

| space | enumeration | source |
| --- | --- | --- |
| g0 id1 keys | 244 distinct keys, 266 call sites, 77 builders | `wifi-rx-init-msg/TABLES-config-senders.md`, `TABLES-h2d-builders.md` **[V\*]** |
| direct ids | ids 0x00-0x2d + 0x94/0x4da style sub-ids seen in the builder table | same **[V\*]** |
| band-core (`hcc_msg_tx_to_core`) ids | 1 `hmac_tx_post_event`, 2 `shuangta_irq_host_mac_isr`, 3 `hmac_rx_data_send_continue_msg`, 4 `hmac_tx_complete_notify_other_core`, 6 `shuangta_irq_host_ba_info_ring_overrun_isr`, 8 `hmac_ba_timeout_fn`, and `shuangta_always_rx_output_format` ids 0/7 | `TABLES-h2d-builders.md` **[V\*]**, re-read by this lane |
| firmware-live g0 id1 keys | 8 | `wifi-ctx-create` / `wifi-rx-init-msg` **[V\*]** |

## F6. Decoder validation rules (implemented in `tools/decode_h2d.py`)

A captured frame is **VALID** iff all hold; otherwise it is **QUARANTINED** (kept, counted, never
silently dropped):

1. node `word0 != 0`, `12 <= node_len <= 0x700`, and `(word1 & 0x1fff) == 0x6d2b`
   (accept `0x2d2b|0x6d2b` variants flagged as `flags-unexpected`);
2. frame has at least `node_len` bytes read;
3. `u16 @ +10 == 0x5a5a` (transport tag);
4. `u16 @ +4 == node_len` (the vendor's own invariant; a mismatch is `len-mismatch` and usually a
   recycled/torn read - quarantine);
5. `group = byte[0] & 0xf` in `0..3`; `id = u16 @ +6` in `0..0x3f`;
6. for g0 id1: `node_len >= 28`, `28 + u16@+22 == node_len`, and the last 4 bytes are
   `ff ff ff ff` (a mismatch is reported but not fatal - vendor builds may vary the tail).

Classification keys: group 0 id 1 -> `(0,1,key)`; else `(group,id)`; plus the `vap/chip/bid` bytes
reported for every frame so a band/vap-specific difference is visible.

## F7. The "ours" set this is diffed against (encoded in `tools/ours_h2d_set.txt`)

```
g0 config key 0x0064   ctx create / vap-add carrier     (ours sends)
g0 config key 0x00D8   TXQ MIB config                   (ours sends; vendor first post-ready)
g0 config key 0x0320   TXQ address sync                 (ours sends)
g0 config key 0x0065   VAP start                        (ours sends)
g0 direct id 0x0006    mgmt TX                          (ours sends)
g0 direct id 0x0007    user add                         (ours sends after the 5 GHz order fix)
g0 direct id 0x0008    user add notify                  (ours sends after the 5 GHz order fix)
WAL group-2 cmd 0x0124/0x0005/0x006D/0x0127/0x0065  AP start (ours sends)
```

Everything else the decoder reports in **MISSING-IN-OURS** is a candidate for RX-2/F1. The current
static candidates are documented in REPORT §6: g0 config key `0x0004` (unaccounted, task-170) and
the **band-core dimension** (no to_core builder is replicated by our driver).
