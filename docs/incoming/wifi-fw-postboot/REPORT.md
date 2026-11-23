# Post-handshake startup continuation of the Hi5622V100 Wi-Fi firmware: why "the scheduler never arms" is an instrument artifact, and what actually gates the ready pair

**Lane:** `wifi-fw-postboot` (shared task **task-88**), **host-only**, 2026-11-23. The bench
router was never contacted; no image was built; no device was opened; **no vendor firmware
byte, ROM byte or module byte is copied into this directory**. All addresses, constants and
decoded instructions below are derived annotations of local artifacts read in place.

Inputs (local evidence only):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B vendor Wi-Fi firmware; CPU address = file offset + 0x40000 |
| `/home/shibbityshwab/staging-fi/rom16k.bin` | 16 KiB mask boot ROM (sha256 `fcfd464f...2198faa4`), ROM cpu address == dump file offset |
| prior lanes | `wifi-rom-dump`, `wifi-boot-kick`, `wifi-fw-ready-gate`, `wifi-fw-link-event`, `wifi-event-router`, `wifi-hcc-service` (cited, not repeated) |
| transport / service source | `wifi-transport-port/build/hi5622_pciet.h`, `wifi-hcc-service/src/` |

Evidence markers: **[V]** verified here from the local artifacts with `tools/fwdis.py`,
`tools/regmap_probe.py` and the prior-lane `romdis.py`; **[V\*]** device-measured by an
earlier attended lane and recorded in the cited document; **[I]** inference from the code;
**[U]** unknown / not decidable without a device-side read.

Address conventions (project-wide, unchanged): CPU address = file offset + 0x40000 for the
image (image covers CPU 0x40000..0x122C98); ROM cpu address == ROM dump offset; device "CA"
= PCIe address space; host BAR0 for RAM/ROM = 0x6B8000 + CPU for the region-5 ACP alias and
= CPU for the region-0 identity alias; the **IO** window is inbound region 3 = BAR0
0x3B8000..0x4D7FFF ↔ CA 0x40000000..0x4011FFFF only.

---

## 0. TL;DR — the answer in one page

1. **The stated premise is not supported by the evidence: "the scheduler never arms" is an
   artifact of the *instrument*, not a device observation.** Every scheduler/GIC
   "fingerprint" word in `hcc_irq_probe` (and in the earlier `evctrl_probe.py` plan) is read
   with `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)`. CA 0x40160000 is past the end of
   inbound **region 3** (CA 0x40000000..0x4011FFFF), so that macro **wraps every 0x4016xxxx
   CA by -0x100000** and decodes it to the unrelated IO registers at **CA 0x4006xxxx**
   (13/13 probed words do not decode to themselves; `tools/regmap_probe.py`). The zeros the
   probe prints are the values of CA 0x40060100/0x40061100/0x40060600..., not of the GIC.
   Phase 32 already found and retracted this exact class of bug for BAR0 0x519100
   [V, `phase32/irq-block-mapping.md`]; the hcc service re-introduced it for the whole
   0x4016xxxx block. The GIC is **not host-visible through the six vendor inbound regions at
   all** [V].
2. **The handshake page itself proves the GIC was enabled on the measured boot.** ROM stage
   S1 (CPU1) writes **CA 0x40160104 = 0xFF and CA 0x40160100 = 1 at ROM 0x5C4/0x5C8**, and
   only afterwards does its WFI loop pass, write page `+8 = 2` (ROM 0x5FE) and page
   `+4 |= 4` (ROM 0x5FA) [V, `romdis.py flow`]. The measured golden page is
   `+4 = 0x00000007` (bit2 set) and `+8 = 0x00000002` [V\*], so ROM 0x5C8 executed and a
   correctly decoded read of CA 0x40160100 **must** return 1 on that boot. The image also
   sets it again unconditionally in `0xC3024` from entry A (0x46EDA, **before** the
   inter-core handshake) and from entry B (0xC7440) [V].
3. **Entry A's post-spin continuation necessarily arms the scheduler timer.** The only branch
   in entry A between the inter-core handshake and the scheduler is the `[0x1BF008] == 2`
   spin itself (0x473F0..0x47618) [V]. Golden page **bit14 = 0x4000** is written at 0x473FC
   *after* the successful compare [V]; following it the code runs straight-line: bounded
   memset 0xC1E0C, boot-task descriptor, **task create 0xC745C**, **scheduler start 0xC704C**
   [V]. `0xC745C` is a short critical-section task-create (no blocking wait, no host
   dependency) [V]; `0xC704C` arms the timer at 0xC7072-0xC708C **before** it can ever block,
   and its only pre-arm gates are two BUG traps (stack-page word `[sp & ~0xFFF + 4] == 0`
   and CPSR I == 1) [V]. A BUG there would have entered the image exception path and stamped
   the exception mask CA 0x40008; the measured mask is **0** [V\*]. Therefore **the timer
   WAS armed and event 0x1D WAS enabled on the boot whose golden state was measured**
   (the scheduler does not need any host transition).
4. **What the observable state actually is:** the boot task's state byte CA 0x118B60 = 1 and
   the one-shot slots CA 0x17D42C / CA 0x1808DC / [0x117C00+0xEEC] stamped with BUG stubs
   0xC19D1 / 0xC19E3 / 0xC19D7 [V\*, V] prove the boot state machine **ran pass 1**: it
   advanced state 0 -> 1 at 0x48174, built the per-state HCC objects, created its per-state
   worker task, registered the H2D id-1 handler 0x40511 and event-0x46 handler 0x40525, and
   then jumped to 0x47D28 to build and send HCC group-4 id-1 through the send primitive
   0xC2760 [V]. That is exactly "state = 1" in the vendor state machine
   [V, `wifi-fw-ready-gate` section 2.2].
