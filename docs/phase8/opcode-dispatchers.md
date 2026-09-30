# Mapping the firmware message-opcode dispatchers and binding the live-captured opcodes (phase 8, 2026-09-30)

Scope: local files, read-only, no device access. Inputs: `build/tmp/FIRMWARE.bin`
(928,920 B = `0xE2C98`), `pyenv/Scripts/python.exe` with capstone 5.0.7
(`CS_ARCH_ARM`, `CS_MODE_THUMB`) and pyelftools. Prior art: `ulw/phase8/cali-handler-address.md`
(lead: three candidates `0xDA0FC`, `0xDB5EC`, `0xDC378`; tail name pool inert),
`ulw/phase8/firmware-cali-parser.md`, `ulw/phase7/firmware-callgraph.md`,
`ulw/phase5/message-decode.md`, `ulw/phase6/message-fields.md`.

**Bias note.** All addresses are **file offsets into FIRMWARE.bin** unless a
runtime address is named. The blob uses runtime = file + `0x40000` for code
(confirmed below), so a table word that is a Thumb code pointer `W` maps to file
`(W & ~1) − 0x40000`. The three candidates are in the second code block
`0xD8000–0xE2000`; the dispatcher found here is in the main block.

**Result in one line.** The three candidate functions are **not** the alg
message-opcode dispatcher: they switch on an internal per-band byte and none of
their comparison constants equals any live opcode. The real alg dispatcher is at
**file `0x3243C`**; it reads the live id word's low 16 bits (`ldrh [msg]`) and
dispatches through a **25-entry `{u32 cfg_id, u32 handler}` table at file
`0xC4CB8`** (runtime `0x10C4CB8`) that contains exactly the live `0x0dad–0x0dc5`
cfg_ids. That binds the `0x0d01/0x0d00` family by matching constants. `get_cca_th`
(cfg `0x012e`, len 14) is outside that table and its handler is **unresolved**.

---

## 1. The three dispatcher candidates

All three were re-disassembled from the raw windows; every excerpt below is
verbatim capstone output.

### 1.1 `0xDA0FC` — a genuine table dispatcher, but on a 1-byte id (not the alg cfg_id)

```
0xDA0FC  push       {r4, r5, r6, lr}
0xDA0FE  mov        r4, r1
0xDA100  sub        sp, #0x20
0xDA102  mov        r0, sp
0xDA104  movs       r2, #0x1e
0xDA106  movs       r1, #0
0xDA108  blx        #0xe20bc          ; memset(sp, 0, 0x1e)
0xDA10C  cbz        r4, #0xda11a
0xDA10E  ldrb       r6, [r4]           ; id  = msg[0]
0xDA110  ldrb       r5, [r4, #1]       ; len = msg[1]
0xDA112  cmp        r5, #0x1e          ; len <= 30 ?
0xDA114  it         ls
0xDA116  cmpls     r6, #0x31           ; and id <= 49 ?
0xDA118  bls        #0xda11e
0xDA11A  add        sp, #0x20
0xDA11C  pop        {r4, r5, r6, pc}
0xDA11E  adds       r2, r4, #2
0xDA120  mov        r3, r5
0xDA122  movs       r1, #0x1e
0xDA124  mov        r0, sp
0xDA126  bl         #0xd8bf0          ; memcpy_s(sp, 0x1e, msg+2, len)
0xDA12A  cbnz       r0, #0xda14a
0xDA12C  movw       r3, #0x18b0
0xDA130  movt       r3, #1             ; r3 = 0x000118b0
0xDA134  add.w      r6, r3, r6, lsl #2 ; r6 = 0x118b0 + 4*id
0xDA138  ldr.w      r3, [r6, #0x988]   ; handler = *(u32*)(0x12238 + 4*id)
0xDA13C  cmp        r3, #0
0xDA13E  beq        #0xda11a
0xDA140  mov        r1, r5             ; arg1 = len
0xDA142  mov        r0, sp             ; arg0 = copied payload
0xDA144  blx        r3
0xDA146  add        sp, #0x20
0xDA148  pop        {r4, r5, r6, pc}
```

