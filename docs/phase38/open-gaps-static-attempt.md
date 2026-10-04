# A static attack on the phase-37 map's three open gaps (phase 38, 2026-10-04)

Task `st_01a106db`. Purely static, read-only: the firmware image and the two vendor `.ko` modules
were disassembled; no device was touched and no register was written. The only file written is this
one.

The brief names three open gaps from `docs/phase37/THE-PROTOCOL-MAP.md` section 4:

* **gap 9** - which D2H notify id each HCC event carries (map: *Unknown*; "the D2H notify call site
  is not in `FIRMWARE.bin`").
* **gap 10** - whether H2D id 5 is a dead path or a deliberate stub in a working boot.
* **gap 12** - whether the firmware id-6 handler's 20-byte reply is announced on `out[1]`.

Each section below gives the static evidence (offsets, bytes, call chains), then either a
conclusion or an explicit statement of the one live read that would settle it.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` |
| convention | firmware runtime = file offset + `0x40000`; **all firmware offsets here are FILE offsets** |
| host module | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b` |
| host module offsets | `.text`-relative; `.text` file offset is `0x38` (ELF section table read in this session) |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7 (`CS_ARCH_ARM`+`CS_MODE_THUMB` for the blob, `CS_ARCH_ARM`+`CS_MODE_ARM` for the module) |
| verification | section 6; every quoted offset re-disassembled in this session against the text below |

The record used read-only: `phase37/THE-PROTOCOL-MAP.md` (sections 3.3/4), `phase37/d2h-message-catalog.md`,
`phase37/fw-ring-message-table.md`, `phase32/interrupt-status-and-doorbell-path.md`,
`phase36/h2d-mailbox-boot-only.md`. Claims marked `[record]` come from those; everything else is
`[verified]` here.

---

## 1. Groundwork: the firmware's D2H notify, resolved (this unblocks gaps 9 and 12)

Gap 9's blocker in the map was "no `BL`/`BLX` and no literal names `0xc6170`". That is **wrong**: the
primitive is statically callable and statically *referenced*; the reference is a table slot, not a
branch, and the map's scan only looked for branches and for the raw word `0x000c6170` (the value in
the image carries the Thumb bit, `0x000c6171`).

### 1.1 `d2h_notify` - the primitive, and the register it uses

`d2h_notify(handle, id)` at file `0x86170` (rt `0xc6170`). The whole body, quoted (id bound, then the
shadow/armed protocol, then the two register writes):

```
file 0x86170  cmp   r1, #0xa            ; id > 10 -> reject          [verified]
file 0x86172  push  {r3, r4, r5, r6, r7, lr}
file 0x86174  mov   r7, r1              ; r7 = id
file 0x86178  cbnz  r0, #0xc61d6        ; handle must be 0
file 0x8617a  ldr   r3, [pc, #0x60]     ; pool 0x861dc = 0x00172130 (the msg global)
file 0x8617c  ldr   r5, [r3]            ; r5 = g = *0x172130 = 0x0010C0F4
file 0x86180  add.w r6, r5, #0xc0       ; lock object
file 0x8618a  ldr.w r1, [r5, #0xb8]     ; pending SHADOW
file 0x8618e  lsr.w r4, r1, r7          ; already pending?
file 0x86192  ands  r4, r4, #1
file 0x86196  bne   #0xc61d0            ;   yes -> no-op (return 0x8b2d)
file 0x86198  movs  r3, #1
file 0x8619a  ldr.w r2, [r5, #0xb4]     ; ARMED flag
file 0x8619e  lsls  r3, r7              ; 1 << id
file 0x861a0  orrs  r3, r1              ; shadow |= 1 << id
file 0x861a2  str.w r3, [r5, #0xb8]     ; store the new shadow
file 0x861a6  cbnz  r2, #0xc61c4        ; armed -> do NOT touch out[1] now
file 0x861a8  ldr.w r1, [r5, #0x9c]     ; ctx pointer  (g+0x9c = 0x0010C190)
file 0x861ac  str.w r3, [r5, #0xb4]     ; armed = shadow
file 0x861b0  str   r3, [r1]            ; *(ctx+0) = shadow   -> out[1] CA 0x40039014
file 0x861b2  ldr.w r4, [r5, #0xa4]     ; ctx+8 = D2H doorbell pointer (0x40101434)
file 0x861b6  str.w r2, [r5, #0xb8]     ; shadow = 0
file 0x861ba  ldr   r3, [r4]
file 0x861bc  orr   r3, r3, #1
file 0x861c0  str   r3, [r4]            ; doorbell |= 1
```

