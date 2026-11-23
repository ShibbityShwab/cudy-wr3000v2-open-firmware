# Hi5622V100 firmware boot flow & ROM/dispatcher contract — reconstructed from the image alone

Host-only lane `wifi-rom-flow`, 2026-11-23. Inputs: the local firmware blob
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B, **local
evidence only — never copied or committed**) plus the prior lanes' published results (cited, not
repeated). No device contact, no vendor bytes in this document, no builds.

Evidence markers: **[V]** re-verified here from the image with the committed tool
`tools/fwdis.py` (objdump-able commands in §10); **[V\*]** device-measured in an earlier lane and
cited; **[I]** inference from code structure; **[U]** unknown/not decidable from the image.

---

## 0. TL;DR — answers to the four questions

1. **Image start / external entry contract.** The first `0x64` bytes of the image are a **header
   consumed by the ROM, not by image code**: `+0x00` = `0x00046971` (Thumb entry A,
   `CPU 0x46970` system/HW init), `+0x04` = `0x000C742D` (Thumb entry B, `CPU 0xC742C` OS/task
   bootstrap), `+0x08..+0x4C` zero, `+0x50..+0x60` = the **five ARM exception handlers**
   (reset/undef/svc/prefetch/abort). Neither entry pointer is referenced anywhere inside the image
   [V: 1 occurrence each, both in the header]. ROM reads them; the host cannot see ROM.
2. **Vector/exception stubs and r1 contract.** The five handlers are ARM stubs at
   `CPU 0x103090..0x103140`; each builds a 17-word frame, then jumps through an ARM–Thumb veneer at
   `CPU 0x103B40..0x103B70` into a Thumb thunk that sets `r1` and enters the common routine
   `CPU 0xC6E14`: **r1=1 data abort, r1=2 prefetch, r1=3 svc, r1=4 undef** [V, exact sites in §2].
   The mask at `CA 0x40008` is `1<<r1` — an **exception-seen bitmap, not a stage table** [V].
3. **Boot state machine.** The boot task `CPU 0x47628` is a **one-shot first-boot task**: it
   initialises the boot global `CA 0x117C00`, populates the destination registry `CA 0x171B6C`,
   sends the id-1 message, **disarms its own reset-dispatch slot `CA 0x17D42C` to a BUG stub
   `0x000C19D1`**, and advances the state byte `CA 0x118B60` from 0 to 1. The image has **no second
   writer of that state byte and no writer of values 2..N** [V] — there is no image-reachable
   "next stage" of the boot task. The 0x47628 address is **never materialised in image code**
   (no literal/movw/movt/ADR/BL) [V], so its initial dispatch must be armed from **outside the
   image (ROM)** [I].
4. **Most plausible missing stage for the data abort.** The writer `CPU 0x87D64` derives its store
   address from `[obj+0x2CC]`; the per-object/per-core value written there `CA 0x846B8` and
   `CA 0xFEAF6` comes from a **per-core bring-up loop in `CPU 0x651xx`** that we have never seen run
   [V code, U observation]. If `[obj+0x2CC]` is 0 the writer targets `CPU 0x2B8/0x3CC+4i`, i.e.
   **ROM space below the image base (<0x40000) → write to ROM → data abort** [I, consistent with
   every measured frame value]. That per-core record-configure stage (itself host/H2D-driven, see
   §8) is the missing external stage; more host pokes cannot replace it.

---

## 1. Address conventions and image layout (image-only facts)

Established/confirmed conventions (used consistently below):

| quantity | rule | evidence |
| --- | --- | --- |
| CPU address of image byte f | `CPU = file + 0x40000` | header table values match stub bytes; e.g. header `+0x50` = `0x00103140` and the stub code is at file `0xC3140` [V] |
| host BAR0 offset (CPU RAM) | `CPU + 0x6B8000` | prior lane §12.1/§17, cross-checked by the tool [V\*] |
| host BAR0 offset (IO block) | `0x3B8000 + (CA - 0x40000000)` | prior lane C1f/C1g [V\*] |
| host upload address | `0x1240000 + file` | vendor `firmware_download` CA base [V\*] |
| image span | CPU `0x40000..0x122C98`; RAM used up to ~0x1C0000 | stack `0x1BCE54`, heap objects `0x117C00..0x17DFxx` [V/V\*] |

### 1.1 Image header — file `0x0000..0x0064` (CPU `0x40000..0x40064`) [V]

