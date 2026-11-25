# TABLES - the corrected family table and the evidence per family

Source: `/tmp/h2d-capture.log` (2026-11-28, sha256 `970b80064896da8eaf11272119f1c70fd6a04bfb3003e34c5f899804f75b1679`,
2603 lines, 1716 M-records, 210 s). Regenerate the numbers with
`python3 tools/decode_f1_capture.py /tmp/h2d-capture.log` (output committed as
[evidence/FAMILY-TABLE.txt](evidence/FAMILY-TABLE.txt)). All addresses are CPU addresses
(CA = file offset + 0x40000). Grading: **[V]** verified by this lane from a named local artifact;
**[V\*]** committed earlier lane; **[M]** bench-measured earlier; **[I]** inference; **[U]** unknown.

## 1. Why the raw family split over-counts

The sampler emits one `M` line per ring node; `len` is the node's own length word and `total`
is the frame's `+4` field. Both are read separately from host DRAM, so a node read across a host
update (or a recycled buffer) yields `len != total` and possibly stale leading bytes. In this
capture: **1716 raw records / 604 valid (`len == total`) / 1112 torn** (of the torn ones 250 are
unreadable: declared total longer than the bytes read). Valid == true host->device sends.

## 2. Valid H2D families (what the stock really sends)

| # | family | n | frame geometry (protocol bytes as text) | builder / decoder |
| --- | --- | ---: | --- | --- |
| 1 | g0 id1 config events | 254 | 12-B HCC header + 8-B token (mostly zero) + u16 key + u16 data length + data + 4-B tail; total = 28 + data_len | `hmac_config_send_event`/`hdpp_config_send_event`; key at frame+20 = payload+8 [V\*] |
| 2 | g0 id29 EDCA opt timeout | 329 | header `00 01 00 <fb> 48 00 1d 00 00 00 5a5a` + 20 x 00 + 40 x ff (72 B total) | `hmac_edca_opt_timeout_fn` @0xef43c [V] |
| 3 | g0 id2 alg request | 2 | header `00 01 00 05 27 00 02 00 ...` + token + key 0x0101 + subkey 0x0b + 15-B data (39 B) | `hmac_config_alg_send_event` family [V] |
| 4 | g0 id12 scan request | 1 | header `00 01 00 04 08 02 0c 00 ...`, 508-B payload (520 B) | `hmac_enc_scan_send_req` (alloc(508) @0x9ab74) [V] |

### 2.1 g0 id29 - the EDCA timeout, byte-exact [V]

Vendor wifi.ko `hmac_edca_opt_timeout_fn` @0xef43c:

```
ef46c-ef47c  memset_s(payload, 60, 0)            ; payload buffer on the stack
ef480-ef490  memset_s(payload + 20, 40, 0xff)    ; the 0xff tail
ef7c8        hcc_msg_alloc(60, clear=0)          ; size 0x3c
ef7e0-ef800  hdr[0] &= 0x0f (group nibble 0), hdr[7] = 0, hdr[6] = 0x1d
ef804-ef814  hdr[1] = (dst << 4) | alloc flag    ; dst = ctx[+4]
ef81c-ef828  hdr[2] = ctx[+6]; hdr[3] = ctx[+1]  ; field B = per-chip selector
ef830-ef838  memcpy(hdr + 12, payload, 60)
ef848        hcc_msg_tx(msg)
```

The captured frame is byte-identical: `00 01 00 <fb> 48 00 1d 00 00 00 5a5a` +
`00`*20 + `ff`*40. `<fb>` = 1/2/4/5 in the capture (per-chip selector; 329 valid records);
`hdr[0]`'s high nibble is uninitialised in the vendor (alloc clear=0) - 33 records show a stale
high nibble (0x10/0x50/0x60/0x70/0x80) while the dispatch nibble is 0. Payload data is **all zero**
in all 329 records ("timeout, no EDCA data") and the traffic is steady from 26.8 s at ~0.44 s
intervals across chips.

Firmware side [V]: the firmware group-0 table (committed `wifi-h2d-catalog`) has id `0x1d` ->
handler CA `0xB568C` (file `0x7568C`), verified here: it copies the 60-B payload to a local
struct and calls the registered hook `G+0x378` with (hdr[3], payload), `G` = CA `0x1747C4`.
`G+0x378` = firmware `0x68C0A` (installer CA `0x5C3B6`, committed `wifi-fw-callback-install`);
it looks the per-chip module context up via `0x961AE`, checks the RF chain word, processes the
payload as four 5-byte EDCA entries (payload[0..19]) and writes the per-chip context fields
`+0x4aa` (checksum), `+0x533` (per-AC/user flags) and `+0xdb` bit 2. **It is a per-chip EDCA/MAC
context sync, not an RX-ring or TRX enable; the handler sends no D2H reply.**