* **Prologue:** `push {r4,r5,r6,lr}; mov r4,r1; sub sp,#0x20; …memset(local,0,0x1e)`.
* **Selection:** bounds check then **random-access table index** (no comparison chain):
  require `msg[1] ≤ 0x1e` and `msg[0] ≤ 0x31`, copy `msg[1]` bytes from `msg+2`
  into a 30-byte local, then load `handler = RAM[0x12238 + 4*msg[0]]`.
* **Constants:** id bound `0x31` (50 ids, `0..49`), length bound `0x1e` (30),
  table base runtime `0x118b0 + 0x988 = 0x12238` (RAM).
* **Handler addresses: NOT recoverable.** The table word is read from runtime
  `0x12238`, which is below the file's lowest runtime code address
  (`file 0 + 0x40000`). Under the alternative flat reading (`0x12238` = file
  offset) the bytes there are Thumb code (`0x40 f2 9d 11 …` = `movw r1,#0x119d`),
  not pointers. A whole-file search for a 50-entry code-pointer run finds none
  (max runs are 32/28/20/17/16). So the 50 handlers cannot be read statically.

### 1.2 `0xDB5EC` — a small switch on `struct[1]`, cases `0x00` and `0x34`

```
0xDB5EC  push       {r4, lr}
0xDB5EE  ldrb       r2, [r0, #1]       ; op = struct[1]
0xDB5F0  mov        r4, r0
0xDB5F2  cmp        r2, #0x34
0xDB5F4  ldrb       r1, [r0]           ; idx = struct[0]
0xDB5F6  beq        #0xdb628           ; case 0x34
0xDB5F8  cbz        r2, #0xdb63e       ; case 0x00
0xDB5FA  movs       r3, #0
0xDB5FC  movs       r2, #2
0xDB5FE  strb.w     r3, [r4, #0x346]   ; default: set state bytes
0xDB602  strb.w     r3, [r4, #0x348]
0xDB606  strb.w     r2, [r4, #0x345]
0xDB60A  movw       r3, #0x3aa0
0xDB60E  movs       r0, #1
0xDB610  movt       r3, #1             ; r3 = 0x00013aa0
0xDB614  add.w      r3, r3, r1, lsl #2
0xDB618  ldr        r2, [r3, #0x5c]    ; r2 = *(u32*)(0x13afc + 4*idx)
0xDB61A  strb.w     r0, [r4, #0x344]
0xDB61E  ldr        r3, [r2, #4]
0xDB620  orr        r3, r3, #8
0xDB624  str        r3, [r2, #4]
0xDB626  pop        {r4, pc}
0xDB628  ldr        r3, [r0, #0x1c]
0xDB62A  ldrb       r3, [r3, #0xb]
0xDB62C  cmp        r3, #1
0xDB62E  ittt       eq
0xDB630  moveq      r2, #0
0xDB632  strbeq.w   r3, [r0, #0x34a]
0xDB636  strbeq.w   r2, [r0, #0x347]
0xDB63A  bne        #0xdb5fa
0xDB63C  b          #0xdb60a
0xDB63E  movw       r0, #0x11a3
0xDB642  lsls       r3, r1, #0x18
0xDB644  and        r3, r3, #0xf000000
0xDB648  movt       r0, #0x2064         ; MMIO base 0x20640000
0xDB64C  mov.w      r1, #0x18c
0xDB650  strb.w     r2, [r4, #0x347]
0xDB654  orrs       r0, r3
0xDB656  bl         #0xdacec
0xDB65A  ldrb       r1, [r4]
0xDB65C  b          #0xdb60a
```

* **Prologue:** `push {r4,lr}; ldrb r2,[r0,#1]; mov r4,r0; cmp r2,#0x34; ldrb r1,[r0]`.
* **Selection:** **two direct comparisons** (`0x34`, then `0`) into three tails;
  no table for the opcode. The `idx = struct[0]` only indexes a *different*
  per-index RAM table at `0x13aa0 + 0x5c + 4*idx = 0x13afc + 4*idx`.
* **Constants:** `0x34`, `0x00`; RAM table offset `0x5c` off `0x13aa0`;
  MMIO `0x20640000`.