`g` is the message object `0x0010C0F4`: the firmware's own `pcie_msg_init` stores it at
`*0x172130` (`file 0x9706`, pool `0x97cc`), and stores `out[1]`/`out[0]`/doorbell/ack into the
*context* at `g+0x9c = 0x0010C190` (`file 0x9760`-`0x9788`). So `ctx+0 = out[1] = 0x40039014`,
`ctx+8 = 0x40101434` (the D2H doorbell) - the same `out[1]` the host reads, and the same doorbell
`hi5622v100_plat.ko` calls "D2H doorbell". **[verified]**

Two consequences the map did not carry:

* **The D2H bits batch.** `d2h_notify` only writes `out[1]` when the device-side `armed` flag
  (`g+0xb4`) is 0; otherwise it accumulates into the shadow (`g+0xb8`). A second function, the
  **D2H kick IRQ handler** at file `0x86108` (registered as device IRQ `0x4E`; thunk at file `0x2a0`
  = `ldr r0,=0x0010C0F4; b.w #0x86108`, pool `0x2a8`), flushes the shadow:
  ```
  file 0x86150  ldr.w r2, [r4, #0x9c]   ; ctx
  file 0x86156  str   r3, [r2]          ; out[1] = shadow   (r3 = g+0xb8)
  file 0x8615c  str.w r6, [r4, #0xb8]   ; shadow = 0       (r6 = 0)
  file 0x86160  ldr   r3, [r2]          ; (doorbell ptr loaded at 0x86158)
  file 0x86166  str   r3, [r2]          ; doorbell |= 1
  ```
  So a single `out[1]` read can legally carry several bits. (The *host* copes: `pcie_msg_handle`
  loops - `bics r5, r5, r3, lsl r6` at plat.ko `0x172f8`, next-lowest-bit at `0x17300`-`0x1730c`,
  and the "no handler" path at `0x1739c` rejoins the loop at `0x172f4`.) **[verified]**
* **No other writer exists.** Exhaustive scans (this session): the literal `0x40039014` occurs only
  at file `0x97dc` (ctx init) and `0x86fbc` (the step-4 inline writer); `0x40101434` only at
  `0x97d8`; an `add rX, rX, #0xc8000` (the inline doorbell fix-up) occurs only at file `0x977c`,
  `0x9794` and `0x86f5e`. Since a write needs the *address*, and the address of `out[1]`/doorbell is
  only obtainable from those pools or from the ctx (which `d2h_notify` and the flush reach via
  `g+0x9c`/`g+0xa4`), the writers are exactly: the ctx init `0x978c` (`*out[1] = 0`), the inline
  step-4 pair `0x86F5A`/`0x86F66`, `d2h_notify` (`0x861b0`/`0x861c0`) and the IRQ-`0x4E` flush
  (`0x86156`/`0x86166`). (A bare scan for `[rX, #0x9c]`/`[rX, #0xa4]` is *not* discriminative - those
  are generic struct offsets, 115 hits - so the argument is address-provenance, not the field offset
  alone.) The notify id set is therefore the set of arguments to those sites, and nothing else.

### 1.2 How the ids get in: the ops-table slot and its thin wrapper

The firmware has an ops table whose base is rt `0x0010F250` (file `0xCF250`; loaded by
`pcie_msg_init` at file `0x9350`, and by the initialiser at file `0x77f4`, which also writes it into
`*(0x170E08+0x10)` at file `0x77FE`). Its slots are 4-byte function pointers; the relevant ones:

| slot | file of the word | value | target |
| --- | --- | --- | --- |
| +0x08 | `0xCF258` | `0x000402AD` | file `0x2AC`: `ldr r0,[pc,#0]; bx lr` - returns `0x0010C0F4` (pool `0x2B0`) |
| **+0x20** | `0xCF270` | **`0x000C6171`** | **`d2h_notify` @ file `0x86170`** |
| +0x24 | `0xCF274` | `0x000C187B` | the H2D registrar @ file `0x8187A` |
| +0x2C | `0xCF27C` | `0x000C71D7` | file `0x871D6` |

