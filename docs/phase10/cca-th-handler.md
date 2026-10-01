# Phase 10 — firmware handler for `get_cca_th` (cfg_id `0x012e`)

Scope: local files, read-only, no device. Inputs: `build/versions/2.5.24/FIRMWARE.bin`
(928,920 B = `0xE2C98`, md5 `0e530b976d5a20e87358671f1a577695`, identical to `build/tmp/FIRMWARE.bin`)
and `build/versions/2.5.24/hi5622v100_wifi.ko` (3,564,728 B, ARM32 `ET_REL`, not stripped).
Tooling: `pyenv/Scripts/python.exe` + capstone 5.0.7 (`CS_ARCH_ARM`, `CS_MODE_THUMB`) + pyelftools.
Prior art: `phase6/message-fields.md` (§2.7, the capture), `phase8/opcode-dispatchers.md`
(the `0x3243C` dispatcher), `phase3/alg-commands.md` (driver cfg table), `phase6/firmware-symbols.md`.

**Bias note.** Addresses are **file offsets into FIRMWARE.bin** unless called *runtime*. The
firmware blob's runtime = file + `0x40000` (established in phase 4/8). The driver `.ko` is a
relocatable object: every section has `sh_addr = 0`, so a driver "address" below is
section-relative unless the file offset is named.

---

## 0. Headline

The handler for `cfg_id 0x012e` is **file `0xACC1C`** (runtime `0xECC1D`, Thumb). It is not
reached by the `0x3243C` dispatcher because that function is only **one row** of a larger
cfg-range dispatch table:

* There is a **34-row, 12-byte `{u16 lo, u16 hi, u32 a, u32 handler}` table at file
  `0xC6D64 … 0xC6EF0`** whose rows map a *cfg_id range* to a family handler.
* Row 22 (`0xC6E6C`) is `lo=0x0dac, hi=0x0dca → handler 0x3243C` — the equipment family.
  Its own code range check `cfg−0x0dad ≤ 0x1c` covers `0x0dad..0x0dc9 = lo+1 .. hi−1`.
* Row 29 (`0xC6EC0`) is `lo=0x012c, hi=0x0131 → handler 0xACC1C`, which brackets `0x012e`.

So "the other path" is the same alg-message cfg dispatch; `0x012e` simply falls in a different
row than the equipment family. The handler `0xACC1C` proves the compact 14-byte layout: it
**compares `ldrh[msg]` with `0x12e`** and under get writes three signed bytes to
`msg+0x0b/+0x0c/+0x0d` — exactly the three offsets in the live capture.

The three values `0xC2/0xC5/0xC8` (−62/−59/−56) **do exist in the image**, at file
`0x1D456..0x1D46C`, written in that order into a per-class descriptor at `+2/+3/+4`. Whether
that descriptor is the state the get path copies from is *inferred*; the emitted values are
runtime state (two live captures differ in the first value). Details in §3.

---

## 1. The search: every place `0x012e` appears in FIRMWARE.bin

Method: (a) byte scan for `2e 01 00 00` (u32 LE `0x0000012e`, a literal-pool word); (b) robust
per-2-byte Thumb decode of both code regions `(0x0,0xC0000)` and `(0xD8000,0xE2000)` keeping any
instruction whose operand is `#0x12e`; (c) byte scan for raw halfword `2e 01`.

### 1.1 Literal-pool words — none

```
$ python: d.find(b'\x2e\x01\x00\x00')
0xc939d  ...00011d8c 582e0100 00000000 84800000...
```
There is **exactly one** u32 LE `0x0000012e` in the whole image, at **`0xC939D`** — and it is
**not word-aligned**. `0xC939D` is the high half of the word `0x00012E58` stored at the aligned
address `0xC939C` (part of a data region of u32 "offset-like" values at `0xC9300+`). No 4-aligned
literal-pool word equals `0x0000012e`, so **no code loads `0x012e` from a literal pool**.

### 1.2 Instruction immediates — six, and only one is a dispatch

Robust per-2-byte sweep (both regions); the earlier phase-8 sweep missed `cmp.w` because it keyed
on the exact mnemonic `cmp`:

```
=== any instruction imm 0x12e in main ===
0x57c5e mov.w r1, #0x12e | 4ff49771
0x66862 mov.w r1, #0x12e | 4ff49771
0x6938c mov.w r1, #0x12e | 4ff49771
0x74b04 mov.w r1, #0x12e | 4ff49771
0x96f32 mov.w r1, #0x12e | 4ff49771
0xacc62 cmp.w r3, #0x12e | b3f5977f
=== any instruction imm 0x12e in blk2 ===   (none)
```

Containing function, by nearest preceding `push` prologue (heuristic, since the raw blob has no
symtab):

| site | containing function (nearest preceding prologue) | role |
|---|---|---|
| `0x57C5E` | `0x574F8` `push.w {r4-r8,sb,sl,fp,lr}` | `mov.w r1,#0x12e` is a **log line id** (r1) passed to `bl 0x1F90` |
| `0x66862` | `0x666D4` `push.w {r4-r8,sb,sl,fp,lr}` | log id (r1) to `bl 0x2C28` |
| `0x6938C` | `0x69364` `push {r0,r1,r4,r5,r6,lr}` | log id (r1) to `bl 0x2C28` |
| `0x74B04` | not bounded by a `push` within 0x800 B (start earlier) | log id (r1=`#0x12e`) to `bl 0x1F90`, arg r2 |
| `0x96F32` | `0x96ED0` `push.w {r0,r1,r2,r4-r8,sb,lr}` | log id (r1) to `bl 0x1C70` |
| **`0xACC62`** | **`0xACC1C` `push {r0,r1,r2,r4,r5,r6,r7,lr}`** | **`cmp.w r3, #0x12e` on `ldrh[msg]` — the cfg dispatch** |

Excerpts proving the five non-dispatch sites are log ids (r1 = line number, r0 = module base,
return value discarded):

```
0x57c5c ldr   r3, [sp, #0x18]     0x6685a ldr   r0, [pc, #0x12c]   0x6938c mov.w r1, #0x12e
0x57c5e mov.w r1, #0x12e          0x66862 mov.w r1, #0x12e        0x69390 ldr   r0, [pc, #0x38]
0x57c62 ldrh  r2, [r3, #0x16]     0x66866 orrs  r0, r3            0x69392 orrs  r0, r3
0x57c64 ldr   r0, [pc, #0x3c]     0x66868 bl    #0x2c28           0x69394 add   sp, #8
0x57c66 bl    #0x1f90             0x6686c mov   r2, r4            0x6939a b.w   #0x2c28
```

### 1.3 Raw halfword `2e 01` — 35 hits, all coincidental

A byte scan for `2e 01` returns 35 offsets. Decoding 2 bytes before/after each shows **none** is a
Thumb `movw/mov/cmp` immediate `#0x12e` and none is a cfg table key (e.g. `0x2DF18` is the second
half of `movw r1, #0x82e` at `0x2DF16`; `0xDA655/0xDA769/0xE1BFD` are inside the
`0xDA0FC`-family RAM-pointer data). They are not reproduced here as they carry no cfg binding.

### 1.4 The one structured appearance — the dispatch table row

```
0xC6EC0: 2c 01 31 01 40 00 00 00 1d cc 0e 00   ->  {lo=0x012c, hi=0x0131, a=0x40, handler=0x0ECC1D}
```
That is the only place `0x012e`-adjacent data forms a dispatchable record; §2 decodes the table.

---

## 2. Best candidate handler and its disassembly walk

### 2.1 The 12-byte cfg-range table at file `0xC6D64`

Parse: for `R = 0xC6D64 + 12*k`, `lo = u16[R]`, `hi = u16[R+2]`, `a = u32[R+4]`,
`handler = u32[R+8]`; handler file = `(handler & ~1) − 0x40000`.

