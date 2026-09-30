# Firmware-side parser of the calibration buffer: constant-fingerprint search (phase 8, 2026-09-30)

Scope: local files, read-only. Input: `build/tmp/FIRMWARE.bin` (928,920 B = `0xE2C98`). Tooling:
`pyenv/Scripts/python.exe`, capstone 5.0.7 (`CS_ARCH_ARM`, `CS_MODE_THUMB`). Prior art used:
`ulw/phase8/cali-buffer-populator.md`, `ulw/phase8/kv-payload-check.md`,
`ulw/phase7/firmware-callgraph.md`, `ulw/phase6/firmware-symbols.md`, `ulw/phase2/alg-dispatch.md`.

**Bias note.** Every address in this document is a **file offset into FIRMWARE.bin** (the Thumb code
regions `0x0-0xBFFFF` and `0xD8000-0xE1FFF`). The `-0x40000` bias from phase 4 applies only to the
`{code_addr, name_ptr}` symbol tables (table address `= file offset + 0x40000`); it is **not** applied
to the offsets below.

**Headline.** The 112/48 immediate-constant fingerprint does **not** isolate a parser. `0x30`, `0x60`,
`0x70`, `0x90`, `0xC0` are among the most common immediates in the image (stack frames, struct
offsets, MMIO init): a bounded sweep of 2,768 functions found 265 of them carrying at least one
skeleton constant and 20 carrying three or more. No function uses `0x150` or `0x1C0` as a *stride*,
none uses `0x230` as a loop increment, and no function pairs a skeleton stride with a 45-58 loop
counter. The only structurally interesting hits are three per-chain table walkers in the second code
block (`0xDC4EC`, `0xDCB78`, `0xDD688`) that build a pointer as `0x24*idx + 0x70` - which numerically
matches the store's 36-byte sub-stride, but they iterate **4** chains, not 45-58 records, and operate
on a firmware struct, not on a DMA'd file buffer. Details, disassembly and the explicit negatives
follow.

---

## 1. Search method (exact)

Fingerprint set (from the task premise: multiples of 112 and 48):

```
112 x1..x5 : 0x70, 0xE0, 0x150, 0x1C0, 0x230
 48 x1..x4 : 0x30, 0x60, 0x90, 0xC0
```

Two sweeps were run over the two Thumb-2 code regions `(0x0, 0xC0000)` and `(0xD8000, 0xE2000)`:

1. **Whole-region linear sweep** - `Cs(CS_ARCH_ARM, CS_MODE_THUMB).disasm(d[a:b], a)`, parse every
   `#imm` token with regex `#(-?0x[0-9a-f]+|-?\d+)`, drop branches and `[pc, #...]` literal-pool
   loads. Result: **69 hits** (68 in `0xD8000-0xE1FFF`, 1 in `0x0-0xBFFFF`).
2. **Bounded function sweep (primary)** - segment the image on Thumb prologue bytes
   (`2d e9` = `push.w`, or high byte `0xB4`/`0xB5` = 16-bit `push`), then disassemble each function
   from its start until the first epilogue (`pop {..., pc}` or `bx lr`) or 0x400 bytes. Result:
   **2,768 functions; 265 with skeleton hits; 527 hits total** (471 in the main block, 56 in the
   second block).

The two sweeps disagree because the whole-region linear decoder walks through the main block's
embedded data/jump tables and mis-consumes the byte stream after the first desync, whereas the
bounded sweep restarts at every prologue. The bounded sweep is treated as primary; the linear sweep
is quoted only as a cross-check. Scan (primary sweep):

```python
import capstone, re, collections
d = open('FIRMWARE.bin','rb').read()
md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
SKEL = {0x70,0xE0,0x150,0x1C0,0x230, 0x30,0x60,0x90,0xC0}
pat  = re.compile(r'#(-?0x[0-9a-fA-F]+|-?\d+)')
st = sorted(set(o for o in range(0,0xE2000-4,2)
                if d[o:o+2]==b'\x2d\xe9' or d[o+1] in (0xB4,0xB5)))
def body(f):                                   # one function, stop at first epilogue
    n = 0
    for ins in md.disasm(d[f:f+0x600], f):
        yield ins; n += ins.size
        if (ins.mnemonic.startswith('pop') and 'pc' in ins.op_str) or \
           (ins.mnemonic=='bx' and 'lr' in ins.op_str): return
        if n > 0x400: return
res = {}
for f in st:
    hits = []
    for ins in body(f):
        if ins.mnemonic in ('b','bl','blx','cbz','cbnz') or ins.mnemonic.startswith('b.'): continue
        if '[pc' in ins.op_str: continue
        for m in pat.findall(ins.op_str):
            v = int(m,0)
            if v in SKEL: hits.append((ins.address,v,ins.mnemonic,ins.op_str))
    if hits: res[f] = hits
```

