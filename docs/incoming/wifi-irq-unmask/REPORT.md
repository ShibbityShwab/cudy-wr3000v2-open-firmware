# IRQ-unmask transition and the H2D mailbox delivery gap (Hi5622V100 firmware)

**Lane:** `wifi-irq-unmask` (shared task **task-90**), **host-only static RE**, 2026-11-24.
The bench router was never contacted; no image was built; no device was opened. The vendor
firmware blob and the mask-ROM dump remain **local evidence only** (paths in section 1) and
are **not copied into this directory**; every byte-level value below is a derived annotation
(address, constant, instruction encoding) needed to specify the analysis and the patches.

Inputs (local, read in place):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B vendor Wi-Fi firmware, sha256 `7fc87e20…311b`; **CPU = file + 0x40000** |
| `/home/shibbityshwab/staging-fi/rom16k.bin` | 16 KiB mask ROM, sha256 `fcfd464f…8faa4`, ROM CPU == dump offset |
| prior lanes | `wifi-fw-postboot`, `wifi-fw-link-event`, `wifi-event-router`, `wifi-rom-dump`, `wifi-rom-s01`, `wifi-rom-flow`, `wifi-s1-cpu1`, `wifi-hcc-service`, `wifi-d2h-direct`, `wifi-ete-frames` |
| ledger | `mem-entries.md` 2026-11-24 14:00 (ready handshake) and 15:10 (first H2D frame) |

Evidence markers: **[V]** verified in this lane from the local artifacts with
`tools/irq_map.py`; **[V\*]** device-measured earlier and cited; **[I]** inference from code;
**[U]** not decidable without a device read or a vendor trace. Negative results are full-image
censuses, not grep misses.

---

## 0. TL;DR — the answers

1. **The missing transition is NOT the CPU IRQ mask, and the image is not missing a
   `cpsie i`.** The image runs `cpsie i` **before every task function starts**: the initial
   task context is built with CPSR `0xB3` (SVC | I | …; IRQ masked) at CPU 0x0C72F8, the
   scheduler's first-dispatch tail (0xC70F8 -> 0x103B60 -> 0x102C04) restores it, and the
   PC it restores is the task-entry trampoline **0xC2B88**, which calls **0xC2B7A**
   (`cpsie i` at **CPU 0xC2B80**) before calling the task function [V]. The same
   `cpsie` is executed again on **every task resume and exit path** of the scheduler switch
   0xC24DE: its normal exit (0xC2694) tail-branches to 0xC2B7A, and a task resumed from a
   context switch returns from 0x102C14 into 0xC2658 -> 0xC2694 -> 0xC2B7A [V]. So on the
   golden boot the boot task starts with **I = 0**; the "inherited CPSR I" / "no reachable
   `cpsie`" hypothesis is falsified.
2. **What raises the image IRQ for a host mailbox write: event 0x4C only.** The image's H2D
   mailbox notification is the device event controller event **0x4C**, registered by
   `pcie_msg_init` to handler wrapper **0x40294 -> 0xC18AC** (the H2D mailbox dispatcher) and
   enabled via `0xC6FF4(0x4C)` [V]. The image has **no other reader of out[0]** (CA
   0x40039010): only the 0xC18AC dispatcher reads/clears it [V]. ETE SR completion
   (events 0x2D/0x2E -> 0x462F9/0x4624D) and the scheduler timer (event 0x1D -> 0xC6C88) are
   different services and do not consume the mailbox. The host side of the raise is the
   vendor sequence **out[0] = pending bitmap, then out[2] |= 1** (CA 0x40039010, CA
   0x400392D4) — exactly what our H2D probe already does.