* **Not an opcode dispatcher for the live opcodes** (see §3).

### 1.3 `0xDC378` — a small switch on `struct[1]`, cases `0x32` and `0x33`

```
0xDC378  push.w     {r4, r5, r6, r7, r8, lr}
0xDC37C  ldrb       r5, [r0]           ; idx = struct[0]
0xDC37E  ldrb       r2, [r0, #1]       ; op  = struct[1]
0xDC380  movt       r3, #1
0xDC384  lsls       r5, r5, #2
0xDC386  add        r3, r5             ; r3 = 0x000107b8 + 4*idx
0xDC388  cmp        r2, #0x32
0xDC38A  mov        r4, r0
0xDC38C  ldr.w      r6, [r3, #0xa2c]   ; r6 = *(u32*)(0x111e4 + 4*idx)
0xDC390  beq        #0xdc3c0           ; case 0x32
0xDC392  movw       r3, #0x18b0
0xDC396  movt       r3, #1             ; r3 = 0x000118b0
0xDC39A  add        r5, r3
0xDC39C  movs       r3, #2
0xDC39E  ldr.w      r1, [r5, #0xe4]    ; r1 = *(u32*)(0x11994)
0xDC3A2  cmp        r2, #0x33
0xDC3A4  strb.w     r3, [r4, #0x214]
0xDC3A8  it         eq
0xDC3AA  moveq      r0, #0x36           ; case 0x33 -> struct[1] = 0x36
0xDC3AC  str        r3, [r1]
0xDC3AE  it         ne
0xDC3B0  movne      r0, #0x37           ; other     -> struct[1] = 0x37
0xDC3B2  movs       r2, #1
0xDC3B4  movs       r3, #1
0xDC3B6  strb       r0, [r4, #1]
0xDC3B8  strb       r2, [r6, #8]
0xDC3BA  strb       r3, [r4, #3]
0xDC3BC  pop.w      {r4, r5, r6, r7, r8, pc}
0xDC3C0  ldr        r7, [r0, #0x68]    ; case 0x32 path
0xDC3C2  add.w      r8, r0, #0x48
0xDC3C6  mov        r1, r8
0xDC3C8  mov        r0, r7
0xDC3CA  ldrb       r2, [r4, #3]
0xDC3CC  bl         #0xdbfb0           ; <- the only call on this path
...
```

* **Prologue:** `push.w {r4-r8,lr}; ldrb r5,[r0]; ldrb r2,[r0,#1]; movt r3,#1; …`.
* **Selection:** **two direct comparisons** (`0x32` → call `0xDBFB0`; `0x33` →
  set `struct[1]=0x36`, else `0x37`). `idx = struct[0]` indexes only per-band
  RAM tables (`0x107b8+0xa2c+4*idx = 0x111e4+4*idx`, and `0x118b0+0xe4`).
* **Constants:** `0x32`, `0x33`, `0x36`, `0x37`; RAM offsets `0xa2c` off
  `0x107b8`, `0xe4` off `0x118b0`.

### 1.4 Verdict on the three candidates

None is the alg cfg_id dispatcher. Each keys on a **single message/struct byte**
(`[r0]` index, `[r0+1]` opcode) whose value space is `{0x00,0x1e,0x31}` /
`{0x00,0x34}` / `{0x32,0x33}` — none of which appears in the live alg wire
values (`0x0101`, `0x010e`, `0x0d01`, `0x0d00`). Only `0xDA0FC` is a table
dispatcher, and its 50 handler words are in RAM and not in the image.

---

## 2. The recovered opcode space and handler addresses

### 2.1 The real alg dispatcher: file `0x3243C` + table file `0xC4CB8`

Reproduced from the raw window (the function start is `0x3243C`; `0x32456/0x32458`
are its shared epilogue/exit):