```
+0x00  0x00046971   Thumb entry A = 0x46970 (system / hardware init, §3.1)
+0x04  0x000C742D   Thumb entry B = 0xC742C (OS / task bootstrap, §3.2)
+0x08 .. +0x4C      all zero (reserved for the loader)
+0x50  0x00103140   handler[0] reset / init        (ARM stub, calls reset dispatcher)
+0x54  0x00103114   handler[1] undefined instruction
+0x58  0x001030E8   handler[2] supervisor call (SVC)
+0x5C  0x001030BC   handler[3] prefetch abort
+0x60  0x00103090   handler[4] data abort
+0x64  ARM code      second-level exception-return / event-dispatch trampoline (§2.3)
```

The **word at CPU 0x40050 (file 0x50) is what the ROM trampoline reads**: the ROM identity-window
reads from the previous lane show ROM code at CPU `0x50` =
`E59F018C E5900000 E12FFF10 E52D0004` = `ldr r0,[pc,#0x18C]` → `ldr r0,[r0]` → `bx r0` →
`str r1,[sp,#-4]!` [V\*]. I.e. ROM loads a **table pointer from CPU 0x1E4**, loads the word and
**branches (no link)** into it — this is the mechanism that reaches the image's handlers in ARM state
with the caller's LR preserved. Whether the word it pushes (`str r1,[sp,#-4]!`) is a return
address, a vector number or a context word is **not decidable from the image** ([U]; the image stub
only consumes it with `pop {r0}` into frame word 0, which read 0 in the one observed frame
[V\*]).

The 5-word table order is ARM exception order (reset, undef, svc, prefetch, abort); this is
confirmed by the r1 values the veneeers/thunks set (§2.2) and by the observed mask bit semantics
[V + V\*: mask 0x08 = svc, 0x0A adds abort].

### 1.2 Where the two header entries are consumed

Both entry pointers occur **exactly once in the file — at header offsets 0 and 4** [V:
`fwdis.py findval 0x00046971 0x000C742D`]. They are therefore consumed by the ROM/loader, not by
image code. This is the cleanest evidence that the **image expects at least two init stages to be
invoked from outside** (§3).

---

## 2. The vector / exception contract (fully decoded from the image)

### 2.1 ARM stubs at CPU 0x103090..0x103140 (file 0xC3090..0xC3150) [V]

All four exception stubs share a 0x2C-byte ARM prologue. r0=1 abort (file 0xC3090):

```
pop  {r0}                 ; consume the word ROM pushed at entry
sub  sp, sp, #0x44
stm  sp, {r0-r12}         ; frame words 0..12
mov  r0, sp               ; r0 = frame pointer for the Thumb handler
mrs  r6, spsr
str  lr, [r0, #0x3c]      ; word15 = lr (hardware LR at exception entry)
str  r6, [r0, #0x40]      ; word16 = spsr
cps  #0x13                ; SVC mode
str  sp, [r0, #0x34]      ; word13 = SVC sp
str  lr, [r0, #0x38]      ; word14 = lr again
b    <veneer>
```

The reset handler (file 0xC3140) differs: `pop {r0}; sub lr,lr,#4; srsdb sp!,#0x13; cps #0x13;
push {r0-r12}; …` and calls the **reset dispatcher** (§2.4) instead of the Thumb handler.

### 2.2 ARM↔Thumb veneers and r1 thunks (file 0xC3B40..0xC3B70) [V]

| veneer (ARM, file) | CPU | loads Thumb pointer | thunk (CPU) | sets r1 | vector |
| --- | --- | --- | --- | --- | --- |
| 0xC3B48 | 0x103B48 | 0x000C6FEB | 0xC6FEA | **1** | data abort |
| 0xC3B40 | 0x103B40 | 0x000C6FE1 | 0xC6FE0 | **2** | prefetch abort |
| 0xC3B70 | 0x103B70 | 0x000C6FD7 | 0xC6FD6 | **3** | SVC |
| 0xC3B68 | 0x103B68 | 0x000C6FC9 | 0xC6FC8 | **4** | undefined |
| 0xC3B50 | 0x103B50 | 0x000C26B9 | 0xC26B8 | – | reset post-path helper |

Veneers are `ldr pc,[pc,#-4]` followed by the Thumb address [V, hexdump at file 0xC3B40].
Each thunk adjusts the saved return word at `[r0+0x3c]` (`subs #8` for undef, `subs #4` for
abort/prefetch/svc) and branches to the common handler **0xC6E14** with r1 = vector id [V:
file 0x86FC8..0x86FF2].

