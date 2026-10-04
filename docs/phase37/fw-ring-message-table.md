# The firmware's H2D ring-message dispatch table (phase 37, 2026-10-04)

Reconstruction of what the device firmware does when the host posts a host->device message:
the dispatcher, the handler table, **every message id the table holds**, each handler's address,
the message format it expects, and what it does with it (including the reply path).

Purely static, read-only analysis of the on-disk image. No device was touched, nothing was written
outside this file.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B (`0xe2c98`), md5 `0e530b976d5a20e87358671f1a577695` |
| convention | runtime address = file offset + `0x40000`; **all offsets below are FILE offsets** unless written `rt` |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM` + `CS_MODE_THUMB` (ARM for the vendor `.ko`) |
| verification | every quoted offset re-disassembled; 177 instructions + 13 pointer words checked in two passes, 0 failures (section 8) |
| record used | `phase30/the-gate-located.md`, `phase32/interrupt-status-and-doorbell-path.md`, `phase32/THE-GATE-MAP.md`, `phase24/handler-table.md`, `phase24/fw-sr-gate.md`, `phase24/vendor-sr-announce-id.md`, `phase25/chip-ops-table.md`, `phase36/h2d-mailbox-boot-only.md` |
| vendor module (for one cross-check only) | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b` |

**Result in one line:** the firmware's H2D dispatch is a single table at runtime `0x00118D68`,
indexed by the *bit position* of the host's pending mask `out[0]`; exactly **four** ids are
populated - **1, 3, 5, 6** - and their handlers are at file `0x510`, `0x85144`, `0x819dc`, `0x4cce4`.
The mailbox message carries **no body at all** (only the id bit); bodies ride the SR ring / HCC
queue in the header format quoted in section 4.

---

## 1. The trigger, and the one dispatcher

The host announces an H2D message by writing `1 << id` into the pending mask at CA `0x40039010`
(`out[0]`) and OR-ing bit 0 of the doorbell CA `0x400392D4` (`out[2]`) - both proven host-side in
`phase20/message-service.md` A.4 and re-verified against `hi5622v100_plat.ko` `pcie_msg_send` here.

The device side is one routine, entered with the firmware's message context (`rt 0x0010C190`,
loaded by the thunk at file `0x294`; `phase32/interrupt-status-and-doorbell-path.md` section 4.5).
It is the **only** caller chain in the image, and this is its whole body:

```
file 0x818ac  push  {r3, r4, r5, r6, r7, lr}
file 0x818ae  mov   r6, r0                 ; r6 = ctx
file 0x818b0  cbz   r0, #0xc18c8           ; NULL ctx -> return
file 0x818b2  movs  r7, #1
file 0x818b6  ldr   r2, [r0, #0xc]         ; ctx+0xc  = ack register pointer
file 0x818b8  str   r7, [r2]               ; *ack = 1                  -> CA 0x400392F0
file 0x818ba  ldr   r2, [r0, #4]           ; ctx+4    = pending pointer
file 0x818bc  ldr   r5, [r2]               ; pending  = *out[0]        -> CA 0x40039010
file 0x818be  str   r1, [r2]               ; *out[0]  = 0              (consume)
file 0x818c0  movs  r1, #8
file 0x818c2  ldr   r2, [r0, #0x10]        ; ctx+0x10 = doorbell pointer
file 0x818c4  str   r1, [r2]               ; *doorbell = 8             -> CA 0x400392D4
file 0x818c6  cbnz  r5, #0xc18ca           ; pending == 0 -> return
file 0x818c8  pop   {r3, r4, r5, r6, r7, pc}
file 0x818ca  rsbs  r4, r5, #0
file 0x818cc  ands  r4, r5                 ; r4 = pending & -pending   (lowest set bit)
file 0x818ce  clz   r4, r4
file 0x818d2  rsb.w r4, r4, #0x1f          ; r4 = index of lowest set bit  == message id
file 0x818d6  cmp   r4, #9
file 0x818d8  bhi   #0xc18c8               ; id > 9 -> return (loop ends)
file 0x818da  ldr   r3, [r6, #0x20]        ; r3 = handler table
file 0x818dc  add.w r2, r3, r4, lsl #3     ; r2 = &table[id]
file 0x818e0  ldr.w r3, [r3, r4, lsl #3]   ; r3 = table[id].fn
file 0x818e4  cbz   r3, #0xc18ea           ; NULL -> skip
file 0x818e6  ldr   r0, [r2, #4]           ; r0 = table[id].arg
file 0x818e8  blx   r3                     ; call fn(arg)
file 0x818ea  lsl.w r4, r7, r4             ; r4 = 1 << id
file 0x818ee  bic.w r5, r5, r4             ; clear the dispatched bit
file 0x818f2  b     #0xc18c6               ; loop -> next set bit
```