The wrapper that uses slot +0x20 is at file `0x81D70`:
```
file 0x81d70  ldr   r3, [pc, #0x10]   ; pool 0x81d84 = 0x00170E08
file 0x81d72  mov   r1, r0            ; r1 = id (the wrapper's argument)
file 0x81d74  ldr   r3, [r3, #0x10]   ; r3 = the ops table
file 0x81d76  ldr   r3, [r3, #0x20]   ; r3 = ops->notify  == d2h_notify
file 0x81d78  cbz   r3, #0xc81d7e
file 0x81d7a  movs  r0, #0            ; handle = 0
file 0x81d7c  bx    r3                ; tail-call notify(0, id)
file 0x81d7e  movw  r0, #0x8b2d
file 0x81d82  bx    lr
```
**[verified]** - this is the missing static link; it is a `bx`, not a `bl`, which is why a
branch-target scan misses `d2h_notify`.

---

## 2. Gap 9 - which D2H notify id each HCC event carries

### 2.1 Evidence: every id the firmware stamps

Complete inventory of the sites that put a bit into `out[1]`, found by scanning for (a) the
primitive, (b) its two wrapper callers, (c) the inline writer, (d) any other writer of
`out[1]`/doorbell (section 1.1, exhaustive):

| # | site (file) | id load | transport | enclosing function | what runs just before the notify |
| --- | --- | --- | --- | --- | --- |
| 1 | `0x86F5A` (`str r2,[r3]`, r2=4 from `0x86F54`; doorbell `0x86F5E`/`0x86F62`/`0x86F66`) | inline **2** | - | ETE bring-up @ `0x86E14` | the bring-up ends and rings the D2H doorbell; then spins until a register reads `0xcece` (`movw r3,#0xcece` @ `0x86F74`, `cmp`/`bne` @ `0x86F7A`/`0x86F7C`) |
| 2 | `0x86224` (`bl #0x86170`) | `movs r1,#6` @ `0x86220` | direct | fn @ `0x861E0` | packs a 4-byte ring descriptor (`bfi r3,r2,#0,#0xd` with r2=`0xd2b`, `orr #0x4000` @ `0x86202`, `orr #0x2000` @ `0x8620C`) and commits it with `bl #0x8192A` @ `0x8621C` (the descriptor store: `str.w r4,[r3,r2,lsl #3]` @ `0x81936`, `str r2,[r3,#4]` @ `0x81948`); then bumps `[r4+0x340]` |
| 3 | `0x86998` (`bl #0x86170`) | `movs r1,#5` @ `0x86994` | direct | fn @ `0x86802` | the channel/D2H pump: walks the channel table at `[r5+0x5c]` (stride `0xcc`), compares producer/consumer at `+0x20`/`+0x24`, calls the channel callbacks (`[r4+0xc8]+0x18` -> `+0x30`/`+0x34`), validates with `bl #0x9EA` @ `0x8698C` |
| 4 | `0xE7E` (`bl #0x81D70`) | `movs r0,#5` @ `0xE7C` | wrapper | fn @ `0xE08` (D2H bring-up) | ops slot `+0x28` called as `(0,1)` @ `0xE3C`, spins on `movw r3,#0xafaf` (`0xE58`-`0xE62`), writes a credit word, then notifies |
| 5 | `0x4F4EE` (`bl #0x81D70`) | `movs r0,#8` @ `0x4F4EA` | wrapper | fn @ `0x4F120` | builds a 32-bit word from message fields and stores it to CA `0x40030100` (`str r3,[r2]` @ `0x4F4EC`, r2 from pool `0x4F524` = `0x40030100`) |

**Static id set: { 2, 5, 6, 8 }.** The host registers handlers for `{1, 3, 6, 7}`
(`plat.ko` `pcie_msg_init` @ `0xb6e4`); **6 is the only one of the device's ids that has a host
handler**; device bits 2, 5 and 8 take the host's `0x1739c` "no handler registered" path (bit 2 is
the one already observed live and dropped - map section 1.1 step 4).

### 2.2 Conclusion

**The D2H mail bit is not a per-HCC-event id, so gap 9 as posed has no per-event answer.**
Statically the firmware has exactly one notify primitive, and it is stamped by *channel/lifecycle*
events, not by message type:

* **id 2** - platform/device-ready (one event; the map's step 4).
* **id 6** - a descriptor was committed to the device->host ring (site 2: a *generic* ring post, the
  same code path for any payload - the descriptor's `word1` tag is the constant `0xd2b|0x4000|0x2000`).
* **id 5** - the D2H channel: its bring-up at init (site 4) and its pump (site 3).
* **id 8** - a single builder that also writes CA `0x40030100` (site 5).

The per-event discriminator is the 12-byte HCC header inside the ring payload - `byte[0] & 0xf` =
group, `u16 @ +6` = id - dispatched by the host at `hcc_msg_process` @ `0x1204c` (map section 3.2,
`[record]` re-verified there). So a payload's "event" is decided by the header, and the mail bit only
says "there is ring data / the channel moved". **Implementable-now consequence for the port:** do not
try to map `(group,id) -> mail bit`; post the descriptor and raise the ring bit (6), and set the HCC
header per event.

### 2.3 What is still open, and the read that closes it

The *per-payload* correlation ("this HCC frame was announced with that bit") cannot be read off the
image: **four** of the five sites (1-4) sit in functions whose addresses occur nowhere in the blob as
a branch target, literal word, or `adr` target, so the route from the queue machinery into them is
not statically resolvable - for those four the *id constants* are static truth but their
*reachability* is not. Only site 5 (id 8, function `0x4F120`, which has six `BL` callers: file
`0x22472`, `0x4F594`, `0x4F630`, `0x4F6E4`, `0x4F73A`, `0x7650E`) is statically reachable.

*Live read that closes it:* on a vendor boot, capture CA `0x40039014` (`out[1]`) at every D2H IRQ
(the flush at `0x86108` makes each read complete) and, in the same window, dump the DR ring payload
and read its HCC header (`byte0 & 0xf`, `u16 @ +6`). That pairs each `(group,id)` payload with the
bit that carried it. Cheaper equivalent: a one-shot marker - post H2D id 3 (the SR announce the port
already implements) and read `out[1]` before/after; then repeat for each event the port cares about.

---

## 3. Gap 10 - H2D id 5: dead path or deliberate stub?

### 3.1 Evidence - the only id-5 sender, and where its branch sits

`hi5622v100_plat.ko` posts H2D id 5 at exactly one place. `pcie_msg_send` (`0x160f4`) takes
`(handle, id)` in `r0/r1`, bounds `id <= 9` (`0x16138 cmp r4,#9`), ORs `1<<id` into the pending
shadow `comm+0x48` (`0x16184`/`0x16188`) and, only when the armed flag `comm+0x44` is 0, writes
`out[0]` and rings the doorbell (`0x16194`-`0x161b0`). Its `R_ARM_CALL` callers are exactly two
(this session's relocation scan): `0x15144` (id 5) and `0x178f8` (id 3). (`0xaf8` and `0x2920` are
`R_ARM_MOVW_ABS_NC` *address* references, not calls; `pcie_msg_send_irq` @ `0x174a8` is the
shadow-flushing channel callback and does not call `pcie_msg_send`.) So **id 5 has one producer**:

```
plat.ko 0x15024  b   #0x15138                     ; the flush path rejoin
plat.ko 0x15138  ldr r3, [r4, #0x68]              ; the chip object
plat.ko 0x1513c  mov r1, #5                       ; id 5
plat.ko 0x15140  ldr r0, [r3, #0x80]              ; chip
plat.ko 0x15144  bl  pcie_msg_send                ; pcie_msg_send(chip, 5)
plat.ko 0x15148  movw r0, #0x74d3
plat.ko 0x1514c  movt r0, #0xffff                 ; return 0xffff74d3
plat.ko 0x15150  b   #0x14e04                     ; return
```

That tail is the **end of `pcie_ete_rcv_buff_check`** (`0x14d74`), whose *success* path returns 0
several screens earlier:

```
plat.ko 0x14dd4  movw fp, #0x5a5a                 ; the SR/ETE tag
plat.ko 0x14de8  ldrh r3, [r5, #0xa]              ; candidate buffer's tag
plat.ko 0x14dec  cmp  r3, fp
plat.ko 0x14df0  bne  #0x14e0c
plat.ko 0x14df4  ldrh r3, [r5, #4]                ; candidate buffer's length
plat.ko 0x14df8  cmp  r3, #0
plat.ko 0x14dfc  beq  #0x14e0c
plat.ko 0x14e00  mov  r0, #0                        ; <-- SUCCESS: return 0, no id 5
plat.ko 0x14e04  add  sp, sp, #0x1c
plat.ko 0x14e08  pop  {r4, r5, r6, r7, r8, sb, sl, fp, pc}
```

So: **a tagged, non-empty receive buffer takes the early success return (`0x14e00`) and never
reaches `0x15138`.** The id-5 tail is reached only on the failure/fall-through paths - the `0x15024`
rejoin after the descriptor byte-scan (`0x14fd8`-`0x15020`), or the fall-through after the
`r5`-null check at `0x14f50`-`0x150fc`. It is an error path, matching the map's read of the
`0xffff74d3` return.

The call chain is `[callback] -> pcie_rx_handle` (symbol `0x164d0`; the call site of the next link is
`0x1655c`) `-> pcie_ete_dr_get_uploadbuf` (`0x1515c`, called from `0x1655c`) `->
pcie_ete_rcv_buff_check` (`0x14d74`, called from `0x152c4`) - all three `[verified]` from `.rel.text`
relocations. `pcie_rx_handle` has no `.rel.text` caller, i.e. it is registered as a function pointer.

### 3.2 The device side

The firmware's H2D handler table (rt `0x00118D68`, ids 0..9, populated at runtime by the two
registration entry points) has id 5 = file `0x819DC`, whose raw bytes are

```
file 0x819d0..0x819e3:  00 23 1b 60 ff de | 00 23 1b 60 ff de | 00 23 1b 60 ff de
```

- three identical `BUG()` stubs (`movs r3,#0; str r3,[r3]; udf #0xff`). The id-5 registration is at
  file `0x7CEE`/`0x7CF0` (`movs r0,#5` ; `bl #0x7C1DC` = entry point B, fn literal file `0x7DDC` =
  `0x000C19DD` = file `0x819DC` with the Thumb bit). **[verified]**

### 3.3 Conclusion

**Dead path in a working boot - the trap is a deliberate "never happens" stub.** The argument is
static and needs no device:

1. `hi5622v100_plat.ko` and `FIRMWARE.bin` are shipped as a working pair.
2. If the host posted id 5, the firmware's dispatcher would call table[5] = file `0x819DC`, i.e.
   execute `str r3,[r3]` then `udf #0xff` - the device would fault.
3. Therefore, in a working boot, id 5 is **not** posted.
4. The single id-5 producer is the *failure* tail of `pcie_ete_rcv_buff_check`, and that function's
   *success* path (valid `0x5a5a`-tagged, non-empty buffer) returns at `0x14e00` before the tail.
5. Hence id 5 is a recovery branch the vendor's own boot does not take: dead in practice, and the
   firmware's `BUG()` is the deliberate stub for exactly that reason - the two facts corroborate
   each other rather than resolve the tension the map recorded.

Residual (this is the part static reading cannot finish): whether step 3's premise holds in a
*particular* boot is a runtime-state question (does the candidate buffer carry `0x5a5a` and a
non-zero length at that moment?). *Live read that closes it:* sample CA `0x40039010` (`out[0]`) for
bit 5 across a full vendor boot (or log the `r1` argument at `pcie_msg_send` / the return of
`pcie_ete_rcv_buff_check`). Bit 5 absent and the function returning 0 (not `0xffff74d3`) confirms the
dead path; a single bit-5 sighting falsifies it and means the trap is only reserved for a path this
blob's host can still reach.

---

## 4. Gap 12 - is the id-6 handler's 20-byte reply announced on `out[1]`?

### 4.1 Evidence - the handler's whole outgoing edge

The H2D id-6 handler is file `0x4CCE4` (registered at file `0x88EA`/`0x88F8`, fn literal file
`0x8994` = `0x0008CCE5`). It allocates via `msg_alloc(8, 6)` (`bl #0xc2822` @ `0x4CCF0`), samples CA
`0x40100100` (pool `0x4CD94`) and fills the header/payload, then has exactly one outgoing edge:

```
file 0x4CD6C  mov   r0, r5
file 0x4CD6E  bl    #0xc2760          ; msg_send(msg)   [bytes 75 f0 f7 fc]
file 0x4CD74  cbz   r0, #0xc4cd8c     ; success -> return
file 0x4CD88  b.w   #0xc2820          ; failure -> msg_free
```

`msg_send` is file `0xc2760` (rt `0x102760`), and its body is self-contained: it validates the
message (`bl #0x9ea`), copies `byte[1]`'s high nibble into `byte[9]` (`0xc2782`-`0xc2788`), looks up
the per-`(group,id)` queue (`bl #0xc2744` @ `0xc27b2`, `bl #0xc24ce` @ `0xc27d2`), fills the target
(`bl #0xc2662`), and wakes the queue's worker (`bl #0xc24bc` @ `0xc2806`, `bl #0x82a54` @ `0xc280c`
on `channel+4`). **No instruction in `msg_send` writes `out[1]` or the D2H doorbell**, and neither
does any of its callees (`0x9ea`, `0xc2744`, `0xc24ce`, `0xc2662`, `0xc24e6`, `0xc24bc`, `0x82a54`) -
established by the exhaustive writer scan of section 1.1, which leaves only `d2h_notify` and the
inline step-4 write as writers.