### 2.3 Common handler CPU 0xC6E14 (file 0x86E14) — executed per exception [V]

Literal pool at CPU 0xC6F88..0xC6FC4 (file 0x86F88):

| literal | value | use |
| --- | --- | --- |
| 0xC6F88 | 0x0017D398 | system object base r5 (flags at r5+0xBE0 = 0x17DF78) |
| 0xC6F8C | 0x00105FDC | per-core table; [core*8+4] passed to 0xC6DB8 |
| 0xC6F90 | 0x40101250 | bit5 cleared |
| 0xC6F94 | 0x40161F00 | per-core word: core0 writes 0x20002; core1 writes 0x10002; others 0x10002 then 0x20002 (two guarded stores at 0xC6E46-0xC6E6E) |
| 0xC6F98 | 0x00010002 | (value for the above) |
| 0xC6F9C | 0x4003022C | word cleared |
| 0xC6FA0 | 0x4000010C | **gate register**: halfword cleared, then spin until 0xcece |
| 0xC6FA4 | 0x00040008 | **exception-seen mask** halfword, OR `1<<r1` |
| 0xC6FA8 | 0x0010F294 | crash-dump descriptor table (3 × {src,len,cursor}) |
| 0xC6FAC | 0x010DB800 | crash-dump destination (trace RAM window) |
| 0xC6FB0 | 0x40000558 | halfword bit0 cleared |
| 0xC6FB4/8 | 0x40039000 / 0x40039800 | ETE/mbox helper blocks (0x407C0/0x4078E) |
| 0xC6FBC | 0x40039014 | out[1] ← 4 (announce) |
| 0xC6FC0 | 0x0017DF44 | system lock object |
| 0xC6FC4 | 0x010DFFFF | crash-dump window end |

Sequence: lock 0x17DF44 → per-core init 0xC6DB8 → clear CA 0x40101250 bit5 → flag bits 1/2/4/8 into
`CA 0x17DF78` → zero the gate `CA 0x4000010C` and release `CA 0x40000108` (halfwords) →
**stage the 17-word frame at CA 0x4000C..0x4004C and OR the mask at CA 0x40008** → flush caches
(0xC2EE4 over 0x40000–0x1C0000 and 0x1000000–0x1060000) → **copy the crash dump** from the table
`0x10F294` (RAM 0x1BD000/0x2000; CA 0x4003A000/0xC28; CA 0x40039000/0xD5C) to `0x10DB800` →
pulse ETE blocks + clear CA 0x40000558 bit0 → **out[1] (CA 0x40039014) ← 4**, doorbell
`CA 0x40101434 ← 1` → **spin until CA 0x4000010C == 0xcece** → unlock, tail-call 0xFDF72.

This is the only firmware code that writes the mask and the frame block, i.e. the whole
`CA 0x40008` "exception frame" is a **host-visible crash record**, not an init stage table [V].
The adjacent crash-dump descriptor table at CPU `0x10F294` (= file 0xCF294) is consumed here [V];
which also anchors the "module table" question (§7).

### 2.4 Reset handler CPU 0x103140 (file 0xC3140) — header[0] [V]

```
pop {r0}; sub lr,lr,#4; srsdb sp!,#0x13; cps #0x13; push {r0-r12}
page = sp & ~0xFFF ; counter = [page+4]; [page+4] = counter+1
blx 0xC2EEC            ; reset dispatcher: r3 = [0x17D398+0x94]; if (r3) bx r3
[page+4] = counter ; if (counter==0 && [page]==0) bl 0xC3B50 -> Thumb 0xC26B8
pop; rfeia sp!         ; return to the interrupted context
```

**This is the only in-image path into the reset dispatcher** (prior lane's negative BL scan
confirmed) and it is only reachable from the ROM (header[0]) [V/V\*]. The dispatcher itself [V]:

```
0xC2EEC: ldr r3,=0x17D398 ; ldr r3,[r3,#0x94] ; cbz r3,ret ; bx r3
```

The slot `CA 0x17D42C` (`0x17D398+0x94`) is the **external boot kick**: on a cold boot it must
already contain `0x00047629` (Thumb boot task) for the observed boot-task execution [V\*: task ran].
But `0x47628/0x47629` is **never materialised by image code** — not as a literal, movw/movt pair,
ADR, or BL target [V: findval + movimm + ADR + BL scans]. The image therefore relies on the **ROM to
arm that slot** (or on a ROM-side vector we cannot see) [I]; this is the one external stage that is
both mandatory and invisible from the image.

