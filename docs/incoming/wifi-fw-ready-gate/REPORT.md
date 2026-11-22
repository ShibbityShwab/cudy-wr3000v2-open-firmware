# The firmware-ready gate: what the device checks before it sends HCC chip-group-4 id1/id2 - static reversal

**Lane:** fw-ready-gate (task-67), host-only RE. **Written:** 2026-11-22.
**Scope:** the closed 928,920-byte Hi5622V100 firmware blob, static only. No device was touched, no
vendor binary/blob is committed here, no OpenWrt image was built. Evidence markers: **[V]** verified
from the named file offset / instruction in this lane's disassembly; **[V*]** measured on the device
by an earlier attended lane and recorded in the named repo doc; **[I]** inference; **[U]** unknown /
not resolvable from the blob.

**Artifact** (local, not committed): /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin,
928,920 B, sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b.
Conventions (unchanged): firmware offsets are **file** offsets, Thumb; **runtime = file + 0x40000**;
pointer literals in the blob are runtime addresses. Disassembly: capstone 5.0.7 (Thumb) cross-checked
with arm-none-linux-gnueabihf-objdump -D -b binary -marm -Mforce-thumb. Host CA/BAR0 (region 3):
**BAR0 = 0x3b8000 + (CA - 0x40000000)** [V].

**Sources read first:** incoming/wifi-hcc-re/REPORT.md, phase19/fw-handshake.md,
phase42/word-semantics.md, phase43/device-dr-side.md, phase37/fw-ring-message-table.md,
phase37/d2h-message-catalog.md, incoming/wifi-firmware-re/REPORT.md, phase20/host-window.md,
phase22/fw-hostmem.md.

---

## 0. TL;DR - the answer in one page

1. **The 0xcece-gated routine at file 0x86E14 is not the ready-message sender.** It is the per-core
   platform/ETE-up announce: it stages per-chip boot parameters into the firmware image header,
   reprograms the two PCIe glue blocks, writes **out[1] = 0x4** + the D2H doorbell, and then spins on
   **CA 0x4000010C == 0xcece**. After the spin it only unlocks and returns [V].
2. **The two HCC group-4 ready frames are built by the firmware's boot/handshake state machine - one
   large function at file 0x7628 (runtime 0x47628).** It is a re-entered state machine whose state
   byte lives at device RAM **rt 0x118B60** (main-image BSS), with per-state records at rt 0x118B64
   (stride 0x84):
   * **id 1 "Device plat ready"** (hdr[0]=4, hdr[6]=1, payload len 0) is built and queued at
     **file 0x7D28-0x7D54** - but only when ***(u8*)0x118B60 > 1** (file 0x7D10-0x7D18); states 0/1
     take the setup branch at file 0x8160 which advances the counter and registers the H2D handler
     table [V].
   * **id 2 "DEVICE READY"** (hdr[0]=4, hdr[6]=2, 4-byte payload = 0x01060850) is built and queued at
     **file 0x9000-0x9162**, gated by **[sp+0x24]==2** (file 0x8FD6) and
     ***(u8*)(rt 0x1ADB80 + 0x110) == 0** (file 0x8FE4/0x8FF0) [V].
3. **Both go out through one send primitive at file 0xC2760** - validate (0x9EA), set header +9 =
   source-core, look up the per-(core,group) destination ([0xC281C] + 0x448 + 24*core, entity
   [dest+0x10] + 12*group), then enqueue at file 0xC2662. Missing destination -> printk + return
   0x8B2D, no mailbox write [V]. The downstream DR/HCC deposit path is file 0x861DC (build 8-B node,
   commit via 0x8192A, d2h_notify(obj,6)) -> file 0x86170 (writes out[1] |= 0x40 and pulses
   CA 0x40101434) [V].
4. **No H2D message and no SR-ring state is required for the ready sends.** The whole image contains
   no reference to the D2H ack (0x40101438) or re-arm (0x40101414) CAs - those are host-side-only
   [V, full literal + movw/movt scan]. The firmware's only mailbox reads are out[1] (0x40039014),
   [0x40039010] in the H2D dispatcher, and 0x4000010C [V].
5. **The one firmware-side condition the host controls on the publish path is P2: out[1] must read 0**
   before the next D2H word is published (file 0x86120-0x86128 in the flush routine 0x86108) [V]. The
   0x4 announce the host has already seen is exactly the word it must now ack/drain/re-arm.
6. **The helper pair writes a 22-step analog/PHY trim sequence to base+0x41C with a strobe at
   base+0x428, status at base+0x424, and reads back 10 block registers - for both 0x40039000 (id 0x10b)
   and 0x40039800 (id 0x10c).** It is not mailbox setup and not a gate; the values are in the blob at
   file 0xC3BA0/0xC3B78 [V]. See section 6.

The three most likely bench causes and the first probe are in section 7; the bench plan is section 8.

---

## 1. The 0xcece gate - file 0x86E14, re-verified

### 1.1 Entry, argument and the four thunks

The routine starts push.w {r3-r8,sb,lr} at 0x86E14. It is entered through **four tail-jump thunks**
that each set r1 (core) to 4/3/2/1 and call the same body [V]:

| thunk (file) | sequence |
| --- | --- |
| 0x86FC8 | ldr r3,[r0,#0x3c]; subs r3,#8; str r3,[r0,#0x3c]; clrex; movs r1,#4; b 0x86E14 |
| 0x86FD6 | ldr r3,[r0,#0x3c]; movs r1,#3; subs r3,#4; str r3,[r0,#0x3c]; b 0x86E14 |
| 0x86FE0 | same with movs r1,#2 |
| 0x86FEA | same with movs r1,#1 |

The thunks are referenced from the ARM veneer table at file 0xC3B38-0xC3B78 (e51ff004 + <runtime
target>: file 0xC3B44 -> rt 0xC6FE1 (thunk 0x86FE0), 0xC3B4C -> 0xC6FEB, 0xC3B6C -> 0xC6FC9,
0xC3B74 -> 0xC6FD7) [V]. The invoker of that veneer table is not statically named in the blob [U].

### 1.2 What it checks / writes, in order (all [V], file offsets, objdump)

1. **copy per-chip boot parameters into the firmware image header** (r0 = arg = 17-word block; r1 =
   core): strh OR (1<<core) into the halfword at **CA 0x40008** (lit @0x86FA4 = 0x00040008,
   0x86E90-0x86E96), i.e. the image-header core mask; then 17 words copied from [r0] to
   **CA 0x4000C-0x40044** (0x86E9C-0x86EDE).
2. **PCIe block bit**: CA 0x40101250 (lit 0x86F90) &= ~0x20 (0x86E36-0x86E3E); if cpu_id()==0 write
   0x00010002 to CA 0x40161F00, else 0x00020002 (0x86E46-0x86E6E).
3. **zero the handshake CAs**: CA 0x4003022C = 0 (0x86E7A); **halfword 0 to CA 0x4000010C** (0x86E7E)
   and **halfword 0 to CA 0x40000108** (0x86E80). This is the line that can wipe a host-written 0xcece
   if it lands too early.
4. stage a device-RAM flag word (rt 0x17D398+0xBE0) with bits 1/2/4/8 through the routine
   (0x86E42, 0x86E84-0x86E8C, 0x86F22-0x86F2C, 0x86F68-0x86F70) [V].
5. cache clean/invalidate over [0x40000,0x1C0000) and [0x1000000,0x1060000) (blx 0xC2EE4,
   0x86EE0-0x86EF4) - i.e. the firmware image and PKTRAM.
6. copy a **3-entry table** (file 0xCF294 = rt 0x10F294) into device RAM at 0x010DB800 with end limit
   rt 0x10DFFFF (0x86EF8-0x86F20); entries {src 0x001BD000 len 0x2000}, {src 0x4003A000 len 0xC28},
   {src 0x40039000 len 0xD5C} [V].
7. clear bit 0 of the halfword at **CA 0x40000558** (0x86F26-0x86F3C) [V].
8. **helper pair on both PCIe blocks** (see section 6): 0x7C0(0x40039000); 0x78E(0x40039000);
   0x7C0(0x40039800); 0x78E(0x40039800) (0x86F3E/0x86F44/0x86F4A/0x86F50) [V - corrects the
   interleaved pairing in phase43 section 4.3].
9. **announce**: *CA 0x40039014 = 4 (0x86F54-0x86F5A) and *CA 0x40101434 = 1 (0x86F5E-0x86F66:
   out[1] + 0xC8000 + 0x420) [V].
10. **the gate**: movw r3,#0xcece; loop { r2 = *[0x4000010C]; if r2!=r3 } (0x86F74-0x86F7C, literal
    @0x86FA0 = 0x4000010C) [V].
11. epilogue pop then tail-call 0xBDF72(rt 0x17DF44) = unlock + return 0x86F7E-0x86F86; ldr r0,
    lit @0x86FC0 = 0x0017DF44) [V].

### 1.3 A second 0xcece waiter, and who writes 0x4000010C

* **file 0x820F8 (rt 0xC20F8)**: push {r3,lr}; mov r3,sp; cpsid i; bl cpu_id; movw r3,#0xcece;
  ldr r1,lit=0x4000010C; spin until *r1==r3; pop (0x82102-0x8210E, literal 0x82110) [V].