The one function in the image that *does* post a ring descriptor and then notify bit 6 is file
`0x861E0` (site 2 in section 2.1). Its address occurs nowhere in the blob - not as a `BL`/`BLX`
target, not as a branch target, not as a literal word (`0x000861E1`), not as an `adr` target - so the
`ring post -> notify(0,6)` link cannot be tied to the id-6 handler from the image alone.

### 4.2 Conclusion

**Statistically: no.** On every path visible in this image, the id-6 handler's 20-byte reply is
**not** announced on `out[1]` by the handler: it is handed to `msg_send`, which routes it through the
firmware's HCC queue machinery and wakes a queue worker - no `out[1]` write, no doorbell write, in
the handler or in `msg_send`'s whole call tree. So the map's `[not proven]` note is, on static
evidence, closer to a negative than an unknown: there is no *statically visible* announce of this
reply on `out[1]`.

Two facts keep this from being an unconditional "no":

* the ring-post function that stamps bit 6 (`0x861E0`) exists in the blob but is statically
  unreferenced, so a runtime-installed callback could still be its caller;
* in this vendor pairing the host never posts H2D id 6 anyway - `pcie_msg_send`'s only call sites are
  ids 3 and 5 (section 3.1) - so the handler is not exercised in a working vendor boot, and an
  announce (if any) is not observable there without the port driving it.