5. **Therefore the missing transition is downstream of the scheduler**: the boot state
   machine's pass 1 -> ready pair progression, and the D2H deposit/notify path. The host
   observes D2H notify **bit6 (0x40, "deposit notify")** appearing [V\*], which is written
   only by the firmware's `d2h_notify` (0xC6170, `out[1] |= 1<<bit` at 0xC619A-0xC61C0)
   after a D2H node deposit [V]; i.e. the firmware **did** make a deposit and notified the
   host, but the host did not decode a g4 id-1/id-2 frame from it. The open question is a
   DR/host-ring receive-contract or state-machine phase question, not an IRQ-enable or
   scheduler question. No host write can arm a scheduler that is already armed.
6. **Boot-slot / FIQ slot CA 0x17D42C:** the value in the slot is a **BUG trap stub**
   (0xC19D0: `movs r3,#0; str r3,[r3]; udf #0xff`). The slot is the **FIQ dispatcher's
   one-shot entry hook**: ROM FIQ vector -> ROM stub 0x7C -> ROM slot table 0x1E0 ->
   image header +0x50 = 0x103140 -> ARM FIQ trampoline -> Thumb dispatcher **0xC2EEC**
   (`r3 = [0x17D398+0x94]; if (r3) bx r3; else bx lr`) [V]. The **only** write to CA
   0x17D42C anywhere in the image is the boot task stamping 0xC19D1 in its one-shot-slot
   disarm block (0x47CF6-0x47D08, gated on the H2D id-5 registration returning 0) [V]; a
   whole-image 32-bit scan finds no occurrence of 0x47629/0x47624/0x47628 and only one
   0x47625 (entry A's task descriptor literal at 0x4751C) [V]. So nothing in the image ever
   arms that slot with a code pointer, and the FIQ path is a no-op in this image. It is
   **not** the IRQ/FIQ unmask or scheduler kick path.
7. **FIQ never should have fired and never did:** the ROM release context leaves F=1, the
   image contains **six `cpsie i` sites and zero `cpsie f` / `msr cpsr_c` sites** [V], so no
   image context can ever unmask FIQ. Entry A's `CA 0x40161F00 = 0x00020000` is the
   **inter-core software interrupt targeting CPU1** (`(1<<core)<<16`), the wake for ROM S1's
   WFI loop -- not a FIQ to CPU0 [V]. Exception mask = 0 says no exception (and no FIQ handler
   run) was taken [V\*].
8. **Host action:** none can supply the scheduler arm (nothing is missing there). The single
   useful host-side action is a **diagnostic**: a read-only **7th endpoint-iATU viewport**
   mapping a free BAR0 window to target CA 0x40160000 (e.g. BAR0 0x8D0000..0x8DFFFF ->
   CA 0x40160000, size 0x10000) so the GIC state can finally be read; the service's current
   CA -> BAR0 translation must not be used for CA >= 0x40120000. Any further conclusion about
   the event layer from the current probe is invalid.

---

## 1. Method and tools

* `tools/fwdis.py` - the image disassembly helper used for every address in this report.
  Capstone-based, resolves Thumb `ldr` PC-literals and prints the word at the literal
  address, plus whole-image 32-bit value search, "code sites that ldr a literal equal to X"
  (`lrefs`, the main writer/target census primitive), literal-pool dump and call-target
  listing. It reads the local blob in place.
* `tools/regmap_probe.py` - the **instrument audit**: for each CA in the hcc IRQ probe it
  computes `HI5622_BAR0_IO(ca)`, finds the inbound region that BAR0 offset really belongs
  to, decodes the real target CA, and flags every CA that does not decode to itself. It also
  prints the ROM S1 ordering cross-check that makes the golden handshake page prove
  CA 0x40160100 == 1.
* Prior-lane tools reused verbatim for cross-checks: `wifi-rom-dump/tools/romdis.py`
  (`flow`, `stores`, `dis`) and the region table in
  `wifi-transport-port/build/hi5622_pciet.h` (six inbound regions; `HI5622_BAR0_IO`).
* Negative results are full-image censuses, not grep misses: `fwdis.py find` scans every
  even file offset for a u32, `fwdis.py lrefs` scans the whole Thumb stream with capstone
  including data (`skipdata`) for PC-literal loads resolving to the value.

---

## 2. The instrument bug: the scheduler "fingerprint" reads are not reads of the GIC

The hcc service reads device CAs with

    static u32 hcc_io_read(struct hi5622_pci *ctx, u32 ca)
    { return readl(ctx->bar0 + HI5622_BAR0_IO(ca)); }

with `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` [V, transport header]. The inbound
region table actually programmed by the vendor (and by our transport) is:

| region | BAR0 window | size | device CA target |
| ---: | --- | ---: | --- |
| 0 | 0x0000000-0x001BFFF | 0x01C0000 | 0x00000000 |
| 1 | 0x001C000-0x001D7FF | 0x0018000 | 0x00400000 |
| 2 | 0x001D800-0x03B7FFF | 0x01E0000 | 0x01000000 |
| 3 | 0x03B8000-0x04D7FFF | 0x0120000 | 0x40000000 (**CA 0x40000000..0x4011FFFF only**) |
| 4 | 0x04D8000-0x06B7FFF | 0x01E0000 | 0x02000000 |
| 5 | 0x06B8000-0x08CFFFF | 0x0218000 | 0x01200000 |

[V, `phase18/inbound-map.md` A.6 = transport header = vendor `g_shuangta_region_types`]

CA 0x40160100 is 0x40100 **past** the end of region 3. The mask in `HI5622_BAR0_IO` keeps
only the low 20 bits, so:

| intended CA | macro offset | region | really decodes to |
| ---: | ---: | ---: | ---: |
| 0x40160100 | 0x418100 | 3 | **0x40060100** |
| 0x40160104 | 0x418104 | 3 | 0x40060104 |
| 0x40160108 | 0x418108 | 3 | 0x40060108 |
| 0x4016010C | 0x41810C | 3 | 0x4006010C |
| 0x40160110 | 0x418110 | 3 | 0x40060110 |
| 0x40161100..0x4016128C | 0x4181xx/0x419xxx | 3 | 0x400611xx / 0x400612xx |
| 0x40160600/08/0C | 0x418600/08/0C | 3 | 0x40060600/08/0C |

[V, `tools/regmap_probe.py`: 13/13 probed CAs do not decode to themselves]. The earlier
`evctrl_probe.py` plan used a different arithmetic slip (`0x3B8000 + (CA - 0x40000000)`
without the mask): for CA 0x40161100 that yields BAR0 0x519100, which is **region 4** ->
device CA 0x02041100 (ACP SRAM). Either way the reads are not the GIC. Phase 32 documented
and retracted exactly this mistake for BAR0 0x519100 [V, `phase32/irq-block-mapping.md`];
the GIC block at CA 0x40160000 is not reachable through the six vendor inbound regions.

**Consequence:** every "absent scheduler fingerprint" conclusion drawn from those words -
event-controller enable 0, dispatch vector/EOI 0, enables 0, latched raises 0, timer 0 - has
no device evidence behind it. The same applies to the earlier lane-plan writes to "CA
0x40161188 via BAR0 0x519188" (region 4 ACP) or "0x418188" (CA 0x40061188, an unrelated IO
register); neither is the GIC distributor.