3. **Why no dispatch on our boot:** every *host-visible* prerequisite for event 0x4C is
   satisfied — CPU I = 0 (section 2), ROM IRQ vector = 0x40064 (section 3.1), controller
   global enable (0xC3024), per-event enable bit (0x40161108 bit 12 = 0x1000), priority
   (0x50), target (0x01), handler registration (proven by the D2H pipeline being live) [V].
   The remaining prerequisites are **not host-visible**: the device mailbox block must
   actually assert event 0x4C on the host's out[0]/out[2] write, and the 0x4016 event
   controller must actually deliver it to the CPU. Neither the image nor the readable ROM
   programs any standard (Cortex-A9) GIC [V negative]; the 0x4016 block is the only
   interrupt controller in the design and it lives **outside all six inbound PCIe regions**
   (the postboot-lane retraction) so the host cannot read, let alone write, its state.
   That is the exact blocked transition: **event assertion/vectoring for 0x4C**, downstream
   of the CPU mask.
4. **Host-only action: none.** No host-writable register can unmask the CPU or raise/route
   event 0x4C (0x4016xxxx is not host-visible; the old "write 0x40161188 via BAR0 0x519188"
   plan was retracted as a wrong-window alias). The only host→device notification path is
   the mailbox (out[0]/out[2]) plus the ETE rings; both are already exercised.
5. **Recommended patch (diagnostic, bypasses the uncertain event layer): PD.** Redirect the
   boot task's successful epilogue (CPU 0x4916A) into a permanent poll loop that calls the
   firmware's own H2D dispatcher wrapper 0x40294 (0xC18AC) at CPU 0x47610 (the
   handshake-failure path, not executed on a golden boot). Expected: after the host posts
   the SR announce, **out[0] bit3 clears, out[5] reads 1, out[2] reads 8** — the dispatcher
   ran. If it does not clear, the event layer is not the only gap and the hcc instance
   CA 0x10C190 fields must be checked. Section 5 gives the exact bytes, section 6 the
   ranking, and the `cpsie`-family patches PA/PB/PC as low-risk controls.

---

## 1. Conventions and method

* CPU address of image byte f: `CPU = f + 0x40000`; file offset of CPU address c: `c - 0x40000`
  (the two are used interchangeably and every site below is given in both).
* Host BAR0 windows (unchanged, from `wifi-transport-port/build/hi5622_pciet.h`):
  device IO region 3 = CA 0x40000000..0x4011FFFF only; RAM region 5 = CPU + 0x6B8000;
  identity ROM region 0 = BAR0 0x0..0x1BFFF. **CA 0x4016xxxx is past region 3** and cannot be
  read or written from the host with the vendor region table.
* `tools/irq_map.py` (this lane) is a capstone-based census/disassembler over the local blob
  and ROM dump; `tools/patch_builder.py` builds patched copies of the local blob into a
  non-repo output directory. Both refuse to write vendor bytes into this repository and
  both have a self-test. Reproduction commands are in section 8.

---

## 2. The post-boot continuation, and the exact unmask transition

