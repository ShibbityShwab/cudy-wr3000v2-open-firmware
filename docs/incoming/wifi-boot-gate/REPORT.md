# Boot-task post-send chain and the 0x48820 gate: why the per-link +0x4B0 bind does or does not run (boot-gate lane)

Host-only lane "boot-gate", shared task task-77, 2026-11-23. Inputs are local artifacts only:
the vendor firmware image at
/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin
(928,920 B, **local evidence only - never copied or committed**) and the committed prior lane
reports (cited, not repeated). No device was contacted; no image was built or modified; nothing
was written outside this directory. No vendor bytes are reproduced: only CPU addresses,
mnemonics and decoded arguments of the kind prior lanes already use.

Evidence markers: **[V]** verified here from the image with tools/bootgate.py; **[V*]**
device-measured by an earlier attended lane and cited; **[I]** inference from code structure;
**[U]** unknown / not decidable from the image.

Address conventions (same as wifi-fw-link-event / wifi-abort-rca):

    CPU address = file offset + 0x40000
    host BAR0 (CPU RAM object, region 5) = CPU + 0x6B8000
    host BAR0 (device IO CA, region 3)   = 0x3B8000 + (CA - 0x40000000)
    region-0 identity window maps BAR0 offset = CPU for CPU < 0x40000

---

## 0. TL;DR - answers to the four questions

1. **The chain** (all addresses CPU; file = CPU-0x40000; boot task 0x47628). The state check
   0x47D0A (byte CA 0x118B60 <= 1) enters the setup branch **0x48160**, which advances the state
   byte at **0x48174** and then unconditionally runs the id-1 message path **0x47D28**
   (0x102822 alloc + 0x102760 send; success = return 0 -> **0x49188**). 0x49188 builds a second
   frame from CA 0x1043A4 and takes the stage gate **0x491F0**: [CA 0x17D420] and
   [CA 0x17D424] must both be non-zero (otherwise a deliberate null write + udf at 0x491FE),
   and [CA 0x17D424] != 0 selects **0x48292** (post a D2H frame whose first scatter word is
   CA 0x17D420). Control then calls **0xD1B68** and only if that returns 0 enters **0x49204**,
   the post-send init chain: arena CA 0x170E28 is zeroed (0x700 B), a token 1 is pushed into
   the queue at CA 0x171448, a 19-entry pointer array is written (CA 0x171458 + 4*i =
   0x119758 + 0xB40*i, i = 0..18), and CA 0x169E90 is initialised. Then **0x48336** (HCC
   handler-table registration via 0x102866), the record count byte CA 0x180810 = 1, and the
   per-record body **0x4845C ...** which pops the token (0x48552), checks the record table
   CA 0x174BE0 (0x4847A -> 0xB1D90), and reaches **0x4881E / 0x48820 / 0x48840**.
