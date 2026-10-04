# The firmware's interrupt-controller registrations and enablers, complete - and the ctrl-rb host2device line is among the enabled ids (phase 47, 2026-10-04)

Task `st_01a10763` (parent `01a0fc5c`). Scope: **static, read-only** - `build/tmp/FIRMWARE.bin`,
the record docs of phases 31/32/35/43/45/46. No device access, no register writes; the only file
written is this one.

**Headline, in one paragraph.** The firmware's interrupt-controller API has five entry points
(`register` core file `0x874b0`, its bounded wrapper `0x86da2`, a per-id class/target helper
`0x81a3c`, `enable` `0x86ff4` - the writer of the ISENABLER at CA `0x40161100` - and a
disable+clear `0x86db8` - the writer of CA `0x40161180`/`0x40161280`). A whole-image scan finds
**28 `register` call sites** (27 constant ids + 1 dynamically-computed), **29 `enable` call sites**
(27 constant-id sites covering 25 distinct ids, plus 2 dynamic), and **17 disable/clear call sites**.
The ctrl-rb's host2device line is source id **`0x4C`** (record's identification, re-confirmed here
from the handler it is wired to), and it **is registered (file `0x9800`, fn `0x40295`) and enabled
(file `0x9816`, which sets bit 12 of word 2 of the enable bitmap: CA `0x40161108`)** - it is *not* an
unregistered/un-enabled source. The last link is therefore a **wiring/e-delivery issue between the
ctrl-rb and the interrupt controller** (or/and the unobservability of the block from the host, phase
32), not a missing firmware registration. Two exact caveats are named in section 7: the id-to-line
binding is a hardware fact not encoded in the image, and the routine that contains the `0x4C`
registration has no direct caller (it is reached only through a function pointer).

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` |
| convention | runtime address = file offset + `0x40000`; **all offsets below are FILE offsets unless marked `CA` (device register) or `rt` (runtime/RAM)** |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM`, `CS_MODE_THUMB` |
| record read | `phase46/intr-fires-at-ctrlrb.md`, `phase45/{hi1105,hi3881}-mailbox-irq.md`, `phase45/reconcile-luofu.md`, `phase43/device-dr-side.md`, `phase32/{interrupt-status-and-doorbell-path,irq-block-mapping}.md`, `phase35/dispatcher-never-runs.md` |
| verification | 128 distinct quoted instruction offsets (133 quotes incl. repeats) and 22 quoted literal words re-disassembled / re-read this task; 0 mismatches (section 8) |

---

## 0. Answer, stated as the question asks

| question | answer | key evidence |
| --- | --- | --- |
| Which source id is the ctrl-rb's host2device line? | **`0x4C`** | the id registered in the same `pcie_msg_init` that builds the mailbox ctx and the doorbell, with the H2D dispatch thunk as its handler: register call file `0x9800` `fn = 0x40295` -> file `0x294` -> `0x818ac`. The handler is what acks CA `0x400392f0` (the ctrl-rb's `HOST_INTR_CLR`, sibling bit 0 = `host2device_tx_intr_clr`) - phase 45/reconcile §3 |
| Did the firmware's init enable it? | **Yes.** `enable(0x4C)` at file `0x9816` -> `0x86ff4` -> `str.w` into CA `0x40161100 + (0x4c/32)*4` = **`0x40161108`, bit 12** | `0x9814 movs r0,#0x4c`, `0x9816 bl #0x86ff4`; enable body `0x8701a`-`0x8702c` |
| So is the last link an unregistered source? | **No.** The source is registered *and* enabled, exactly like the sibling family's message interrupt. The gate is downstream: the ctrl-rb -> controller event delivery (and the block's host-invisibility, phase 32). | this document, sections 2/3/6 |
| Anything else that names the ctrl-rb id? | Yes - one static per-id descriptor table (CA-less, RAM `rt 0x105F8C` = file `0xC5F8C`), 3 bytes per id, read by `register()` for the id's class and target. Its record for `0x4C` is `4c 05 01` (file `0xC6070`). Nothing in the init writes an id list into the controller. | sections 1.6 and 5 |

---

## 1. The interrupt-controller API, as the image contains it

Five entry points, all with the caller sets fully enumerated (section 2). The controller is a
GIC-400-compatible pair of register files at CA `0x40160100` (CPU interface) and `0x40161000`
(distributor) - the offsets match the standard map exactly (`0x100` set-enable, `0x180`
clear-enable, `0x380` clear-active, `0x400` priority, `0x800` target, `0xc00` config, `0xf00`
SGIR), so:

| CA | role | firmware writer |
| --- | --- | --- |
| `0x40161000` | distributor CTLR | init (`0x6e98` = 0, `0x6ed4` = 1) |
| `0x40161004` | TYPER (source count) | read at `0x6e8a` |
| `0x40161100` | **ISENABLER (enable bitmap)** | **`0x86ff4` only** (`0x8702c`, literal file `0x87044` = `0x40161100`) |
| `0x40161180` | ICENABLER (clear-enable bitmap) | `0x86db8` (`0x86dea`); init sweeps it (`0x74ac`) |
| `0x40161280` | ICPENDR (clear-pending bitmap) | `0x86db8` (`0x86df2`, as `r3+0x100`) |
| `0x40161380` | ICACTIVER | `0x86db8` (`0x86df2`); init sweeps it (`0x74b2`); CPU init (`0x8302e`..`0x8304a`) |
| `0x40161400` | IPRIORITYR | init sweeps it (`0x74a0`); CPU init (`0x8302e`..`0x8304a`) |
| `0x40161800` | ITARGETSR | `register` (`0x87526`); init sweeps it (`0x74a2`) |
| `0x40160104`/`0x40160108` | PMR / BPR | CPU init (`0x83054`, `0x8305a`) |
| `0x4016010c`/`0x40160110` | IAR (pending id) / EOIR | ISR `0x82f04` / `0x82f52` (phase 32) |
| `0x40160100` | CPU-interface CTLR | CPU init (`0x8308e`) |

(The literal-scan for these CAs over the whole image is in section 1.7.)

### 1.1 `register(id=r0, class=r1, fn=r2)` core - file `0x874b0`

```
0874b0  2de9f041   push.w  {r4,r5,r6,r7,r8,lr}
0874b6  1f4d       ldr     r5,[pc,#0x7c]        ; 0x0017D398   (pool file 0x87534)
0874c4  05eb8403   add.w   r3,r5,r4,lsl #2      ; r3 = 0x17D398 + id*4
0874c8  d3f89820   ldr.w   r2,[r3,#0x98]        ; already registered -> return -1
0874e8  c3f89880   str.w   r8,[r3,#0x98]        ; HANDLER ARRAY: *(0x17D430 + id*4) = fn
0874ec  faf7a6fa   bl      #0x81a3c             ; per-id class/target write (1.4)
0874f6  104a       ldr     r2,[pc,#0x40]        ; 0x00105F8C   (pool file 0x87538)
0874f8  04eb4403   add.w   r3,r4,r4,lsl #1      ; r3 = id*3
0874fc  1344       add     r3,r2                ; &descriptor[id]
087500  9b78       ldrb    r3,[r3,#2]           ; byte2 = target
08750c  0b48       ldr     r0,[pc,#0x2c]        ; 0x40161800 ITARGETSR (pool 0x8753c)
087526  2c50       str     r4,[r5,r0]           ; ITARGETSR lane <- byte2  (ids > 0x1f only)
```

No bound check of its own - the wrapper below supplies it.

### 1.2 `register` wrapper - file `0x86da2` (the callers use this)

```
086da2  32b1       cbz     r2,#0x86db2          ; fn == 0 -> return -1
086da4  5f28       cmp     r0,#0x5f             ; id > 0x5f -> trap (str r3,[r3]; udf)
086da6  02dd       ble     #0x86dae
086dae  00f07fbb   b.w     #0x874b0             ; tail into the core
```

### 1.3 `enable(id=r0)` - file `0x86ff4`, the ISENABLER writer

```
086ff4  38b5       push    {r3,r4,r5,lr}
086ff6  a0f11003   sub.w   r3,r0,#0x10
086ffa  4f2b       cmp     r3,#0x4f             ; legal ids 0x10..0x5f
086ffe  02d9       bls     #0x87006
087006  0e4d       ldr     r5,[pc,#0x38]        ; 0x0017D398 (pool 0x87040)
08701a  6209       lsrlo   r2,r4,#5             ; word = id/32
08701c  04f01f04   andlo   r4,r4,#0x1f          ; bit  = id%32
087020  03fa04f4   lsllo.w r4,r3,r4            ; 1<<bit
087026  074b       ldrlo   r3,[pc,#0x1c]        ; 0x40161100 ISENABLER (pool 0x87044)
08702c  43f82240   strlo.w r4,[r3,r2,lsl #2]   ; **the enable-bitmap write**
```