### 2.1 The chain (all [V] unless marked)

    ROM S0 (CPU0): host 0x5A5A release -> CA 0x4000010C = 0xCEED -> blx image header +0x00 = entry A 0x46970
    ROM S1 (CPU1):  page +8 = 2, +4 |= 4 -> blx image header +0x04 = entry B 0xC742C

    entry A 0x46970 (system init; calls 0xC3024 event-controller init at 0x46EDA)
      0x473CC..0x473F0  handshake page CA 0x1BF000: {+8=0, +C=0xABCD}, dmb/isb, CA 0x40161F00 = 0x20000
      0x473F0..0x47618  poll [+8] == 2 (fail path = infinite poll with cache maintenance)
      0x473F8          page +0 |= 0x4000
      0x47410..0x47420 task descriptor {name 0x104243, fn 0x47625, kind 7}
      0x47424          bl 0xC745C   RTOS task create (boot task)
      0x47428          bl 0xC704C   RTOS scheduler start
      0x4742C          epilogue (reached only if 0xC704C returns via the no-task idle path)

    0xC745C: critical section, take a TCB off free list 0x17E3F0 (stride 0x238, stack
             base 0x1BC000 - n*0x1000), 0xC7218 init, 0xC2B58 make-ready; returns TCB.
            The TCB is the boot task: stack base 0x1BC000 / size 0x1000 (same 0x1BC000
            page the ROM SVC context uses), fn 0x47625, arg 0.
    0xC7218: builds the initial exception frame at the stack top (0x1BCFC0):
              [top-0x40] = CPSR  = 0xB3  (SVC, I=1, F=0, T=1)   <- 0xC72F4 ite mi;
                                                           0xC72F6 movmi r1,#0xB3
              [top-0x3C] = TCB (r0 argument to the trampoline)
              [top-0x08] = LR    = 0x0C2E1D  (task-exit path 0xC2E1C)
              [top-0x04] = PC    = 0x0C2B89  (task-entry trampoline 0xC2B88)
              TCB+8 = sp = top-0x40
    0xC704C: pre-arm gates (stack-page word +4 == 0; CPSR I == 1) -> arm timer CA 0x40160600 =
             0x0061A800 / +04 / +08=7 / +0C=1 -> 0xC6FF4(0x1D) -> cpsid i -> 0xC2382 -> lock
             0xC2354 -> 0xC2324 pick task -> dispatch: 0xC70EA r0 = TCB+8 -> 0xC70F8
             b.w 0x103B60 -> ARM b 0x102C04 -> (ldr sp,[r0]; pop {r4}=CPSR; msr spsr_fsxc,r4;
             ldm sp!,{r0-r12,lr,pc}^) -> PC 0xC2B88, CPSR 0xB3

    0xC2B88 (task-entry trampoline, runs on the new task's stack):
      0xC2BB8  bl 0xC2740         per-core scheduler bookkeeping
      0xC2BC4  list check; 0xC2BD0 bl 0xC2B7A   <-- THE UNMASK
      0xC2BD4  bl 0xC196C         preempt count --
      0xC2BD8  ldrd r3,r0,[TCB+0xC]; bx r3       -> boot task fn 0x47624(arg 0)

    0xC2B7A (idle/unmask helper):
      0xC2B7C  bl 0xC2342        per-core wake counter++, sev
      0xC2B80  cpsie i           <-- CPU I cleared (bytes 62 B6)
      0xC2B82  pop {r3,lr}; b 0xC26E0   reschedule-check (returns when [page]==0 or preempt != 0)

    0xC2B7A is ALSO the tail of the scheduler switch function 0xC24DE:
      0xC2694 mov r0,r4 ; pop {r4..sl,lr} ; 0xC269A b.w 0xC2B7A   (normal exit / same-task path)
      a task that was switched out returns from blx 0x102C14 at 0xC2658 -> falls through to
      0xC2682 -> 0xC2694 -> 0xC2B7A, i.e. it also executes cpsie i on every resume.

**Conclusion [V]:** every firmware thread/task context has IRQs enabled before its function
body executes. There is no "the image never reaches cpsie i" failure in the boot flow. The
mask-0 observation in the golden boot is consistent with this (mask records *exceptions*, not
IRQ unmask), and the parent observation "IRQ handler never ran" cannot be attributed to the
CPU I bit.

### 2.2 The complete `cpsie i` census (whole image, Thumb + ARM linear with skipdata)

| CPU | bytes | site | role / reachability |
| --- | --- | --- | --- |
| 0xC2B80 | 62 B6 | `cpsie i` | 0xC2B7A idle/unmask helper: called from the task-entry trampoline 0xC2BD0 and tail-called from the scheduler switch 0xC24DE at 0xC269A — **reached on every task start and every resume** [V] |
| 0xC2F46 | 62 B6 | `cpsie i` | inside event dispatcher 0xC2EFC, temporarily unmasks around the handler `blx` (IRQ context only) [V] |
| 0xC26C0 | 62 B6 | `cpsie i` | deferred-dispatch loop 0xC26B8, reached from exception-return helper 0x103B50 (only after an exception/IRQ took the vector) [V] |
| 0x120A32 | 62 B6 | `cpsie i` | CPU1/second event dispatcher 0x120A0C (IRQ context only) [V] |
| 0x120A60 | 62 B6 | `cpsie i` | tail of the CPU1/second event dispatcher 0x120A0C (IRQ context only) [V] |
| 0x121BBC | 62 B6 | `cpsie i` | late/thread code in the image tail (secondary-core bootstrap region) [V] |

(Whole-image linear scan with skipdata: exactly these six `cpsie i` sites and zero
`cpsie f`; one prior-lane "PHY cpsie" address decodes as `cpsid i` in the exact scan.)
The image contains **no `cpsie f` and no `msr cpsr_c/#imm` that clears F**: FIQ is never
unmasked by thread code (F=0 only in the initial frames, but a FIQ has no armed slot:
CA 0x17D42C is stamped with the BUG stub 0xC19D1 by the boot task at 0x47D08) [V].

---

## 3. The IRQ path and what must be true for delivery

### 3.1 ROM vector -> image trampoline -> dispatcher [V]

From the local ROM dump (derived words only): the ARM vector table at ROM CPU 0x00..0x1C is
8x `ldr pc,[pc,#…]`; vector 6 (IRQ, ROM 0x18) loads the literal at ROM 0x34 =
**0x00040064** = the image's ARM second-level trampoline; vector 7 (FIQ, ROM 0x1C) loads
ROM 0x7C which jumps through the ROM slot table at 0x1E0 -> image header +0x50 =
0x103140 -> reset/FIQ dispatcher 0xC2EEC -> CA 0x17D42C. (The IRQ target 0x40064 was also
device-confirmed by the earlier lane's live ROM read; here it is re-derived from the dumped
ROM.)

Image trampoline CPU 0x40064 (ARM, file 0x64): `sub lr,lr,#4; srsdb sp!,#0x13; cps #0x13;
push {r0-r12}; …; blx 0xC2EFC; …; pop; rfeia sp!` — i.e. IRQ/FIQ entry saves context, calls
the dispatcher 0xC2EFC, returns with rfeia. It is the **only caller of 0xC2EFC** in the
image, and no image code branches to 0x40064: the ROM vector is the only invoker [V].

Dispatcher 0xC2EFC: `r7 = [CA 0x4016010C]; idx = r7 & 0x3FF; handler = (idx <= 0x5F) ?
[CA 0x17D430 + 4*idx] : 0; lock/unlock CA 0x17D3D8; cpsie i; if (handler) blx handler;
cpsid i; CA 0x40160110 = r7` (EOI) [V].

### 3.2 Event controller configuration for the H2D mailbox event [V]

`pcie_msg_init` (CPU 0x49334, called by the boot task at CPU 0x4781E through the module
table) is where the H2D/D2H mailbox events are wired:

| step | CPU site | effect (CA) |
| --- | --- | --- |
| register ETE irq events | 0x496C8/0x496D6 -> 0xC74B0 | handler 0x462F9 for id 0x2D, 0x4624D for id 0x2E; enable via 0xC6FF4 |
| raise (or unmask) 0x4C, 0x4E | 0x49754 (0xC6DB8(0x4C)), 0x4975A (0xC6DB8(0x4E)) | writes bank CA 0x40161180 + 4*(id>>5) and mirror 0x40161280 |
| H2D/D2H mailbox register pointers | 0x49760..0x49790 | hcc instance (0x10C190): out[0]=CA 0x40039010, out[1]=CA 0x40039014, out[5]=CA 0x400392F0, out[2] doorbell=CA 0x400392D4, D2H doorbell=CA 0x40101434 |
| clear out[0]/out[1] | 0x4978C/0x4978E | CA 0x40039010/14 = 0 |
| D2H notify enable | 0x49794..0x497A4 | 16-bit RMW CA 0x40101410 |= 1 |
| register 0x4C -> 0x40295, 0x4E -> 0x402A1 | 0x49800..0x4980C (per prior lane's disasm; same function) | handler table CA 0x17D430[0x4C/0x4E] |
| enable 0x4C/0x4E | 0x49816/0x4981C -> 0xC6FF4 | CA 0x40161100 + 4*(id>>5), bit id&31 |

Derived register state for the mailbox event (reader = dispatcher, writer = pcie_msg_init):

| CA | role for event 0x4C | value |
| --- | --- | --- |
| 0x40160100 | controller global enable (0xC3024, entry A/B and ROM S1) | 1 |
| 0x40160104 | priority threshold (0xC3024) | 0xFF |
| 0x40160108 | config (0xC3024) | 3 |
| 0x4016010C | current vector/status (dispatcher index = low 10 bits) | hard-ware |
| 0x40160110 | EOI (dispatcher writes status back) | - |
| 0x40161100+4*(id>>5) | per-event **enable**, W1S bit id&31 | for 0x4C: word 0x40161108, bit 12 = 0x1000; for 0x4E: bit 14 = 0x4000; for 0x1D: word 0x40161100 bit 13 = 0x2000 -> expected enable words 0x2000 / 0xC0006000 / **0x5000** (matches the lane-plan value) |
| 0x40161180+4*(id>>5) | raise/unmask bank (0xC6DB8) | bit 12 (0x4C), bit 14 (0x4E) touched at pcie_msg_init; polarity [U] (set-pending vs unmask; entry A also pre-fills -1) |
| 0x40161400+4*(id>>2), byte id&3 | priority (0xC1A3C) | level 5 -> 0x50 for 0x4C/0x4E |
| 0x40161800+4*(id>>2), byte id&3 | target core (0xC74B0 from table CPU 0x105F8C + 3*id + 2, only for 0x1F < id < coreword) | 0x4C -> 0x01 (core 0); 0x2D/0x2E/0x4E/0x46 -> 0x01; coreword [CA 0x17D398] = 0x60 on the golden boot (bench read) so all ids 0x20..0x5F get a target |
| 0x40160600/04/08/0C | scheduler timer block, event 0x1D source (0xC704C) | 0x61A800 / 0x61A800 / 7 / 1 |

### 3.3 Delivery prerequisites — met vs open

| prerequisite | status |
| --- | --- |
| CPU CPSR I = 0 in the context that should take the IRQ | **MET [V]** — 0xC2B80 `cpsie i` runs before every task body, on every resume, and in idle |
| ROM IRQ vector points into the image trampoline | **MET [V]** — ROM vector[6] literal = 0x00040064 |
| event controller global enable 0x40160100 = 1 | **MET [V]** — 0xC3024 from entry A (and ROM S1) |
| event 0x4C handler registered + enabled | **MET [V]** — pcie_msg_init 0x497F4..0x4981C; also proven by the live D2H pipeline (the same function set up D2H notify/out[1]) |
| priority below threshold, target = core 0 | **MET [V]** — level 0x50 < 0xFF; target 0x01 |
| **the device mailbox block asserts event 0x4C when the host writes out[0]/out[2]** | **OPEN [U]** — hardware-internal, not modelled in the image |
| **the 0x4016 controller delivers that event to the CPU (IRQ line / controller wiring)** | **OPEN [U]** — no GIC programming exists in image or readable ROM [V negative]; the 0x4016 block is outside all six inbound regions so its live state cannot be read |

So the exact blocked transition is **(event 0x4C assertion + delivery)**, not the CPU mask.
Our transport already does everything the vendor host does for the mailbox (out[0]=bit3,
out[2] |= 1, and the ETE SR node); the firmware's ETE engine consumed the SR node, but the
mailbox event never surfaced as a dispatch (out[0] stayed 0x8, out[5]=0, out[2] read back
without the dispatcher's 8). A host-only action cannot create the missing assertion:
CA 0x4016xxxx is not host-writable through region 3 (the earlier "write 0x40161188 through
BAR0 0x519188" plan is the retracted wrong-window alias -> CA 0x02041188 in ACP SRAM).

---

## 4. Patch site inventory (all offsets are file = CPU - 0x40000 on the local blob)

Original encodings are listed only as the patch specification (instruction encodings, not
blob content); the builder verifies them by a window hash before applying, and the full blob
is never copied into this repo.

| id | CPU | file | original instruction(s) / bytes | patched bytes | instruction after patch |
| --- | ---: | ---: | --- | --- | --- |
| **PA** | 0xC72F6 | 0x872F6 | `movmi r1,#0xB3` — B3 21 | **33 21** | `movmi r1,#0x33` (SVC, I=0, F=0, T=1 for Thumb tasks) |
| **PB** | 0xC24F8 | 0x824F8 | `cpsid i` — 72 B6 | **00 BF** | `nop` |
| **PC** | 0x102C20 | 0xC2C20 | `tst lr,#1; orrne r4,r4,#0x20` — 01 00 1E E3 20 40 84 13 | **80 40 C4 E3 00 F0 20 E3** | `bic r4,r4,#0x80; nop` (ARM) |
| **PD-1** | 0x47610 | 0x7610 | `mov r1,r6; ldr r0,[pc,#0xC]; blx 0x102E68` first 6 B — 31 46 03 48 BB F0 | **F8 F7 40 FE FC E7** | `bl 0x40294; b 0x47610` |
| **PD-2** | 0x4916A | 0x916A | `movs r0,#0; add sp,#0x84` — 00 20 21 B0 | **FE F7 51 BA** | `b.w 0x47610` |

Window hashes used by the builder to verify the source (first 16 hex of sha256 of the
original window): PA `51028cac…4fff`, PB `560de662…c420`, PC `1498affc…02d7`,
PD-1 `e81740cc…d442`, PD-2 `c5975cf4…2f88`.

Notes:
* **PA** is a data/constant patch inside task-create 0xC7218 (site 0xC72F6, the `movmi` arm
  for odd/Thumb function pointers): it changes the initial CPSR of
  *every newly created task* from I=1 to I=0. It is a strict no-op relative to the trampoline
  unmask (0xC2B80), so it is a **control**, not a fix.
* **PB** removes the `cpsid i` at the head of the scheduler switch 0xC24DE (0xC24F8). The
  switch still takes spinlock 0xC2354 and every external caller uses 0xC2394 (which does its
  own `cpsid`), so the window is small but non-zero.
* **PC** is the ARM arch context-save 0x102C14: replace the interworking correction with an
  explicit I-clear in the SPSR image, so every task that is switched out is resumed with
  IRQs enabled. T-bit correctness is preserved because all image thread code is Thumb and
  the saved CPSR already has T=1 when the save happens in Thumb code (the original
  `tst lr,#1; orrne` only matters for ARM-state saves, which the thread layer does not use).
* **PD** does not touch the mask or the event layer at all. 0x47610 is the *handshake
  failure* path of entry A ("cache maintenance + repeat the +8==2 poll"); on a golden boot
  (page +8 == 2) it is never executed [V]. The boot task's success epilogue 0x4916A is
  reached after it has built and sent g4 id1 (0x47D54) and the id2 DEVICE READY frame
  (0x48FF4 -> 0x49160) and all of its post-send init chain, so redirecting it there keeps
  the whole ready handshake intact and then parks the task in
  `bl 0x40294 (-> 0xC18AC); b 0x47610` forever. 0x40294 is the firmware's own event-0x4C
  handler wrapper: it loads r0 = hcc instance CA-relative 0x0010C190 and tail-calls the H2D
  dispatcher 0xC18AC, which (in order) writes the ack word `*[hcc+0xC] = 1` (CA 0x400392F0),
  reads **and clears** out[0] through `*[hcc+4]`, writes `*[hcc+0x10] = 8` (CA 0x400392D4 =
  8, the H2D-done doorbell), then dispatches one handler per pending bit from
  `[hcc+0x20]`.

---

## 5. Expected bench effect and ranking

### 5.1 PD (recommended first, one idea per fire)

Boot protocol unchanged: rings armed, upload patched image (byte-exact verify against the
patched copy, not against the vendor blob), custom blocks seeded, release 0x5A5A only, no
0xCECE during boot. Expected sequence:

1. Golden handshake page unchanged: 0x86807FEF / 0x7 / 0x2 / 0xABCD, state CA 0x118B60 = 1,
   mask CA 0x40008 = 0; D2H ready pair still decoded (g4 id1, g4 id2 payload 0x01060850) —
   they are sent *before* the redirected epilogue.
2. The host then posts the existing single H2D SR announce (group 0 id 0, 12 B, tag 0x5A5A,
   node devva|0x6D2B, mailbox out[0] = BIT(3), out[2] |= 1) exactly as in the 15:10 session.
3. **Expected within the next loop iteration (sub-millisecond):**
   * out[0] (CA 0x40039010) bit3 **clears to 0** — the dispatcher read+cleared it;
   * **out[5] (CA 0x400392F0, read-only) reads 1** — the dispatcher's ack write;
   * **out[2] (CA 0x400392D4) reads 8** — the dispatcher's H2D-done doorbell;
   * the ETE SR consumer already advanced (it did in the 15:10 run);
   * mask stays 0 (no exception); CECE is **not** needed for this patch.
4. If a handler for mailbox bit3 exists in the hcc table it runs in the boot task's context
   (that handler chain is the next layer to map); if not, the consumption observables above
   are still the definitive positive.
5. Negative result (out[0] stays 0x8): the hcc instance CA 0x10C190 fields or the dispatcher
   contract differ from the static model. Next read (host-readable RAM,
   BAR0 = CPU + 0x6B8000): CA 0x10C190 +0x04/+0x0C/+0x10/+0x20 words, and the CPU 0x17D430
   handler table. Do not retry the frame in the same session.

### 5.2 PA / PB / PC (mask controls; expected to be no-ops)

All three force I=0 into contexts that section 2 already proves are unmasked. If one of them
*does* change the H2D result, the simple model is wrong somewhere (e.g. a context we did not
map resumes masked), which is itself valuable. Rank: PC (explicit, keeps scheduler cpsid) >
PA (data-only, 2 bytes) > PB (removes scheduler cpsid, small reentrancy risk).

### 5.3 Suggested sessions

1. **Session 1 (decisive, low cost): PD only.** One boot, ready pair, one H2D frame,
   read out[0]/out[2]/out[5] + mask. This separates "mailbox consumer reachable" from
   "event 0x4C delivery is dead" with one bit of information.
2. **Session 2 (only if PD negative): PA+PC combined control**, same procedure. A negative
   here rules out the mask layer completely and leaves only the device event/vector layer.
3. Do not combine PD with PB in the same flight and do not write CA 0x400392F0 / 0x40160110
   from the host; the firmware owns them.

---

## 6. What would close the remaining [U] items (read-only / host-free)

* The device event controller and its CPU interface are device-internal; the only host-side
  way to observe them is a diagnostic **7th endpoint-iATU viewport** mapping a free BAR0
  window to CA 0x40160000 (postboot lane section 7.2). Read-only expectations if the layer is
  alive on a vendor golden boot: 0x40160100 = 1, 0x40160104 = 0xFF, 0x40160108 = 3,
  0x40161100/04/08 = 0x2000/0xC0006000/0x5000, 0x4016010C = 0 unless an event is currently
  being taken, 0x40161188/0x40161288 pending/raise state. **Never write these from the host.**
* The 0x4016 block is outside all six inbound regions; the old hcc_irq_probe arithmetic wraps
  the address by -0x100000. Do not reuse it.
* A vendor golden trace (stock boot, kprobe/trace on the vendor stack) that watches
  CA 0x4016010C / the dispatcher entry 0xC2EFC when the vendor host posts an H2D message
  would close the raise-source question directly.

---

## 7. Evidence table (new claims of this lane)

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | ROM IRQ vector[6] literal (ROM 0x34) = 0x00040064; FIQ -> ROM 0x7C -> slot table 0x1E0 -> image header +0x50 -> 0xC2EEC | `irq_map.py rom` over the local ROM dump | [V] |
| E2 | task create builds PC=0xC2B89 (entry trampoline), LR=0xC2E1D (exit), CPSR=0xB3 at the stack top; the scheduler first-dispatch path 0xC70EA -> 0x70F8 -> 0x103B60 -> 0x102C04 restores exactly those words | disasm 0xC7218/0xC745C/0xC70C0/0x102C04/0x102C14 | [V] |
| E3 | the task-entry trampoline calls 0xC2B7A before the task fn; 0xC2B7A executes `cpsie i` at 0xC2B80; the scheduler switch 0xC24DE exits via 0xC2694 -> 0xC2B7A and a resumed task returns into 0xC2658 -> 0xC2694 | disasm 0xC2B88, 0xC2B7A, 0xC24DE, 0x102C14 | [V] |
| E4 | the image has exactly the six `cpsie i` sites of section 2.2 and zero `cpsie f`/CPSR-F writes | whole-image capstone census (Thumb+ARM linear, skipdata) + prior lanes agreed | [V] |
| E5 | event 0x4C is the only H2D mailbox notification: handler wrapper 0x40294 -> 0xC18AC; enable 0x40161108 bit12; priority 0x50; target 0x01 from table 0x105F8C+3*id+2; dispatcher 0xC2EFC reads CA 0x4016010C and EOIs CA 0x40160110 | disasm 0xC2EFC/0x49334/0x496C8..0x49830/0xC74B0/0xC6FF4/0xC6DB8 + table read | [V] |
| E6 | the image has no reader of out[0] CA 0x40039010 except the 0xC18AC dispatcher (literal census: one occurrence, 0x497E0, in pcie_msg_init's instance-pointer setup; the dispatcher uses the hcc pointer fields) | whole-image u32 census + disasm | [V] |
| E7 | no standard (Cortex-A9) GIC programming exists in the image or the readable ROM: whole-blob scans for 0x10100100/0x10101000/0x10480000 etc. return no code references | whole-image u32 census + ROM scan | [V negative] |
| E8 | every host-visible prerequisite of 0x4C delivery is met in the golden boot; the open prerequisites are the hardware raise and the controller->CPU delivery, both outside the image | E1..E7 + bench state from the ledger | [I high] |
| E9 | the patch candidates encode the intended instructions and apply cleanly on the local blob | `tools/patch_builder.py --selftest` (disassembles the patched image) | [V] |

---

## 8. Reproduction (host-only; local blob read in place; nothing vendor committed)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools

    # 1) IRQ path + unmask census + mailbox event configuration + patch table
    $PY $T/irq_map.py rom           # ROM vectors -> image trampoline target
    $PY $T/irq_map.py map           # image trampoline/dispatcher/switch/trampoline chain
    $PY $T/irq_map.py unmask        # cpsie/cpsid census with roles
    $PY $T/irq_map.py mailbox       # event 0x4C registration/enable/priority/target
    $PY $T/irq_map.py patch         # patch sites, verified against the local blob
    $PY $T/irq_map.py selftest      # self-checking (exit 0 on PASS)

    # 2) build patched copies in a NON-repo directory (default /tmp/hi5622-patched)
    $PY $T/patch_builder.py --list
    $PY $T/patch_builder.py --patch PD          # -> /tmp/hi5622-patched/FIRMWARE.PD.bin
    $PY $T/patch_builder.py --patch PA --patch PC
    $PY $T/patch_builder.py --selftest

The builder refuses an output directory inside `opensource/`, verifies the vendor blob
sha256, verifies each original window by hash, and records a manifest with the patch recipe
and hashes only (no vendor bytes).

---

## 9. Open items ([U])

1. Does the device mailbox block assert event 0x4C on the host's out[0]/out[2] write, and how
   is event 0x4C delivered to the CPU if no GIC is programmed? (Requires the 7th-viewport
   read-only diagnostic or a vendor trace.)
2. Polarity/role of the 0x40161180/0x40161280 banks (raise vs unmask) — entry A pre-fills -1;
   a correct-viewport read on the vendor golden boot decides.
3. What registers/event id the image's event-0x46 handler 0x40525 (reads CA 0x4000604C, queue
   CA 0x117E94) belongs to; not needed for the mailbox path.
4. Whether CPU0 has a permanent idle/poll task after the boot task exits (the ready bitmap is
   empty -> 0xC24DE has no idle fallback other than 0xC2B7A for the null-current case); the
   post-exit CPU0 task graph is the next static item if PD shows the consumer alive but the
   post-boot contexts never run.

---

*Lane: wifi-irq-unmask, 2026-11-24, shared task task-90. Host-only static reversal.
The vendor firmware and the mask-ROM dump remain local evidence only; no vendor bytes are
committed. Single-instruction encodings above are patch specifications, and the tooling
re-verifies them against the local blob by hash.*