The reset handler's post-path `0x103B50 → Thumb 0xC26B8` is a deferred-dispatch loop
(`cpsie i`/`cpsid i`, calls 0xC2382/0xC24DE/0xC196C, checks the per-page word) — the "run pending
system work before returning" path [V].

Boot init also ends with the interrupt/event dispatcher at **CPU 0xC2EFC** [V]: it reads a status
register from the `CA 0x40105000` block, takes the low 10 bits as an index (≤0x5F), looks up a
handler in the table at **`0x17D398+0x98 = 0x17D430`**, and `blx`es it. Entry A initialises that
table with `[0]=0xC2081, [1]=0xC20A3, [0x1D]=0xC6C89` (and more) [V, §3.1]. So the image also
expects **hardware event delivery** through the `CA 0x40105000` block (external/ROM-owned interrupt
controller) — one more outside-in stage.

---

## 3. The two header entries: what the ROM should invoke

### 3.1 Entry A — CPU 0x46970 (file 0x6970), header +0x00 [V code, [U] observation]

A long (~3 KB) Thumb function that is the **system/hardware initialiser**. Highlights:

- Touches the SoC control block `CA 0x40105000` (literal pool 0x47134/0x47138/0x4713C/0x47144 =
  `0x4010502C/0x40105024/0x40105030/0x40105028`) and the `CA 0x40161000/0x40161400/0x40161800/
  0x40161700/0x40161C00` blocks; sets bits in `CA 0x40105000+0` (0x20/0x40/0x800/0x800000).
- **Initialises the system/dispatcher object `CA 0x17D398`**: `[0x17D398+0] = (core_id+1)<<5`
  (from `CA 0x40161004` & 0x1F), handler table `0x17D430 = 0x17D398+0x98`:
  `[0]=0x000C2081, [1]=0x000C20A3, [0x1D]=0x000C6C89` (0xC2080, 0xC20A2, 0xC6C88 Thumb) at
  0x46E70-0x46E92; also `CA 0x17DF10` and `CA 0x40161C00` sites.
- Calls `0xBBDEC`, `0x40D60`, `0x40EE8`, `0x4041C`, `0xC3024` (interrupt table init:
  fills `-1` over a table), `0xC2020`, and `blx 0xC2E68` etc.

Expected observable state: system object `CA 0x17D398` populated, core-count word `(core+1)<<5`,
handler table `0x17D430` non-zero, SoC glue bits set. **Status: [U]** — it cannot be distinguished
from ROM-only init in our current read set; a host read of `0x17D398`/`0x17D430` would confirm it
(corrected BAR0 = CPU+0x6B8000).

### 3.2 Entry B — CPU 0xC742C (file 0x8742C), header +0x04 [V code, [U] observation]

Short Thumb function: `orr [0x17D398-relative reg], #0x10` then
`bl 0x82082; bl 0x82020; bl 0x83024; bl 0x82098; movs r0,#3; bl 0x7edd8; bl 0x873a0; pop;
b 0x8704C` [V].

Reading: the `0x83024/0x82020/0x82082` calls are the interrupt/thread setup layer, `0x873a0` a
task/thread creation, and the tail `b 0x8704C` is the **thread-loop continuation** already known
from the D2H publish path (prior lane: tail 0x85128 / loop body 0xC71D6 family). So entry B is the
**OS/task bootstrap**: it starts the worker threads that later run the message/D2H machinery.
**Status: [U]**; if the ROM never calls it (or calls it after an ACP/host gate we do not reproduce),
the thread layer that should initialise late driver objects never starts — this is candidate #2 in
§8.

---

## 4. Stage table (the deliverable)

`obs` = observed on the chip in prior lanes (V\*) / not observed (—). "outside" = invocable only
from ROM/hardware/host, not from image code (established by negative in-image reference scans).