---

## 3. Static proof that the measured golden boot DID enable the GIC and DID arm the timer

### 3.1 ROM S1 enables the GIC before it writes the handshake ACK

`romdis.py flow` (read from the local ROM dump; no bytes reproduced) [V]:

    S0c CPU1/S1 at ROM 0x564:
      0x5C4  [CA 0x40160104] = 0xFF     ; GIC CPU-interface PMR (or equivalent)
      0x5C8  [CA 0x40160100] = 1        ; GIC CPU-interface CTLR enable
      0x5D2  [0x1BF004] |= 2            ; CPU1 armed
      0x5E2..0x5EE  WFI loop until [0x1BF00C] == 0xABCD && [0x4000010C] == 0xCEED
      0x5F0..0x5FE  [0x1BF004] |= 4 ; [0x1BF008] = 2 ; blx image header +0x04 (entry B)

The measured golden handshake page is `CA 0x1BF000 = 0x86807FEF / 0x00000007 / 0x00000002 /
0x0000ABCD` [V\*]. `+4 = 7` has bit2 set, and bit2 is written only at ROM 0x5FA/0x5FE in
the S1 success path [V]; `+8 = 2` is the ROM 0x5FE ACK [V]. Therefore ROM 0x5C4/0x5C8 - which
precede the WFI - executed, and **CA 0x40160100 must read 1, CA 0x40160104 must read 0xFF,
on that very boot**. (The image's entry A also sets them again via 0xC3024, unconditionally,
at 0x46EDA, which is before the handshake.)

### 3.2 Entry A's post-spin continuation has no gate before the timer arm

Entry A = CPU 0x46970. The handshake block [V, disassembly of 0x473CC-0x47620]:

    0x473CC  r4 = 0x1BF008 ; 0x1BF00C = 0xABCD ; [0x1BF008] = 0
    0x473D8  cache maintenance (blx 0x102EE4) ; dmb
    0x473E0  CA 0x40161F00 <- 0x00020000        ; inter-core wake, target core 1
    0x473E8  isb
    0x473EC  r6 = r4 + 8
    0x473F0  r3 = [r4] ; cmp r3,#2 ; bne 0x47610   ; <-- the only branch
    0x473F8  (page CA 0x1BF000) |= 0x4000           ; bit14 "handshake complete"
    0x47402  memset(sp+4, 20, 0) via bounded helper 0xC1E0C
    0x47410  descriptor: {+0 name=0x00104243, +4 fn=0x00047625, +8 = 0, +0x14 kind = 7}
    0x47424  bl 0xC745C        ; RTOS task create (boot task)
    0x47428  bl 0xC704C        ; RTOS scheduler start (arms timer, enables event 0x1D)
    0x4742C  epilogue (only if 0xC704C ever returns)

    0x47610  (fail path) cache maintenance (blx 0x102E68) ; b 0x473F0  ; infinite poll

Golden page bit14 = 1 [V\*] means the 0x473F8 store executed, i.e. the spin passed. From
0x473F8 to the timer-arm instructions there is no conditional branch [V].

### 3.3 0xC745C cannot block the way to the scheduler

`0xC745C` [V, disasm 0xC745C-0xC74A6]: critical-section enter 0xC2394 (mrs APSR / cpsid i /
preempt count / task spin-lock 0xC2354, all bounded), read the free-task-list head 0x17E3F0,
`0xC23B2` unlink, critical-section exit 0xC2730 (may run a bounded reschedule via 0xC26E0
but returns), task init 0xC7218 (validation + RAM writes only), make-ready 0xC2B58. It issues
no message, touches no device register, waits on nothing host-visible.

### 3.4 0xC704C arms the timer before any scheduling decision