Facts this body proves, and they are the frame for every id below:

* **The message selector is the bit index**, not a length-prefixed field: `id = 31 - clz(pending & -pending)`.
* **The loop is exhaustive and lowest-bit-first**: it re-enters at `0xc18c6` after each handler, so
  *every* set bit of `out[0]` is dispatched in ascending id order until one exceeds 9.
* **The handler receives exactly one argument**: `table[id].arg` (`ldr r0,[r2,#4]` at `0x818e6`).
  There is no pointer to a message buffer, no length, no register-block address in the call.
* **The acknowledgement/turnaround is fixed and id-independent**: `ack = 1` (`0x818b8`),
  `out[0] = 0` (`0x818be`), `doorbell = 8` (`0x818c4`). This matches the record's summary in
  `phase32/THE-GATE-MAP.md` section 1 line for line.

## 2. The handler table, and how it is filled

### 2.1 Location and entry layout

The table pointer lives in the message context at `ctx+0x20`; the context is the object at
`rt 0x0010C190` whose `+0x20` field is loaded by the dispatcher at file `0x818da`. Its **static
initializer** is readable in the image:

```
file 0xcc1b0 : 68 8d 11 00   -> 0x00118D68        (ctx+0x20 = &table, runtime address)
```

An entry is 8 bytes, `{u32 fn @ +0, u32 arg @ +4}`, and the table admits ids `0..9`
(the dispatcher's `cmp r4,#9` at file `0x818d6`, and the registrar's `cmp r1,#9` at file `0x8187a`).

**Where the table actually lives.** Runtime `0x00118D68` is *RAM*, not image bytes. The blob's
byte at file `0xD8D68` (which the plain `file+0x40000` rule maps to the same runtime address) belongs
to a **second, self-contained image appended after the main firmware** - every BL from file >= `0xD0000`
targets runtime >= `0x110000` (548 callers, 478 of them into `0x110000..0x11FFFF`, and **zero** into the
main image), while no BL from the main region targets between `0x10A000` and `0x10FFFF` (it stops at
`0x106AE0`), and file `0xCFF00..0xD0100` is 97.9% zero padding. The main firmware's file-backed extent
therefore ends at file `~0xD0000`; `0x117C00`, `0x118D68`, `0x170E08`, `0x172130` are all BSS in the main
firmware's address space, initialised at runtime. Consequence for this document: the table's content is
**not** readable from the file - it is exactly what the registration sites of section 2.2 write, which is
what the record's live dump in `phase30/the-gate-located.md` independently shows.

### 2.2 The two registration entry points

**Entry point A - `file 0x8187a` (runtime `0xc187a`) - `{fn, arg}`, object path.**

```
file 0x8187a  cmp   r1, #9                  ; id <= 9
file 0x8187c  push  {r4, r5, lr}
file 0x8187e  bhi   #0xc189c               ; id > 9 -> error
file 0x81880  cbz   r2, #0xc189c           ; fn == NULL -> error
file 0x81882  cbnz  r0, #0xc18a2           ; only the global object (r0 == 0) is supported
file 0x81884  ldr   r4, [pc, #0x20]         ; pool word at file 0x818a8 = 0x00172130 (pcie_msg global)
file 0x81886  ldr   r4, [r4]                ; r4 = *0x172130 = the pcie_msg object
file 0x8188a  ldr.w r4, [r4, #0xbc]         ; r4 = object+0xbc = the handler table     <-- the table
file 0x8188e  cbz   r4, #0xc18a2           ; NULL table -> error
file 0x81890  add.w r5, r4, r1, lsl #3      ; &table[id]
file 0x81894  str.w r2, [r4, r1, lsl #3]    ; table[id].fn  = fn
file 0x81898  str   r3, [r5, #4]            ; table[id].arg = arg
```

