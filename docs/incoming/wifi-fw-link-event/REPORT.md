# Firmware-internal link/event bring-up: the boot-region "dispatch table", the event pump, and why the boot never dispatches it (fw-link-event lane)

Host-only lane "fw-link-event", shared task task-76, 2026-11-23.
Inputs are local artifacts only: the vendor firmware image at
/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
(928,920 B, **local evidence only - never copied or committed**) and the committed prior
lane reports (cited, not repeated). No device was contacted; no image was built; nothing
was written outside this directory.

Evidence markers: **[V]** verified here from the image with tools/fw_cfg.py;
**[V*]** device-measured by an earlier attended lane and cited; **[I]** inference from
code structure; **[U]** unknown / not decidable from the image.

Address conventions: CPU = file + 0x40000; host BAR0 (CPU RAM, region 5) = CPU + 0x6B8000;
host BAR0 (device IO, region 3) = 0x3B8000 + (CA - 0x40000000); the region-0 identity window
maps BAR0 offset = CPU for the ROM/WRAM view (CPU < 0x40000).

---

## 0. TL;DR - answers to the four questions

1. **The "dispatch table at CPU 0x474B8" is not a table.** It is the embedded literal pool
   of the boot entry A function (CPU 0x46970..0x474B8). A complete census of every
   PC-relative literal load whose word points into 0x474B8..0x47528 returns **30 sites,
   all inside entry A** (0x471C0..0x4744A); no other code and no ROM-visible table prologue
   references it [V, tools/fw_cfg.py pool 0x474B8 0x47528]. The word 0x00046849 is loaded
   at 0x471F0 and 0x4720E and handed to the system-object event registrar
   0xC6DA2(id,level,handler) -> it is registered as the handler for **hardware event
   vectors 0x3E and 0x3F** in the handler table CA 0x17D430 [V]. The same block writes the
   boot global CA 0x117C00 (+0xEE0/+0xEE4/+0xEE8) and queues the boot task pointer 0x47625
   through the task framework (0xC745C) before calling the task loop 0xC704C [V]. **It
   builds event-pump wiring, per-core queue heads, RAM objects and the event handler
   registrations - not the per-link +0x4B0 context.**
2. **CPU 0x46848 is not the link-create worker; it is the event pump** for the device event
   block CA 0x40036xxx. It is invoked only as handler 0x3E/0x3F of the event dispatcher
   0xC2EFC and dispatches the boot-global callbacks 0x461B7 / 0x467D9 per set status bit
   [V]. The per-link [obj+0x4B0] = [sb+8] bind is at 0x48840 **inside the boot task
   (0x47628..~0x492xx)**, reached only through the boot task's post-send init chain
   (0x48160 -> 0x47D28 -> 0x49188 -> ... -> 0x48336 -> ... -> 0x4881E), never through the
   0x474B8 data [V].
3. **The five "event-thread loop entries" are five "bl 0xC5128" call sites** of the shared
   thread post/wake tail (sem at obj+0x98, queue post at obj+0x50), not function entries
   [V]. Their prerequisites are the queue/thread objects built by the boot task setup branch
   (CA 0x117C00+0x214..+0x250, +0x270..+0x27C) and - upstream - the event dispatch layer
   that feeds them (section 5).
4. **Why our boot misses the bootstrap (the decisive new finding).** The image contains the
   IRQ/FIQ second-level trampoline at **CPU 0x40064** (image header +0x64); it saves the
   full context, calls the event dispatcher **0xC2EFC**, and returns with rfeia [V]. 0xC2EFC
   reads the device event-status word CA 0x4016010C, takes its low 10 bits as an index into
   the handler table CA 0x17D430 and blx-es the registered handler [V]. **0xC2EFC has
   exactly one caller in the whole image: that ROM-driven trampoline** (no BL, no literal
   pointer, no movw/movt) [V]. Every firmware-internal link/event bring-up we can find is
   one of its registered handlers: 0x3E/0x3F -> event pump 0x46848, 0x40/0x42 -> status
   dispatchers, 0x2D/0x2E -> ETE ring completion, 0x4C -> the H2D mailbox message dispatcher
   0xC18AC, 0x4E -> the D2H flush 0xC6108 [V]. The host-visible consequence is already
   measured: the H2D mailbox probe wrote out[0] and the firmware never dispatched/acked it
   (out[0] stuck, ack=0, -110) [V*, wifi-hcc-link section 5.5] - i.e. event 0x4C was never
   dispatched, so the whole registered-handler layer is idle. **The missing input is a
   delivered hardware event (IRQ assertion/vectoring into the image), not a host message,
   not a RAM word, and not a table walk.** The single next bench action is a read-only
   evidence pack (section 8) that separates "events pending but not delivered" (ROM IRQ
   vector/mask gap) from "event sources never asserted" (device event router never armed).
   Neither can be fixed from the host side.

---

## 1. Method and the correction that unlocks everything

Prior lanes treated the boot-region words at CPU 0x474B8 as a pointer/dispatch table with an
unknown consumer ("who walks that table?"). The image is a flat blob with embedded literal
pools; entry A uses PC-relative ldr instructions, so its pool contains a mix of code
pointers, RAM-object pointers and device CAs that looks exactly like a table.

