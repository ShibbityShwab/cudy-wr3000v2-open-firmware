# Calibration-store record semantics from the three RF-chain walkers (phase 10, 2026-10-01)

Scope: local files, read-only. Inputs: `build/versions/2.5.24/FIRMWARE.bin` (928,920 B =
`0xE2C98`), also `build/versions/2.4.15/FIRMWARE.bin` (byte-identical, md5
`0e530b976d5a20e87358671f1a577695` for both, and for `build/tmp/FIRMWARE.bin`). Tooling:
`pyenv/Scripts/python.exe` + capstone 5.0.7 (`CS_ARCH_ARM`, `CS_MODE_THUMB`). Prior art:
`ulw/phase8/firmware-cali-parser.md` (the three walkers), `ulw/phase8/kv-payload-check.md`
(112/48 skeleton), `ulw/phase8/cali-buffer-populator.md` (the buffer is a verbatim file copy),
`ulw/phase8/opcode-dispatchers.md` (RAM table conventions), `ulw/phase4/firmware-disasm.md`
(the `-0x40000` bias).

**Result in one line.** The three walkers do **not** walk the `.kv` payload and do **not** stride by
112. Each one indexes a **36-byte chain element** (`0x24` stride) at a **one-time base offset
`0x70`** inside a firmware **RAM** object, using loop bounds of **4, 5 and 10**; the `.kv` store's
112/48-byte periods (and any 45-58 record count) appear nowhere in them. The `0x24*idx + 0x70`
coincidence is real arithmetic but a false lead: `0x70` is a constant base add, never a period, and
`0x24` is a sub-record stride, never a record stride. §7 states the arithmetic that would be
required for a store mapping and shows it fails; §5-6 table what the walkers *do* touch.

Nothing in this document is applied to the store: no field of `wifi_cali_data*.kv` is assigned a
name by this pass. The store's field order remains **open** (as `kv-payload-check.md` already said).

---

## 1. Address convention (and the `-0x40000` bias)

The task's "remember the `-0x40000` file bias" resolves as follows, and it is load-bearing here.

* **File offset -> runtime code address:** `runtime = file_offset + 0x40000` (phase 4; re-confirmed
  by `opcode-dispatchers.md` §2.2, where a PC-relative literal `0x00104CB8` is the runtime image of
  file `0xC4CB8`). So the three walkers at file `0xDC4EC / 0xDCB78 / 0xDD688` execute at runtime
  `0x11C4EC / 0x11CB78 / 0x11D688` respectively.
* **The addresses the walkers put in registers are *runtime* addresses, not file offsets.** They
  use `movw <reg>,#imm16; movt <reg>,#1` to build values below `0x40000` (e.g. `0x118B0`,
  `0x13AA0`, `0x10078`, `0x1199C`). Because `imm16 < 0x10000` and `movt #1` supplies the top half,
  these are `0x0001xxxx` addresses. Under `file = runtime - 0x40000` they would be **negative**, i.e.
  they are **RAM/TCM objects, not bytes of `FIRMWARE.bin`**. This is the code's own proof that the
  walkers act on a runtime RAM object.
* **Reading the walkers from the file.** I disassembled the raw file at the *file* offsets
  `0xDC4EC/0xDCB78/0xDD688` (no bias applied), and the bytes there decode to coherent Thumb-2 whose
  instructions reproduce every excerpt in `firmware-cali-parser.md` §3.1 verbatim. The
  `-0x40000` bias is therefore **not** applied when locating the walkers; it is applied only when
  interpreting the `0x0001xxxx` data addresses they build.

Raw confirmation of the three prologues and the `0x118B0` construction:

```
file 0x0dc4ec: 2d e9 f0 47   push.w {r4,r5,r6,r7,r8,sb,sl,lr}
file 0x0dcb78: 2d e9 f0 4f   push.w {r4,r5,r6,r7,r8,sb,sl,fp,lr}
file 0x0dd688: 2d e9 f0 47   push.w {r4,r5,r6,r7,r8,sb,sl,lr}

file 0x0dc5ae: 41 f6 b0 06   movw  r6,#0x18b0
file 0x0dc5ba: c0 f2 01 06   movt  r6,#1          ; r6 = 0x000118b0
file 0x0dcb80: 41 f6 b0 04   movw  r4,#0x18b0
file 0x0dcb92: c0 f2 01 04   movt  r4,#1          ; r4 = 0x000118b0
file 0x0dd694: 41 f6 b0 03   movw  r3,#0x18b0
file 0x0dd6a0: c0 f2 01 03   movt  r3,#1          ; r3 = 0x000118b0
```

Two other constants matter and are RAM too (built the same way): `0x13AA0` (a per-`[msg[0]]` table
base: `0xda5xx`/`0xdcbxx`/`0xdd6xx` all use `movw #0x3aa0; movt #1`), and `0x10078`
(`movw #0x78; movt #1` at `0x0dc648`).

The full disassembly used below is the file-range dump saved at `build/tmp/cali_dis.txt`
(windows `0xDC4EC-0xDC970`, `0xDCB78-0xDD0C0`, `0xDD688-0xDD7F0`).

---

## 2. Walker A - file `0xDC4EC` (runtime `0x11C4EC`)

### 2.1 Entry and bounds

`r0` is the argument and is copied to `r4` (`0x0dc500 mov r4,r0`). This is a **message/state
struct**, not a file buffer; it is read at `[r0]`, `[r0,#1]`, `[r4,#0x228]`, `[r4,#0x234]`, etc.
Two bytes select the branch: `ip=[r0]` (index) and `r6=[r0,#1]` (opcode).