`file 0x8187a` has **exactly one caller in the whole image**, found by a complete BL/BLX sweep:
file `0x9838`, inside `pcie_msg_init` (the firmware's own message-service init,
`phase24/fw-sr-gate.md` section 1.6):

```
file 0x982a  ldr   r2, [pc, #0x38]          ; pool word at file 0x9864 = 0x000c5145  (id-3 handler)
file 0x9830  mov   r1, r6                   ; r6 = 3  (the handler id)
file 0x9832  add.w r3, r5, #0x34            ; r3 = 0x10C0C4+0x30 ... r5 = 0x10C0C0 -> arg = 0x0010C0F4
file 0x9838  bl    #0xc187a                 ; register(0, id=3, fn=0xc5145, arg=0x0010C0F4)
```

So entry point A installs **id 3 only, with a non-NULL arg** (the pcie_msg object at `rt 0x0010C0F4`).

**Entry point B - `file 0x7c1dc` (runtime `0xbc1dc`) - `{fn}` only, through an ops table.**

```
file 0x7c1dc  push  {r4}
file 0x7c1de  ldr   r3, [pc, #0x1c]         ; pool word at file 0x7c1fc = 0x00170e08
file 0x7c1e0  mov   r2, r1                  ; r2 = 2nd argument (the handler fn)
file 0x7c1e2  ldr   r3, [r3, #0x10]         ; r3 = *(*(0x170e08)+0x10) = the object
file 0x7c1e4  ldr   r4, [r3, #0x24]         ; r4 = object+0x24 = the registrar function
file 0x7c1e6  cbz   r4, #0xbc1f4           ; NULL -> error
file 0x7c1e8  movs  r3, #0
file 0x7c1ea  mov   r1, r0                  ; r1 = 1st argument (the id)
file 0x7c1f0  pop   {r4}
file 0x7c1f2  bx    ip                      ; tail call registrar(0, id, fn)
```

Entry point B has **exactly three callers** (same sweep), one per remaining handler:

| id | call site | fn literal (file) | instruction sequence |
| --- | --- | --- | --- |
| 1 | file `0x8226` | `0x84d0` = `0x00040511` | `file 0x8224 movs r0, #1` ; `file 0x8226 bl #0xbc1dc` |
| 5 | file `0x7cf0` | `0x7ddc` = `0x000c19dd` | `file 0x7cee movs r0, #5` ; `file 0x7cf0 bl #0xbc1dc` |
| 6 | file `0x88f8` | `0x8994` = `0x0008cce5` | `file 0x88ea movs r0, #6` ; `file 0x88f8 bl #0xbc1dc` |

**Why both paths must write the same table [inference, with its grounds].** Entry point B's registrar
is reached only indirectly (`*(*(0x170E08)+0x10)+0x24`); its address appears nowhere in the image as a
literal or as a `movw`/`movt` pair, so it cannot be named statically. But the four function values
`0x40511`, `0xc5145`, `0xc19dd`, `0x8cce5` occur **exactly once each** in the whole image (byte search),
and they are exactly the four (id, fn) pairs the two entry points register. Since entry point A is the
only other writer of `*object+0xbc`, and the record's live dump of `0x118D68` shows ids 1/3/5/6 with
those four fns, entry point B's registrar must write the same array. The id -> handler assignment below
therefore rests on two independent legs: the static registration sites, and `phase30`'s live read.

---

## 3. The table: every id the firmware handles

Ids 0, 2, 4, 7, 8, 9 are **empty** in the record's live dump (`phase30/the-gate-located.md`) and no
registration site for them exists in the image - the sweep for callers of both entry points returns
exactly the four sites above and no others. What follows is one row per populated id.

### 3.1 id 1 - handler file `0x510` (runtime `0x00040511`), arg `0`

Registration: entry point B, file `0x8226` (section 2.2). Handler body, in full:

```
file 0x510  ldr   r3, [pc, #0xc]      ; pool word at file 0x520 = 0x00117C00  (global message object)
file 0x512  add.w r1, r3, #0x274      ; r1 = object+0x274  (a queue/list head)
file 0x516  ldr.w r0, [r3, #0x270]    ; r0 = *(object+0x270)
file 0x51a  b.w   #0xc6d0c            ; tail call 0xc6d0c(r0, object+0x274)
```

The tail target (file `0x86d0c`) validates then enqueues and wakes the message worker:

```
file 0x86d0c  push  {r3, r4, r5, lr}
file 0x86d0e  mov   r5, r1            ; r5 = object+0x274
file 0x86d10  mov   r4, r0            ; r4 = *(object+0x270)
file 0x86d12  cbnz  r0, #0xc6d1a      ; NULL -> return -1
file 0x86d1a  ldr   r3, [r1, #0xc]    ; object+0x280 must be non-zero
file 0x86d1e  beq   #0xc6d14          ; ... else return -1
file 0x86d38  bl    #0xc2114          ; list add
file 0x86d48  bl    #0xc2a54          ; run the message worker on r4+0x3c
```

The list add (file `0x82114`) is a textbook circular-list insert (`next = [head+4]; [next] = item;
[item+4] = next; [head+4] = item; [item] = head`):

```
file 0x82114  ldr   r3, [r0, #4]
file 0x82116  str   r1, [r3]
file 0x82118  ldr   r3, [r0, #4]
file 0x8211a  str   r3, [r1, #4]
file 0x8211c  str   r1, [r0, #4]
file 0x8211e  str   r0, [r1]
```

* **Expected message:** the `out[0]` bit 1 alone. No body, no length, no fields are read by this
  handler; `arg` is 0 and unused (the handler ignores `r0` on entry).
* **What it does:** posts the global message object at `rt 0x00117C00` onto the firmware's message
  queue (`*(object+0x270)` is the queue object, `object+0x274` is the item, `object+0x280` a
  readiness flag) and then runs the message worker `0xC2A54` on that queue.
* **Reply:** none directly. The handler produces work for the worker thread; any reply is produced
  there (or by the message it drains). No mailbox write is made by this handler itself.

### 3.2 id 3 - handler file `0x85144` (runtime `0x000c5145`), arg `rt 0x0010C0F4`

Registration: entry point A, file `0x9838` inside `pcie_msg_init` (section 2.2) - the only id that
carries an argument. This is the id the vendor's own SR post announces
(`phase24/vendor-sr-announce-id.md`: `shuangta_ete_sr_dscr_fill` ends `pcie_msg_send(chip, 3)`).

```
file 0x85144  b     #0xc5128          ; 2-byte thunk: the real body is at file 0x85128
file 0x85128  push  {r4, lr}
file 0x8512a  mov   r4, r0            ; r4 = arg = rt 0x0010C0F4
file 0x8512c  cbz   r0, #0xc5142      ; NULL -> return
file 0x8512e  adds  r0, #0x98         ; r0 = arg+0x98
file 0x85130  movs  r1, #1
file 0x85132  bl    #0x1024bc        ; atomic store: *(arg+0x98) = 1
file 0x85136  add.w r0, r4, #0x50     ; r0 = arg+0x50
file 0x8513e  b.w   #0xc2a54          ; run the message worker on arg+0x50
```

The store is the firmware's `ldrex/strex` primitive (file `0xc24bc`):

```
file 0xc24bc  ldrex r3, [r0]
file 0xc24c0  mov   r3, r1
file 0xc24c2  strex r2, r3, [r0]
file 0xc24c6  teq.w r2, #0
file 0xc24ca  bne   #0x1024bc
file 0xc24cc  bx    lr
```

* **Expected message:** the `out[0]` bit 3. The handler reads **no length, no payload and no header
  field** - it only uses the table's `arg` (`phase24/handler-table.md`'s "the device-side handler
  validates a body" describes the *host-side* `host_ready_msg_process`, not this code: the firmware's
  own `~0x62` length check sits at file `0x62358`, `ldrh r2,[r4,#8]` / `subs r3,r2,#2` /
  `cmp r3,#0x62`, i.e. a different offset and a different function).