The decisive check is the inverse literal-load census (tools/fw_cfg.py pool LO HI):

    $ PY=.provision-venv/bin/python
    $ $PY tools/fw_cfg.py pool 0x474B8 0x47528
    load 471C0 (lit 474B8) = 00172A88   insn ldr r7, [pc, #0x2f4]
    load 471EE (lit 474BC) = 000467D9   insn ldr r3, [pc, #0x2cc]
    load 471F0 (lit 474C0) = 00046849   insn ldr r2, [pc, #0x2cc]
    ...
    load 4744A (lit 47524) = 003479F6   insn ldr r2, [pc, #0xd8]

All 30 load sites lie in 0x471C0..0x4744A, i.e. inside entry A; there is no walker, no
indexed reader, no ROM-side table contract visible in the image. **The "table" is data, not
a dispatch table** [V]. The previous statement that 0x46849's "only in-image pointer
reference is a thumb pointer inside a boot-region pointer table" is therefore correct as a
byte observation but misleading as a semantic one: the pointer is consumed by entry A's own
literal load at 0x471F0/0x4720E [V].

Also corrected here: the per-link +0x4B0 store 0x48840 is *not* "inside the link-create
worker 0x46848"; it is inside the boot task 0x47628's large body (section 4) [V].

---

## 2. The consumer: entry A (CPU 0x46970) and what it builds

Entry A is header word +0x00 (0x00046971, Thumb). It runs a long system-init body with
embedded pools at 0x46A60, 0x46DAC, 0x46DE0, 0x47150.. and 0x474B8. Using the new fw_cfg.py
CFG walk (recursive descent from 0x46970) the block that consumes the 0x474B8 pool is
0x471C0..0x4725A plus the tail 0x473CC..0x4742E [V]:

### 2.1 Per-core event queue heads + boot-global pump wiring (0x471C0..0x4721C)

| site | effect | evidence |
| --- | --- | --- |
| 0x471C0 | r7 = CA 0x00172A88 (RAM), r8 = r7+0x380; loop over 8 entries of 0x70 bytes: 0xC71B8(r7) list init, [r7-0x20] = -1, list head pair at [r7-0x10], zero at [r7-8] | [V] |
| 0x471CA | boot global CA 0x117C00: +0xEE0 = 0 (event-status base offset) | [V] |
| 0x471F0/0x4720E | load 0x46849 from the pool, 0xC6DA2(0x3E,5,0x46849) and 0xC6DA2(0x3F,5,0x46849) -> handler table CA 0x17D430[0x3E]/[0x3F] = 0x46849, then enable via 0xC6FF4 | [V] |
| 0x471F2 | [0x117C00+0xEE8] = 0x000467D9 (pump callback B) | [V] |
| 0x471FC | [0x117C00+0xEE4] = 0x000461B7 (pump callback A) | [V] |
| 0x4720A..0x47218 | r4 = CA 0x00172DE8 (RAM object), r2 = 0x46849, register/enable 0x3F | [V] |
| 0x47222..0x4725A | initialise RAM objects CA 0x00172DE8 / 0x17DEA8 / 0x18083C with 0xFDA0C (object init) and store code pointers 0x1041F1/0x41691/0x404FF/0x104204 | [V] |

### 2.2 Status dispatchers (0x46F64, 0x46F74)

Entry A also registers two further event handlers with the same registrar:

* 0xC6DA2(0x40,5,0x40670) - handler 0x40670 reads device CA 0x401060A0 and tail-calls
  0xBC10A(0x10E6BC, value) [V];
* 0xC6DA2(0x42,5,0x4065C) - handler 0x4065C reads device CA 0x401190A0 and tail-calls
  0xBC10A(0x10E6EC, value) [V].

0xBC10A is a 4-bit status fan-out: for each set bit it takes [obj + 0x0C*i + 8] and blx-es
it; i.e. another event-dispatch tier [V].

### 2.3 System object, boot-task queue and the scheduler kick (0x46E70, 0x473CC, 0x47410)

| site | effect |
| --- | --- |
| 0x46E70..0x46E94 | CA 0x17D398 + 0x00 = ((CA 0x40161004 & 0x1F)+1) << 5 (core-count word); handler table CA 0x17D430 at +0x98: [0]=0xC2081, [1]=0xC20A3, [0x1D]=0xC6C89, plus more entries in the core-count branch arms 0x47496/0x474A0/0x474AA [V]. The live read of 0x17D430[0] = 0x000C2081 [V*] proves entry A ran at least to here. |
| 0x473CC..0x473F0 | r4 = CA 0x001BF008; store {0, 0xABCD}; cache flush blx 0x102EE4 (ARM); CA 0x40161F00 = 0x20000; poll [0x1BF008] == 2, calling 0x102E68 (ARM cache-maintenance) in the wait loop [V] |
| 0x47410..0x47428 | build a task descriptor {src blob = 0x00104243, boot fn = 0x00047625, ..., kind = 7} and hand it to 0xC745C (queue/task register); then bl 0xC704C (the system task/scheduler entry, same tail as header entry B 0xC742C) [V] |

The pool word 0x00047625 (Thumb pointer to the boot-task loader stub 0x47624, which falls
through into 0x47628) is materialised **only here** in the whole image [V]. Prior lanes
inferred that the ROM arms the reset-dispatch slot CA 0x17D42C with 0x47629; the image
never stores 0x47625/0x47629 into 0x17D42C (the only write of that slot in image code is the
boot task's own disarm to 0x000C19D1 at 0x47D06) [V]. Whether the task framework (0xC745C)
ultimately makes 0x47625 reachable via the reset dispatcher is **[U]** - no direct store
path from 0xC745C to 0x17D42C was found. What *is* proven: the boot task itself ran on a
cold boot (state byte + slot disarm) [V*], and 0x47410 is the only image-side site that
could have supplied its address [I].

---

## 3. What 0x46848 really is: the device event pump (handler 0x3E/0x3F)

CPU 0x46848 (registered as above) disassembles as [V]:

    push.w {r4..fp,lr}; sub sp,#0x2c; memset(sp+8,0,0x20)
    r7 = [lit 0x46920] = CA 0x00117C00            ; boot global
    r4 = [r7+0xEE0] (=0 at boot)                  ; status base offset
    status = word[ ((r4 + 0x8000000 + 0x6D00) << 3) & 0xFFFFFFFF ]   ; == CA 0x40036800 at boot
    loop over set bits (lowest first):
        bit = 31 - clz(status & -status)
        block = CA 0x40036000 + 64*bit            ; per-event 64-byte block
        if (ctrl & bit) && (block.flags & 0x80):
            buf[0x20] = block[0x00..0x20]
            cb = [r7+0xEE4] (=0x461B7); if cb: cb(bit, buf)
        if (ctrl & bit) && (block.flags & 0x40):
            buf[0x20] = block[0x00..0x20]
            cb = [r7+0xEE8] (=0x467D9); if cb: cb(bit, buf)
        clear bit in status

The 0x20-byte payload handlers are: 0x461B6 -> per-core/per-block record processing
(0x70*bit stride, core-id-based object lookup, posts to a queue) and 0x467D8 -> a small
{u8 table; u8 index; ...} request dispatcher that selects a handler from a table and calls
it [V]. A separate small function 0x46938 (also registered as an event handler, at 0xBEE40 -
id not decoded) raises the current core's pending bit via 0xC6DB8 and tail-calls the
boot-global hook [0x117C00+0xEEC] [V]; the boot task later sets that hook to the BUG-stub
address 0x000C19D7 (0x47D02), so no real kick is installed by the image boot - another sign
that the event layer is expected to replace it later [I].

**Consequence:** the +0x4B0 dereference chain of the abort (tag callbacks 0x745E0/0x7469C via
dispatcher 0x7243C) is *not* fed by this pump or by the 0x474B8 pool. The pump is an
independent, radio/device-event service.

---

## 4. Where the per-link +0x4B0 context is really built (boot task 0x47628)

The boot task (invoked through the reset/task path; header entry A hands its pointer to the
task framework, section 2.3) is one large function 0x47628..~0x492xx with embedded pools.
The per-link bind block 0x4881E..0x48F6x is reached only through this chain [V, CFG walk]:

    0x47628  boot task prelude: mmap regs (0x1048C48/0x1042DE8/0x103B048/0x102ABA8),
             RAM object 0x172134, 6-record config-table walk (0x4769E..0x4774A),
             per-record init 0x47756..0x477EA,
             0x477EC: object 0x170E08 -> callback [0x10F250+8] (if any) and
                      [0x10F250+4] = 0x49334 module/thread init;
                      if 0x49334 returns 0 -> registry populate at 0x47ADC
    0x47ADC  memset CA 0x171B6C, 0x490; fill 0x24-byte destination records from a
             0x2C-stride config table; per-core record CA 0x171FB4 = HCC instance
             resource record (see wifi-h2d-catalog section 4.1)
    0x47D0A  state check [0x117C00+0xF60]; 0/1 -> 0x48160
    0x48160  setup branch: state=1 (0x48174); thread/callback objects
             [0x117C00+0x214..0x250], queue heads [+0x270..+0x27C]; b 0x47D28
    0x47D28  id-1 message build + send 0x102760; on success -> 0x49188
    0x49188  second frame build (table CA 0x1043A4) + send; gate 0x491F0:
             [CA 0x17D420] must be non-zero (else deliberate BUG 0x491FE);
             [CA 0x17D424] selects 0x48292 vs 0x49204  [V]
    0x48292/0x49204  post-send init chain: 0x700-byte memset at CA 0x170E28,
             per-module blocks from CA 0x119758 (0xB40 stride, 0xD5C0 end),
             CA 0x169E90, CA 0x118C6C ...
    0x48336  register the firmware H2D handler tables into the HCC instance
             (group-2 table CA 0x10CC18, group-0 CA 0x10CCFC; matches
             wifi-h2d-catalog section 4.2 registry sites CPU 0x4833C/0x48346)  [V]
    0x4845C .. 0x487B6  config branches (0x101978 / 0x1471CC ...)
    0x4881E  per-link object build:
               sb = 0x9438C(byte[r5+1]);  r4 = 0x961AE(byte[r5])
               [r4+0x4B0] = [sb+8]        <-- THE per-link context bind
               [r4+0x4B8] = 0xA4FC8(byte[r5+2]); [r4+0x4AC] = 0
               0xFE6BC(...); [r4+0xAB8] = 4; 0x1A58-byte memset; ...
             failure paths -> 0x48F68

So the per-link +0x4B0 contexts are created by the **boot task's own post-send init chain**.
They are gated by (a) the boot task reaching and passing 0x47D28 -> 0x49188 (id-1 send
success plus the 0x17D420/0x17D424 state), and (b) the config item at r5 selecting an
existing link/channel. Reaching 0x48820 is **not** conditioned on any host H2D message
(there is no host-call edge into the boot task) [V negative, consistent with wifi-hcc-link
section 5].

Bridge function 0x437B0 (contains the other +0x4B0 bind 0x44BBC) is a separate data-path
function; its only H2D caller is group-2 id-9 hmac_send_mgmt_to_host (a post-boot
management path) [V, wifi-hcc-link section 5.2], not an init/bring-up message.

---

## 5. The five "event-thread loop entries" and their prerequisites

All five addresses are "bl 0xC5128" call sites, i.e. *uses* of the shared thread post/wake
tail, not function entries [V]. 0xC5128(obj): if obj, lock obj+0x98 (semaphore) with value 1
and tail 0xC2A54(obj+0x50) = post one item to the object's queue [V].

| call site (CPU) | enclosing function | what it is | prerequisite |
| --- | --- | --- | --- |
| 0xC630A | 0xC497E | per-user / per-frame HCC processing (0xFC0/0x7E0 index strides, [r3+0x5AB], CA 0x173800 table, calls 0xC1906/0xC5146) | a received data/mgmt frame + a live user/VAP object |
| 0xC63B4 | 0xC497E | second exit branch of the same per-frame processor | same |
| 0xC68FE | 0xC497E | third exit branch (per-message helper [fp+0x18]+0x30) | same |
| 0xC71F4 | 0xC52B4 (loop body 0xC71D6) | threaded message worker: init 0xC71B8, wait 0x1024BC, [obj+0x4C]=1, wake 0xC5128, then poll [obj+0x48] with a 0x1F4-timeout wait 0xC23A8 | a created thread object + a queued message |
| 0xBF102 | 0xBExxx H2D/mailbox service block | reads [CA 0x17D398+0x230], if non-zero wakes it via 0xC5128 | a live service/thread object registered at 0x17D398+0x230 |

Shared prerequisites (all built by the boot task / init layer, not by the image's event
data):

* thread/queue objects at CA 0x117C00+0x214..0x250 and queue heads +0x270..0x27C
  (setup branch 0x48160) [V + V* live: list heads/thread objects initialised];
* constructors 0xC5218/0xC745C (0xC71B8 list init, object init) and the message-layer
  g object CA 0x10C0F4 (g+0x9C = HCC instance CA 0x10C190) [V];
* **an event to feed them**: the dispatcher 0xC2EFC and the message/SR path are the only
  producers; without event delivery these loops stay parked (0xC71F8 polls [obj+0x48] with
  a timeout) [I].

So the five sites are *downstream consumers* of the event layer. Mapping them is not a way
to start bring-up; it confirms what stalls when the event layer is idle.

---

## 6. The event dispatcher chain (the real firmware-internal bootstrap)

### 6.1 The IRQ/FIQ trampoline at image CPU 0x40064 (file 0x64) [V]

The image header is +0x00 entry A (0x46970), +0x04 entry B (0xC742C), +0x08..+0x4C zero,
+0x50..+0x60 five ARM exception stubs, and **+0x64 ARM code**:

    0x40064 sub lr,lr,#4 ; srsdb sp!,#0x13 ; cps #0x13 ; push {r0..ip}
    0x40074 and r1,sp,#4 ; sub sp,sp,r1 ; push {r1,lr}
    0x40080 page = sp & ~0xFFF ; r5 = [page+4] ; [page+4] = r5+1
    0x40094 blx 0xC2EFC                  <-- the event dispatcher
    0x40098 [page+4] = r5 ; if (r5==0 && [page]==0) bl 0x103B50 (reset post-path helper)
    0x400BC pop {r0..ip} ; rfeia sp!

This is the standard second-level IRQ/FIQ entry (save context, run the handler, return with
rfeia). It is the **only caller of 0xC2EFC in the entire image** [V]. Since no image code
branches to 0x40064 either (it is not reachable from any header entry, BL, or pointer),
**the ROM's exception vector must be the invoker** [I]; the previous lane's description of
the same trampoline region ("ARM code second-level exception-return / event-dispatch
trampoline") is confirmed and now tied to the dispatcher call site [V].

### 6.2 Event dispatcher 0xC2EFC [V]

    r7 = word[CA 0x4016010C]          ; device event-status/IRQ word
    r4 = CA 0x17D398                  ; system object
    index = r7 & 0x3FF
    handler = (index <= 0x5F) ? word[0x17D398 + 4*index + 0x98] : 0
    ... lock CA 0x17D3D8, per-core accounting ...
    cpsie i ; if (handler) blx handler ; cpsid i
    word[CA 0x40160110] = r7          ; ack/clear the dispatched status word

So the handler table is CA 0x17D430 (0x60 entries, 4-byte Thumb pointers); index = low 10
bits of the device event-status register. The registered handler set recovered by a full
BL/registrar census (tools/fw_cfg.py regs, [V]) is:

| event id | handler | what it does | registrar (image site) |
| ---: | --- | --- | --- |
| 0x02 | 0xC20F9 | low-level per-core service | boot task 0x47CE0 |
| 0x2D | 0x462F9 | PCIe/message service (object CA 0x10F1F0) | pcie_msg_init 0x496CA |
| 0x2E | 0x4624D | ETE receive-completion ring processing (CA 0x10F1F0, ring CA 0x10CAD0/4) | pcie_msg_init 0x496DC |
| 0x3E | 0x46849 | device event pump 0x46848 (section 3) | **entry A 0x47200** |
| 0x3F | 0x46849 | device event pump 0x46848 | **entry A 0x47212** |
| 0x40 | 0x40671 | status CA 0x401060A0 -> 0xBC10A(0x10E6BC, val) | entry A 0x46F70 |
| 0x42 | 0x4065D | status CA 0x401190A0 -> 0xBC10A(0x10E6EC, val) | entry A 0x46F74 |
| 0x46 | 0x40525 | CA 0x4000604C service + queue CA 0x117E94 | boot task 0x4821A |
| 0x4C | **0x40295** | wrapper -> mailbox message dispatcher **0xC18AC** with HCC instance CA 0x10C190 | **pcie_msg_init 0x49800** |
| 0x4E | **0x402A1** | wrapper -> D2H flush **0xC6108** with g=CA 0x10C0F4 | **pcie_msg_init 0x4980C** |
| 0x2F,0x31..0x34,0x35,0x37..0x3A,0x3B,0x3C,0x3D,0x52,0x53,0x54,0x55 | PHY/radio handlers (0xAExxx-0xB0xxx) | radio interrupt service | PHY init block 0xD1Exx/0xD3Fxx |

Two consequences that close the loop with the H2D lane:

* **Event 0x4C is the H2D mailbox message notification.** pcie_msg_init both software-raises
  it (0xC6DB8(0x4C) at 0x49754) and registers + enables its handler (0x49800/0x49816) [V].
  It also registers event 0x4E = D2H flush and raises it at 0x4975A [V]. Therefore the
  firmware expects host-mailbox traffic to arrive as a **hardware event**, dispatch through
  0xC2EFC, and only then run the mailbox dispatcher 0xC18AC that reads/clears out[0].
* The prior H2D probe result (out[0] bit3 stuck, ack=0, timeout -110) is exactly "event 0x4C
  never dispatched": the registered handler layer is idle because the IRQ entry never runs
  [V*, consistent with wifi-hcc-link section 5.5].

### 6.3 Why no H2D handler can substitute

No H2D handler reaches the dispatcher or the link/event layer (wifi-hcc-link section 5.2,
re-confirmed): the mailbox dispatcher itself is invoked only as event 0x4C. A host frame
that the mailbox dispatcher never reads cannot start anything downstream of it. This is
the structural reason the H2D lane is closed, and it is *not* fixed by a better frame.

---

## 7. Why our reachable boot state never enters the bootstrap

Observed state after our transport boots the image (prior lanes, [V*]): entry A ran (live
0x17D430[0] = 0xC2081; system object +0x00 populated), pcie_msg_init ran (g = 0x0010C0F4),
boot task ran its first pass (state=1, registry/HCC instance CA 0x171B6C populated, reset
slot disarmed to 0xC19D1), mask 0x0A (SVC + data abort seen), DR fills 0, per-link/registry
+0x4B0 words zero.

Reasoning:

1. **All firmware-internal link/event bring-up code is behind the event dispatcher.** The
   registrations made by entry A and pcie_msg_init install handlers in CA 0x17D430, but the
   only invocation path is the ROM-driven IRQ trampoline 0x40064 -> 0xC2EFC [V]. There is
   no image-side call/pointer reference to 0xC2EFC (negative BL + literal + movw/movt scan)
   [V]. Our transport has never observed an event-dispatch side effect: the H2D mailbox
   probe (event 0x4C) was not dispatched/acked, and the D2H announce/flush (event 0x4E) was
   never driven by the firmware's event layer either [V*].
2. **The missing condition/input is a delivered hardware event**, i.e. the device event
   controller CA 0x4016010C asserting an enabled event line and the CPU taking the ROM IRQ
   vector. That is outside the image (ROM-owned vector path; the image only provides the
   trampoline at 0x40064). Its prerequisites are device-internal (event-source enables in
   the 0x40160xxx/0x40036xxx blocks and the IRQ vector installation); none of them is a
   host-writable register we have identified, and the host cannot fabricate the event.
3. **Second, independent gap (do not conflate):** the per-link +0x4B0 contexts are built by
   the boot task's post-send chain (section 4), which is gated on the id-1 send success and
   the 0x17D420/0x17D424 state - not on the event layer. Even with event delivery fixed,
   the boot task must continue past 0x47D28/0x49188 to reach 0x48820. The observed data
   abort in the DR/descriptor path (wifi-abort-rca) occurs in a different subtree during
   this era and may be a separate missing precondition; the read pack in section 8
   distinguishes which of the two gaps is active first.
4. **Previously proposed host levers are exhausted and not the fix:** the H2D wake/frames
   (dead consumer, section 6.3), the g+0xB4 staged-armed write (only unblocked the D2H
   publish once), the 0xcece gate service, the D2H ack/drain/re-arm, the PHY table
   pre-apply (withdrawn: table was fully applied [V*]), and CA 0x40114568 writes. None of
   them creates the missing event-delivery path or the missing per-link context; deeper
   host writes are not justified (abort-rca section 10.4 stand).

**Statement of the missing link (with markers):** [V] image's only event-dispatch entry is
ROM-driven (0x40064 -> 0xC2EFC); [V] all recovered link/event bootstrap code exists only as
0xC2EFC handlers; [V*] the measured H2D consumer is dead in our state (event 0x4C never
dispatched); [I] therefore the firmware is starved of the hardware event that would start
every one of its registered services; [U] the exact reason the event controller/vector never
delivers (never enabled vs never asserted vs ROM vector not installed for our release path)
is not decidable from the image.

---

## 8. One exact next bench action (READ-ONLY) - and what each read decides

Because a host-visible **verification** remains (not a trigger), the single next attended
action is: **on one fresh cold boot, after the standard prepare/upload/verify/release with
the C1e-style service loop, take the read-only event-layer status pack below (no writes at
all).** tools/live_reads.py prints the full list, addresses and expected values.

| # | read (all read-only) | BAR0 | expected if the image is where we think | decides |
| ---: | --- | ---: | --- | --- |
| 1 | CA 0x17D430[0x02/0x2D/0x2E/0x3E/0x3F/0x40/0x42/0x46/0x4C/0x4E] | 0x835438/0x8354E4/0x8354E8/0x835528/0x83552C/0x835530/0x835538/0x835548/0x835560/0x835568 | 0xC20F9, 0x462F9, 0x4624D, 0x46849, 0x46849, 0x40671, 0x4065D, 0x40525, 0x40295, 0x402A1 | did entry A and pcie_msg_init reach their registration blocks? |
| 2 | CA 0x4016010C (event status), 0x40160110 (ack - read only, never write) | 0x51810C / 0x518110 | 0 with no IRQ pending; nonzero = a dispatched-in-waiting event word | is the event controller asserting anything? |
| 3 | CA 0x40036800 (pump status for 0x3E/0x3F) | 0x3EE800 | 0 in idle | is radio event block asserting? |
| 4 | boot global +0xEE0..+0xEEC (CA 0x118AE0..0x118AEC) | 0x7D0AE0..0x7D0AEC | 0x0, 0x461B7, 0x467D9, 0xC19D7 | did entry A run the pool-consumer block and did the boot task write +0xEEC? |
| 5 | CA 0x17D398+0x00, +0x74 | 0x835398 / 0x83540C | (core+1)<<5; dispatcher flag | entry A system init sanity |
| 6 | out[0] CA 0x40039010 after a single existing hcc_link_wake-style mailbox write | 0x3F1010 | 0 if consumed; still set if not | re-confirm event 0x4C delivery (already negative, do not repeat more than once) |
| 7 | ROM vectors 0x00..0x1C and 0x1E4 through the identity window | 0x00..0x1C, 0x1E4 | 0xE59FF018 style stubs; IRQ=vector[6], FIQ=vector[7] | is IRQ/FIQ vectored into the image trampoline 0x40064, or ROM-internal? |

Decision matrix:

* **(A)** all handler[] entries set (row 1) **and** CA 0x4016010C nonzero while out[0] is
  still pending -> events are pending but not dispatched -> **IRQ delivery/vectoring is the
  missing link (ROM-owned)**. Definitive firmware/ROM-internal; escalate as in section 9.
* **(B)** handler entries set but CA 0x4016010C stays 0 while out[0] is pending and the ETE
  rings are armed -> the device event router never asserts the event -> **event-source
  arming is missing and it is also firmware/ROM-owned** (host writes already proven unable
  to substitute, section 6.3).
* **(C)** any of handler[0x3E/0x3F/0x4C] is 0 -> entry A / pcie_msg_init stopped before its
  registration block. Read row 4 + system object row 5 to localise the stop; still
  firmware-internal (the image has no host-visible hook), but this narrows the missing write
  to a specific image block.
* **(D)** ROM vectors (row 7) show vector[6]/[7] pointing into the image range
  0x40064..0x400C4 -> IRQ delivery *is* wired to the image; then the gap is event-source
  assertion ((B)). If they point only into ROM (<0x40000), the IRQ path stays ROM-internal
  and only a ROM dump/vendor trace can close it.

**DO NOT WRITE** in this session: CA 0x400392F0 (out[5]), CA 0x40160110, CA 0x40039010/14,
any 0x17D4xx handler slot, boot global +0xEE0..+0xEEC, any RAM object, any fabricated
+0x4B0 pointer. Abort-rca section 10.4 remains in force.

---

## 9. Definitive conclusion and what evidence would close it

**Conclusion (firmware-internal).** The remaining blocker is not a host message, a host RAM
word or a table walk. The image's boot code (entry A, pcie_msg_init, entry B/boot task) is
wired to receive and dispatch **hardware events** through a ROM-owned IRQ vector that enters
the image's trampoline at CPU 0x40064 and calls the event dispatcher 0xC2EFC, whose handler
table CA 0x17D430 is populated with the link/event/mailbox/ETE services. Our reachable boot
state has that table populated (entry A and pcie_msg_init ran) but no dispatch has ever been
observed - the H2D mailbox event 0x4C is not dispatched (out[0] never acked) and the per-link
+0x4B0 contexts (built by the boot task's own post-send chain) remain zero. The host has no
register that asserts the missing hardware event or installs/enters the ROM IRQ vector; the
H2D lane's negative result is a *corollary*, not a separate bug.

**What would close it (ranked):**

1. **The read-only pack above (section 8)** - one boot, no writes. It is the only step that
   can still be done from our side and it localises the failure to "pending but not
   dispatched" (A) vs "never asserted" (B) vs "registration block never ran" (C), and reads
   the ROM IRQ vector words via the identity window (D). This is the recommended next lane
   action.
2. **ROM dump path.** The ROM body at CPU 0x37160 is not host-readable through either
   window (prior lane), but the low ROM vector area (0x0..0x1C and the 0x1E4 trampoline
   table pointer) *is* readable through the region-0 identity window [V*]. Reading those is
   cheap and is (D) above; if they do not resolve the vector, a ROM-side dump (vendor debug
   SoC access, or a ROM-extraction method) is required to decode the IRQ entry - the one
   part of the contract that lives outside the image.
3. **Vendor golden trace (diagnostic only).** On a stock 5.10 vendor system (attended),
   instrument the vendor stack (plat_debug.ko / wifi_debug.ko exist) or use a debug SoC
   trace to record whether/when 0xC2EFC runs, which status words CA 0x4016010C carries, and
   when the per-link +0x4B0 words of the four registry destinations become nonzero. That
   gives the ordering evidence the image cannot.
4. **Full instruction-level reconstruction** of the boot task post-send chain
   (0x49188 -> 0x48820) and of the 0x48820 config-item selection, to prove which boot-time
   branch the vendor takes; this is host-only and can be advanced from the image without a
   device, but it cannot prove *why* IRQ delivery is dead (that is the ROM/event-controller
   side).

**Host-side work that can still progress it:** exactly item 1 (read-only pack) and item 4
(static reconstruction of the boot task continuation and the 0x48820 gate); plus extending
the existing hcc_link_status observability attribute with the ~20 words of the pack
(read-only). Nothing else.

---

## 10. Evidence table (new claims of this lane)

| # | claim | artifact / method | grade |
| ---: | --- | --- | --- |
| E1 | CPU 0x474B8 is entry A's literal pool, not a dispatch table; 30 literal-load sites 0x471C0..0x4744A are its only consumers | fw_cfg.py pool 0x474B8 0x47528 | [V] |
| E2 | entry A loads 0x46849 and registers it for event ids 0x3E/0x3F via 0xC6DA2 -> handler table CA 0x17D430 | fw_cfg.py regs; disassembly 0x471EE..0x47218; 0xC74B0 body | [V] |
| E3 | same block writes boot global CA 0x117C00 +0xEE0=0 / +0xEE4=0x461B7 / +0xEE8=0x467D9 | disassembly 0x471CA/0x471F2/0x471FC; scan of all +0xEE0..+0xEEC accesses | [V] |
| E4 | CPU 0x46848 is a device event pump over CA 0x40036800 / 0x40036000 blocks, dispatching callbacks 0x461B7 / 0x467D9 | disassembly 0x46848..0x4691A, 0x461B6, 0x467D8 | [V] |
| E5 | per-link +0x4B0 bind 0x48840 lies inside boot task 0x47628; reached only via 0x48160 -> 0x47D28 -> 0x49188 -> ... -> 0x4881E; not via 0x474B8 | fw_cfg.py reach 0x48160 0x48820 and reach 0x49188 0x48820 | [V] |
| E6 | image contains ARM IRQ/FIQ second-level trampoline at CPU 0x40064 that blx-es the event dispatcher 0xC2EFC; 0xC2EFC has no other caller | ARM disassembly 0x40064..0x400C0; full-image BL/literal/movimm negative scan | [V] |
| E7 | dispatcher 0xC2EFC indexes CA 0x17D430 by low 10 bits of device event status CA 0x4016010C and acks via CA 0x40160110 | disassembly 0xC2EFC + literal pool 0xC3010 | [V] |
| E8 | pcie_msg_init registers event 0x4C -> mailbox dispatcher 0xC18AC (wrapper 0x40294) and 0x4E -> D2H flush 0xC6108 (wrapper 0x402A0), and software-raises both | disassembly 0x496C8..0x4981C; 0x40294/0x402A0 | [V] |
| E9 | the five "thread loop entries" are bl 0xC5128 call sites inside 0xC497E / 0xC52B4 / 0xBExxx; 0xC5128 = sem obj+0x98 + queue post obj+0x50 | fw_cfg.py dump on the five sites | [V] |
| E10 | H2D probe out[0] never consumed = event 0x4C never dispatched | prior attended lane [V*] + E6/E8 | [V*]/[I] |
| E11 | host has no register to assert the missing hardware event / enter the ROM IRQ vector | sender/init-path census (wifi-hcc-link sections 2-4) + E6 | [V]/[I] |
| E12 | entry A materialises 0x47625 only at 0x47416 and hands it to task framework 0xC745C; 0x17D42C is only written (disarm) by the boot task | fw_cfg.py findlit 0x47625 0x17D42C; disassembly 0x47410/0x47D06 | [V], store path [U] |

Corrections carried into the prior lanes:

* "boot-region dispatch table at CPU 0x474B8" -> entry A literal pool (E1);
* "0x46848 = link-create worker" -> event pump handler 0x3E/0x3F (E4);
* "per-link bind 0x48840 inside 0x46848" -> inside boot task 0x47628 (E5);
* rom-flow section 6 "event dispatcher reads CA 0x40105000" -> dispatcher 0xC2EFC reads
  CA 0x4016010C (E7); CA 0x40105000 belongs to a different (system-init) path;
* "ROM arms the reset slot 0x17D42C with 0x47629" remains [U]; the only image-side
  materialisation of the boot-task pointer is entry A's 0x47416 (E12).

---

## 11. Ranked list - what to check first

1. **Handler table CA 0x17D430 entries** for ids 0x2D/0x2E/0x3E/0x3F/0x40/0x42/0x46/0x4C/0x4E
   (10 reads, one boot): decides whether entry A and pcie_msg_init reached their
   registration blocks (E2/E8) or stopped earlier -> routes to case (A)/(B)/(C).
2. **Device event status CA 0x4016010C + pump status CA 0x40036800** (2 reads): decides
   "pending but not dispatched" vs "never asserted".
3. **ROM vectors 0x00..0x1C and 0x1E4** via the identity window (9 reads): decides whether
   IRQ is vectored into the image trampoline 0x40064 or stays ROM-internal.
4. **Boot global +0xEE0..+0xEEC and system object +0x00/+0x74** (6 reads): confirms entry A's
   pool-consumer block ran (E3) and the boot task wrote +0xEEC.
5. Only then, and only with a human at the bench + recovery armed, re-confirm the mailbox
   event 0x4C with ONE existing hcc_link_wake-style write (no new frames, no RAM writes).
6. Then choose: case (A)/(D-internal) -> ROM IRQ vector lane (golden trace or ROM dump);
   case (B)/(D-internal) -> device event-router arming lane (still ROM/first-stage);
   case (C) -> localise the unexecuted image block (static work, no bench).

Do not: fabricate +0x4B0 pointers or RAM objects; write handler-table slots; write
CA 0x40160110/out[5]/out[0]; post speculative H2D frames; re-run the g+0xB4 staged write;
re-apply the PHY table (already fully applied).

---

## 12. Reproduction (host-only; local blob only; nothing vendor is committed)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python   # capstone 5.0.7
    T=opensource/docs/incoming/wifi-fw-link-event/tools

    $PY $T/fw_cfg.py dump 0x46970 0x60            # entry A start
    $PY $T/fw_cfg.py pool 0x474B8 0x47528         # E1: pool consumers (all in entry A)
    $PY $T/fw_cfg.py regs                         # E2/E8: event-id -> handler census
    $PY $T/fw_cfg.py dump 0x471C0 0x60            # E3: pump wiring + 0x3E/0x3F registration
    $PY $T/fw_cfg.py dump 0x46848 0xD8            # E4: event pump
    $PY $T/fw_cfg.py --lo 0x47600 --hi 0x49E00 reach 0x49188 0x48820 --compact   # E5
    $PY $T/fw_cfg.py --lo 0x47600 --hi 0x49E00 reach 0x48160 0x48820 --compact
    $PY $T/fw_cfg.py dump 0xC2EFC 0xA0            # E7: event dispatcher (Thumb)
    $PY $T/fw_cfg.py dump 0x496C8 0x160           # E8: pcie_msg_init registrations
    $PY $T/fw_cfg.py findlit 0x47625 0x17D42C     # E12
    $PY $T/live_reads.py                          # section 8 read-only bench plan

    # prior-lane tools used for cross-checks (read-only):
    $PY opensource/docs/incoming/wifi-rom-flow/tools/fwdis.py dis 0x40064 0x60 arm
    $PY opensource/docs/incoming/wifi-rom-flow/tools/fwdis.py ctx 0x00046849
    $PY opensource/docs/incoming/wifi-hcc-link/tools/fw_xref.py dis 0x46848 --len 0xD8

fw_cfg.py does a recursive-descent Thumb walk (no linear sweep through literal pools),
resolves every PC-relative literal, and adds pool (literal-target consumer census) and regs
(event-handler registration census). live_reads.py only prints addresses; it never opens a
device.

---

## 13. Open items ([U])

1. Does the task framework (0xC745C) ultimately install the boot-task pointer 0x47625 into
   the reset-dispatch slot CA 0x17D42C, or does the ROM arm that slot independently?
   (Image has no direct store to 0x17D42C besides the boot task disarm.)
2. Which handler id is registered at 0xBEE40 for the 0x46938 "kick" function (the call
   site's r0 is computed, not an immediate)? Not needed for the conclusion.
3. What is the consumer of the static service descriptor table CA 0x106E40 (row
   {0xF, 0x7243D, 0x0ED8, 0x0EDB}, the tag dispatcher 0x7243C)? It has no in-image literal
   consumer; like the header entries it may be ROM-consumed. If the ROM reads it, the tag
   layer is also bootstrapped from outside the image [I].
4. Exact reason the IRQ vector does/does not deliver on our transport (section 8 case A vs
   B vs D) - needs the read pack and then ROM/golden-trace evidence.

---

*Lane: fw-link-event, 2026-11-23. Host-only, image-only. Complements wifi-rom-flow
(header/vector contract), wifi-abort-rca (per-link descriptor chain), wifi-boot-kick
(boot-task/live-state), wifi-hcc-link (H2D negative + observability) and wifi-h2d-catalog
(HCC instance/registration tables). Vendor blob and device values remain local evidence
only; no vendor bytes or firmware data are committed.*