Census by value across the primary sweep (hits where the constant is a **code/struct immediate**, not
a branch target and not a `[pc]` literal):

| value | 112/48 multiple | hits |
|---|---|---:|
| `0x30` | 48x1 | 160 |
| `0x60` | 48x2 | 64 |
| `0x70` | 112x1 | 78 |
| `0x90` | 48x3 | 72 |
| `0xC0` | 48x4 | 73 |
| `0xE0` | 112x2 | 30 |
| `0x150` | 112x3 | 37 |
| `0x1C0` | 112x4 | 6 |
| `0x230` | 112x5 | 7 |

(sum = 527; the sites are `ldr/ldr.w/ldrh/ldrd/str/strh/str.w/strd` offsets, `movs/mov.w/movw`
immediates, `add/add.w/sub/sub.w` immediates, `cmp` immediates, and `and/orr/bic/tst` masks.) The
2,768-function distribution shows the fingerprint is not selective: `0x30` alone appears 160 times,
almost always as a stack slot or struct field, and only **20** functions carry three or more distinct
skeleton values (6 carry four or more).

---

## 2. Candidate functions, ranked by distinct skeleton constants

Ranking key = number of **distinct** skeleton values in the function (the count of skeleton
immediate sites is shown as `n`). All offsets are file offsets; the excerpts are the instructions that
produced the hits, plus enough context to identify the construct.

### 2.1 Main block `0x0-0xBFFFF`

| rank | function | distinct | n | values | construct |
|---:|---|---:|---:|---|---|
| 1 | `0x96144` | 5 | 7 | `30 60 70 90 C0` | reads a local struct at `sp+0x30` / `sp+0x70` |
| 2 | `0x196e0` | 5 | 6 | `60 70 150 1C0 230` | bulk struct-clear (stores `r6=0` to many fields) |
| 3 | `0xee8` | 5 | 5 | `30 60 70 C0 150` | register-block/config init |
| 4 | `0x6970` | 4 | 7 | `30 60 90 150` | mixed field ops |
| 5 | `0x1bdd8` | 4 | 6 | `30 60 70 1C0` | mixed field ops |
| 6 | `0xb3fbc` | 4 | 4 | `60 C0 150 1C0` | field stores |
| 7 | `0x3071c` | 3 | 9 | `30 60 70` | repeated `ldrh.w r3,[r4,#0x70]` + `ldrd [ip,#0x30]` |
| 8 | `0x4ca1c` | 3 | 6 | `90 C0 E0` | `and`-mask extraction |
| 9 | `0x824d2` / `0x824de` | 3 | 6 | `30 60 C0` | `ldrh.w r3,[r3,#0xC0]` reads + `adds r0,#0x60` |
| 10 | `0x9d984` | 3 | 6 | `30 60 90` | stack slots only |
| 11 | `0x986c` | 3 | 5 | `30 90 C0` | `add.w r7,r4,#0x90`; `ldr.w r2,[r4,#0xC0]` |
| 12 | `0x493a2` | 3 | 5 | `30 60 150` | stack slots + `str.w r1,[r3,#0x150]` |
| 13 | `0x67234` | 3 | 5 | `60 70 C0` | stack buffer + `and r2,r2,#0x70` |

Representative disassembly:

`0x196e0` (rank 2) - a struct-clearing init; the "112-multiples" are just field offsets:

```
0x01977c: str.w  r6, [r4, #0x150]
0x0197bc: str.w  r6, [r4, #0x230]
0x0198ae: str    r6, [r4, #0x60]
0x01990c: str.w  r6, [r4, #0x1c0]
0x0199e4: mov.w  r3, #0x230
0x019a4a: movs   r0, #0x70
```
(head: `push.w {r4-r8,sb,sl,fp,lr}; ldr r5,[pc,#0x32c]; sub sp,#0x3c`; body stores `r6` (=0) to
offsets `0x74,0xac,0xd4,0x13c,0x140,0x14c,0x150,0x158,0x15c,0x164,0x16c,0x174,0x178,0x19c,0x1e4,
0x200,0x230,0x24c,0x2d4,0x334,0x414` (among others) of one base register - a big object clear, not a
record walk.)

`0xee8` (rank 3) - MMIO/config init; `r3` is advanced then used with negative offsets:

```
0x000fba: adds     r3, #0x30
0x000fbc: str      r2, [r3, #-0x30]
0x000fc4: str      r2, [r3, #-0x24]
0x000fec: str      r2, [r3, #0x60]
0x000ffe: str.w    r2, [r3, #0xc0]
0x001014: ldr.w    r2, [r4, #0x150]
0x001082: add.w    r3, r3, #0x70
```

`0x96144` (rank 1) - every hit is stack-relative (`r5 = sp+0x70`, `r4 = sp+0x30`), i.e. a local
buffer, not the DMA target:

```
0x09614c: add      r5, sp, #0x70
0x096152: movs     r2, #0xc0
0x09615e: add      r4, sp, #0x30
0x09622c: ldr      r2, [r5, #0x30]
0x09623e: ldr      r2, [r5, #0x60]
0x096250: ldr.w    r2, [r5, #0x90]
0x096300: ldr      r2, [r4, #0x30]
```

### 2.2 Second block `0xD8000-0xE1FFF` (the code block nearest the symbol tables)

Primary sweep (first-epilogue windows): 29 functions carry skeleton immediates. This table lists every
one with two or more distinct values.

| rank | function(s) | distinct | n | values | construct |
|---:|---|---:|---:|---|---|
| 1 | `0xda50c` + `0xda528` + `0xda544` (overlapping prologues; hits at `0xda5b2`,`0xda6ca`,`0xda7da`,`0xda81e`) | 2 | 4 | `60 90` | `cmp r2,#0x90` bounds checks + `ldr r2,[r3,#0x60]` |
| 2 | `0xdbdd0` (twin `0xdbec0`) | 2 | 4 | `90 C0` | `cmp r2,#0x90`; `sub sp,#0xc0` / `add sp,#0xc0` |
| 3 | `0xdcb78` | 2 | 3 | `60 70` | the `0x24*idx + 0x70` walker (§3.1) |
| 4 | `0xda65c` | 2 | 3 | `60 90` | same `cmp #0x90` family |
| 5 | `0xda770` | 2 | 2 | `60 90` | `cmp r2,#0x90` + `ldr r2,[r3,#0x60]` |
| 6 | `0xdd688` | 2 | 2 | `60 70` | the `0x24*idx + 0x70` walker (§3.1) |
| 7 | `0xe0a7a` (first-epilogue window) | 1 | 6 | `70` | `str/ldr.w [sl,#0x70]` struct field |
| 8 | `0xd8dd0`, `0xdbfb0` | 1 | 2 | `30` / `C0` | `ldr r3,[r5,#0x30]`; `sub sp,#0xc0` |
| 9 | `0xda84c`, `0xda9d6`, `0xda9f8`, `0xdae04`, `0xddb10`...`0xddc74`, `0xddd0c` | 1 | 1 | `90` / `60` | single `cmp r2,#0x90` or `adds r0,#0x60` sites |

**Segmentation caveat (important).** The primary sweep stops at the first `pop {..., pc}`, so functions
with early returns are undercounted, and prologue-overlapping starts can share hits. Two verified
examples that this table therefore under-reports:

* `0xdc4ec` - first epilogue at `0x0dc51e` (`pop.w {r4, r5, r6, r7, r8, sb, sl, pc}`), yet the
  `0x24*idx + 0x70` build at `0x0dc674`..`0x0dc682` is later in the same function (see §3.1).
* `0xe0a7a` - a ~4.5 KB multipath init whose `add.w r4,r1,#0x230` at `0xe15d2` lies far past an
  earlier return, so only its `0x70` fields appear in the window count.

Those two instructions are confirmed from raw windows, shown here:

```
### window inside the 0xe0a7a region (MMIO init loop) ###
0x0e1524: mov.w    r2, #0xc000
0x0e1528: movs     r1, #0
0x0e152a: add.w    r3, ip, #8
0x0e152e: movt     r2, #0x4004
0x0e1532: add.w    r3, r2, r3, lsl #2
0x0e1536: sub.w    r2, r2, #0x8000
0x0e153a: add.w    r2, r2, ip, lsl #6
0x0e153e: add.w    ip, ip, #1
0x0e1542: cmp.w    ip, #0x10
0x0e1546: str      r1, [r3, #4]
0x0e154a: str      r1, [r2, #0x64]
0x0e154c: bne      #0xe1524
...
0x0e15d2: add.w    r4, r1, #0x230
```
The loop counter is `ip` with bound `0x10` (16), and the target base is `movt r2,#0x4004/0x4006`
(an MMIO window), not a message buffer. `0x230` here is a one-off struct/pointer offset.

One further artifact: an earlier, epilogue-ignoring sweep credited `0xd8910` with `60 C0 E0`; the `C0`
and `E0` sites (`0xd8a38`, `0xd8a44`) lie **after** that function's `pop {..., pc}`
(`0xd89f8`/`0xd8a26`) and are the head of a literal/jump pair, not code. They are excluded here.

---

## 3. Per-record loop patterns

### 3.1 The only base-pointer stride that matches the store's 0x24 sub-stride

Three functions build an element address as **`0x24 (36 B) * index + 0x70 (112 B)`** and walk it. This
is the one place where the task's 112 and the store's 36-byte sub-block (`kv-payload-check.md`
§1.3: `[4 zero + 5 data]` groups 36 B apart) co-occur. But the loop bound is the **number of RF
chains (4)**, and the base is a per-band struct (`0x35c` stride) or an MMIO table.

`0xdc4ec` - direct pointer build, index `r3`, loop `cmp r3,#3 / bls`:

```
0x0dc674: movs   r1, #0x24
0x0dc676: mla    r1, r1, r3, r4          ; r1 = 0x24*r3 + r4
0x0dc67a: cmp    r3, #3
0x0dc67c: add.w  r1, r1, #0x70           ; + 112
0x0dc680: str    r1, [r4, #0x1c]         ; store the selected element pointer
0x0dc682: bls    #0xdc5ae               ; loop while r3 <= 3
...
0x0dc7e0: movs   r1, #0x24
0x0dc7e2: mla    r1, r1, r3, r4
0x0dc7e6: adds   r1, #0x70
0x0dc7e8: str    r1, [r4, #0x1c]
```

`0xdcb78` - same shape, `r5 = 0x35c * band` (per-band struct stride):

```
0x0dcb7c: mov.w  r5, #0x35c
0x0dcb88: mul    r5, r5, r0
0x0dcd0a: add.w  r1, r3, r3, lsl #3      ; r1 = 9*idx
0x0dcd0e: add.w  r3, r5, #0x70           ; per-band base + 112
0x0dcd12: add.w  r3, r3, r1, lsl #2      ; + 36*idx
0x0dcd16: ldr    r1, [pc, #0x388]
0x0dcd18: add    r3, r1
...
0x0dce56: movs   r3, #0x24
0x0dce58: add.w  r0, r5, #0x70
0x0dce5c: smlabb r3, r3, fp, r0          ; 0x24*fp + (r5 + 0x70)
```
Its inner loop is `subs r3,#1 / cmp r3,#3 / bne` (4 iterations over chains); the other branch
(`0xdcd22`..`0xdce76`) uses `r2 = 0..9` for a rate table.

`0xdd688` - identical idiom:

```
0x0dd714: add.w  r1, r2, r2, lsl #3      ; 9*idx
0x0dd718: add.w  sb, sb, r2, lsl #6      ; table + 64*idx
0x0dd71c: lsls   r1, r1, #2              ; 36*idx
0x0dd71e: add.w  sb, sb, #0x60
...
0x0dd73c: adds   r1, #0x70
0x0dd748: add    r1, r6
0x0dd74e: add    r1, r3
0x0dd754: str.w  r1, [r8, #0x108]
```