```
0x0dc4f0: ldrb.w ip, [r0]          ; ip  = M[0]     index
0x0dc4fc: ldrb   r6, [r0, #1]      ; r6  = M[1]     opcode
0x0dc506: cmp    r6, #0
0x0dc508: beq    #0xdc580
0x0dc50a: cmp    r6, #0x53
0x0dc50c: beq    #0xdc522
```

The RF-chain path (opcode `0`) bounds the chain index at **4** and rejects larger:

```
0x0dc580: ldrb.w r1, [r0, #0x228]  ; r1 = M[0x228]           chain index
0x0dc584: cmp    r1, #3
0x0dc586: bhi.w  #0xdc7b4          ; error/log path if index > 3
```

The element index is then **remapped through a RAM byte table** and re-bounded:

```
0x0dc648: movw   r3, #0x78
0x0dc64a: movt   r3, #1            ; r3 = 0x10078
0x0dc650: add    r3, r1            ; r3 = 0x10078 + chain_index
0x0dc652: ldrb.w r1, [r3, #0x49c]  ; r1 = *(u8*)(0x14f14 + chain_index)   chain_id
0x0dc65c: add.w  r3, r1, r1, lsl #3 ; r3 = 9*chain_id
0x0dc660: add.w  r3, r4, r3, lsl #2 ; r3 = M + 36*chain_id        (= E - 0x70)
0x0dc666: ldrh.w lr, [r3, #0x72]    ; lr = E[+2]   u16 compare value
```

The pointer build itself:

```
0x0dc674: movs   r1, #0x24
0x0dc676: mla    r1, r1, r3, r4     ; r1 = 0x24*chain_id + M          (36-byte stride)
0x0dc67a: cmp    r3, #3
0x0dc67c: add.w  r1, r1, #0x70      ; r1 = M + 0x70 + 0x24*chain_id   (+112 base)
0x0dc680: str    r1, [r4, #0x1c]    ; M[0x1c] = selected element pointer
0x0dc682: bls    #0xdc5ae           ; chain_id <= 3 -> element block
```

and the second occurrence (paths where `r3` is set to `1` / `2` by the compare chain at
`0x0dc7b0`/`0x0dc7de`):

```
0x0dc7e0: movs   r1, #0x24
0x0dc7e2: mla    r1, r1, r3, r4     ; r1 = M + 0x24*idx
0x0dc7e6: adds   r1, #0x70          ; r1 = M + 0x24*idx + 0x70
0x0dc7e8: str    r1, [r4, #0x1c]
```

**Loop bound / base for walker A:** base `M` (the argument struct), **stride `0x24`**, one-time
**base add `0x70`**; index `chain_id` is bounded by `cmp r3,#3` / `bhi` and `cmp r3,#3` / `bls`,
so the index range is **0..3 (4 chains)**. The `x9, lsl#2` at `0x0dc65c-0x0dc660` is the same
`36*x` product expressed as `(9x)<<2`, and is used to read `E[+2]` without materialising the `+0x70`.

### 2.2 Element block `0x0dc5ae..0x0dc646`

Entered with `r3 = chain_id`; `r0 = M + 36*chain_id` (so `r0 = E - 0x70`):

```
0x0dc5cc: add.w r3, r3, r3, lsl #3   ; r3 = 9*chain_id
0x0dc5d4: add.w r0, r4, r3, lsl #2   ; r0 = M + 36*chain_id = E - 0x70
0x0dc5e0: ldrb.w r3, [r0, #0x7a]     ; r3 = E[+0x0a]   u8
0x0dc5e4: add   r2, ip               ; r2 = M[0] + 4*M[0]... (index accumulation)
0x0dc5e6: add.w r3, r3, r2, lsl #1   ; r3 = E[+0x0a] + 2*(...)
0x0dc5ea: add.w r3, r7, r3, lsl #2   ; r3 = 0x13aa0 + 4*(...)
0x0dc5ee: ldrh.w r2, [r0, #0x72]     ; r2 = E[+2]     u16
0x0dc5f2: ldr.w  r1, [r3, #0x880]    ; r1 = table[E[+2]-dependent]
0x0dc5f6: ubfx   r3, r2, #0, #0xf    ; r3 = E[+2] & 0x7fff   (15-bit index)
0x0dc5fa: ldr.w  r0, [r1, r3, lsl #2]; r0 = table2[15-bit index]  -> M[0x10]
0x0dc600: str    r0, [r4, #0x10]
```

Later, with `r3 = M[0x1c] = E`:

```
0x0dc728: ldr    r3, [r4, #0x1c]     ; r3 = E
0x0dc72c: strb.w r8, [r3, #0x18]     ; E[+0x18] = 1     u8
0x0dc730: strh.w r8, [r3, #6]        ; E[+0x06] = 1     u16
```

### 2.3 Every field offset walker A touches

`M` = argument (r4). Widths: `b`=1, `h`=2, `w`=4.