```
0x3243C  push       {r0, r1, r2, r4, r5, r6, r7, lr}
0x3243E  mov        r4, r1              ; r4 = msg
0x32440  mov        r5, r0              ; r5 = dev
0x32442  cbz        r0, #0x32446
0x32444  cbnz       r1, #0x3245a
0x32446  mov        r3, r4
0x32448  mov        r2, r5
0x3244A  movw       r1, #0x4fc
0x3244E  ldr        r0, [pc, #0x70]
0x32450  bl         #0x1df4            ; null-arg log
0x32454  movs       r0, #0x64
0x32456  add        sp, #0xc
0x32458  pop        {r4, r5, r6, r7, pc}
0x3245A  ldrh       r2, [r1]            ; cfg_id = *(u16*)msg   <-- live id word low half
0x3245C  movw       r3, #0xdad
0x32460  movw       r1, #0x501
0x32464  ldr        r0, [pc, #0x5c]
0x32466  bl         #0x1df4            ; trace log (cfg_id, 0x0dad)
0x3246A  ldrh       r2, [r4]
0x3246C  subw       r3, r2, #0xdad
0x32470  cmp        r3, #0x1c           ; cfg_id-0x0dad <= 0x1c  (0x0dad..0x0dc9)
0x32472  bls        #0x3248e
0x32474  movw       r3, #0xdca
0x32478  movw       r1, #0x50e
0x3247C  str        r3, [sp]
0x3247E  ldr        r0, [pc, #0x40]
0x32480  movw       r3, #0xdac
0x32484  bl         #0x1cde            ; "unsupported cfg_id" log
0x32488  movw       r0, #0x8b2d
0x3248C  b          #0x32456
0x3248E  ldr        r6, [pc, #0x38]     ; r6 = literal @0x324C8 = 0x00104CB8 (table base)
0x32490  movs       r3, #0
0x32492  mov        r1, r6
0x32494  ldr.w      r7, [r6, r3, lsl #3]  ; r7 = table[i].cfg_id
0x32498  uxtb       r0, r3
0x3249A  cmp        r2, r7
0x3249C  beq        #0x324a6
0x3249E  adds       r3, #1
0x324A0  cmp        r3, #0x18           ; i < 24
0x324A2  bne        #0x32494
0x324A4  mov        r0, r3              ; fallback index 24
0x324A6  add.w      r3, r1, r0, lsl #3
0x324AA  ldr        r3, [r3, #4]        ; handler = table[i].handler
0x324AC  cbz        r3, #0x324ba
0x324AE  mov        r1, r4              ; arg1 = msg
0x324B0  mov        r0, r5              ; arg0 = dev
0x324B2  add        sp, #0xc
0x324B4  pop.w      {r4, r5, r6, r7, lr}
0x324B8  bx         r3                  ; tail-call handler(dev, msg)
```

**Selection method:** linear search (not a comparison chain, not an indexed jump
table) over a **25-entry `{u32 cfg_id, u32 handler}` table**, guarded by the range
check `cfg_id − 0x0dad ≤ 0x1c`. The base comes from the PC-relative literal at
`0x324C8`, whose four bytes are `B8 4C 10 00` = `0x00104CB8 = 0xC4CB8 + 0x40000`.

### 2.2 The table at file `0xC4CB8` (runtime `0x10C4CB8`)

25 records of 8 bytes (`u32 cfg_id`, `u32 handler_runtime`; Thumb bit set).
Handler file offset = `(handler_runtime & ~1) − 0x40000`.