**Assessment.** These are RF-chain/rate-table walkers. Their `0x24` stride coincides numerically with
the store's sub-block spacing, but (a) the counter is 4 (or 10 for the rate table), never 45-58;
(b) the base is a firmware struct/MMIO table, not a message- or DMA-supplied pointer; (c) the
`0x70` is a fixed *base* added once, not a period. They are the closest code analogue to a
"36-byte-record + 112-byte-base" walk and the best shortlist target, but nothing here parses the
`.kv` buffer.

### 3.2 Loop counters that would fit 45-58 records

Sweeping every bounded function for `cmp #imm` with `imm` in `0x2D..0x3A` (45..58) gives ≈28 sites.
The second block contributes exactly three functions / four sites, all of which are message-opcode
dispatches, not record counters:

| address | instruction | owner | reading |
|---|---|---|---|
| `0xda116` | `cmpls r6, #0x31` | `0xda0fc` | bounds on `ldrb r6,[r4]`, `ldrb r5,[r4,#1]` - a message id pair |
| `0xdb5f2` | `cmp r2, #0x34` | `0xdb5ec` | `ldrb r2,[r0,#1]` - message opcode compare |
| `0xdc388` | `cmp r2, #0x32` | `0xdc378` | `ldrb r2,[r0,#1]` - message opcode compare |
| `0xdc3a2` | `cmp r2, #0x33` | `0xdc378` | same field, chained opcode compare |

```
### func 0xdc378, message-opcode dispatch, not a loop ###
0x0dc378: push.w  {r4, r5, r6, r7, r8, lr}
0x0dc37c: ldrb    r5, [r0]
0x0dc37e: ldrb    r2, [r0, #1]
0x0dc388: cmp     r2, #0x32
0x0dc38c: ldr.w   r6, [r3, #0xa2c]
0x0dc390: beq     #0xdc3c0
...
0x0dc3a2: cmp     r2, #0x33
0x0dc3a8: it      eq
0x0dc3aa: moveq   r0, #0x36
0x0dc3ae: it      ne
0x0dc3b0: movne   r0, #0x37
```

```
### func 0xda0fc, message-id range check ###
0x0da10e: ldrb    r6, [r4]
0x0da110: ldrb    r5, [r4, #1]
0x0da112: cmp     r5, #0x1e
0x0da114: it      ls
0x0da116: cmpls   r6, #0x31
0x0da118: bls     #0xda11e
```

In the main block, the one function that combines a 45-58 comparison with skeleton offsets is
`0x17ec8` - and its 45-58 value is a **state/retry cap**, not a record count:

```
### func 0x17ec8: cmp #0x31 (49) + [r0,#0x70], [r1,#0x30], [r3,#0x90] ###
0x017ed2: ldrb.w  r2, [r3, #0x9c]
0x017ed6: ldr     r0, [r0, #0x70]        ; base pointer out of the argument object
0x017ed8: cmp     r2, #0x31              ; counter cap = 49
0x017eda: add.w   r4, r3, #0x74
0x017ede: bhi     #0x17fa0
0x017ee0: adds    r2, #1
0x017ee2: add.w   r1, r0, #0x1680
0x017ee6: strb.w  r2, [r3, #0x9c]        ; ++counter
...
0x017f82: lsr.w   r1, r6, r2             ; inner loop over 8 rate indices
0x017f9c: cmp     r2, #8
0x017f9e: bne     #0x17f82
...
0x017fa4: cmp     r2, #0x31
0x017faa: bls     #0x17fde
```
The 49 bounds a statistics/retry state machine (`ldr/udiv/str` of rate counters at `r3+0x7c..0x94`),
and the `0x70` is the object's base-pointer field. There is **no** function in the image that pairs a
`0x70`/`0xE0`/`0x150` stride with a 45-58 loop bound.

---

## 4. Connection to the host side / message vocabulary

### 4.1 `smac_msg_proc_set_cali_ppdu_tx_num_req` has no address in the image

The string lives in the tail name pool (real text region `0xE266C-0xE2B90` identified in phase 4):

```
0x0e2868: 'smac_msg_proc_set_cali_self_cts_tx_num_req'
0x0e2894: 'smac_msg_proc_set_cali_ppdu_tx_num_req'     <-- name_off = 0xE2894
0x0e28bc: 'smac_msg_proc_rx_ring_reset_req'
0x0e28dc: 'smac_msg_proc_update_tx_bypass_ctrl_req'
```

