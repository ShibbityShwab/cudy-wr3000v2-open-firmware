# service-thread-gate: the H2D dispatcher's only caller is interrupt 0x4c, and the firmware is already armed for it (phase 32, 2026-10-04)

Task `st_01a1056d`. Read-only static analysis of `build/tmp/FIRMWARE.bin`
(md5 `0e530b976d5a20e87358671f1a577695`, 928,920 B) against the record
(`docs/phase30/the-gate-located.md`, `docs/phase31/doorbell-no-latch.md`,
`docs/phase31/doorbell-8-silent.md`, `docs/phase22/fw-hostmem.md`,
`docs/phase20/fw-accept.md`, `docs/phase17/fw-download.md`, `docs/phase18/inbound-map.md`).

One question: **what device-side event calls the H2D dispatcher (file `0x818ac`), and what has
to be true for it to happen?** Every offset quoted below disassembles to the instruction claimed;
the reproduction script is at the end. Nothing was run against the router and nothing was written
except this file.

---

## Verdict (one paragraph)

The dispatcher has **exactly one caller in the whole image**: the firmware's own interrupt-
controller ISR at file **`0x82efc`**, via the per-id handler slot for **interrupt id `0x4c`**. The
slot holds the 12-byte stub at file **`0x294`**, which is a tail-call wrapper
(`ldr r0, =0x0010C190 ; b.w #0x818ac`). `pcie_msg_init` installs that stub and enables id `0x4c`
itself (`register(0x4c, prio=5, fn=0x40295)` at file **`0x9800`**; `enable(0x4c)` at files
**`0x9756`** and **`0x9816`**). So the message service is **not a thread and has no polling
flag**: it is a pure ISR. The wake condition is **"interrupt id `0x4c` is the active id latched in
the device interrupt controller's register CA `0x4016010c`"**; the input that satisfies it is the
**mailbox's H2D message interrupt being asserted on line `0x4c`**. The firmware half of that gate
is fully programmed in a takeover (the same code that runs the other registrations runs these);
what is never fulfilled is the **assertion** -- the doorbell is consumed by the mailbox but no id
`0x4c` ever appears at CA `0x4016010c`, so `blx r5` never executes and `0x818ac` is never entered.

This also corrects one thing the record assumed: the ids the phase-30 instrument saw registered
(`0x2d = 0x462f9`, `0x2e = 0x4624d`) are the **per-channel receive** handlers, **not** the H2D
dispatcher. The H2D dispatcher sits at id **`0x4c`**, one slot the phase-30 read did not cover.

---

## 0. Method and sources