`0xC704C` = scheduler start [V, disasm 0xC704C-0xC70F8]:

    0xC704E-0xC705A  gate 1: r2 = [ (sp & ~0xFFF) + 4 ] ; if r2 != 0 -> BUG trap (0xC705C)
    0xC706A-0xC7070  gate 2: mrs APSR ; if I == 0 (IRQ enabled) -> BUG trap (0xC705C)
    0xC7072  r1 = 1 ; CA 0x40160608 <- 0        ; stop
    0xC707A  CA 0x4016060C <- 1                 ; start/arm
    0xC7080  CA 0x40160600 <- 0x0061A800        ; reload
    0xC7086  CA 0x40160604 <- 0x0061A800        ; reload
    0xC708A  CA 0x40160608 <- 7                 ; control
    0xC708C  bl 0xC6FF4 (r0 = 0x1D)             ; enable event 0x1D  -> CA 0x40161100 |= 0x2000
    0xC7090  bl 0xC2382/cpu_id ; r4 = 0x173060 + 0xCC*core   ; per-core scheduler object
    0xC709E  cpsid i ; lock 0xC2354 ; next-task 0xC2324
    0xC70B0  if (task) dispatch via 0xC2442/0x103B60 (never returns to 0xC704C)
    0xC70B2  else idle 0xC2B7A: cpsie i ; b 0xC26E0   ; <-- the only pre-task IRQ unmask

The arming instructions are straight-line before the first task decision. The only way to
skip them after entering 0xC704C is a BUG trap, which would enter the image exception path
(exception mask CA 0x40008) and park in the 0x86E14 0xcece routine; the measured mask is
**0** [V\*]. So on the measured golden boot the timer was armed and event 0x1D enabled.

### 3.5 CPU1 runs the same scheduler start via entry B

ROM S1 dispatches image header +0x04 = **entry B CPU 0xC742C** on CPU1 after writing +8=2
[V, ROM decode]. Entry B [V, disasm]:

    0xC742C  [0x1BF004] |= 0x10
    0xC7438  0xC2082 (per-stack-page flag clear) ; 0xC2020 (CP15/cache setup)
    0xC7440  0xC3024 (event-controller init: CA 0x40160100 = 1 again, priorities, banks)
    0xC7444  0xC2098 (cpu id)
    0xC7448  0xBEDD8(3)
    0xC744E  0xC73A0   ; create one per-core "main task": fn 0x000C6A09, stack
                       ; (0x1BE - core) << 12 (core0 0x1BE000, core1 0x1BD000), size 0x1000
    0xC7452  pop ; b 0xC704C

So both cores enter the scheduler-start path; CPU0's entry (0x47428) and CPU1's entry
(0xC7456) are the two call sites of 0xC704C [V].

---

## 4. The startup state machine map (addresses, functions, gates)

### 4.1 Cold boot to the handshake (context)

    ROM S0 CPU0 0x8C -> 0x54C/0x50C cold path
      -> host 0x5A5A release -> CA 0x4000010C = 0xCEED -> blx entry A (CPU 0x46970)

    entry A (CPU0, Thumb):
      0x46970 .. 0x46E00   early SoC/clock glue, boot globals, RTOS object init
      0x46E20 .. 0x46ED4   dispatcher object 0x17D398 init: handler table 0x17D430
                           [0]=0xC2081, [1]=0xC20A3, [0x1D]=0xC6C89; CA 0x40161004 read,
                           coreword (cores+1)<<5 stored at [0x17D398]; CA 0x40161000 = 1;
                           banks 0x40161180/0x40161380 masked (-1); CA 0x40161C00 = 0
      0x46EDA              bl 0xC3024   <-- GIC CPU-interface + distributor init (section 5)
      0x46EF2 .. 0x47380   soft-uart/debug, boot globals, RTOS task-table init:
                           - 0x17E3F0  : 9-task control-block array (0x238 stride),
                                         stacks 0x1BC000 - n*0x1000, lock 0x17F7F0
                           - 0x173060  : per-core scheduler objects (0xCC stride)
                           - 0x17D3D8  : system lock
      0x47380 .. 0x473C8   CPU/core bookkeeping, boot page progress bits, lock 0x118B24
      0x473C2              page CA 0x1BF000 |= 0x2000      (bit13 "handshake requested")
      0x473CC .. 0x473F0   post handshake request: [0x1BF008]=0, [0x1BF00C]=0xABCD,
                           cache flush, CA 0x40161F00 = 0x00020000 (wake CPU1), isb
      0x473F0 .. 0x47618   poll [0x1BF008] == 2 (ROM S1 ACK), fail path = infinite poll
      0x473F8              page |= 0x4000 (bit14)
      0x47410 .. 0x47428   boot-task create + scheduler start (section 3.2)

    ROM S1 CPU1 0x564:
      clear +4/+C, GIC words, arm, WFI on {+C == 0xABCD, CA 0x4000010C == 0xCEED}
      -> +4 |= 4, +8 = 2, blx entry B (CPU 0xC742C) -> 0xC3024 + task create + 0xC704C

### 4.2 The four roles inside the RTOS start

| role | function | what it does | gate |
| --- | --- | --- | --- |
| task create | `0xC745C(desc)` | critical section, take a task control block off 0x17E3F0, `0xC7218` init from descriptor, `0xC2B58` make ready; returns the task object | none; needs the entry-A task-table init |
| scheduler start | `0xC704C` | two BUG gates (stack-page word + I bit), arms timer 0x40160600/04/08/0C, enables event 0x1D via 0xC6FF4, picks per-core scheduler object 0x173060 + 0xCC*core, dispatches a ready task or falls into idle | must be entered from a context with preempt-count 0 on its stack page and I=1 |
| idle | `0xC2B7A` | `cpsie i ; b 0xC26E0` | entered only when no ready task |
| dispatch/reschedule loop | `0xC26E0` | nesting counter on stack page, reschedule when [page] != 0 and I == 0, yield `0xC24DE` | per-stack-page flags [page],[page+4],[page+8],[page+0xC] |

