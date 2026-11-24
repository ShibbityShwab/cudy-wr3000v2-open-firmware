# PD2: why PD data-aborted, and a correct-context firmware patch that runs the H2D mailbox dispatcher

**Lane:** `wifi-irq-unmask`, shared task **task-91 (PD2)**, **host-only static RE**, 2026-11-24.
The bench router was not contacted; no image was built or flashed; no device was opened. The vendor
firmware blob and the mask-ROM dump stay **local evidence only**; nothing vendor-derived is copied
into this directory. Every value below is a derived annotation (address, constant, instruction
encoding) needed to specify the analysis and the patch, and the tools re-verify everything against
the pinned local artifacts by hash.

Inputs (local, read in place):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B vendor Wi-Fi firmware, sha256 `7fc87e20…311b`; **CPU = file + 0x40000** |
| `/home/shibbityshwab/staging-fi/rom16k.bin` | 16 KiB mask ROM, sha256 `fcfd464f…8faa4`; ROM CPU == dump offset |
| PD bench result | ledger 2026-11-24 16:10 + the PD patch of this lane's REPORT.md sections 4-5 |
| prior lanes | `wifi-rom-dump`, `wifi-rom-s01`, `wifi-fw-postboot`, `wifi-ready-emit`, `wifi-abort-rca`, `wifi-irq-unmask` |

Evidence markers: **[V]** verified in this lane from the local artifacts with
`tools/pd2_re.py` (33/33 selftest) or `tools/pd2_patch_builder.py` (38/38 selftest);
**[V\*]** device-measured by an earlier attended lane (cited); **[I]** inference from code;
**[U]** not decidable without a device read.

Address conventions: image **CPU = file + 0x40000** (file = CPU - 0x40000); ROM cpu == dump
offset; host BAR0 of CPU RAM = CPU + 0x6B8000; the host's IO window covers device
CA 0x40000000..0x4011FFFF only (CA 0x4016xxxx is **not** host-visible). "CA" values that the
firmware itself stores and dereferences (out[0] 0x40039010 etc.) are chip CPU addresses and are
directly addressable by the firmware.

---

## 0. TL;DR