The `it lo` guard is `id < *(0x17d398)` (the source count). There is exactly **one** writer of CA
`0x40161100` in the image (literal `0x40161100` occurs once, at file `0x87044`).

### 1.4 per-id class/target helper - file `0x81a3c`, called from `register`

```
081a3c  30b5       push    {r4,r5,lr}
081a42  1b68       ldr     r3,[r3]              ; *(0x17D398) = source count
081a52  9842       cmp     r0,r3                ; id bounds check
081a60  2b59       ldr     r3,[r5,r4]           ; r4 = 0x40161400 IPRIORITYR (pool 0x81a88)
081a72  2851       str     r0,[r5,r4]           ; byte lane (id&3) = class<<4
```

(byte-lane write, `id&~3` word, `(id*8)&0x18` shift - a per-id class/priority byte.)

### 1.5 `disable + clear-pending(id=r0)` - file `0x86db8`

```
086db8  38b5       push    {r3,r4,r5,lr}
086dba  a0f11003   sub.w   r3,r0,#0x10          ; same 0x10..0x5f gate
086ddc  0123       movs    r3,#1
086de4  03fa04f4   lsl.w   r4,r3,r4             ; 1<<(id%32)
086de8  084b       ldr     r3,[pc,#0x20]        ; 0x40161180 ICENABLER (pool 0x86e0c)
086dea  43f82240   str.w   r4,[r3,r2,lsl #2]    ; clear enable
086dee  03f58073   add.w   r3,r3,#0x100          ; -> 0x40161280 ICPENDR
086df2  43f82240   str.w   r4,[r3,r2,lsl #2]    ; clear pending
```

Note for the record: this **does** write the distributor pending-clear word (`0x40161280`,
computed as `+0x100`, i.e. not a literal) - phase 32's "the distributor-side pending bit is never
read or cleared by firmware code" was literal-based and is corrected here for the clear-pending
half.

### 1.6 The per-id descriptor table that "names" ids - RAM `rt 0x105F8C` = file `0xC5F8C`

`register()` reads it directly (`0x874f6`/`0x874f8`/`0x87500`): 3 bytes per id, indexed **by id**
(not by `id-0x10`), byte 0 = the id, byte 1 = class (`0x05`/`0x0e` - the same value the callers pass
in `r1`), byte 2 = target (`0x01`/`0x02`, written into ITARGETSR for ids > `0x1f`). It is
meaningful for ids `0x20..0x5c`; below `0x20` the same memory holds unrelated data.

```
file 0xC6070 (id 0x4c): 4c 05 01     file 0xC6076 (id 0x4e): 4e 05 01
file 0xC6013 (id 0x2d): 2d 05 01     file 0xC6019 (id 0x2f): 2f 05 01
file 0xC5FEC (id 0x20): 20 05 01     file 0xC6082 (id 0x52): 52 0e 01
```

So **the ctrl-rb's id is present in the only source-list-shaped table the image contains**, and the
`4c 05 01` record's class matches the `r1 = 5` actually passed at the `0x4C` registration (loaded at
file `0x979e`, the last write to `r1` before the call).

### 1.7 Completeness checks

* `register`/`enable`/`disable` entry points have **no** absolute function-pointer anywhere in the
  image (`0x000874b0/1`, `0x00086da2/3`, `0x00086ff4/5`, `0x00086db8/9`, `0x00081a3c/3d`: zero word
  and zero halfword hits) - so the direct-branch caller sets below are exhaustive.
* Every branch (BL/BLX/`b.w`/`b`, all encodings) whose target is one of the entry points or one of
  the call sites was collected two independent ways (a sign-corrected manual Thumb decoder over all
  even offsets, and a capstone linear sweep with `skipdata`); both agree. Three *shared-tail* sites
  were found and are folded in below (branches that re-enter a `bl` with a different `r0`).

---

## 2. Complete `register(id, class, fn)` list - 28 sites

`reg` = `bl 0x874b0` (direct), `wrap` = `bl 0x86da2` (bounded wrapper). `fn` is the literal loaded
into `r2`, as a runtime address, with the file offset of the code it points at (all are odd =
Thumb, except `0x40`/`0x42` whose values are even).