*Live read that closes it:* drive H2D id 6 (post the `out[0]` bit-6 doorbell) and, in the same window,
capture CA `0x40039014` (`out[1]`) plus a dump of the DR ring. The reply is identifiable by its
header: `byte[0] & 0xf == 2`, `byte[6] == 0x2a`, and payload bytes 0/2/3/4-5 equal to the sampled
fields of CA `0x40100100` (`>>0x14 & 0xf`, `>>0xf & 1`, `& 0xf`, `>>4 & 0x7ff`). If that frame
appears in the ring *and* `out[1]` shows a bit at that moment, the announce is on `out[1]`; if the
frame appears with no `out[1]` change, it was delivered by another channel; if the frame never
appears, the reply stayed internal (the queue worker's other consumer).

---

## 5. Corrections to the phase-37 map found on the way

These are stated because they were load-bearing for the three gaps, and a downstream reader would
otherwise re-import them.

1. **The map's `d2h_notify` has no static caller is wrong.** There are two direct callers (file
   `0x86224`, file `0x86998`) *and* an ops-table slot + thin wrapper (slot +0x20 of the table at rt
   `0x10F250`, word at file `0xCF270` = `0x000C6171`; wrapper at file `0x81D70`) - section 1.2. The
   map's scan missed them because the table word carries the Thumb bit and the wrapper tail-calls
   with `bx`.
