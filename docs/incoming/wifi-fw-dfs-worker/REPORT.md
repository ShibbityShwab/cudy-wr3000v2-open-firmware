# REPORT - the 0x01F7 config-event acceptance: field B does not say "4", it says "band 0"

**Lane:** wifi-fw-dfs-worker (shared task **task-191**), 2026-11-27.
**HOST-ONLY**: static decode of the pinned local artifacts; no device contact,
no write to any artifact, no fire. **The Lead fires.** Companions:
[README.md](README.md), [TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_dfs_worker_re.py](tools/fw_dfs_worker_re.py),
[patches/0001-fieldb-context.patch](patches/0001-fieldb-context.patch).

Grades: **[V]** re-derived here by the committed tool from the pinned artifacts;
**[V*]** bench-measured by a committed lane or the Lead; **[I]** inference;
**[U]** needs a device read.

## 0. TL;DR

The F1 fire's 29-byte frame was **accepted**: the g0-id1 dispatcher has no
length/flag/checksum gate, found table A row 93 (key 0x01F7 -> worker
0x0B5B9D) and called the worker with (ctx, len=1, body). The worker passed all
three memory preconditions - but on the **wrong band object**, because
`hdr[3]` ("field B") is the **per-module context index**, and field B=0x04 is
the context the boot C2 ctx_create created and bound to **band 0 (2.4 GHz)**
from body[0x12]=0. The worker therefore wrote `mode=3` into
**band0+0xF04 (0x15D90C)** and the ch36..64 RF image into **band 0's** R block;
the 5 GHz band object (0x15E570 = context 0, bound by our AP-start vap_add with
body[0x12]=1) was never touched. The host's "published=0x00175a18 attach=1"
readback read context 0 and was true - but the frame addressed context 4.
The fix is one header byte per frame: field B must equal the AP-start module
index (0 in the fired recipe), not the TXQ capture's 4.

## 1. Method and inputs

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`, CPU = file + 0x40000 |
| host module | `.../lib/hisilicon/ko/hi5622v100_wifi.ko` (3,581,748 B; ELF `.text` file offset 0x38) |
| disassembler | lab objdump for context; **all tool assertions are raw instruction encodings** (`--selftest`), so the decode is re-derivable without objdump |
| committed anchors | ../wifi-fw-rf-descriptor (chain, table A row 93, worker/mode addresses), ../wifi-fw-desc-table (g0-id1 wire, table A/B), ../wifi-ctx-create (field-B context table, CA 0xFE578), ../wifi-vap-start (key-0x64 worker band binding, module-4 band-0 bench readback), ../wifi-h2d-catalog (handler list), ../wifi-dfs-mode-send (the fired F1 frame) |

## 2. The g0-id1 dispatch for key 0x01F7 [V]

Handler CA **0x43D58** (message = group 0 id 1 config event; called through the
HCC receive table; `hdr[6..7] = id = 1`):

```
43D58  push {...} ; validates the message pointer
43D6C  bl CA 0x409EA          ; header fetch/validate: message-pool membership
                              ; + class field (hdr[5]>>1 & 7) <= 3