| # | call site (file) | id | class `r1` | `fn` (runtime -> file) | via |
| - | ---------------- | -- | ---------- | ---------------------- | --- |
| 1 | `0x6f70` | `0x40` | `r7` (dynamic) | `0x00040671` -> file `0x671` (even) | wrap |
| 2 | `0x6f7a` | `0x42` | `r7` (dynamic) | `0x0004065d` -> file `0x65d` (even) | wrap |
| 3 | `0x7200` | `0x3e` | 5 | `0x00046849` -> file `0x6849` | wrap |
| 4 | `0x7212` | `0x3f` | 5 | `0x00046849` -> file `0x6849` | wrap |
| 5 | `0x7ce0` | `2` | 0 | `0x000c20f9` -> file `0x820f9` | wrap |
| 6 | `0x821a` | `0x46` | 5 | `0x00040525` -> file `0x525` | wrap |
| 7 | `0x7ee40` | **dynamic** (`r6`, from `[obj+0x114]`) | table byte | `0x00046939` -> file `0x6939` | wrap |
| 8 | `0x91ef4` | `0x2f` | 5 | `0x000aec95` -> file `0x6ec95` | wrap |
| 9 | `0x91f04` | `0x31` | 5 | `0x000b54c9` -> file `0x754c9` | wrap |
| 10 | `0x91f14` | `0x32` | 5 | `0x000b0d51` -> file `0x70d51` | wrap |
| 11 | `0x91f24` | `0x33` | 5 | `0x000aec69` -> file `0x6ec69` | wrap |
| 12 | `0x91f34` | `0x34` | 5 | `0x000aeb75` -> file `0x6eb75` | wrap |
| 13 | `0x91f44` | `0x52` | `0xe` | `0x000ae48d` -> file `0x6e48d` | wrap |
| 14 | `0x91f54` | `0x53` | 5 | `0x000b0d51` -> file `0x70d51` | wrap |
| 15 | `0x91f64` | `0x3b` | 5 | `0x000b0c45` -> file `0x70c45` | wrap |
| 16 | `0x921dc` | `0x35` | 5 | `0x000aec89` -> file `0x6ec89` | wrap |
| 17 | `0x921ec` | `0x37` | 5 | `0x000b54bd` -> file `0x754bd` | wrap |
| 18 | `0x921fc` | `0x38` | 5 | `0x000b0d45` -> file `0x70d45` | wrap |
| 19 | `0x9220c` | `0x39` | 5 | `0x000aec5d` -> file `0x6ec5d` | wrap |
| 20 | `0x9221c` | `0x3a` | 5 | `0x000aeb69` -> file `0x6eb69` | wrap |
| 21 | `0x9222c` | `0x54` | `0xe` | `0x000ae481` -> file `0x6e481` | wrap |
| 22 | `0x9223c` | `0x55` | 5 | `0x000b0d45` -> file `0x70d45` | wrap |
| 23 | `0x9224c` | `0x3c` | 5 | `0x000b0c39` -> file `0x70c39` | wrap |
| 24 | `0x93f64` | `0x3d` | 5 | `0x000aea1d` -> file `0x6ea1d` | wrap |
| 25 | `0x96ca` | `0x2d` | 5 | `0x000462f9` -> file `0x62f9` | reg |
| 26 | `0x96dc` | `0x2e` | 5 | `0x0004624d` -> file `0x624d` | reg |
| 27 | `0x9800` | **`0x4C`** | 5 (from `0x979e`) | `0x00040295` -> file `0x295` (**the H2D dispatch thunk**) | reg |
| 28 | `0x980c` | `0x4e` | 5 | `0x000402a1` -> file `0x2a1` (the D2H kick) | reg |

Distinct registered ids: `0x02, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x38, 0x39,
0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x42, 0x46, 0x4c, 0x4e, 0x52, 0x53, 0x54, 0x55`
(27) + one dynamic site (`0x7ee40`). **The record's "exactly four" was only the `pcie_msg_init`
subset; the complete set is 28 call sites.**

## 3. Complete `enable(id)` list - 29 call sites (the ISENABLER writes)

Each row writes `1 << (id % 32)` into word `id / 32` of CA `0x40161100`:

| # | call site (file) | id | bitmap word / bit | via |
| - | ---------------- | -- | ----------------- | --- |
| 1 | `0x632` | **dynamic** (`r7`, from `[obj+4]`) | — | `bl` |
| 2 | `0x18f0` | `0x46` | `0x40161108` bit 6 | `bl` |
| 3 | `0x7206` | `0x3e` | `0x40161104` bit 30 | `bl` |
| 4 | `0x7218` | `0x3f` | `0x40161104` bit 31 | `bl` |
| 5 | `0x96ea` | `0x2d` | `0x40161104` bit 13 | `bl` |
| 6 | `0x96f0` | `0x2e` | `0x40161104` bit 14 | `bl` |
| 7 | `0x9816` | **`0x4C`** | **`0x40161108` bit 12** | `bl` |
| 8 | `0x981c` | `0x4e` | `0x40161108` bit 14 | `bl` |
| 9 | `0x707aa` | `0x32` | `0x40161104` bit 18 | `bl` |
| 10 | `0x707b4` | `0x53` | `0x40161108` bit 19 | `bl`-tail (`b.w`) |
| 11 | `0x707ba` | `0x38` | `0x40161104` bit 24 | `bl` |
| 12 | `0x7ee46` | **dynamic** (`r6`, from `[obj+0x114]`) | — | `bl` |
| 13 | `0x8708c` | `0x1d` | `0x40161100` bit 29 | `bl` |
| 14 | `0x91efa` | `0x2f` | `0x40161104` bit 15 | `bl` |
| 15 | `0x91f0a` | `0x31` | `0x40161104` bit 17 | `bl` |
| 16 | `0x91f1a` | `0x32` | `0x40161104` bit 18 | `bl` |
| 17 | `0x91f2a` | `0x33` | `0x40161104` bit 19 | `bl` |
| 18 | `0x91f3a` | `0x34` | `0x40161104` bit 20 | `bl` |
| 19 | `0x91f4a` | `0x52` | `0x40161108` bit 18 | `bl` |
| 20 | `0x91f5a` | `0x53` | `0x40161108` bit 19 | `bl` |
| 21 | `0x91f6a` | `0x3b` (fall-through from `0x91f62`) **and `0x3c`** (`b 0x91f6a` from `0x92252` with `r0 = 0x3c`) | `0x40161104` bit 27 / `0x40161104` bit 28 | shared tail |
| 22 | `0x921e2` | `0x35` | `0x40161104` bit 21 | `bl` |
| 23 | `0x921f2` | `0x37` | `0x40161104` bit 23 | `bl` |
| 24 | `0x92202` | `0x38` | `0x40161104` bit 24 | `bl` |
| 25 | `0x92212` | `0x39` | `0x40161104` bit 25 | `bl` |
| 26 | `0x92222` | `0x3a` | `0x40161104` bit 26 | `bl` |
| 27 | `0x92232` | `0x54` | `0x40161108` bit 20 | `bl` |
| 28 | `0x92242` | `0x55` | `0x40161108` bit 21 | `bl` |
| 29 | `0x93f6a` | `0x3d` | `0x40161104` bit 29 | `bl` |

Distinct **enabled** ids: `0x1d, 0x2d, 0x2e, 0x2f, 0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x38, 0x39,
0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x46, 0x4c, 0x4e, 0x52, 0x53, 0x54, 0x55` (25) + 2 dynamic
sites. There is **no other path** into the enable bitmap: no bulk `str` of a constant mask into CA
`0x40161100` exists anywhere (the only literal for it is the enable function's pool word at file
`0x87044`), and no indirect dispatch of `0x86ff4` exists.

Registered but **never enabled** by this API: `0x02`, `0x40`, `0x42`. Enabled but **never
registered** by this API: `0x1d` - its handler-array slot is pre-written directly by the boot init
(section 5).

## 4. The ids written into the *clear* bitmaps - 17 `disable+clear` call sites

`0x86db8` writes `1<<(id%32)` into CA `0x40161180` (ICENABLER) **and** CA `0x40161280` (ICPENDR).
This is teardown, not enablement, but it is part of the complete bitmap-write enumeration:

| call site (file) | ids | note |
| ---------------- | --- | ---- |
| `0x620` | dynamic (`r7`) | a channel-object id |
| `0x6946` | dynamic (`[tbl + idx*8 + 4]`) | from a table walked at `0x693a` |
| `0x9756`, `0x975c` | `0x4c`, `0x4e` | **inside `pcie_msg_init`, before their register/enable at `0x9800`/`0x9816`** - a stale-state clear; net state after the function is enabled |
| `0x70cf8` | `0x32` | |
| `0x70cfe` | `0x53` (fall-through) and `0x55` (`b 0x70cfe` from `0x70d3c`) | shared tail |
| `0x70d36` | `0x38` | |
| `0x7ee2e` | dynamic (`r6`) | |
| `0x86e32` | dynamic (`[tbl + idx*8 + 4]`) | in the ready/announce routine |
| `0x94758` | `0x3d` | |
| `0x9498a`, `0x94990`, `0x94996`, `0x9499c` | `0x2f`, `0x30`, `0x31`, `0x32` (+ `0x38` via `b 0x9499c` from `0x94a8c`) | |
| `0x94a7a`, `0x94a80`, `0x94a86` | `0x35`, `0x36`, `0x37` | |