Searching the whole file for a pointer to that name - both `name_off` and the phase-6 bias value
`name_off + 0x40000` - returns **zero occurrences**:

```
ptr 0x000e2894  (raw file offset)   found at: []
ptr 0x00122894  (file offset +0x40000) found at: []
```

So, exactly as `ulw/phase6/firmware-symbols.md` §4 warned, the `smac_*` names have **no recovered
code addresses**; only the 30 radar/DFS table entries were resolvable. Consequently the cali message
processor's code cannot be located by name, and its use of skeleton constants cannot be tested
directly. (This is the single biggest limit of this pass - see §5.)

### 4.2 `ulw/phase7/firmware-callgraph.md` contains no calibration functions

That report's graph is the 30 radar/DFS handlers (`cac`, `detect_check`, `pulse_check_filter`,
`radarfilter`, ...) and their four shared helpers. It has **no** calibration-sync entries. The
calibration sync functions named elsewhere (`hmac_sync_dmac_cali_cfg_rsp_entry`,
`hmac_chan_tx_cali_sync`, `hmac_save_cali_data_to_file_2g/5g`) are **driver-module** (`.ko`)
functions, not firmware functions, so they are outside the firmware scan. They were checked anyway:

| function | module / offset | immediate operands observed (full-body A32 disassembly) | skeleton constants? |
|---|---|---|---|
| `hmac_chan_tx_cali_sync` | `wifi.ko` `.text+0x46dac`, 280 B | `#5`, `#0x2c4`, `#4`, `#0x537`, `#0x2c0`, `#0x2c9`, `#0x2ce`, `#0x2da`, `#0x7e8` | **none** |
| `hmac_sync_dmac_cali_cfg_rsp_entry` | `wifi.ko` `.text+0x881a4`, 148 B | `#0x8b2d`, `#0x268e`, `#0x75`, `#0x2684`, `#0xe`, `#0x599` | **none** |

Representative instructions (from the full-body listing):

```
hmac_chan_tx_cali_sync:        hmac_sync_dmac_cali_cfg_rsp_entry:
  mov    r3, #0x2c4              movw   r4, #0x8b2d
  movw   r2, #0x537              mov    r1, #0x75
  cmp    r3, #4                  movw   r3, #0x268e
  mov    r3, #0x2c0              movw   r3, #0x2684
  movw   r3, #0x2c9              movw   r2, #0x599
  movw   r1, #0x7e8              mov    r1, #0xe
```

For context, the same constant set is common module-wide (a naive whole-`.text` ARM immediate sweep,
counting branch targets as well, finds 123 skeleton-value sites in `wifi.ko` and 55 in `plat.ko`), so
the absence inside these two specific functions is the meaningful part: **the named calibration sync
functions do not reference the 112/48 skeleton constants.**

### 4.3 What this means for the "host connection"

The driver hands the firmware a physical address (`plat.ko` `.bss+0x128`/`0x12c`, per
`cali-buffer-populator.md` §1) and the firmware presumably consumes it inside whichever
`smac_msg_proc_*` handler reads the calibration request. That handler is in the image, but its
address is not recoverable from the name pool (§4.1). No function found by the skeleton-constant
search was observed loading a base pointer from a message/DMA argument and striding it by 112 or 48.

---

## 5. Limits and ranked shortlist for the next pass

### Limits (explicit, unresolved)

1. **The fingerprint is not selective.** `0x30/0x60/0x70/0x90/0xC0` appear 527 times in 265 of 2,768
   functions. Presence of these constants cannot identify a parser; only the *pattern* (stride + loop
   bound + base provenance) can, and no such pattern matches 45-58 records.
2. **`0x150` and `0x1C0` are never strides.** They occur only as struct-field offsets/stack slots
   (e.g. `0x196e0`, `0xb3fbc`, `0x493a2`) and never as `add #imm` increments. `0x230` occurs 7 times,
   all as `mov/mov.w/ldr/str/strh` immediates or a one-off base add (`0xe15d2 add.w r4,r1,#0x230`);
   none is a loop increment or stride.