```
R=0xc6d64 lo=0x0190 hi=0x01ac a=0x1c handler_file=0x2b990
R=0xc6d70 lo=0x02bc hi=0x02c0 a=0x7a handler_file=0xac7e8
...
R=0xc6e48 lo=0x0c80 hi=0x0c84 a=0x0e handler_file=0x95c2c
R=0xc6e54 lo=0x0d48 hi=0x0d50 a=0x65 handler_file=0x2b8e4
R=0xc6e60 lo=0x0c1c hi=0x0c2a a=0x0e handler_file=0xa74bc
R=0xc6e6c lo=0x0dac hi=0x0dca a=0x0f handler_file=0x3243c   <-- equipment family (the phase-8 "dispatcher")
R=0xc6e78 lo=0x0ed8 hi=0x0edb a=0x75 handler_file=0x179b8
R=0xc6e84 lo=0x1036 hi=0x103a a=0x7a handler_file=0x95a6a
...
R=0xc6ec0 lo=0x012c hi=0x0131 a=0x40 handler_file=0xacc1c   <-- brackets 0x012e
R=0xc6ecc lo=0x06a4 hi=0x06ab a=0x59 handler_file=0xd9cc
R=0xc6ed8 lo=0x10cc hi=0x10cf a=0x17 handler_file=0xa394c
R=0xc6ee4 lo=0x10d0 hi=0x10d4 a=0x1c handler_file=0x67884
R=0xc6ef0 lo=0x1130 hi=0x1132 a=0x7a handler_file=0x5ab9c
```
(34 rows, `0xC6D64..0xC6EF0`; run ends at `0xC6EFC` where the handler word stops being a code
pointer. Full list generated by the scan in the Verification section.)

**Convention `handler covers lo+1 .. hi−1` — verified against the handlers' own range checks:**

| row | lo..hi | handler | handler's internal range check (excerpt) |
|---|---|---|---|
| `0xC6E6C` | `0x0dac..0x0dca` | `0x3243C` | `0x3246C subw r3,r2,#0xdad; 0x32470 cmp r3,#0x1c` → `0x0dad..0x0dc9` |
| `0xC6E60` | `0x0c1c..0x0c2a` | `0xA74BC` | `0xA74E4 subw r4,r2,#0xc1d; 0xA74EA cmp r3,#0xc` → `0xc1d..0xc29` |
| `0xC6E48` | `0x0c80..0x0c84` | `0x95C2C` | `0x95C4A movw r4,#0xc82 … 0x95C54 movw r4,#0xc83 … 0x95C5E movw r4,#0xc81` → `0xc81..0xc83` |

So a row `lo=0x012c, hi=0x0131` covers `0x012d..0x0130`, which **includes `0x012e`**. This is a
direct structural binding, not a name match.

### 2.2 The handler `0xACC1C` (runtime `0xECC1D`) — verbatim capstone walk