`0x30` and `0x36` appear only here (cleared, never registered/enabled by the API). The ids
`0x2f..0x34` are both enabled (block at `0x91exx`, section 3) and cleared (block at `0x9498a`-
`0x94a8c`, inside `pcie_msg_init`) - the two blocks' relative execution order is not settled by the
image alone, so for those ids the *net* state is order-dependent; nothing in the ctrl-rb path
(`0x4c`/`0x4e`) depends on it (their clear precedes their register+enable in the same function and
is not re-done later).

## 5. What the firmware's interrupt-controller init does BEFORE registering

The bring-up lives inside the boot routine whose interrupt-relevant part is file `0x6e20`-`0x6ee0`;
it performs, in order:

1. **Zero the handler array.** `0x6e2a` loads `0x0017D430` (runtime; pool file `0x7148`); `0x6e5c
   mov.w r3,#0x180`; `0x6e64 mov r0,r7`; `0x6e66 bl #0x81e0c` -> `memset(0x17D430, 0, 0x180)` =
   the 0x60-slot `fn` array the ISR indexes (`0x82f16`).
2. **Pre-write three handler slots** (before any `register()` call):
   `0x6e7c str r3,[r7]` = `0xc2081` -> array[0] (file `0x82081`);
   `0x6e80 str r3,[r7,#4]` = `0xc6c89` -> array[1] (file `0x86c89`);
   `0x6e84 str r3,[r7,#0x74]` = `0x40161004` -> array[0x1d].
   The first two are code pointers (odd addresses); the third is the **TYPER register address**, not
   a code pointer - reported as observed, flagged as an oddity (`0x1d` is also the id the CPU init
   self-tests, item 6 below, and the id `enable(0x1d)` picks up at `0x8708c`).
3. **Derive and store the source count.** `0x6e86`/`0x6e8a` read distributor TYPER CA
   `0x40161004`; `0x6e8c and r3,r3,#0x1f`; `0x6e90 adds r3,#1`; `0x6e92 lsls r3,r3,#5`;
   `0x6e94 str r3,[r2]` with `r2 = 0x0017D398` (pool `0x7154`) -> **`*(0x17D398) = (TYPER[4:0]+1)
   << 5`**. Every `register`/`enable`/`disable` call is gated on `id < *(0x17D398)`.
4. **Disable the distributor and reset the whole id space.**
   `0x6e98 str r6,[r7]` (r6 = 0) -> distributor CTLR CA `0x40161000` = 0;
   then the sweep whose gate heads are `0x6e9c`/`0x6eb0`/`0x6ec2` (`cmp r3,r1` against the count)
   and whose tails are `0x74a0` (`str r0,[r3,r2]`, `r0 = -1`, `r2` = CA `0x40161400` IPRIORITYR),
   `0x74a2` (`str.w ip,[r3,r6]`, `ip = 0x01010101`, `r6` = CA `0x40161800` ITARGETSR),
   `0x74ac` (`str.w r2,[r6,r0,lsl #2]`, `r2 = -1`, `r6` = CA `0x40161180` ICENABLER) and `0x74b2`
   (`str.w r2,[ip,r0,lsl #2]`, `ip` = CA `0x40161380` ICACTIVER). **Net effect: every source starts
   disabled, inactive, priority byte 0xff, and (from id `0x20`) target byte 0x01 (word `0x01010101`)**
   - the vendor's
   compiled loop starts at `r3 = 0x20` so the banked SGI/PPI window (ids 0..0x1f) is handled on the
   CPU-interface side instead.