The per-stack-page control block (`sp & ~0xFFF`) is the RTOS anchor: `+0` reschedule
request, `+4` preemption nesting, `+8`/others counters, `+0xC` current task. This is why
`0xC704C`'s first gate reads `[sp & ~0xFFF + 4]`: "scheduler must be started from a
non-nested, non-IRQ context" [I].

### 4.3 The boot state machine (fn CPU 0x47624, runtime 0x118B60 state byte)

| object | address | note |
| --- | --- | --- |
| boot-task entry | CPU 0x47624 (Thumb ptr 0x47625) | descriptor literal at 0x4751C |
| global message object | CPU 0x117C00 | `r4` in the function |
| state byte | CA 0x118B60 = [r4+0xF60] | 0 -> 1 -> 2 ... |
| per-state records | CA 0x118B64 + 0x84*state | descriptor data for the per-state worker |
| state lock | CA 0x118B24 | 0x118B60 access lock |
| TCM stash | CA 0x18179C <- 0x004166B0 | first instructions of the task (TCM object) |
| one-shot slot stamps | CA 0x1808DC = 0xC19E3; [0x117C00+0xEEC] = 0xC19D7; CA 0x17D42C = 0xC19D1 | one-shot-dispatch disarm, gated on H2D id-5 registration result |
| state check | 0x47D0A-0x47D18: `ldrb r6,[r4+0xF60]; cmp r6,#1; bls 0x48160` | state <= 1 -> setup branch |
| setup branch | 0x48160-0x4828A | `[r4+0xF60] = state+1` at 0x48174; memset/init state record; builds HCC callback/thread objects [r4+0x214..0x250] and list heads [r4+0x270..0x27C]; creates per-state worker task via 0xC745C (0x481C0, fn 0x000C6D57); registers H2D id-1 handler 0x40511 via 0xBC1DC(1) at 0x48222; registers event 0x46 handler 0x40525 via 0xC6DA2 at 0x4821A; then `b 0x47D28` |
| id-1 frame | 0x47D28-0x47D54 | `msg_alloc(0,6)`, header[0]=4 (HCC group 4), header[6]=1, source core, `bl 0xC2760` (send) |
| send continuation | 0x47D58 | if send returns 0 -> 0x49188 (builds and sends a group-4 id-3 exception-info frame), then 0x4782E; else free msg, 0x4782E; runs on to the common epilogue at 0x49196 |
| later phases (same invocation) | 0x4782E .. 0x49196 | includes the id-2 "DEVICE READY" builder at 0x8FD6-0x9160, gated on internal [sp+0x24]==2 and [0x1ADB80+0x110]==0 [V, ready-gate] |

**Does the state-1 pass return into the scheduler or block?** It does **not** block waiting
for a host message: the setup branch contains only bounded lock/init/registration calls, and
the id-1 send primitive 0xC2760 has no long wait (short locks + queue enqueue; ready-gate
section 3 and boot-kick section 18.2) [V]. It jumps to the id-1 build immediately. The
function's later phases run in the **same** invocation; there is no branch back to the state
check (whole-image scan: the only reference to 0x47D0A besides its own block is the 0xC7CF4
conditional at function entry) [V]. So state = 1 is a **transient of pass 1**, not a parking
state, and the path to the ready pair is: pass 1 builds + sends id-1; the send deposits a
D2H node and `d2h_notify(obj,6)` publishes out[1] bit6; the host must consume the deposit
and the firmware must reach the id-2 phase in the same/next dispatch.

Observation vs this model: the measured state is 1, the one-shot slots are stamped (pass 1
ran) and D2H notify bit6 appeared (the deposit path ran) [V\*]. So the firmware got through
the id-1 enqueue/deposit; the missing piece is the host-side DR/HCC receive/decode (or the
id-2 phase gate), not the scheduler.

---

## 5. The event-controller init (who writes CA 0x40160100 = 1 and the enable regs)

`0xC3024` - full body [V, disasm 0xC3024-0xC3090]:

    CA 0x40161380 = -1
    CA 0x40161400 + 0x00,0x04,...,0x9C (= bytes +0x80.. from 0x40161380) = -1  ; priority/group pad
    CA 0x40160104 = 0xFF            ; CPU-interface priority mask
    CA 0x40160108 = 3               ; CPU-interface config/binary point
    0xC1A3C(id=0, level=0) ; (id=1, 0xE) ; (id=2, 0) ; (id=0x1D, 0xE)   ; priorities
    CA 0x40160100 = 1               ; <-- CPU-interface CONTROL enable (0xC308A-0xC308E)

It is called **unconditionally** by entry A at 0x46EDA (before the inter-core handshake) and
by entry B at 0xC7440; nothing gates it on a host value, a message or a flag [V]. ROM S1
pre-writes CA 0x40160104/0x40160100 even earlier (section 3.1) [V].