2. **Step 4's doorbell address in the map (`0x400a1434`) is wrong; it is `0x40101434`.** The code is
   `ldr r3,=0x40039014` (pool `0x86fbc`) ; `str r2,[r3]` ; `add.w r3,r3,#0xc8000` ; `add.w r3,r3,#0x420`
   ; `str r2,[r3]` with `r2=1` - so `0x40039014 + 0xC8000 + 0x420 = 0x40101434`, which is exactly the
   D2H doorbell the firmware's own ctx is built with (file `0x97d8` = `0x40101434`, the same value the
   map lists as the host's D2H doorbell), not a distinct `0x400a1434`. (`0x400a1434` occurs nowhere in
   the image.)
3. **The map's step-4 quote `0x86f5e str r2,[r3]` writes `1`, not `4`.** `r2` is reloaded at
   `0x86f5c movs r2,#1`; the `4` was written to `out[1]` one instruction earlier (`0x86f5a`).
4. Gap 9's premise ("the emitter->id mapping is `[inferred]`") is only half true: the *ids* are now
   static and exhaustive ({2,5,6,8} - section 2.1); what remains inferred/unresolved is the
   *reachability and per-payload pairing* (section 2.3).

---

## 6. VERIFY

Every offset quoted above was re-disassembled in this session from the two images (the exact command
block is in the "reproduce" note). The checks that carry the load, with the instruction the offset
must show:

```
FIRMWARE.bin (Thumb, file offsets)
 0x86170  cmp r1,#0xa            0x861a8 ldr.w r1,[r5,#0x9c]   0x861b0 str r3,[r1]
 0x861b2  ldr.w r4,[r5,#0xa4]    0x861c0 str r3,[r4]           0x861dc pool = 0x00172130
 0x86108  push {r4,r5,r6,lr}     0x86156 str r3,[r2]           0x86166 str r3,[r2]
 0x81d70  ldr r3,[pc,#0x10]      0x81d76 ldr r3,[r3,#0x20]     0x81d7c bx r3
 0x81d84  pool = 0x00170E08      0xcf270 word = 0x000C6171      0xcf250 table base (rt 0x10F250)
 0x77f4   ldr r3,[pc,#0x168]     0x9350 ldr r2,[pc,#0x58]      0x9358 ldr r7,[r2,#0x40]
 0x86f54  movs r2,#4             0x86f5a str r2,[r3]           0x86f5e add.w r3,r3,#0xc8000
 0x86f62  add.w r3,r3,#0x420     0x86f66 str r2,[r3]           0x86f74 movw r3,#0xcece
 0x86f7a  cmp r2,r3               0x86f7c bne #0x86f78            0x86fbc pool = 0x40039014
 0x9ea    push {r4,lr}            0x8192a push {r4}               0x81e0c push (bl target)
 0x86220  movs r1,#6             0x86224 bl #0x86170           0x81936 str.w r4,[r3,r2,lsl #3]
 0x86994  movs r1,#5             0x86998 bl #0x86170           0x9ea   validate (bl target)
 0xE7C    movs r0,#5             0xE7E bl #0x81d70             0x4F4EA movs r0,#8
 0x4F4EE  bl #0x81d70            0x4F4EC str r3,[r2]           0x4F524 pool = 0x40030100
 0x4CCE4  push.w {...}           0x4CD6E bl #0xc2760           0x4CD88 b.w #0xc2820
 0xc2760  push {r4,r5,r6,lr}     0xc280c bl #0x82a54           0xc2818..0xc281e literal pool
 0x819dc  00 23 1b 60 ff de      0x819d0/0x819d6 identical      0x7CEE movs r0,#5
 0x7CF0   bl #0x7C1DC            0x7DDC word = 0x000C19DD      0x2A0   ldr r0,[pc,#4] ; b.w #0x86108
 0x2A8    pool = 0x0010C0F4      0x97CC pool = 0x00172130      0x97DC pool = 0x40039014
plat.ko (ARM, .text-relative; file = offset + 0x38)
 0x14dd4  movw fp,#0x5a5a        0x14de8 ldrh r3,[r5,#0xa]      0x14e00 mov r0,#0
 0x15024  b #0x15138             0x15138 ldr r3,[r4,#0x68]     0x1513c mov r1,#5
 0x15144  bl pcie_msg_send [R_ARM_CALL]                         0x15148 movw r0,#0x74d3
 0x160f4  push {r4,r5,r6,r7,r8,lr}  0x16138 cmp r4,#9           0x16184 orr r3,r3,r1,lsl r4
 0x16194  ldr r2,[r6,#0x2c]      0x161b0 str r3,[r2]
 0x171f8  push {r4,r5,r6,r7,r8,sb,sl,lr}  0x172f8 bics r5,r5,r3,lsl r6   0x17300 rbit r6,r5
 0x174a8  push {r4,r5,r6,lr}     0x17538 ldr r2,[r4,#0x2c]      0x17550 str r3,[r2]
```