| base+off | w | instruction(s) | address(es) | what it does |
|---|---|---|---|---|
| M+0x00 | b | `ldrb.w ip,[r0]` | 0xdc4f0 | index into per-index table |
| M+0x01 | b | `ldrb r6,[r0,#1]`; `strb r0,[r4,#1]` | 0xdc4fc; 0xdc578 | opcode select; clears opcode |
| M+0x03 | b | `ldrb r2,[r4,#3]` | 0xdc5fe, 0xdc6b8 | arg to `0xdbfb0` |
| M+0x08 | b | `ldrb r0,[r4,#8]` | 0xdc758 | bit source |
| M+0x09 | b | `strb.w r8,[r4,#9]` | 0xdc76a | flag = 1 |
| M+0x10 | w | `str r0,[r4,#0x10]` | 0xdc600, 0xdc694 | table-lookup result |
| M+0x1c | w | `str r1,[r4,#0x1c]`; `ldr r3,[r4,#0x1c]` | 0xdc5aa/0xdc680/0xdc7e8; 0xdc728 | selected element pointer E |
| M+0x20 | - | `add.w r1,r4,#0x20` | 0xdc602, 0xdc6b4 | arg to `0xdbfb0` |
| M+0x2a | b | `ldrb.w r2,[r4,#0x2a]`; `strb.w r1,[r4,#0x2a]` | 0xdc8ae/0xdc8c2/0xdc8d6; 0xdc8ce | state bits |
| M+0x30 | w | `str r3,[r4,#0x30]`; `ldr r1,[r4,#0x30]` | 0xdc8cc; 0xdc8ec | packed rate value |
| M+0x48 | - | `add.w r2,r4,#0x48` | 0xdc636 | arg to `0xd8bf0` |
| M+0x72+0x24*i | h | `ldrh.w lr,[r3,#0x72]` (r3=M+36i) | 0xdc666 | E[+2], compare |
| M+0x7a+0x24*i | b | `ldrb.w r3,[r0,#0x7a]` (r0=M+36i) | 0xdc5e0 | E[+0x0a], index addend |
| M+0x96 | h | `ldrh.w r1,[r4,#0x96]` | 0xdc7a4 | E(idx1)[+2], compare |
| M+0xba | h | `ldrh.w r6,[r4,#0xba]` | 0xdc794 | E(idx2)[+2], compare |
| M+0xde | h | `ldrh.w r6,[r4,#0xde]` | 0xdc598 | E(idx3)[+2], compare |
| M+0x214 | b | `strb.w r6,[r4,#0x214]` | 0xdc56a | state |
| M+0x218 | - | `add.w r2,r4,#0x218` | 0xdc740 | arg to `0xd8bf0` |
| M+0x228 | b | `ldrb.w r1,[r0,#0x228]`; `strb.w r6,[r4,#0x228]` | 0xdc580; 0xdc6e8 | chain index; rewritten |
| M+0x229 | b | `strb.w r6,[r4,#0x229]` | 0xdc6f0 | state |
| M+0x22a | b | `strb.w r6,[r4,#0x22a]`; `ldrb.w r3,[r4,#0x22a]` | 0xdc6f8; 0xdc71e | state |
| M+0x22b | b | `strb.w r6,[r4,#0x22b]` | 0xdc708 | state |
| M+0x22c | h | `strh.w r0,[r4,#0x22c]`; `ldrh.w r3,[r4,#0x22c]` | 0xdc704; 0xdc718 | state |
| M+0x234 | w | `ldr.w r2,[r4,#0x234]`; `str.w r2,[r4,#0x234]` | 0xdc546/0xdc5b6/0xdc69e; 0xdc562/0xdc5d8/0xdc6ae | per-radio flag word |
| M+0x250 | w | `str.w r3,[r4,#0x250]` | 0xdc770 | scaled index |
| M+0x350..0x359 | b | `strb.w lr/r3/ip/r1/r6,[r4,#0x350..0x355]`; `strb.w r2/lr,[r4,#0x358/0x359]`; `ldrb.w r2/r3,[r4,#0x358/0x359]`; `ldrb.w r3,[r4,#0x350]` | 0xdc80c-0xdc838; 0xdc85a/0xdc85e; 0xdc876 | derived cal bytes (see note) |

Note on `M+0x350..0x359` (0x0dc7ec-0x0dc83c): five `ubfx` extracts (`#0,#6`, `#6,#7`, `#0xd,#3`,
`#0x10,#4`, `#0x18,#2`) of `[r7+0x18]` (`r7=[r6+0x7ac]`) packed back into bytes, then mixed with
`[r7+0xb4]` - this is the only place in the three walkers that resembles calibration-value
manipulation, and it still operates on a RAM struct (`0x118b0 + 4*band + 0x7ac`), not on the `.kv`
buffer.

---

## 3. Walker B - file `0xDCB78` (runtime `0x11CB78`)

### 3.1 Entry and base

`r0` is a **band index** and is kept in `r8`; `r5` and the per-band object are formed by an integer
multiply, and `r4` is the RAM base `0x118B0` built in §1:

```
0x0dcb7c: mov.w  r5, #0x35c
0x0dcb80: movw   r4, #0x18b0
0x0dcb86: sub    sp, #0x1c
0x0dcb88: mul    r5, r5, r0          ; r5 = 0x35c * band
0x0dcb8c: bl     #0xd7ea0            ; helper (lock/init)
0x0dcb92: movt   r4, #1              ; r4 = 0x118b0
0x0dcb96: add.w  sl, r4, r5          ; sl = P = 0x118b0 + 0x35c*band   <-- per-band base
0x0dcb9a: ldr.w  r3, [sl, #0x320]
```

So **base P = `0x118B0 + 0x35C * band`** (the *record* stride, if any, is `0x35C = 860`, not
`0x70`). `r7 = P + 0xEC` (`0x0dcbea add.w r3,r4,#0xec`; `0x0dcbf0 add.w r7,r3,r5`).

### 3.2 The two distinct `0x24` walks

**(a) Descending u16 scan, 5 iterations.** `r3` starts at `8` and is decremented until it equals 3;
`ip` starts at `P+0xEC` and is decremented by `0x24` each pass:

```
0x0dcc00: movs   r3, #8
0x0dcc18: add.w  r1, sl, r3, lsl #6   ; r1 = chain_table + 64*r3
0x0dcc1c: ldr    r0, [r1, #0x54]
0x0dcc36: ldrh.w r0, [ip, #0x192]     ; u16 at (P+0xEC)+0x192 - 0x24*k = P+0x27e - 0x24*k
0x0dcc40: subs   r3, #1
0x0dcc42: cmp    r3, #3
0x0dcc44: sub.w  ip, ip, #0x24        ; 36-byte decrement
0x0dcc48: bne    #0xdcc18             ; body r3 = 8,7,6,5,4   (5 passes)
```