Per-id enables are the separate W1S write in `0xC6FF4(id)`: `CA 0x40161100 + 4*(id>>5) |=
1<<(id&31)` for ids 0x10..0x5F; the scheduler calls it for id 0x1D (0xC708C) [V]. The other
expected enable bits in the probe (0x2D/0x2E/0x3E/0x3F/0x4C/0x4E) are written by
`pcie_msg_init` (inside the boot task's module table entry, CPU 0x49334) and by entry A's
registrations — all of which are downstream of the same pass 1 that is proven to have run
[V, ready-gate/event-router]. Whether those specific bits are set is **not readable from the
host** with the current six-region map; it requires the diagnostic viewport of section 7.

---

## 6. FIQ / wake cross-check (task item 6)

* **Chain:** ROM vector table FIQ -> ROM 0x7C -> the 5-word ROM slot table at ROM 0x1E0 maps
  the FIQ stub to image header word +0x50 = **0x103140** -> ARM FIQ trampoline (srsdb/rfeia)
  -> Thumb **0xC2EEC**: `r3 = [0x17D398+0x94]; if (r3) bx r3; else bx lr` [V, romdis +
  image].
* **Slot CA 0x17D42C = 0x17D398+0x94.** Its only image writer is the boot task's one-shot
  disarm block at 0x47CF6-0x47D08: if `0xBC1DC(5) == 0` then
  `CA 0x1808DC <- 0xC19E3; [0x117C00+0xEEC] <- 0xC19D7; CA 0x17D42C <- 0xC19D1` [V].
  `0xC19D0` is a BUG trap (`movs r3,#0; str r3,[r3]; udf #0xff`, the same stub shape as
  0xC19D6 and 0xC19DC) [V]. A whole-image u32 scan contains no 0x00047629/0x00047624/
  0x00047628 anywhere and only one 0x00047625 (entry A's descriptor) [V]; no other code site
  loads the literal 0x17D42C [V]; the ROM store census (`romdis.py stores`) has **no** write
  to 0x17D42C [V]. So neither the image nor the readable ROM ever arms the slot: it is a
  one-shot dispatch hook whose pre-boot value (if any) comes from outside the readable
  image/ROM [U], and after pass 1 it is a trap.
* **Should a FIQ have fired?** No. The ROM release context runs with F=1; the whole image
  contains **six `cpsie i` sites (0xC26C0, 0xC2B80, 0xC2F46, 0x120A32, 0x120A60, 0x121BBC)
  and zero `cpsie f` / `msr cpsr_c` sites** [V]. No image context can unmask FIQ, and no
  image code raises a FIQ source (the GIC timer is event 0x1D = IRQ path; the only
  CA 0x40161F00 writes are the inter-core SW interrupt targeting the other core: entry A
  0x00020000, exception handler 0x00010002/0x00020002) [V]. Exception mask CA 0x40008 = 0
  [V\*] is consistent: no exception, no FIQ handler run.
* **So entry A's CA 0x40161F00 = 0x20000 is not a self-FIQ.** It is `(1<<1)<<16` = the
  inter-core doorbell that wakes CPU1's ROM S1 WFI loop (ROM 0x5E2-0x5EE) [V]. It is the
  handshake wake, not the scheduler kick; the scheduler is started by direct calls at
  0x47428 / 0xC7456.

---

## 7. What is actually missing, and the one useful host action

### 7.1 Corrected conclusion

* The scheduler arm / IRQ unmask layer is **not** the missing transition: static code order
  plus the measured handshake page and exception mask say it ran on the measured boot.
* The remaining gap is between the boot state machine's pass 1 (state 1, id-1 frame sent)
  and the host observing the HCC group-4 ready pair (id 1 <= 2.0 s, id 2 <= 20.0 s). Evidence
  that the firmware's D2H deposit path ran: notify **bit6** observed [V\*]. Leading
  candidates, in order:
  1. **DR / HCC receive contract on the host** - the deposit notified but the host did not
     decode a g4 frame (host ring geometry, outbound-window address, or channel/id mapping).
     This is host-side (wifi-hcc-service / DR receive path), not firmware startup.
  2. **The id-2 phase gate inside the same state machine invocation** - internal gates
     `[sp+0x24]==2` (0x8FD6) and `[0x1ADB80+0x110]==0` (0x8FE4/0x8FF0) [V, ready-gate].
     Both are firmware-internal flags; no host write can supply them.
  3. **A D2H publish ordering stall** - previously observed armed/pending = 0x40 and out[1]
     stuck behind the 0x4 announce (boot-kick section 18.4); the C1c test of full D2H
     ack/drain/re-arm was negative, so this is now lower probability [V\*].
* There is **no host action that "starts" the scheduler**: it is started from firmware code
  paths with no host input, and every host-visible precondition on those paths is already
  satisfied in the measured state.

### 7.2 The one host action worth taking next (diagnostic only, read-only)

Map the GIC block **once**, read-only, with a **7th endpoint-iATU viewport** instead of
reusing `HI5622_BAR0_IO`:

    viewport 6:  ctrl2    = 0x80000000 | (0 << 8)     ; enable, BAR0
                 base_lo  = 0x008D0000                ; free BAR0 window (region 5 ends 0x8CFFFF)
                 limit_lo = 0x008DFFFF                ; size 0x10000
                 target_lo= 0x40160000                ; the GIC block CA
                 target_hi= 0
    read-only:   CA 0x40160100 (+0x00) .. (+0x1ffff) via BAR0 0x8D0000 + offset

Expected on a healthy vendor-exact boot: 0x40160100 = 1, 0x40160104 = 0xFF, 0x40160108 = 3,
0x40161100 bit13 = 1 (event 0x1D enable = 0x2000), 0x40160600 = 0x0061A800, 0x40160608 = 7,
0x4016060C = 1. If those read nonzero, the scheduler/timer question is closed forever and the
lane moves to the DR/HCC receive contract. If they read zero **through a correct viewport**,
then the scheduler really did not run and the next question is the two BUG gates of 0xC704C
(stack-page word value, I bit) - but that is not the current evidence.
Do **not** write any 0x4016xxxx register from the host; the GIC is the device CPU's own
interrupt controller.

Note for the hcc service maintainer: `hcc_irq_probe` and the write plan in
`wifi-event-router/REPORT.md` section 7 (BAR0 0x519188/0x519288) both hit the wrong target;
they should be replaced by the viewport above before any further IRQ-layer conclusion or
write.

### 7.3 Does any host-visible action exist for the state machine?

Per the static paths: no. `state` advances only from firmware writes (0x48174) and the
setup branch's dispatch hooks are internal RTOS objects (H2D id-1 handler 0x40511,
event-0x46 handler 0x40525, per-state worker fn 0xC6D57). The only host-visible input the
firmware consumes after boot is the mailbox (out[0] bitmap + out[2] doorbell) whose handler is
registered in this same pass 1 - the host can send that, but it is the *post-ready* HCC
traffic, not a ready-pair precondition, and the suite's H2D negative (wifi-hcc-link) plus the
C1c D2H-service negative bound this. So the honest statement is: **the remaining gate is
firmware/DR-side (state-machine phase advance + D2H deposit decode); the scheduler is not the
gate, and the probe that said it was is invalid.**

---

## 8. Corrections to prior lanes

| prior claim | correction | evidence |
| --- | --- | --- |
| "the scheduler/event-layer fingerprint is absent: CA 0x40160100=0, 0x40161100/04/08=0, 0x40161188/0x40161288=0, 0x40160600/08/0C=0 (hcc_irq_probe)" | those reads use `HI5622_BAR0_IO`, which for CA >= 0x40120000 wraps by -0x100000; they read CA 0x4006xxxx IO registers, not the GIC. The GIC is not host-visible via the six vendor regions. | section 2, `tools/regmap_probe.py`; phase32 retraction |
| "the image scheduler 0xC704C never armed the timer -> no context executed cpsie i" | not proven and contradicted by the measured state: ROM S1 wrote CA 0x40160100=1; entry A's post-spin path has no gate before the timer arm; a BUG gate would have set exception mask 0x40008 (measured 0). | sections 3, 7 |
| event-router write plan: "write 0x1000 to CA 0x40161188 via BAR0 0x519188 / 0x519288" | wrong target: BAR0 0x519188 is region 4 -> CA 0x02041188 (ACP SRAM); the GIC needs the diagnostic viewport. | section 2 |
| "ROM arms the reset/FIQ dispatch slot CA 0x17D42C with 0x47629" | no such value or writer exists in the image or the readable ROM; the only write is the boot task's BUG-stub disarm. Pre-image arming remains [U]. | section 6 |
| "the boot task ran from the ROM-armed reset slot / FIQ path" | the boot task can only be dispatched through the RTOS task created by entry A at 0x47424 (single 0x47625 descriptor literal; FIQ cannot fire because F is never unmasked). That dispatch implies the scheduler ran. | sections 3.3, 6 |
| "entry A spins forever on +8==2 in the current state" | superseded by the corrected release-only protocol: the measured golden page has +8=2 and bit14 set, so the spin passed. | [V], [V\*] |

---

## 9. Evidence table (new claims of this lane)

| # | claim | artifact / method | grade |
| ---: | --- | --- | --- |
| E1 | `HI5622_BAR0_IO` masks CA to 20 bits; all 13 CA 0x4016xxxx reads in `hcc_irq_probe` decode to CA 0x4006xxxx, not the GIC; the GIC is outside all six inbound regions | `tools/regmap_probe.py`; transport header macro + phase18 A.6 region table | [V] |
| E2 | ROM S1 writes CA 0x40160104=0xFF, CA 0x40160100=1 at ROM 0x5C4/0x5C8 before page +4 bit2 (0x5FA) / +8=2 (0x5FE); golden page +4=7/+8=2 implies those writes happened | `romdis.py flow` + measured page [V\*] | [V]/[V\*] |
| E3 | 0xC3024 unconditionally sets CA 0x40160100=1, 0x40160104=0xFF, 0x40160108=3, priority bank, ids 0/1/2/0x1D; called by entry A 0x46EDA and entry B 0xC7440 | `fwdis.py dis 0xC3024` | [V] |
| E4 | Entry A post-spin continuation 0x473F8-0x4742E: page bit14, bounded memset 0xC1E0C, descriptor {0x104243, 0x47625, kind 7}, 0xC745C, 0xC704C; no conditional branch between spin exit and scheduler call | `fwdis.py dis 0x473CC 0xE0` | [V] |
| E5 | 0xC745C = non-blocking RTOS task-create (critical section, free-list 0x17E3F0, 0xC7218 init, 0xC2B58 ready) | `fwdis.py dis 0xC745C/0xC7218/0xC2394/0xC2B58` | [V] |
| E6 | 0xC704C arms timer CA 0x40160600=0x0061A800, 0x40160604=0x0061A800, 0x40160608=7, 0x4016060C=1 and enables event 0x1D (0x40161100 bit13) before any task decision; pre-arm gates are two BUG traps | `fwdis.py dis 0xC704C 0x180` | [V] |
| E7 | 0xC704C then enters dispatch (0xC2324/0xC2354/0xC2442/0x103B60) or idle 0xC2B7A (`cpsie i; b 0xC26E0`); per-core scheduler object CA 0x173060 + 0xCC*core | `fwdis.py dis 0xC704C, 0xC2B7A, 0xC26E0` | [V] |
| E8 | image FIQ is never unmasked: six `cpsie i`, zero `cpsie f`/CPSR writes | whole-image scan (`grep` of capstone disassembly) | [V] |
| E9 | FIQ chain ROM 0x1C -> 0x7C -> ROM slot table 0x1E0 -> image header+0x50=0x103140 -> 0xC2EEC -> bx [0x17D398+0x94] | `romdis.py` map + `fwdis.py dis 0xC2EEC` | [V] |
| E10 | CA 0x17D42C: only writer in image is the boot-task one-shot stamp at 0x47D08 (0xC19D1); no 0x47629/0x47624/0x47628 anywhere; only one 0x47625 (0x4751C); no ROM store | `fwdis.py find/lrefs`; `romdis.py stores` | [V] |
| E11 | 0xC19D0/0xC19D6/0xC19DC are BUG traps; the 0x47CF6-0x47D08 block stamps CA 0x1808DC<-0xC19E3, [0x117C00+0xEEC]<-0xC19D7, CA 0x17D42C<-0xC19D1, gated on H2D id-5 registration | `fwdis.py dis 0x47CE0 0x60` | [V] |
| E12 | boot state machine: entry 0x47624, state byte CA 0x118B60, state check 0x47D0A, setup branch 0x48160 (state+1 at 0x48174, worker create 0x481C0, H2D id-1 reg 0x48222, event 0x46 reg 0x4821A), id-1 build/send 0x47D28/0x47D54, continuation 0x4782E..0x49196; no branch back to the state check | `fwdis.py dis` windows + full-image branch scan | [V] |
| E13 | entry B CPU 0xC742C: 0x1BF004|=0x10, 0xC2082, 0xC2020, 0xC3024, 0xC73A0 (per-core main task fn 0xC6A09, stack 0x1BE000-0x1000*core), tail b 0xC704C | `fwdis.py dis 0xC742C 0x30; 0xC73A0 0xC0` | [V] |
| E14 | the only host-visible evidence of the state machine's progress (page bits 13/14, +4 bits 0/1/2, +8=2, state=1, three stamped one-shot slots, mask=0) is consistent only with "entry A post-spin + scheduler start + boot-task pass 1 ran"; the scheduler-arm claim must be verified through a correct GIC viewport, not the current probe | E2-E12 + [V\*] | [I high] |

---

## 10. Reproduction (host-only; local blob only; nothing vendor committed)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-fw-postboot/tools

    # instrument audit - the headline result
    $PY $T/regmap_probe.py

    # entry A post-handshake continuation
    $PY $T/fwdis.py dis 0x473CC 0xE0
    # task create and scheduler start
    $PY $T/fwdis.py dis 0xC745C 0x60
    $PY $T/fwdis.py dis 0xC704C 0x180
    # event contract init
    $PY $T/fwdis.py dis 0xC3024 0xA0
    # FIQ dispatcher and BUG stubs
    $PY $T/fwdis.py dis 0xC2EEC 0x20
    $PY $T/fwdis.py dis 0xC19C0 0x40
    # slot owner census
    $PY $T/fwdis.py lrefs 0x0017D42C
    $PY $T/fwdis.py find 0x00047629
    $PY $T/fwdis.py find 0x00047625
    # boot state machine
    $PY $T/fwdis.py dis 0x47CE0 0x60
    $PY $T/fwdis.py dis 0x48140 0x160
    $PY $T/fwdis.py dis 0x47620 0x50
    # entry B / CPU1 parallel path
    $PY $T/fwdis.py dis 0xC742C 0x30
    $PY $T/fwdis.py dis 0xC73A0 0xC0
    # CPSR usage census (cpsie i sites; no cpsie f)
    $PY - <<'EOF'
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
    data = open("/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin","rb").read()
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB); md.skipdata = True
    for ins in md.disasm(data, 0x40000):
        if ins.mnemonic in ("cpsie", "cpsid", "msr"):
            print(hex(ins.address), ins.mnemonic, ins.op_str)
    EOF

    # ROM side (prior lane tool, reads the local ROM dump in place)
    $PY opensource/docs/incoming/wifi-rom-dump/tools/romdis.py flow
    $PY opensource/docs/incoming/wifi-rom-dump/tools/romdis.py stores