43D72  ldrb r1,[r4,#3]        ; r1 = hdr[3] = FIELD B
43D74  movs r0,#0
43D76  bl CA 0xFE578          ; r0 = per-module context = lookup(0, fieldB)
43D7A  cbnz r0 -> 43D8E       ; (0 -> log 0xA1E, return 0x3E8)
43D8E  movs r3,#0 ; bound 0x6C8
43D90  ldrh r2,[r4,#20]       ; key = u16 hdr+20 (= payload+8)
43D92  table A = CA 0x10C23C
43D94..43DA0  compare u16 entry[+0] with key, stride 8, 217 entries
43DA2  cmp r3,#0x6C8 ; bne loop
43DA8..43DBE  second table B = CA 0x103BF8, bound 0x2C0 (88 entries)
43DC0  not found -> log 0xA2C, return 1000
43DCE  ldr r3,[entry+4]        ; worker (bit0 = Thumb)
43DD0  cbz r3 -> log 0xA36, return 1000
43DEC  add.w r2,r4,#24         ; body = hdr+24
43DF0  ldrb r1,[r4,#22]        ; len = ONE BYTE hdr+22
43DF2  blx r3                 ; worker(r0=ctx, r1=len, r2=body)
```

Table A entries are exactly `{u16 key; u16 pad; u32 worker|thumb}` - **no
flags, no expected length**. The only gate before the worker is the message
header validation (pool membership + header class) and the context lookup
(field B <= 0x12). **A well-formed 29-B frame cannot be rejected for its key,
length, body byte, tail or total.**

Row decode (raw, asserted): row 93 = `f7 01 00 00 9d 5b 0b 00` -> key
0x01F7, pad 0, worker 0x0B5B9D; row 92 = key 0x00F7, worker 0x045A31.
The key/len/body offsets are **identical for every key on this path** (the
"desc-table path" is the same g0-id1 handler; keys 0x00F7 and 0x01F7 differ
only in what their worker does with the body).

## 3. The field-B context model [V]

`ctx = CA 0xFE578(0, fieldB)`:

```
FE578  push {r4,r5,r6,lr}; r5=r0; r4=r1
FE57E  bl 0xFE558          ; r6 = singleton getter(0) -> literal 0x001ADB80
FE584  mov r0,r5 ; bl 0x943AC  ; r0 = G = literal 0x00170E28
FE59C  cmp r4,#0x12        ; field B bound 0..18
FE5A8  add.w r4,r4,#0x18C  ; (0x18C + fieldB)*4 = 0x630 + 4*fieldB
FE5AC  ldrh.w r3,[r0,#0x6DC]   ; u16 common offset
FE5B0  ldr.w r0,[r0,r4,lsl #2] ; context pointer
FE5B4  add r0,r3           ; ctx = pointer + offset
```

Boot init CA **0x49204..0x49264** memsets G (0x700 B) and fills all **19**
slots with `0x119758 + k*0xB40` (region 0x119758..0x126D18), zeroing every
0xB40-byte body; it stores the common offset once at CA 0x4923A
(`strh r6,[G+0x6DC]`). So every field B 0..0x12 aliases a *valid but initially
zeroed* context body. A context only becomes meaningful when a message
targeting it creates/binds it.

The **band binding** is made by the key-**0x0064** (VAP-add / ctx-create)
worker CA **0x449E4** (table A row 1). After the create helper it does:

```
44B6C  ldrb r1,[r7,#0x12]      ; body[0x12] = band selector
44B6E  ldrb r0,[r4,#2]         ; (module argument)
44B70  bl CA 0xA4FC8           ; band object
44B76  add.w r1,r0,#0x1A00
44B7A  ldr r3,[r1]             ; [band+0x1A00] must be non-zero (published)
44BBC  str.w r3,[r4,#0x4B0]    ; ctx+0x4B0 = band published per-link
44BC2  str.w r0,[r4,#0x4B8]    ; ctx+0x4B8 = band object
44BC6  str.w r3,[r4,#0x4B4]
```

with CA 0xA4FC8(a, band) = literal **0x0015CA08 + 0x1B68*band** (band 0/1).
Note the worker resolves the *created module* from body[4]; its own field B
only selected the dispatcher context. [V]

## 4. The 0x01F7 worker, line by line [V]

| CA | instruction | meaning |
| ---: | --- | --- |
| 0xB5B9E | `ldr.w r4,[r0,#0x4B8]` | r4 = band object of the *selected context* |
| 0xB5BA6 | `add.w r6,r4,#0x1A00` | published per-link address |
| 0xB5BAA | `ldr r0,[r6]` ; `cbnz` | published per-link, required non-zero (else log **1442**, ret 0x64) |
| 0xB5BC8 | `ldrb.w r1,[r4,#0xD70]` ; `cmp #1` | attach flag, required == 1 (else log **1448**, ret 0) |
| 0xB5BE2 | `ldrb r5,[r2,#0]` | **mode = body[0]** |
| 0xB5BE4 | `cmp r5,#1` / `bne` | mode 1 -> its own RF image block |
| 0xB5CDA | mode == 2 -> `bl 0x89A78` | mode-2 image helper |
| 0xB5D7C | mode == 5 -> its own image block (loads R at 0xB5D80) | |
| 0xB5E78 | mode == 3 -> `bl CA 0x89924` | **ch36..64 RF image writer (called with O)** |
| 0xB5E82 | mode == 4 -> common | |
| 0xB5E88 | out of 1..5 -> log 1485 | (mode byte is stored before this check) |
| 0xB5D1C | `strb.w r5,[r4,#0xF04]` | **the ONLY mode-byte writer in the image** |
| 0xB5CE4..0xB5D68 | common | `memcpy_s(O+0x1C3, 4, &word, 4)` where word = {mode, saved-len[1..3]}; band fields +0xF00..+0x10F4 |
| 0xB5D72 | `tbb` table `9b 90 9b 97 90` | per-mode tail fragments (targets 0xB5E10/0xB5E06/0xB5E10/0xB5E0C/0xB5E06) |

CA 0x89924 (mode 3) starts `ldr.w r4,[r0,#0x2D8]` (R = `*(O+0x2D8)`) and
stores the ch36..64 image to R+0x3A8..+0x418. So a correctly-targeted mode-3
frame writes **band+0xF04 = 3 AND the whole RF image**.

## 5. Why the fired frame programmed band 0 (the root cause) [V]/[I]

F1 stack on the booted box:

| binding | created by | field B | body[0x12] | resulting `ctx+0x4B8` |
| --- | --- | ---: | ---: | --- |
| context 4 = 0x11C458 | boot C2 `h2d_ctx_create` (g0 key 0x0064, module/user 4) | 4 | **0** | **band 0 object 0x15CA08**, per-link 0x174BF0 |
| context 0 = 0x119758 | AP-start `vap_add` (g0 key 0x0064, `vap_start_module=0`, band 1) | **0** | **1** | **band 1 object 0x15E570**, per-link 0x175A18 |

* [V] The C2 ctx_create frame is built with `fb == user == 4` and the same
  key-0x64 worker binds `ctx+0x4B8` from `body[0x12]`; the synthesized body
  carries `data[0x12] = 0` -> **band 0**.
* [V*] The committed wifi-vap-start bench readback on a C2-created boot:
  **module-4 ctx+0x4B0 = 0x00174BF0, ctx+0x4B8 = 0x0015CA08** (band 0), i.e.
  exactly the model above.
* [V] The AP-start vap_add/provider sets `module = vap_start_module`,
  `field_b = 0` (default), `data[0x12] = band = 1` -> context 0 -> band 1.
  The driver's own F1 readback (`bandobject=0x0015e570 published=0x00175a18
  attach=1`) is a read of **context 0** and is consistent.
* [V] The 0x01F7/0x00F7 frame builders hard-code `hdr[3] = 0x04`
  (../wifi-dfs-mode-send/src/hi5622_dfs_mode.h `HI5622_DFS_MODE_FB 0x04u`;
  ../wifi-desc-table-send/src/hi5622_desc_table.c `b[3] = 0x04u`), inherited
  from the vendor first-H2D **txq_sync** capture - a *different* message sent
  by the data-plane module 4. The vendor's own country/DFS emitter
  `hmac_config_set_country_for_dfs` forwards its hmac ctx, whose byte 0 the
  shared builder writes to `hdr[3]` - so the field is the sender's context,
  not a constant.

Consequence: the 0x01F7 worker is invoked with context 4 -> band 0. Every
check passes (band 0 object is published and attached), so it writes
**band0+0xF04 = 3 (CPU 0x15D90C)**, the ch36..64 image into band 0's R block
and the mode word into band 0's published per-link. The 5 GHz band 1 object
(0x15E570) is untouched: mode at 0x15F474 stays 0, R (0x400B2000) untouched,
so CA 0xA2FA2 (mode==3) silently skips - exactly the F1 observation. There is
**no error log and no fault** because nothing failed.

**Fallback hypothesis H1 [I]:** if the C2 ctx_create did not bind module 4 on
the fired boot (e.g. `ctx4+0x4B8 == 0`), the worker exits at the first check
with log **1442** and return 0x64 - also silent to the host, and the same fix
applies. The probe pack's `u32(0x11C910)` and `u8(0x15D90C)` distinguish
H2 ("worker ran on band 0") from H1 ("worker bailed"); the fix is identical.

## 6. The body format of the fired frame [V/I]

* **body[0] is the mode value itself** (1..5), not a pointer or index: the
  worker loads it with a plain `ldrb` and branches on 1/2/3/4/5. Mode 3 is
  the value CA 0xA2FA2 requires, so a stock `set_country_for_dfs` for the
  5 GHz RF programme passes **3**. [V parse; I stock value]
* The vendor emitter `hmac_config_set_country_for_dfs` (wifi.ko
  `.text+0x5762C`) is a pure forwarder: `(ctx r0, len r1, data r2)` ->
  `hmac_config_send_event(ctx, r1=0x01F7, r2=len, r3=data)`; the payload is
  whatever the WAL caller passed, and `hmac_config_send_event` writes key at
  hdr+20, len at hdr+22, data at hdr+24 on a g0/id1 carrier. [V]
* The handler reads the length as **one byte** at hdr+22 (not u16) and the
  0x01F7 worker never validates it (it is saved on the stack and only its
  bytes 1..3 can leak into the 4-byte mode word copied to O+0x1C3; with our
  len=1 only the mode byte is nonzero). So len=1 and the 0xFF tail are fine. [V]
* The committed wifi-h2d-catalog phrase "payload +0xA (u8 sub-key)" is
  imprecise for this path: hdr+22 is the length low byte, keyed workers of
  other keys may use it differently. No 0x01F7 acceptance depends on it. [V]

## 7. Preconditions the worker actually checks (no extra state) [V]

In order: (1) dispatcher message validation (pool membership + header class);
(2) field B <= 0x12 and a non-NULL context slot; (3) `ctx+0x4B8 != 0`
(band object bound); (4) `band+0x1A00 != 0` (published per-link); (5)
`band+0xD70 == 1` (attach). There is **no** country/regdomain state, no
"0xF7 table present" test, no page/exception check, and no mode byte
precondition in the 0x01F7 worker. The attach flag is written only by the
boot at CA 0x48A7E as `min(u8(perlink+1), 2)`; the sibling rf-descriptor
REPORT's probe `P+0xD70 = 0x15F3E0` is an arithmetic slip (band1+0xD70 is
**0x15F2E0**; the driver's readback used `bandobj+0xD70` correctly and read 1).

## 8. Fix and fire recipe (summary; full rung in FIRE-RECIPE.md)

1. **P0 read-only, on the still-live F1 AP** (no fire): read the probe pack
   (TABLES T5). H2 is confirmed if `u32(0x11C910) == 0x0015CA08` and
   `u8(0x15D90C) == 3` (the 0x01F7 worker really ran, on band 0).
2. **F2a (one-byte fix, then fire)**: build the 0x01F7 frame with
   `hdr[3] = 0` (= `vap_start_module` of the AP ladder) and the 0x00F7 frame
   with `b[3] = 0`; reload with the same F1 parameters, start the ch40 AP,
   and read band 1: `u8(0x15F474) == 3`, `u32(0x175CF0)` unchanged R,
   `R+0x3A8 = 0x01800060`, `R+0x3AC = 0x03E6138A`, `R+0x410 bit0 = 1`,
   then the 5200 MHz beacon. The frame bytes are in TABLES T6.
3. Do **not** "fix" it by setting `vap_start_module=4`: that would move the
   whole AP-start ladder onto the C2-created context and disturb the
   bench-validated 2.4 GHz path; the two new frames are the ones that are
   out of family.
4. If F2a still leaves mode 0, the probe pack splits H2 vs a second effect
   (e.g. attach/band object changed after vap_add); do not blind-loop.

## 9. Limits / honest unknowns [U]

* The live values in the probe pack (H2 vs H1) are **[U]** until the Lead
  reads them; this lane contacts no device. [I-high] is H2 because of the
  committed wifi-vap-start bench readback of module-4 ctx+0x4B8 = band 0.
* The exact stock userspace value passed to `set_country_for_dfs` remains
  **[U]** (not in the artifacts); 3 is inferred from CA 0xA2FA2's requirement.
* The field-B value of the **vendor** 0x01F7 message is [I] equal to its hmac
  ctx's module index (the same context as the AP ladder it is configuring);
  on our clean host that is 0.
* The patch in patches/ is a source recipe only: this lane did not rebuild the
  module; the implementation lane must rebuild and update the lane-190
  golden-frame host tests (which currently assert hdr[3]=0x04).

## 10. Reproduction

```
cd opensource/docs/incoming/wifi-fw-dfs-worker
./tools/fw_dfs_worker_re.py --selftest   # 142/142 PASS (evidence/selftest.txt)
./tools/fw_dfs_worker_re.py --decode     # dispatcher + worker + context model
./tools/fw_dfs_worker_re.py --frame      # current and field-B-fixed 29-B frames
./tools/fw_dfs_worker_re.py --probes     # read-only probe pack
./tools/fw_dfs_worker_re.py --ctx        # 19 context slots + band bindings
```
Artifacts are read in place and never copied; no vendor byte is committed.

## 11. Sources

* pinned local FIRMWARE.bin (sha256 checked) and hi5622v100_wifi.ko (read in
  place; `.text` byte assertions);
* committed lanes: ../wifi-fw-rf-descriptor/ (worker/mode/table-A/wire),
  ../wifi-fw-desc-table/ (g0-id1 frame and table A/B),
  ../wifi-ctx-create/ (CA 0xFE578 field-B context model, CA 0xFE6BC),
  ../wifi-vap-start/ (key-0x64 worker band binding, module-4 bench readback),
  ../wifi-h2d-catalog/ (handler inventory), ../wifi-dfs-mode-send/ (the fired
  F1 frame and its probes), ../wifi-fw-rf-programme/ (CA 0xA2FA2 chain).