**(b) The `0x24*idx + 0x70` element pointer.** On the mismatch exit of the scan, `r3` is the scan
index (4..8), and the selected element is built at `0x0dcd0a`:

```
0x0dcd0a: add.w  r1, r3, r3, lsl #3   ; r1 = 9*idx
0x0dcd0e: add.w  r3, r5, #0x70        ; r3 = P + 0x70
0x0dcd12: add.w  r3, r3, r1, lsl #2   ; r3 = P + 0x70 + 0x24*idx
0x0dcd16: ldr    r1, [pc, #0x388]     ; r1 = literal @0xdd0a0 = 0x0001199c
0x0dcd18: add    r3, r1               ; r3 = P + 0x70 + 0x24*idx + 0x1199c
0x0dcd1a: str.w  r3, [lr, #0x108]     ; P[0x108] = element pointer   (lr = sl = P)
```

The same idiom on the rate path (index `fp`, 0..9):

```
0x0dce56: movs   r3, #0x24
0x0dce58: add.w  r0, r5, #0x70        ; r0 = P + 0x70   (r5 = P here)
0x0dce5c: smlabb r3, r3, fp, r0       ; r3 = 0x24*fp + P + 0x70
0x0dce62: add    r1, r4
0x0dce64: add    r3, r0
0x0dce66: cmp.w  fp, #9
0x0dce6a: str.w  r3, [r1, #0x108]     ; P[0x108] = element pointer
0x0dce72: bls.w  #0xdcd22             ; rate loop while fp <= 9  -> 10 entries
```

The literal at `0x0dd0a0` was resolved from the raw bytes `9c 19 01 00` = `0x0001199C`; all four
`ldr [pc,#imm]` in walker B (`0xdcd16`, `0xdcd62`, `0xdcdf6`, `0xdce60`) resolve to the same word
(the imm values differ so the PC-relative sums land on `0x0dd0a0`).

**Loop bounds / base for walker B:** base `P = 0x118B0 + 0x35C*band`; element stride `0x24`;
one-time base add `0x70`; bounds **5 (scan `r3`=8..4)** and **10 (rate `fp`=0..9)**. There is no
112-byte stride and no 45-58 counter.

### 3.3 The element blob `P[0x108]` and dispatch

The pointer stored in `P[0x108]` is dereferenced with a byte at `+0xa`, and a value of 7 selects a
second sub-path:

```
0x0dcdb0: ldr.w  r2, [r3, #0x108]     ; r2 = element pointer
0x0dcdb4: ldrb   r1, [r2, #0xa]
0x0dcdb6: cmp    r1, #7
0x0dcdb8: beq    #0xdce78
...
0x0dce78: ldrh.w r1, [r3, #0xf6]      ; r3 = element pointer
0x0dce7c: ldrb.w r3, [r3, #0x111]
```

(**Limit:** because `P[0x108]` carries the `+0x1199C` add, the offsets `+0xa / +0xf6 / +0x111` are
relative to a blob in *another* RAM region, not to `P`; I did not recover that region's identity -
see §9.)

### 3.4 Every field offset walker B touches on `P`

| P+off | w | instruction(s) | address(es) | what it does |
|---|---|---|---|---|
| P+0x320 | w | `ldr.w r3,[sl,#0x320]`; `str.w r3,[sl,#0x320]` | 0xdcb9a; 0xdcbba | flag word, `orr #0x1000`, `orr #1` |
| P+0x2c4 | b | `strb.w r3,[sl,#0x2c4]`; `ldrb.w r2,[r5,#0x2c4]` | 0xdcbda; 0xdccca | chain count = `ubfx([r2+0xc],0,4)` |
| P+0x2c5 | b | `strb.w r2,[sl,#0x2c5]` | 0xdcbde | `ubfx([r2+0xc],4,2)` |
| P+0x2c6 | b | `strb.w r2,[sl,#0x2c6]` | 0xdcbce | `ubfx([r2+0xc],7,1)` |
| P+0x2c7 | b | `strb.w r2,[sl,#0x2c7]` | 0xdcbc6 | `ubfx([r2+0xc],6,1)` |
| P+0xec | b | `ldrb.w r2,[sl,#0xec]`; `ldrb.w r3,[sl,#0xec]` | 0xdcc04; 0xdcf0a | chain/table index |
| P+0xed | b | `strb.w r2,#0x90,[r3,#0xed]` | 0xdd06e | state = 0x90 |
| P+0xee | b | `ldrb.w sb,[sl,#0xee]` | 0xdcbe6 | branch selector (`cmp sb,#1`) |
| P+0xf6 | h | `ldrh.w r1,[r3,#0xf6]` | 0xdce78 | via element blob |
| P+0xfa | h | `ldrh.w r0,[lr,#0xfa]` | 0xdcc28 | chain bitmask |
| P+0xfc | w | `str.w r1,[r8,#0xfc]` (walker C) | 0xdd766 | chain-table lookup result |
| P+0x108 | w | `str.w r3,[lr,#0x108]`; `ldr.w r2,[r3,#0x108]` | 0xdcd1a/0xdce6a; 0xdcdb0 | selected element pointer |
| P+0x111 | b | `ldrb.w r5,[sl,#0x111]`; `ldrb.w r3,[r3,#0x111]` | 0xdcf7c; 0xdce7c | via element blob |
| P+0x112 | b | `ldrb.w lr,[sl,#0x112]` | 0xdcf40 | cal byte |
| P+0x114 | b | `ldrb.w fp,[sl,#0x114]` | 0xdcf4c | cal byte |
| P+0x2e4 | w | `str.w fp,[sl,#0x2e4]`; `ldr.w r3,[sl,#0x2e4]` | 0xdcf44; 0xdd040 | blob pointer |
| P+0x2e8..0x2ff | b, ~18 sites | `strb.w`/`ldrb.w` (see §3.5) | 0xdcf56..0xdd03a | packed byte record |