2. **The gate.** At **0x48820** the test is r0 = 0x9438C(byte[CA 0x170E29]), then beq 0x48F68
   if r0 == 0. 0x9438C is a singleton accessor: it returns CA **0x169E90** iff its argument is
   0, and logs an assert + returns 0 otherwise. CA 0x170E29 is written by **0x485E4** from
   r4 = return of 0xB1D90, and the only path that reaches 0x4881E has 0xB1D90 return 0, so
   byte[0x170E29] == 0 and **the 0x48820 test passes structurally on the path that reaches it -
   it is an assertion tier, not a data-dependent gate.** The data-dependent decision is the
   immediately following lookup at **0x4882E**: r4 = 0x961AE(byte[CA 0x170E28]) =
   0xFE578(0, 0x12) = **word[CA 0x1714A0] + u16[CA 0x171504]**, and beq 0x48F68 if that is
   zero. CA 0x170E28 holds 0x12 (written at 0x487FA), so the lookup is the fixed module-table
   entry 18: word CA **0x1714A0** = 0x119758 + 18*0xB40 = **CA 0x001261D8**, plus the u16
   count/offset CA **0x171504** (0 on the normal path). Both words are written **only** by the
   boot task's own post-send init chain: the array store str r0,[r8,#4]! at **0x4924C** (file
   0x924C) and strh r6,[r4,#0x6DC] at **0x4923A** (file 0x923A). A whole-image census found no
   other writer (section 4). **Dependency: boot-task progress / message layer - NOT the event
   layer (CA 0x4016010C, handler table CA 0x17D430, 0xC2EFC handlers), NOT the D2H/ETE path,
   NOT a hardware event.** The only external precondition is that the boot task is (re-)entered
   by the firmware task/scheduler path outside the image.
3. **Read-only formula (paused chip, BAR0 = CPU + 0x6B8000 for these RAM objects):**

       gate_B_open = (word(BAR0 0x8294A0) + u16(BAR0 0x829504)) != 0
         OPEN   expected: word = 0x001261D8, u16 = 0x0000 -> 0x001261D8 != 0
         CLOSED expected: word = 0x00000000, u16 = 0x0000 -> boot task never ran 0x49204
       gate_A_arg  = byte(BAR0 0x828E29)   (CA 0x170E29; expected 0; nonzero = corruption)
       discriminators: CA 0x171458 (=0x00119758 if the init array loop started),
       CA 0x169E90 (=0x00170E28 if the per-record body ran), CA 0x169E98 (=0 always;
       no writer in the image, section 5), CA 0x17D420/0x17D424 (=0x0017D420 if entry A
       initialised the system object). Full table in section 6 / tools/gate_reads.py.
4. **Host action: no.** The gate words are RAM fields written by the boot task itself earlier
   in the same pass; no host register, H2D message, DMA or timing value feeds them. Even a
   completed pass does **not** obtain the per-link context: the bind value at 0x4883A
   (ldr r3,[sb,#8] with sb = CA 0x169E90) reads CA **0x169E98**, and a whole-image writer
   search shows that word is **never written** by any code path (the only initialisation is
   the zero-fill at 0x486A2/0x486A4); the bind therefore stores 0. The real per-link context
   family is the per-bank record returned by 0xB1EE4 (bank0 = CA 0x174BF0, bank1 = CA 0x175A18)
   and published at CA 0x15CA08+0x1A00 by CPU 0x48A6C, then bound by the 0x44BBC family - i.e.
   the already-identified event/link-layer path. The single next bench action is therefore the
   **read-only discriminator pack** in section 6: it decides "chain never reached 0x49204" vs
   "gate open / bind ran and wrote 0" vs "per-link object exists but was never bound".

---

## 1. Method, artifacts and tooling

Reused prior-lane knowledge (addresses/conventions only, not re-derived here):

* wifi-fw-link-event REPORT sections 4 and 6 (boot task 0x47628, chain addresses, event
  dispatcher 0xC2EFC, handler table CA 0x17D430);
* wifi-abort-rca REPORT sections 1-6 (data abort at 0x87D64 via 0x88AFC, LR 0x88BBD, +0x4B0 is
  the per-link sub-context pointer read at 0x745E4/0x746A0/0x846C2, +0x2CC is the descriptor
  block, exception-frame layout at CPU 0x40008);
* wifi-fw-ready-gate REPORT section 1 (state byte CA 0x118B60 is written only at file 0x8174;
  id-1 "Device plat ready" built at file 0x7D28);
* wifi-hcc-link REPORT section 5 (H2D lane negative, out[0] never consumed).

New tooling in this lane (host-only, reads the blob in place, never copies it):

* **tools/bootgate.py** - recursive-descent Thumb CFG walker with literal resolution
  (dis, func, block, trace, callers, xref, writers, calls, bytes, bar). It adds an
  indirect-call-aware block builder (blx rN returns, bx/ldr pc terminates) needed to decode
  the 0x47E00-0x48Fxx region, a whole-image BL-caller census, a PC-relative literal census,
  and a straight-line store taint used for the writer searches.
* **tools/gate_reads.py** - the read-only paused-chip plan: prints every BAR0 read, the
  expected open/closed value and the decision matrix. It never opens a device.

Everything below is reproducible with the commands in section 8.

---

## 2. The chain, reconstructed (CPU addresses; file = CPU - 0x40000)

The boot task 0x47628 is a re-entered state machine (prior lanes [V*]). Only the post-send
continuation is repeated here; the object roles come from this lane's CFG walk.

### 2.1 0x47D0A -> 0x48160 - setup branch and state advance

    0x47D0A  lock CA 0x118B24 (0xBDF8E / 0xBDF50)
    0x47D10  r6 = byte[CA 0x118B60]                 ; handshake state
    0x47D16  cmp r6,#1 ; bls 0x48160                ; state 0/1 -> setup branch
    0x48160  r5 = CA 0x118B64 (per-state record base); idx = r6*0x84
    0x4816A  r3 = r6+1
    0x48174  byte[CA 0x118B60] = r3                 ; <-- ONLY writer of the state byte
    0x4817C  list/thread init on CA 0x118B64+.., 0xC71B8(0x118B9C)
    0x481A0..0x481C0  builds a task descriptor at sp+0x54 and calls 0xC745C (task register)
    0x481C8..0x4828A  writes boot-global thread/queue objects CA 0x117C00+0x214..+0x250,
                      +0x270..+0x27C, registers event handler 0x46 (0xC6DA2 at 0x4821A),
                      and 0xBC1DC(1, 0x40511)
    0x4828A  b 0x47D28

So whenever the state byte is 0 or 1 the setup branch runs and **continues into the id-1 send
in the same pass**. Our measured state byte is 1 [V*], so the boot task executed this branch
at least once; whether the same pass continued through the sends is the open question the
read pack decides (the sends are the next gates).

### 2.2 0x47D28 - id-1 message (group 4, id 1) build and send

    0x47D28  msg = 0x102822(0, 6)                   ; msg-layer allocation; 0 = fail -> 0x4782E
    0x47D38  buf = 0x409EA(msg)                     ; msg buffer
    0x47D3C  buf[0] = 4 ; buf[6] = 1                ; group 4 / message id 1 = "Device plat ready"
    0x47D52  r0 = 0x102760(msg)                     ; msg-layer send
    0x47D58  cmp r0,#0 ; beq 0x49188                ; 0 = success -> continue
    0x47D5E  0x102820(msg) ; 0x4782E                ; failure -> pass exit

0x102760 resolves the destination through the registry CA 0x171B6C: it computes
0x171B6C + 0x448 + 0x18*ubfx(buf[1],4,4) (per-core HCC instance resource record), calls
0x102744(record, buf[0]&0xF) for the group entry and compares u16[buf+6]. The registry
CA 0x171B6C is populated in our measured state [V*], so this gate is not obviously the
blocker - but it is a message-layer gate, not an event-layer one.

### 2.3 0x49188 - second frame and the system-object stage gate 0x491F0

    0x49188  copies 0x2C bytes from CA 0x1043A4 into a stack frame (sp+0x40) and
             memset(sb, 0, 0x2C)
    0x491AE  msg2 = 0x102822(0x3D, 6) ; 0 = fail -> exit
    0x491BE  buf2 = 0x409EA(msg2) ; 0 = fail -> 0x47D5E
    0x491EC  frame word 0x00040008 at sp+0x54
    0x491F0  r3 = CA 0x17D3D8                       ; system object + 0x40
    0x491F2  if word[CA 0x17D420] == 0 -> 0x491FE: store to address 0 + udf (deliberate BUG)
    0x491F6  if word[CA 0x17D424] != 0 -> 0x48292   ; stage-gate pass
    0x48292  builds the message-side parameter/scatter list: first word CA 0x17D420,
             then {0x8,0x14,0x20,0xA0,0xB0,0xC0,0x33,0xD0,0xC8}, copies 0x2C bytes from sb
             with 0xC1E44 (checked memcpy), and 0x102760(msg2) send; failure -> 0x47D5E
    0x482F4  11-iteration wait loop on CA 0x18179C / [obj+0x50]
    0x4830E  q = 0x40D60() ; 0x48314 r = 0xD1B68() ; 0x4831C if r == 0 -> 0x49204
             (r != 0 -> 0x40874(0x87) and pass exit 0x4916A)

CA 0x17D3D8 is the lock-word/list area of the system object CA 0x17D398. Entry A initialises
it at file 0x72FC-0x7306 (CPU 0x472FC-0x47306): add r3,r6,#0x48 ; strd r3,r3,[r6,#0x48]
writes CA 0x17D420 = 0x17D420 and CA 0x17D424 = 0x17D420 (self-referential "initialised"
pair), and the handler table base CA 0x17D430 = system+0x98 follows it. A census of every
literal load of CA 0x17D3D8 (11 sites) and of the lock helpers 0xC2394/0xC2730 found no other
writer of +0x48/+0x4C [V]. So **stage gate C4 passes iff entry A executed its system-object
init block 0x472DC..0x47320**, which is what the read CA 0x17D420 on a parked chip decides.

### 2.4 0x49204 - the post-send init chain that writes the gate state

    0x49204  0xC1E0C(CA 0x170E28, 0x700, 0)              ; arena zero
    0x49214  [CA 0x170E28+0x624] = 0x170E28+0x628 ; [CA 0x170E28+0x620] = 0x100
    0x49226  queue at CA 0x171448: 0xFE5EA(queue, 1)     ; push token 1 (consumed at 0x48552)
    0x49232  r8 = CA 0x171454 ; r6 = 0 (d1b68 returned 0)
    0x49236  byte[CA 0x170E28+0x62C] = 0
    0x4923A  u16[CA 0x170E28+0x6DC] = 0                  ; <-- gate_B word 2 (CA 0x171504)
    0x49242  loop i = 0..18:
               r0 = 0x119758 + 0xB40*i
               str r0,[r8,#4]!                            ; CA 0x171458+4*i = r0
                                                          ; i=18 => CA 0x1714A0 = 0x1261D8
                                                          ; <-- gate_B word 1
               0xC1E0C(r0, 0xB40, 0)                      ; zero each 0xB40 module block
    0x49266  memset(CA 0x169E90, 0x24, 0) ; queue at CA 0x169EA4, [0x169EA8]=0x169EAC,
             [0x169EA4]=0x100, push 1, [CA 0x169E90+0x20]=0
    0x4928A  writes arena fields +0x6FC/+0x6F4/+0x6F0/+0x6F8/+0x6EC/+0x6E8,
             memset(CA 0x118C6C, 0x14, 0)
    0x492C6  loop idx 0..0x91: 0xC1FF4(CA 0x171510, idx) ; r0 != 0 -> continue
    0x4832C  loop counter r4++ ; r4 != 0x92 -> 0x492CA ; else fall through
    0x48336  registration into the HCC instance: 0x102866(2, CA 0x10CC18, 0x13)
             and 0x102866(0, CA 0x10CCFC, 0x28) ; memset(CA 0x1ADB80, 0x118, 0)
    0x48358  [sp+0x14] = 0 ; r6 = byte[CA 0x180810] (record count; =1 set by prelude 0x477F2)
    0x48366  if 0 < count -> per-record body 0x4845C ; else end-of-loop 0x48368

Note the ordering: the gate-state words (0x4923A, 0x4924C) are written **before** 0x48336 and
therefore before the per-record body ever reaches 0x48820. The gate cannot be closed by
anything later than 0x49204.

### 2.5 0x4845C - per-record body up to the bind

    0x4845C  record = 0x1ADB80 + 0x110*idx ; 0xB1D90(idx, record+8)
             returns 0 only if byte[CA 0x174BE0 + 0x1C60*idx + 9] == 1
             (the record slot prepared by 0xD1B68); non-zero -> 0x48484 error, pass exit
    0x48520  r6 == 0 (first record) ; else log and continue to 0x48552
    0x4852E  0xC1E0C(CA 0x15CA08, 0x36D0, r6)          ; zero the per-link object bank 0
             [CA 0x15CA08+0xD68] = idx ; [CA 0x15CA08+0xD6C] = idx
             [CA 0x15F2D8] = idx ; u16[CA 0x15F2D8+4] = 0x100
    0x48552  token = 0xFE5C0(CA 0x171448)              ; pop the token pushed at 0x4922E
             != 0 else error ; slot = token-1 ; [sp+0x20] = slot
             if slot != 0 -> log/error 0x485A0 (only slot 0 takes the full path)
    0x485BA  byte[CA 0x169E90+0x20]++ ; memset(CA 0x170E28, 0x620, 0)
             copy 3 bytes sp+0x54 -> CA 0x1ADC98 ; init CA 0x1ADC98 fields ; 0x1017F8()
    0x48646  0x9438C(0) -> fp = CA 0x169E90        ; singleton accessor (gate A's function)
    0x4869C  memset(CA 0x169E90, 0x14, slot=0) ; [CA 0x169E90] = CA 0x170E28
             [CA 0x169E98] untouched -> stays 0 (this is the bind value word)
    0x486B6  0xB1EE4(record idx, 0)                    ; per-bank record getter
             returns non-zero -> separate branch 0x486C2/0x48932 (not the normal path)
    0x486CC  0xA4FC8(byte[CA 0x170E2A], 0) -> r4 = CA 0x15CA08 (per-link object bank 0)
             ... init of that object (offsets 0x3E9/0x440/0x4F0../0x533/0xAB8, 0xFE6BC,
             loop over CA 0x10ADB8...) and of CA 0x15E570 for bank 1
    0x487B6  memset(CA 0x1471CC, 0xA80, 0) ; memset(sp+0x54, 0x18, 0) ; 0xBC726(0x12)
             byte[CA 0x170E28] = 0x12                ; <-- lookup index for gate B
             byte[CA 0x170E28+1] = 0                 ; <-- gate A argument (r4 == 0)
             byte[CA 0x170E28+2] = record idx (0)
             byte[CA 0x170E28+3] = 0
    0x4881E  ldrb r0,[r5,#1]  (r5 = CA 0x170E28) ; 0x48820 bl 0x9438C
             cmp r0,#0 ; beq 0x48F68                 ; GATE A
    0x4882C  ldrb r0,[r5] (=0x12) ; 0x4882E bl 0x961AE
             cmp r0,#0 ; beq 0x48F68                 ; GATE B
    0x4883A  r3 = word[CA 0x169E98] (=0) ; r1 = 0
    0x48840  word[r4+0x4B0] = r3                    ; <-- the bind (r4 = gate B result)
    0x48844  0xA4FC8(byte[CA 0x170E2A], 0) -> word[r4+0x4B8] = CA 0x15CA08
             word[r4+0x4AC] = 0 ; 0xFE6BC(r4, r5, byte[r5], sp+0x54)
    0x48866  r0==0x64 -> clear flag else set byte[r4+0x533] bit0
    0x48878  byte[r4+4] != 2 -> 0x48F38 (error/teardown path) ; else continue
             (then +0xAB8=4, 0x1A58 memset at CA 0x17793C, 0x42C28/0xBC1DC, and the
             record-loop continuation 0x488A8/0x488B2 -> 0x4835A with idx+1)

The two gate functions:

    0x9438C(arg):  if arg == 0 -> return CA 0x169E90 (boot-global singleton)
                   else log(0x107A0D57, 0x63, arg) ; return 0
    0x961AE(arg):  r1 = arg ; r0 = 0 ; tail 0xFE578(0, arg)   (table id 0)
    0xFE578(tbl, idx):
                   r6 = 0xFE558(tbl)  ; tbl==0 -> CA 0x1ADB80 (else assert, 0)
                   base = 0x943AC(tbl) ; tbl==0 -> CA 0x170E28 (else assert, 0)
                   if idx > 0x12 -> log/return 0
                   return word[base + (0x18C+idx)*4] + u16[base+0x6DC]
                   -> for idx=0x12: word[CA 0x1714A0] + u16[CA 0x171504]

---

## 3. The gate, exactly

### 3.1 The literal 0x48820 test (assertion tier)

| element | value | evidence |
| --- | --- | --- |
| instruction | 0x48820 bl 0x9438C ; 0x48826 cmp r0,#0 ; 0x48828 beq.w 0x48F68 | [V] |
| argument | r0 = byte[CA 0x170E29] | [V] |
| writer of the argument | 0x485E4 strb r4,[r5,#1] (r5 = CA 0x170E28), r4 = return of 0xB1D90 | [V] |
| value on any path that reaches 0x4881E | 0 (0x48480 requires 0xB1D90 == 0 -> r4 == 0; no later write to CA 0x170E29 before the gate) | [V] |
| function semantics | returns CA 0x169E90 iff arg == 0, else logs an assert and returns 0 | [V] |
| consequence | gate A is a structurally-passing singleton assertion; it cannot be the reason a completed pass skips the bind | [V] |

### 3.2 The actual decision - 0x4882E lookup (gate B)

| element | value | evidence |
| --- | --- | --- |
| instruction | 0x4882E bl 0x961AE ; 0x48834 cmp r0,#0 ; 0x48836 beq.w 0x48F68 | [V] |
| argument | r0 = byte[CA 0x170E28] = 0x12 (written 0x487FA) | [V] |
| lookup | 0xFE578(0, 0x12) = word[CA 0x1714A0] + u16[CA 0x171504] | [V] |
| open state | word[CA 0x1714A0] = 0x001261D8 (module block 18 = 0x119758+0xCA80), u16 CA 0x171504 = 0 | [V] |
| closed state | word[CA 0x1714A0] = 0 and u16 CA 0x171504 = 0 (arena never initialised by 0x49204; it is BSS zero and 0x49210 explicitly zeroes it) | [V]/[I] |
| writers | 0x4924C str r0,[r8,#4]! (array store; r8 = CA 0x171454, so CA 0x171458+4*i) and 0x4923A strh r6,[r4,#0x6DC] -> CA 0x171504. No other writer anywhere in the image (section 4) | [V] |
| if closed | r0 = 0 -> 0x48F68 -> 0x48578 (log) -> 0x485B4 -> 0x48668 (error id 0x8B2D) -> 0x4848A -> 0x492F6 -> pass exit. The bind at 0x48840 is skipped | [V] |

### 3.3 Earlier conditional branches on the path (in order)

| # | site | condition | writer of the state | dependency class |
| ---: | --- | --- | --- | --- |
| C1 | 0x47D16 | byte[CA 0x118B60] <= 1 | 0x48174 only (file 0x8174) [V] | boot-task state byte; measured = 1 [V*] |
| C2 | 0x47D4C / 0x47D5A | 0x102822 alloc ok and 0x102760 send returns 0 | message layer; destination registry CA 0x171B6C (+0x448 per-core record) - populated [V*] | message layer |
| C3 | 0x491B0 / 0x491E0 | 0x102822(0x3D,6) ok and 0x409EA ok | message layer | message layer |
| C4 | 0x491F2 / 0x491F6 | word[CA 0x17D420] != 0 (else BUG) and word[CA 0x17D424] != 0 | entry A block 0x472FC-0x47306 writes both = 0x17D420 (self-referential pair); no other writer found [V] | image init (entry A) |
| C5 | 0x4831C | 0xD1B68() == 0 | 0xD1B68 initialises CA 0x174BE0 (0x1C68 B) and returns 0; its own early check is byte[CA 0x180810] <= 1 (the record count written by the boot prelude at 0x477F2) | boot task / init layer |
| C6 | 0x4847A | 0xB1D90(idx, record+8) == 0 -> requires byte[CA 0x174BE0+0x1C60*idx+9] == 1 | written by 0xD1B68 (same pass, C5) | boot task / init layer |
| C7 | 0x48552-0x4859E | pop(CA 0x171448) == 1 so slot == 0 | token pushed at 0x4922E in the SAME init chain | boot task |
| C8 | 0x48820 | gate A (assertion tier) | 0x485E4 (structurally 0) | boot task |
| C9 | 0x4882E | gate B (module-table entry 18 != 0) | 0x4924C / 0x4923A (same pass) | boot task / message-layer chain |

**Classification.** None of C1-C9 is fed by the event dispatcher 0xC2EFC, the device event
status CA 0x4016010C, the handler table CA 0x17D430, the D2H/ETE rings, or any chip register.
Every state word is a RAM object written by the image's own boot-task/entry-A/message code.
The only thing outside the image is the *scheduling* of the boot task itself (task framework
0xC745C / reset path, prior lanes [V]/[U]) - which is why the observable (state byte = 1)
proves at least one pass started but not that the pass got past C2/C4/C5.

---

## 4. Writer census (whole image)

All searches were done over the flat image, reading it in place:

* **CA 0x1714A0 / CA 0x171504 (gate B state).** xref of the arena base CA 0x170E28 finds 11
  PC-relative literal-load sites (0x4858C, 0x49208, 0x4C8FC, 0x8D1A2, 0x93ACC, 0x943BE,
  0x945B4, 0xBCD14, 0xD18E4, 0xFD9FA, 0xFE64E). Only the 0x49204 chain stores into the
  +0x630..+0x6DC window: 0x4924C (array store, str [r8,#4]!) and 0x4923A (u16 at +0x6DC)
  [V]. The array is not addressed by any literal (base+offset only); a direct store census of
  the +0x678/+0x6DC forms finds only these two sites [V].
* **CA 0x17D420 / 0x17D424 (stage gate C4).** xref of CA 0x17D3D8 finds 11 load sites; the
  only stores to the +0x48/+0x4C pair are entry A's strd r3,r3,[r6,#0x48] at CPU 0x47306
  [V]. The dispatcher lock helpers 0xC2394 / 0xC2730 touch only per-CPU stack words, not the
  object (+0x48/+0x4C) [V].
* **CA 0x169E98 (bind source).** xref of CA 0x169E90 finds 5 sites (0x485BA, 0x49268,
  0x9439E, 0x95F40, plus one data-table word at 0x10533C). Straight-line taint from every
  code site reaches only CA 0x169E90+0x14/+0x18/+0x20; the +0x8 word (CA 0x169E98) is written
  **nowhere** in the image [V]. The per-record body does initialise CA 0x169E90 with a
  0x14-byte zero fill at 0x486A2/0x486A4 and stores CA 0x170E28 at CA 0x169E90 (0x486A8), so
  CA 0x169E98 = 0 when the bind executes [V].
* **CA 0x118B60 (state byte).** Confirmed only writer is 0x48174 (file 0x8174), cited [V*]
  and re-checked by the block dump in section 2.1 [V].


---

## 5. The bind value: why the per-link context is not what 0x48840 writes

The bind instruction is str.w r3,[r0,#0x4B0] with r3 = ldr.w r3,[sb,#8] and sb = CA 0x169E90
(gate A's return). The whole-image writer census above shows CA 0x169E98 is never written by
any code path, so on every execution of this instruction r3 == 0: the bind writes NULL into
the module-table object's +0x4B0. That is exactly what our measured state shows ("per-link /
registry +0x4B0 words zero" [V*]) - and it means the observed zero is **not** evidence that
the boot task failed to reach 0x48820; it is also the value written by a fully completed pass.

Where the data-path per-link context really comes from (prior-chain facts re-linked here [V]):

* the per-link/per-bank context objects are the records returned by **0xB1EE4(bank)**: base
  CA 0x174BE0, checked state byte +0x9 == 1, then bank0 context CA **0x174BF0** (requires
  byte[0x174BE0+0x1D2] == 1), bank1 context CA **0x175A18** = 0x174BE0+0xE28+0x10 (requires
  byte[0x175A08+0x1D2] == 1);
* the boot task's per-record body publishes such a context at **CA 0x15CA08+0x1A00**
  (store at CPU 0x48A6C, reached only when 0xB1EE4 at 0x48710 returns non-zero);
* the 0x44BBC bind family (function containing 0x449F0-0x44AEA) then does
  word[module+0x4B0] = word[A4FC8(...)+0x1A00] and word[module+0x4B8] = A4FC8(...) - i.e. the
  context that the S10/DR data path reads at 0x745E4/0x846C2 is the per-bank record context,
  and its creation depends on the record bank being live (0xB1EE4 non-zero), not on a single
  completed boot-task pass.

This reconciles the two lanes: 0x48840 is the module-table bind (empty by construction in this
path), 0x44BBC is the per-link bind that feeds +0x4B0 of the object the abort chain reads; the
gate for the latter is word[CA 0x15CA08+0x1A00] != 0, i.e. a live per-bank record - the
event/link-layer condition already identified by wifi-fw-link-event.

---

## 6. Read-only gate formula for a parked chip

Conventions: BAR0 = CPU + 0x6B8000 for all of these CPU-RAM objects (prior lanes [V*]).
Nothing below is a write; this is exactly what tools/gate_reads.py prints.

| # | read | CA | BAR0 | expected (open) | expected (closed) | decides |
| ---: | --- | ---: | ---: | --- | --- | --- |
| 1 | gate B word 1 | 0x1714A0 | 0x8294A0 | 0x001261D8 | 0x00000000 | did the boot task run the 0x49204 init chain? |
| 2 | gate B word 2 (u16) | 0x171504 | 0x829504 | 0x0000 | 0x0000 | count/offset term; nonzero = partial/corrupt init |
| 3 | array entry 0 | 0x171458 | 0x829458 | 0x00119758 | 0x00000000 | did the array loop start at all |
| 4 | gate A argument | 0x170E29 | 0x828E29 | 0x00 | 0x00 | 0x9438C argument (structurally 0); nonzero = corruption |
| 5 | singleton word 0 | 0x169E90 | 0x821E90 | 0x00170E28 | 0x00000000 | did the per-record body reach 0x486A8 |
| 6 | bind source word | 0x169E98 | 0x821E98 | 0x00000000 | 0x00000000 | always 0; proves the 0x48840 bind writes NULL |
| 7 | stage-gate pair | 0x17D420, 0x17D424 | 0x835420, 0x835424 | 0x0017D420, 0x0017D420 | 0x00000000 (would have hit the 0x491FE BUG) | entry A system-object init ran |
| 8 | module-table entry 18 + 4 | 0x1261DC | 0x7DE1DC | 0x00000002 | n/a | 0xFE6BC ran (bind/present path) |
| 9 | module-table entry 18 + 0x4B0 | 0x126688 | 0x7DE688 | 0x00000000 | n/a | bind executed; expected NULL by row 6 |
| 10 | module-table entry 18 + 0x4B8 | 0x126690 | 0x7DE690 | 0x0015CA08 | n/a | per-link object bank 0 attached |
| 11 | per-link publish word | 0x15E408 (=0x15CA08+0x1A00) | 0x816408 | 0x00174BF0 (bank0) or 0x00175A18 (bank1) | 0x00000000 | the *real* per-link context exists (event/link-layer condition) |
| 12 | state byte | 0x118B60 | 0x7D0B60 | 1 (measured [V*]) | - | boot task entered the setup branch at least once |
| 13 | system handler table sample | 0x17D430+4*2 | 0x835438 | 0x000C20F9 (prior read [V*]) | - | entry A registration block ran |

### Gate formula and decision matrix

    gate_B_open = (word(BAR0 0x8294A0) + u16(BAR0 0x829504)) != 0
    gate_A_open = (byte(BAR0 0x828E29) == 0)          # structural; always true on-path

* **(O) gate open, row 9 == 0, row 11 == 0:** the boot task DID run the post-send chain and
  the bind; the module +0x4B0 is NULL because CA 0x169E98 is never written; the per-link
  context was never published -> the remaining blocker is the event/link layer (0x44BBC /
  0xB1EE4), matching wifi-fw-link-event. The abort is NOT a boot-task scheduling failure.
* **(C) gate closed (row 1 == 0):** the boot task never executed 0x49204 in our reachable
  state -> the pass stopped at C2 (message alloc/send), C4 (0x17D420 pair) or C5 (0xD1B68).
  Row 7 then localises: pair == 0x0017D420 means entry A is fine, so the stop is C2/C5 and the
  D2H/message-layer lane (abort-rca section 1) is the active blocker.
* **(P) gate open, row 11 != 0, row 9 == 0:** the per-link bank exists but the 0x44BBC bind
  never ran -> the remaining blocker is the H2D/link-event handler that owns 0x437B0-0x44BBC
  (group-2 id-9 per wifi-hcc-link), i.e. strictly the event/message layer.

No row is writable from the host: rows 1-11 are RAM objects written by firmware code, row 12
by the boot task, row 13 by entry A. Writing any of them would be fabricating firmware state
(the abort-rca section 10.4 "no targeted write" stand remains in force).

---

## 7. Can a host action satisfy the gate? - No; one read-only bench action remains

**No host write, H2D message, DMA or timing value can open the gate.** The decisive facts:
[V] both gate-B words are written only inside the boot task's own 0x49204 block, after the
message-layer sends at 0x47D28/0x49188; [V] they are firmware RAM at CA 0x1714A0/0x171504 and
are not part of any host-visible register window; [V] the H2D consumer that could run the boot
task's continuation is dead in our state (event 0x4C never dispatched, out[0] stuck,
wifi-hcc-link [V*]); [V] even the bind's value word CA 0x169E98 is never written, so a
successful pass still stores NULL. A host action can at most keep the D2H/announce service
alive (the C1 posture) so that the firmware's own task flow is not parked - that is a
precondition for C2/C5, not a gate write, and it is the already-known, already-tried path.

**Single next bench action (READ-ONLY, attended, no writes):** on one fresh cold boot after
the standard prepare/upload/verify/release, take the pack in section 6 via the existing
observability path (or a read-only BAR0 read). Expected observables and the decision are the
three cases (O)/(C)/(P) above. If the observability attribute is available, add rows
1,2,3,5,6,7,9,10,11 to it (9 words); otherwise take them in the same parked window used for
the prior read packs.

---

## 8. Evidence table (new claims of this lane)

| # | claim | artifact / method | grade |
| ---: | --- | --- | --- |
| E1 | setup branch 0x48160 stores state = r6+1 at 0x48174 (file 0x8174) and then runs the id-1 send path 0x47D28 in the same pass | bootgate.py block/trace 0x47D0A-0x48292 | [V] |
| E2 | 0x47D28 allocates with 0x102822(0,6), fills group 4 / id 1, sends with 0x102760; return 0 continues to 0x49188 | dis 0x47D28-0x47D64; dis 0x102760/0x102822 | [V] |
| E3 | 0x49188 copies 0x2C bytes from CA 0x1043A4, allocates with 0x102822(0x3D,6), and gates at 0x491F0 on word[CA 0x17D420] != 0 and word[CA 0x17D424] != 0 (else null-write BUG at 0x491FE) | dis 0x49188-0x49200 | [V] |
| E4 | CA 0x17D420/0x17D424 are initialised to 0x17D420 by entry A at 0x472FC-0x47306; no other writer in the image (11 xref sites + lock helpers checked) | block 0x472DC-0x47320; xref/writers 0x17D3D8 | [V] |
| E5 | 0x4831C requires 0xD1B68() == 0 before 0x49204; 0xD1B68 initialises CA 0x174BE0 and reads the boot-prelude record count byte CA 0x180810 | dis 0x4830E-0x48328, 0xD1B68 | [V] |
| E6 | 0x49204 chain: 0x700-B memset of CA 0x170E28, token push at 0x4922E, u16 gate word at 0x4923A, 19-entry pointer array at 0x49242-0x49264 (entry 18 -> CA 0x1714A0 = 0x1261D8) | dis 0x49204-0x492DA; xref/writers 0x170E28 | [V] |
| E7 | per-record body requires 0xB1D90 == 0 (record slot CA 0x174BE0+0x1C60*idx+9 == 1), pops token 1 from queue CA 0x171448 (pushed at 0x4922E), and requires slot == 0 | dis 0x4845C-0x485B8, 0xB1D90 | [V] |
| E8 | 0x48820 tests 0x9438C(byte[CA 0x170E29]); 0x9438C returns CA 0x169E90 iff arg == 0; byte CA 0x170E29 is written 0 at 0x485E4 from the 0xB1D90 success code, so gate A passes structurally | dis 0x4881E-0x48840, 0x9438C | [V] |
| E9 | the actual decision is 0x4882E = 0x961AE(0x12) = 0xFE578(0,0x12) = word[CA 0x1714A0] + u16[CA 0x171504]; both written only by 0x4924C / 0x4923A | dis 0x961AE, 0xFE578, 0xFE558, 0x943AC; writer census | [V] |
| E10 | bind value word CA 0x169E98 has NO writer in the image; the per-record body zero-fills CA 0x169E90..+0x14 at 0x486A4, so the 0x48840 bind stores NULL even on a completed pass | xref/writers 0x169E90; dis 0x4869C-0x486C0 | [V] |
| E11 | the real per-link/bank context objects are 0xB1EE4's CA 0x174BF0 (bank0) / CA 0x175A18 (bank1); the boot task publishes one at CA 0x15CA08+0x1A00 (store 0x48A6C), and 0x44BBC binds it into module+0x4B0 | dis 0xB1EE4, 0x44B44-0x44BBC, 0x489AC-0x48A70 | [V]/[I] |
| E12 | no gate input is fed by 0xC2EFC, CA 0x4016010C, CA 0x17D430, the D2H/ETE path or a chip register; all are image RAM written by entry A / the boot task / the message layer | writer census + classified branch table section 3.3 | [V]/[I] |
| E13 | no host action can satisfy the gate; only the read-only pack (section 6) remains, with the three-way decision (O)/(C)/(P) | synthesis of E1-E12 + cited prior negatives (wifi-hcc-link [V*], fw-link-event [V]) | [I] |

## 9. Corrections / refinements to prior lanes

1. **"The gate at 0x48820 decides the bind"** - exact wording should be: the 0x48820 test is
   an assertion on the singleton accessor 0x9438C (always passes on the path); the
   data-dependent gate is the next instruction pair 0x4882E/0x48836 (0xFE578 lookup of arena
   array entry 18). Same block, two different tiers.
2. **"Per-link +0x4B0 is never established because the boot chain does not run"** - the chain
   cannot establish it either way: the 0x48840 store writes CA 0x169E98, which no image code
   writes (value 0). The per-link family the abort path reads is created by 0xB1EE4 and
   published at CA 0x15CA08+0x1A00; 0x44BBC binds that. The lane's read pack (section 6)
   distinguishes "boot task never got there" from "got there, wrote NULL".
3. **0x491F0 is not a boot-progress state gate** - it is an assertion that the system object
   CA 0x17D398 was initialised by entry A (self-referential list-head pair); a zero there is a
   deliberate null-write BUG path, not a normal failure.
4. The chain order is 0x47D28 (id-1 send) -> 0x49188 (second frame + C4) -> 0xD1B68 ->
   0x49204 (arena/gate state) -> 0x48336 -> per-record body -> 0x48820; the gate state is
   written *before* the per-record loop, so a closed gate means "the pass stopped earlier
   upstream", never "the record loop failed later".

## 10. Open items ([U])

1. Who enters/re-enters the boot task in our transport state (0xC745C task framework vs ROM
   reset path) - outside the image (prior lanes [U]). The state byte = 1 [V*] proves one
   entry; the read pack tells whether that entry passed the sends.
2. Exact writer of byte[CA 0x17FCA8+0xB68] (read by 0xD1B68's early check) and of the record
   count byte CA 0x180810 after the prelude sets it to 1 - both are same-pass init-layer
   state, not needed for the three-way decision.
3. Whether the 0x44BBC caller (function 0x437B0 family, reached from H2D group-2 id-9 per
   wifi-hcc-link) or the 0x449F0-family init path is first in vendor order - needs the
   event-layer input that our transport lacks.

---

*Lane: boot-gate, 2026-11-23. Host-only, image-only. Complements wifi-fw-link-event (boot task
chain and event dispatcher), wifi-abort-rca (per-link context +0x4B0 / DR descriptor abort),
wifi-fw-ready-gate (state byte CA 0x118B60), wifi-boot-kick (live state) and wifi-hcc-link
(H2D negative). Vendor blob and device values remain local evidence only; no vendor bytes or
firmware data are committed.*