| idx | file | cfg_id | handler (runtime) | handler (file) |
|---:|---|---|---|---|
| 0 | `0xC4CB8` | `0x0dad` | `0x00072F6D` | `0x32F6C` |
| 1 | `0xC4CC0` | `0x0dae` | `0x00072BEF` | `0x32BEE` |
| 2 | `0xC4CC8` | `0x0daf` | `0x00072BE5` | `0x32BE4` |
| 3 | `0xC4CD0` | `0x0db0` | `0x00072BDB` | `0x32BDA` |
| 4 | `0xC4CD8` | `0x0db1` | `0x00072BD1` | `0x32BD0` |
| 5 | `0xC4CE0` | `0x0db2` | `0x00072B89` | `0x32B88` |
| 6 | `0xC4CE8` | `0x0db3` | `0x00072A39` | `0x32A38` |
| 7 | `0xC4CF0` | `0x0db4` | `0x00072BF9` | `0x32BF8` |
| 8 | `0xC4CF8` | `0x0db5` | `0x00072BF9` | `0x32BF8` |
| 9 | `0xC4D00` | `0x0db6` | `0x00072AAD` | `0x32AAC` |
| 10 | `0xC4D08` | `0x0db7` | `0x00072AAD` | `0x32AAC` |
| 11 | `0xC4D10` | `0x0db8` | `0x00072CFD` | `0x32CFC` |
| 12 | `0xC4D18` | `0x0db9` | `0x00072CFD` | `0x32CFC` |
| 13 | `0xC4D20` | `0x0dba` | `0x000726FD` | `0x326FC` |
| 14 | `0xC4D28` | `0x0dbb` | `0x0007434D` | `0x3434C` |
| 15 | `0xC4D30` | `0x0dbc` | `0x0007469D` | `0x3469C` |
| 16 | `0xC4D38` | `0x0dbd` | `0x000745E1` | `0x345E0` |
| 17 | `0xC4D40` | `0x0dbe` | `0x00074771` | `0x34770` |
| 18 | `0xC4D48` | `0x0dbf` | `0x00074899` | `0x34898` |
| 19 | `0xC4D50` | `0x0dc0` | `0x00074599` | `0x34598` |
| 20 | `0xC4D58` | `0x0dc1` | `0x00074551` | `0x34550` |
| 21 | `0xC4D60` | `0x0dc2` | `0x000724CD` | `0x324CC` |
| 22 | `0xC4D68` | `0x0dc3` | `0x00072F05` | `0x32F04` |
| 23 | `0xC4D70` | `0x0dc4` | `0x00072F05` | `0x32F04` |
| 24 | `0xC4D78` | `0x0dc5` | `0x000723B9` | `0x323B8` |

Every listed handler was disassembled at its file offset and is a coherent
function start (e.g. `0x32F6C push {r3,r4,r5,r6,r7,lr}`, `0x32B88 ldrb r3,[r1,#2]`,
`0x3434C push.w {r4,…,lr}`), and the four consecutive tiny thunks
`0x32BD0/0x32BDA/0x32BE4/0x32BEE` are each exactly 10 bytes and all `b.w 0x90D44`
with different argument registers — internal consistency that the table is real.

### 2.3 The three candidates' case values (for completeness)

| function | selection | case values | handler addresses |
|---|---|---|---|
| `0xDA0FC` | bounds check + indexed table `RAM[0x12238+4*id]` | `id = 0..0x31` (50 cases), `len ≤ 0x1e` | **not recoverable** (RAM table, not in image) |
| `0xDB5EC` | comparison chain on `struct[1]` | `0x00`, `0x34` | inline (no addresses; `0xDB628`, `0xDB63E`) |
| `0xDC378` | comparison chain on `struct[1]` | `0x32`, `0x33` | inline (`0xDC3C0` → `bl 0xDBFB0`; `0xDC392`) |

### 2.4 Other `{cfg_id, handler}` tables in the image (context)

Scanning all 8-byte `{u32, u32}` records with a valid Thumb handler
(`w & 1` and `w−0x40000` in code) found four more tables in the data region:

* `0xC3CD8` — 59 records, cfg ids `0x00BC…0x07E6` (unordered: e.g. `0x0128→0x4313C`,
  `0x0111→0x410BC`, `0x010A→0x4733C`, `0x0190→0x60AA8`). Ordering is not
  ascending, so this is not a linear-search table — likely a different
  (e.g. hashed/bucketed) dispatch or a parameter descriptor array.
* `0xC4688` — 7 records, cfg `0x0A29–0x0A2F`.
* `0xC4750` — 9 records, cfg `0x0709–0x0711`.
* `0xC4ED8` — 4 records, cfg `0x076D–0x0770`.

---

## 3. Binding to the live-captured opcodes