### 3.5 The packed byte record `P+0x2e8 .. P+0x2ff`

Walker B builds one 32-bit word `r3` from ~17 individual bytes before a call:

```
0x0dcf56: strb.w r2, [sl, #0x2e8]   ; = 0
0x0dcf5a: strb.w r2, [sl, #0x2ea]   ; = 0
0x0dcf5e: strb.w lr, [sl, #0x2ec]
0x0dcf62: strb.w fp, [sl, #0x2eb]
0x0dcf70: str.w  r2, [sl, #0x2f4]
0x0dcf74: strb.w r2, [sl, #0x2ff]
0x0dcf80: strb.w sb, [sl, #0x2e9]
0x0dcf84: strb.w r5, [sl, #0x2f2]
0x0dcf88: strb.w sb, [sl, #0x2fd]
0x0dcf8c: strb.w sb, [sl, #0x2fe]
...  (reads back 0x2e8..0x2ff and packs with `bfi` at 0x0dcf96-0x0dd03e)
0x0dd03e: str    r3, [r2, #8]        ; r2 = *(0x118b0 + 4*band + 0xe4)
0x0dd048: str    r3, [r2, #4]        ; fp value
0x0dd05c: str    r1, [r2]
```

The `bfi` chain (`bfi r3,<byte>,#bit,#1` for bits 0,4,5,6,7,0xa,0xb,0xc,8-9,0xd,0xe,0x10,
0x11,0x12,0x13,0x14,0x15,0x16, plus `bfi r3,r2,#0x18,#8`) packs the `0x2e8..0x2f7` bytes into one
word; `P+0x2f1` feeds a second packed word `r1`, and `P+0x2f8` is conditionally stored to
`[r2+0x8b0]` (`0x0dd054-0x0dd058`). This is a per-band **config/status
word**, not a copy of `.kv` bytes.

---

## 4. Walker C - file `0xDD688` (runtime `0x11D688`)

### 4.1 Entry, base, bound

`r0` = band index (`r5`). Two RAM views are formed: `r7 = 0x118B0 + 4*band` (a per-radio pointer
table) and `r8 = 0x118B0 + 0x35C*band` (= `P`, the same per-band object as walker B):

```
0x0dd694: movw   r3, #0x18b0
0x0dd6a0: movt   r3, #1              ; r3 = 0x118b0
0x0dd6a4: lsl.w  sb, r5, #2          ; sb = 4*band
0x0dd6a8: add.w  r7, r3, sb          ; r7 = 0x118b0 + 4*band
0x0dd6c0: mul    r6, r6, r5          ; r6 = 0x35c*band   (r6 preset to 0x35c)
0x0dd6cc: add.w  r8, r3, r6          ; r8 = P = 0x118b0 + 0x35c*band
```

Guard / index source - it aborts unless `P[0xed] == 0`, and derives the index `r2` from a 4-bit
field:

```
0x0dd6dc: ldrb.w ip, [r8, #0xed]
0x0dd6e0: cmp.w  ip, #0
0x0dd6e4: bne    #0xdd794            ; error path
0x0dd6ee: ldr.w  r1, [r2, #0x84]     ; r2 = *(0x118b0 + 4*band + 0x7ac)
0x0dd6f2: ubfx   r1, r1, #0, #4      ; r1 = index, 0..15
0x0dd6f6: uxtb   r2, r1              ; r2 = index
0x0dd6f8: cmp    r2, #9
0x0dd6fa: strb.w r1, [r8, #0x2d0]    ; P[0x2d0] = index
0x0dd6fe: strb   r1, [r4, #5]
0x0dd700: str.w  sl, [r0, #0x1c]     ; sl = 0x1000
0x0dd704: bhi    #0xdd7ae            ; error if index > 9
```

### 4.2 The `0x24*idx + 0x70` element pointer

```
0x0dd706: movw   r0, #0x3aa0
0x0dd710: ldr.w  sb, [sb, #0xd0]     ; sb = table[4*band + 0xd0]
0x0dd714: add.w  r1, r2, r2, lsl #3  ; r1 = 9*idx
0x0dd718: add.w  sb, sb, r2, lsl #6  ; sb = table + 64*idx
0x0dd71c: lsls   r1, r1, #2          ; r1 = 36*idx
0x0dd71e: add.w  sb, sb, #0x60
0x0dd722: add.w  sl, r1, r6          ; sl = 36*idx + 0x35c*band
0x0dd726: ldr.w  sb, [sb, #4]
0x0dd72a: add    sl, r3              ; sl = 0x118b0 + 0x35c*band + 36*idx  (= P + 0x24*idx)
0x0dd72c: ldrh.w sl, [sl, #0x15e]    ; sl = P[0x24*idx + 0x15e] = E[+2]
0x0dd730: uxth.w sb, sb
0x0dd734: cmp    sl, sb              ; compare E[+2] with config table value
0x0dd736: beq    #0xdd7d0
0x0dd738: ldrb.w sb, [r8, #0xec]     ; sb = P[0xec]
0x0dd73c: adds   r1, #0x70           ; r1 = 36*idx + 0x70        <-- +112 base add
0x0dd73e: add.w  sb, sb, sb, lsl #2  ; sb = 5*P[0xec]
0x0dd742: add.w  sb, r2, sb, lsl #1  ; sb = idx + 10*P[0xec]
0x0dd746: adds   r3, #0xec           ; r3 = 0x118b0 + 0xec
0x0dd748: add    r1, r6              ; r1 = 0x35c*band + 36*idx + 0x70
0x0dd74a: add.w  r0, r0, sb, lsl #2  ; r0 = 0x13aa0 + 4*(idx + 10*P[0xec])
0x0dd74e: add    r1, r3              ; r1 = 0x118b0 + 0x35c*band + 0xec + 0x70 + 36*idx
0x0dd750: ldr.w  r0, [r0, #0x880]    ; r0 = table2[...]
0x0dd754: str.w  r1, [r8, #0x108]    ; P[0x108] = element pointer
0x0dd758: ubfx   r1, sl, #0, #0xf    ; 15-bit index from E[+2]
0x0dd75c: ldr.w  r1, [r0, r1, lsl #2]; r1 = table3[15-bit index]
0x0dd760: cmp    r2, #8
0x0dd762: add.w  r0, r3, r6
0x0dd766: str.w  r1, [r8, #0xfc]     ; P[0xfc] = lookup result
0x0dd76a: beq    #0xdd7de
```