```
0xacc1c  push     {r0, r1, r2, r4, r5, r6, r7, lr}
0xacc1e  mov      r4, r1                 ; r4 = msg (payload)
0xacc20  mov      r6, r0                 ; r6 = dev
0xacc22  cbz      r0, #0xacc26
0xacc24  cbnz     r1, #0xacc44
0xacc26  clz r3,r4 ; clz r2,r6 ; lsrs ...   ; null-arg log path
0xacc32  movw     r1, #0x31d
0xacc38  bl       #0x1df4
0xacc3c  movs     r5, #0x64              ; return 100 on bad args
0xacc42  pop      {r4, r5, r6, r7, pc}
0xacc44  ldr.w    r7, [r0, #0x4b8]       ; dev->priv
0xacc48  cbnz     r7, #0xacc60
...
0xacc60  ldrh     r3, [r1]               ; cfg_id = *(u16*)msg
0xacc62  cmp.w    r3, #0x12e             ; *** cfg == 0x12e ? ***
0xacc66  bne      #0xaccd2               ; no  -> return 0x8b2d (FAIL)
0xacc68  movs     r3, #0
0xacc6a  add      r2, sp, #8
0xacc6c  str      r3, [r2, #-0x4]!
0xacc70  movs     r1, #0xb              ; index 0xb into the per-class descriptor table
0xacc72  mov      r0, r7
0xacc74  bl       #0x96bac              ; returns descriptor ptr, stored via [sp+4]
0xacc78  mov      r5, r0
0xacc7a  cbz      r0, #0xacc92
...
; ---- state load (common to both directions) ----
0xacc92  add.w    r0, r7, #0x1a00
0xacc96  ldr      r3, [r0]               ; r3 = *(priv + 0x1a00)
0xacc98  ldrb     r1, [r4, #2]           ; dir = msg[2]  (1 = get, 0 = set)
0xacc9a  ldr.w    r3, [r3, #0x2d8]
0xacc9e  ldr.w    r2, [r3, #0x29c]       ; ED word A
0xacca2  ldr.w    r3, [r3, #0x2a0]       ; ED word B (two threshold bytes)
0xacca6  sxtb     r2, r2                 ; thr1 = signed byte0 of +0x29c
0xacca8  sxtb.w   ip, r3                 ; thr2 = signed byte0 of +0x2a0
0xaccac  ubfx     r3, r3, #8, #8
0xaccb0  sxtb.w   lr, r3                 ; thr3 = signed byte1 of +0x2a0
0xaccb4  cbnz     r1, #0xacd04           ; dir != 0  -> GET path
; ---- SET path (dir == 0) : read msg+0xb/c/d into descriptor+2/3/4, defaulting to state ----
0xaccb6  ldr      r7, [sp, #4]
0xaccb8  ldrb     r3, [r7]
0xaccba  lsls     r3, r3, #0x1f
0xaccbc  bpl      #0xaccd8
...
0xaccd8  ldrsb.w  r1, [r4, #0xb]         ; msg[0xb]
0xaccdc  ldrsb.w  r3, [r4, #0xd]         ; msg[0xd]
0xacce0  cmp      r1, #0
0xacce2  it eq ; 0xacce4 moveq r1, r2     ; default thr1 if zero
0xacce6  ldrsb.w  r2, [r4, #0xc]         ; msg[0xc]
0xaccea  cmp      r3, #0
0xaccec  it eq ; 0xaccee moveq r3, lr     ; default thr3 if zero
0xaccf0  cmp      r2, #0
0xaccf2  it eq ; 0xaccf4 moveq r2, ip     ; default thr2 if zero
0xaccf6  strb     r1, [r7, #2]           ; descriptor+2 = thr1
0xaccf8  strb     r2, [r7, #3]           ; descriptor+3 = thr2
0xaccfa  strb     r3, [r7, #4]           ; descriptor+4 = thr3
0xaccfc  ldr      r0, [r0]
0xaccfe  bl       #0x7e524               ; apply
; ---- GET path (dir != 0) : emit the three thresholds into the wire response ----
0xacd04  strb     r2, [r4, #0xb]         ; *** msg+0x0b = thr1 (signed) ***
0xacd06  strb.w   ip, [r4, #0xc]         ; *** msg+0x0c = thr2 ***
0xacd0a  strb     r3, [r4, #0xd]         ; *** msg+0x0d = thr3 ***
0xacd0e  nop
0xaccd2  movw     r5, #0x8b2d            ; FAIL return for non-0x12e
```

**Response construction (proven):** for a *get* (`msg[2] != 0`) the handler writes the three
signed threshold bytes directly at `msg+0x0b`, `msg+0x0c`, `msg+0x0d` (`0xACD04/06/0A`). This is
byte-for-byte the layout measured live in `phase6/message-fields.md` §2.7 (`+0x0b=0xc2`,
`+0x0c=0xc5`, `+0x0d=0xc8`). The remaining 11 bytes of the 14-byte response (`+0x00` id word
`0x0d01012e`, `+0x04=0`, `+0x08=1`) are set by the transport/dispatch framework, not by this
handler.

**Where the numbers come from (proven):** the get path copies from runtime state, not from image
constants —
`r2/ip/r3 = sxtb( *(*(*(priv+0x4b8)+0x1a00)+0x2d8) + 0x29c )`, `sxtb( …+0x2a0 ).b0`,
`sxtb( …+0x2a0 ).b1`. The helper at `0x96BAC` (`addw r2,r1,#0x66c; add.w r0,r0,r2,lsl#2;
ldr r0,[r0,#4]`) returns the *index-`r1` descriptor* into `*(r3)`; `r1=0xb` selects class-11
(the compact ED-threshold descriptor).

### 2.3 The top-level walker of the `0xC6D64` table — NOT located

Every candidate has been excluded: **no literal-pool word equals the table runtime base**
(`0x10C6D64` or `0x10C6D5C/0x10C6D50/0x10C6D68`), no `movw/movt` builds `0x10C6Dxx`, no `u32`
in the image equals `0x0C6D64` (file-offset form), no `bl` targets `0xACC1C`, and the linear
`ldr rN,[rM,#8]; blx rN` pattern search found nothing. So the **call edge** from the walker into
`0xACC1C` is **inferred** from the range table (§2.1) rather than read from a located call site.
(The `0x96068` function *is* a table walker, but over an 8-byte `{u16 key @+0x1b0, handler
@+0x1b4}` SMAC-processor table, not the 12-byte cfg table.)