3. **The premise period conflicts with a withdrawn analysis.** An earlier report (`kv-payload-structure.md`, since withdrawn from the repository because its template did not reproduce) claimed a 152-byte record with a 38-word stride. The current, reproducible state of the store analysis is `kv-payload-check.md`: the only skeleton that survives re-running is 58 zero bytes inside a 112-byte period (dual store) and 26 inside 48 (2.4 GHz store). The 112/48 constants used in this scan therefore come from the reproducible analysis, not from the withdrawn one.
4. **Function segmentation is heuristic.** Prologue-based starts miss push-less leaf functions; body
   truncation at the first epilogue drops code after early returns (the 0x400-byte cap can also clip
   long functions). The whole-region linear sweep and the bounded sweep disagree by an order of
   magnitude on the main block (1 vs 471 hits) because of embedded data, so any *count* here is a
   lower/upper bound, not exact.
5. **The cali message handler is unlocated.** No pointer to
   `smac_msg_proc_set_cali_ppdu_tx_num_req` exists in the image (§4.1), so its code could not be
   searched at all. Any claim that "the firmware parser does/doesn't use 112" is therefore
   **unresolved**, not negative.
6. **No base-provenance trace was performed.** Proving that none of the skeleton-hit functions reads
   the DMA'd `.kv` buffer would require taint-tracking from the message dispatch; that was out of
   scope for this pass and is not claimed.

### Ranked shortlist (next pass)

1. **`0xdcb78` / `0xdd688` / `0xdc4ec`** - the only `0x24*idx + 0x70` base-pointer walkers; best
   structural match to the store's 36-byte sub-block + 112 base. Next step: taint `r4`/`r5` (per-band
   struct, 0x35c stride) back to its allocator and ask whether any cali message writes that struct;
   if the premise's 112 period is real, check these functions for a second, wider outer loop.
2. **`0x17ec8`** - the only function with a 45-58 comparison (`cmp r2,#0x31`) together with skeleton
   offsets (`[r0,#0x70]`, `[r1,#0x30]`, `[r3,#0x90]`). Next step: identify `r3+0x9c`/`r3+0x98` and the
   `r0` object; if it is a message/cali structure, this is the closest thing to a 49-entry walker.
3. **`0x196e0`** - the only main-block function combining `0x150`+`0x1c0`+`0x230` field stores; decide
   whether the cleared object is the driver-side calibration mirror rather than an unrelated struct.
4. **Resolve the `smac_msg_proc_set_cali_ppdu_tx_num_req` address by index.** The string sits at
   ordinal N in the contiguous tail name pool (`0xE266C`...). If the firmware has a parallel pointer
   table of `smac_msg_proc_*` addresses (the phase-4 `{addr,name}` tables are the radar set only),
   locating a table whose k-th entry points at the k-th name - or any pointer that reaches
   `0xE2894` through a base+offset - would finally give the handler address and let §4 be answered.
   This is the highest-value unresolved item.

---

## Verification

* **Primary scan**: the bounded-function sweep in §1 (2,768 functions; 265 with hits; 527 hits; value
  census table). Every `0x…:` excerpt in §2 and §3 is verbatim capstone output from that sweep or
  from a per-function disassembly window of the cited function.
* **Second-block loop counters** (§3.2): exhaustive list of `cmp #imm ∈ 0x2D..0x3A` for
  `0xD8000-0xE1FFF` = the four sites shown; owner functions confirmed by re-disassembling from each
  owner's prologue.
* **Structure walkers** (§3.1): disassembly window of `0xdc4ec` (`0xdc5ae`-`0xdc964`), `0xdcb78`
  (`0xdcb78`-`0xdd0ba`), `0xdd688` (`0xdd688`-`0xdd7e8`), `0xe0a7a` (`0xe1500`-`0xe159c`).
* **Host side** (§4): string search for `smac_msg_proc_set_cali_ppdu_tx_num_req`
  (`d.find(...)` = `0xE2894`) and 4-byte pointer searches for `0xE2894` and `0x122894` (0 hits each);
  full-body A32 disassembly of `hmac_chan_tx_cali_sync` and `hmac_sync_dmac_cali_cfg_rsp_entry` from
  `hi5622v100_wifi.ko` `.symtab`/`.text`.
* **Cross-check**: whole-region linear sweep gave 69 hits (68 second block, 1 main block); the
  discrepancy with the bounded sweep is explained in §1 and §5 item 4.