**Loop bound / base for walker C:** base `P = 0x118B0 + 0x35C*band`; element stride `0x24`;
one-time base add `0x70` (plus a constant `0xec` in the base); bound **`cmp r2,#9` -> index 0..9
(10 elements)**, with a special case at index 8. `E = P + 0xEC + 0x70 + 0x24*idx`; `E[+2]` is read
directly at `P + 0x15E + 0x24*idx` (since `0xEC + 0x70 = 0x15C`, `+2 = 0x15E`).

Walker C confirms the element layout independently of walker A: `E[+2]` is the 15-bit
(`ubfx #0,#0xf`) index into the same `0x880`-offset table, and the low result is stored in
`P[0xfc]`.

---

## 5. Field table for the record the walkers actually walk

The three walkers index the **same shape**: a 36-byte element `E` at
`base + <constant> + 0x24*idx`. Walker A's base is the message struct `M` with constant `0x70`;
walkers B/C use the per-band RAM object `P` with constant `0x70` (C adds `0xEC` before it). The
element's fields, and what the code does with each, are:

### 5.1 The 36-byte element `E` (table offsets are relative to `E`)

| E+off | width | the code | example address | role |
|---|---|---|---|---|
| +0x00 | - | element base (`P[0x108]` stores it) | 0xdc680; 0xdd754 | selected-chain pointer |
| +0x02 | u16 | `ldrh.w lr,[r3,#0x72]` / `ldrh.w sl,[sl,#0x15e]`; `ubfx #0,#0xf`; `ldr.w r?,[table,r?,lsl#2]` | 0xdc666, 0xdc5f6, 0xdc5fa; 0xdd72c, 0xdd758, 0xdd75c | **index** (15-bit) into a settings table; also **compared** with a config word (`cmp lr,r6` 0xdc66c; `cmp sl,sb` 0xdd734) |
| +0x06 | u16 | `strh.w r8,[r3,#6]` | 0xdc730 | write 1 (enable flag) |
| +0x0a | u8 | `ldrb.w r3,[r0,#0x7a]` (walker A) | 0xdc5e0 | **scale/addend**: scaled by 2 and added to a table index |
| +0x18 | u8 | `strb.w r8,[r3,#0x18]` | 0xdc72c | write 1 (enable flag) |

Only walker A writes `E`; walkers B/C read `E[+2]` (as a compare/table index) and store the
element pointer and the lookup result into `P[0x108]` / `P[0xfc]`. No walker reads a 112-byte
blob from the element.

### 5.2 Mapping the 36-byte elements onto the 112-byte frame

If one frames the region `[P+0x70, P+0x70+0x70)` (0..111) as the "112-byte record", the 36-byte
elements land at frame offsets `0x24*k`:

| k | E start | E+0x02 (index/compare) | E+0x06 (flag) | E+0x0a (scale) | E+0x18 (flag) |
|---|---|---|---|---|---|
| 0 | 0 (0x00) | 2 (0x02) | 6 (0x06) | 10 (0x0a) | 24 (0x18) |
| 1 | 36 (0x24) | 38 (0x26) | 42 (0x2a) | 46 (0x2e) | 60 (0x3c) |
| 2 | 72 (0x48) | 74 (0x4a) | 78 (0x4e) | 82 (0x52) | 96 (0x60) |
| 3 | 108 (0x6c) | **110 (0x6e)** | 114 (0x72)* | 118 (0x76)* | 132 (0x84)* |

`*` = outside the 112-byte window. The span `[0x70, 0x70+0x70)` holds three complete elements plus
the first 4 bytes of a fourth; **4 elements need 144 = 0x90 bytes, not 112.** This alone shows the
"112" is not a record-size in the code: it is a single constant added once.

The one field inside the 112 window that the walkers provably touch is `E(3)[+2]` at frame offset
**110 (0x6e)** - which is exactly walker A's `ldrh.w r6,[r4,#0xde]` at `0x0dc598`
(`M + 0x70 + 0x24*3 + 2 = M + 0xde`).

### 5.3 The containing per-band object `P` (used to select the element)