* **What it does:** signals the pcie_msg object's event word (`arg+0x98 = 1`, a semaphore/flag the
  firmware's message thread waits on) and runs the message worker on `arg+0x50`.
* **Reply:** none from this handler. The worker it wakes is where the queued body is drained; that
  drained work is what produces the device's subsequent words (`out[1]` bits, section 5).

### 3.3 id 5 - handler file `0x819dc` (runtime `0x000c19dd`), arg `0`  -- a trap

Registration: entry point B, file `0x7cf0` (section 2.2). The registered entry point is a 6-byte
`BUG()` stub - the raw bytes at file `0x819dc` are `00 23 1b 60 ff de`:

```
file 0x819dc  movs  r3, #0
file 0x819de  str   r3, [r3]          ; store to address 0
file 0x819e0  udf   #0xff             ; undefined instruction -> exception
```

It is the third of three identical stubs in a row (file `0x819d0`, `0x819d6`, `0x819dc`).

* **Expected message:** the `out[0]` bit 5.
* **What it does:** nothing useful - it faults (NULL store, then `UDF`).
* **Reply:** none. **Hazard:** posting H2D id 5 executes a trap. Note the tension, recorded rather
  than smoothed over: the vendor's host does post id 5 - `pcie_ete_rcv_buff_check` in
  `hi5622v100_plat.ko` ends its failure branch with `0x01513c mov r1,#5` / `0x015144 bl pcie_msg_send`
  and returns an error code (`0xffff74d3`) - but that is an *error* path, and the ids the vendor's other
  init-time H2D sends carry are not established here. Either the path is never taken in a working boot,
  or this blob's id-5 slot is deliberately unimplemented. It is the one id whose handler is not a
  working handler.

### 3.4 id 6 - handler file `0x4cce4` (runtime `0x0008cce5`), arg `0`  -- samples hardware, emits a message

Registration: entry point B, file `0x88f8` (section 2.2). Full body:

```
file 0x4cce4  push.w {r0, r1, r2, r4, r5, r6, r7, r8, sb, sl, fp, lr}
file 0x4cce8  ldr   r3, [pc, #0xa8]    ; pool word at file 0x4cd94 = 0x40100100  (a device CA)
file 0x4ccf0  bl    #0x102822          ; buf = msg_alloc(len=r0=8, ch=r1=6)
file 0x4ccee  ldr   r4, [r3]           ; r4 = *(0x40100100)   <-- hardware word sampled
file 0x4ccf4..0x4cd10   field extraction from r4:
        sb = (r4 >> 0x14) & 0xf     r8 = (r4 >> 0x0f) & 1     r7 = (r4 >> 4) & 0x7ff
        r6 =  r4         & 0xf      sl = (r4 >> 0x18) & 0xf    fp = (r4 >> 0x1c) & 3
        r4 =  r4 >> 0x1e
file 0x4cd14..0x4cd20   if (buf == NULL) -> printk + return
file 0x4cd24  bl    #0x409ea           ; validate the buffer (per-channel pool + type nibble)
file 0x4cd2c  strb  r4, [r0, #2]
file 0x4cd2e  bfi   r3, r2, #0, #4     ; buf[0] low nibble = 2   (message type)
file 0x4cd38  strb  r3, [r0, #6]       ; buf[6] = 0x2a
file 0x4cd3e  bfi   r3, fp, #4, #4     ; buf[1] high nibble = fp
file 0x4cd44  strb  r2, [r0, #8]       ; buf[8] = 0
file 0x4cd46  strb.w sl, [r0, #3]      ; buf[3] = (r4>>0x18)&0xf
file 0x4cd5c  bl    #0xc1e0c           ; zero 8 bytes
file 0x4cd60  strb.w sb, [r4, #0xc]    ; payload[0] = (word>>0x14)&0xf
file 0x4cd64  strb.w r8, [r4, #0xe]    ; payload[2] = (word>>0x0f)&1
file 0x4cd68  strh  r7, [r4, #0x10]    ; payload[4..5] = (word>>4)&0x7ff
file 0x4cd6a  strb  r6, [r4, #0xf]     ; payload[3] = word&0xf
file 0x4cd6e  bl    #0x102760          ; hand the buffer to the transmit path
```

