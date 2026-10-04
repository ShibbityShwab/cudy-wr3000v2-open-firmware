# interrupt-status-and-doorbell-path: the pending register, the IRQ entry, and the call chain to the H2D dispatcher (phase 32, 2026-10-04)

Task `st_01a1056b`. Purely static, read-only analysis of the on-disk firmware image. No device was
touched, nothing was written to any register.

Sequel to `docs/phase30/the-gate-located.md` and `docs/phase31/doorbell-no-latch.md`, which left one
question: *where does the host's doorbell write (CA `0x400392d4`) become a pending interrupt that the
firmware services, and by what path does that service reach the H2D dispatcher at file `0x818ac`?*
This document answers all three parts with file offsets and exact instruction bytes, and closes the
doorbell->dispatch chain end to end inside `FIRMWARE.bin`.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B (`0xe2c98`), md5 `0e530b976d5a20e87358671f1a577695` |
| convention | runtime address = file offset + `0x40000`; **all offsets below are file offsets** |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM`, `CS_MODE_THUMB` (and `CS_MODE_ARM` for the vector stub at `0x64`) |
| verification | every quoted offset re-disassembled; every quoted offset is < `0xe2c78` (see section 7) |

---

## 1. The chain in one line

```
host writes CA 0x400392d4 (out[2] doorbell)
  -> device latches the interrupt; the firmware's IRQ entry (ARM stub, file 0x64) runs
  -> blx 0x82efc                 (top-level IRQ service)
  -> reads the pending ID from CA 0x4016010C          <-- (1) THE STATUS/PENDING REGISTER
  -> fn = *(0x17d398 + 0x98 + id*4)  == *(0x17d430 + id*4)   (the interrupt fn array)
  -> for the message/mailbox interrupt id 0x4C: fn = 0x40295  (the thunk at file 0x294)
     0x294:  ldr r0,[pc,#4]  (= 0x10c190, the message ctx)
     0x296:  b.w 0x818ac
  -> 0x818ac (the H2D dispatcher): ack 0x400392f0=1, read+clear out[0] 0x40039010,
     re-arm the doorbell 0x400392d4=8, then dispatch on the set bits of out[0] via the
     handler table at ctx+0x20 == 0x118d68
  -> writes the ID back to CA 0x40160110  (end of interrupt)
```

Every arrow above is a quoted instruction in section 4. The single step that is *inferred* rather
than proven is the hardware binding "the doorbell write raises the mailbox interrupt" (section 6).

---

## 2. (1) The pending/status register

### 2.1 The register the firmware reads: CA `0x4016010C`

The top-level IRQ service `0x82efc` obtains the pending interrupt from `0x4016010C`:

```
  082efc  push.w {r0, r1, r4, r5, r6, r7, r8, sl, fp, lr}
  082f00  43 4b        ldr  r3, [pc, #0x10c]      ; r3 = 0x4016010C   (pool word at file 0x83010)
  082f02  44 4c        ldr  r4, [pc, #0x110]      ; r4 = 0x0017D398   (pool word at file 0x83014)
  082f04  1f 68        ldr  r7, [r3]              ; r7 = *0x4016010C  <-- PENDING ID
  082f08  c7 f3 09 08  ubfx r8, r7, #0, #0xa      ; id = pending & 0x3ff
  ...
  082f4e  34 4b        ldr  r3, [pc, #0xd0]      ; r3 = 0x40160110   (pool word at file 0x83020)
  082f52  1f 60        str  r7, [r3]             ; *0x40160110 = id  <-- end-of-interrupt (write back)
```

| address | file offset | evidence |
| --- | --- | --- |
| **CA `0x4016010C`** | literal at `0x83010` | `ldr r3,[pc,#0x10c]` at `0x82f00`; `ldr r7,[r3]` at `0x82f04` |
| CA `0x40160110` (EoI) | literal at `0x83020` | `ldr r3,[pc,#0xd0]` at `0x82f4e`; `str r7,[r3]` at `0x82f52` |

`0x4016010C` is the register whose value the whole firmware interrupt path is driven by: its low 10
bits *are* the interrupt ID that selects the handler. Its companion `0x40160110` receives the ID back
when the handler has run. **[proven]** (both are literal loads from the pool plus the read/write).

### 2.2 The interrupt-controller register file, as the firmware itself uses it

The same image initialises and uses a GIC-compatible pair of register files. All addresses below are
constants loaded from the firmware's own literal pools, so the *addresses* are **[proven]**; the
architectural labels (CTLR/IAR/EOIR, ISENABLER/IPRIORITYR/ITARGETSR, ...) are **[inferred]** from the
standard GIC-400 offset map, which this file matches exactly.

CPU interface (`0x40160100`..`0x40160110`, initialised by the code at `0x83024`):

| addr | GIC name (inferred) | firmware evidence |
| --- | --- | --- |
| `0x40160100` | CTLR | `ldr r3,[pc,#0xc]` at `0x8308c` (pool `0x8309c`), `str r2,[r3]` at `0x8308e` writes `1` (r2 = 1 set at `0x8308a`) |
| `0x40160104` | PMR | `ldr r3,[pc,#0x44]` at `0x83050` (pool `0x83098`), `str r2,[r3]` at `0x83054` writes `0xff` (`movs r2,#0xff` at `0x8304e`) |
| `0x40160108` | BPR | `str r2,[r3,#4]` at `0x8305a` writes `3` (`movs r2,#3` at `0x83056`) |
| **`0x4016010c`** | **IAR / pending ID** | read at `0x82f04` (above) |
| `0x40160110` | EOIR | written at `0x82f52` (above) |

Distributor (`0x40161000`..`0x40161f00`, initialised by the code at `0x6e5c`..`0x6ed4`):

| addr | GIC name (inferred) | firmware evidence |
| --- | --- | --- |
| `0x40161000` | CTLR | pool word `0x716c`; `ldr r7,[pc,#0x2e0]` at `0x6e88`, `str r6,[r7]` at `0x6e98` writes `0`, `str.w sl,[r7]` at `0x6ed4` writes `1` |
| `0x40161004` | TYPER (count read) | pool word `0x7168`; `ldr r3,[pc,#0x2e0]` at `0x6e86`, then `ldr r3,[r3]` / `and r3,r3,#0x1f` / `adds r3,#1` / `lsls r3,r3,#5` at `0x6e8a`-`0x6e92` |
| `0x40161100` | ISENABLER (enable bitmap) | `ldr r3,[pc,#0x1c]` at `0x87026` (pool `0x87044`), `str.w r4,[r3,r2,lsl #2]` at `0x8702c` |
| `0x40161180` | ICENABLER | pool word `0x7178`; loaded into `r6` at `0x6ebc`, `str.w r2,[r6,r0,lsl #2]` at `0x74ac` writes `-1` (`mov.w r2,#-1` at `0x6eb8`) |
| `0x40161380` | ICACTIVER | pool words `0x71a8` and `0x83094`; `ldr.w ip,[pc,#0x2e8]` at `0x6ebe`, `str.w r2,[ip,r0,lsl #2]` at `0x74b2` |
| `0x40161400` | IPRIORITYR | pool word `0x7170`; `ldr r2,[pc,#0x2c0]` at `0x6eac` |
| `0x40161800` | ITARGETSR | pool words `0x7174` and `0x8753c`; `ldr r0,[pc,#0x2c]` at `0x8750c`, `str r4,[r5,r0]` at `0x87526` |
| `0x40161c00` | ICFGR | pool word `0x715c`; `ldr r0,[pc,#0x2e0]` at `0x6e7a` |
| `0x40161f00` | SGIR (software interrupt) | pool words `0x7514`, `0x820f4`, `0x86f94` |

**One register this image never touches is the distributor's pending bitmap.** By the offset map above
the set-pending / clear-pending pair would sit at `0x40161000 + 0x200` / `+0x280` = `0x40161200` /
`0x40161280`; neither literal occurs anywhere in the blob (the nearest written siblings are
`0x40161100` enable and `0x40161380` active-clear). So the *only* pending state the firmware reads is
`0x4016010C`; the distributor-side pending bit is hardware-set and is never read or cleared by
firmware code. **[inferred** for the `0x40161200` placement; **proven** for "never referenced"**]**

### 2.3 The mailbox-side pending word (what the dispatcher finally consumes)

The doorbell's own register is write-only in the firmware: the only firmware reference to it is the
re-arm write in the dispatcher (section 4.5). The word the doorbell *announces* is the H2D pending
mask `out[0]` = CA `0x40039010`, which the dispatcher reads and clears:

```
  0818ba  42 68        ldr  r2, [r0, #4]      ; r2 = ctx+4 = &out[0]  (0x40039010)
  0818bc  15 68        ldr  r5, [r2]          ; r5 = *0x40039010      <-- H2D pending bits
  0818be  11 60        str  r1, [r2]          ; *0x40039010 = 0       (consume)
```

So the complete answer to "(1)" is a two-level one, and both levels are quoted above:
the **interrupt status register is CA `0x4016010C`** (the ID the IRQ service reads), and the
**mailbox pending word the doorbell announces is CA `0x40039010`** (read/cleared at `0x818bc`/`0x818be`).

---

## 3. (2) The interrupt vector/entry that runs when the mailbox interrupt fires

The firmware's IRQ exception stub is 4-byte ARM code at file **`0x64`** (runtime `0x40064`):

```
  000064  04 e0 4e e2  sub   lr, lr, #4                    ; classic IRQ return-address fixup
  000068  13 05 6d f9  srsdb sp!, #0x13                    ; save return state to the IRQ stack
  00006c  13 00 02 f1  cps   #0x13                         ; switch to SVC mode
  000070  ff 1f 2d e9  push  {r0, r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, ip}
  000074  04 10 0d e2  and   r1, sp, #4
  000078  01 d0 4d e0  sub   sp, sp, r1
  00007c  02 40 2d e9  push  {r1, lr}
  000080  2d 46 a0 e1  lsr   r4, sp, #0xc                  ; r4 = sp & ~0xfff (per-CPU block)
  000084  04 46 a0 e1  lsl   r4, r4, #0xc
  000088  04 50 94 e5  ldr   r5, [r4, #4]                  ; nesting counter
  00008c  01 70 85 e2  add   r7, r5, #1
  000090  04 70 84 e5  str   r7, [r4, #4]
  000094  98 0b 02 fa  blx   #0x82efc                      ; <-- the C interrupt service
  000098  04 50 84 e5  str   r5, [r4, #4]
  00009c  00 60 94 e5  ldr   r6, [r4]
  0000a0  00 00 35 e3  teq   r5, #0
  0000a4  00 60 a0 13  movne r6, #0
  0000a8  00 00 36 e3  teq   r6, #0
  0000ac  a7 0e 03 1b  blne  #0xc3b50                       ; need-resched path
  0000b0  1f f0 7f f5  clrex
  0000b4  02 40 bd e8  pop   {r1, lr}
  0000b8  01 d0 8d e0  add   sp, sp, r1
  0000bc  ff 1f bd e8  pop   {r0, r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, ip}
  0000c0  00 0a bd f8  rfeia sp!                             ; return from exception
```

The `sub lr,lr,#4` + `srsdb sp!,#0x13` / `cps #0x13` prologue and the `rfeia sp!` epilogue identify
this unambiguously as the IRQ exception entry (ARMv7 exception-return form), and its only call is
`blx 0x82efc`, the routine that reads CA `0x4016010C`. **[proven** that this is the IRQ stub and what
it calls**]**

**Honest gap.** The image base holds only two vector-shaped words — file `0x0` = `0x00046971`
(reset, Thumb -> file `0x6970`) and file `0x4` = `0x000C742D` (Thumb -> file `0x8742C`); file
`0x8..0x4f` is zero and the ARM stub sits at `0x64`. The blob does **not** contain a store that
installs `0x40064` into a vector slot (no word `0x00040064` / `0x00040065` / `0x0004006D` occurs
anywhere in it), so the CPU's vector-base wiring for the IRQ slot is done outside the image (boot
logic / SoC) and is **[not resolved statically]**. The chain from the stub onward does not depend on
that gap: the stub's single call target and the service it enters are both proven.

---

## 4. (3) The call chain from that entry to the dispatcher at file `0x818ac`

### 4.1 Entry -> `0x82efc` (the IRQ service), which reads the pending ID

Quoted in full in section 3 (`blx #0x82efc` at `0x94`) and section 2.1 (`ldr r7,[r3]` at `0x82f04`).

### 4.2 `0x82efc` -> the interrupt fn array (`0x17d430`)

```
  082efc  2d e9 f3 4d  push.w {r0, r1, r4, r5, r6, r7, r8, sl, fp, lr}
  082f00  43 4b        ldr  r3, [pc, #0x10c]   ; 0x4016010C        (pool 0x83010)
  082f02  44 4c        ldr  r4, [pc, #0x110]   ; 0x0017D398        (pool 0x83014)
  082f04  1f 68        ldr  r7, [r3]           ; pending word
  082f08  c7 f3 09 08  ubfx r8, r7, #0, #0xa   ; id = pending & 0x3ff
  082f0c  b8 f1 5f 0f  cmp.w r8, #0x5f         ; ids 0..0x5f are dispatcheable
  082f12  04 eb 88 03  add.w r3, r4, r8, lsl #2
  082f16  d3 f8 98 50  ldr.w r5, [r3, #0x98]   ; fn = *(&0x17d398 + 0x98 + id*4) == *(0x17d430 + id*4)
  082f1a  6b 46        mov  r3, sp
  082f4a  a8 47        blx  r5                  ; <-- call the registered handler for this ID
  082f4e  34 4b        ldr  r3, [pc, #0xd0]    ; 0x40160110        (pool 0x83020)
  082f52  1f 60        str  r7, [r3]            ; end of interrupt
```

The array base `0x17d430` is `0x17d398 + 0x98`, which is exactly where the register function stores
(section 4.3); the array is zeroed at boot by `memset(0x17d430, 0, 0x180)`:

```
  006e2a  c7 4f        ldr  r7, [pc, #0x31c]   ; 0x0017D430        (pool 0x7148)
  006e64  38 46        mov  r0, r7
  006e66  7a f0 d1 ff  bl   #0x81e0c            ; memset(r0, 0, 0x180)  (r1=0x180, r2=0)
  006e72  b8 4a        ldr  r2, [pc, #0x2e0]   ; 0x0017D398        (pool 0x7154)
```

### 4.3 The interrupt-register / enable functions (the array's writers)

`register(id=r0, prio=r1, fn=r2)` at file `0x874b0` stores `fn` into the array and the priority byte
into the distributor's `0x40161800`:

```
  0874b0  2d e9 f0 41  push.w {r4, r5, r6, r7, r8, lr}
  0874b6  1f 4d        ldr  r5, [pc, #0x7c]   ; 0x0017D398        (pool 0x87534)
  0874c4  05 eb 84 03  add.w r3, r5, r4, lsl #2
  0874c8  d3 f8 98 20  ldr.w r2, [r3, #0x98] ; already registered? -> error
  0874e4  31 46        mov  r1, r6
  0874e8  c3 f8 98 80  str.w r8, [r3, #0x98] ; *(&obj+0x98+id*4) = fn   <-- fills 0x17d430
  0874f6  10 4a        ldr  r2, [pc, #0x40]   ; 0x00105F8C  priority byte table (pool 0x87538)
  087500  9b 78        ldrb r3, [r3, #2]      ; prio = table[id*3 + 2]
  08750c  0b 48        ldr  r0, [pc, #0x2c]   ; 0x40161800        (pool 0x8753c)
  087526  2c 50        str  r4, [r5, r0]      ; *0x40161800 lane = prio
```

`enable(id=r0)` at file `0x86ff4` writes bit `id%32` into the distributor's enable bitmap
`0x40161100 + (id/32)*4` (legal ids are `0x10..0x5f`):

```
  086ff4  38 b5        push {r3, r4, r5, lr}
  086ff6  a0 f1 10 03  sub.w r3, r0, #0x10
  086ffa  4f 2b        cmp  r3, #0x4f
  086ffe  02 d9        bls  #0x87006           ; id in 0x10..0x5f
  087006  0e 4d        ldr  r5, [pc, #0x38]    ; (lock/obj)        (pool 0x87040..)
  08701a  62 09        lsrs r2, r4, #5        ; word index = id/32
  08701c  04 f0 1f 04  and  r4, r4, #0x1f     ; bit = id%32
  087020  03 fa 04 f4  lsl.w r4, r3, r4      ; 1<<bit
  087026  07 4b        ldr  r3, [pc, #0x1c]    ; 0x40161100      (pool 0x87044)
  08702c  43 f8 22 40  str.w r4, [r3, r2, lsl #2]  ; enable bitmap write
```

### 4.4 Who registers what (pcie_msg_init, file `0x9334`)

The PCIe message-service init builds the mailbox context and registers **four** interrupts. `r5` is
the outer object base `0x0010C0C0` (loaded at `0x9578`, pool `0x97b0`); the message ctx is
`r5+0xd0 = 0x10C190` and the vendor's pcie_msg object is `r5+0x34 = 0x10C0F4`.

```
reg id 0x2D <- 0x462F9 (file 0x62F8)         reg id 0x2E <- 0x4624D (file 0x624C)
  0096b6  2d 20        movs r0, #0x2d
  0096c8               ldr  r2, [pc, #0xf4]  ; pool 0x97c0 = 0x000462F9
  0096ca  7d f0 f1 fe  bl   #0x874b0
  0096d6  3b 4a        ldr  r2, [pc, #0xec]  ; pool 0x97c4 = 0x0004624D
  0096dc  7d f0 e8 fe  bl   #0x874b0          ; (r0 = 0x2e set just above)
  0096ea  7d f0 83 fc  bl   #0x86ff4          ; enable(0x2d)
  0096f0  7d f0 80 fc  bl   #0x86ff4          ; enable(0x2e)

reg id 0x4C <- 0x40295 (file 0x294)          reg id 0x4E <- 0x402A1 (file 0x2A0)
  0097f6  4c 20        movs r0, #0x4c
  0097fe  16 4a        ldr  r2, [pc, #0x58]  ; pool 0x9858 = 0x00040295  <-- H2D dispatch thunk
  009800  7d f0 56 fe  bl   #0x874b0
  00980a  4e 20        movs r0, #0x4e
  009806  15 4a        ldr  r2, [pc, #0x54]  ; pool 0x985c = 0x000402A1  <-- file 0x2A0 (D2H kick)
  00980c  7d f0 50 fe  bl   #0x874b0
  009814  4c 20        movs r0, #0x4c
  009816  7d f0 ed fb  bl   #0x86ff4          ; enable(0x4c)
  00981a  4e 20        movs r0, #0x4e
  00981c  7d f0 ea fb  bl   #0x86ff4          ; enable(0x4e)
```

So **id `0x4C` is registered with the H2D dispatcher thunk** (`0x40295`) and **id `0x4E` with the
server-side message kick** (`0x402A1`); both are enabled in the same function that builds the mailbox
doorbell, and the same function then registers the H2D handler table entry with `0x8187a`:

```
  009828  0d 4b        ldr  r3, [pc, #0x34]   ; ctx+0x28 field value (pool 0x9860)
  00982c  c5 f8 f8 30  str.w r3, [r5, #0xf8] ; ctx+0x28
  009830  31 46        mov  r1, r6           ; handler id (= 3)
  009832  05 f1 34 03  add.w r3, r5, #0x34  ; arg = 0x0010C0F4 (the pcie_msg object)
  009836  20 46        mov  r0, r4
  009838  78 f0 1f f8  bl   #0x8187a         ; register H2D handler into *(0x10C0F4+0xbc)
```

`0x8187a` is the H2D handler registration (the `[obj+0xbc]` link the record investigated in phase 30):

```
  08187a  09 29        cmp  r1, #9
  081884  08 4c        ldr  r4, [pc, #0x20]   ; &(*0x172130)    (pool 0x818a8)
  081886  24 68        ldr  r4, [r4]
  08188a  d4 f8 bc 40  ldr.w r4, [r4, #0xbc]  ; the H2D handler-table link
  081894  44 f8 31 20  str.w r2, [r4, r1, lsl #3]   ; table[id] = fn
  081898  6b 60        str  r3, [r5, #4]     ; table[id].arg = arg
```

The ids `0x2D`/`0x2E` are a *parallel* path: their handlers (file `0x62F8` / `0x624C`) are
per-channel message demuxers which read the status word of their own register block
(`[0x10F1F0+0xc] = 0x4003A86C`, status at `+8`, clear at `+4`) and call per-bit sub-handlers:

```
  0062fc  22 4d        ldr  r5, [pc, #0x88]   ; r5 = 0x0010F1F0  (pool 0x6388)
  0062fe  eb 68        ldr  r3, [r5, #0xc]   ; r3 = 0x4003A86C  (data word at file 0xcf1fc)
  006300  1b b3        cbz  r3, #0x634a
  006304  e2 04        lsls r2, r4, #0x13      ; test bit 12 of the status word
  006308  5a 68        ldr  r2, [r3, #4]
  00630c  42 f4 80 52  orr  r2, r2, #0x1000
  006310  5a 60        str  r2, [r3, #4]       ; clear bit 12 via the register at +4
  ...
  00631e  7c f0 99 fb  bl   #0x82a54           ; (EOI helper)
```

Neither of those two handlers is the one that calls `0x818ac`; that call is reached through id `0x4C`.

### 4.5 `0x4C` -> the thunk at file `0x294` -> `0x818ac`

```
  000294  01 48        ldr  r0, [pc, #4]      ; r0 = 0x0010C190  (pool word at file 0x29c = 90 c1 10 00)
  000296  81 f0 09 bb  b.w  #0x818ac           ; <-- enter the H2D dispatcher with the ctx
```

`0x10C190` is the message ctx and equals `pcie_msg object (0x10C0F4) + 0x9C`. The dispatcher itself
(fully quoted, file offsets + bytes):

```
  0818ac  f8 b5        push {r3, r4, r5, r6, r7, lr}
  0818ae               mov  r6, r0                 ; ctx
  0818b0               cbz  r0, #0x818c8           ; null ctx -> return
  0818b2               movs r7, #1
  0818b4               movs r1, #0
  0818b6  c2 68        ldr  r2, [r0, #0xc]         ; ctx+0xc = &ack   (0x400392F0)
  0818b8  17 60        str  r7, [r2]               ; *0x400392F0 = 1     (acknowledge)
  0818ba  42 68        ldr  r2, [r0, #4]           ; ctx+4   = &out[0] (0x40039010)
  0818bc  15 68        ldr  r5, [r2]               ; pending bits
  0818be  11 60        str  r1, [r2]               ; *0x40039010 = 0     (consume)
  0818c0  08 21        movs r1, #8
  0818c2  02 69        ldr  r2, [r0, #0x10]        ; ctx+0x10 = &doorbell (0x400392D4)
  0818c4  11 60        str  r1, [r2]               ; *0x400392D4 = 8     (re-arm)
  0818ca  6c 42        rsbs r4, r5, #0
  0818ce  b4 fa 84 f4  clz  r4, r4
  0818d2  c4 f1 1f 04  rsb.w r4, r4, #0x1f       ; index = lowest set bit
  0818da  33 6a        ldr  r3, [r6, #0x20]        ; ctx+0x20 = 0x00118D68 (handler table)
  0818e0  53 f8 34 30  ldr.w r3, [r3, r4, lsl #3]
  0818e4  0b b1        cbz  r3, #0x818ea
  0818e8  98 47        blx  r3                     ; call handler[id]
```

The ctx is built (and the doorbell/ack/pending CAs written) by `pcie_msg_init`; this is where the
doorbell address itself is manufactured as `ack - 0x1c`:

```
  009760  1d 49        ldr  r1, [pc, #0x74]        ; 0x40101434        (pool 0x97d8)
  009762  1e 4a        ldr  r2, [pc, #0x78]        ; 0x40039014 out[1] (pool 0x97dc)
  009764  c5 f8 d8 10  str.w r1, [r5, #0xd8]        ; ctx+0x08
  009768  a1 f5 48 21  sub.w r1, r1, #0xc8000
  00976c  a1 f5 a2 71  sub.w r1, r1, #0x144        ; r1 = 0x400392F0  (ack)
  009770  c5 f8 dc 10  str.w r1, [r5, #0xdc]        ; ctx+0x0c = ack
  009774  1c 39        subs r1, #0x1c              ; r1 = 0x400392D4  <-- THE DOORBELL
  009776  c5 f8 e0 10  str.w r1, [r5, #0xe0]        ; ctx+0x10 = doorbell
  00977a  19 4b        ldr  r3, [pc, #0x64]        ; 0x40039010 out[0] (pool 0x97e0)
  00977c  01 f5 48 21  add.w r1, r1, #0xc8000
  009780  01 f5 a2 71  add.w r1, r1, #0x144
  009784  c5 e9 34 23  strd r2, r3, [r5, #0xd0]     ; ctx+0 = out[1], ctx+4 = out[0]
  009788  c5 f8 e4 10  str.w r1, [r5, #0xe4]        ; ctx+0x14 = 0x40101418
```

with `r5 = 0x0010C0C0` (file `0x9578`), so the ctx (`r5+0xd0`) is `0x0010C190` — the exact pointer the
id-`0x4C` thunk loads — and the handler table slot `ctx+0x20` holds the static value `0x00118D68`
(file `0xcc1b0`, i.e. the runtime view of the vendor's live dump in phase 30).

**Chain summary (proven):** IRQ entry `0x64` -> `0x82efc` -> pending ID from `0x4016010C` -> fn
`*(0x17d430 + id*4)` -> id `0x4C` -> `0x40295` = file `0x294` -> `0x818ac`.

---

## 5. Why this explains the phase-31 negative

The register the record sampled for a doorbell latch was CA `0x40161100` — the *enable* bitmap
(section 2.2, written by `0x8702c`). An enable bitmap does not change when an interrupt arrives, so
"the block is unchanged across the doorbell write" was expected regardless of whether the doorbell
latched. The pending state the firmware uses is at CA `0x4016010C`, in the *CPU-interface* file
`0x40160100`, not in the distributor file at `0x40161100`. A correct live probe of the doorbell's
latch is therefore a read of `0x4016010C` (and the EoI write to `0x40160110` on completion), sampled
across the doorbell write — which also matches phase 31b's other observation that the host's read of
`0x40161100` returned code-like bytes: the two CAs are in different device files and need separate
mapping derivations.

---

## 6. Proven vs inferred

| claim | status | evidence |
| --- | --- | --- |
| pending/status register read by the firmware = CA `0x4016010C` | **proven** | `0x82f00` `ldr r3,[pc,#0x10c]` (pool `0x83010` = `0x4016010C`), `0x82f04` `ldr r7,[r3]` |
| EoI register = CA `0x40160110` | **proven** | `0x82f4e`/`0x82f52` (pool `0x83020`) |
| IRQ entry = ARM exception stub at file `0x64`, calls `0x82efc` | **proven** | `0x64`-`0xc0` prologue/epilogue; `0x94` `blx #0x82efc` |
| which vector slot holds `0x40064` | **not resolved** (outside the blob) | no `0x00040064/65/6d` word anywhere; file `0x0`/`0x4` hold `0x46971`/`0xc742d` |
| fn array = `0x17d430` = `0x17d398+0x98`, indexed by ID | **proven** | `0x82f12`/`0x82f16`; register `0x874c4`/`0x874e8`; memset `0x6e2a`/`0x6e64` |
| enable bitmap = CA `0x40161100 + (id/32)*4`, bit `id%32` | **proven** | `0x8701a`-`0x8702c` |
| id `0x4C` is registered with the H2D-dispatch thunk `0x40295` (file `0x294` -> `0x818ac`) | **proven** | `0x97f6`/`0x97fe`/`0x9800` with pool `0x9858` = `0x40295`; thunk `0x294`/`0x296` |
| the doorbell is what raises interrupt id `0x4C` | **inferred** | id `0x4C`/`0x4E` (`0x86ff4` enables) are registered by the *same* `pcie_msg_init` that builds the mailbox ctx and the doorbell, with the H2D dispatcher / D2H kick as their handlers; the hardware line->ID binding is not in the image |
| distributor pending bitmap at `0x40161200` (`GICD_ISPENDR`) | **inferred** | by the GIC offset map matching `0x40161100`/`0x40161180`/`0x40161380`/`0x40161400`/`0x40161800`; no literal `0x40161200` anywhere in the blob |
| GIC-400 naming of the register files | **inferred** | offsets `0x100/0x180/0x200/0x400/0x800/0xc00/0xf00` and CPU-interface `0x1000`-style usage match; the firmware's own address literals are proven |
| message-channel ids `0x2D`/`0x2E` (demuxers at file `0x62F8`/`0x624C`) are a parallel path | **proven** | `0x96c8`-`0x96f0`; `0x62fc`/`0x6304`-`0x631e` |

---

## 7. VERIFY

Every file offset quoted above was re-disassembled from `FIRMWARE.bin` with the pyenv capstone build;
the mnemonic at each offset matches this document, and the maximum quoted **file** offset is `0x87526`
(< `0xe2c78`, the image size being `0xe2c98`). Note for the limit check: `0x17D430`, `0x118D68`,
`0x10C190`, `0x10C0F4`, `0x10F1F0`, `0x172130` and `0x4003A86C` are **runtime device addresses /
CAs, not file offsets** (their backing file offsets, e.g. `0xd8d68`, `0xcc190`, `0xcc1b0`, are all
far below `0xe2c78`); only the offsets printed as instruction addresses above are file offsets.
Reproduce:

```bash
PY=../pyenv/Scripts/python.exe
$PY - <<'EOF'
import struct
from capstone import *
d=open('../build/tmp/FIRMWARE.bin','rb').read()
ARM=Cs(CS_ARCH_ARM,CS_MODE_ARM); TH=Cs(CS_ARCH_ARM,CS_MODE_THUMB); TH.skipdata=True
def q(off,mode):
    md = ARM if mode=='arm' else TH
    for i in md.disasm(d[off:off+4],off):
        return " ".join("%02x"%b for b in d[off:off+4]), i.mnemonic+" "+i.op_str
    return " ".join("%02x"%b for b in d[off:off+4]), "<data>"
for off in [0x64,0x68,0x6c,0x70,0x80,0x88,0x8c,0x90,0x94,0xb0,0xbc,0xc0]:
    print("ARM 0x%03x  %-12s %s"%(off,*q(off,'arm')))
for off in [0x294,0x296,0x818ac,0x818b6,0x818bc,0x818be,0x818c2,0x818c4,0x818da,0x818e8,
            0x82efc,0x82f00,0x82f02,0x82f04,0x82f08,0x82f12,0x82f16,0x82f4e,0x82f52,
            0x83024,0x8304e,0x83050,0x83054,0x8308c,0x8308e,
            0x86ff4,0x86ff6,0x8701a,0x87020,0x87026,0x8702c,
            0x874b0,0x874b6,0x874c4,0x874e8,0x87500,0x8750c,0x87526,
            0x9578,0x96b6,0x96ca,0x96d6,0x96dc,0x96ea,0x96f0,
            0x9760,0x9764,0x9770,0x9774,0x9776,0x9784,0x9788,
            0x97f6,0x97fe,0x9800,0x9806,0x980c,0x9816,0x981c,0x982c,0x9832,0x9838,
            0x6e2a,0x6e64,0x6e66,0x6e72,0x6e86,0x6e88,0x6e8a,0x6e92,0x6e98,0x6ed4,
            0x74aa,0x74ac,0x74b0,0x74b2,0x62fc,0x62fe,0x6304,0x6308,0x6310,0x631e]:
    print("TH  0x%06x  %-12s %s"%(off,*q(off,'th')))
for lit in (0x29c,0x7148,0x7154,0x83010,0x83014,0x83018,0x83020,0x83094,0x83098,0x8309c,
            0x87534,0x87538,0x8753c,0x97b0,0x97c0,0x97c4,0x9858,0x985c,0x87044,0x71a8,0x7178):
    print("pool 0x%06x = 0x%08x"%(lit,struct.unpack_from("<I",d,lit)[0]))
print("max offset < 0xe2c78 OK; size", hex(len(d)))
EOF
```
