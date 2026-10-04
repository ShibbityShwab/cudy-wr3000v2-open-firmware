# word-semantics: the ring descriptor head word `0xXX016000` and the vendor's node words (phase 42)

Task `st_01a10708` (parent `01a0fc5c`). Static, read-only analysis: no device access, no register
write. Every claim below is tagged **[proven]** (a byte sequence in a binary that disassembles to the
quoted instruction, re-checked by the script in §9), **[observed]** (a value in the read-only
snapshots) or **[unknown]**.

Sources (hashes as given in the task):

| file | md5 | use |
| --- | --- | --- |
| `build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | host ETE ring fill/commit + ring-init code |
| `build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` | wifi host RX/TX descriptor queue code |
| `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` (928,920 B) | device-side (D2H) fill/commit/notify |
| `build/tmp/{msg,ete,ete2}-normal-op-20261004.txt` | - | the three 1024-word window snapshots |

Address conventions (unchanged from the record): `.ko` addresses are **`.text` offsets** with
`.text` at file offset `0x38` (`.text` `sh_addr` = 0); `.ko` code is **ARM** (`CS_MODE_ARM`).
Firmware addresses are **file offsets**, Thumb (`CS_MODE_THUMB`), runtime = file + `0x40000`.

Companion: `block-structure.md` in this directory maps the *slot* geometry from the same snapshots
(24 slots of 0x200, a 4-word/16-byte head at `+0x00` and payload from `+0x10`, and the marker word
behaving as a per-record tag rather than a monotonic counter). This file covers the other half of the
phase-42 question: what those words *mean* in the vendor's own descriptor format, from the fill,
commit and notify code in the two modules and the firmware.

---

## 1. The answer in one block

1. **The vendor's descriptor is an 8-byte "node" of exactly two words.**

   | word | meaning | written by |
   | --- | --- | --- |
   | `word0` | **buffer device address** (the payload buffer the transfer reads/writes) | fill (host and device), `str r1,[r3,r2,lsl#3]` |
   | `word1` | **`(len << 16) \| flags`** — bits 31:16 = length, bits 15:0 = flags | fill (host and device) |

   The flag field is 16 bits wide: bits 0:12 are a per-direction flag value (`0xd2b` in every
   vendor fill on both sides) and bits 13 and 14 are set by the fill as "descriptor present/valid"
   (`0x2000` and `0x4000`, together **`0x6000`**). There is **no third or fourth descriptor
   word**: the "commit" is not a word at all but the *producer index* (§3).

2. **The observed head word `0xXX016000` reads as `(len << 16) \| flags` with
   `flags = 0x6000` and `len = 0xXX01`** — bits 15:0 are *exactly* the two vendor "valid" bits,
   with the low-13 flag sub-field zero. `0x6000` is a **flag** value, not an address and not a
   length.
   The `0x01` constant in all 17 slots is the **high byte of the length halfword**; the varying
   byte (`0x07,0x1d,0x29,0x33,0x4b,0x53,0x79,0xbf,0xef`) is the **low byte of that same length
   halfword** — a byte count, **not a counter**, and the value is *content-derived*, not monotonic
   (the nine identical STP records all carry `0x79016000`). §5 covers the two candidate field
   parses and why the "counter" label is not supported by any code.

3. **How the device-side writer updates them** (firmware, §2.3): build `word1` in a stack node,
   commit `word0` then `word1` into the ring at the low-10-bit producer index, advance the index
   (bit 10 toggles on wrap), notify the host by OR-ing the pending shadow into `out[1]` and pulsing
   the D2H doorbell, then increment the per-ring counter at `ctx+0x340`.

4. **What the observed window is *not*:** not the ETE SR/DR node ring (its `word0` would be a
   buffer address; every observed head has `word0 = 0` and the node stride is 8, not 0x200), and not
   the `wifi.ko` host RX descriptor (its payload-length field at `+0x1e` would read 43876 for the
   first slot). §6/§7.

---

## 2. The vendor's node words, instruction by instruction

### 2.1 Host H2D (SR) fill — `shuangta_ete_sr_dscr_fill` @ `0x17858` (plat.ko, ARM) [proven]

```
0x017858  4c40a0e1  push {r4, r5, lr}
0x017884  1318dfe7  bfi  r1, r3, #0x10, #0x10   ; word1 bits31:16 = len (r3 = uxth(3rd arg), 0x17868)
0x017888  04208de5  str  r1, [sp, #4]          ; node.word1 (stack node)
0x0178a0  012982e3  orr  r2, r2, #0x4000       ; flag bit14
0x0178ac  022a82e3  orr  r2, r2, #0x2000       ; flag bit13
0x0178b8  1120cce7  bfi  r2, r1, #0, #0xd      ; flag bits0:12 = 0xd2b (r1 = movw #0xd2b, 0x1789c)
0x0178c0  0c2094e5  ldr  r2, [r4, #0xc]        ; packed producer index
0x0178c8  5220e9e7  ubfx r2, r2, #0, #0xa      ; index = low 10 bits
0x0178cc  821183e7  str  r1, [r3, r2, lsl #3]  ; node[idx].word0 = buffer devva
0x0178e0  042083e5  str  r2, [r3, #4]          ; node[idx].word1 = (len<<16)|flags
0x0178f4  0310a0e3  mov  r1, #3                ; announce id 3
```

So `word1 = (len << 16) | 0x4000 | 0x2000 | 0xd2b = (len << 16) | 0x6d2b`. The store *order* is
`word0` (offset 0) then `word1` (offset 4), i.e. stride 8.

### 2.2 Host D2H (DR) fill and the field accessors — plat.ko [proven]

| function | offset | instruction | semantic |
| --- | --- | --- | --- |
| `shuangta_ete_dr_dscr_fill` | `0x1765c` | `ldr r3,[r0,#0x1c]` / `ubfx r3,r3,#0,#0xa` / `str r1,[r2,r3,lsl #3]` (`0x17664`/`0x17668`) | writes **word0 only** (the buffer address) at the low-10-bit index of the ring at `[ctx+0x10]` |
| `shuangta_ete_dr_get_dscr_addr` | `0x176cc` | `ldr r0,[r2,r3,lsl #3]` | reads **word0** |
| `shuangta_ete_sr_get_dscr_len` | `0x17690` | `lsr r0,r0,#0x10` on `[node+4]` | **length = word1 >> 16** |
| `shuangta_ete_sr_get_dscr_flag` | `0x176a4` | `ubfx r0,r0,#0,#0xd` on `[node+4]` | **flag = word1 & 0x1fff** (13 bits) |
| `shuangta_ete_dr_set_sr_dscr_flag` | `0x176f4`/`0x176f8` | `bfi r3,r2,#0,#0xd` / `str r3,[r1,#4]` | flags are patched in place in **word1 bits0:12** |

The mask constants are the authority for the split: **length = word1[31:16], flags = word1[15:0]
with the flag *value* living in bits 0:12 and bits 13/14 used as the valid bits.**

### 2.3 Device-side writer (firmware, Thumb, file offsets) [proven]

**(a) Build + commit + notify + counter — `0x861dc` (the device's D2H send):**

```
0x0861dc  30211700  movs r1, #0x30
0x0861e0  ...       push {r0, r1, r2, r4, r5, lr}
0x0861e6            str  r2, [sp]              ; node.word0 = 3rd arg = BUFFER ADDRESS
0x0861f0  63f31f42  bfi  r2, r3, #0x10, #0x10 ; node.word1 bits31:16 = len (r3 = uxth(4th arg))
0x086202  43f48043  orr  r3, r3, #0x4000      ; flag bit14
0x08620c  43f40053  orr  r3, r3, #0x2000      ; flag bit13
0x086216  62f30c03  bfi  r3, r2, #0, #0xd     ; flag bits0:12 = 0xd2b (r2 = movw #0xd2b, 0x861fe)
0x08621c  fbf785fb  bl   #0x8192a             ; commit the node into the ring
0x086224  fff7a4ff  bl   #0x86170             ; d2h_notify(obj, 6)   (r1 = #6 set at 0x86220)
0x08622e  c4f84033  str.w r3, [r4, #0x340]    ; ++ring counter at ctx+0x340 (r3 = [r4+0x340]+1, 0x8622c)
```

The alternate arm (`0x86236`, taken when the 5th stack argument is 0) clears the two valid bits
instead of setting them and keeps the same low-13 flag:

```
0x08623a  61f38e33  bfi r3, r1, #0xe, #1      ; bit14 = r1 (= 0 here)
0x086244  61f34d33  bfi r3, r1, #0xd, #1      ; bit13 = r1 (= 0 here)
```

So the firmware's word is `(len<<16) | 0x6000 | 0xd2b` when the descriptor is "present", and
`(len<<16) | 0xd2b` when it is not — bits 13/14 are the vendor's own **presence/valid** bits.

**(b) The commit — `0x8192a` (device twin of the host's `0x178c0..0x178e0`):**

```
0x08192a  10b4c268  push {r4}
0x08192c  c2680368  ldr  r2, [r0, #0xc]        ; packed producer index
0x081936  43f83240  str.w r4, [r3, r2, lsl #3]; ring[idx].word0 = [node] (buffer address), idx = low10
0x081948  5a6050f8  str  r2, [r3, #4]          ; ring[idx].word1 = [node+4] = (len<<16)|flags
0x081952  d8e70068  b    #0x81906             ; advance the producer index
```

**(c) The index advance (wrap) — `0x81906` (host twin `pcie_ete_ring_ptr_plus` @ `0x13ef8`):**

```
0x08190a  62f30903  bfi  r3, r2, #0, #0xa      ; idx = (idx+1) & 0x3ff
0x08191a  c3f38022  ubfx r2, r3, #0xa, #1      ; (inside the itttt eq at 0x81914) read the wrap bit
   plat.ko twin:
0x013ef8  003090e5  ldr  r3, [r0]
0x013f00  1230c9e7  bfi  r3, r2, #0, #0xa
0x013f04  5320e9e7  ubfx r2, r3, #0, #0xa
0x013f10  5325e007  ubfxeq r2, r3, #0xa, #1
0x013f1c  003080e5  str  r3, [r0]             ; commit the new producer word
```

The producer word is a **packed index in bits 0:9 plus a phase/toggle bit 10** — not a byte offset
and not an address. A host that commits a node must write `((idx+1) & 0x3ff)` and toggle bit 10
when the index wraps, exactly as both twins do.

**(d) The notify — `0x86170` `d2h_notify(obj, id)`:**

```
0x08618e  21fa07f4  lsr.w r4, r1, r7          ; bit = pending >> id (id = r7 = r1 argument)
0x0861a2  c5f8b830  str.w r3, [r5, #0xb8]     ; pending shadow at ctx+0xb8
0x0861b0  0b60d5f8  str   r3, [r1]            ; write it to out[1] (CA 0x40039014, ctx+0x9c)
0x0861c0  23601446  str   r3, [r4]            ; pulse the D2H doorbell (CA 0x40101434, ctx+0xa4)
```

`id = 6` is what the fill path uses (`0x86220`), matching the live `out[1] = 0x40` (bit 6) that
every takeover has logged — so **this firmware routine is the writer of the D2H "bit 6" event**.

### 2.4 The device-side reader of the same words (for completeness) [proven]

```
plat.ko pcie_ete_rcv_buff_check @ 0x14d74:
0x014dd4  5aba05e3  movw fp, #0x5a5a         ; the SR/ETE transport tag
0x014de8  ba30d5e1  ldrh r3, [r5, #0xa]      ; buffer[0xa] (the HCC header's u16 tag)
0x014dec  0b0053e1  cmp  r3, fp
0x014df4  b430d5e1  ldrh r3, [r5, #4]        ; buffer[4] = the HCC u16 total length
plat.ko pcie_ete_tx_queue_handle @ 0x155d4:
0x015798  b4b0d8e1  ldrh fp, [r8, #4]        ; same u16 length, used as the DMA size
```

So the *buffer* pointed at by `word0` carries an HCC message with a u16 total length at buffer `+4`
and the `0x5a5a` tag at `+0xa`, and both are read by the host — a second, independent confirmation
that lengths on this path are 16-bit fields of the described buffer (and that the *node* has no
length-counter of its own).

---

## 3. Ring geometry the fills assume (for the port)

* node stride **8 bytes**; node array base published at ETE register `+0x10` (SR) / `+0x30` (DR) as a
  **device VA** computed from the host CA by `pcie_hostca_to_devva` (`0x14884` → `str r0,[r6,#0x30]`).
* depth is stored in the low 10 bits of the register at `+0x14`/`+0x34` (`bfi r1,r3,#0,#0xa`, `0x148a4`),
  i.e. **up to 1024 nodes**, and the DR ring ctx is built with `min(2, cfg[4])` etc. (`pcie_ete_dr_init`
  @ `0x14908`, ctx fields `+0x1c/+0x20/+0x24/+0x3c`).
* The window-resident content uses a **0x200** stride, so it is not this ring: the vendor's node
  stride and the observed slot stride are different geometries.

---

## 4. The observed head word, field by field

### 4.1 Slots

The head of a record-starting slot is 4 words (`+0x00..+0x0F`); the payload (an L2 frame) begins at
`+0x10` (verified: slot `0x3f1000`'s payload starts `01 00 5e 7f ff fa | d4 0d ab 64 1c 73 | 08 00
45 00`, §4.3). 17 of the 24 slots have `+0x00 = 0` and `(word&0xffffff) == 0x016000` at `+0x04`:

| slot base | head `+0x04` | +0x08 | +0x0c | payload dst MAC |
| --- | --- | --- | --- | --- |
| 0x3f1000 | `0x07016000` | `0x00211039` | `0x00100000` | `01:00:5E:7F:FF:FA` (SSDP) |
| 0x3f1200 | `0x4B016000` | `0x00207039` | `0x00100000` | `01:00:5E:7F:FF:FA` |
| 0x3f1800 | `0xEF016000` | `0x001C9039` | `0x00100000` | `01:00:5E:7F:FF:FA` |
| 0x3f1a00 | `0xBF016000` | `0x00209039` | `0x00100000` | `01:00:5E:7F:FF:FA` |
| 0x3f1c00, 0x3f2000, 0x3f2400, 0x3f2800, 0x3f2c00, 0x3f3000, 0x3f3400, 0x3f3800, 0x3f3c00 (×9, identical head) | `0x79016000` | `0x00052039` | `0x00100000` | `01:80:C2:00:00:13` (802.1 link-local) |
| 0x3f2200 | `0x53016000` | `0x00215039` | `0x00100000` | `01:00:5E:7F:FF:FA` |
| 0x3f2a00 | `0x33016000` | `0x00219039` | `0x00100000` | `01:00:5E:7F:FF:FA` |
| 0x3f3200, 0x3f3a00 | `0xBF016000` | `0x00209039` | `0x00100000` | `01:00:5E:7F:FF:FA` |

`+0x0c = 0x00100000` in **all 17** heads; `+0x08` is `0x00YYZ039` (low byte `0x39` always,
middle byte `0x10/0x20/0x50/0x70/0x90`, high half `0x0005/0x001c/0x0020/0x0021`).

### 4.2 Field reading of the head word

Split the head word at 16 bits (the vendor's own split):

| bits | value in the 17 slots | meaning |
| --- | --- | --- |
| **15:0** | `0x6000` in *all 17* | the two "descriptor valid" bits (bits 13,14) — **exactly** the pair both vendor fills OR in (`0x178a0`/`0x178ac`, `0x86202`/`0x8620c`); flag sub-field bits 0:12 = 0 |
| **23:16** | `0x01` in *all 17* | the **high byte of the 16-bit value in the length position** |
| **31:24** | `0x07,0x1d,0x29,0x33,0x4b,0x53,0x79,0xbf,0xef` | the **low byte of that same 16-bit value** |

Two candidate parses of that 16-bit value exist, and the code plus the slot geometry select between
them:

* **Parse A (favoured): byte-swapped halfword, `0x01XX` = 263..495 bytes.** Reading bits 31:16 in
  big-endian order gives `0x0107, 0x014b, 0x01ef, 0x01bf, 0x0179, 0x0153, 0x0133, 0x0190, …`, i.e.
  **0x0107..0x01ef = 263..495**, which is exactly the slot's payload capacity (a 0x200 slot with a
  0x10-byte head leaves 496 bytes; the largest value seen is `0x1EF` = 495). This parse also gives
  the low half its only vendor meaning (`0x6000` = the valid bits, `0x0060` matches no constant in
  either module or the firmware).
* **Parse B: native halfword, `0xXX01` = 1793..61185 bytes.** This is what a plain \`ldr\` of the
  word followed by the vendor's own `lsr #0x10` would yield, but a 1793+-byte length cannot fit any
  0x200 slot, so it cannot be a byte length of this record.

A mixed layout — one halfword native, one halfword network order — is exactly what
`word = (htons(pkt_len) << 16) | (0x6000 | flags)` produces, so Parse A is the reading I favour.

**Why not a counter.** The value is *content-derived*: the nine 802.1 records have byte-identical
head words (`0x79016000`), identical `+0x08`/`+0x0c` words and the same payload prefix, while the
SSDP records differ. A monotone counter cannot repeat one value nine times. And no code in either
module or the firmware stores a per-node counter: the only counter on this path is the **per-ring**
counter at `ctx+0x340` (`0x8622e`). The phase-41 prose ("the low byte counting") is therefore not
supported by the artifacts; the byte that moves is the low byte of the record's length halfword.

### 4.3 Window byte order [observed, verified]

The window's payload is not in forward byte order. For every 8-byte group, the logical bytes are
**`BE(word@+4)` followed by `BE(word@+0)`**, groups in ascending address order — i.e. a 64-bit
endianness swap. Verified on slot `0x3f1000`:

```
logical payload[0:16] = 01 00 5e 7f ff fa d4 0d ab 64 1c 73 08 00 45 00
                        dst MAC          src MAC          ethertype IPv4
ascii[0:120] = ..^......d.s..E....=@.../.....g.l....NOTIFY * HTTP/1.1..HOST: 239.255.255.250:1900..
               CACHE-CONTROL: max-age=60..LOCA
```

(`block-structure.md` derives the same rule from the MAC prefixes; this section records it because it
decides how a multi-byte *field* in this window must be read — and it is why bits 31:16 of the head
word must be byte-swapped to yield a slot-sized length, Parse A above.)

---

## 5. The words after the head word

* **In the vendor's format there are none.** The node is 2 words; the device's own commit
  (`0x8192c`–`0x81948`) touches only offsets `0` and `4`, and the host's accessors only read
  `[node]` and `[node+4]` (`0x176cc`, `0x1768c`, `0x176a4`). So "the words following word0" is
  **exactly one** word: `(len<<16)|flags`.
* Inside the observed head, the two words after the marker are `+0x08 = 0x00YYZ039` and
  `+0x0c = 0x00100000` (constant in all 17 heads). **[unknown]** Neither has a writer or a reader in
  `plat.ko`, `wifi.ko` or the firmware. Their shape half-resembles the `wifi.ko` host RX
  descriptor fields (`+0x08` u16 read with a 12-bit sub-field: `ldrh r2,[r4,#8]` @ `0x384b8` then
  `ubfx r2,r2,#0,#0xc` @ `0x384c0`), but the observed low byte `0x39` and the `+0x09` values `0x10/0x20/0x50/0x70/0x90` do not
  satisfy that descriptor's rules, so the correspondence is unproven.
* The `+0x0c = 0x00100000` constant and the `+0x08` companion are stable across all 17 heads and all
  three windows, so they are *header* fields of whatever structure this is, not payload.

---

## 6. Negative results (what the window slots are *not*)

1. **Not an ETE SR/DR node ring.** A posted node's `word0` is a *buffer address* — the host writes it
   with `str r1,[r3,r2,lsl#3]` (`0x178cc`, `0x17668`) and the device's fill passes the buffer
   address as the node's first word (`str r2,[sp]` @ `0x861e6`). Every observed head has
   `word0 = 0` at `+0x00`, so it is not a {buffer address, len|flags} pair. The ETE node stride is
   **8** (§3), not 0x200, and the ETE ring base is published as a device VA through a register
   (`0x14884`/`0x14888`, `0x14aac`/`0x14ab0`).
2. **Not the `wifi.ko` host RX descriptor queue.** That queue does use **0x200-byte entries**
   (`shuangta_rx_host_init_dscr_queue` @ `0x37898`: `moveq r2,#0x200` @ `0x379e0`, `moveq r3,#0x800`
   @ `0x379d4`), and its per-entry descriptor is read at `0x3844c` with the payload length as a u16
   at `[dscr+0x1e]` (`ldrh r2,[r4,#0x1e]` @ `0x38640`). For the first observed slot that field would
   be `0xAB64` = 43,876 bytes — impossible inside a 0x200 slot. The `wifi.ko` descriptor also has a
   type nibble `== 3` at `[dscr+0x23]`; the observed `+0x23` byte is `0x00` for the SSDP slots.
3. **Consequence for the record.** `docs/phase41/d2h-window-resident-ring.md` reads the observation
   as "*the vendor's DR ring is carved from the message window*" and "*word0 = window buffer
   address, word1 = length|flags*". The code says the DR ring base is a host-coherent address
   programmed through a register and that its nodes carry buffer addresses: the window-resident
   ring claim is **not supported** by either module, and the observed `word0 = 0` refutes the
   "word0 = window buffer address" reading. What *is* supported is the field *format* of the marker
   word: a length|flags word carrying the vendor's `0x6000` valid bits.

---

## 7. What this means for the port (`opensource/lab/wifidrv1`)

* Post DR nodes in the vendor's exact shape: 8-byte nodes, `word0` = device VA of a
  `dma_alloc_coherent` buffer (through the same `hostca_to_devva` map the vendor uses), `word1 =
  (len << 16) | 0x6000 | 0xd2b`, and commit the producer as `((idx+1) & 0x3ff)` with bit 10 toggled
  on wrap. Those are the only fields the vendor's own writer, committer and accessors touch, so they
  are the only fields the device's D2H engine has any reason to interpret.
* Do **not** expect the device to deposit into a system-RAM DR ring and then look for the payload in
  the `0x3f1xxx` window: the window slots are not a node ring, and their marker word's only
  vendor-recognisable field is the `0x6000` valid-flag pattern and a 16-bit length.
* The experiment this report leaves: capture one window slot together with the length of the frame
  that produced it (or dump a slot before/after a known-size transfer). If bits 31:16 track the byte
  count (Parse A), the marker word is confirmed as `(BE length << 16) | 0x6000`; if the whole
  16-bit value tracks a sequence instead, it is a per-record id and not a length. One read decides it
  on the standing snapshot method.

## 8. Summary table

| field | value(s) observed | meaning | status |
| --- | --- | --- | --- |
| head `+0x00` | `0x00000000` (all 17) | none / not a buffer address | [observed]; rules out the ETE node ring |
| head `+0x04` bits 15:0 | `0x6000` (all 17) | descriptor valid/present flag bits 13,14 — the vendor's own pair | [proven by match: `0x178a0/0x178ac`, `0x86202/0x8620c`] |
| head `+0x04` bits 31:16 | `0xXX01` (X varies) | 16-bit value in the vendor's length position: `0x01XX` = 263..495 B (Parse A, favoured), or `0xXX01` native (Parse B, no slot can hold it) | [inferred from the slot capacity] |
| head `+0x04` varying byte | `0x07,0x1d,0x29,0x33,0x4b,0x53,0x79,0xbf,0xef` | low byte of that length — **not a counter** | [inferred; content-derived: 9 identical 802.1 records share `0x79016000`] |
| head `+0x08` | `0x00YYZ039` | unknown (§5) | [unknown] |
| head `+0x0c` | `0x00100000` (all 17) | unknown; stable header field | [unknown] |
| the vendor's node | `{word0 = buffer devva, word1 = (len<<16)|flags}` | 2 words, 8-byte stride, index low-10 + wrap bit 10 | [proven, §2/§3] |

## 9. Verification

Every offset quoted above was re-disassembled from the given binaries (capstone 5.0.7,
`CS_ARCH_ARM/CS_MODE_ARM` for the modules with file offset = `0x38` + `.text` offset;
`CS_MODE_THUMB` for the firmware with file offset = address). All quoted sites decode exactly as
written; the only two notes are cosmetic: the firmware routine's entry is `0x861dc`
(`movs r1,#0x30`; the `push` is at `0x861e0`), and `0x8191a`'s `ubfx` carries the `EQ` condition
from the `itttt eq` at `0x81914` (capstone prints it without the suffix in a linear sweep).

```
PY=pyenv/Scripts/python.exe
KO=opensource/build/register-dumps/teardown/hi5622v100_plat.ko
WK=opensource/build/tmp/hi5622v100_wifi.ko
FW=build/tmp/FIRMWARE.bin
$PY opensource/lab/ko_disasm.py $KO shuangta_ete_sr_dscr_fill shuangta_ete_dr_dscr_fill \
    shuangta_ete_sr_get_dscr_len shuangta_ete_sr_get_dscr_flag shuangta_ete_dr_set_sr_dscr_flag \
    pcie_ete_ring_ptr_plus pcie_ete_dr_reg_init pcie_ete_rcv_buff_check pcie_ete_tx_queue_handle
$PY opensource/lab/ko_disasm.py $WK shuangta_rx_host_init_dscr_queue shuangta_host_rx_get_msdu_info_dscr
$PY - <<'EOF'                      # firmware windows (file offsets, Thumb)
from capstone import *
d=open("build/tmp/FIRMWARE.bin","rb").read(); md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.skipdata=True
for a,b in [(0x861dc,0x86260),(0x81906,0x81956),(0x86170,0x861d0),(0x818b0,0x818f0),(0x86f50,0x86f6a)]:
    print("== 0x%x =="%a)
    for i in md.disasm(d[a:b],a): print("  %06x rt%06x %-8s %s"%(i.address,i.address+0x40000,i.mnemonic,i.op_str))
EOF
```

Snapshot evidence (read-only): the payload byte order and the head/field split, reproduced with

```
$PY - <<'EOF'
import re
d={}
for ln in open("build/tmp/msg-normal-op-20261004.txt"):
    m=re.match(r'([0-9a-fA-F]+) = 0x([0-9a-fA-F]+)',ln.strip())
    if m: d[int(m.group(1),16)]=int(m.group(2),16)
base=0x3f1000
out=b""
for o in range(base+0x10, base+0x200, 8):          # per 8B group: BE(word@+4)+BE(word@+0)
    out += d[o+4].to_bytes(4,'big') + d[o].to_bytes(4,'big')
print(out[:16].hex())                              # 01005e7ffffad40dab641c7308004500
for pat in (b"NOTIFY",b"HOST: 239.255.255.250:1900",b"CACHE-CONTROL",b"USN: uuid"):
    print(pat, out.find(pat))
w=[d[base+4*i] for i in range(4)]                  # head: 0x0, 0xXX016000, 0x00YYZ039, 0x00100000
print([hex(x) for x in w], "flags=", hex(w[1]&0xffff), "len(BE)=", hex((((w[1]>>16)&0xff)<<8) | (w[1]>>24)))
EOF
=> 01005e7ffffad40dab641c7308004500
   b'NOTIFY' 42   b'HOST: 239.255.255.250:1900' 61   b'CACHE-CONTROL' 89   b'USN: uuid' 270
   ['0x0', '0x7016000', '0x211039', '0x100000'] flags= 0x6000 len(BE)= 0x107
```

Hazard note: this phase performed no device access and wrote no register; it only read the given
artifacts and wrote this file.