---

## 3. Cross-check with the live capture (−62/−59/−56)

Live facts (`phase6/message-fields.md` §2.7, device `Hisilicon0`):
`alg get_cca_th` → `ed_high_20th = [-62], ed_high_40th = [-59], ed_high_80th = [-56]`, response
`+0x0b=0xC2`, `+0x0c=0xC5`, `+0x0d=0xC8`. A second capture (`phase3/alg-commands.md` probe #177,
`vap0`) gives `ed_high_20th = [-57], ed_high_40th = [-59], ed_high_80th = [-56]`.

### 3.1 Constants that yield `−62/−59/−56` **do** exist in the image

```
0x1d456  movs r0, #0xc5
0x1d458  orr  r2, r2, #2
0x1d45c  strb.w r2, [r3, #0x134]
0x1d460  movs r1, #0xc8
0x1d462  movs r2, #0xc2
0x1d464  mov.w ip, #1
0x1d468  strb r2, [r6, #2]      ; 0xC2 -> descriptor+2
0x1d46a  strb r0, [r6, #3]      ; 0xC5 -> descriptor+3
0x1d46c  strb r1, [r6, #4]      ; 0xC8 -> descriptor+4
```
`0xC2/0xC5/0xC8` are written **in exactly the response order** into a 3-byte buffer at
`+2/+3/+4` — the *same* `+2/+3/+4` slot that `0xACC1C`'s set path writes and the class-11
descriptor exposes (`0x96BAC`, `r1=0xb`). A whole-image scan shows this is the **only** place
where `#0xc2`, `#0xc5`, `#0xc8` immediates cluster in one function.

### 3.2 What is proven vs inferred

* **Proven (image):** `0xACC1C` compares `ldrh[msg] == 0x12e` (`0xACC62`) and, on get, writes three
  signed bytes to `msg+0xb/0xc/0xd` (`0xACD04/06/0A`). The response offsets match the capture
  exactly.
* **Proven (image):** the constants `0xC2/0xC5/0xC8` (= −62/−59/−56) exist at `0x1D456..0x1D46C`
  and are stored into a 3-byte descriptor at `+2/+3/+4` in that order.
* **Proven (live):** the emitted values are **runtime state**, not unconditional constants — the
  same command returned `−62/−59/−56` on `Hisilicon0` and `−57/−59/−56` on `vap0`. The get path
  copies from RAM (`…+0x29c`, `…+0x2a0`), so whatever is stored there is what ships.
* **Inferred:** that `0x1D456` is the *default initializer* for the exact descriptor/state the get
  path reads. The values and slot offsets match, but the two structures use different offsets
  (`descriptor+2/3/4` vs state `+0x29c/+0x2a0`), and no write to `+0x29c/+0x2a0` with these
  constants was located in the timebox, so the link is by value/order, not by a traced store.

**Answer to the cross-check:** the handler *would* emit whatever `+0x29c/+0x2a0` hold. The image
does contain arithmetic-free constants `−62/−59/−56` in the correct order (`0x1D456`), so the
`Hisilicon0` capture is consistent with those defaults still being in effect; the `−57` in the
other capture shows the first slot had been adjusted at runtime. The middle/third slots
(`−59/−56`) were stable in both captures, consistent with them being the untouched defaults.

---

## 4. Host-side path: how the driver's `get_cca_th` reaches the firmware

### 4.1 Driver-side binding (from `phase3/alg-commands.md`, re-checked here)

The driver `.data` cfg table is 414 × 12 B at `st_value = 0x35c8` with layout
`{char* name (+0), u16 cfg_id (+4), u8 dir (+6), u8 pad (+7), u32 flags (+8)}`. Entries 397/398:

```
entry 397 file 0x248314 cfg 302 dir 0 flags 0x1010000   (set_cca_th)
entry 398 file 0x248320 cfg 302 dir 1 flags 0x1010000   (get_cca_th)
```
`302 = 0x12E`, `dir=1` = get. So the driver's `get_cca_th` is uniquely keyed by `(cfg_id=0x012e,
dir=1)`.

### 4.2 Driver-side output handler — `alg_cca_config_param_output_entry` (`0x815F0`, A32)

```
0x815f0 push {r4,r5,r6,r7,lr} ; subs r4,r2,#0 ; sub sp,sp,#0xc
0x81600 ldrh   r2, [r1]            ; cfg_id from the response
0x81608 movw   r3, #0x12e
0x8160c cmp    r2, r3
0x81610 beq    #0x81624            ; != 0x12e -> return 0x8b2d
0x81624 ldrb   r6, [r1, #2]        ; dir
0x81628 ldrsb  r3, [r1, #0xb]      ; thr1 (signed)
0x8162c ldrsb  r2, [r1, #0xc]      ; thr2
0x81634 ldrsb  r0, [r1, #0xd]      ; thr3
```
The `ldrsb` triple at `+0xb/+0xc/+0xd` is decoded with the format strings
`.rodata.str1.4` `0x23d934 "ed_high_20th = [%d], ed_high_40th = [%d], ed_high_80th = [%d]"` —
exactly the observed console line. So the **driver does not compute the thresholds and does not
pass them on the request**; it only formats firmware bytes at `+0xb/c/d`.

### 4.3 The path, end to end

1. Userspace `iwpriv <wlan> alg get_cca_th` → wireless-extensions private handler
   `wal_hipriv_alg_cfg` (`hi5622v100_wifi.ko .text 0x14881c`), ioctl cmd `0x0101`
   (`g_st_iw_handler_def`/`g_ast_iw_priv_args`, phase 2).
2. The driver looks up `(name="get_cca_th")` in the 414-entry `.data+0x35c8` table →
   `cfg_id = 0x012e, dir = 1`. It builds a **14-byte** payload whose first word is
   `0x0d01012e` (`0x0d01` = get tag; `len=0x0e`, header `m14 = 0x0e0101`) and sends the alg event
   (`hmac_config_alg_send_event`; trace in `phase6/message-fields.md`).
3. Firmware alg cfg dispatch: `ldrh[msg] = 0x012e` is matched against the **12-byte range table at
   file `0xC6D64`** (§2.1); row `0xC6EC0` (`lo=0x012c, hi=0x0131`) selects **`0xACC1C`**.
4. `0xACC1C` (`dir != 0` → `0xACD04/06/0A`) writes the three signed thresholds into
   `msg+0x0b/0x0c/0x0d`; the framework returns the 14-byte payload.
5. The driver's output callback `alg_cca_config_param_output_entry` (`0x815F0`) reads
   `+0xb/+0xc/+0xd` and prints the line.

**Why `0x3243C` "is out of range" is not a contradiction.** `0x3243C` is *itself* row 22 of the
same `0xC6D64` table (`lo=0x0dac, hi=0x0dca`). The phase-8 result ("the alg dispatcher at `0x3243C`
covers `0x0dad..0x0dc5`") is correct but describes only the equipment-family row; the table's
other 33 rows cover the other cfg families, and `0x012e` lands in row 29.

**Unresolved within this path:** the code that *walks* `0xC6D64` (the call site of `0xACC1C`) was
not located statically (§2.3); the binding is via the range table, not a traced call.

---

## 5. Explicit limits

1. **The `0xC6D64` table walker was not located.** No literal/`movw-movt` references the table
   base, no `bl` targets `0xACC1C`, and a `ldr rN,[rM,#8]; blx rN` search found nothing. The
   `lo..hi`→handler binding is proven by the table contents and by three handlers' own range
   checks; the *call edge* is inferred.
2. **The table's `a` field is unexplained.** It is neither the cfg range nor (for `0xACC1C`,
   `a=0x40`) the 14-byte response length. Its semantics were not determined.
3. **The default-init link is inferred by value/order only.** `0x1D456` writes `0xC2/0xC5/0xC8`
   into a `+2/+3/+4` descriptor; the get path reads a different structure at `+0x29c/+0x2a0`.
   No store of these constants to `+0x29c/+0x2a0` was located. The `−57` capture proves the
   emitted value is runtime state regardless.
4. **Response bytes `+0x00..+0x0a` are not attributed to this handler.** Only `+0x0b/+0x0c/+0x0d`
   are written by `0xACC1C`; the id word and `+0x08=1` come from the transport/framework, whose
   exact fill site was not traced.
5. **`0x96BAC` semantics are described structurally only.** It returns
   `*(u32*)( base + (idx+0x66c)*4 + 4 )`; that `idx=0xb` is the compact ED descriptor is inferred
   from the `0xACC1C` set path, not proven by a symbol.
6. **Containing functions in §1.2 are heuristic.** The image has no symtab, so "nearest preceding
   `push`" can mis-attribute a site in a large or data-adjacent function; `0x74B04` was not
   bounded at all. This does not affect the dispatch conclusion (the five non-`0xACC1C` sites are
   log-id `mov.w r1`, not branches).
7. **No device access.** All of the above is static; the runtime-state contents of
   `+0x29c/+0x2a0` were not read (that is the phase-7 RAM-dump scope).

---

## Verification

Every claim is backed by a scan or excerpt run this session (`pyenv/Scripts/python.exe`, capstone
5.0.7 Thumb; FIRMWARE.bin md5 `0e530b976d5a20e87358671f1a577695`).

* **§1.1 literal scan** — `d.find(b'\x2e\x01\x00\x00')` → one hit `0xC939D`; context
  `…00011d8c 582e0100 00000000 84800000…` shows it is the high half of `0x00012E58` at `0xC939C`.
* **§1.2 immediate sweep** — per-2-byte decode of `(0x0,0xC0000)` and `(0xD8000,0xE2000)`,
  operand `#0x12e`: six sites, listed with bytes; five are `mov.w r1,#0x12e` into log calls
  (`bl 0x1F90/0x2C28/0x1C70`), one is `cmp.w r3,#0x12e` at `0xACC62`.
* **§1.2 containing functions** — nearest preceding `push` prologue per site.
* **§2.1 table parse** — `struct.unpack_from('<HHII', …)` at `0xC6D64 + 12k`, `k=0..33`; full list
  printed; row `0xC6EC0` = `{0x012c,0x0131,0x40,0x0ECC1D}`; row `0xC6E6C` = `{0x0dac,0x0dca,0xf,
  0x07243D}`. Convention cross-checked with `0x3243C` (`subw #0xdad; cmp #0x1c`), `0xA74BC`
  (`subw #0xc1d; cmp #0xc`), `0x95C2C` (`movw #0xc81/0xc82/0xc83`).
* **§2.2 handler walk** — verbatim capstone window `0xACC1C..0xACD0E`; `0x96BAC` window
  `0x96BAC..0x96C40`.
* **§2.3 negatives** — u32 scans for `0x10C6D64/0x10C6D5C/0x10C6D50/0x10C6D68` and
  `0xC6D64/0xC6D5C/0xC6D50/0xC6D68/0xC6D4C` → none; `movw #0x6d64/0x6d5c/0x6d4c/0x6d70` → none;
  no `bl` to `0xACC1C`; `ldr rN,[rM,#8]; blx rN` → none.
* **§3.1 constants** — whole-main-region sweep for `#0xc2/#0xc5/#0xc8` immediates clustered within
  0x80 B → single cluster `0x1D456/0x1D460/0x1D462`, window `0x1D440..0x1D46C` verbatim.
* **§4.1 driver table** — pyelftools `.data` at `sh_offset + 0x35c8 + i*12`; entries 397/398 have
  `cfg=302`, `dir=0/1`, `flags=0x01010000` (matches `phase3/alg-commands.md` §table rows 397/398).
* **§4.2 driver handler** — verbatim capstone A32 window `0x815F0+0x38`; strings
  `.rodata.str1.4` `0x23d934` `"ed_high_20th = [%d]…"`.
* **§3/§4 live facts** — `phase6/message-fields.md` §2.7 (`−62/−59/−56`, `+0x0b/c/d = c2/c5/c8`)
  and `phase3/alg-commands.md` probe #177 (`−57/−59/−56`).

**Bottom line.** `cfg_id 0x012e` is handled by firmware file `0xACC1C`, selected by the 12-byte
range table at file `0xC6D64` (row `0xC6EC0`, `lo=0x012c..hi=0x0131`). The handler proves the
14-byte response layout by writing three signed thresholds to `msg+0x0b/0x0c/0x0d`, matching the
live capture. `0x3243C` is row 22 of the same table; there is no separate dispatcher. The values
`−62/−59/−56` appear in the image as `0xC2/0xC5/0xC8` at `0x1D456` but the emitted values are
runtime state (a second capture gives `−57/−59/−56`).