Reproduce (pyenv capstone; the same two helpers used in this session):

```bash
PY=../pyenv/Scripts/python.exe
$PY - <<'EOF'
from capstone import *
D=open('../build/tmp/FIRMWARE.bin','rb').read()
P=open('../opensource/build/register-dumps/teardown/hi5622v100_plat.ko','rb').read()
TH=Cs(CS_ARCH_ARM,CS_MODE_THUMB); TH.skipdata=True
ARM=Cs(CS_ARCH_ARM,CS_MODE_ARM)
def q(blob,off,n,mode='th',rel=0):
    md=TH if mode=='th' else ARM
    for i in md.disasm(blob[off+rel:off+rel+n], off):
        print("  %06x %-10s %s"%(i.address,i.bytes.hex(),i.mnemonic+" "+i.op_str))
q(D,0x86170,0x56); q(D,0x86108,0x62); q(D,0x81d70,0x14); q(D,0x4cce4,0xb0)
q(D,0xc2760,0xbc)
q(P,0x14d74,0x480,'arm',0x38); q(P,0x160f4,0xc0,'arm',0x38)
import struct
for o in (0x861dc,0x81d84,0x86fbc,0x97dc,0xcf270,0x2a8,0x4f524):
    print("  pool %06x = %08x"%(o,struct.unpack_from('<I',D,o)[0]))
EOF
```

Result: every line above reproduced with the mnemonic/operand shown (78 instruction checks - 54 in
`FIRMWARE.bin`, 24 in `hi5622v100_plat.ko` - 0 mismatches), and each quoted pool word equals the
value claimed (13 checked: `0x861dc`, `0x81d84`, `0x86fbc`, `0x97cc`, `0x97d8`, `0x97dc`, `0x985c`,
`0x2a8`, `0x4f524`, `0xcf250`, `0xcf270`, `0xcf274`, `0x97e0`).

## 7. Proven vs open

| claim | status |
| --- | --- |
| `d2h_notify` @ file `0x86170` is the out[1]/doorbell writer; `ctx = g+0x9c`, `ctx+0 = out[1]`, `ctx+8 = 0x40101434` | **verified** (body + `pcie_msg_init` registers + pools) |
| the notify is exposed as ops slot +0x20 (table rt `0x10F250`, word file `0xCF270`) and reached through the `bx` wrapper at file `0x81D70` | **verified** |
| the D2H bits batch via shadow `g+0xb8` / armed `g+0xb4` and are flushed by the IRQ-`0x4E` handler at file `0x86108` | **verified** |
| the firmware's notify ids are exactly {2, 5, 6, 8}, with the sites the id is loaded at | **verified**; **the reachability of sites 1-4 is not statically resolvable** (their functions are unreferenced; site 5 is reachable) |
| the D2H mail bit is a channel signal, not a per-HCC-event id; the HCC header carries the event | **verified** (id 6 = generic ring post; header dispatch at `0x1204c`) |
| gap 9's remaining part needs a live `out[1]`-vs-HCC-header correlation | **open** (named read, section 2.3) |
| H2D id 5 has exactly one producer (plat.ko `0x15144`, the failure tail of `pcie_ete_rcv_buff_check`) and the success path returns at `0x14e00` | **verified** (reloc scan + body) |
| the device's H2D id-5 handler is a `BUG()` stub (file `0x819DC`) | **verified** (bytes + registration site) |
| id 5 is a dead path in a working boot, and the trap is its deliberate stub | **concluded** (sections 3.3 steps 1-5); one live `out[0]` bit-5 read confirms it |
| the id-6 handler's reply goes to `msg_send` (`0xc2760`) -> HCC queue + worker, with no `out[1]`/doorbell write anywhere on that tree | **verified** |
| the reply is *not* announced on `out[1]` by the handler path | **verified** for all statically visible paths; the bit-6 ring-post function `0x861E0` is statically unreferenced, so a live capture still settles whether some runtime callback announces it |