| item | value | use |
| --- | --- | --- |
| `build/tmp/FIRMWARE.bin` | 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` | the device image |
| address convention | runtime = file offset + `0x40000` (phase 4/6/8) | pointer literals in the blob are runtime addresses |
| `pyenv/Scripts/python.exe`, capstone 5.0.7 `CS_ARCH_ARM, CS_MODE_THUMB` | | disassembly |
| sibling phase-32 notes | `docs/phase32/irq-block-mapping.md`, `docs/phase32/doorbell-endpoint-path.md` | CA `0x4016xxxx` is not host-visible; vendor rings the doorbell through EP1, the port through EP0 |

All firmware addresses below are **file offsets** unless marked `rt` (= +`0x40000`). Quotes give the
file offset, the raw bytes, and the capstone text. `[...]` after an `ldr` is the resolved
pc-relative literal and its raw value.

---

## 1. The dispatcher has exactly one caller, and it is the ISR

### 1.1 There is exactly one branch to `0x818ac` in the image

A linear sweep of every `b`/`bl` in `FIRMWARE.bin` finds a single branch whose target is the
dispatcher:

```
000296 rt040296  b.w      #0x818ac        bytes=81 f0 09 bb
```

Nothing else branches there, and no 4-byte literal anywhere in the image holds
`0x000c18ac`/`0x000c18a8` -- the dispatcher is never reached by a stored function pointer either.
The one path is indirect through the interrupt-controller handler array (section 3).

### 1.2 The caller is a 12-byte stub at file `0x294`

Files `0x284`..`0x2b0` are a table of naked thunks of the shape "load an object pointer, tail-call
a routine"; `0x294` is one of them:

```
000294 rt040294  ldr      r0, [pc, #4]              bytes=01 48 81 f0
000296 rt040296  b.w      #0x818ac                  bytes=81 f0 09 bb
000298 rt040298  nop
00029c rt04029c  (pool)                             bytes=90 c1 10 00   -> 0x0010C190
```

`ldr r0,[pc,#4]` at `0x294` resolves to the literal at `0x29c`: **r0 = `0x0010C190`**, then
`b.w 0x818ac`. So the stub is literally `pcie_msg_handle_caller(0x10C190)` -- the dispatcher's
context argument is the fixed runtime address `0x10C190`.

Its sibling at `0x2a0` is the other message stub, used by interrupt `0x4e` (section 3.3):

```
0002a0 rt0402a0  ldr      r0, [pc, #4]              bytes=01 48 85 f0
0002a2 rt0402a2  b.w      #0x86108                  bytes=85 f0 31 bf
0002a8 rt0402a8  (pool)                             bytes=f4 c0 10 00   -> 0x0010C0F4
```

### 1.3 Why `0x10C190` and not `0x10C0F4`

The blob's initialized data holds the whole static "message object" at runtime `0x10C0C0`
(file `0xCC0C0`, inside the loaded image):

* `pcie_msg_init` loads the object base `r5 = 0x0010C0C0` from the pool at file `0x97b0`
  (`bytes= c0 c0 10 00`).
* `r5 + 0x34 = 0x0010C0F4` is the pointer the record already knows: `*0x172130` and the
  `handler[3]` argument in the phase-30 live dump.
* `r5 + 0xD0 = 0x0010C190` is the **dispatcher context** -- the stub's argument.

The dispatcher's own field offsets bind exactly onto `r5+0xD0`:

| dispatcher read | CTX offset | `r5+` | CA value written by `pcie_msg_init` (file) |
| --- | --- | --- | --- |
| `ldr r2,[r0,#4]`  -- pending, read+cleared | `0x10C194` | `+0xD4` | `0x40039010` (out[0]); literal at file `0x97e0`, stored at `0x9784` |
| `ldr r2,[r0,#0xc]` -- ack `=1`             | `0x10C19C` | `+0xDC` | `0x400392F0`; computed at files `0x9768`/`0x976c` (`0x40101434-c8000-144`), stored at `0x9770` |
| `ldr r2,[r0,#0x10]` -- doorbell `=8`       | `0x10C1A0` | `+0xE0` | `0x400392D4`; computed at file `0x9774` (`-0x1c`), stored at `0x9776` |
| `ldr r3,[r6,#0x20]` -- handler table        | `0x10C1B0` | `+0xF0` | `0x00118D68` (static data, file `0xCC1B0`) |

`CTX+0x20 = 0x10C1B0` is byte-identical to the phase-30 `[*0x172130 + 0xbc]` slot, since
`0x10C0F4 + 0xBC = 0x10C1B0`. That closes the identification: the stub hands the dispatcher the
right object, and the record's `[pcie_msg+0xbc]` observation is the same word the dispatcher's
`ctx+0x20` is.

For completeness, the dispatcher body (already recovered in phase 20, re-quoted here so the
condition below is self-contained):

```
0818ac rt0c18ac  push     {r3, r4, r5, r6, r7, lr}
0818b6 rt0c18b6  ldr      r2, [r0, #0xc]        ; -> CA 0x400392F0
0818b8 rt0c18b8  str      r7, [r2]              ; *ack = 1        (r7=1)
0818ba rt0c18ba  ldr      r2, [r0, #4]          ; -> *out[0] 0x40039010
0818bc rt0c18bc  ldr      r5, [r2]              ; r5 = pending mask
0818be rt0c18be  str      r1, [r2]              ; *out[0] = 0     (r1=0)
0818c0 rt0c18c0  movs     r1, #8
0818c4 rt0c18c4  str      r1, [r2]              ; re-arm doorbell 0x400392D4 = 8
0818da rt0c18da  ldr      r3, [r6, #0x20]       ; handler table = 0x00118D68
0818e8 rt0c18e8  blx      r3                    ; handler(arg = table[id].arg)
```

---

## 2. The registration: `pcie_msg_init` installs the stub for interrupt `0x4c`

The whole receive chain is built by the function at file `0x9334` (`pcie_msg_init`). Its
registration block, with the literal pool it uses (the pool is between `0x97a8` and `0x97f2`, jumped
over by `b #0x97f4` at file `0x97a6`):

```
0096b2 movs r1, #5                         ; prio 5
0096c8 ldr  r2, [pc, #0xf4]   ; -> 0x97c0 = 0x000462F9
0096ca bl   #0x874b0                       ; register(0x2d, 5, 0x462F9)   <- per-channel
0096d8 movs r1, #5
0096dc bl   #0x874b0                       ; register(0x2e, 5, 0x4624D)   <- per-channel
0096e8 movs r0, #0x2d
0096ea bl   #0x86ff4                       ; enable(0x2d)
0096ee movs r0, #0x2e
0096f0 bl   #0x86ff4                       ; enable(0x2e)

009754 movs r0, #0x4c
009756 bl   #0x86db8                       ; enableA(0x4c)
00975a movs r0, #0x4e
00975c bl   #0x86db8                       ; enableA(0x4e)
...
0097f6 movs r0, #0x4c
0097fe ldr  r2, [pc, #0x58]   ; -> 0x9858 = 0x00040295   == the stub at file 0x294
009800 bl   #0x874b0                       ; register(0x4c, r1=5, fn=0x40295)  <-- THE DISPATCHER
009806 ldr  r2, [pc, #0x54]   ; -> 0x985c = 0x000402A1   == the stub at file 0x2a0
009808 movs r1, #5
00980a movs r0, #0x4e
00980c bl   #0x874b0                       ; register(0x4e, 5, fn=0x402A1)
009814 movs r0, #0x4c
009816 bl   #0x86ff4                       ; enable(0x4c)
```

`r1` at `0x97fe` is the `movs r1,#5` from `0x979e` (file `0x979e`), which is why the record's
live `handler[3]`/other entries carry their priorities. `0x874b0` is the registration function; it
writes the handler into the per-id array (section 3.1). So:

* **id `0x4c` -> `0x00040295` -> stub `0x294` -> dispatcher `0x818ac`** (the H2D path), and
* **id `0x4e` -> `0x000402A1` -> stub `0x2a0` -> `0x86108`** (the D2H-completion sibling).

The record's phase-30 ids `0x2d`/`0x2e` are the **per-channel receive** handlers
(`0x000462F9` -> file `0x62f8`, `0x0004624D` -> file `0x624c`); they never touch `0x818ac`.

---

## 3. The waiter: the device interrupt-controller ISR at file `0x82efc`

### 3.1 The handler array

`register` (`0x874b0`) and the ISR both index a fixed runtime array:

```
0874b6 ldr   r5, [pc, #0x7c]   ; -> 0x87534 = 0x0017D398   (the intc object base)
0874c4 add.w r3, r5, r4, lsl #2
0874c8 ldr.w r2, [r3, #0x98]   ; existing handler for id r4
0874e8 str.w r8, [r3, #0x98]   ; fn_array[id] = r8   (r8 = the fn argument)
0874ec bl    #0x81a3c          ; intc program (priority/route, CA 0x40161400 block)
08750c ldr   r0, [pc, #0x2c]   ; -> 0x8753c = 0x40161800
087526 str   r4, [r5, r0]      ; priority byte for the id (4 ids per word)
```

so `fn_array[id] = [0x0017D398 + 0x98 + id*4]`. This is exactly the record's
"interrupt fn array `0x17d430`" (`0x17D398 + 0x98 = 0x17D430`), and `fn_array[0x4c]` lives at
runtime `0x17D560`. The array is in RAM (runtime `0x17D430` is past the loaded image, which ends at
runtime `0x122D98`), so its contents are produced at run time by these `str.w` writes, not by the
file.

### 3.2 The ISR body

```
082efc rt0c2efc  push.w  {r0, r1, r4, r5, r6, r7, r8, sl, fp, lr}
082f00 rt0c2f00  ldr     r3, [pc, #0x10c]   ; -> 0x83010 = 0x4016010C  <-- ACTIVE-ID REGISTER
082f04 rt0c2f04  ldr     r7, [r3]           ; r7 = active-id word
082f08 rt0c2f08  ubfx    r8, r7, #0, #0xa   ; r8 = active id (10 bits)
082f0c rt0c2f0c  cmp.w   r8, #0x5f
082f12 rt0c2f12  add.w   r3, r4, r8, lsl #2 ; r4 = 0x0017D398 (from [0x83014])
082f16 rt0c2f16  ldr.w   r5, [r3, #0x98]     ; r5 = fn_array[id]
...
082f46 rt0c2f46  cpsie   i
082f48 rt0c2f48  cbz     r5, #0x82f4c
082f4a rt0c2f4a  blx     r5                 ; <-- CALL THE HANDLER
082f4c rt0c2f4c  cpsid   i
082f4e rt0c2f4e  ldr     r3, [pc, #0xd0]    ; -> 0x83020 = 0x40160110
082f52 rt0c2f52  str     r7, [r3]           ; EOI: write the id back
```

Pool: `0x83010 = 0x4016010C`, `0x83014 = 0x0017D398`, `0x83020 = 0x40160110`.

A scan of every `ldr.w rX,[rY,#0x98]` in the image finds exactly two that index the intc object
base `0x0017D398` -- this ISR (file `0x82f16`) and `register` (file `0x874c8`); every other hit
uses a different object. So the ISR is the **only consumer** of `fn_array`. It is an exception
entry, not `bl`-called by anything; the firmware's boot code starts the controller and seeds the
array:

```
006e2a ldr  r7, [pc, #0x31c]   ; -> 0x7148 = 0x0017D430   (the fn array address)
006e78 ldr  r3, [pc, #0x2dc]   ; -> 0x7158 = 0x000C2081   (fn[0])
006e7c str  r3, [r7]
006e7e ldr  r3, [pc, #0x2e0]   ; -> 0x7160 = 0x000C20A3   (fn[1])
006e80 str  r3, [r7, #4]
006e82 ldr  r3, [pc, #0x2e0]   ; -> 0x7164 = 0x000C6C89   (fn[0x1d])
006e84 str  r3, [r7, #0x74]
006eda bl   #0x83024           ; intc_init: clears the register file
...
08308a movs r2, #1
08308c ldr  r3, [pc, #0xc]     ; -> 0x8309c = 0x40160100
08308e str  r2, [r3]           ; *0x40160100 = 1   <-- start the controller
```

### 3.3 The waiting primitive

There is **no software wait on the H2D path** -- no semaphore, no event flag, no `wfe`/`wfi` in or
around the dispatcher. The "wait" is the CPU's IRQ exception: the ISR above runs on the device's
exception entry, reads the active id from hardware, and tail-calls the handler. Two wait-like
instructions exist nearby but are not the gate:

* `cpsie i` / `cpsid i` at file `0x82f46` / `0x82f4c` simply open and close the IRQ window around
  the handler call.
* `wfi` at file `0x86bd0` (`bytes= 30 bf 3e 46`) idles the firmware's ring-service loop when it has
  nothing to do; it is not on the dispatcher path.

So the waiting primitive is **the interrupt controller's active-id register CA `0x4016010C` plus
the CPU's IRQ line** -- the service blocks on hardware, not on a flag it can poll.

---

## 4. The exact condition

The dispatcher runs iff **all** of the following hold:

1. `*CA 0x4016010C` (read at file `0x82f04`) has `id = value & 0x3FF == 0x4C` (id `0x4c` must also
   be `<= 0x5F`, the bound tested at file `0x82f0c`).
2. `fn_array[0x4c] != 0` (tested at file `0x82f48`); the firmware writes `0x00040295` there at
   file `0x9800` through `0x874b0`'s `str.w r8,[r3,#0x98]` (file `0x874e8`).
3. Then `blx r5` (file `0x82f4a`) enters the stub at file `0x294`, which tail-calls `0x818ac`
   with `ctx = 0x0010C190`.

Equivalently: **interrupt id `0x4c` must be the active id presented by the device interrupt
controller.** The EOI (`str r7,[0x40160110]`, file `0x82f52`) then retires it.

---

## 5. Which input would satisfy it

The immediate input is a **register**: CA `0x4016010C` must present `0x4C`. That register is
driven by the **mailbox's H2D message interrupt**, not by firmware code -- the blob never writes
`0x4016010C` anywhere (its only reference is the ISR's read). Two firmware-side enables must also
be set; both are already written by `pcie_msg_init`:

| where | instruction | effect |
| --- | --- | --- |
| `enableA` at file `0x86db8`, called from `0x9756` (`movs r0,#0x4c`) | `086ddc movs r3,#1` / `086de0 and r4,r4,#0x1f` / `086de4 lsl.w r4,r3,r4` / `086de8 ldr r3,[pc,#0x20]` (=`0x40161180`) / `086dea str.w r4,[r3,r2,lsl #2]` / `086dee add.w r3,r3,#0x100` / `086df2 str.w r4,[r3,r2,lsl #2]` | bit `id%32` into CA `0x40161180 + (id/32)*4`, and the mirror `0x40161280 + (id/32)*4`. For id `0x4c`: word 2, bit 12 -> **CA `0x40161188 = 0x00001000`** and `0x40161288 = 0x1000` |
| `enable` at file `0x86ff4`, called from `0x9816` (`movs r0,#0x4c`) | `087014 cmp r4,r3` / ... / `087026 ldr r3,[pc,#0x1c]` (=`0x40161100`) / `08702c str.w r4,[r3,r2,lsl #2]` | bit `id%32` into CA `0x40161100 + (id/32)*4`. For id `0x4c`: **CA `0x40161108 = 0x00001000`** |

Two consequences that matter for the record's measurements:

* **The record sampled the wrong word.** Phase 30 read CA `0x40161100` and reasoned about word 1
  bits 13/14 (`0x40161104 = 0x6000`) because it believed the message ids were `0x2d`/`0x2e`.
  The H2D dispatcher's id is `0x4c`, whose enable word is **word 2** -- `0x40161108` (via
  `0x86ff4`) and `0x40161188` (via `0x86db8`). A read of word 0 cannot see the H2D enable at all.
* **CA `0x40161100`/`0x40161180` are outside every host window anyway.** Region 3
  (`SHUANGTA_REGION_IO`) ends at device `0x4011FFFF` (phase 17/18, re-derived in the sibling
  `docs/phase32/irq-block-mapping.md`), so the host can never read or write these enables. They
  are device-internal, written only by the firmware itself.

The only software-interrupt register the firmware exercises is CA `0x40161F00` -- an IPI:

```
0820dc rt0c20dc  dmb      ishst
0820e4 rt0c20e4  ldr      r2, [pc, #0xc]   ; -> 0x820f4 = 0x40161F00
0820ea rt0c20ea  str      r3, [r2]         ; *0x40161F00 = (target_cpu << 17) | 1
```

That targets a CPU, not the mailbox line, and cannot stand in for id `0x4c`.

So the satisfying input is: **the mailbox must assert its H2D message interrupt as line `0x4c`**,
at which point CA `0x4016010C` presents `0x4c`, `fn_array[0x4c]` (`0x40295`) runs, and the
dispatcher enters. The host's only lever on this device is the mailbox doorbell at CA
`0x400392D4` (host-visible in region 3); the routing of that mailbox event to interrupt line
`0x4c` is device-internal.

---

## 6. What remains unfulfilled in a takeover

Nothing on the firmware side. Everything the firmware must do to receive an H2D message is
programmed in a takeover by the same code path that a vendor boot runs:

1. the controller is started (file `0x8308e`: `*0x40160100 = 1`) and its array seeded (file
   `0x6e7c`..`0x6e84`);
2. the handler is registered (`register(0x4c,5,0x40295)` at file `0x9800`);
3. the enables are written (file `0x9756` -> `0x40161188`, file `0x9816` -> `0x40161108`);
4. the context object and its handler-table link are in place (`ctx = 0x10C190`, `ctx+0x20 =
   0x00118D68`, statically at file `0xCC1B0`).

What is never fulfilled is the **assertion of interrupt `0x4c`**. The host writes the H2D set-bits
into `out[0]` (CA `0x40039010`) and rings the doorbell (CA `0x400392D4`); the doorbell is consumed
(readback 0, phase 31) but no id `0x4c` ever appears at CA `0x4016010C`, so `blx r5` at file
`0x82f4a` never executes and `0x818ac` is never entered -- exactly the record's "the dispatcher's
ack signature is never observed". No host write into the mailbox CAs can substitute for the ISR:
the **only** reader of `out[0]` is the dispatcher itself (file `0x818bc`, `ldr r5,[r2]` with
`r2 = ctx+4`), and the dispatcher is reachable only through `fn_array[0x4c]`.

The remaining link is therefore precisely: **mailbox H2D event -> device interrupt line `0x4c` ->
CA `0x4016010C`.** The two candidate places it can fail are (a) the mailbox's own interrupt
generation, and (b) the per-root-complex path the doorbell TLP takes -- the port writes the
doorbell through EP0 while the vendor writes it through EP1 (sibling
`docs/phase32/doorbell-endpoint-path.md`). Both are outside `FIRMWARE.bin`; the firmware's end is
armed either way.

---

## 7. Corrections carried forward

* Phase 30's inference that "the firmware's message interrupt" is id `0x2d`/`0x2e` is wrong. Those
  ids are the per-channel receive handlers (`0x462F9`/`0x4624D`); the H2D dispatcher is id `0x4c`
  (`0x40295`), a slot the phase-30 live dump did not reach (it read from `0x17D430` forward but
  reported only the low ids).
* Phase 30's enable-bitmap reading (`0x40161100`, word 1) does not test the H2D gate. The H2D
  enables are word 2 of that block (`0x40161108`) and of the `0x40161180` block (`0x40161188`) --
  and neither block is host-visible at all (`docs/phase32/irq-block-mapping.md`).
* The phase-22 "device-side caller of the dispatcher (an internal wake)" is now named: an
  interrupt, id `0x4c`, not a host-memory population and not a firmware thread.

---

## Reproduce

```bash
PY=pyenv/Scripts/python.exe
FW=build/tmp/FIRMWARE.bin
$PY - <<'EOF'
from capstone import *
d=open("build/tmp/FIRMWARE.bin","rb").read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.detail=True
def show(a,n,lit=False):
    for i in md.disasm(d[a:a+n],a):
        ex=""
        if lit:
            from capstone.arm import ARM_OP_MEM, ARM_REG_PC
            for op in i.operands:
                if op.type==ARM_OP_MEM and op.mem.base==ARM_REG_PC:
                    t=((i.address+4)&~3)+op.mem.disp
                    ex="  ; [%#x]=%08x"%(t,int.from_bytes(d[t:t+4],'little'))
        print("%06x rt%06x  %-8s %-24s%s"%(i.address,i.address+0x40000,i.mnemonic,i.op_str,ex))
show(0x294,0x10,True)      # the stub -> 0x818ac
show(0x97f4,0x2a,True)     # register(0x4c,5,0x40295) / register(0x4e,5,0x402a1)
show(0x82efc,0x60,True)    # the ISR
show(0x874b0,0x80,True)    # the register fn
show(0x86db8,0x40,True)    # enableA -> 0x40161180/+0x100
show(0x86ff4,0x40,True)    # enable   -> 0x40161100
EOF
```

---

## Claims ledger

| # | claim | evidence |
| --- | --- | --- |
| 1 | the dispatcher `0x818ac` has exactly one branch caller, file `0x296` | full `b`/`bl` sweep; no `0x000c18ac` literal in the image |
| 2 | the caller is the stub at file `0x294`, `ldr r0,=0x0010C190 ; b.w 0x818ac` | files `0x294`,`0x296`,`0x29c` |
| 3 | `0x10C190` is the dispatcher ctx (`r5+0xD0`) | pool `0x97b0 = 0x0010C0C0`; `+0x34 = *0x172130 = 0x10C0F4`; `+0xD0 = 0x10C190` |
| 4 | `pcie_msg_init` registers the stub for interrupt `0x4c` (prio 5) | files `0x97f6`,`0x97fe`,`0x9800`; pool `0x9858 = 0x00040295` |
| 5 | id `0x4e` maps to the stub `0x2a0 -> 0x86108` | files `0x9806`,`0x980a`,`0x980c`; pool `0x985c = 0x000402A1` |
| 6 | ids `0x2d`/`0x2e` are the per-channel handlers, not the dispatcher | files `0x96c8`,`0x96ca`,`0x96dc`; pool `0x97c0/0x97c4` |
| 7 | the handler array is `[0x0017D398 + 0x98 + id*4]`, written by file `0x874e8` | files `0x874b6`,`0x874c4`,`0x874c8`,`0x874e8` |
| 8 | the only consumer of that array is the ISR at file `0x82efc` | file `0x82f12`/`0x82f16`; scan of all `ldr.w rX,[rY,#0x98]` |
| 9 | the ISR reads the active id from CA `0x4016010C` and calls `fn_array[id]` | files `0x82f00`,`0x82f04`,`0x82f0c`,`0x82f4a`; pool `0x83010` |
| 10 | the ISR EOIs by writing the id back to CA `0x40160110` | files `0x82f4e`,`0x82f52`; pool `0x83020` |
| 11 | the controller is started by file `0x8308e` (`*0x40160100 = 1`) | file `0x8308a`..`0x8308e`; pool `0x8309c` |
| 12 | H2D enables are written to CA `0x40161188` and CA `0x40161108` | files `0x86ddc`..`0x86df2`, `0x87026`/`0x8702c`; pools `0x86e0c`,`0x87044` |
| 13 | the service has no polling flag; the wake is the IRQ, idles with `wfi` at `0x86bd0` | file `0x86bd0`; `cpsie/cpsid` at `0x82f46`/`0x82f4c` |