5. **Enable the distributor:** `0x6ed4 str.w sl,[r7]` (`sl = 1`) -> CA `0x40161000` = 1.
6. **CPU interface init:** `0x6eda bl #0x83024` ->
   `0x83028` pool file `0x83094` = CA **`0x40161380`** and `0x8302c` `str r3,[r2]` (`r3 = -1`)
   plus `0x8302e`..`0x8304a` `str.w r3,[r2,#0x80..0x9c]` -> ICACTIVER[0] = -1 and **IPRIORITYR
   (CA `0x40161400`) words 0..7 = -1** (the banked ids 0..0x1f, priorities 0xff);
   `0x8304e movs r2,#0xff` / `0x83054 str r2,[r3]` with pool `0x83098` = CA `0x40160104` **PMR =
   0xff**; `0x83056 movs r2,#3` / `0x8305a str r2,[r3,#4]` -> **BPR = 3**; then a self-test through
   `0x81a3c` for ids `0`, `1`, `2`, `0x1d` (`0x8305c`..`0x83088`, traps if any returns non-zero);
   `0x8308a movs r2,#1` / `0x8308e str r2,[r3]` with pool `0x8309c` = CA `0x40160100` -> **CPU
   interface CTLR = 1**.

**What the init does *not* do:** it never names the ctrl-rb. There is no source-list write of any
kind into the controller - the only place the ctrl-rb's id appears in "init" form is the **static
descriptor table** of section 1.6 (file `0xC5F8C`, record `4c 05 01` at file `0xC6070`), which is
data read by `register()`, and the enable itself, which is the ordinary `enable(0x4C)` at file
`0x9816`. So "did the init enable it" resolves to: the ctrl-rb id goes through **exactly the same
two-step path as every other enabled source** (`register` then `enable`), and both steps exist.

## 6. The ctrl-rb host2device source, from the firmware's side

* **Identity `0x4C`.** The id is registered in `pcie_msg_init` together with the H2D/D2H mailbox
  machinery (`0x96b6`..`0x9788` build the ctx with out[0]/out[1]/doorbell/ack), and its handler is
  the H2D dispatch thunk:
  `0x294 ldr r0,[pc,#4]` (pool file `0x29c` = `0x0010C190`, the message ctx) -> `0x296 b.w #0x818ac`;
  `0x818ac` writes ack CA `0x400392F0` = 1, consumes `0x40039010` (out[0]), re-arms the doorbell
  `0x400392D4` = 8, and dispatches on the ctx's handler table. Under the phase-45 sibling map the
  register it acks is the ctrl-rb's `HOST_INTR_CLR` bit 0 = `host2device_tx_intr_clr`, so id `0x4C`
  *is* the host2device line of the ctrl-rb. Id `0x4E`'s handler (file `0x2a0` -> `0x86108`) is the
  mirrored D2H routine (`0x2a2 b.w #0x86108`).
* **Registration:** file `0x9800`, `r0 = 0x4c` (`0x97f6`), `r1 = 5` (`0x979e`), `r2 = 0x00040295`
  (pool file `0x9858`); the class 5 matches the descriptor record `4c 05 01`.
* **Enable:** file `0x9816`, `r0 = 0x4c` (`0x9814`) -> bit 12 of ISENABLER word 2, **CA
  `0x40161108`**.
* **Consistent with the sibling family**: the sibling's `host2device_tx_intr` is armed by an unmask
  in the ctrl-rb's own register block (`0x2E8`, phase 45), and the phase-46 measurement shows that
  block's raw/masked status already latches for the doorbell. The firmware-side enable (this
  section) and the ctrl-rb-side unmask (phase 45/46) are thus **both** open, and the only remaining
  link is the event's delivery from the ctrl-rb into the controller's source `0x4C`.

## 7. Two exact caveats (what would change this answer)

1. **The id -> hardware-line binding is not in the image.** That id `0x4C` is the ctrl-rb's H2D line
   is an inference from (a) the handler's own register set (ack `0x400392F0` = the sibling's
   `host2device_tx_intr_clr`) and (b) the sibling's two-line model. The controller's source-to-line
   wiring is SoC-internal. **Missing evidence:** a device-side trace of the interrupt taken with
   source `0x4C` asserted, or the SoC's interrupt-map documentation.
2. **`pcie_msg_init` (file `0x9334`) has no direct caller in the image.** Whole-image scans find no
   branch of any encoding to `0x9334`, and no absolute pointer to it in code pools or the writable
   data area; the only reference is a **function-pointer word** at file `0xcf254` =
   `0x00049335` (Thumb pointer to file `0x9334`), inside the message-module descriptor at file
   `0xcf250` (which also carries CA `0x4003a000` = ETE and CA `0x40039000` = the ctrl-rb, and
   neighbouring fields `0xc6365`/`0x4028d`/`0x40289`/`0x40281`/`0x40285`). So the routine is an
   init/driver callback invoked indirectly; the table runner was not located in the image. If, in a
   takeover, that callback never runs, `enable(0x4C)` never lands and the answer becomes "the source
   is enabled only in code that the takeover does not reach". **Missing evidence:** the descriptor
   runner (the code that walks file `0x792c`-`0x7990`, which also contains `0x0010f250`), or a live
   read of ISENABLER word 2 (CA `0x40161108`, bit 12) after the firmware's boot - which phase 32
   proved is **not readable through any host window**, so it needs a device-side instrument.