| id | stage | owner | entry point (CPU) | trigger / condition | CAs / objects touched | expected observable | obs |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S0 | ROM load + vector table | ROM (<0x40000) | ROM 0x0/0x20/0x50 | power-on, image validation | ROM RAM, table ptr at ROM 0x1E4 | image at CPU 0x40000; ROM branches to image handlers | indirect V\* |
| S1 | header entries A/B dispatch | ROM | header +0x00 / +0x04 | after load | — | PC enters 0x46970 / 0xC742C | U |
| S2 | system init | entry A | 0x46970 (header+0) | ROM call | CA 0x40105000, 0x40161000/0x40161C00, 0x17D398 (+0x98 table), 0x17DF10 | 0x17D398+0=(core+1)<<5; 0x17D430 non-zero | U |
| S3 | OS/task bootstrap | entry B | 0xC742C (header+4) | ROM call | 0x17D398-relative, 0x83024/0x82020/0x873a0/0x8704C | worker threads/timers running; DR fills advance | U |
| S4 | reset handler | ROM → image | 0x103140 (header[0]) | reset vector (0) path | page counters, dispatcher 0xC2EEC | — | — |
| S5 | reset dispatch slot | ROM arms → image runs | slot CA 0x17D42C = 0x47629 | slot pre-armed by ROM [I] | 0x17D398, 0x17D42C | slot becomes BUG stub 0x000C19D1; boot task ran | V\* |
| S6 | boot task first pass | image (one-shot) | 0x47628 | S5 | CA 0x117C00/0x118AEC/0x118B60, CA 0x171B6C registry, CA 0x17D42C, msg layer g=0x172130 | state 0→1; registry populated; g valid; slot disarmed | V\* |
| S7 | destination registry populate | inside S6 | 0x47ADC.. | callback at 0x4782A non-zero | CA 0x171B6C, 0x490 B; 24-B records; config table 0x2C stride | core-0 record {0,0x103FB8,0x171B70,0x171E1C,…} | V\* |
| S8 | id-1 message send | inside S6 | 0x47D28 → send 0x102760 | state path | g 0x10C0F4, HCC node | id-1 accepted (no long wait) | V\* (accepted; publish deferred) |
| S9 | D2H announce / publish | image threads | d2h_notify 0xC6170, flush 0xC6108 | g+0xB4 armed==0 and out[1]==0 | g+0xB0/B4/B8, CA 0x40101418, out[1] 0x40039014, doorbell 0x40101434 | out[1] bit6 0x40 published; pending clears | V\* (achieved only after host forced armed=0) |
| S10 | per-core record bring-up | image (0x651xx loop) | fn 0x84664; callers 0x651E6/0x65268 | per-core loop (4 cores), log ids 0xb31/0xb3a/0xb42 | record = [r4+0x190]+0x1C*core; **+0x2CC descriptor base**; +0x2B8/+0x3CC arrays | per-core +0x2CC valid (descriptor array base) | U |
| S11 | descriptor fill (D2H/ETE) | image | 0x88AFC → 0x87D64 | from 0xDA8C8 / 0xE9B18 (PHY paths) | obj+0x2CC, obj+0x2B8+4i, obj+0x3CC+4i, 0x200-B stack buffer | DR/ETE descriptor arrays written | — (aborts) |
| S12 | exception service | ROM → image | 0x103090/BC/E8/114 → 0xC6E14 | exception taken | CA 0x40008 mask/frame, 0x4000010C gate, 0x40039014, 0x40101434, 0x10DB800 dump | mask bit; frame staged; announce; gate spin | V\* (svc 0x08, abort 0x0A) |
| S13 | event/interrupt dispatch | hardware/ROM | 0xC2EFC; table 0x17D430 | CA 0x40105000 status register | CA 0x4010502x, 0x17D398+0x98 table | handler blx; firmware advances | U |

**Observed vs not:** the only *outside-in* stages we have positive evidence for are S5/S6 (boot task
ran once) and S12 (svc/abort handler ran). S1/S2/S3 and S13 have no observation; S10 is required by
the failing S11 but has never been observed. This re-frames the problem: the "missing stage" is not
another exception-vector poke (those are complete) and not another boot-task pass (unreachable by
construction), but a **driver-object bring-up stage** that the image expects from its own late/thread
layer (S10), which in turn depends on S3/the host's HCC side.

---

## 5. Boot state machine (boot task CPU 0x47628) — precise scope

- Only **two** accesses to the state byte exist in the whole image [V, pattern scan for
  `[r4,#0xF60]` with r4 = CA 0x117C00]: `ldrb r6,[r4,#0xf60]` at CPU 0x47D10 and
  `strb r3,[r4,#0xf60]` at CPU 0x48174 (the `state+1` in the setup branch, file 0x8174).
  **No writer of states 2..N exists** [V]. The `cmp #1; bls` makes states 0/1 take the setup
  branch; states >1 would take the "normal" path at 0x47D1C (unlock + id-1 send), but no image code
  can ever produce state >1.