* The boot state machine (0x7628, section 2) **registers 0x820F8 as callback slot 2**:
  0x7CDC ldr r2,lit @0x7DD4 = 0x000C20F9; movs r1,#0; movs r0,#2; bl 0x86DA2; the wrapper 0x86DA2
  (cbz r2; cmp r0,#0x5f; b 0x874B0) calls 0x874B0, which stores the handler at [global+0x98+slot*4]
  (0x874C4-0x874E8) [V]. Slot 2's invoker is [U].
* The same boot state machine **writes 0xdeaf to CA 0x4000010C**: 0x7CE4 movw r2,#0xdeaf; ldr r3,lit
  @0x7DD8 = 0x4000010C; ldr r1,lit @0x7DDC = 0x000C19DD; str r2,[r3] (0x7CE4-0x7CEC) [V]. (0x000C19DD
  is the id-5 BUG() stub 0x819DC.)
* 0x7CEE-0x7CF0 then registers **H2D id 5 = 0x819DC (BUG() trap)** via the firmware's own registrar
  (0x7C1DC) [V]; phase37's "id 5 traps" observation is the firmware's own doing.

**Consequence for the bench handshake** [I, strong]: CA 0x4000010C is written three different ways in
this boot flow - the boot state machine writes 0xDEAF (0x7CEC), the ready routine writes 0 (0x86E7E)
and then waits for 0xCECE (0x86F78), and the second waiter 0x820F8 waits for the same value. A host
that writes 0xcece exactly once at release time can lose the write to the 0x86E7E store and leave the
ready routine spinning forever.

---

## 2. Where the two group-4 ready messages are actually built and sent

### 2.1 The boot/handshake state machine: file 0x7628 (rt 0x47628)

One function: prologue 0x7628 stmdb sp!,{r4-r9,sb,sl,fp,lr}, single epilogue at 0x916A
pop.w {r4,r5,r6,r7,r8,sb,sl,fp,pc}; body spans 0x7628-0x9168 [V]. Its globals come from literals at
the head: **rt 0x117C00** = the firmware global message object (0x7934, also written into r4) [V].
The handshake state is a **byte at rt 0x118B60** ([r4+0xF60]), with per-state records at rt 0x118B64
(stride 0x84; lit @0x84B8 = rt 0x118B64) and a lock at rt 0x118B24 (lit @0x7DF4) [V]. The function has
**no static caller and no pointer literal** in the blob [V scan]; it is re-entered by a runtime
scheduler [I].

### 2.2 id 1 - "Device plat ready" (HCC chip group 4, message id 1, no payload) [V]

Site: **file 0x7D22-0x7D54**.

0x7D0A ldr r0,lit @0x7DF4 = 0x00118B24 ; lock object
0x7D0C bl  0xBDF8E                      ; lock
0x7D10 ldrb.w r6,[r4,#0xF60]            ; r6 = state byte (rt 0x118B60)
0x7D16 cmp  r6,#1
0x7D18 bls.w 0x8160                     ; state <= 1 -> setup branch (advance + setup), NOT this send
0x7D1C ldr r0,0x00118B24 ; bl 0xBDF50   ; unlock
0x7D22 movs r3,#0 ; str r3,[r4,#0x270]  ; clear global pending pointer
0x7D28 movs r1,#6 ; movs r0,#0 ; bl 0xC2822   ; msg_alloc(payload_len=0, pool/ch=6)
0x7D38 bl  0x9EA                        ; validate pool membership/class
0x7D3C movs r3,#4 ; strb r3,[r0,#0]     ; +0 = 4 (HCC group 4)
0x7D40..0x7D48 ldrb r3,[r0,#1]; bfc r3,#4,#4; strb r3,[r0,#1]  ; +1 high nibble = core 0
0x7D4A movs r3,#0 ; movs r2,#1
0x7D4C strb r2,[r0,#6]                  ; +6 = 1  (HCC message id 1)
0x7D4E strb r3,[r0,#3] ; strb r3,[r0,#7] ; +3 = +7 = 0
0x7D52 mov r0,r4 ; bl 0xC2760           ; SEND (see section 3)
0x7D58 cmp r0,#0
0x7D5A beq.w 0x9188                     ; on queue success continue at 0x9188 (see 2.4)

The **setup branch 0x8160** (taken while state <= 1) is what advances the counter: [r4+0xF60] = r6+1
(0x816A-0x8174), then it builds the per-state objects and registers the H2D id-1 handler: 0x8224
movs r0,#1; bl 0x7C1DC with r1 = lit @0x84D0 = 0x00040511 = file 0x510, the firmware's H2D id-1
handler (phase37's "queue + wake worker") [V]. Therefore, on the vendor boot path: call 1 (state 0) ->
state 1 + setup; call 2 (state 1) -> state 2 + setup; call 3 (state 2) -> **id 1 is queued** [V
structure, [I] call count].

### 2.3 id 2 - "DEVICE READY" (HCC chip group 4, message id 2, 4-byte payload) [V]

Site: **file 0x8FD6-0x9168**.

0x8FD6 ldr r3,[sp,#0x24] ; cmp r3,#2 ; bne ...   ; one internal state gate
0x8FDC bl  0x1268
0x8FE0 movs r6,#1
0x8FE2 ldr r5,lit @0x914C = 0x001ADB80          ; per-core/per-chip context
0x8FE4 ldrb.w r3,[r5,#0x110] ; strb r3,[sp,#0x54]
0x8FEC ldrb.w r7,[sp,#0x54]                     ; r7 = core selector
0x8FF0 cmp r7,#0 ; bne 0x9050                   ; non-zero core takes another branch
0x8FF4..0x8FFC memset(sp+0x54, 4 bytes) = 0
0x9000 ldr r3,lit @0x9150 = 0x01060850          ; <-- the payload word (PKTRAM device address)
0x9002 movs r1,#6 ; movs r0,#4 ; str r3,[sp,#0x54]
0x9008 bl  0xC2822                               ; msg_alloc(payload_len=4, pool 6)
0x90E0 bl  0x9EA                                 ; validate
0x90E4 ldrb r3,[r0,#1] ; movs r5,#4
0x90E8 bfi  r3,r7,#4,#4 ; strb r3,[r0,#1]       ; +1 high nibble = core
0x90EE movs r3,#2
0x90F0 strb r5,[r0,#0]                          ; +0 = 4 (group 4)
0x90F2 strb r3,[r0,#6]                          ; +6 = 2  (HCC message id 2)
0x90F4 strb r7,[r0,#3] ; strb r7,[r0,#7]        ; core in +3 and +7
0x90FA bl  0x9EA ; adds r0,#0xC
0x9106 bl  0x81E44                              ; copy 4-byte payload to hdr+0xC
0x9160 bl  0xC2760                              ; SEND
0x9166 cmp r0,#0 ; bne ... ; 0x916A epilogue

Notes: the payload is the **literal 0x01060850** (device CA in PKTRAM, region 2 = BAR0+0x238850), not a
value read from it [V - the instruction is ldr r3,[pc,#0x14C]; str r3,[sp,#0x54], no load through r3];
the host's id-2 handler receives it verbatim into its 0x64-byte per-core record
(phase19/fw-handshake.md A.2) [V*]. The gate on [r5+0x110] (rt 0x1ADB80+0x110) selects core; a second
branch at 0x9050 handles r7 != 0 [V].

### 2.4 The other group-4 builders (for completeness, same send primitive)

| group4 id | host handler (plat.ko) | firmware builder (file) | alloc | notes |
| ---: | --- | --- | --- | --- |
| 0 | oam_rx_post_action_function (device log blob) | 0xC48, caller 0xC70 | msg_alloc(len,7) | sets +0=4, +6=0, len=arg+0xC, copies payload; 0xC70 also calls the send [V] |
| **1** | **device_plat_ready_msg_process** | **0x7D28** | msg_alloc(0,6) | see 2.2 [V] |
| **2** | **host_ready_msg_process** | **0x9000** | msg_alloc(4,6) | see 2.3 [V] |
| 3 | exception_info_msg_process | 0x91C8, payload table rt 0x1043A5 = file 0xC43A5 = "DEVICE EXCP_INFO" | msg_alloc(0x3D,6) | reached from 0x7D5A on id-1 queue success; seeds a 61-byte payload from 0x9304 = 0x001043A5 [V path; [U] why the exception slot is used on this branch] |
| 4 | heartbeat_msg_process | not found in this pass | - | [U] |

The group ids line up with the host-side table in incoming/wifi-hcc-re/REPORT.md section 2.4, which
independently confirms that the group-4 builder catalogue is the ready/heartbeat/exception family [V].
The 4-byte id-2 payload is the only field-carrying ready body; id 1 is header-only (len = 0xC) [V].

---

## 3. The shared send primitive and the path to the DR ring

### 3.1 file 0xC2760 - hcc_msg_send(msg) [V]

0xC2760 push {r4,r5,r6,lr}; mov r5,r0
0xC2764 cbnz r0 -> 0xC2776 ; NULL -> printk(0x179) + return 0x8B2D
0xC2776 bl 0x9EA                  ; validate the message buffer (pool + class <= 3)
0xC2782 ldrb r3,[r0,#1] ; ubfx r3,r3,#4,#4 ; strb r3,[r0,#9]   ; hdr[9] = source core
0xC278C bl 0x9EA ; mov r4,r0
0xC279A movw r3,#0x448            ; per-core record base
0xC27A0 ldrb r1,[r0,#9]           ; core
0xC27A2 smlabb r2,r2,#24,r3       ; + 24*core
0xC27A6 ldr r3,lit @0xC281C       ; destination table base
0xC27AA ldrb r1,[r0,#0] ; and r1,#0xF   ; group
0xC27AC add r2,r3(r2) ; bl 0xC2744      ; dest = [base+0x448+24*core+0x10] + 12*group
0xC27B6 cbz r0 -> printk(0x189) + return 0x8B2D   ; **** group not registered -> no send ****
0xC27BA ldrh r1,[r4,#6]           ; id
0xC27BC ldr r3,[r0]               ; dest max id
0xC27BE cmp r1,r3 ; if id < max: [dest+4]+id*16 -> .stats[8]++
0xC27CE ldr r1,[r0,#8] ; bl 0xC24CE     ; take the destination lock
0xC27EE bl 0xC2662               ; enqueue(msg, dest, lock handle)
0xC2810 movs r0,#0               ; success

So a ready frame can be **silently dropped inside the firmware** if the per-(core,group=4) destination
record is not present: the function prints and returns 0x8B2D, no mailbox write, no visible host-side
change [V]. That table is populated at runtime; its registration site was not mapped in this pass [U].

### 3.2 From the enqueue to the DR ring and the D2H notify [V]

* **file 0x861DC** is the D2H deposit block (first instruction movs r1,#0x30, then
  push {r0,r1,r2,r4,r5,lr}; end pop {r4,r5,pc} at 0x86234): builds the 8-byte node in sp -
  [sp] = buffer address, [sp+4] = (len<<16) | 0x6000 | 0xD2B (0x861E6-0x86216), calls **0x8192A** to
  commit it (0x8621C), calls **d2h_notify(obj,6)** (0x86220 movs r1,#6; 0x86224 bl 0x86170), then
  str [ctx+0x340]++ (0x8622E).
* **file 0x81906** is the producer-index advance: idx=(idx+1)&0x3FF, on wrap clear low 10 bits and
  toggle **bit 10** (phase bit), store (0x8190A-0x81926) [V]; **0x8192A** writes node word0 then word1
  at ring[idx] with idx = [ctx+0xC]&0x3FF, then tails into 0x81906 [V].
* **file 0x86170 d2h_notify(obj,id)**: id<=10, obj==0, global g=*rt 0x172130, pending shadow g+0xB8,
  out[1] = g+0x9C, D2H doorbell = g+0xA4; writes out[1] = shadow | (1<<id) (0x861B0) and pulses
  doorbell |= 1 (0x861C0) [V]. **The ready path uses id 6 => out[1] bit 6 (0x40)** [V].
* **file 0x86108 (flush)**: ldr r3,[r0+0xB0]; *r3 = 1 re-asserts the channel enable (0x86110-0x86114),
  then mov.w r3,#0x10000 and **spins until *[g+0x9C] (out[1]) == 0** (0x86120-0x86128) before publishing
  the pending shadow and the doorbell (0x86156-0x86166) - the P2 "keep the mailbox drained" condition
  [V]. It is registered as callback slot 0x4E in pcie_msg_init (0x9806-0x980C: r0=0x4E, r1=5,
  r2=thunk@0x2A0 -> b.w 0x86108) [V].
* **Static callers of 0x86170 found this pass** (contradicting the earlier "no static caller"
  statement in phase37/fw-ring-message-table.md section 5): **file 0x86224** (inside the D2H fill
  block, id 6), **file 0x86998** (id 5 after the ring advance at 0x869C6), and **file 0x62AC** (third
  notify site, context not mapped here) [V].

### 3.3 Which DR channel?

Firmware's own ETE channel table is at **file 0xC4054** (rt 0x104054), 7 x 12 B [V]: {0x400,0x06800020,1}
{0x450,...,1} {0x4A0,...,1} {0x590,...,1} {0x5E0,...,0} {0x630,...,0} {0x680,...,0} - i.e. SR0-2 and
**DR0 are flagged 1 (control/HCC)**, DR1-3 flagged 0 (data), the same split
incoming/wifi-hcc-re section 4.1 inferred from plat.ko. The ready frames are control messages =>
**prediction: DR0 carries the group-4 id1/id2 frames** [I - firmware's flag + only DR0 enabled for
control], while the host must be prepared to see them on any DR channel [U until B3].

### 3.4 The DR-ring contract the host must satisfy for these frames (question 2)

* Node = 8 B, **word0 = buffer device VA** (host-posted buffer in the 0x80000000..0xFFFFFFFF outbound
  window), **word1 = (len<<16) | 0x6D2B** (bits 13/14 present, low 13 = 0xD2B) [V].
* Producer index = **packed (idx & 0x3FF) | phase<<10**, advanced +1, phase toggled at idx == depth
  (0x81906; host twin pcie_ete_ring_ptr_plus) [V].
* Host-side DR registers: **+0x30 = node-array device VA, +0x34 = depth-1, +0x38 = free-buffer
  producer (host publishes, device consumes), +0x3C = device fill index** (host reads to know deposit
  progress) [V* phase-hcc/43].
* The device **never writes DR channel registers**; it only writes the node array through the outbound
  window [V phase43 negative]. Pre-posted DR free buffers are therefore a hard prerequisite [V* B2 did
  this].
* The ready frames are announced with **out[1] bit 6 (0x40)**, not bit 3; bit 3 (0x8) is the ETE
  transfer-done notify [V firmware/0x86170 id6 + phase-hcc host table].

---

## 4. Host-visible mailbox/H2D/SR preconditions (question 3)

Full scan of the blob for the six mailbox CAs and the handshake CAs, via literal-pool resolution and
movw/movt operand scan over every decoded instruction [V]:

| CA | firmware access | site |
| --- | --- | --- |
| 0x4000010C | **write 0xDEAF** | 0x7CEC (boot state machine) [V] |
| 0x4000010C | **write 0 (halfword)** then **spin-read ==0xCECE** | 0x86E7E / 0x86F78 (ready routine) [V] |
| 0x4000010C | spin-read ==0xCECE | 0x82108 (second waiter 0x820F8) [V] |
| 0x40000108 | write 0 (halfword) | 0x86E80 (ready routine) [V] |
| 0x40039014 (out[1]) | RMW write (announce 4; pending shadow) | 0x86F5A, 0x861B0 [V] |
| 0x40039014 | read, spin while != 0 | 0x86124/0x86126 (flush) [V] |
| 0x40101434 (D2H doorbell) | RMW |= 1 | 0x861C0, 0x86F66 (as +0xC8420 from out[1]) [V] |
| 0x40039010 (out[0]) | read + write 0 | 0x818BC/0x818BE (H2D dispatcher) [V] |
| 0x400392F0 | write 1 (H2D ack) | 0x818B8 [V] |
| 0x400392D4 | write 8 (H2D done doorbell) | 0x818C4 [V] |
| **0x40101438** (D2H ack) | **no reference anywhere** | [V negative] |
| **0x40101414** (D2H re-arm) | **no reference anywhere** | [V negative] |

Relevant literals: 0x86FBC = 0x40039014; 0x97D8 = 0x40101434; 0x97DC = 0x40039014; 0x97E0 =
0x40039010; 0x86FA0 = 0x4000010C; 0x86FB0 = 0x40000558; 0x86FB4 = 0x40039000; 0x86FB8 = 0x40039800 [V].

**What this means:**

1. **No H2D message is required by the ready path.** The firmware is ready to send without any host
   message; the vendor host's only acts in the ready handshake are IRQ enable and status reads
   (phase19/fw-handshake.md A.3) [V*]. The firmware itself registers the H2D handlers it needs (ids
   1/3/5/6; id 5 = BUG trap at 0x7CEE) [V].
2. **No SR-ring state is consulted.** The only ring state used is the firmware's own DR device-local
   producer (0x81906) [V].
3. **The single firmware-side host-controlled condition on the D2H publish path is P2**: out[1] == 0
   before the next publish (0x86120-0x86128), i.e. the host must ack 0x40101438=1, read out[1], write
   out[1]=0, re-arm 0x40101414=1 - exactly the contract in incoming/wifi-hcc-re/REPORT.md section 5.4.
4. **0x4000010C must hold 0xCECE at the moment the two waiters test it.** Because the ready routine
   zeroes it first (0x86E7E) and the boot task writes 0xDEAF (0x7CEC), a single early host write can
   be lost [V instructions, [I] race].

---

## 5. The helper pair - what it does to 0x40039000 / 0x40039800 (question 4) [V]

### 5.1 The main-image helpers

**file 0x7C0 (write_seq(base))**: ldr r1,lit @0x7FC = 0x00103BA0 (table inside the blob at file
0xC3BA0), copy 0x58 B to stack, then for i=0..21:
r3 = tab[i]; [base+0x41C] = r3; [base+0x428] = 1; [base+0x428] = 0; r3 = [base+0x424];
0x244(r0=100) (the 0x244 helper is a busy-wait/nanosecond delay using the cp15 timer: umull;
blx 0xC338C; loop until elapsed == 100) [V].

The 22 values (file 0xC3BA0-0xC3BF4, runtime 0x103BA0):
0x04, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1E, 0x30, 0x1008, 0x1009, 0x100A, 0x100B, 0x100C,
0x100D, 0x1012, 0x1013, 0x101C, 0x101E, 0x101F, 0x1024, 0x1025 [V].

**file 0x78E (readback(base))**: copies 10 offsets from file 0xC3B78 (rt 0x103B78) and executes
ldr r2,[base + offset[i]] for each (values discarded - a status read-back/flush): offsets
0x224, 0x23C, 0x414, 0x43C, 0x440, 0x460, 0x464, 0x468, 0x46C, 0x514 [V].

Therefore, for the two call arguments in the ready routine:

| base | writes (data/strobe/status) | read-back addresses (sample) |
| --- | --- | --- |
| 0x40039000 (block id 0x10B) | 0x4003941C <= 22 values; 0x40039428=1/0; 0x40039424 read | 0x40039224 (= pcie0_status in dev_status_check), 0x4003923C, 0x40039414, 0x4003943C, 0x40039440, 0x40039460/64/68/6C, 0x40039514 |
| 0x40039800 (block id 0x10C) | same offsets +0x800: 0x40039C1C/0x40039C28/0x40039C24 | same +0x800 |

Ready-routine call sequence: 0x7C0(0x40039000) at 0x86F3E; 0x78E(0x40039000) at 0x86F44;
0x7C0(0x40039800) at 0x86F4A; 0x78E(0x40039800) at 0x86F50 [V].

**Semantics** [I]: the pair is a per-PCIe-controller lane/PHY trim write sequence plus a block status
read-back; it is not mailbox setup and it does not gate the ready messages (it completes before the
out[1]=4 announce in the same routine). Block ids 0x10B/0x10C are the two PCIe/message glue blocks
(reg_all: 0x4003A000=0x10A ETE, 0x40039000=0x10B, 0x40039800=0x10C) [V* phase43]. The written values do
**not** include any mailbox CA and none of the three touched offsets is a documented mailbox register [V].

### 5.2 The ITCM twins (same algorithm, tables in mask ROM)

The blob's second executable region (file >= 0xD0000, runtime >= 0x110000, delivered as the INI
firmware_itcm_src_addr=0x117BC4 len=0xA968 / firmware_dtcm_src_addr=0x12252C len=0x76C segments)
contains the same pair [V]:

* **file 0xD8450**: 0x58-byte table from **rt 0x100A0**, same 22-step write/strobe/status sequence, but
  with a settle loop until the cp15 timer has advanced >= 39 (0xD83C8 used twice) [V].
* **file 0xD8268**: 10-offset table from **rt 0x10078**, same read-back [V].

Both tables live **below the firmware image base** (rt < 0x40000 = region-0 ROM/WRAM), so they are
**not in the blob** [U values], but region 0 is host-visible: the tables would be readable at
**BAR0+0x100A0 / BAR0+0x10078** if the chip's ROM/WRAM window is mapped (read-only probe idea).

---

## 6. Ranked candidate causes on our bench, with the exact host-side probe

> Ordering is by how directly the firmware code proves the host condition, and by how cheap the probe
> is. The first two are the same physical chain and should be tested together.

### C1 (most likely) - the host never services the D2H mailbox, so the publish path stays blocked on out[1]

* **Firmware fact** [V]: the flush routine 0x86108 spins on out[1] == 0 (0x86120-0x86128); d2h_notify
  maintains out[1] as a pending bitmap; the ready routine already left out[1] = 0x4. Our transport arms
  ETE and polls, but the described host state does not say it ever performs the ack/drain/re-arm sequence.
* **Experiment** (writes are the already-proven D2H service sequence in
  incoming/wifi-hcc-re/REPORT.md section 5.4 - **attended**):
  1. read CA 0x40039014 (BAR0 0x3F1014); expect 0x4.
  2. write CA 0x40101438 = 1 (ack; BAR0 0x4B9438).
  3. read CA 0x40039014, then write 0 (drain; BAR0 0x3F1014).
  4. write CA 0x40101414 = 1 (re-arm; BAR0 0x4B9414).
  5. poll CA 0x40039014 for 30 s.
* **Decision value**: a subsequent **0x40 (bit 6, a D2H message deposit with a ready frame)** and/or
  **0x8 (bit 3, ETE transfer done)** -> C1 confirmed, the ready frames were only mailbox-blocked. Only
  0x4 ever -> path never publishes; go to C2/C3.

### C2 - the 0xCECE write was lost to the ready routine's own zeroing (or was written before the routine ran)

* **Firmware fact** [V]: ready routine writes halfword 0 to CA 0x4000010C at 0x86E7E and then spins for
  0xCECE at 0x86F78; the boot state machine writes 0xDEAF to the same CA at 0x7CEC; the second waiter
  0x820F8 spins too. A single host write at release time can be erased.
* **Experiment** (**attended write**): read CA 0x4000010C (BAR0 0x3B810C) after out[1]=0x4 has been
  observed (so the routine has passed its zero store). If it reads 0x00000000 or 0x0000DEAF, write
  0x0000CECE to CA 0x4000010C (halfword or word), then poll CA 0x40039014 for 0x40/0x8.
* **Decision value**: firmware exits the spin and announces bits 3/6 -> C2 confirmed (race). If
  0x4000010C already reads 0xCECE but nothing changes -> the gate passed; go to C1/C3.

### C3 - the boot state machine never reaches state > 1 (or its group-4 destination lookup fails)

* **Firmware fact** [V]: id 1 is only queued when *(u8*)0x118B60 > 1; the setup branch that increments
  it also registers the H2D handler table and per-state objects; the send primitive silently returns
  0x8B2D if the group-4 destination record is missing (0xC27B6).
* **Read-only side-effect probe** (all reads; BAR0 windows):
  * CA 0x4003022C (BAR0 0x3E822C): the ready routine zeroes it at 0x86E7A; if it reads non-zero the
    routine has not reached that point.
  * CA 0x4000010C: 0xDEAF proves the state machine reached 0x7CEC; 0 / 0xCECE proves it also passed the
    ready routine's 0x86E7E.
  * CA 0x40000558 (BAR0 0x3BD558) bit 0: cleared at 0x86F30 after the ready routine's parameter staging;
    bit 0 still 1 => the ready routine has not run to that line.
  * CA 0x40039014 = 0x4 proves 0x86F5A ran but **not** that the 0x86F7C spin exited.
* **Decision value**: if 0x4003022C == 0 and 0x4000010C == 0xDEAF while out[1] still 0x4, the ready
  routine was stuck at the 0x86F7C spin => C2. If 0x4000010C == 0xCECE and 0x40000558 bit0 clear => the
  gate is passed but the state machine has not queued id1 => C1/C4.

### C4 - the ready frame is dropped inside the firmware's send primitive (group-4 destination missing)

* **Firmware fact** [V]: 0xC27B6 returns 0x8B2D and prints if [dest+0x10]+12*4 has no record for core 0;
  no host-visible side effect.
* **Probe**: only distinguishable after C1-C3 pass (mailbox clear, gate passed, DR enabled) and still no
  out[1] bits: snapshot DR0 +0x00/+0x10/+0x14/+0x18 (enable must be 1) and the host ring
  +0x30/+0x34/+0x38/+0x3C, and read the six CA 0x40039xxx/0x40039Axxx block-registers for any change.
  If all clean -> firmware-internal failure; escalates to section 9 open items.

### C5 - DR host-ring geometry / outbound-window address mismatch

* **Firmware fact** [V]: the device only writes its own node array; the host ring side is host-posted. If
  the host-posted node array or payload buffers sit outside the programmed outbound iATU window (devva
  0x80000000..0xFFFFFFFF identity), the engine cannot deposit and the host sees no nodes.
* **Probe**: read each DR channel's +0x30 (host ring base) and verify it lies in the outbound-window
  range; verify +0x34 == 0x1F (depth 32) and +0x38 advanced from the host's initial publish. Then watch
  +0x3C for advance.
* **Decision value**: +0x3C advancing without out[1] bits => notify/mailbox side (C1/C2); +0x3C static
  => deposit side (C5) or no send at all (C3/C4).

### C6 (low) - ETE interrupt mask / engine control mismatch

The host masked CA 0x40039508 to 0x3F201818 "as stock" while the firmware writes 0xE0E0F8F8
(0x96B0-0x96FC) [V]. Read CA 0x40039508 (BAR0 0x3F1508): if it does not read 0x3F201818 after the
firmware init ran, the masked value is being overwritten by the device - read-only evidence about init
progress, unlikely to gate the ready sends.

---

## 7. "How to confirm" - bench plan (reads first; writes marked ATTENDED)

Recovery door armed, UART read-only, one idea per session, never write CA 0x400392F0.

**Step 0 - read-only snapshot (no writes):** dump and save 0x40039010, 0x40039014, 0x40101434,
0x4000010C, 0x40000108, 0x4003022C, 0x40000558, 0x40101250, 0x40039508, 0x40039000, 0x40039800,
0x4003941C, 0x40039424, 0x40039C1C, 0x40039C24 plus all four DR blocks 0x4003A590/5E0/630/680 at
+0x00/+0x08/+0x10/+0x14/+0x18/+0x30/+0x34/+0x38/+0x3C (BAR0 = CA - 0x40000000 + 0x3B8000). Also
BAR0+0x238850 (CA 0x01060850, the id-2 payload word) and optionally BAR0+0x100A0/0x10078 (ROM helper
tables). Decision: out[1]==0x4 and 0x4000010C in {0, 0xDEAF} => C1/C2; out[1]==0x4 and
0x4000010C==0xCECE => gate passed, look at C1 first.

**Step 1 - ATTENDED mailbox service (C1):** ack 0x40101438=1 -> read+clear 0x40039014 -> re-arm
0x40101414=1; poll out[1] for 30 s for 0x40/0x8. This is the exact sequence already proven safe in B2.
**Expected success signature:** out[1] transitions 0x4 -> 0x40 (bit 6 ready deposit) or 0x8 (bit 3 ETE
done), and the DR block(s) +0x3C advance.

**Step 2 - ATTENDED C2 re-write:** if Step 1 gives nothing and CA 0x4000010C was 0 or 0xDEAF, write
0xCECE to CA 0x4000010C *after* out[1]=4 is confirmed, then repeat Step 1's poll.

**Step 3 - on any out[1] bit 3/6: read-only DR drain inspection:** record DR +0x3C/+0x38 before/after,
then dump the node ring and the first 12 B of up to 8 buffers: look for node word1 = (len<<16)|0x6D2B
and HCC header byte0&0xF = 4, u16@+4 = len, u16@+6 = 1 or 2 (group 4, ids 1/2). This is the B3 P2/P3
probe from incoming/wifi-hcc-re/REPORT.md section 9.

**Step 4 - if all clean: firmware-internal candidate C3/C4.** Only then consider a *read-only* dump of
the firmware global object (NOT mapped by our driver today; device RAM is not host-visible), or escalate
the two remaining unknowns (the 0x118B60 state writer/invoker and the group-4 destination registration)
to a source-request follow-up.

**Write budget:** the only writes in this plan are the four proven mailbox bytes (0x40101438=1,
0x40039014=0, 0x40101414=1, 0x4000010C=0xCECE). No ETE/SR/DR config is changed.

---

## 8. Verification and reproduction

Every site above was checked twice (capstone 5.0.7 Thumb + GNU objdump) in this lane. Position ranges
(file offsets, Thumb):

    085CC0-086FF0  ready routine 0x86E14 (incl. literal pool 0x86F88-0x86FD0 and the four thunks)
    07628-09168    boot/handshake state machine (id1 @0x7D28, id2 @0x9000, id3 @0x91C8)
    0C2740-0C2830  send primitive 0xC2760 / dest lookup 0xC2744
    086108  flush; 086170 d2h_notify; 0861DC-08622E D2H fill + node + commit + notify(6)
    0818AC  H2D dispatcher; 081906/08192A ring commit
    0078E/007C0     helper pair; tables file 0xC3B78 / 0xC3BA0
    0D8450/0D8268   ITCM helper twins (ROM tables rt 0x100A0 / 0x10078)
    0C4054  firmware ETE channel table (7 x 12 B)

Reproduce (local blob, no repo files needed):

    PY=.provision-venv/bin/python    # capstone 5.0.7
    $PY - <<'EOF'
    from capstone import *
    d=open("/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin","rb").read()
    md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.skipdata=True
    for a,b in [(0x7C00,0x7D70),(0x8FD0,0x9170),(0x91A0,0x9210),(0xC2740,0xC2830),
                (0x860F0,0x86240),(0x86E14,0x86FF8),(0x7C0,0x7D0),(0x78E,0x7BA)]:
        print("== %05x =="%a)
        for i in md.disasm(d[a:b],a): print("  %05x %-8s %s"%(i.address,i.mnemonic,i.op_str))
    EOF
    # helper tables
    xxd -s 0xC3B78 -l 0x80 /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
    # ARM/Thumb cross-check: GNU objdump -D -b binary -marm -Mforce-thumb on a dd slice + --adjust-vma

**Status of prior claims this lane corrects:**
1. phase37/fw-ring-message-table.md section 5: "the firmware's only mailbox announce path is 0x86170 ...
   no BL names 0xc6170" - **now three static BL sites** (0x86224, 0x86998, 0x62AC) [V].
2. phase43/device-dr-side.md section 4.3: helper-call pairing was interleaved; the verified order is
   0x7C0(0x40039000), 0x78E(0x40039000), 0x7C0(0x40039800), 0x78E(0x40039800) [V].
3. phase37/d2h-message-catalog.md section 3 "the D2H notify call site is not in this image": the notify
   site is (above), but the **ready-frame builders** (the actual HCC id1/id2 construction) are in the
   main image at 0x7D28 / 0x9000 - previously unfound [V].

## 9. Open items (honest limits)

1. **Invoker of the boot state machine 0x7628** - no static caller, no data pointer literal; re-entered
   from a runtime scheduler/other image [U].
2. **The only writer of the state byte rt 0x118B60** that we found is 0x8174 (state setup branch); whether
   another path resets it is [U].
3. **Slot-2 callback invoker** (0x820F8 wait_cece, registered via 0x86DA2 -> 0x874B0 at 0x7CE0) - the
   dispatcher that calls [global+0x98+slot*4] is not mapped [U].
4. **Group-4 destination registration** (table [0xC281C]+0x448+24*core, entity +0x10+12*group) -
   where/when it is populated is not mapped [U]; if it is never populated on our bench, C4 stands.
5. **Purpose of the id-1 "success" continuation at 0x9188** (builds a group-4 id3 message whose payload is
   seeded from the "DEVICE EXCP_INFO" string at rt 0x1043A5) - [V] instructions, [U] semantics.
6. **ITCM helper ROM tables** rt 0x100A0 / 0x10078 are below the image base and not in the blob [U];
   read-only host probe proposed in section 5.2.
7. **Which DR channel** carries the ready frames - prediction DR0 (section 3.3) [I]; bench probe B3 P2.
8. **Who stamps the 0x5A5A transport tag** on D2H buffers - no store of 0x5A5A exists anywhere in the
   firmware image except the two compares at 0x8699E / 0x862A6, so it is set below the HCC layer (engine
   or a table) [U].

---

*No vendor binary, blob, calibration value, MAC or secret is included; this document contains only
offsets and analysis. Lane wrote only this file. Device untouched.*

---

## 10. Follow-up (2026-11-22, bench results in): the 0x4 announce is an exception/startup-vector handshake - decision tree and corrected reads

Bench facts supplied by the Lead (host reads only; no device contact in this lane):
(A) CA 0x4000010C read 0x0000BAAD before release, 0x00000000 2 s after release;
(B) one late 0xcece write read back 0xcece, then read 0x00000000 one second later and out[1] re-announced 0x4
    (a second announce event) - the device re-enters the announce path;
(C) 400 hammered 0xcece writes make CA 0x4000010C persist, but still no out[1] bit 6 (0x40), no DR fills advance
    (all four DR +0x3C stuck at init, node word1 = 0), no group-4 frames;
(D) device status live, ETE intr 0x3F201818, outbound iATU window programmed, DR channels armed with plain
    publish (+0x38 = 0x1F).

### 10.1 New verified finding: 0x86E14 is the chip's non-reset exception/startup vector handler

The four ready thunks are not called by any firmware code; they are the **targets of the ARM vector stubs**:

| vector table word (file 0x50+) | stub (file) | branches to (ARM veneer) | veneer target (runtime) | ready thunk (file, r1) |
| --- | --- | --- | --- | --- |
| 0x54 = 0x00103114 | 0xC3114 | 0xC3B68: ldr pc,[pc,#-4]; 0x000C6FC9 | 0xC6FC8 | 0x86FC8, r1=4 |
| 0x58 = 0x001030E8 | 0xC30E8 | 0xC3B70: ...; 0x000C6FD7 | 0xC6FD6 | 0x86FD6, r1=3 |
| 0x5C = 0x001030BC | 0xC30BC | 0xC3B40: ...; 0x000C6FE1 | 0xC6FE0 | 0x86FE0, r1=2 |
| 0x60 = 0x00103090 | 0xC3090 | 0xC3B48: ...; 0x000C6FEB | 0xC6FEA | 0x86FEA, r1=1 |
| 0x50 = 0x00103140 (reset) | 0xC3140 | calls runtime 0x82EEC after saving the context frame | - | - |

Each non-reset stub pops the exception PC, saves a **17-word context frame** at sp and passes **r0 = frame
pointer**, then branches to the veneer (ldr pc,[pc,#-4] at file 0xC3B40/0xC3B48/0xC3B68/0xC3B70) which jumps
to the ready thunk [V]. The thunks then adjust the saved exception return address at frame+0x3C
(subs r3,#4, or #8 for the r1=4 variant) and enter 0x86E14 with r1 = 1..4 [V].

Consequences (this supersedes the "core id" language in sections 1.1 and 1.2):

1. **The routine stages the exception/startup context, not host boot parameters.** The 17 words it copies to the
   firmware image header (CA 0x4000C..0x4004C; runtime = BAR0+0x4000C..0x4004C via the region-0 alias) are
   r0-r12, sp, lr, lr_exc, spsr of the entering vector [V layout]. The halfword it ORs at CA 0x40008 is
   **1<<vector** - a vector-seen bitmask [V].
2. **The re-entry the bench saw is exactly this: every entry zeroes CA 0x4000010C (0x86E7E), writes
   out[1]=4 + doorbell (0x86F5A/0x86F66), and spins for 0xcece (0x86F78)** [V]. A single host 0xcece write can
   only release the one entry that happens to be spinning; the next entry zeroes it again. Hammering works
   because every spin test then sees 0xcece. Persistence after hammering means no further vector entries were
   pending (all four vectors already released).
3. **The boot flow waits on the four vector entries before the normal firmware task can progress** [I]. The
   vendor host does not fight this race because its platform layer writes 0xcece at the right time (the peer
   write site is still [U] in the host objects); our single-shot write loses it.

### 10.2 Answers to the five questions

**Q1 - what advances the state byte at rt 0x118B60?** Only file 0x8174 in the boot/handshake state machine
0x7628 (strb.w r3,[r4,#0xF60] with r3 = r6+1, under the lock at rt 0x118B24) [V]. The function that contains it
has **no static caller and no pointer literal anywhere in the blob**: a raw BL/BLX decode over every even file
offset (whole image, both regions) finds no branch to 0x7628 and a word scan finds no 0x00047628/0x00047629 [V
negative]. So it is not invoked by a static firmware call tree: it is entered from **outside the image** (ROM
fixed-address entry/task poll) or through a runtime pointer that is not in the image [U]. Therefore we cannot
prove "boot timer" vs "interrupt" from the blob; what we can prove is that it is not driven by any code in the
blob. Host-visible side effect: read the state byte itself (10.4 read list) - 0 or 1 = the setup branch has not
completed twice; >= 2 = id1 is queued on the next entry, so a missing bit 6 is then a publish/send problem, not
a state problem.

**Q2 - what is CA 0x4003022C?** It is a **live region-3 IO word, not RAM**. The boot state machine writes 1 to it
at file 0x7640 (str r2,[r3] with the literal rt 0x4003022C at file 0x7938) and the ready routine writes 0 at
0x86E7A [V]. The bench value 0x443CF249 proves neither write persists as a memory flag: writes to this address
are consumed/overridden by hardware. The read itself is valid (region 3, BAR0+0x3E822C) but it is **not a
progress indicator** - my earlier "expected 0" in section 6/C3 is corrected and must be dropped from the
decision tree. The same caution applies to CA 0x40000558 (BAR0+0x3BD558): it is region-3 IO; treat its value as
hardware state, not as a firmware progress latch.

**Q3 - does the id1 build at 0x7D28 depend on host pre-release writes?** No. The id1 path reads no host-written
memory: its only inputs are the state byte, the message allocator and the destination table [V]. The 17-word
"boot parameter header" at CA 0x40008-0x4004C is **not host-owned**: it is the saved vector context written by
the ready routine on every vector entry (10.1), so (a) a host must not write boot params there - any host word
would be clobbered by the next entry, and (b) nothing in id1 depends on it. The only host values this path needs
are the uploaded firmware image (already verified diffs=0), the one-shot release CA 0x40000108 = 0x5a5a, and
**CA 0x4000010C holding 0xcece at each vector entry** (sustained, not one-shot).

**Q4 - exact decision tree of reads after gate acceptance.** Two different mappings are needed:
* region-3 **IO** CAs (0x40000000..0x4011FFFF): BAR0 = 0x3B8000 + (CA - 0x40000000);
* region-0 **firmware RAM/image alias** CAs (0x00000000..0x001BFFFF): BAR0 = CA directly (no 0x3B8000 offset).
  The firmware runtime address equals the CA in that window (runtime = file + 0x40000), e.g. ready-routine code
  rt 0xC6E14 = file 0x86E14 -> BAR0+0xC6E14; the state byte rt 0x118B60 -> BAR0+0x118B60.

Read list (all reads unless marked ATTENDED), in order:

| # | what | CA / runtime | BAR0 offset | expected / decision |
| --- | --- | --- | --- | --- |
| 0 | image-alias sanity | CA 0x00040000 / 0x00040004 | 0x40000 / 0x40004 | 0x00046971 / 0x000C742D; if not, the region-0 alias is not what you think and all rt reads are invalid |
| 0b | code sanity | rt 0xC6E14 (file 0x86E14) | 0xC6E14 | 0x43F8E92D (halfwords E92D 43F8); confirms the RAM alias reads code |
| 1 | **vector mask** | CA 0x00040008 (halfword) | 0x40008 | bits 1..4 = which of the four vector entries have run; 0x001E = all four; changes across two reads mean a vector re-entered |
| 2 | **last vector context** | CA 0x00040048 / 0x0004004C | 0x40048 / 0x4004C | word 15 = saved exception return address (runtime), word 16 = saved SPSR (mode bits: 0x13 SVC, 0x1F SYS, 0x12 IRQ, 0x11 FIQ, 0x17 ABT, 0x1B UND, 0x10 USR). Two reads differing = re-entry. This tells exactly which vector fired and where |
| 3 | **state byte** | rt 0x118B60 | 0x118B60 | 0/1 = boot task did not complete setup (its invoker never ran enough); >=2 = id1 built/queued on the next entry |
| 3b | per-state record | rt 0x118B64 + (state-1)*0x84 | 0x118B64 + ... | non-zero record = that state slot was processed |
| 3c | state lock | rt 0x118B24 | 0x118B24 | 0 unlocked; non-zero = someone holds it |
| 4 | global msg object | rt 0x117C00 | 0x117C00 | +0x9C should read 0x40039014 (out[1] pointer); +0xA4 = 0x40101434; +0xB4 armed; +0xB8 pending shadow; +0x270/0x274/0x278/0x27C list heads; +0xF60 = state byte again |
| 5 | out[1] | CA 0x40039014 | 0x3F1014 | 0x00000004 before mailbox service; 0x00000040 (bit 6) or 0x8 (bit 3) after |
| 6 | out[0] | CA 0x40039010 | 0x3F1010 | 0 |
| 7 | gate | CA 0x4000010C | 0x3B810C | 0x0000CECE while served; 0x00000000/0xDEAF = an entry is/was waiting |
| 8 | DR0..DR3 +0x00 enable | CA 0x4003A590/5E0/630/680 | 0x3F2590/5E0/630/680 | 1 (firmware-set); 0 = firmware DR init did not run/complete |
| 9 | DR0..DR3 +0x34 depth-1 | same +0x34 | +0x34 | 0x1F (host-programmed) |
| 10 | DR0..DR3 +0x38/+0x3C | same +0x38/+0x3C | +0x38/+0x3C | +0x38 = host publish; +0x3C must advance if a D2H deposit/notify happened; static = no publish (see 10.4) |
| 11 | ETE intr | CA 0x40039508 | 0x3F1508 | currently 0x3F201818 (host-authored, see Q5) |
| 12 | PCIe glue ids | CA 0x40039000 / 0x40039800 | 0x3F1000 / 0x3F1800 | 0x10B / 0x10C (block-id sanity) |
| 13 | id2 payload source word | CA 0x01060850 (PKTRAM region 2) | 0x238850 (0x1D8000 + (CA - 0x1000000)) | host-readable; the id2 payload is the constant 0x01060850 itself, so this only cross-checks the constant |

Decision logic: (a) if row 1 is 0 or rows 2 stop changing, the vector handshake is over; (b) if row 3 < 2 the
boot task never advanced - no host-side fix exists (escalate the ROM entry/trigger question); (c) if row 3 >= 2,
run the mailbox service of section 7 step 1 while keeping CA 0x4000010C = 0xcece, then re-read rows 5, 10, 11;
(d) if row 4 shows +0xB8 pending shadow with bit 6 but row 5 does not, the publish is blocked on P2 (out[1] not
drained) - the single most likely remaining cause (10.4); (e) if rows 8/9 show the DR init state missing, the
firmware's own pcie_msg_init (file 0x9334) did not run - a different, earlier failure.

**Q5 - does the firmware program ETE interrupt/mask registers we could have clobbered?** Yes, once, in
pcie_msg_init (file 0x96B0-0x96FC) [V]:
ldr r3,[r4,#0xc] (the mapped ETE intr pointer) -> read/modify bfi r2,r7,#0xc,#1 (bit 12),
bfi r2,r7,#0x1d,#1 (bit 29), then ldr r3,lit @0x97C8 = 0xE0E0F8F8; ands r3,r1; str - i.e. the firmware
ends with *0x40039508 &= 0xE0E0F8F8 (clears bits 0-2, 8-10, 16-20, 24-28; keeps 3-7, 11-15, 21-23, 29-31)
[V]. It also registers two ETE callbacks (slots 0x2D/0x2E) there. There are **no reads** of CA 0x40039508
anywhere in the image, so the host's write of 0x3F201818 cannot gate firmware progress - and note 0x3F201818 has
bits 24-28 set, which the firmware's own AND would have cleared, so the current value is host-authored (that is
the same style of write the vendor host performs: AND with 0xFFE0F8F8 on the same register). Conclusion: the ETE
intr mask is not the missing trigger; the state-machine trigger is not an ETE interrupt (Q1). No other
interrupt-mask register is written anywhere in the ready/send paths. The only interrupt-like setup near the
state machine is the callback registration via 0x86DA2 -> 0x874B0 (slot 2 = the second 0xcece waiter 0x820F8;
slots 0x4C/0x4E in pcie_msg_init for the H2D dispatcher and D2H flush) [V]; those are mailbox/HCC callbacks, not
timer/IRQ masks.

### 10.3 Corrected priority list after the bench data

The earlier C2 (0xcece race) is now **[V*] confirmed** and is no longer a cause by itself; the announce
re-entry is explained (10.1), and persistence after hammering proves the vector entries can be released. The
state byte / vector-context reads above are the deciders. My earlier CA 0x4003022C indicator is withdrawn (Q2).

### 10.4 Single most likely remaining cause (with the exact first experiment)

**The D2H publish is blocked on P2: out[1] (CA 0x40039014) still holds 0x4 and has never been acked/drained/
re-armed.** The bench never reported writing out[1]=0/0x40101438/0x40101414; the firmware's flush routine
0x86108 spins on out[1]==0 (0x86120-0x86128) before it can publish any further D2H word, and the only id-6 D2H
deposit block 0x861DC is reached through that same service. This explains every (C) observation without any new
firmware fault: no bit 6, no DR +0x3C advance, no group-4 frames - the frames (if built) never leave the device.

Experiment (ATTENDED; the four proven mailbox bytes):
1. read CA 0x40039014 (BAR0 0x3F1014) and CA 0x4000010C (BAR0 0x3B810C);
2. hold CA 0x4000010C = 0x0000CECE (re-write after each out[1] announce);
3. write CA 0x40101438 = 1 (ack, BAR0 0x4B9438), read CA 0x40039014 then write 0 (drain, BAR0 0x3F1014),
   write CA 0x40101414 = 1 (re-arm, BAR0 0x4B9414);
4. poll CA 0x40039014 for 30 s; then re-read rows 3/4/10/11 of the read list.
Success signature: out[1] transitions 0x4 -> 0x40 (bit 6) and/or 0x8 (bit 3), global-object +0xB8 shadow takes
the new bit, DR0 +0x3C advances, and the node ring shows word1 = (len<<16)|0x6D2B with HCC header group 4,
id 1/2. If the state byte (row 3) is < 2 at that moment, the block is earlier and the state-machine trigger
(ROM entry) becomes the open question instead.


---

## 11. Follow-up 2 (2026-11-22, bench says the read map is wrong): corrected link/run map, the boot-task entry chain, and a decode-ready probe list

Bench facts supplied by the Lead (host-only; no device contact in this lane):
(A) after release the vector handler DID run: CA 0x4000010C read 0x00000000 (after a pre-release 0x0000BAAD)
    and out[1] = 0x4 appeared;
(B) holding CA 0x4000010C = 0xCECE and holding out[1] at 0 (600-iteration loop with ack/drain/re-arm)
    still produced no out[1] bit 6, no DR fill advance (DR0-3 +0x3C stuck at the pre-release 0x10), no
    group-4 frame;
(C) the section-10 read map does NOT match the device: BAR0+0x40000 = 0x00000020 (not 0x00046971),
    BAR0+0xC6E14 = 0, BAR0+0x118B60 = 0, BAR0+0x117C9C = 0.

### 11.1 Corrected link/run map (supersedes section 10.2/Q4 and the old record convention)

**The image is loaded once, at CPU runtime base 0x40000, and the host reaches that RAM only through the
region-5 window.** The host uploads file byte f to device CA 0x1240000 + f (region-5 window,
BAR0 0x6F8000 + f). The CPU executes the same memory at CPU runtime R = 0x40000 + f. Therefore:

* **CPU runtime R  <->  device CA (region-5 view) = R + 0x1200000**
* **host BAR0 offset for a CPU runtime R: BAR0 = 0x6B8000 + R** (region-5 window starts at BAR0 0x6B8000
  for CA 0x1200000), valid for R in [0, 0x217FFF] (window ends CA 0x1417FFF).
* file offset f = R - 0x40000 (unchanged).
* region-3 IO is separate and unchanged: **BAR0 = 0x3B8000 + (CA - 0x40000000)**.
* region-2 PKTRAM: **BAR0 = 0x1D8000 + (CA - 0x1000000)**.
* **The region-0 window (BAR0 0x0-0x1BFFFF) does NOT contain this RAM** - the bench disproves the old
  "also aliased at BAR0+0x40000" claim (BAR0+0x40000 reads 0x20, not the image header). Do not read
  firmware RAM through BAR0 offsets below 0x1C0000; use the 0x6B8000 formula.

Sanity anchors for the formula: CPU 0x40000 (image header) -> BAR0 0x6F8000 should read 0x00046971
(word 0) and 0x000C742D (word 1); CPU 0xC6E14 (ready routine) -> BAR0 0x77EE14 should read 0x43F8E92D;
CPU 0x47628 (boot state machine) -> BAR0 0x6FF628 should read 0x4FF0E92D.

Also corrected: the image header words are **CPU runtime pointers**, not file offsets -
+0x00 = 0x00046971 is Thumb CPU 0x46970 (file 0x6970, a real function prologue), +0x04 = 0x000C742D is
CPU 0xC742C (file 0x8742C, a function), and the 5-word table at +0x50 is 0x103140/0x103114/0x1030E8/
0x1030BC/0x103090 (CPU vector entries) [V].

### 11.2 What the boot state machine (CPU 0x47628 / file 0x7628) is and what must happen before it advances

**Entry evidence [V negative]:** an exhaustive scan of the whole blob finds **no reference to CPU
0x47628 / 0x47629 anywhere** - no BL/BLX/B.W target (raw Thumb decode over every even offset), no
literal word at any alignment, no movw/movt pair. So the state machine is **not called by any code in
the image**; its entry comes from outside the blob (ROM/dispatcher/other boot stage) [I].

**The only in-image reset dispatch chain [V]:**
1. reset vector entry CPU 0x103140 (file 0xC3140) is an ARM stub that saves a context frame, then
   calls CPU 0xC2EEC (file 0x82EEC) (ARM BX with interworking);
2. CPU 0xC2EEC is a 6-instruction dispatcher: it loads CA 0x17D398 and tail-calls the function pointer
   stored at **CA 0x17D42C** (0x17D398 + 0x94), or returns if it is 0;
3. the boot state machine's **prelude** (file 0x7CE4-0x7D08, immediately before its state check)
   *one-shot disables that slot*: it writes the BUG stub value **0x000C19D1** (file 0x819D0) to
   **CA 0x17D42C** at file 0x7D06-0x7D08, writes BUG stubs 0x000C19E3 and 0x000C19D7 to CA 0x1808DC and
   CA 0x118AEC, writes 0xDEAF to CA 0x4000010C, and registers H2D id 5 = BUG and callback slot 2 =
   the second 0xcece waiter [V].

That one-shot disarm strongly implies CA 0x17D42C is the boot/recovery entry slot the ROM seeds and
the reset dispatcher calls; i.e. the boot state machine is reached at cold boot through that slot, and
it disarms the slot once its prelude has run [I]. The seeding value itself is set off-image (ROM) [U].

**The 0x8160 condition, exactly [V]:** 0x7D0A locks CA 0x118B24; 0x7D10 loads the state byte CA 0x118B60;
0x7D16 compares it with 1; 0x7D18 branches to 0x8160 when state <= 1. The setup branch immediately
advances it (0x816A-0x8174: state = state+1) and then, after per-state setup, branches to **0x7D28**
(the group-4 id-1 build) at 0x828A/0x8290. The state>1 path at 0x7D1C-0x7D28 goes to the same id-1
build. Consequence: **the state byte is not a wait flag; advancing it and attempting the id-1 send
happen in the same pass.** Therefore:
* state byte == 0 -> the boot state machine never reached 0x7D0A (never entered, or exited in its
  prelude, or is stuck before the lock);
* state byte >= 1 -> id-1 was attempted in that pass (if it is 1 the setup branch aimed for the send
  but may have exited between 0x8174 and 0x828A/0x8290; if 2 the direct send path ran).

**No timer/IRQ mask is involved in this path [V]:** the prelude and the state check contain no read of
the ETE interrupt block (0x40039508) and no timer-enable write; the only host-visible register the
state machine writes before the check is CA 0x4000010C = 0xDEAF (file 0x7CEC) plus the deliberately
disarmed callback slots above. The prelude's hardware reads are chip ID/config/strap reads (0x3E8,
0x858 and the config-table read at 0x76E2-0x76FC); none is host-writable.

**Prelude early-exit points (why state can stay 0 even if the task runs) [V]:** at 0x76A6 it calls
0x3E8(index) and exits at 0x9172 if the result is non-zero; at 0x76CE it exits if the pointer loaded
from CA 0x172134 is NULL; at 0x76F0-0x76FC it reads a config table and exits if the value is >0xFDE8;
at 0x7704 it exits if a count is >6; at 0x7756 it calls 0x858 and exits if the result is 0; and at
0x7800-0x782A it calls function pointers from freshly built objects and exits on NULL/zero returns.
These are firmware/ROM/hardware conditions; none is a host mailbox or ETE condition.

### 11.3 Probe list - what to read and what each result proves (corrected mapping)

Formula reminder: **CPU runtime R -> BAR0 = 0x6B8000 + R** (region-5 window); region-3 IO ->
BAR0 = 0x3B8000 + (CA - 0x40000000).

First verify the map (all reads):
| # | what | CPU R / CA | BAR0 offset | expected |
| --- | --- | --- | --- | --- |
| 0 | alias check: image header w0 | R 0x40000 | 0x6F8000 | 0x00046971 |
| 0b | image header w1 | R 0x40004 | 0x6F8004 | 0x000C742D |
| 0c | ready-routine code | R 0xC6E14 | 0x77EE14 | 0x43F8E92D |
| 0d | boot-task code | R 0x47628 | 0x6FF628 | 0x4FF0E92D |
If row 0 is not 0x00046971 the window/offset is still wrong - stop and fix that first.

Then the boot-task progress markers (this is the decisive test):
| # | marker | CPU R | BAR0 | expected if prelude ran |
| --- | --- | --- | --- | --- |
| 1 | early head marker A | 0x18179C | 0x83979C | 0x004166B0 (written 0x762E) |
| 2 | early head marker B (self-list) | 0x118B58 | 0x7D0B58 | 0x00118B58 (written 0x7646) |
| 3 | early head marker C (self-list) | 0x118B5C | 0x7D0B5C | 0x00118B58 (written 0x764A) |
| 4 | late prelude marker A | 0x1808DC | 0x8388DC | 0x000C19E3 (written 0x7CFA) |
| 5 | late prelude marker B | 0x118AEC | 0x7D0AEC | 0x000C19D7 (written 0x7D02) |
| 6 | late prelude BUG slot | 0x17D42C | 0x83542C | 0x000C19D1 (written 0x7D08) |
| 7 | STATE BYTE | 0x118B60 | 0x7D0B60 | 0 = not reached 0x7D0A; 1 = setup branch ran; 2 = send path ran |
| 8 | state lock | 0x118B24 | 0x7D0B24 | 0 unlocked; non-zero = held |
| 9 | per-state record | 0x118B64 | 0x7D0B64 | non-zero record = state slot processed |
| 10 | global msg object ptr | 0x117C00 | 0x7CFC00 | +0x9C (0x7CFC9C) = 0x40039014; +0xA4 (0x7CFCA4) = 0x40101434; +0xB4 armed; +0xB8 (0x7CFCB8) pending shadow |
| 11 | vector-seen mask | 0x40008 | 0x6F8008 | halfword bits 1-4 set (0x001E if all four vectors ran) |
| 12 | saved exception lr / spsr | 0x40048 / 0x4004C | 0x6F8048 / 0x6F804C | exception return address (CPU) / saved SPSR; changing between reads = re-entry |
| 13 | gate | CA 0x4000010C | 0x3B810C | 0xCECE while served; 0/0xDEAF = an entry is waiting |
| 14 | out[1] / out[0] | CA 0x40039014 / 0x40039010 | 0x3F1014 / 0x3F1010 | pending D2H bitmap |
| 15 | D2H ack / re-arm | CA 0x40101438 / 0x40101414 | 0x4B9438 / 0x4B9414 | readable host-side mailbox bytes |
| 16 | ETE intr | CA 0x40039508 | 0x3F1508 | 0x3F201818 (host-authored) |
| 17 | DR0..DR3 enable / depth / fill | CA 0x4003A590/5E0/630/680 | 0x3F2590/5E0/630/680 at +0x00/+0x34/+0x3C | +0x00 = 1; +0x34 = 0x1F; +0x3C must advance after a deposit |
| 18 | boot-task memset targets (PKTRAM) | CA 0x1048C48, 0x1042DE8, 0x103B048 | 0x224C48, 0x222DE8, 0x21B048 | zeroed (sizes 0x2A80 / 0x5E60 / 0x7DA0); weak evidence because they may already be zero |

Decision tree:
* rows 1-3 all zero -> the boot state machine **never entered** its prelude. Since nothing in the blob
  calls it, its entry is the off-image reset-dispatch slot CA 0x17D42C (or an equivalent ROM task
  bounce). This is a ROM/boot-stage problem, not a mailbox problem; the driver cannot fix it by any
  register write seen in the blob. Candidate cause C-A.
* rows 1-3 set, rows 4-6 zero -> the task entered but **exited inside the prelude** before 0x7D08
  (checks at 0x76A6/0x76CE/0x76FC/0x7704/0x7756/0x7800). Candidate cause C-B; read row 18 and rows 4-6
  again after a delay to confirm it is not still running.
* rows 1-6 set but row 7 == 0 -> it reached 0x7D08 but never crossed 0x7D0A: check row 8 (lock) - if the
  lock is held, the lock at CA 0x118B24 is the blocker; otherwise it died between 0x7D08 and 0x7D14
  (should not happen on a live core). Candidate cause C-C.
* row 7 >= 1 and no out[1] bit 6 -> id-1 build/send was attempted. Read row 10 (+0xB8 pending shadow):
  if the shadow has 0x40 the publish is blocked on P2/drain (serve the mailbox); if the shadow is 0 the
  frame was dropped inside send primitive 0xC2760 (group-4 destination record missing, 0xC27B6) -
  candidate cause C-D. Row 17 +0x3C tells whether any deposit ever happened.

### 11.4 Most likely remaining cause (and the first probe)

Given (A)+(B), the vector handshake is working and the host can satisfy 0xcece and P2, yet nothing is
queued. The single most likely remaining cause is therefore **C-A/C-B: the boot state machine never
reached its state check** - it either was never entered (no in-image caller exists; its entry is the
off-image reset-dispatch slot CA 0x17D42C / ROM task bounce) or it exited inside its prelude. In both
cases the state byte stays 0 and no id-1 is ever built, which matches every (B) observation. The
P2/mailbox theory is now demoted: with out[1] drained and 0xCECE held, a built id-1 would have produced
bit 6.

First probe (all reads, one script): BAR0 0x6F8000 (expect 0x00046971), 0x6F8004 (0x000C742D),
0x77EE14 (0x43F8E92D), 0x83979C (0x004166B0), 0x7D0B58/0x7D0B5C (0x00118B58), 0x8388DC (0x000C19E3),
0x7D0AEC (0x000C19D7), 0x83542C (0x000C19D1), 0x7D0B60 (state: 0/1/2), 0x7D0B24 (lock), 0x6F8008
(vector mask), 0x7CFCB8 (pending shadow), 0x3F1014 (out[1]). That one snapshot decides C-A vs C-B vs
C-C vs C-D without any write.

Note on the pre-release 0xBAAD: CA 0x4000010C is written by the firmware prelude to 0xDEAF (0x7CEC)
and zeroed by the vector handler (0x86E7E), so 0xBAAD was an earlier host/test value; it carries no
meaning by itself.


---

## 12. Follow-up 3 answers: vector/entry model, frame decode, RAM aperture, and the destination-record probe

### 12.1 (a) Only vector bit 3 (mask 0x08) - what that means

The staged halfword at CA 0x40008 is OR of (1 << r1) where r1 is the thunk index 1..4; 0x08 = index 3 only.
The five header words at CA 0x40050 are *entry addresses used by boot code that calls the stubs*, and the
saved frame (see 12.2) shows a normal call, not a hardware exception. So the other indices are not queued
by hardware: each is taken when later boot code calls the corresponding entry (1/2/4). Because the boot
flow stopped at the id-1 send drop in the same pass that advanced the state (12.5), no later entry was
ever called. The extra out[1] = 4 events you saw while hammering 0xCECE are re-entries of the *same*
handler (index 3) only - they do not run the boot state machine.

### 12.2 (b) Frame decode (layout: {r0-r12, sp, lr, lr_exc, spsr} staged at CA 0x4000C..0x4004C)

r0 = 0x0017DF44 (lock/callback object; the same pointer the handler unlocks at 0x86F82), r1 = 0, r2 =
0x001BD000, r3 = 0x001BDC40, r4 = 0x0017E1B8, r5 = 0x42085EBF, r6 = 0x0017E1B8, r7 = 0x3577D5C2, r8 =
0x000BF98F, r9 = 2, r10 = 0x0017E1B8, r11 = 0x0017E2AC, r12 = 0x1A, sp = 0x001BDE90, lr = 0x00088BBD,
lr_exc = 0xFF33CA94, spsr = 0x20000193.

* lr = 0x00088BBD is a valid CPU address inside the image (file 0x48BBD) - this is the return address of a
  **normal call** to the entry stub. lr_exc = 0xFF33CA94 is not a valid code address, so the stub was not
  entered as a hardware exception.
* spsr = 0x20000193 decodes as mode 0x13 (SVC), ARM state, IRQ/FIQ masked, C flag set; because the entry
  was a call, this is the **stale SPSR left by the last real exception (reset)**, not the type of this
  entry [I]. Therefore the SVC-mode context does NOT identify an SVC exception.
* sp = 0x001BDE90 proves the CPU RAM/stack extends to at least that address (so internal RAM covers roughly
  the region-0 size, 0x0-0x1BFFFF).

### 12.3 (c) Does the handler return into the boot task?

No. The handler returns to its caller through lr = 0x00088BBD (a boot/init call site in the image), not
into the boot state machine at CPU 0x47628. The handler and the boot task are different code paths.

### 12.4 (d) The 0xFFFFFFFF markers: real writes, not readable through our host aperture

CPU addresses 0x17D42C / 0x1808DC / 0x18179C are:
* inside the CPU internal RAM extent (region-0 space 0x0-0x1BFFFF; stack evidence above), and
* inside the vendor region-5 CA window (CA 0x1200000-0x1417FFF, size 0x218000), i.e. they map to CA
  0x137D42C / 0x13808DC / 0x138179C and host BAR0 0x83542C / 0x8388DC / 0x83979C (your reads).
But on our device host reads above roughly internal 0x140000 return 0xFFFFFFFF (here: everything >= 0x17D000
failed, while 0x118AEC / 0x118B58 / 0x118B5C / 0x118B60 all worked). So the prelude writes did execute
(proved independently: state byte = 1 means 0x7D0A was reached, which is after the 0x7D08 write), they are
just outside the host-visible ACP aperture on our unit. First read: BAR2 iATU viewport 5 registers - ctrl2
BAR2+0xB04, base_lo +0xB08, limit +0xB10, target_lo +0xB14 - expect base 0x406B8000, limit 0x408CFFFF,
target 0x01200000. If limit is correct, the 0xFFFFFFFF ceiling is a device/ACP decode limit; if limit is
smaller than 0x408CFFFF, our driver's region-5 viewport is undersized and can be corrected host-side.

### 12.5 Destination records of send primitive 0xC2760 - what they are, who fills them, and why they are empty

**Address/layout [V].** The registry base is CA **0x171B6C** (literal at file 0xC281C). For a message with
source core c (header +9) the record is at **CA 0x171B6C + 0x448 + 24*c** (core 0 -> CA **0x171FB4**).
Lookup 0xC2744(record, group):
* fail #1: [record+4] == 0; fail #2: [record+0x10] == 0;
* fail #3: [[record+4]+0x10] <= group (the object's max group id); group 4 needs max id > 4;
* record+0x08 = a lock key (used by 0xC24CE), record+0x10 = pointer to an array of 12-byte group entries.
* the chosen entry = array + 12*group; entry+8 is a per-group send counter; entry is what 0xC2662 enqueues.
Any of the three failures -> 0xC27B6 printk and return 0x8B2D, no mailbox write: exactly your drop.

**Who fills them [V].** The only in-image populator is a block inside the same boot-state-machine function,
file **0x7ADC-0x7F1E**: at 0x7ADC-0x7AE6 it memsets 0x490 bytes at CA 0x171B6C (memset helper 0x81E0C,
literal 0x7D6C = 0x00171B6C); then it walks a 0x2C-stride config table at CPU 0x103B78 (file 0xC3B78,
literal 0x7D74; runtime base 0x103FB8 at 0x7D70) whose entries carry {start,end} counts at +0x440/+0x444,
and for each id fills CA 0x171B6C+24*id fields +0x448/+0x44C/+0x450/+0x454/+0x458/+0x45C with object and
array pointers into the hcc object set based at CA 0x117C00, with range checks (counts <=3, id <=7) and a
callback at [object+0x28] that must return 0, else the function exits via 0x782E.

**When it runs - and why it is empty on our device [I, from the observed state].** The id-1 send on the
first pass takes the 0x8160 setup branch, which increments the state and jumps directly to the id-1 build
at 0x7D28; the registry-populate block is a later block/pass. Our first send failed and the function took
the 0x782E error exit, and the boot task cannot run a second pass (state = 1; the reset-dispatch slot
CA 0x17D42C was one-shot overwritten with BUG 0x000C19D1 at 0x7D08; the only references to CA 0x17D42C and
0x171B6C in the image are in this boot task, so nothing re-seeds/re-enters it; the extra handler re-entries
do not touch it). In a vendor boot the registry must therefore be populated by an earlier stage before the
first boot-task pass (candidate: the function at file 0x46C8C, which writes byte 2 at CA 0x171B6C at
0x46CA8, and the off-image ROM init) [I]; no host-writable register or mailbox bit appears anywhere in the
population path, so **there is no host register action from the blob that can make the records appear**.

### 12.6 Next probe set (reads; aperture first)

1. BAR2 iATU viewport 5 (BAR2+0xB04/+0xB08/+0xB10/+0xB14) - confirms whether region-5 is full-size. If it
   is undersized, fix the driver and re-read (attended, iATU config only). This unblocks everything below.
2. After the aperture works: registry dump at host BAR0 **0x82B16C** (CA 0x171B6C): read 0x490 bytes in
   0x40-byte chunks and decode per 24-byte record. Core-0 record CA **0x171FB4** -> BAR0 0x831FB4; read
   words at +0x00, +0x04 (object ptr), +0x08 (lock key), +0x10 (group-array ptr), +0x14.
   Cores 1-3: CA 0x171FCC / 0x171FE4 / 0x171FFC.
3. If record+4 = O (object): read O+0x10 (max group id) -> host BAR0 0x6B8000+O+0x10. If record+0x10 = A:
   group entry for group 4 = A+48; read its 3 words (entry+0/4/8).
4. Low-RAM markers still readable today (no aperture fix needed): CA 0x118AEC = 0x000C19D7 (late prelude
   reached 0x7D02), CA 0x118B58 = CA 0x118B5C = 0x00118B58 (head ran), CA 0x118B60 = 1 (setup branch ran),
   CA 0x118B24 = 0x00010001.
5. Correction of my earlier row: the D2H pending shadow is NOT at CA 0x117C00+0xB8 (that is the firmware
   message/queue object; your 0x01A301A3 belongs to it). It is at (g)+0xB8 with g = *(CA 0x172130), and
   CA 0x172130/g are in the high RAM not readable through the current aperture, so that read must wait for
   step 1. The earlier '+0x9C = 0x40039014' expectation applied to that g object, not to CA 0x117C00.
6. Only after a record with max group id > 4 and a valid array exists should a second id-1 attempt be
   expected; watch out[1] (BAR0 0x3F1014) and DR0-3 +0x3C. Absent a boot-task re-entry, a fresh power
   release is the only way to give the boot task another first pass once the destination is known to exist.