## 3. Torn content that is still a real H2D message

| class | n | decode |
| --- | ---: | --- |
| residue g2 id29 / 16 B | 328 | **`hcc_timer_timeout_proc`** (plat.ko 0x13410, verified here): `hcc_msg_alloc(4,0)`; `hdr[0]=2`, `hdr[3]=0`, `hdr[6]=0x1d`, `hdr[7]=0`, `hdr[12]=r4` (the HCC timer context pointer); `hcc_msg_tx`. Captured payloads are host kernel pointers (`e02250c4`, `104a50c4`, `30d9bcbf`, ...) => host-built. The firmware group-2 table has **no** id 0x1d handler (`wifi-h2d-catalog`) => likely dropped device-side; do not fire (pointer payload). |
| residue g9 id29 / 16 B | 4 | same content with stale leading bytes (`49 4e 47` = "ING" ...) - an alias of the same timer message. |
| collapse g0 id1 keys 0x00d8/0x000b/0x07e8/... | 151 | torn records whose payload+total match a valid config event; counted under the valid families above. |

## 4. Alias: g12 id1 is not a family [V]

18 of the 21 `g12 id1` records are **valid by length** but their content signature
`(total = 184, payload)` is byte-identical to the `g0 id1 key 0x0127` (set_beacon) config events;
only the first four header bytes differ:

```
g0 id1 key 0x0127 : 00 01 00 01 | b8 00 01 00 00 00 5a 5a | 00*8 | 27 01 9c 00 | data 156 | tail 4
"g12 id1"         : 0c 18 30 00 | b8 00 01 00 00 00 5a 5a | 00*8 | 27 01 9c 00 | data 156 | tail 4
```

The four stale bytes are the partially rewritten 12-byte HCC header (builder writes bytes 4..11 and
the payload before/around bytes 0..3). The canonical family is `g0 id1 key 0x0127` (20 + 18 = 38
valid sends). **Not an RX message** (set_beacon; the beacon path is already under the 5 GHz lanes).

## 5. Residue that is device->host (D2H) content in recycled buffers

| class | n | evidence |
| --- | ---: | --- |
| g4 id4 HEARTBEAT / 22 B | 83 | **byte-identical to the firmware's D2H heartbeat builder** at CA `0xC6BB2` (see REPORT sec. 2): alloc(10), `hdr[0]=4`, `hdr[3]=0`, `hdr[6]=4`, `hdr[7]=0`, 10-B payload copied from the `HEARTBEAT` literal CA `0x106328`, sent with firmware tx `0x102760`. No host module contains the literal; the vendor host handler `heartbeat_msg_process` (plat.ko 0x10214) only restarts a 10-s hrtimer. |
| g4 id0 / 66, 438, 111, 33, 1824 B | 117 | firmware OAM/device-log blobs (the 1824-B one starts with ASCII `driver`; the 0x7e-prefixed ones are log records) - the committed D2H tables list g4 id0 = OAM log [V\*]. |
| g2 id9 / 40 B | 56 | text-ish payload (`vap0/vap3` + the stock SSID) - [I] D2H log class, not an H2D frame. |
| g1 id37 / 36 B, g1 id3 / 48 B, g1 id17 / 124 B, g1 id38 / 16 B, g2 id37 / 24 B, g2 id8 / 40 B | 52 | no vendor host builder in the committed sender catalogue and (for groups 1/9) no firmware HCC receive table in the image => D2H event class [I]. g1 id3 was already observed as a D2H firmware response [V\*], g2 id8 is the host's D2H acs-echo clone target [V\*]. |
| g2 id0 / 188 B, 44 B | 121+2 | [U] torn residue: the visible prefix is a config-event-style blob (key 0xD8/0x65 fragments, host kernel pointers) but no vendor builder matches a group-2 id-0 message and no firmware group-2 id-0 handler exists. Not a trustworthy send. |
| g0 id1 / 12 B, 29 B | 41+21 | [U] torn records whose declared total is shorter than the config-event geometry (partial buffer overwrite/scan artifact); no vendor send has key-less g0-id1 payloads. |

## 6. The diff against our driver [M/V]

The known bench diff (parent lane) is **108 matched / 0 drift**; the capture is **consistent with
that count being 104 x g0 id1 key 0x00d8 (the first post-ready TXQ/AMSDU-MIB config) + 4 x g0 id1
key 0x0065 (VAP start)** (those are the only families our documented send set shares with the
stock's valid set). Nothing else our driver sends appears in the stock boot, and nothing the
stock sends is an RX enable (sec. 3 of REPORT). Our driver must **not** add: the HEARTBEAT (D2H),
the key 0x0004 config event (never sent by the stock - killed candidate), or the g2 id29 timer
(pointer payload, no firmware handler). The one implementable missing class is g0 id29 - already
built in the driver as `edca`.