- Prelude (0x47628..0x47D0A) [V]: clears flags at `CA 0x117C00+0x50/0x51`; writes a CA (from
  literal pool) ← 1; initialises list heads at `0x118B58/0x118B5C`; memsets big blocks
  (`0x2A80`-sized) via 0xC1E0C; walks board-config tables using helpers `0x403E8 / 0x40800 /
  0x40858`; **populates the destination registry** (memset `CA 0x171B6C`, 0x490 bytes; walks the
  0x2C-stride configuration table and fills 24-byte records) [V\*]; builds HCC callback/thread
  objects at +0x214..+0x250 and queue heads +0x270..+0x27C; sends the id-1 message; **disarms
  `CA 0x17D42C ← 0x000C19D1`** (literals at CPU 0x47DEC/0x47DF0, file 0x7DEC/0x7DF0) [V];
  then locks, reads the state and takes the state-0/1 setup branch (0x48160).
- Setup branch 0x48160: `state = state+1`; builds thread/callback objects; falls into the message
  path 0x47D28 (frame header byte0=4, byte6=1, send 0x102760) [V]. Cold-boot observation
  [V\*]: state=1, registry populated, g=0x0010C0F4 valid, slot=0xC19D1, mask=0x08.

**Consequence (negative result):** a second dispatch of the boot task is impossible: its only
caller channel is the reset-dispatch slot it disarms itself with a BUG stub, and no in-image code
materialises its address. Do not attempt further "re-kick" variants (prior C1d already showed a
running CPU ignores the release re-kick) [V\*].

---

## 6. System object 0x17D398 and the dispatcher contract (builds on boot-kick §12/§16)

- `+0x000`: `(core_id+1)<<5` written by entry A from `CA 0x40161004` & 0x1F [V].
- `+0x094`: reset-dispatch slot `CA 0x17D42C` — loaded and `bx`ed by 0xC2EEC [V]; reset handler
  calls 0xC2EEC [V]; slot written only by the boot task (disarm) in image code [V]; must be
  armed by ROM [I].
- `+0x098`: 0x60-entry handler table `CA 0x17D430`, indexed by the low 10 bits of a
  `CA 0x40105000` status register in the event dispatcher 0xC2EFC [V]; initialised by entry A.
- `+0xBAC`: lock object `CA 0x17DF44` used by the common exception handler [V].
- `+0xBE0`: flag word `CA 0x17DF78`; bits 1/2/4/8 set inside the common handler [V]; observed
  0x0F after the handler completed [V\*].

Host-readable mapping: `BAR0 = CPU + 0x6B8000` → object 0x17D398 = **BAR0 0x835398**, slot
0x17D42C = **BAR0 0x83542C** (matches the prior lane's slot read), lock 0x17DF44 = 0x835F44, flags
0x17DF78 = 0x835F78, g 0x172130 = 0x82A130. The mask/gate exceptions are IO-block CAs:
0x40008 = 0x6F8008, 0x4000010C = 0x3B810C. Use `fwdis.py bar0 <cpu...>` to avoid arithmetic slips.

---

## 7. The "module table" at CPU 0x10F254 (file 0xCF254)

Raw layout [V, `fwdis.py w 0x10F240 48`]:

```
CPU 0x10F254: 0x00049335 | 0x000402AD | 0 | 0 | 0 |
CPU 0x10F268: 0x000C6365 0x000C18F5 0x000C6171 0x000C187B 0x00040291 0x000C71D7
              0x0004028D 0x00040289 0x00040281 0x00040285             (10 pointers)
CPU 0x10F290: 0
CPU 0x10F294: 3 × {src, len, cursor}: {0x001BD000,0x2000}, {0x4003A000,0xC28},
              {0x40039000,0xD5C}                                  (crash-dump table)
CPU 0x10F2B8: 0x01060440 (window word) …
```

Interpretation [I]: this is a **pointer/registration table with weak default stubs**:
`0x40281/0x40285/0x40289/0x4028D` are the image's trivial `movs r0,#0; bx lr` stubs (file 0x280..
0x2&nbsp;region, [V]) and `0x40291/0x402AD` are loader-thunks; the non-stub entries are real
message/thread-layer functions: `0xC6171` = d2h_notify (file 0x86170), `0xC71D7` = thread-loop
body (file 0x871D6), plus `0xC6365/0xC18F5/0xC187B`. `0x49334` (file 0x9334) is a list/module
initialiser [V]. The table's consumer is **not** found as a literal/ADR reference from image code
[V negative], so it is either (a) built for/looked up by the ROM, or (b) read through a base+index
in a module-framework loop [U]. What *is* certain: the crash-dump descriptor table immediately after
it (0x10F294) **is** consumed by the exception handler (§2.3) [V].