Live facts (phase 5/6): the request header carries `u16 0x0101` and a length word
(`0x010e`=270 for equipment commands, `0x0e`=14 for `get_cca_th`); the request
and response payload begins with a 32-bit id word `0x0D0<dir>_<cfg_id>`
(`0x0d01` = get, `0x0d00` = set) whose low 16 bits are the driver `cfg_id`.
Captured cfg_ids: `0x0dad` (xo_ducy_cali), `0x0dae`/`0x0db0` (2g/5g power),
`0x0db2` (xo_ppm_cali), `0x0db3` (rssi), `0x0db4`/`0x0db5` (2g/5g all_curve),
`0x0db6`/`0x0db7` (2g/5g curve_factor), `0x0db8`/`0x0db9` (2g/5g upc);
`get_cca_th` = `0x012e`.

### 3.1 PROVEN by matching constants — the `0x0d01`/`0x0d00` family

1. **The dispatcher consumes the live id word's low half directly.**
   `0x3245A ldrh r2,[r1]` reads a u16 from the message; the live payload word is
   little-endian `<cfg_lo> <cfg_hi> 01 0d` (get) or `<cfg_lo> <cfg_hi> 00 0d`
   (set), so `ldrh [msg]` = the `cfg_id`.
2. **The range and table constants match the live cfg_ids exactly.**
   The guard is `cfg_id − 0x0dad ≤ 0x1c`, and the table at `0xC4CB8` contains
   **all** live get cfg_ids `0x0dad`, `0x0dae`, `0x0db0`, `0x0db2`, `0x0db3`,
   `0x0db4`, `0x0db5`, `0x0db6`, `0x0db7`, `0x0db8`, `0x0db9` (plus the
   uncaptured `0x0daf`, `0x0db1`, `0x0dba–0x0dc5`). This is an exact constant
   match, not a range coincidence.
3. **The `0x0d01`/`0x0d00` direction split is proven at the handler.** Handler
   `0x32F6C` (cfg `0x0dad`) begins:
   ```
   0x32F6C push  {r3, r4, r5, r6, r7, lr}
   0x32F6E ldrb  r6, [r1, #2]     ; = 0x01 for get (id word 0x0d01_xxxx)
   0x32F70 mov   r4, r1           ;   = 0x00 for set (id word 0x0d00_xxxx)
   0x32F72 cmp   r6, #1
   0x32F74 ldr.w r5, [r0, #0x4b0]
   0x32F78 bne   #0x33012
   ```
   and handler `0x32B88` (cfg `0x0db2`) begins `ldrb r3,[r1,#2]; cmp r3,#1`.
   The byte at payload `+2` is exactly the `0x01`/`0x00` that distinguishes the
   live get `0x0d01` from set `0x0d00` id words. **Both directions bind to the
   same cfg_id handler**, selected at `msg+2`.

### 3.2 NOT bound — the three candidates

None of `0xDA0FC`, `0xDB5EC`, `0xDC378` compares any of `0x0101`, `0x010e`,
`0x0d01`, `0x0d00`. Their opcode constants are `0x00`, `0x1e`, `0x31`, `0x34`,
`0x32`, `0x33`. So no case in §2.3 corresponds to a live opcode by a matching
constant; the earlier lead's "three dispatchers" are internal SMAC per-band
state machines, not the alg-message dispatcher.

### 3.3 Plausible but NOT proven

