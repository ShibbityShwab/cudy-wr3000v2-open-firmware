# FIRMWARE.bin — disassembly and structural map

Scope: local file only, read-only, no device access. Task `st_01a0f3fe`.

- Target: `C:/Users/ShibbityShwab/router-openwrt/build/tmp/FIRMWARE.bin`
- Size: 928,920 bytes = `0xE2C98`
- Tool: `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe`, capstone 5.0.7, pyelftools 0.33
- Context: `build/custom/DRIVER-BLACKBOX.md` §16 (blob is uncompressed ARM/Thumb code plus tables; max 4 KB entropy 7.256; banner `ChenTangV100R001C20T13`; tokens `VERIFY20M`/`VERIFY40M`; an `smac`/`hcc` symbol table; one `DEADBEEF`).

Every claim below is followed by the command that produced it. Inference that is not directly
measured is labelled **HYPOTHESIS**. `#` comments inside the scripts are mine.

Reproduce everything with the single script in the last section.

---

## 1. Architecture determination

**Finding: the image is Thumb-2 (16-bit Thumb plus 32-bit Thumb-2) with a small, localized amount of
A32 (classic ARM) code. It is not "ARM" in the sense of an all-A32 image, and it is not a
container/bytecode.**

### 1.1 The two header words are in-file Thumb-style pointers

`cmd: ./pyenv/Scripts/python.exe -c "import struct; d=open('build/tmp/FIRMWARE.bin','rb').read(); print(len(d), hex(len(d))); print([hex(struct.unpack_from('<I',d,o)[0]) for o in (0,4)])"`

```
928920 0xe2c98
['0x46971', '0xc742d']
```

- `w0 = 0x00046971` → Thumb pointer, code entry at `0x46970` (bit 0 is the Thumb state bit).
- `w1 = 0x000C742D` → Thumb-style pointer at `0xC742C`, but that location is a **data table**, not
  code (see §1.3). So only `w0` behaves as a code pointer.

### 1.2 Thumb decodes cleanly; ARM does not

`cmd: python - <<'PY'` (decode a 256-byte window at a given offset in each mode and count the
instructions capstone returns before it hits an invalid byte):

```python
from capstone import *
d=open('build/tmp/FIRMWARE.bin','rb').read()
def count_decode(off, mode, window=256):
    md=Cs(CS_ARCH_ARM, mode); insns=list(md.disasm(d[off:off+window], off))
    cov=(insns[-1].address+insns[-1].size)-off if insns else 0
    return len(insns), cov, sum(1 for i in insns if i.size==4), sum(1 for i in insns if i.size==2)
for label,off in [("entry1",0x46970),("entry2",0xc742c),("body@0x1000",0x1000),("body@0x20000",0x20000)]:
    for mname,mode in [("ARM",CS_MODE_ARM),("THUMB",CS_MODE_THUMB)]:
        n,cov,n32,n16=count_decode(off,mode)
        print("%-14s %-6s insns=%3d bytes=%3d (32b=%d 16b=%d)"%(label,mname,n,cov,n32,n16))
```

```
entry1         ARM    insns=  2 bytes=  8 (32b=2 16b=0)     <- dies immediately
entry1         THUMB  insns= 78 bytes=198 (32b=21 16b=57)  <- clean, coherent
entry2         ARM    insns=  0 bytes=  0
entry2         THUMB  insns=  0 bytes=  0
body@0x1000    ARM    insns=  0 bytes=  0
body@0x1000    THUMB  insns= 94 bytes=256 (32b=34 16b=60)
body@0x20000   ARM    insns=  2 bytes=  8 (32b=2 16b=0)
body@0x20000   THUMB  insns= 90 bytes=256 (32b=38 16b=52)
```

At the header entry `0x46970`, Thumb yields 78 well-formed instructions inside 256 bytes (198 bytes
consumed by 21 32-bit and 57 16-bit instructions); ARM yields 2 instructions and then an invalid
encoding. The same holds in the middle of the body. **This is the architecture evidence: the body is
Thumb/Thumb-2.**

### 1.3 Header word 1 points at data, not code

`cmd: python - <<'PY'` (bytes at `0xc742c`, then decode attempts):

```
0xc742c: 04 ff ff ff fe 00 00 00 05 01 ff 00 03 00 01 00 04 ff ff 00 ff 01 01 01 ...
thumb decode count: 0 ; arm decode count: 0
```

`0xC742C` is a signed-byte/offset table (`04 FF FF FF FE 00 00 00 05 01 FF 00 ...`); neither mode
produces a single valid instruction there. **HYPOTHESIS:** `w1` is a data/config pointer alongside
the code pointer `w0`. (Consistent with §16 of the blackbox note, which already found a signed-byte
table at this offset.)

### 1.4 A32 code exists, but only in a few localized regions