Practical note for the Lead: if a prior note called this "the module table", the safest statement is
"registration table with weak stubs + pre-registered message-layer entries; consumer unproven; the
adjacent 0x10F294 table is the exception crash-dump list".

---

## 8. Most plausible missing stage leading to the data abort (ranked)

Facts to explain [V/V\*]: mask 0x0A (svc + data abort handlers ran), frame lr = 0x88BBD (return
site after `bl 0x87D64` at 0x88BB8), saved frame registers are the PHY/PCIe table pairs
(0x40114564, 0x40114568, 0x40114588 with values 3/3/0x33) and RAM pointers (0x1BC000, 0x1BCC48,
sp 0x1BCE54); the aborting routine 0x87D64 is a **pure data writer**, no device IO [V]:
`r5 = [obj+0x2CC]`, then `[r5 + 0x3CC + 4i] |= buf[i]` and `[r5 + 0x2B8 + 4i]` [V, file 0x47D64].

**Candidate 1 (best): uninitialised per-core descriptor base `[obj+0x2CC] = 0` (stage S10 missing).**
If `[obj+0x2CC]` is 0, the writes land at `CPU 0x2B8/0x3CC + 4i` — **inside ROM (<0x40000), not
the image** — and a store to ROM raises exactly a data abort; the handler then skips the faulting
instruction and resumes, matching the observed mask 0x0A and the unchanged DR fills. The only
writers of `obj+0x2CC` in the image are: `0x5C26C` (driver-object init with a static literal),
`0x846B8` (per-core record: `[ [r4+0x190] + 0x1C*core + 0x2CC ] = r5`) and `0xFEAF6` (loop over 4
per-core records) [V]. The per-core writer is reached **only** from the per-core bring-up loop at
`CPU 0x651xx` (`0x651E6` / `0x65268`, calls `0x42C28` logging ids 0xb31/0xb3a/0xb42 and
`0x836FC`), which loops over 4 cores [V]. That loop is the **per-core configure/bring-up stage**
(normally reached after the HCC/message layer has a host peer), and it is exactly the stage we have
never observed running. Its absence leaves the descriptor base null and the next descriptor fill
aborts. Missing stage **S10** = "per-core HCC/DR record configuration (0x65178 loop → 0x84664)".

**Candidate 2: entry B (header +0x04, 0xC742C) never invoked** — then the thread/timer bootstrap
(`0x83024/0x873a0/0x8704C`) that later initialises driver objects in S10 would not run either; S10
sits downstream of it [I]. A ROM-side call order of entry A → entry B → handler[0] would produce
exactly the observed one-shot boot state; whether entry B ran is [U] (host-visible check: worker
thread objects, DR fills — but they are currently 0, weakly suggesting entry B did **not** run or
started threads that then parked).

**Candidate 3: host H2D HCC protocol never engaged** — the per-core bring-up and the D2H publish
both depend on a host peer (plat-ready/ready handshake, wifi-hcc-re §3.1). This is the same
conclusion as the previous lane ("full vendor driver replica or H2D frame"), now with a concrete
firmware entry point for the missing stage (0x65178/0x84664) [I].

**Not the cause (rule out with evidence):** another exception-vector poke (all five entries are
mapped and the two that can occur already ran; entries 2/4 only on prefetch/undef) [V]; another
boot-task pass (unreachable, §5) [V]; a ROM-dispatcher re-kick (CPU ignores release re-kick per
C1d) [V\*]; the PHY/PCIe init table (fully applied per boot-kick §21) [V\*]; ACP gate
(0x06210321 correct) [V\*]; a host-writable gate for the 0x4011 block (host writes stick) [V\*].

---

## 9. Negative results / do-not-chase list