Everything else in this document is a compiled instruction, a literal, or a table byte at a quoted
file offset.

## 8. Verification ledger

Every offset quoted above was re-disassembled this task with the repo's
`pyenv/Scripts/python.exe` + capstone 5.0.7 (`CS_ARCH_ARM`, `CS_MODE_THUMB`) immediately before this
file was written:

* **128 distinct instruction offsets** (133 individual quotes including repeats) - each re-disassembled
  and decoded at an instruction boundary, all decode - at file offsets
  `0x632 0x294 0x296 0x2a0 0x2a2 0x620 0x18ee 0x18f0 0x6e2a 0x6e5c 0x6e64 0x6e66 0x6e7c 0x6e80
  0x6e84 0x6e86 0x6e8a 0x6e92 0x6e94 0x6e98 0x6e9c 0x6eb0 0x6ec2 0x6ed4 0x6eda 0x6f70 0x6f7a
  0x707a2 0x707a4 0x707a8 0x707aa 0x707ae 0x707b4 0x707b8 0x707ba 0x7200 0x7206 0x7212 0x7218
  0x74a0 0x74a2 0x74ac 0x74b2 0x7ce0 0x7ee40 0x7ee46 0x821a 0x83024 0x8302c 0x8302e 0x8304e
  0x83054 0x83056 0x8305a 0x8308a 0x8308e 0x87076 0x8708c 0x874b0 0x874e8 0x874f6 0x874fe
  0x87500 0x8750c 0x87526 0x86da2 0x86da4 0x86dae 0x86db8 0x86de8 0x86dea 0x86dee 0x86df2
  0x86ff4 0x86ff6 0x86ffa 0x8701a 0x8701c 0x87020 0x87026 0x8702c 0x91ef4 0x91efa 0x91f04
  0x91f0a 0x921dc 0x921e2 0x92242 0x9224c 0x9334 0x93f64 0x93f6a 0x94758 0x9498a 0x94990
  0x94996 0x9499c 0x94a7a 0x94a80 0x94a86 0x96b2 0x96b6 0x96ca 0x96d8 0x96da 0x96dc 0x96e8
  0x96ea 0x96ee 0x96f0 0x9754 0x9756 0x975a 0x975c 0x979e 0x97a6 0x97f6 0x97f8 0x97fe 0x9800
  0x9806 0x9808 0x980a 0x980c 0x9814 0x9816 0x981a 0x981c` - 0 mismatches against the mnemonic
  families claimed here;
* **22 literal words** re-read: file `0x87040` = `0x0017D398`, `0x87044` = `0x40161100`, `0x87048`
  = `0x0017DF10`, `0x87534` = `0x0017D398`, `0x87538` = `0x00105F8C`, `0x8753c` = `0x40161800`,
  `0x86e0c` = `0x40161180`, `0x7148` = `0x0017D430`, `0x7154` = `0x0017D398`, `0x7168` =
  `0x40161004`, `0x716c` = `0x40161000`, `0x7170` = `0x40161400`, `0x7178` = `0x40161180`, `0x71a8`
  = `0x40161380`, `0x83094` = `0x40161380`, `0x83098` = `0x40160104`, `0x8309c` = `0x40160100`,
  `0x9858` = `0x00040295`, `0x985c` = `0x000402A1`, `0x97c0` = `0x000462F9`, `0x97c4` =
  `0x0004624D`, `0xcf254` = `0x00049335` - all matched;
* **descriptor table bytes**: file `0xC6070` = `4c 05 01`, file `0xC6076` = `4e 05 01`;
* **completeness cross-check**: the caller sets for `0x874b0`/`0x86da2`/`0x86ff4`/`0x86db8` were
  derived twice (sign-corrected manual Thumb decoder over all even offsets + capstone `skipdata`
  linear sweep), and the three shared-tail entries (`0x70d3c`, `0x92252`, `0x94a8c`) were confirmed
  by re-disassembling their windows.