* **`0x010e` (270) and `0x0e` (14) are length fields, not cases.** No dispatcher
  in the image compares an opcode/length against `0x010e`; the value lives in the
  transport header (`msg+0x0a` on the host-built message) and in a few unrelated
  firmware functions (`mov.w r1,#0x10e` at `0x2C84C`, `0x2FB44`, `0x82002`,
  0xB9356` — all memcpy/log lengths, none a branch). The 270-byte requests are
  the ones carrying the `0x0dad–0x0dc5` cfg_ids that `0x3243C` dispatches, so the
  association is plausible, but no constant equality proves it.
* **`get_cca_th` (`0x012e`, len 14).** Its cfg_id is outside the `0x3243C`
  range (`0x012e − 0x0dad` underflows, so the guard fails), and a file-wide scan
  for a `{u32 0x012e, u32 handler}` record finds **no** valid entry (the single
  occurrence of the u32 word `0x0000012e`, at file `0xC939D`, is followed by
  `0x84000000`, not a code pointer). So its handler is **unresolved**. The
  `0xDA0FC` candidate would admit a 14-byte message (its length bound is
  `0x1e ≥ 0x0e`) and `0x2e ≤ 0x31`, so a short-message dispatcher *could* handle
  it at id `0x2e`; that is only plausible, and the `0xDA0FC` table is not in the
  image, so it cannot be confirmed.

---

## 4. Calibration-family handler

The live calibration-family cfg_ids that bind are `0x0dad`
(`get_xo_ducy_cali_param`), `0x0db2` (`get_xo_ppm_cali_param`), and the
broader `0x0dxx` equipment set. Their handlers are in §2.2; the two calibration
handlers' first instructions:

**cfg `0x0dad` → file `0x32F6C`** (runtime `0x72F6D`):

```
0x32F6C push  {r3, r4, r5, r6, r7, lr}
0x32F6E ldrb  r6, [r1, #2]      ; direction byte (1 = get)
0x32F70 mov   r4, r1
0x32F72 cmp   r6, #1
0x32F74 ldr.w r5, [r0, #0x4b0]
0x32F78 bne   #0x33012          ; set path
0x32F7A ldrb  r0, [r5]
0x32F7C mov   r1, r6
0x32F7E bl    #0x71ee4
0x32F82 mov   r5, r0
0x32F84 cbnz  r0, #0x32fa6
0x32F86 movw  r1, #0x1e2f
0x32F8A ldr   r0, [pc, #0xd0]
0x32F8C bl    #0x2c28
```

**cfg `0x0db2` → file `0x32B88`** (runtime `0x72B89`):

```
0x32B88 ldrb  r3, [r1, #2]      ; direction byte
0x32B8A ldr   r2, [pc, #0x38]
0x32B8C cmp   r3, #1
0x32B8E bne   #0x32b9e          ; set path
0x32B90 ldr   r2, [r2]
0x32B92 str.w r3, [r1, #0x10a]  ; writes the response word-count field
0x32B96 str.w r2, [r1, #0x8a]   ; writes the response data word
0x32B9A movs  r0, #0
0x32B9C bx    lr
0x32B9E ldr.w r3, [r1, #0xa]
```

The `0x8a` data offset and `0x10a` word-count field in the `0x32B88` handler are
exactly the response layout measured live in phase 6 (`table base +0x8a`, count
at `+0x10a`), an independent confirmation of the binding.

The 2g/5g power-parameter pair binds to two adjacent 10-byte thunks —
`0x0dae → 0x32BEE`, `0x0db0 → 0x32BDA` — each ending in `b.w 0x90D44` with a
different `(r1,r2)` argument pair.

`get_cca_th` (`0x012e`) has **no** bound handler (§3.3).

---

## 5. Explicit limits

1. **The `0xDA0FC` 50-entry handler table is not in the image.** Its word is read
   from runtime `0x12238`, below the file's runtime code base, and no 50-entry
   code-pointer run and no literal `0x12238`/`0x118b0` exist in the file. The 50
   handler addresses are therefore unrecoverable from FIRMWARE.bin; the claim
   that this dispatcher is unrelated to the alg opcodes rests on its constants
   and on the fact that the alg dispatch is fully accounted for by `0x3243C`.
2. **`get_cca_th` (`0x012e`) handler unresolved.** It is out of the `0x3243C`
   range and no `{0x012e, handler}` record was found. The `0xDA0FC`/id-`0x2e`
   route is a hypothesis only.
3. **The three candidates' exact roles are inferred.** They were identified by
   `cmp #imm` on `ldrb [rN,#1]`; the disassembly proves they are switches, but
   their calling context (which SMAC message class invokes them) was not traced.
   Their handler tables (`0x13afc+4*idx`, `0x111e4+4*idx`) are RAM and were not
   dereferenced.
4. **The `0x3243C` caller and message length were not traced.** The dispatcher
   reads `cfg_id` at `msg+0` and calls `handler(dev,msg)`; nothing here proves
   which transport path (and thus which `0x010e`/`0x0e` length) delivered the
   message. The 270-byte association is plausible, not proven.
5. **The other `{cfg,handler}` tables in §2.4 were not disassembled**, and the
   `0xC3CD8` table's ordering suggests it may not be a linear-search table at all;
   its role (and whether `0x012e` is dispatched by it) is unresolved.
6. **Handler→name mapping is by cfg_id, not by symbol.** The firmware image has no
   symbolic name for these handlers; the command names come from the phase-2/5/6
   driver-side cfg_id table. The cfg_id↔handler binding is direct and constant-
   matched; the cfg_id↔command-name binding is inherited from the driver reports.
7. **The tail `smac_msg_proc_*` name pool remains unreferenced** (phase 8,
   `cali-handler-address.md`); nothing found here changes that.

---

## Verification

Every claim above is backed by a scan or disassembly run in this session
(`pyenv/Scripts/python.exe`, `build/tmp/FIRMWARE.bin`; capstone Thumb):

* **§1 (three candidates).** Verbatim capstone windows
  `0xDA0FC-0xDA14E`, `0xDB540-0xDB660`, `0xDC300-0xDC4B4`.
* **Runtime = file + 0x40000.** Phase-4 table word `0x4bd20` → file `0xbd20`,
  which disassembles to `push {r4,r5,r6,r7,lr}; bl 0x81dc0; bl 0xbcfe` matching
  the phase-7 `debug` handler's callees; the raw `0x4bd20` window does not.
* **§2.1 dispatcher.** Verbatim window `0x3243C-0x324BC`; literal at `0x324C8`
  read as `B8 4C 10 00` = `0x00104CB8`; pointer scan for `0x00104CB8` found it at
  `0x324C8`, `0x326F8`, `0x33480`, `0x365B0`, `0x38648`, `0x39480`.
* **§2.2 table.** `struct.unpack_from('<II')` at `0xC4CB8 + 8*i`, i = 0..24;
  row `0x0dad→0x72F6D` etc. Each handler decoded at `(W&~1)−0x40000`.
* **§2.2 handler coherence.** Disassembly of `0x32F6C`, `0x32B88`, `0x32BEE`,
  `0x32BDA`, `0x32A38`, `0x32BF8`, `0x32AAC`, `0x32CFC`, `0x326FC`, `0x324CC`,
  `0x3434C`, `0x32F04`, `0x323B8` — all coherent function starts.
* **§2.3.** Bounds/constants read from the §1 windows; 50-entry code-pointer run
  scan over all 4-aligned words (max run lengths 32/28/20/17/16, none 50).
* **§2.4.** Whole-file scan for 8-byte `{u32,u32}` records with
  `u32 ∈ [0x0100,0x4000]` and `w&1` and `w−0x40000` in code → the five tables
  listed.
* **§3.** Live cfg_id values from phase-5/6; dispatcher `ldrh`/range/table from
  §2.1; direction-byte handler reads `0x32F6E`/`0x32B88` from §2.2/§4.
* **§3.3.** File-wide scan for `{u32 0x012e, u32 handler}` → only one u32
  occurrence (`0xC939D`, followed by `0x84000000`); `mov.w r1,#0x10e` sites
  enumerated by the bounded-function immediate sweep (`0x2C84C`, `0x2FB44`,
  `0x82002`, `0xB9356`).
* **§4.** Verbatim windows `0x32F6C-0x32FBC` and `0x32B88-0x32BD0`; `+0x8a`/`+0x10a`
  cross-checked against `phase6/message-fields.md` §2.2.
* **Negative results.** No `movw/movt` pair and no PC-relative literal builds
  `0xC4CB8` under any base tested other than `0x10C4CB8`; no reference to the tail
  name pool was added by this pass.

**Bottom line.** The alg message-opcode dispatcher is file `0x3243C`, indexed by
the live id word's low 16 bits (`ldrh [msg]`) through the 25-entry
`{cfg_id, handler}` table at file `0xC4CB8`; the calibration cfg_ids
`0x0dad`/`0x0db2` are bound by exact constant matches to handlers `0x32F6C`/
`0x32B88`, and the `0x0d01`/`0x0d00` get/set split is proven by the `cmp #1` on
the payload's `+2` direction byte. The three prior candidate functions are
internal per-band switches and do not bind. `get_cca_th` (`0x012e`) and the
`0xDA0FC` 50-entry RAM table remain unresolved.