| P+off | width | the code | role |
|---|---|---|---|
| +0x2c4 | u8 | `ubfx([0x118b0+4*band+0x7ac]+0xc,0,4)` -> `strb.w [sl,#0x2c4]` (0xdcbc0-0xdcbda) | chain count/selector (0..15) |
| +0x2c5/+0x2c6/+0x2c7 | u8 | `ubfx(...,4,2)` / `...,7,1` / `...,6,1` | chain flags |
| +0xec | u8 | `ldrb.w [sl,#0xec]` (0xdcc04, 0xdcf0a) | chain/table index |
| +0xee | u8 | `ldrb.w sb,[sl,#0xee]; cmp sb,#1` (0xdcbe6) | branch selector: alternate init path (see §8) |
| +0xed | u8 | `ldrb.w ip,[r8,#0xed]; cmp ip,#0` (0xdd6dc) | guard / state |
| +0x108 | w | element pointer (0xdcd1a, 0xdce6a, 0xdd754) | selected chain element |
| +0xfc | w | lookup result (0xdd766) | chain table value |
| +0x320 | w | flag word `orr #0x1000; orr #1` (0xdcbba) | per-band busy/valid bits |
| +0x2d0 | u8 | index written (0xdd6fa) | derived chain index |
| +0x2e4 | w | blob pointer (0xdcf44) | config blob |

`P`'s stride is `0x35C`; `0x118B0` is the RAM base and `0x35C*band` selects the band. `0x35C` is
not related to the store's 112 or 48.

---

## 6. What the code does *not* do (explicit negatives)