---

## 11. Open items ([U])

1. The actual value of the two pre-arm gates in 0xC704C on our boot: the word at
   `(SVC stack & ~0xFFF) + 4` (ROM SVC stack page base 0x1BE000 for core 0) and the CPSR I
   bit at call time. Static analysis says they pass; a correct GIC readout (section 7.2) or
   a device-side trace would settle it.
2. The real GIC/timer state: requires the diagnostic viewport (section 7.2).
3. What pre-arms the FIQ slot CA 0x17D42C before the boot task's BUG-stub stamp: not in the
   image, not in the readable ROM. [U] - but it cannot be the scheduler kick path (the
   scheduler is reached by direct call and FIQ is never unmasked).
4. The exact DR/HCC receive contract the host must satisfy to decode the g4 id-1/id-2
   frames once the notify bit6 arrives: host-side lane (hcc-service DR receive path).
5. Whether a second dispatch of the boot state machine ever happens, or whether all phases
   (including the id-2 builder at 0x8FD6) run inside the single pass-1 invocation: the code
   has no static re-entry hook; a device-side trace of CA 0x118B60 transitions would answer.

---

*Lane: wifi-fw-postboot, 2026-11-23, shared task task-88. Host-only static reversal. All
contents are derived addresses, constants, decoded instructions and structure; the device ROM
dump and vendor firmware remain local evidence only and nothing vendor is committed.*