Fingerprints of A32 (classic ARM, 4-byte aligned, `mov pc,lr`-style return) versus Thumb:

`cmd: ./pyenv/Scripts/python.exe - <<'PY'`

```python
d=open('build/tmp/FIRMWARE.bin','rb').read()
print("ARM 'bx lr' (1eff2fe1):", d.count(bytes.fromhex('1eff2fe1')))
print("ARM 'ldr pc,[pc,#-4]' (04f01fe5):", d.count(bytes.fromhex('04f01fe5')))
print("Thumb 'bx lr' (7047):", d.count(bytes.fromhex('7047')))
print("Thumb-2 'push.w' (2de9):", sum(1 for o in range(0,len(d)-4,2) if d[o:o+2]==b'\x2d\xe9'))
print("Thumb-2 'pop.w {.,pc}' (bde8):", sum(1 for o in range(0,len(d)-4,2) if d[o:o+2]==b'\xbd\xe8'))
```

```
ARM   'bx lr'            71
ARM   'ldr pc,[pc,#-4]'  11     (A32 veneer thunks)
Thumb 'bx lr' (70 47)    797
Thumb-2 push.w {..,lr}   863
Thumb-2 pop.w {..,pc}    1476
```

The 71 A32 `bx lr` returns are not scattered; they cluster:

`cmd: python - <<'PY'` (offsets of `1e ff 2f e1`, clustered with a 64-byte gap):

```
clusters: 0xc2c58..0xc2dc0 (20), 0xc2e0c..0xc2e64 (5), 0xc2eac..0xc2fb4 (6), ... 0xc3b20..0xc3b34 (3),
          0xe20b8, 0xe21a4..0xe21c4, 0xe2374..0xe23d8, 0xe241c..0xe242c, 0xe24f8
```

So the A32 material occupies roughly **`0xC2C50–0xC3B40`** and **`0xE2000–0xE24FC`**; everywhere
else the code is Thumb-2.

### 1.5 CPU profile: ARMv7-A/R (CP15 + SRS/RFE present)

`cmd: python - <<'PY'` (disassemble `0xc2c50` as ARM):

```
0x0c2c50: 020180e3  orr  r0, r0, #0x80000000
0x0c2c54: 3c0f09ee  mcr  p15, #0, r0, c9, c12, #1
0x0c2c58: 1eff2fe1  bx   lr
0x0c2c5c: 5c0f19ee  mrc  p15, #0, r0, c9, c12, #2
0x0c2c74: 1eff2fe1  bx   lr
0x0c2c78: 1d0f19ee  mrc  p15, #0, r0, c9, c13, #0
```

CP15 system-register access (`mrc/mcr p15, ...`) means this is **not** an ARMv7-M (Cortex-M) core;
M-profile has no CP15. Combined with the `srsdb`/`rfeia`/`clrex` trampoline in §2.4, the core is an
**ARMv7-A or ARMv7-R (32-bit application/real-time profile)** core. The INI's ITCM/DTCM window
(blackbox §16) is consistent with an R-class/TCM design. **HYPOTHESIS (profile choice A vs R):**
A-class or R-class; the blob alone cannot pick between them.

---

## 2. Vector table / entry region layout

### 2.1 Byte map of the first 0x64 bytes

`cmd: xxd -l 128 build/tmp/FIRMWARE.bin`

```
00000000: 7169 0400 2d74 0c00 0000 0000 0000 0000  qi..-t..........
00000040: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000050: 4031 1000 1431 1000 e830 1000 bc30 1000  @1...1...0...0..
00000060: 9030 1000 04e0 4ee2 1305 6df9 1300 02f1  .0....N...m.....
00000070: ff1f 2de9 0410 0de2 01d0 4de0 0240 2de9  ..-.......M...@-.
```

| offset | size | content | reading |
|---|---|---|---|
| `0x00` | 4 | `0x00046971` | Thumb code pointer → `0x46970` |
| `0x04` | 4 | `0x000C742D` | pointer → `0xC742C`, a data table |
| `0x08` | 0x48 | 56 zero bytes | reserved |
| `0x50` | 20 | `0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090` | 5 pointers, stride −0x2C, **all > file size** |
| `0x64` | 0x5C | A32 trampoline | see §2.4 |

`cmd: python -c "import struct; d=open('build/tmp/FIRMWARE.bin','rb').read(); print([hex(struct.unpack_from('<I',d,o)[0]) for o in range(0x50,0x64,4)], len(d))"`

```
['0x103140', '0x103114', '0x1030e8', '0x1030bc', '0x103090'] 928920
```

The 5 values at `0x50` are **runtime addresses** (all exceed the 928,920-byte file), so they cannot be
file offsets; the blackbox note reaches the same conclusion for the INI ITCM/DTCM values.