- Mask `CA 0x40008` is an exception-seen bitmap (`1<<r1`), not a stage dispatch table [V].
- `0x47628` is never materialised in image code; no second call path exists [V].
- State byte `CA 0x118B60` is written only once (state+1 at 0x48174); no writer of 2..N [V]. The
  "boot state machine" is a one-shot boot flag; do not hunt for a missing dispatch.
- The 17-word block at `CA 0x40008..0x4004C` is the host-visible crash frame staged by the
  exception handler; it is not an init table [V].
- The frame's lr/lr-4 pair (0x88BBD/0x88BB9) is the return address after the `bl 0x87D64` call site,
  not the faulting PC: these stubs save `lr` twice and the thunk decrements one copy [V; the
  boot-kick §20.1 reading is confirmed by the thunk code].
- ROM at CPU 0x37000+ is not host-readable through either window (prior lane) — the post-0x37172
  ROM flow cannot be decoded from the chip; only the image contract (§2) is recoverable.

---

## 10. Reproduction (host-only, local image; nothing vendor committed)

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python   # capstone 5.0.7
T=opensource/docs/incoming/wifi-rom-flow/tools/fwdis.py

$PY $T info                          # image size, first header words
$PY $T dis 0x46970 0x2E0 thumb       # entry A (header +0x00) system init
$PY $T dis 0xC742C 0x40  thumb       # entry B (header +0x04) OS bootstrap
$PY $T dis 0x103090 0x100 arm        # 4 exception stubs (r1 = 1..4 thunk path)
$PY $T dis 0x103B40 0x40  arm        # ARM->Thumb veneers (ldr pc,[pc,#-4])
$PY $T dis 0xC6E14 0x19C thumb       # common exception handler (mask/frame/announce/gate)
$PY $T dis 0xC6FC8 0x2C  thumb       # r1=4 / r1=3 thunks
$PY $T dis 0x47628 0x1C0 thumb       # boot task prelude
$PY $T dis 0x47D00 0x70  thumb       # slot disarm (0x17D42C <- 0xC19D1) + state check
$PY $T dis 0x48160 0x140 thumb       # setup branch (state+1), thread objects
$PY $T dis 0x87D40 0x80  thumb       # aborting descriptor writer 0x87D64
$PY $T dis 0x88A80 0x1A0 thumb       # its caller 0x88AFC (descr. builder, 0x200-B buffer)
$PY $T dis 0x65170 0x100 thumb       # per-core bring-up loop -> 0x84664 (missing stage)
$PY $T dis 0x84680 0x60  thumb       # per-core writer of obj+0x2CC
$PY $T w   0x10F240 48               # registration table + crash-dump descriptor table
$PY $T findval 0x00046971 0x000C742D 0x00047629 0x0017D42C 0x0010F254
$PY $T ctx  0x0017D398 thumb         # literal-load xrefs for the system object
```

The tool reads the blob **in place** (`FW_PATH`) and only prints derived facts; the blob is not
copied into the repository.

---

## 11. Open questions ([U], for the next host-only or bench lane)

1. Did the ROM call **entry A (0x46970)** / **entry B (0xC742C)** before handler[0]? Host-visible
   test: read `CA 0x17D398` (+0, +0x98 table) and `CA 0x117C00+0xF60`/thread objects (BAR0 =
   CPU+0x6B8000) on a fresh cold boot; non-zero 0x17D398+0x98 entries imply entry A ran.
2. Who arms `CA 0x17D42C` with 0x47629? Not image code [V]. If ROM cannot be dumped, look for a
   ROM-side table pointer at CPU 0x1E4 (the trampoline literal) — the identity window read a value
   for CPU 0x50 but not the target; a read of CPU 0x1E4 (BAR0 0x1E4 through the identity window)
   might reveal the table the ROM uses (it is the same table at 0x40050 for exceptions; the reset
   entry may come from elsewhere).
3. Is the registration table at 0x10F254 indexed by a module id from the ROM or written at runtime?
   (No image reference found.)
4. The per-core bring-up loop 0x65178: what event starts it (H2D frame id? timer? plat-ready)? It
   logs ids 0xb31/0xb3a/0xb42 through 0x42C28; the same ids may appear in vendor driver messages
   (wifi-hcc-re §3.1) — cross-check there.

---

*Lane: wifi-rom-flow, 2026-11-23. All artifacts image-derived; vendor blob and device values are
local evidence only. Complements (does not repeat) wifi-boot-kick §12/§16-§21, wifi-fw-ready-gate
§13-§14 and wifi-hcc-re §3.1.*