* **Expected message:** the `out[0]` bit 6, alone. Again no length or field is read from the mailbox.
* **What it does:** samples one 32-bit device word at CA `0x40100100` (a *register*, not the ring),
  splits it into five bit-fields, builds a 20-byte (`0x14`) message whose header is produced by the
  firmware's own constructor (section 4), and hands it to the firmware's message transmit path.
* **Reply:** this handler *is* the reply path: it emits a device->host message. It is the only one of
  the four that produces a message body. (Direction: the message is handed to the same
  validate-and-route pair - file `0x9ea` -> file `0x8f8` - that the firmware's HCC tx uses; the
  mailbox-level D2H announce for such messages is the primitive in section 5.)

---

## 4. The message format: what actually carries fields

The mailbox message has no body: the dispatcher reads `out[0]` as a bitmap and calls a
zero-argument (or single fixed-arg) handler (section 1). Field-carrying messages exist, and the
firmware's own code fixes their layout. Two independent pieces of firmware code agree:

**(a) The constructor**, file `0xc2822` (`msg_alloc(len, ch)`), which every outgoing message goes
through - quoted at the points that define the format:

```
file 0xc2822  push  {r4, r5, r6, lr}
file 0xc2838  adds  r4, #0xc            ; total = payload + 12
file 0xc283a  uxth  r4, r4              ; ... as a 16-bit quantity
file 0xc2840  bl    #0xbee68            ; allocate (channel r0, size r1)
file 0xc284a  str   r6, [r0, #8]        ; +8 = a caller-supplied tag
file 0xc285a  ldrb  r3, [r0, #1]
file 0xc285c  strh  r4, [r0, #4]        ; +4  = u16 total length (payload + 0xC)
file 0xc285e  bfc   r3, #0, #4          ; +1 low nibble cleared
file 0xc2862  strb  r3, [r0, #1]
```

**(b) The length check** in the receive/rx path, file `0x62354`:

```
file 0x62354  ldrh  r2, [r4, #8]        ; a 16-bit length field
file 0x62356  subs  r3, r2, #2
file 0x62358  cmp   r3, #0x62           ; bound on the payload length
file 0x6235a  bls   #0xa236a
```

The routed-message decoder, file `0x8f8`, reads the routing fields the same way:

```
file 0x8f8  push  {r4, r5, r6, lr}
file 0x8fa  ldrb  r5, [r0, #6]          ; +6
file 0x8fc  ldrb  r4, [r0, #5]          ; +5
file 0x8fe  and   r5, r5, #0xf          ; +6 low nibble  = channel
file 0x902  ubfx  r4, r4, #1, #3        ; +5 bits 1..3   = class
file 0x90c  bl    #0x4088c              ; per-(channel,class) target object
file 0x918  ldrh  r0, [r0, #4]          ; target's u16 capacity at +4
file 0x91e  cmp   r3, r0                ; 12-bit length (from +6/+7) vs capacity
```

Combining, the field-carrying (body) message format is:

| offset | size | meaning | firmware evidence |
| --- | --- | --- | --- |
| `+0` bits 0..3 | 4b | message type (the route validator requires <= 3) | file `0x9ea` `cmp r3,#3` at file `0xa12` |
| `+1` bits 0..3 | 4b | cleared by the constructor (a flag nibble) | file `0xc285e` `bfc r3,#0,#4` |
| `+1` bits 4..7 | 4b | source-core nibble (set by the direction's maker) | file `0x4cd3e` `bfi r3, fp, #4, #4` |
| `+4` | u16 | **total length**, = payload + `0xC` | file `0xc285c` `strh r4,[r0,#4]` |
| `+5` bits 1..3 | 3b | class (route selector) | file `0x902` `ubfx r4,r4,#1,#3` |
| `+6` low | 4b | channel / queue (route selector) | file `0x8fe` `and r5,r5,#0xf` |
| `+6`/`+7` | 12b | a length the route compares against the target's `+4` capacity | file `0x918`/`0x91e` |
| `+8` | u32 | caller tag | file `0xc284a` `str r6,[r0,#8]` |
| `+0xC` | payload | **payload starts here**; bounded by `0x62` | file `0xc2838` `adds r4,#0xc`; file `0x62358` `cmp r3,#0x62` |

This agrees with the record's host-side reading of the same wire format
(`phase19/fw-handshake.md` A.2: "byte[1] high nibble = source core, `u16 at +4` = length,
`byte[9]` = core index, payload at `+0xc`") and with `phase24/handler-table.md`'s
"16-bit length at `+4` / payload from `+0xc`". Where the record and this document differ is only the
*attribution*: that header belongs to the message **body** (HCC/ring), not to the mailbox id, and the
firmware's `~0x62` check reads `+8`, not `+4`.

**So the answer to "what does each id expect" is:**
`ids 1, 3, 6 (and the trapping 5) expect the pending-mask bit and nothing else`; the body, when the
flow has one, is the HCC message of the table above, delivered through the SR ring (the node layout -
`word0 = buffer device VA`, `word1 = (len << 16) | flag` - is quoted in `phase25/chip-ops-table.md`
against the vendor's own accessors) and drained by whatever id-3/id-1 wakes.

## 5. The device -> host reply primitive

The firmware's only mailbox announce path is at file `0x86170` (`notify(0, id)`, ids `0..10`):

```
file 0x86170  cmp   r1, #0xa            ; id <= 10
file 0x86178  cbnz  r0, #0xc61d6        ; handle must be 0
file 0x8617a  ldr   r3, [pc, #0x60]     ; the pcie_msg global
file 0x8617c  ldr   r5, [r3]
file 0x8618a  ldr.w r1, [r5, #0xb8]     ; pending shadow
file 0x8619e  lsls  r3, r7              ; 1 << id
file 0x861a2  str.w r3, [r5, #0xb8]
file 0x861a8  ldr.w r1, [r5, #0x9c]     ; the D2H pending register
file 0x861b0  str   r3, [r1]            ; *D2H = bitmap of ids
file 0x861b2  ldr.w r4, [r5, #0xa4]     ; the doorbell
file 0x861ba  ldr   r3, [r4]
file 0x861bc  orr   r3, r3, #1
file 0x861c0  str   r3, [r4]            ; *doorbell |= 1
```

So replies are announced exactly the way requests are: a one-bit-per-id mask plus a doorbell bit.
The register pair is the device-side mirror of `out[1]`/`out[2]` (the record's H2D/D2H convention);
this is what `phase24/fw-sr-gate.md` describes as the D2H notify primitive. The handler of section 3.4
is the one H2D id whose work product is a message that leaves through this path.

## 6. Coverage

| id | handler (runtime) | handler (file) | arg | registered at (file) | handler works? |
| --- | --- | --- | --- | --- | --- |
| 1 | `0x00040511` | `0x510` | 0 | `0x8226` (entry point B) | yes - queue + wake worker |
| 3 | `0x000c5145` | `0x85144` | `0x0010C0F4` | `0x9838` (entry point A) | yes - signal + wake worker |
| 5 | `0x000c19dd` | `0x819dc` | 0 | `0x7cf0` (entry point B) | **no - `BUG()` trap** |
| 6 | `0x0008cce5` | `0x4cce4` | 0 | `0x88f8` (entry point B) | yes - sample CA, emit message |
| 0, 2, 4, 7, 8, 9 | - | - | - | no registration site exists | empty |

Every id with a `{fn,arg}` entry in the firmware's table is listed; every registration site in the
image is listed; the id bound (0..9) is quoted from both the registrar and the dispatcher. There is
one handler table and one dispatcher in the main firmware: `file 0x818ac` has exactly one caller
(through the id-`0x4C` interrupt thunk, `phase32`), and the table pointer is written only through
`*object+0xbc`.

## 7. Open items, stated plainly

1. **id 5 traps.** The vendor's host posts id 5 on the `pcie_ete_rcv_buff_check` failure branch
   (section 3.3). Whether that path is dead in a working boot, or this blob's id-5 slot is a
   deliberate stub, is not settled by static reading alone.
2. **Entry point B's registrar is unnamed.** `*(*(0x170E08)+0x10)+0x24` cannot be resolved to an
   address from the image (no literal, no `movw`/`movt` pair). The argument that it writes the same
   table rests on the uniqueness of the four fn values plus the record's live dump (section 2.2).
3. **The ids the vendor's other init-time H2D sends carry** are not established here; only the SR
   announce (id 3) is, from `phase24/vendor-sr-announce-id.md` and re-verified at
   `hi5622v100_plat.ko` `0x0178f4 mov r1, #3` / `0x0178f8 bl pcie_msg_send`.
4. **`not proven`**: that the id-6 handler's emitted message is announced on `out[1]` specifically -
   the handler hands it to the shared validate/route pair (`0x9ea`/`0x8f8`), and the only announce
   primitive in the image is `0x86170`; the link between them is not a direct call.

## 8. Verification

Every offset quoted above was re-disassembled from `FIRMWARE.bin` (md5
`0e530b976d5a20e87358671f1a577695`) with `pyenv/Scripts/python.exe`, capstone 5.0.7,
`CS_ARCH_ARM`/`CS_MODE_THUMB`, comparing the *mnemonic and operand string* against the expected text.
Result:

```
pass 1: checked 153 instructions + 13 words ; failures=0
pass 2: checked  24 instructions           ; failures=0   (the branch targets and the id-6 field block)
```

The 13 pointer words checked are the literal pools the document relies on:
file `0x29c` = `0x0010C190`, `0x2a8`/`0x2b0` = `0x0010C0F4`, `0x520` = `0x00117C00`,
`0x7c1fc` = `0x00170E08`, `0x818a8` = `0x00172130`, `0x84d0` = `0x00040511`,
`0x7ddc` = `0x000c19dd`, `0x8994` = `0x0008cce5`, `0x9864` = `0x000c5145`,
`0x4cd94` = `0x40100100`, `0xcc1b0` = `0x00118D68`, `0x97b0` = `0x0010C0C0`.
Address-frequency claims (which fns occur where, which callers exist, the call-graph boundary at
file `0xD0000`) come from exhaustive BL/BLX decodes over the whole blob, not from sampling.

## 9. Index of every quoted file offset

`0x294`/`0x296` (id-`0x4C` thunk, from `phase32`), `0x29c`, `0x2a8`, `0x2b0`, `0x510`-`0x51a`,
`0x520`, `0x7c1dc`-`0x7c1f2`, `0x7c1fc`, `0x7cee`, `0x7cf0`, `0x7ddc`, `0x8224`, `0x8226`, `0x84d0`,
`0x85128`-`0x8513e`, `0x85144`, `0x8187a`-`0x81898`, `0x818a8`, `0x818ac`-`0x818f2`, `0x819dc`-`0x819e0`,
`0x82114`-`0x8211e`, `0x82a54`-`0x82a86`, `0x86170`-`0x861c0`, `0x86d0c`-`0x86d48`, `0x88ea`, `0x88f8`,
`0x8994`, `0x97b0`, `0x9ea`, `0x9f0`, `0x9f2`, `0xa12`, `0xa28`, `0x8f8`-`0x91e`, `0x4cce4`-`0x4cd6e`,
`0x4cd94`, `0x62354`-`0x6235a`, `0x982a`-`0x9838`, `0x9864`, `0xc2822`-`0xc2862`, `0xc24bc`-`0xc24cc`,
`0xc2760`/`0xc2764`/`0xc2776`, `0xcc1b0`.

Record sources: `docs/phase30/the-gate-located.md` (live table dump, ids 1/3/5/6),
`docs/phase32/interrupt-status-and-doorbell-path.md` + `docs/phase32/THE-GATE-MAP.md` (the chain and
the dispatcher), `docs/phase24/handler-table.md`, `docs/phase24/fw-sr-gate.md`,
`docs/phase24/vendor-sr-announce-id.md`, `docs/phase24/id6-trigger-proven.md`,
`docs/phase19/fw-handshake.md` (wire header), `docs/phase25/chip-ops-table.md` (SR node layout),
`docs/phase36/h2d-mailbox-boot-only.md` (the H2D mailbox is boot-only in a vendor boot).