1. **PD failed for a completely different reason than "wrong calling convention".** PD's first
   segment patched **CPU 0x47610**, which is *not* a dead handshake-failure path but the **retry
   body of entry A's ROM<->image handshake poll** (`0x473F0: ldr r3,[page+8]; cmp r3,#2;
   bne.w 0x47610`). That retry runs on **every failed poll while CPU1's ROM stage S1 has not
   written page+8==2 yet** - i.e. *before* the boot task exists and *before* `pcie_msg_init`
   (CPU 0x49334) has initialised the H2D mailbox pointers. PD therefore called the dispatcher with
   the hcc instance still **static zero**, and its first store `str r7,[r2]` at CPU **0xC18B8**
   with `r2 = [hcc+0xC] = 0` wrote to CPU address 0 -> precise **DATA ABORT** (exception mask
   id 4, CA 0x40008 = 0x10). **[V]** The boot-task epilogue redirect (PD-2) was **never reached**
   in that boot: entry A died inside its handshake wait, which is exactly why state was 0, page+0
   bit14 (set at 0x47400 *after* a successful poll) was missing, and 4x out[1]=4
   "startup/vector announce" appeared (4 exception entries; the host CECE policy answered 3 of
   the spins). **[V]**
2. **The handshake-page clobber was not a bad SP in the loop.** The image's exception recovery
   long-jumps to the faulting function's caller and **never restores the ABT-mode stack pointer**;
   with ROM ABT top 0x1BF110 and a 0x44-byte frame per entry, consecutive aborts stack at
   0x1BF0CC, 0x1BF088, 0x1BF044, **0x1BF000**. The 4th frame's words {r0,r1,r2,r3} =
   `{0x0010C190, 0, 0, 0x1BEE50}` - byte-for-byte the bench-observed page overwrite
   (0x0010C190 / 0 / 0 / 0x1BEE50). **[V]** So the clobber is the *symptom* of the abort storm,
   not a missing stack setup.
3. **The dispatcher contract is simple and the PD2 context satisfies it.** `0x40294` is a
   no-argument wrapper (`ldr r0,=0x10C190; b.w 0xC18AC`); the dispatcher needs **only a valid
   SVC stack and an initialized hcc instance** (fields +4/+0xC/+0x10 = CA out[0]/ack/out[2] and
   +0x20 = handler table). It preserves r3-r7 (`push {r3,r4,r5,r6,r7,lr}` -> `pop`). It needs
   **no per-core context pointer, no event args, no global state** beyond the instance. What it
   *does* need is the **initialisation phase of `pcie_msg_init` (CPU 0x49764-0x497A4) to have
   run**, which only happens from the boot task (module-table call at CPU 0x47824). **[V]**
4. **PD2 = drop PD-1, keep the epilogue redirect, and run the dispatcher from a guarded poll loop
   placed in a genuinely dead code block.** Segments (file offsets):
   * **PD2-ENTRY**: file **0x916A**, 4 B: `00 20 21 B0` -> **`FE F7 62 B9`**
     (`b.w 0x47432`, the boot-task epilogue CPU 0x4916A - reached only after the full init
     chain).
   * **PD2-LOOP**: file **0x7432**, 62 B (CPU 0x47432-0x4746F): a guard+poll stub that waits for
     the hcc instance to be initialized, polls mailbox **out[0] = CA 0x40039010**, and only then
     calls the firmware's own wrapper 0x40294. If the dispatcher's handler-table pointer still
     holds its static/uninitialised value 0x118D68 (an image code address), the stub substitutes
     the mask-ROM RAZ window 0x2000 as a **permanently-zero handler table**, so the dispatcher
     still executes the vendor ack/consume/doorbell sequence (out[5]=1, read+clear out[0],
     out[2]=8) without `blx`-ing an uninitialised table, then restores the pointer. **[V]**
5. **Expected bench effect (one H2D SR announce after the golden boot):** page stays
   `0x86807FEF / 0x7 / 0x2 / 0xABCD`, state CA 0x118B60 = 1, mask stays **0** (contrast PD:
   mask 0x10 + page clobber + state 0), **out[0] 0x8 -> 0, out[5] = 1, out[2] = 8**. That is the
   first end-to-end proof that the firmware's mailbox consumer runs when the mailbox event is
   delivered. **[I high]**
6. **Module gate fix for the Lead:** the SR-announce gate must **latch** boot completion on the
   first post-release observation (`page+8==2`, page+0 bit14, or `state==1 && mask==0`)
   instead of re-reading the live page at announce time - the page is legitimately rewritten by
   the firmware (and, in crash paths, wiped by the exception routine), so a live re-read is a
   fragile gate. Details in section 6.

---

## 1. Method and deliverables

`tools/pd2_re.py` (new) is a read-only verifier over the local blob/ROM: subcommands
`dispatcher` (wrapper/dispatcher/instance-field contract + event-0x4C registration/enable),
`fault` (PD's abort reconstruction incl. the nested ABT-frame arithmetic), `cave` (PD2
code-cave justification: entry, neighbours, reference census) and `selftest` (33 checks).
`tools/pd2_patch_builder.py` (new) is the PD2 build/verify tool: `--list`, `--build [OUTDIR]`
(defaults to a non-repo /tmp dir; refuses to write inside the repository), `--recipe`
(machine-readable recipe for the bench tooling) and `--selftest` (38 checks: pinned blob
sha256/len, every source-window hash, every patched-instruction decode, offset/overlap/range
checks, "only the segments differ", manifest has no vendor bytes).

The patched image is a **local replay artifact only**; the repository keeps hashes and
disassembly annotations, never vendor bytes. No ROM/firmware content is committed.

---

## 2. The H2D dispatcher path, exactly

### 2.1 Which handler is the H2D mailbox consumer

* Event-controller event **0x4C** is the only H2D mailbox notification. `pcie_msg_init`
  (CPU 0x49334, called by the boot task through the module table at CPU 0x47824 with r0=0)
  registers **handler 0x40295** (wrapper 0x40294 | Thumb bit) for event 0x4C via
  `0xC74B0(0x4C, arg=5)` at CPU 0x497FE-0x49800, and enables it via `0xC6FF4(0x4C)` at
  0x49814-0x49816 (**W1S word CA 0x40161108, bit 12 = 0x1000**; mirror bank CA 0x40161280). **[V]**
* Wrapper CPU 0x40294 (12 B): `ldr r0,[pc,#4]` (literal at 0x4029C = **0x0010C190**) then
  `b.w 0xC18AC` - a tail call; it ignores any incoming arguments and always passes the
  **hcc instance** CPU 0x10C190 (= global base 0x10C0C0 + 0xD0). **[V]**
* This is a **different path from the exception staging routine 0x86E14** (CPU 0xC6E14; file
  0x86E14). The PD bench's 4x `out[1]=4` came from 0xC6E14 - the data-abort *report* path
  (section 3.3), which does **not** call the event dispatcher and does **not** touch out[0]. The
  event-IRQ path is ROM vector 0x18 -> image ARM trampoline CPU 0x40064 -> event dispatcher
  CPU 0xC2EFC -> `blx` handler-table entry [CA 0x17D430 + 4*0x4C] = 0x40295 -> wrapper ->
  0xC18AC. **[V]**

### 2.2 Dispatcher CPU 0xC18AC - byte-level contract

    push {r3,r4,r5,r6,r7,lr}      ; preserves r3-r7 for the caller
    r6 = r0 ; if (r0 == 0) return ; r7 = 1 ; r1 = 0
    r2 = [r0+0x0C] ; *r2 = 1      ; 0xC18B6/B8  ack: CA 0x400392F0 = out[5] <- 1
    r2 = [r0+0x04] ; r5 = *r2 ; *r2 = 0   ; read and clear out[0]: CA 0x40039010
    r1 = 8 ; r2 = [r0+0x10] ; *r2 = r1    ; H2D-done doorbell: CA 0x400392D4 = out[2] <- 8
    if (r5 == 0) return           ; nothing pending
    index = lowest-set-bit(r5); if (index > 9) return
    table = [r6+0x20]             ; 0xC18DA  handler table, 8-byte {fn,arg} records
    fn = table[index].fn ; if (fn) { arg = table[index].arg ; blx fn }
    r5 &= ~(1<<index) ; repeat until r5 == 0

**Instance fields it dereferences (with the CAs `pcie_msg_init` installs):** **[V]**

| hcc field | absolute | runtime value | role |
| --- | --- | --- | --- |
| +0x04 | CPU 0x10C194 | 0x40039010 | out[0]: H2D pending bitmap, dispatcher reads then clears |
| +0x0C | CPU 0x10C19C | 0x400392F0 | out[5]: ack word, dispatcher stores 1 |
| +0x10 | CPU 0x10C1A0 | 0x400392D4 | out[2]: H2D-done doorbell, dispatcher stores 8 |
| +0x20 | CPU 0x10C1B0 | **unresolved [U]** | handler table, 8-byte {fn,arg} records, ids 0-9 |

The three CA values are derived statically from the init code (out0/out1 literals at CPU
0x497E0/0x497DC; ack = 0x40101434 - 0xC8000 - 0x144 = 0x400392F0; doorbell = ack - 0x1C) and are
constants - the same in the vendor boot. **Before `pcie_msg_init` runs, all four fields are
static zero** (verified from the image bytes; no other image code writes them). **[V]**

### 2.3 Calling convention / required context (the task's question)

| requirement | verified value |
| --- | --- |
| argument | **r0 = hcc instance** CPU 0x10C190; no other argument (the wrapper supplies r0) |
| stack | a valid SVC stack; the dispatcher pushes 24 B (6 words). No sp-relative layout or sp-alignment-sensitive access. Callers should keep SP 8-byte aligned at the public call (AAPCS) because the dispatcher may `blx` handlers. |
| registers | r1-r7 caller-saved *in principle*, but the dispatcher itself restores **r3-r7** (it pushes them) and never needs any of them on entry. |
| globals / per-core context | **none.** No coreword, no per-core pointer, no event args, no lock. Only the instance (and the instance's handler table). |
| event args | none; it is invoked as a no-argument handler from the event dispatcher (`blx`), so a direct `bl 0x40294` is contract-equivalent as long as the stack is valid and the instance is initialised. |
| preconditions | `pcie_msg_init` (CPU 0x49334) must have completed its mailbox-field phase; otherwise +4/+0xC/+0x10 are 0 and the first store faults (section 3). |

**Why PD's direct `bl 0x40294` data-aborted:** not stack, alignment, globals, per-core context or
event args - the call was simply made **during the ROM<->image handshake, before the boot task had
run `pcie_msg_init`**. The first dereference `ldr r2,[r0,#0xC]` read 0 and the store
`str r7,[r2]` at CPU **0xC18B8** faulted (address 0 is the mask-ROM window, not writable).

### 2.4 Handler table: unresolved static value [U] (important for PD2 safety)

`[hcc+0x20]` (CPU 0x10C1B0) has the **static value 0x00118D68**, which is an address *inside
image code* (CPU 0x118D68 is in the middle of a Thumb function at 0x118CF8), and:

* no image code writes [0x10C1B0]: `pcie_msg_init`'s mailbox-field window writes +0xD0/+0xD4/
  +0xD8/+0xDC/+0xE0/+0xE4/+0xE8/+0xEC and +0xF4/+0xF8 - but **not** +0xF0 (= hcc+0x20);
* a whole-image scan finds no `add.w rX,rY,#0xF0` and no PC-relative literal materialising
  0x118D68;
* there *is* a firmware helper that registers 8-byte {fn,arg} handler records (CPU 0xC187A:
  `table[index].fn = r2; table[index].arg = r3` for index <= 9), but its table pointer comes
  from `[[0x172130]+0xBC]` (a different runtime object) - not from hcc+0x20. **[V]**

So either a firmware layer we have not mapped populates hcc+0x20 at runtime, or the field is a
different structure whose meaning we have not decoded. This matters because a pending out[0] bit
makes the dispatcher `blx` `table[index].fn` - with the static value that would be a jump to
garbage inside image code. PD2 therefore **never trusts a handler table that still holds
0x118D68**: it substitutes the ROM RAZ window (0x2000) for the call, which makes the dispatch loop
see fn=0 and consumes the mailbox without executing handlers (section 5.3). Section 10 O1 lists
what would close this.

---

## 3. PD's data abort, reconstructed

### 3.1 The handshake retry is a hot path, not a failure-only path

Entry A (CPU 0x46970, CPU0) boots the chip:

    0x473CC  page CA 0x1BF000: [+8]=0, [+C]=0xABCD ; flush the two words
    0x473E0  CA 0x40161F00 = 0x00020000 (wake CPU1) ; isb
    0x473F0  ldr r3,[page+8] ; cmp r3,#2 ; bne.w 0x47610        <-- poll
    0x473F8  page+0 |= 0x4000 (bit14) ; task create 0xC745C ; scheduler start 0xC704C
    0x47610  mov r1,r6 ; ldr r0,=page+8 ; blx 0x102E68 (ARM D-cache clean+invalidate)
    0x47618  b 0x473F0                                          <-- retry

and CPU1 runs ROM stage S1, which only writes `page+8 = 2` once it sees
`page+C == 0xABCD` and the ROM boot gate `CA 0x4000010C == 0xCEED` (both already true at
0x473E0). Between the wake and the S1 ACK, entry A executes the retry body **many times**. PD-1
replaced the first 6 bytes of 0x47610 with `bl 0x40294; b 0x47610`, so the very first failed
poll called the H2D dispatcher. **This is deterministic, not a race:** the first poll always
fails, because S1 cannot have written +8 before the wake doorbell. **[V]**

### 3.2 The first faulting instruction

At that moment the boot task does not exist, `pcie_msg_init` has not run, and the hcc instance
fields are static zero. Dispatcher entry:

    CPU 0xC18B6  ldr r2,[r0,#0xC]   ; r0 = 0x10C190, r2 = [0x10C19C] = 0
    CPU 0xC18B8  str r7,[r2]        ; store 1 to CPU address 0 -> precise DATA ABORT

Address 0 is the mask-ROM window (ROM is mapped at CPU 0 and is not writable), so the write is a
precise external data abort. The exception ID is 4: ROM data-abort vector 0x10 -> ROM stub 0x6C
-> image header slot CPU 0x40054 -> image exception-table word[1] = **CPU 0x103114** -> tail
0x103B68 -> thunk 0xC6FC8 (id 4, adjusts the saved PC by -8) -> staging routine 0xC6E14; mask
`CA 0x40008 |= 1<<4 = 0x10`, matching the bench. **[V]**

### 3.3 What the exception report path does (why out[1]=4 and the CECE spin)

Exception staging routine CPU 0xC6E14 (file 0x86E14) - this is the code the PD ledger saw as
"the image exception/vector handler": it (1) raises a per-core report event via 0xC6DB8 from
table 0x105FDC, (2) **wipes `CA 0x4000010C` and `CA 0x40000108`** (boot gate and release
word), (3) stores **out[1] = 4** at 0xC6F56 (CA 0x40039014) and pulses the D2H doorbell
CA 0x40101434 = 1, (4) **spins until CA 0x4000010C == 0xCECE**, then unlocks, restores the
faulting function's LR and **long-jumps to its caller**. It never restores the ABT-mode SP and
never returns to the faulting instruction. **[V]**

Hence each fault needs one host `0xCECE` write to unwind; the host CECE policy wrote 3 times ->
3 recoveries, and the 4th fault parked in the spin. That yields exactly **4x out[1]=4** for the
log and leaves the CPU stuck (no retry loop in the usual sense). **[V]**

### 3.4 The handshake-page overwrite, explained exactly

The image data-abort stub (CPU 0x103114) does `pop {r0}; sub sp,#0x44; stm sp,{r0-r12}` and
stores lr/spsr/SVC-sp/SVC-lr in the frame; the recovery then long-jumps away and the ABT-mode SP
is left 0x44 bytes lower each time (ROM ABT top = 0x1BF110, from the ROM table at 0x1E0):

| abort # | ABT SP on entry | staged frame |
| --- | --- | --- |
| 1 | 0x1BF110 | 0x1BF0CC..0x1BF10F |
| 2 | 0x1BF0CC | 0x1BF088..0x1BF0CB |
| 3 | 0x1BF088 | 0x1BF044..0x1BF087 |
| **4** | **0x1BF044** | **0x1BF000..0x1BF043** |

The 4th frame starts exactly at the handshake page CA 0x1BF000; its first words are the staged
**r0, r1, r2, r3 of the faulting context**: `r0 = 0x0010C190` (the wrapper's hcc argument),
`r1 = 0` (dispatcher zeroed it), `r2 = 0` (the invalid mailbox pointer), `r3 = 0x1BEE50`
(entry A leftover). The bench read `0x0010C190 / 0 / 0 / 0x1BEE50` - an exact match. So the page
clobber **is** the exception-frame save; it is a symptom of the abort storm, not of a bad SP in
the poll loop (the boot-task epilogue context and stack were never the problem). **[V]**

### 3.5 The full PD bench signature, decoded

| bench observation | explanation |
| --- | --- |
| page+0 = 0x86803FEF, bit14 missing | entry A died in the poll loop before 0x47400 set bit14 |
| page+4 = 0x17 (bits 0,1,2,4) | ROM S1 ne-path bits 0,1,2 (`|=1, |=2, |=4`) + entry B 0xC742C `page+4 |= 0x10` (CPU1's ROM dispatched entry B) |
| page+8 = 2, page+C = 0xABCD | S1 completed independently (it only needs +C==0xABCD and the gate 0xCEED, both written before the poll) |
| state CA 0x118B60 = 0 | the boot task was never created (entry A never passed the poll) |
| mask CA 0x40008 = 0x10 | data abort id 4 (section 3.2) |
| 4x out[1]=4 "startup/vector announce" | 4 exception entries; CECE policy answered 3 spins (section 3.3) |
| page later = 0x0010C190/0/0/0x1BEE50 | 4th nested ABT frame (section 3.4) |
| out[0] never consumed | no host H2D traffic was posted in that session; and the dispatcher was dead |

**Correction to the PD lane/ledger interpretation:** "the direct call to wrapper 0x40294/0xC18AC
from the boot-epilogue context faults (bad context)" is not what happened - the redirect site
0x4916A was never executed. The faulting call came from the **handshake retry site 0x47610**,
which PD-1 patched under the wrong assumption that it is a terminal failure path.

---

## 4. PD2: design candidates, ranked

| id | candidate | verdict |
| --- | --- | --- |
| **PD2-A (chosen)** | Keep only the epilogue redirect (0x4916A -> small stub) and place a **guard+poll stub** in the conditionally-dead config-3 block CPU 0x47432: wait for the hcc instance to be initialised, poll out[0], and only then call the wrapper 0x40294; substitute the ROM zero table when the handler table still holds its static sentinel. | **Safest and diagnostic:** no call before init, no handler blx with an unverified table, handshake path (0x47610) untouched. |
| PD2-B | Same stub but always call the *live* instance/table (no zero-table substitution). | Runs real handlers if the HCC link layer ever populates hcc+0x20; risks `blx` to the static sentinel 0x118D68 on our boots -> data abort -> page clobber again. Not for the first PD2 fire; it is the natural follow-up once O1/O2 are closed. |
| PD2-C | Route through the working exception entry (raise a controlled exception - or event 0x4C - and let the firmware path reach the dispatcher). | **Rejected by static analysis.** The data-abort staging routine 0xC6E14 (the path PD already exercised) only reports/announces and spins for CECE; it never calls 0xC2EFC or 0xC18AC. Raising event 0x4C via 0xC6DB8 would need the event-controller delivery that is the very unproven layer (device event -> CPU IRQ), i.e. it does not remove the unknown. |
| PD2-D | Fix the page clobber by fixing "the bad SP". | **Not applicable:** the ABT SP is not our loop's SP; the clobber is the nested, never-unwound ABT stack (section 3.4). PD2 avoids it by never aborting. The only residual risk is an aborted *handler* (PD2-B), which PD2-A excludes. |

---

## 5. PD2 patch specification

### 5.1 Segments (file offsets; CPU = file + 0x40000)

| id | CPU | file | len | original window bytes | patched bytes | decode |
| --- | ---: | ---: | ---: | --- | --- | --- |
| **PD2-ENTRY** | 0x4916A | **0x0916A** | 4 | `00 20 21 B0` (`movs r0,#0; add sp,#0x84`) | **`FE F7 62 B9`** | `b.w 0x47432` |
| **PD2-LOOP** | 0x47432 | **0x07432** | 62 (0x3E) | (dead config-3 block; window sha16 `4ab344e16a74f8b4`) | see below | guard+poll stub |

Original-entry window sha16 (first 16 hex of sha256 of the original 4 bytes): **
`c5975cf4bd702f88`** (the same window as PD's second segment, different patched bytes).
PD2-LOOP source-window sha16: **`4ab344e16a74f8b4`**.

`PD2-LOOP` patched bytes (hex):

    0b4f81b03b68002bfcd0094a1368002bf8d07b69084ca34202d0f8f722fff1e7064c7c61f8f71dff7b61
    ebe700bf9cc1100010900340688d110000200000

Patched image: sha256 `7a6918077af91601f00901ad46b47f5ea36956b03faeda9d742ecc384a1b7728`, length
unchanged 928,920 B, and **only the 66 bytes of the two segments differ** from the vendor blob
(verified by `pd2_patch_builder.py --selftest`).

### 5.2 The stub, decoded

    0x47432  ldr  r7,[pc,#0x2c]     ; r7 = 0x10C19C  (hcc+0xC field: instance-init guard;
                                    ;                  the dispatcher preserves r3-r7)
    0x47434  sub  sp,#4             ; one-time bias: each bl (pushes LR) then enters the
                                    ; public wrapper with an 8-byte aligned SP. Never undone.
    0x47436  loop:
             ldr  r3,[r7]           ; hcc+0xC value (dispatcher's first dereference target)
             cmp  r3,#0
             beq  loop              ; instance not initialised -> wait (safe, no call)
             ldr  r2,[pc,#0x24]     ; r2 = 0x40039010 (mailbox out[0], H2D pending)
             ldr  r3,[r2]
             cmp  r3,#0
             beq  loop              ; nothing pending -> poll
             ldr  r3,[r7,#0x14]     ; r3 = [hcc+0x20] handler table pointer
             ldr  r4,[pc,#0x20]     ; r4 = 0x00118D68 (static/uninitialised sentinel)
             cmp  r3,r4
             beq  zero
             bl   0x40294           ; wrapper -> H2D dispatcher (live table)
             b    loop
    0x47452  zero:
             ldr  r4,[pc,#0x18]     ; r4 = 0x00002000 (mask-ROM RAZ window: reads 0)
             str  r4,[r7,#0x14]     ; consume-only callback table
             bl   0x40294           ; wrapper -> H2D dispatcher (zero table)
             str  r3,[r7,#0x14]     ; restore live table pointer (r3/r7 preserved)
             b    loop
    0x4745E  nop
    0x47460  .word 0x0010C19C, 0x40039010, 0x00118D68, 0x00002000

Both paths enter the dispatcher with the real live instance and the real CA pointers; the only
difference is a handler table that is provably zero (RAZ) vs the live one. In the zero path the
dispatcher still does the full vendor mailbox contract (ack out[5]=1, read+clear out[0],
out[2]=8) and then clears all pending bits without `blx`-ing anything.

### 5.3 Why this cave is safe (dead-code proof)

* **Entry:** the block CPU 0x47432 is the target of exactly one branch in the image - CPU 0x46A1E
  `beq.w 0x47432`, taken only if the **host-seeded platform block** CPU 0x1B2800 word
  **[+0x24] == 3**. The captured vendor platform block has [+0x24] = **2**, and our transport
  pre-seeds that exact block on every successful boot (GP4/GP5/GP8 procedure), so the branch is
  never taken. **[V code; V\* block value]**
* **Boundaries:** CPU 0x4742C-0x47431 is entry A's real epilogue (`add sp,#0x34; pop.w {...,pc}`,
  no fall-through), and CPU 0x47472 is live code (a branch target from 0x46CC8). The block
  0x47432-0x47471 is therefore self-contained.
* **No other references:** whole-image census finds no literal load, no u32 data word and no
  further branch target into [0x47432, 0x47472). `pd2_re.py --cave` prints the census.
* **Residual hazard:** if the platform block were *not* seeded (or seeded with [+0x24]==3), the
  config-3 path would jump into the stub during entry A; the stub would wait for the hcc instance
  for ever (safe hang, no fault, no crash) - and such a boot would fail S1 anyway because the
  block is required. This is documented as an explicit precondition of the patch.

### 5.4 Nothing else is patched

CPU 0x47610 (the handshake retry) is **untouched**; the success-epilogue replacement removes the
only path to the original `movs r0,#0; add sp,#0x84; pop.w {r4-r11,pc}` sequence (the pop at
0x4916E stays in the image but is unreachable). The boot task never returns to the RTOS task-exit
path; it becomes the H2D dispatcher poller. IRQs remain enabled in that task (timer IRQ preemption
keeps the rest of the firmware scheduling normally, and a preempted stub resumes at the same PC).

### 5.5 Expected bench observables (pass/fail)

Run the standard GP8 golden-boot procedure (single clean endpoint pulse, rings armed, upload +
verify, platform block 24 words + wifi block 618 words pre-seeded, release 0x5A5A **only**, no
0xCECE during boot), then post **one** H2D SR announce and read back.

| # | observable | pass | fail / meaning |
| --- | --- | --- | --- |
| 1 | page CA 0x1BF000 after release | `0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD` | row differs -> boot did not reach the epilogue |
| 2 | state CA 0x118B60 | 1 | 0 = boot task never ran (PD2 not reached) |
| 3 | exception mask CA 0x40008 | **0 throughout** | 0x10 = data abort (should be impossible in PD2; capture out[1]/page) |
| 4 | page integrity | unchanged after the announce (no clobber) | changes = PD2 fault mode (report frame words) |
| 5 | **out[0] CA 0x40039010** | **0x8 -> 0** | stays 0x8 = poll loop/dispatcher not reached |
| 6 | **out[5] CA 0x400392F0** | **1** (dispatcher ack) | 0 = dispatcher did not run |
| 7 | **out[2] CA 0x400392D4** | **8** (H2D-done doorbell) | 0 = dispatcher did not run |
| 8 | D2H ready pair continues | id1/id2 still decodable, frames keep flowing | stall = the parked boot task harmed the D2H path (should not) |
| 9 | (bonus) D2H response to the announce | only if the live handler table is populated; with the zero-table path none is expected | n/a |

Stop rules: one boot, one H2D frame, read all rows; if rows 5-7 fail, stop and park the chip for
read-only diagnostics (do not retry in the same session). Never write out[5]/out[2]/out[0] from
the host.

### 5.6 Build / deploy

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/pd2_patch_builder.py --selftest            # 38/38 PASS
    $PY $T/pd2_patch_builder.py --recipe              # machine-readable recipe
    $PY $T/pd2_patch_builder.py --build /tmp/hi5622-pd2
    # -> /tmp/hi5622-pd2/FIRMWARE.PD2.bin  sha256 7a6918077af91601f00901ad46b47f5ea36956b03faeda9d742ecc384a1b7728
    #    /tmp/hi5622-pd2/PD2-MANIFEST.txt  (hashes only)
    # deploy as the firmware-loader name hi_wifi/FIRMWARE.PD2.bin (relative to /lib/firmware)

The bench tooling (../wifi-bench-tooling/) can apply/verify PD2 from the recipe in section 8;
PD2 is **independent of PD** (`based_on: null`): it does not reuse PD-1 and it overwrites the
same entry window as PD-2 with a different branch.

---

## 6. Module policy fix for the Lead: latch boot completion

**Observation.** The module's SR-announce gate re-reads the handshake page (`page+8` and/or
page+0 bit14) at announce time. In the PD boot the page was overwritten by the exception frame
(corrupting +8 to 0) and the announce was refused - but the same fragility exists in any patched
or crash-recovered boot, and the exception staging routine legitimately wipes/mutates the page.

**Suggested policy (patched boots and normal boots alike).**

1. After release, run a boot witness that watches the page and the state word:
   * `page+8 == 2` (ROM S1 ACK), **or**
   * `page+0 & 0x4000` (entry A passed the poll), **or**
   * `state CA 0x118B60 == 1 && mask CA 0x40008 == 0` (boot task ran).
2. On the **first** confirmed observation, latch `boot_complete = 1` in module state (single
   atomic write; never cleared until unload or a fresh release). Record the evidence, timestamp
   and a diagnostic snapshot of the page in the sysfs attrs.
3. Base the SR-announce gate and the CECE-write policy on the **latched** flag, not on a live
   page re-read. Keep the live page only as a diagnostic (report it, do not gate on it).
4. Optional hardening: accept the alternates above as equivalent witnesses so one overwritten
   word never turns a successful boot into a refused announce.

Why this is safe: the gate exists to avoid posting H2D before the firmware is ready; a latched
observation is strictly stronger evidence than a re-read, and the page is not a stable protocol
register after boot (firmware writes +4/+8 during ROM/S1 and the exception routine zeroes +8/+C
on crash paths).

---

## 7. Single next bench action

**Fire PD2-A once** (image sha256 `7a691807...1b7728`) with the standard golden-boot procedure,
then post exactly **one** H2D SR announce (`printf 1 > hcc_sr_announce`, no newline) and read the
section 5.5 table. Pass condition: rows 1-7 as specified (mask 0, page intact, out[0] 0x8->0,
out[5]=1, out[2]=8). Do not change any other variable in the same fire; keep the read-only D2H
direct reader running to prove the boot and the D2H path are unaffected.

---

## 8. Machine-readable recipe (bench tooling schema)

    {
      "schema": "hi5622-fw-patch-recipe/1",
      "name": "PD2",
      "about": "post-boot H2D mailbox poll loop: boot epilogue -> out[0] poll -> firmware dispatcher (guarded)",
      "source": {"sha256": "7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b", "len": 928920},
      "based_on": null,
      "segments": [
        {"file": 29746, "len": 62, "source_sha16": "4ab344e16a74f8b4",
         "patched_hex": "0b4f81b03b68002bfcd0094a1368002bf8d07b69084ca34202d0f8f722fff1e7064c7c61f8f71dff7b61ebe700bf9cc1100010900340688d110000200000",
         "isa": "thumb", "expect": "PD2 guard+poll stub (CPU 0x47432..0x4746F)"},
        {"file": 37226, "len": 4, "source_sha16": "c5975cf4bd702f88",
         "patched_hex": "fef762b9", "isa": "thumb",
         "expect": "b.w 0x47432 (boot-task epilogue CPU 0x4916A -> PD2 loop)"}
      ]
    }

(29746 = 0x7432, 37226 = 0x916A; the recipe prints these as decimal, exactly as emitted by
`pd2_patch_builder.py --recipe`.)

---

## 9. Reproduction (host-only)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/pd2_re.py --dispatcher          # dispatcher fields/contract + event-0x4C registration
    $PY $T/pd2_re.py --fault               # PD abort chain + nested ABT frame -> page clobber
    $PY $T/pd2_re.py --cave                # dead-cave proof + neighbours + reference census
    $PY $T/pd2_re.py --selftest            # 33/33 PASS
    $PY $T/pd2_patch_builder.py --list     # segments + source-window hashes
    $PY $T/pd2_patch_builder.py --selftest # 38/38 PASS
    $PY $T/pd2_patch_builder.py --build /tmp/hi5622-pd2

Both tools read the pinned local artifacts in place, refuse a wrong blob, and never write vendor
bytes into this repository.

---

## 10. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | H2D mailbox dispatcher = wrapper CPU 0x40294 (`ldr r0,=0x10C190`) -> CPU 0xC18AC; instance fields +4/+0xC/+0x10 = CA out[0]/ack/out[2], +0x20 = handler table | fresh disasm `pd2_re.py --dispatcher` | [V] |
| E2 | `pcie_msg_init` (CPU 0x49334) installs those CA pointers at CPU 0x49764-0x497A4 and registers/enables event 0x4C->0x40295 (0x497FE-0x49816; enable word CA 0x40161108 bit12) | disasm + literal/arith checks | [V] |
| E3 | hcc instance fields are static zero; no image code writes them before `pcie_msg_init` | whole-image writer scan | [V] |
| E4 | handler table [hcc+0x20] static value 0x118D68 is image code; no image writer found; helper 0xC187A targets a different table ([[0x172130]+0xBC]) | disasm + whole-image scans | [V] / [U] on its runtime population |
| E5 | CPU 0x47610 is the handshake poll retry body, executed on every failed poll before S1's ACK; PD-1 therefore called the dispatcher pre-init | disasm 0x473F0/0x47610 + ROM S1 flow | [V] |
| E6 | first fault = CPU 0xC18B8 `str r7,[r2]` with r2=[hcc+0xC]=0 -> store to CPU 0 -> data abort id 4 -> mask 0x10 | disasm + bench mask | [V]/[V\*] |
| E7 | page clobber = 4th nested ABT frame at 0x1BF000 with frame words {0x10C190,0,0,0x1BEE50} | ROM table + stub arithmetic + bench page read | [V]/[V\*] |
| E8 | the exception staging routine 0xC6E14 (file 0x86E14) writes out[1]=4, wipes CA 0x40000108/0x4000010C, spins for 0xCECE, long-jumps to the caller; it never calls the H2D dispatcher | disasm | [V] |
| E9 | PD2 stub disassembles exactly as designed and the patched image differs only in the two segments | `pd2_patch_builder.py --selftest` | [V] |
| E10 | the PD2 cave CPU 0x47432 is entered only by the config-3 branch (block[+0x24]==3; vendor value 2) and has no literal/u32 references | census + disasm | [V code] / [V\* block] |
| E11 | PD2's expected bench observables: out[0] 0x8->0, out[5]=1, out[2]=8, mask 0, page intact | dispatcher contract + stub semantics | [I high] |

---

## 11. Open items ([U])

1. **O1 - what populates `[hcc+0x20]`?** The static value is an image code address; no image
   writer was found. If a firmware link/event layer populates it after the host HCC handshake,
   PD2's live-table branch would become active automatically. A read-only host read of CPU
   0x10C1B0 (BAR0 = CPU + 0x6B8000 = 0x7C41B0) on a golden boot settles it. This is the single
   most valuable read after the PD2 fire.
2. **O2 - which handler table ids does the host's SR announce use?** The announce writes out[0]
   bit3; the dispatcher index is 3. If the live table is populated, table[3].fn is the H2D
   message consumer. A read of the table (or of the real table pointer) would identify it.
3. **O3 - does the parked boot task harm anything?** PD2 keeps the boot task alive as the poller;
   the vendor lets it return. Row 8 in section 5.5 checks this. If the D2H ready/deposit path
   stalls, the alternative is to run the poller from a different existing periodic context (e.g.
   an idle hook) - not needed unless observed.
4. **O4 - entry-B page+4 bit4 in the PD boot.** It indicates CPU1's ROM dispatched image entry B
   (a warm-reset ROM gate artifact), consistent with the page+4=0x17 reading; it does not affect
   PD2 (which runs after the full boot on CPU0).

---

*Lane wifi-irq-unmask, 2026-11-24, shared task task-91. Host-only static reversal. The vendor
firmware blob and the mask-ROM dump remain local evidence only; this directory keeps addresses,
constants, instruction encodings and hashes, never vendor bytes.*