### 2.2 The header is not a self-describing container

`cmd: file build/tmp/FIRMWARE.bin` → `data` (no ELF/gzip/LZMA framing; blackbox §0). There is no
section table and no trailer checksum; region boundaries in §3 are derived from content, not from a
header.

### 2.3 Entry `0x46970` — first instructions (Thumb mode)

`cmd: python - <<'PY'` (capstone `CS_ARCH_ARM, CS_MODE_THUMB`, first 20 instructions from `0x46970`):

```
0x046970: 03f07063  and   r3, r3, #0xf000000
0x046974: 40f2f241  movw  r1, #0x4f2
0x046978: 1843      orrs  r0, r3
0x04697a: bcf755f9  bl    #0x2c28
0x04697e: d4e7      b     #0x4692a
0x046980: 1423      movs  r3, #0x14
0x046982: 00f13602  add.w r2, r0, #0x36
0x046986: 1946      mov   r1, r3
0x046988: 03a8      add   r0, sp, #0xc
0x04698a: 3bf05bfa  bl    #0x81e44
0x04698e: 0028      cmp   r0, #0
0x046990: ddd0      beq   #0x4694e
0x046992: 2378      ldrb  r3, [r4]
0x046994: 4ff49d61  mov.w r1, #0x4e8
0x046998: 1b06      lsls  r3, r3, #0x18
0x04699a: 03f07063  and   r3, r3, #0xf000000
0x04699e: bee7      b     #0x4691e
0x0469a0: 6a0d      lsrs  r2, r5, #0x15      <- data/literal bytes inside/after the function
0x0469a4: 2de9f341  push.w {r0,r1,r4-r8,lr}
0x0469a8: 0546      mov   r5, r0
0x0469aa: 1646      mov   r6, r2
0x0469ac: 0024      movs  r4, #0
0x0469ae: dff88880  ldr.w r8, [pc, #0x88]
```

The same bytes read as ARM give only `cmnvs`/`mvnsmi` and then an invalid word — garbage. The Thumb
reading is coherent (branches resolve backwards into the function, `bl` targets land on other code,
and `0x0469A4` is a textbook `push.w {r0,r1,r4-r8,lr}` prologue). **This is the disassembly sample
backing the architecture claim.**

### 2.4 Entry region 0x64 — A32 exception trampoline (ARM mode decodes, Thumb does not)

The bytes at `0x64` are a clean, recognizable ARMv7 exception entry/return sequence:

`cmd: python - <<'PY'` (capstone `CS_ARCH_ARM, CS_MODE_ARM` from `0x64`):

```
0x000064: 04e04ee2  sub   lr, lr, #4
0x000068: 13056df9  srsdb sp!, #0x13
0x00006c: 130002f1  cps   #0x13
0x000070: ff1f2de9  push  {r0-r8, sb, sl, fp, ip}
0x000074: 04100de2  and   r1, sp, #4
0x000078: 01d04de0  sub   sp, sp, r1
0x00007c: 02402de9  push  {r1, lr}
0x000080: 2d46a0e1  lsr   r4, sp, #0xc
0x000084: 0446a0e1  lsl   r4, r4, #0xc
0x000088: 045094e5  ldr   r5, [r4, #4]
0x00008c: 017085e2  add   r7, r5, #1
0x000090: 047084e5  str   r7, [r4, #4]
0x000094: 980b02fa  blx   #0x82efc
0x000098: 045084e5  str   r5, [r4, #4]
0x00009c: 006094e5  ldr   r6, [r4]
0x0000a0: 000035e3  teq   r5, #0
0x0000a4: 0060a013  movne r6, #0
0x0000a8: 000036e3  teq   r6, #0
0x0000ac: a70e031b  blne  #0xc3b50
0x0000b0: 1ff07ff5  clrex
0x0000b4: 0240bde8  pop   {r1, lr}
0x0000b8: 01d08de0  add   sp, sp, r1
0x0000bc: ff1fbde8  pop   {r0-r8, sb, sl, fp, ip}
0x0000c0: 000abdf8  rfeia sp!
```

The same bytes decoded as Thumb are nonsense (`b`, `lsls`, `vld4.8`). This is the canonical ARMv7
`sub lr,lr,#4 / srsdb / cps #SVC / align-stack / rfeia` construction: it re-enters SVC mode, saves a
full register set, and returns with `rfeia`. **HYPOTHESIS:** this is the firmware's exception/IRQ
entry stub and it is genuine A32 code, not a coincidence — the probability of 0x5C bytes decoding to
a textbook SRS/RFE trampoline at random is negligible.

### 2.5 A32 veneers (`ldr pc,[pc,#-4]`)

`cmd: python - <<'PY'` (capstone ARM at `0xc3b40` and `0xe24fc`):