1. **No 112-byte stride.** Across all three walkers, the only loop increments/strides are `0x24`
   (`0x0dcc44 sub.w ip,ip,#0x24`; `0x0dc676`/`0x0dc7e2 mla ...,#0x24,...`; `0x0dce5c smlabb ...,#0x24,...`),
   a `64` table stride (`r3,lsl #6`), and a `9`/`36` product. `0x70` is only ever a **constant add**
   (`0x0dcd0e add.w r3,r5,#0x70`; `0x0dce58 add.w r0,r5,#0x70`; `0x0dd73c adds r1,#0x70`; plus
   walker A's `add.w r1,r1,#0x70`), never a loop increment or a per-record period. This confirms
   `firmware-cali-parser.md` §3.1 and §5 item 2 independently from the raw bytes.
2. **No 45-58 record count.** Bounds are `cmp r3,#3` (A), `cmp r3,#3` (B scan, r3=8..4 => 5
   passes), `cmp fp,#9` (B rate), `cmp r2,#9` (C). None is 45-58; the only 45-58 constants in the
   image are message-opcode compares (phase 8 §3.2).
3. **Bases are RAM, not the `.kv` DMA buffer.** Walker A's base is the argument struct; B/C's base
   is `0x118B0 + 0x35C*band`, built with `movw #0x18b0; movt #1`. The `.kv` bytes are loaded by the
   driver (`hwifi_rf_cali_file_load_5g/_2g`) into a heap block and handed to firmware as a DMA/phys
   address (`cali-buffer-populator.md` §1/§3); no walker loads such a pointer, and `0x118B0` is a
   fixed compile-time RAM object, below the image's first runtime code address (`0x40000`), so it
   cannot be a byte offset into `FIRMWARE.bin` either.
4. **The element is control/state, not calibration payload.** Walker A *writes* `E[+6]=1` and
   `E[+0x18]=1`; B/C *compare* `E[+2]` and store derived pointers. There is no block copy of bytes
   from a `.kv`-shaped source into these elements.

---

## 7. Link to the store - the arithmetic, and why it does not bind

`kv-payload-check.md` establishes (reproducibly, from the two store files):

| store | payload bytes | long zero run | period |
|---|---|---|---|
| `wifi_cali_data.kv` (dual) | 8,904 | 58 B, at offsets 54, 166, 278 | **112 B** |
| `wifi_cali_data_2g.kv` (2g) | 2,328 | 26 B, at offsets 22, 70 | **48 B** |

If the walkers were the store parser, the address of record `r`, chain `i` would have to be

```
store_base + PERIOD * r + 0x24 * i + f          with PERIOD = 0x70 (112) or 0x30 (48)
```

and the outer loop would have to increment the pointer by `PERIOD`. In the code:

* `store_base` is **not** the `.kv` buffer pointer (it is `M` for walker A, `0x118B0+0x35C*band`
  for B/C). The `.kv` buffer lives at the driver's `oal_noncache_alloc` heap block; the firmware
  address for it is whatever the message/DMA carries, and none of the three walkers reads one.
* `PERIOD` appears as `0x70` only as a **constant base add**, and `0x30` does not appear at all;
  neither is ever used as a loop increment. The only record-like stride present is `0x35C` (the
  per-band object), not 112 or 48.
* The index range is 4 (A), 5/10 (B), 10 (C) - versus the dual store's ~80 periods and the 2g
  store's ~48 periods.

So there is **no offset in `wifi_cali_data*.kv` that can be assigned** to any walker access. The
coincidences are exactly two numbers, `0x70 = 112` and `0x24 = 36`; the *first* is a structural
base (constant add) and the *second* is a sub-element stride. **The store's 112-byte period is not
reproduced by these walkers, and the store's field order remains unassigned.**

If the parent wants a store parser, the search target is *not* these walkers: it must be the code
that receives the calibration message carrying the DMA address. `cali-handler-address.md` and
`opcode-dispatchers.md` §3.1 give the verified route into the calibration family: the alg
dispatcher at file `0x3243C` (table `0xC4CB8`, cfg ids `0x0DAD-0x0DC5`), with the calibration
handlers at file `0x32F6C` (cfg `0x0DAD`) and `0x32B88` (cfg `0x0DB2`). Those are the functions
that should be checked for a load of the DMA/cali pointer - that is a separate pass.

---

## 8. 2g versus dual-band, in code terms

* **Stores:** two files, two driver buffers - `wifi_cali_data.kv` (dual, 0x22D0 = 8912 B at
  `.bss+0x120`/`+0x128`) and `wifi_cali_data_2g.kv` (2g, 0x920 = 2336 B at `.bss+0x124`/`+0x12C`);
  the observed skeleton periods are 112 B vs 48 B (`cali-buffer-populator.md`,
  `kv-payload-check.md`).
* **Code:** there is **no separate 2g walker and dual walker.** All three functions take a **band
  index** (`r0` -> `r8`/`r5`) and select a per-band RAM object with `0x35C * band`
  (`0x0dcb88 mul r5,r5,r0`; `0x0dd6c0 mul r6,r6,r5`). Band is a *parameter*, not a code fork, and
  the code never sees the store's 112 or 48.
* **The one band-conditional branch** is walker B's `ldrb.w sb,[sl,#0xee]; cmp sb,#1;
  beq.w #0x0dcf0a` (`0x0dcbe6-0x0dcbf4`): when `P[0xee] == 1` it takes the alternate init path at
  `0x0dcf0a` instead of the chain-scan path. Walker C refuses to run if `P[0xed] != 0`
  (`0x0dd6dc-0x0dd6e4`). These are **per-band state gates**, not 2g-vs-5g data layouts.
* **Consequence:** the store's 2g/dual difference (48 vs 112) is a property of the *file producer*;
  it is not mirrored in the walker code, which is band-parameterised and uses 36/0x35C only.

---

## 9. Limits (explicit)

1. **No store field is named by this pass.** The walkers do not touch the `.kv` buffer (§6.3), so
   this document cannot map a single `.kv` byte to a field. The store layout stays open, matching
   `kv-payload-check.md`'s conclusion.
2. **The three walkers are state machines, not parsers.** Their element fields are flags/pointers
   selected by a chain index; the values written (`1`) and compared (config words) are control
   state. I did not identify the semantic meaning of `E[+2]`'s table (`0x13AA0 + 0x880` region) or
   of the `P` object; only its addressing.
3. **Walker B's `P[0x108]` blob is not resolved.** The element pointer carries a `+0x1199C` term
   (`literal @0x0dd0a0`), so `+0xa`, `+0xf6`, `+0x111` are offsets into a *different* RAM region
   whose base I did not identify. Consequently the `P+0x2e8..0x2ff` packed record (§3.5) is
   listed but not semantically decoded, and the 10-entry rate path is summarised, not
   instruction-by-instruction.
4. **Walker A is multi-exit.** It has several epilogues (`0x0dc51e`, `0x0dc57c`, the tail call at
   `0x0dc644`) and re-enters the element block at `0x0dc5ae` from the pointer build; I mapped the
   chain-element addressing and its field accesses, but not every message-flow edge of this
   ~1.1 KB function.
5. **Element layout is inferred from the addressing, not from a type definition.** `E[+2]` =
   u16 15-bit table index is strongly supported (walkers A and C both compute it, both mask
   `#0,#0xf`, both index the `0x880` table), and `E[+6]`/`E[+0x18]` = written flag bits is direct.
   `E[+0x0a]`'s role as a scale/index addend is direct in walker A. No other element offsets are
   read by any of the three walkers.
6. **The 112-byte "record" is not a code object.** Given §5.2, `0x70` in these functions is the
   base offset at which a 36-byte-stride sub-array begins; it is not a record size, and the
   store's 112-byte period should not be derived from this constant.
7. **Scope.** Everything here is from `FIRMWARE.bin` (both version copies identical). No device
   was touched; the `.kv` files are used only as the cited prior measurement, not re-measured here.

---

## 10. Verification (commands and excerpts)

* **Identity:** `md5sum build/tmp/FIRMWARE.bin build/versions/2.5.24/FIRMWARE.bin
  build/versions/2.4.15/FIRMWARE.bin` -> all `0e530b976d5a20e87358671f1a577695`; length `0xE2C98`.
* **Walkers located at file offsets (no bias applied):** prologue bytes `2d e9 f0 47` at
  `0xDC4EC` and `0xDD688`, `2d e9 f0 4f` at `0xDCB78`; the instruction at `0x0DC674` is
  `24 21 01 fb 03 41` = `movs r1,#0x24; mla r1,r1,r3,r4`, verbatim as in
  `firmware-cali-parser.md` §3.1 (whereas the `-0x40000` alternative `0x9C674` does not decode to
  it).
* **Disassembly:** capstone 5.0.7 `CS_ARCH_ARM/CS_MODE_THUMB`, windows `0xDC4EC-0xDC970`,
  `0xDCB78-0xDD0C0`, `0xDD688-0xDD7F0`, saved to `build/tmp/cali_dis.txt`; every excerpt above is
  copied from it.
* **Literals:** all four `ldr [pc,#imm]` in walker B resolve to `0x0dd0a0`, bytes
  `9c 19 01 00` = `0x0001199C`; walker B/C tail thunks read `0x00010518` (`0x0dd0d0`) and
  `0x00010540` (`0x0dd7fc`).
* **`0x118B0` is RAM:** built by `movw #0x18b0; movt #1` at `0x0dc5ae/0xdc5ba`,
  `0x0dcb80/0x0dcb92`, `0x0dd694/0x0dd6a0`; the phase-4 convention `runtime = file + 0x40000`
  (`opcode-dispatchers.md` §2.2) makes `0x118B0 - 0x40000` negative, so it is not a file offset.
* **No 112/48 stride:** exhaustive scan of the three windows for pointer increments; the only
  increments are `sub.w ip,ip,#0x24` (`0x0dcc44`), the `#0x24` mla/smlabb builds
  (`0x0dc676`, `0x0dc7e2`, `0x0dce5c`), and `lsl#6` table strides. `0x70`/`0x30` appear only as
  constants/literals (`0x0dcd0e`, `0x0dce58`, `0x0dd73c`, and walker A's `add.w r1,r1,#0x70`).
* **Store skeleton:** taken from `kv-payload-check.md` §"What does reproduce" (periods 112/48,
  zero-run starts 54/166/278 and 22/70); not re-measured in this pass.