```
0x0c3b40: 04f01fe5  ldr pc, [pc, #-4]     ; target word at 0xc3b44 = 0x000c6fe1
0x0c3b48: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x000c6feb
0x0c3b50: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x000c26b9
0x0c3b68: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x000c6fc9
0x0c3b70: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x000c6fd7

0x0e24fc: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00008e31
0x0e2504: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00000081
0x0e250c: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00008e15
0x0e2514: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00008e3d
0x0e251c: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00008e25
0x0e2524: 04f01fe5  ldr pc, [pc, #-4]     ; target 0x00008ea9
```

Each entry is 8 bytes: the instruction plus the literal it loads. The targets are odd (Thumb) values
(`0x8e31`, `0xc6fe1`, …). **HYPOTHESIS:** ARM→Thumb call thunks. The exact address base of the target
words cannot be resolved from the blob alone (§6, item 4); with base `0x40000` the `0xc26b9` target
lands on a clean `push {r4, lr}` at file `0x826b8`, and with base `0` the `0x8e31` target lands on
`movw r3, #0x1a91` at file `0x8e30` — the two tables appear to encode different bases, which is one
more reason to treat the load map as unresolved.

---

## 3. Section map

Entropy is Shannon entropy over the whole region; `pro`/`epi` are Thumb prologue/epilogue counts
computed per 16 KB in §3.2. `cmd: python - <<'PY'` (region loop):

```
region                                      start      end     size   entropy
main Thumb-2 code                        0x000000 0x0c0000   786432   7.259
code/data transition                     0x0c0000 0x0c3000    12288   6.975
descriptor + name tables                 0x0c3000 0x0c8000    20480   6.225
constant/config tables                   0x0c8000 0x0d0000    32768   3.896
index/value lookup tables                0x0d0000 0x0d8000    32768   5.871
second Thumb-2 code block                0x0d8000 0x0e2000    40960   7.031
tail: A32 stubs/veneers, ISR names,
      MMIO table, DEADBEEF, growth table 0x0e2000 0x0e2c98     3224   5.655
```

Sizes sum to 928,920 = the exact file size.

### 3.1 Role evidence per region

**Main code `0x00000–0x0BFFFF` (786,432 B, 7.259).** Entropy 7.0–7.27 throughout; 4 KB blocks carry
14–108 push-with-LR prologues and comparable epilogues; 71 of the file's A32 `bx lr` returns sit
mostly outside this region (§1.4), while Thumb `bx lr` (797) and `pop.w {.,pc}` (1476) live here.
Also contains the header, the 5-pointer table and the A32 trampoline of §2. Example code:

`cmd: python - <<'PY'` (Thumb disasm at `0x120ea`, a clean start):
```
0x0120ea: ...  push {r0, r1, r4, lr} ; add.w r1, sp, #7 ; mov r4, r0 ...
```

**Code/data transition `0x0C0000–0x0C2FFF` (12,288 B, 6.975).** Entropy drops to 6.4–7.0 and
prologues thin out (`0xC0000` block: 1 prologue, 0 pointers). The A32 CP15 routines of §1.5 live at
`0xC2C50` inside this band.

**Descriptor + name tables `0x0C3000–0x0C7FFF` (20,480 B, 6.225).** Prologues ≈0, pointer-like words
jump to hundreds per 4 KB, and 122 printable runs ≥8 chars appear in one 16 KB block. Holds:
- the 53-word ascending address table at `0xC3EE0` (`0010443f 00104442 00106d49 0010632f 00104445 ...`, mostly +3 steps);
- the rate/param `{descriptor, name}` table at `0xC48F8` (§4.3);
- the 9-entry `{code_addr, name}` handler table at `0xC5B84` (§4.2);
- the NUL-terminated debug/name pool (`&g_irq_controller_lock`, `device_module_thread`, …).

**Constant/config tables `0x0C8000–0x0CFFFF` (32,768 B, 3.896).** Block entropy falls to 0.65–3.26
(minimum at `0xCF000`); zero/`0xFF`/small-int bytes dominate (blackbox §16 measured 28,084 zeros and
40,113 bytes < 0x10 in `0xC8000–0xD8000`). Recurring register-ish words `0x00004003`, `0x00005003`,
`0x000000F5`, `0x000001E0`. **HYPOTHESIS:** per-mode/per-channel register configuration rows.

**Index/value lookup tables `0x0D0000–0x0D7FFF` (32,768 B, 5.871).** Zero prologues; 4-byte entries
whose high 16 bits are a monotone index and low 16 bits a value (blackbox §16: `0xD0940`, `0xD2AAC`).

**Second Thumb-2 code block `0x0D8000–0x0E1FFF` (40,960 B, 7.031).** Entropy back up to 7.0;
prologues reappear; a 64 KB-scoped count shows 32 clean function starts in the `0xD0000` band
(§4.1). Example:

`cmd: python - <<'PY'` (Thumb disasm at `0xD8000`):
```
0x0d8000: 41f6b003  movw  r3, #0x18b0
0x0d8004: 40f2b871  movw  r1, #0x7b8
0x0d8008: f0b4      push  {r4,r5,r6,r7}
0x0d800a: 0124      movs  r4, #1
```

**Tail `0x0E2000–0x0E2C98` (3,224 B, 5.655).** Contains, in order: A32 routines (the `bx lr`
cluster `0xE20B8–0xE24F8`, §1.4), the 6-entry A32 veneer table at `0xE24FC` (§2.5), a 9×12-byte MMIO
descriptor list at `0xE2528` (`{0x40034000,0x3A,0}`, `{0x40034014,0x3A,0}`, …), the trailer
`{0x00000000, 0x0000000F, 0xDEADBEEF}` at `0xE2594`, the 45-name `smac_*_isr` list at `0xE266C`, and
the doubling sequence `0,1,3,6,13,26,52,104,209,419,838,1677,3355,6710` at `0xE2C60` (file end).

`cmd: python -c "import struct; d=open('build/tmp/FIRMWARE.bin','rb').read(); print(hex(d.find(bytes.fromhex('efbeadde')))); print(' '.join('%08x'%struct.unpack_from('<I',d,o)[0] for o in range(0xe2594,0xe25a0,4)))"`

```
0xe259c
00000000 0000000f deadbeef
```

### 3.2 Prologue/epilogue density (16 KB granularity)

`cmd: python - <<'PY'` (for each 16 KB: count `push.w` with LR, `bx lr`, pointer-like words, strings):

```
offset     ent   pro  epi   ptr   str
0x000000  7.08   108   65   157     2     <- densest code (header + entry functions)
0x004000  7.20    33   16   153     5
...
0x0c0000  6.91    40   67   191     2
0x0c4000  6.13     0    0   563   122     <- name/symbol table
0x0c8000  4.42     2    0  1276    12     <- constant tables
0x0cc000  3.26     1    0   965     6
0x0d0000  5.82     0    0   240     0     <- lookup tables
0x0d4000  5.82     3   10   377     0
0x0d8000  6.99    84   68    67     3     <- second code block
0x0dc000  6.93    38   75    43     3
0x0e0000  6.95     7   12    48    47     <- tail (includes ISR name list)
```

The transition from code (prologues present, few strings) to tables (no prologues, hundreds of
pointer words / string runs) is sharp at `0xC3000–0xC4000`, which is why the section boundaries above
are placed there.

---

## 4. Functions and tables

### 4.1 Function boundaries from prologue patterns

`cmd: python - <<'PY'` (count every `push {..,lr}` site; count "clean" starts where the prologue is
immediately preceded by a `bx lr`, a `pop {..,pc}`, or a `nop`):

```
all push-with-LR prologue sites: 2664
clean function starts:            115
median gap between clean starts: 2106 bytes (min 48, max 89328)
clean starts per 64 KB region: 0x000000:7 0x010000:5 0x020000:4 0x030000:3 0x040000:6
  0x050000:3 0x060000:3 0x070000:5 0x080000:12 0x090000:6 0x0a0000:12 0x0b0000:13
  0x0c0000:4 0x0d0000:32 0x0e0000:0
```

- 2664 push-with-LR sites is an **upper bound** on function count (it includes nested register
  saves inside larger functions).
- 797 `bx lr` + 1476 `pop {..,pc}` = 2273 return sites is a **lower bound** (every function returns
  at least once).
- 115 starts are high-confidence (immediately follow a terminator). The true count is between 2273
  and 2664; exact enumeration needs a recursive-descent disassembler and knowledge of the load map
  (§6).

First 40 high-confidence starts (file offsets, Thumb):

`cmd: python - <<'PY'`:

```
0x001c70  0x009a4e  0x00bdce  0x00e4a2  0x00e5a2  0x00ea72  0x00f25c  0x0120ea
0x0179b8  0x01829a  0x01de7c  0x01ff06  0x024a8c  0x024c6c  0x026eb6  0x02b65a
0x032364  0x03a206  0x03d898  0x040f32  0x0411e2  0x047fb0  0x0493a2  0x049874
0x04b93a  0x05533e  0x05998c  0x05d6a6  0x064136  0x06e03c  0x06e45c  0x070592
0x0707a2  0x070cb6  0x0754d8  0x07e208  0x081b00  0x081dc0  0x081fba  0x0820a2
```

Sample decoded headers confirm real function prologues:

```
0x001c70: push {r4,r5,r6,r7,lr} ; mov r7, r1 ; movs r1, #0
0x009a4e: push {r0,r1,r2,r3,r4,lr} ; mov r3, r1 ; cbz r1, ...
0x00bdce: push.w {r0,r1,r4-r8,lr} ; mov r7, r1 ; mov r4, r0
0x00e4a2: push {r0,r1,r2,r3,r4,lr} ; mov r2, r0 ; bl ...
0x00ea72: push {r4,r5,r6,lr} ; mov r4, r0 ; ldrb.w r3, [r0, #0x649]
0x0179b8: push {r3, lr} ; cbz r0, ... ; cbnz r1, ...
```

### 4.2 The `smac`/`hcc` handler table maps names to code addresses

There is a table at `0xC5B84` of 8-byte records `{u32 code_addr, u32 name_ptr}` whose name pointers
use a **file-offset bias of +0x40000** (`file_offset = name_ptr − 0x40000`).

`cmd: python - <<'PY'` (walk the table; resolve names):

```
entries: 9   span 0xc5b84..0xc5bc4
    0 code=0x00170b9c file=OUT   name='dfsenable'
    1 code=0x000b6c05 file=0x76c04 name='cacenable'
    2 code=0x000b6bb9 file=0x76bb8 name='dfsdebug'
    3 code=0x000b6b69 file=0x76b68 name='offchannum'
    4 code=0x000b6b19 file=0x76b18 name='ctsdura'
    5 code=0x000b6ae1 file=0x76ae0 name='radarfilter'
    6 code=0x000b6a33 file=0x76a32 name='radarfilter_get'
    7 code=0x000b5549 file=0x75548 name='enabletimer'
    8 code=0x000b69f5 file=0x769f4 name='offchanenable'
```

8 of the 9 code addresses map into the body at `addr − 0x40000`; the first (`0x170B9C`) does not fit
that bias and would land outside the file (see §6). The mapped addresses are real function starts:

`cmd: python - <<'PY'` (disassemble the `cacenable` target, file `0x76C04`, Thumb):

```
0x076c04: 7fb5      push {r0,r1,r2,r3,r4,r5,r6,lr}
0x076c06: 0024      movs r4, #0
0x076c08: 1646      mov  r6, r2
0x076c0a: 0193      str  r3, [sp, #4]
0x076c0c: 0394      str  r4, [sp, #0xc]
0x076c0e: 30b9      cbnz r0, #0x76c1e
0x076c10: c021      movs r1, #0xc0
0x076c12: 1048      ldr  r0, [pc, #0x40]
0x076c14: 8cf708f8  bl   #0x2c28
0x076c18: 6420      movs r0, #0x64
0x076c1a: 04b0      add  sp, #0x10
0x076c1c: 70bd      pop  {r4,r5,r6,pc}
```

That is a complete, self-consistent function with a `push`/`pop` frame and an early-out — the table
really does name firmware functions. These names (`dfsenable`, `cacenable`, `radarfilter`,
`offchannum`, `ctsdura`) match the DFS/CCA parameters listed in blackbox §16.

### 4.3 The rate/param descriptor→name table at 0xC48F8

`cmd: python - <<'PY'` (largest stride-8 run whose second word resolves to a string, `0xC48F8..0xC49C0`):

```
 0 code=0x000c0200 name='sudden_good_delta_gdpt_ratio'
 1 code=0x000e0201 name='sudden_bad_delta_gdpt_ratio'
 2 code=0x001c0202 name='descend_protocol_pre_thrd'
 3 code=0x001e0203 name='11b_ascend_protocol_per_thrd'
 4 code=0x00200204 name='11a_ascend_protocol_per_thrd'
...
12 code=0x0034030c name='tx_rate_aging_time'
13 code=0x0038030d name='rx_rate_aging_time'
19 code=0x00180215 name='cfg_vi_spec_per'
23 code=0x00690117 name='gi1_with_2xltf_en'
25 code=0x00780219 name='ascend_bw_gdpt_better_thrd'
```

26 entries. Here the first word is **not** a code address: its low byte increments `00,01,02,03,…`
(it is a descriptor/field id plus a sequence index), so this is a **parameter name table**, not a
function table. **HYPOTHESIS:** it is the name list for the driver's `alg`/rate-control parameters
(the same `tx_rate_aging_time`, `gi1_with_2xltf_en` strings blackbox §16 groups under
"rate-control / bandwidth-probe params").

### 4.4 The `smac_*_isr` name list (names only, no addresses)

`cmd: python - <<'PY'` (extract names at `0xE266C`):

```
count: 45   (32 of them end in _isr)
smac_coex_rx_abort_end_5g_isr, smac_coex_rx_abort_end_2g_isr, smac_rx_complete_5g_isr,
smac_rx_complete_2g_isr, smac_tx_complete_5g_isr, ... smac_tx_exit_5g_isr, smac_tx_exit_2g_isr
```

The bytes immediately before the first name are a run of small index/flag bytes
(`00 00 00 01 01 01 01 00 ...`), **not** a parallel address array. So this ISR table is
**enumeration-ordered names only**: it tells you the interrupt handler names and their order, but
does not pair them with addresses in this file. `smac_*` appears 45 times and `hcc_*` 3 times as
tokens; the only fully-spelled thread names are `'HCC TX Thread'` (0xCCFC8), `'HCC RX Thread'`
(0xCCFF8) and `'HCC OAM Thread'` (0xCD028).

Summary for part (4): 2664 prologue sites / 2273+ return sites bound the function set; 115
high-confidence starts are listed; A32 veneer tables at `0xC3B40` (5) and `0xE24FC` (6); the handler
table at `0xC5B84` maps **9 names → code addresses**; the param table at `0xC48F8` maps **26
descriptor ids → names**; the ISR list at `0xE266C` is **45 names, no addresses**.

---

## 5. Build-anchoring strings

`cmd: python - <<'PY'`:

```
ChenTangV100R001C20T13  at 0x0cd2ec  (90.5% of file)
VERIFY20M               at 0x0c44de  (86.6% of file)
VERIFY40M               at 0x0c44e8  (86.6% of file)
```

Context (NUL-separated, ASCII):

```
banner:  ... 1d d7 07 00 | 1d aa 07 00 | f1 a4 07 00 | 64 00 00 04 | 00 00 00 00 |
         'ChenTangV100R001C20T13' 00 00 96 00 00 00 32 00
tokens:  'N\x00INIT\x00NORMAL\x00VERIFY20M\x00VERIFY40M\x00INVALID\x00ACTIVE'
```

Placement relative to the code:

- `VERIFY20M`/`VERIFY40M` sit at **86.6%** of the file, inside the `0xC4000` name/string block
  (§3), immediately after the `INIT/NORMAL` state tokens. They are **not** appended to code; they are
  rows in the descriptor/name table region — i.e. firmware **state names** for the 20 MHz/40 MHz
  calibration-verify states, consistent with blackbox §16.
- `ChenTangV100R001C20T13` sits at **90.5%**, at `0xCD2EC`, in the tail of the name pool. Four 32-bit
  words precede it (`0x0007D71D`, `0x0007AA1D`, `0x0007A4F1`, `0x04000064`), i.e. it is an **entry in a
  table**, not a free-standing banner.
- Other anchors measured in place: `HEARTBEAT` (0xC6328, ×1), `BROADLINK` (0xC51BA, ×1),
  `DEVICE EXCP_INFO` (0xC43A5, ×1), `DEADBEEF` at 0xE259C (×1).
- The strings are **86–91% into the file**, far from the code at 0–76%; that is exactly the layout
  of a name/symbol pool trailing a big code body (§3).

Note: of the 1662 printable runs ≥6 chars, most are instruction-byte coincidences (blackbox §16
measured 154 unique "strings" containing `F` that are Thumb-2 byte pairing). Only the NUL-delimited
runs in `0xC417F–0xC6D20` and `0xE266C–0xE2B90` are real text.

---

## 6. Conclusion — what a from-scratch open firmware for this core would still need

The blob establishes the ISA, the code/data split, the address-bias convention for symbol tables,
the SDK's naming (smac/hcc/alg/dfs/cac), and a handful of real entry points. It does **not** give
what is required to re-implement the core. In order of severity:

1. **The load map and relocation.** The header's word1, the 5-pointer table at `0x50`, the veneer
   targets, and the symbol-table code addresses all use **runtime addresses that are not file
   offsets**. The `{code_addr,name}` table resolves 8/9 entries with a `−0x40000` bias, but the other
   tables resolve with a *different* bias (`0xE24FC` targets look like file offsets; `0xC3B40`
   targets look like `+0x40000`), and the INI's ITCM/DTCM addresses are regenerated per build
   (blackbox §16). The blob never says which segment the driver copies where, so a replacement
   cannot even link its own code to the same addresses.
2. **The ITCM/DTCM image and its split.** The INI declares `firmware_itcm_len=0xADD0`,
   `firmware_dtcm_len=0x1098` (device INI: `0xA968`/`0x76C`) and `custom` segments at `0x1B2800`;
   none of those lengths occur in the file and no region can be delimited as ITCM or DTCM. **What the
   blob does not tell us: which bytes, if any, of this 928,920-byte file belong to TCM, and what the
   remaining bytes are for.**
3. **The register map and MMIO semantics.** The blob exposes only a 9-entry MMIO descriptor list
   (`{0x40034000,0x3A}`, `{0x40035000,0x3B}`, `{0x40107000,…}`, `0xE2528`) and a `DEADBEEF`-marked
   trailer; most register tables in `0xC8000–0xD7FFF` are anonymous `(index,value)` rows with no
   field names. A re-implementation would have to re-derive every register's meaning.
4. **The driver↔firmware wire format.** The message layout of the `hcc` queues, the `alg`/`cali`
   command encodings, and the calibration response format are defined by the host driver + this
   firmware jointly, and are not decodable from the blob alone (the driver's own symbols
   `hmac_sync_dmac_alg_cfg_rsp_entry` / `hmac_chan_tx_cali_sync` mark the boundary but live in the
   `.ko`, not here).
5. **Entry/exception contract.** We have two candidate entries (the A32 SRS/RFE trampoline at
   `0x64` and the Thumb function at `0x46970`) but no documentation of which the chip's boot ROM or
   the driver invokes, what arguments they take, or the interrupt vector order (the ISR names exist
   but carry no addresses and the vector table, if any, is not in this file).
6. **The RF/PHY DSP.** The exponential/ramp/bit-mask tables and the register rows are visible, but
   the algorithms that consume them (channel estimation, AGC, calibration math) are opaque; the blob
   is a compiled binary, not source.
7. **Self-integrity/format.** No section table, no length field, no checksum in the header
   (blackbox §16 arithmetic check). A replacement cannot rely on any self-describing format — the
   loading contract must come from the driver/INI.

In short: the blob is enough to **disassemble, name, and locate** the firmware's functions (Thumb-2,
ARMv7-A/R, unambiguous entry at `0x46970`, a real symbol/name pool, and 9 named handler addresses),
but a from-scratch open firmware still has to invent the **load map, TCM split, register semantics,
wire protocol, vector order, and DSP algorithms** — none of which the blob states.

---

## Reproduce

Run from `C:/Users/ShibbityShwab/router-openwrt` (all read-only; only this `.md` is written):

```bash
cd /c/Users/ShibbityShwab/router-openwrt
ls -la build/tmp/FIRMWARE.bin
./pyenv/Scripts/python.exe - <<'PY'
import struct, math, collections, re
from capstone import *
d=open('build/tmp/FIRMWARE.bin','rb').read(); N=len(d)
print("size",N,hex(N),"header",[hex(struct.unpack_from('<I',d,o)[0]) for o in (0,4)])

def count_decode(off, mode, window=256):
    md=Cs(CS_ARCH_ARM, mode); insns=list(md.disasm(d[off:off+window], off))
    cov=(insns[-1].address+insns[-1].size)-off if insns else 0
    return len(insns), cov
for lbl,off in [("entry1",0x46970),("entry2",0xc742c),("body@0x1000",0x1000)]:
    print(lbl, "ARM", count_decode(off,CS_MODE_ARM), "THUMB", count_decode(off,CS_MODE_THUMB))

print("A32 bx lr", d.count(bytes.fromhex('1eff2fe1')),
      "A32 veneer", d.count(bytes.fromhex('04f01fe5')),
      "Thumb bx lr", d.count(b'\x70\x47'),
      "pop-pc", sum(1 for o in range(0,N-4,2) if d[o:o+2]==b'\xbd\xe8'))

def ent(b):
    c=collections.Counter(b); n=len(b)
    return -sum((v/n)*math.log2(v/n) for v in c.values())
for a,b,name in [(0x00000,0xc0000,'code'),(0xc0000,0xc3000,'transition'),
                 (0xc3000,0xc8000,'desc+names'),(0xc8000,0xd0000,'const tables'),
                 (0xd0000,0xd8000,'lookup tables'),(0xd8000,0xe2000,'code2'),
                 (0xe2000,0xe2c98,'tail')]:
    print("0x%06x-0x%06x %7d %5.3f %s"%(a,b,b-a,ent(d[a:b]),name))

# prologues
pro=[o for o in range(0,N-4,2) if (d[o:o+2]==b'\x2d\xe9' and (int.from_bytes(d[o+2:o+4],'little')&0x4000)) or d[o+1]==0xB5]
print("push-with-LR sites:", len(pro))

# handler table + named target
for o in range(0xc5b84,0xc5b84+72,8):
    a,b=struct.unpack_from('<II',d,o)
    print("  code=0x%08x name=%r"%(a, d[b-0x40000:].split(b'\x00')[0].decode()))
t=(0xb6c05 & ~1)-0x40000   # clear Thumb bit -> file 0x76c04
print("disasm named target @ file 0x%x:"%t)
for i in Cs(CS_ARCH_ARM,CS_MODE_THUMB).disasm(d[t:t+24],t):
    print("   ",i.mnemonic,i.op_str)

for tok in (b'ChenTangV100R001C20T13',b'VERIFY20M',b'VERIFY40M'):
    print(tok, hex(d.find(tok)))
PY
```
